#pragma once
#include <Arduino.h>

// ============================================================
//  weather_client.h — OpenWeatherMap API data structures and
//                     fetch function declarations
//
//  Two OWM endpoints are used:
//    /data/2.5/weather  — current conditions (single JSON object)
//    /data/2.5/forecast — 5-day / 3-hour forecast (40 time slots)
//
//  Both requests use `units=imperial` so temperatures arrive in
//  °F and wind speed in m/s (OWM's imperial unit for wind).
//
//  Location can be specified two ways:
//    • City name: "London", "New York"  → query param q=London
//    • GPS coords: "lat=51.5&lon=-0.1"  → appended directly to URL
//  The locationQuery() helper in weather_client.cpp handles the
//  two cases transparently.
// ============================================================


// Number of 3-hour slots to pull from the forecast feed.
// 8 slots × 3 h = 24 hours of hourly data shown on screen.
#define HOURLY_SLOTS  8

// Number of full calendar days to show in the daily forecast panel.
// "Today" is excluded; these are the NEXT 3 days.
#define DAILY_SLOTS   3


// ------------------------------------------------------------
//  WeatherData — current conditions from /data/2.5/weather
//
//  Fields:
//    city        — resolved city name returned by OWM (may differ
//                  from what the user typed, e.g. "NYC" → "New York")
//    description — human-readable condition, e.g. "light rain"
//    tempF       — current temperature in °F
//    feelsLikeF  — apparent temperature (wind-chill / heat-index) in °F
//    humidityPct — relative humidity 0–100 %
//    windSpeedMs — wind speed in metres per second
//    valid       — false if the fetch or parse failed; callers should
//                  check this before reading any other field
//    errorMsg    — human-readable error description when valid==false,
//                  e.g. "HTTP 401" or "JSON parse error".  Empty when
//                  valid==true.  Shown on the display in the weather band.
// ------------------------------------------------------------
struct WeatherData {
    String city;
    String description;
    float  tempF;
    float  feelsLikeF;
    int    humidityPct;
    float  windSpeedMs;
    bool   valid;          // set to true only after a successful parse
    String errorMsg;       // populated on failure; empty on success
};


// ------------------------------------------------------------
//  HourlyForecast — one 3-hour time slot
//
//  Fields:
//    hour      — hour-of-day (0, 3, 6, …, 21) extracted from dt_txt
//    tempF     — forecast temperature at this slot in °F
//    shortDesc — short condition category, e.g. "Rain", "Clouds",
//                "Clear" (the "main" field in OWM's weather array)
// ------------------------------------------------------------
struct HourlyForecast {
    int    hour;       // 0–23
    float  tempF;
    String shortDesc;  // e.g. "Rain", "Clouds", "Clear"
};


// ------------------------------------------------------------
//  DailyForecast — aggregated summary for one calendar day
//
//  Built by scanning all 3-hour slots that fall on a given date
//  and picking the min/max temperature plus a representative
//  description (the noon slot is preferred; first slot otherwise).
//
//  Fields:
//    dayName — 3-letter abbreviation: "Mon", "Tue", "Wed", …
//    highF   — highest temperature across all slots for that day (°F)
//    lowF    — lowest temperature across all slots for that day (°F)
//    desc    — representative weather description for the day
// ------------------------------------------------------------
struct DailyForecast {
    String dayName;   // "Mon", "Tue", …
    float  highF;
    float  lowF;
    String desc;      // representative description (prefers noon slot)
};


// ------------------------------------------------------------
//  ForecastData — combined hourly + daily forecast payload
//
//  Populated by forecastFetch().  The `valid` flag is false if
//  the API call or JSON parse failed — callers should check it
//  before reading the arrays.
//
//  errorMsg is populated on failure (e.g. "HTTP 404", "JSON
//  parse error") and shown on the display in the forecast band.
// ------------------------------------------------------------
struct ForecastData {
    HourlyForecast hourly[HOURLY_SLOTS];   // first 8 × 3-hour slots (≈24 h)
    DailyForecast  daily[DAILY_SLOTS];     // next 3 full calendar days
    bool   valid;
    String errorMsg;   // populated on failure; empty on success
};


// ------------------------------------------------------------
//  weatherFetch — fetch current conditions
//
//  Makes a single HTTPS GET to /data/2.5/weather, deserialises
//  the JSON, and fills `out`.
//
//  Parameters:
//    apiKey       — OWM API key string
//    cityOrCoords — city name OR "lat=XX.X&lon=YY.Y"
//    out          — struct to populate (out.valid set on success)
//
//  Returns true on success, false on HTTP error or JSON parse failure.
// ------------------------------------------------------------
bool weatherFetch(const char *apiKey, const char *cityOrCoords, WeatherData &out);


// ------------------------------------------------------------
//  forecastFetch — fetch hourly + daily forecast
//
//  Makes a single HTTPS GET to /data/2.5/forecast (40 slots, 5 days).
//  Uses an ArduinoJson *filter* during deserialisation to discard
//  unwanted fields — this reduces heap usage by ~80 % compared to
//  parsing the full ~20 KB payload.
//
//  Populates:
//    out.hourly[] — first HOURLY_SLOTS slots (3-hour steps, ≈24 h ahead)
//    out.daily[]  — next DAILY_SLOTS calendar days (skips today)
//
//  Returns true on success, false on error.
// ------------------------------------------------------------
bool forecastFetch(const char *apiKey, const char *cityOrCoords, ForecastData &out);
