#include "weather_client.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

// Tag prepended to all Serial log lines from this module
static const char *TAG = "weather";


// ------------------------------------------------------------
//  locationQuery — build the URL fragment that specifies location
//
//  OWM supports two ways to identify a location:
//    • City name:  &q=New+York   (spaces replaced with '+')
//    • GPS coords: &lat=40.7&lon=-74.0  (already formatted correctly)
//
//  We detect the GPS form by checking whether the string starts
//  with "lat="; if so we just prepend "&" and append as-is.
//  Otherwise we URL-encode spaces and prepend "&q=".
// ------------------------------------------------------------
static String locationQuery(const char *cityOrCoords)
{
    if (strncmp(cityOrCoords, "lat=", 4) == 0) {
        // GPS coordinates already in the correct query-string format
        return String("&") + cityOrCoords;
    }
    // City name — replace spaces with '+' for URL encoding
    String city = String(cityOrCoords);
    city.replace(" ", "+");
    return String("&q=") + city;
}


// ============================================================
//  weatherFetch — GET /data/2.5/weather (current conditions)
//
//  Example URL:
//    https://api.openweathermap.org/data/2.5/weather
//      ?units=imperial&appid=<KEY>&q=London
//
//  The response is a single JSON object; we extract only the
//  six fields we display and discard the rest.  ArduinoJson's
//  JsonDocument automatically manages heap allocation.
// ============================================================
bool weatherFetch(const char *apiKey, const char *cityOrCoords, WeatherData &out)
{
    out.valid = false;   // assume failure until we successfully parse

    // Build the full request URL
    String url = "https://api.openweathermap.org/data/2.5/weather?units=imperial&appid=";
    url += apiKey;
    url += locationQuery(cityOrCoords);

    HTTPClient http;
    http.begin(url);
    int code = http.GET();
    if (code != 200) {
        Serial.printf("[%s] current: HTTP %d\n", TAG, code);
        out.errorMsg = "HTTP " + String(code);   // e.g. "HTTP 401"
        http.end();
        return false;
    }
    String payload = http.getString();   // read entire response into heap
    http.end();

    // Deserialise — JsonDocument sizes itself dynamically
    JsonDocument doc;
    if (deserializeJson(doc, payload)) {
        out.errorMsg = "JSON parse error";
        return false;
    }

    // Extract the fields we need.  The JSON path mirrors OWM's schema:
    //   name           → city name
    //   weather[0].description → human-readable condition
    //   main.temp      → current temp (°F with units=imperial)
    //   main.feels_like
    //   main.humidity
    //   wind.speed     → m/s regardless of units setting
    out.city        = doc["name"].as<String>();
    out.description = doc["weather"][0]["description"].as<String>();
    out.tempF       = doc["main"]["temp"].as<float>();
    out.feelsLikeF  = doc["main"]["feels_like"].as<float>();
    out.humidityPct = doc["main"]["humidity"].as<int>();
    out.windSpeedMs = doc["wind"]["speed"].as<float>();
    out.valid       = true;
    return true;
}


// ============================================================
//  forecastFetch — GET /data/2.5/forecast (3-hour / 5-day data)
//
//  The OWM forecast endpoint returns 40 JSON objects (5 days ×
//  8 slots/day, at 3-hour intervals).  The full payload is ~20 KB
//  which is tight on the ESP32's ~300 KB free heap.
//
//  Solution: ArduinoJson's *filter* feature.  We build a small
//  JsonDocument that mirrors only the keys we care about, then
//  pass it as DeserializationOption::Filter(filter).  ArduinoJson
//  skips all other fields during parsing, reducing allocations by
//  roughly 80 %.
//
//  The response is stream-parsed directly from the HTTP payload
//  (http.getStream()) rather than buffering the full string first,
//  further reducing peak heap usage.
// ============================================================
bool forecastFetch(const char *apiKey, const char *cityOrCoords, ForecastData &out)
{
    out.valid = false;

    String url = "https://api.openweathermap.org/data/2.5/forecast?units=imperial&appid=";
    url += apiKey;
    url += locationQuery(cityOrCoords);

    // ---- Build the JSON filter ----------------------------------------
    // The filter must mirror the exact structure of the response.
    // Only keys present in the filter (with value `true`) are kept.
    // Everything else is silently discarded by the parser.
    JsonDocument filter;
    JsonArray fList = filter["list"].to<JsonArray>();
    JsonObject fSlot = fList.add<JsonObject>();
    fSlot["dt"]                        = true;   // unix timestamp (for day-of-week)
    fSlot["dt_txt"]                    = true;   // "2026-03-17 15:00:00" (for hour + date)
    fSlot["main"]["temp"]              = true;   // temperature at this slot
    fSlot["main"]["temp_min"]          = true;   // slot min (used for daily low)
    fSlot["main"]["temp_max"]          = true;   // slot max (used for daily high)
    fSlot["weather"][0]["main"]        = true;   // short category ("Rain", "Clouds")
    fSlot["weather"][0]["description"] = true;   // longer description

    HTTPClient http;
    http.begin(url);
    int code = http.GET();
    if (code != 200) {
        Serial.printf("[%s] forecast: HTTP %d\n", TAG, code);
        out.errorMsg = "HTTP " + String(code);   // e.g. "HTTP 401"
        http.end();
        return false;
    }

    // Stream-parse with filter — never buffers the full 20 KB string
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, http.getStream(),
                                               DeserializationOption::Filter(filter));
    http.end();
    if (err) {
        Serial.printf("[%s] forecast JSON: %s\n", TAG, err.c_str());
        out.errorMsg = String("JSON: ") + err.c_str();
        return false;
    }

    JsonArray list = doc["list"].as<JsonArray>();


    // ---- Hourly: first HOURLY_SLOTS entries --------------------------
    // Each entry in `list` is a 3-hour slot.  We just take the first
    // HOURLY_SLOTS (8) to get the next 24 hours.  The hour is parsed
    // from dt_txt which has the format "YYYY-MM-DD HH:MM:SS"; the
    // hour digits start at character offset 11.
    int hi = 0;
    for (JsonObject slot : list) {
        if (hi >= HOURLY_SLOTS) break;
        const char *dtTxt = slot["dt_txt"] | "00:00:00";
        out.hourly[hi].hour      = atoi(dtTxt + 11);   // parse "HH" at offset 11
        out.hourly[hi].tempF     = slot["main"]["temp"].as<float>();
        out.hourly[hi].shortDesc = slot["weather"][0]["main"].as<String>();
        hi++;
    }


    // ---- Daily: next DAILY_SLOTS calendar days (skip today) ----------
    //
    // Strategy:
    //   1. Extract "today's" date string from the very first slot.
    //   2. Iterate all slots; skip any whose date == today.
    //   3. For each future date, find or create a bucket (trackedDates[]).
    //   4. Track running min/max temperature for that bucket.
    //   5. Use the noon (hour == 12) slot's description; fall back to
    //      the first slot's description for days without a noon entry.
    //   6. Copy the populated buckets into out.daily[].

    // Derive today's date from the first slot (format "YYYY-MM-DD")
    const char *firstDtTxt = list[0]["dt_txt"] | "0000-00-00 00:00:00";
    char todayStr[11];
    strncpy(todayStr, firstDtTxt, 10);
    todayStr[10] = '\0';

    // Abbreviated day names indexed by tm_wday (0 = Sunday)
    static const char *DNAMES[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};

    // Per-day accumulator arrays (indices 0..DAILY_SLOTS-1)
    char  trackedDates[DAILY_SLOTS][11] = {};   // "YYYY-MM-DD" for each bucket
    float dailyMin[DAILY_SLOTS]         = { 999,  999,  999};
    float dailyMax[DAILY_SLOTS]         = {-999, -999, -999};
    String dailyDesc[DAILY_SLOTS];
    String dailyDayName[DAILY_SLOTS];
    int   dailyCount = 0;   // number of distinct future dates seen so far

    for (JsonObject slot : list) {
        const char *dtTxt = slot["dt_txt"] | "";
        if (strlen(dtTxt) < 16) continue;   // malformed entry — skip

        // Extract the date portion "YYYY-MM-DD" from dt_txt
        char dateStr[11];
        strncpy(dateStr, dtTxt, 10);
        dateStr[10] = '\0';

        if (strcmp(dateStr, todayStr) == 0) continue;   // skip today's slots

        // Find the bucket for this date, or create a new one
        int di = -1;
        for (int i = 0; i < dailyCount; i++) {
            if (strcmp(trackedDates[i], dateStr) == 0) { di = i; break; }
        }
        if (di == -1) {
            if (dailyCount >= DAILY_SLOTS) continue;   // already have enough days
            di = dailyCount++;
            strncpy(trackedDates[di], dateStr, 10);
            trackedDates[di][10] = '\0';

            // Derive the day name from the unix timestamp using gmtime_r
            // (avoids floating-point date arithmetic)
            time_t t = slot["dt"].as<long>();
            struct tm ti;
            gmtime_r(&t, &ti);
            dailyDayName[di] = DNAMES[ti.tm_wday];
        }

        // Update running min/max for this day
        float temp = slot["main"]["temp"].as<float>();
        if (temp < dailyMin[di]) dailyMin[di] = temp;
        if (temp > dailyMax[di]) dailyMax[di] = temp;

        // Prefer the noon slot's description; accept first available
        int hour = atoi(dtTxt + 11);
        if (dailyDesc[di].isEmpty() || hour == 12) {
            dailyDesc[di] = slot["weather"][0]["description"].as<String>();
        }
    }

    // Copy accumulators into the output struct
    for (int i = 0; i < dailyCount; i++) {
        out.daily[i].dayName = dailyDayName[i];
        out.daily[i].highF   = dailyMax[i];
        out.daily[i].lowF    = dailyMin[i];
        out.daily[i].desc    = dailyDesc[i];
    }

    // Mark as valid only if we got at least some hourly and daily data
    out.valid = (hi > 0 && dailyCount > 0);
    return out.valid;
}
