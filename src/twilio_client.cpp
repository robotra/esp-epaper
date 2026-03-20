#include "twilio_client.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <base64.h>

// Tag prepended to all Serial log lines from this module
static const char *TAG = "twilio";


// ------------------------------------------------------------
//  inWhitelist — check whether a number appears in the whitelist
//
//  The whitelist is stored as a comma-separated string, e.g.:
//    "+15550001234,+15550005678"
//
//  This function iterates the tokens without allocating a
//  separate array, comparing each token against `number`.
//
//  Special case: if whitelist is NULL or empty, every number
//  is allowed (returns true immediately).
// ------------------------------------------------------------
static bool inWhitelist(const String &number, const char *whitelist)
{
    // Empty whitelist = allow all senders
    if (!whitelist || whitelist[0] == '\0') return true;

    String w = whitelist;
    int start = 0;
    while (start < (int)w.length()) {
        int comma = w.indexOf(',', start);
        if (comma < 0) comma = w.length();   // last token has no trailing comma
        if (w.substring(start, comma) == number) return true;
        start = comma + 1;
    }
    return false;   // number not found in whitelist
}


// ============================================================
//  twilioFetchMessages
//
//  Step-by-step:
//    1. Base64-encode "AccountSid:AuthToken" for the Basic Auth header.
//    2. URL-encode the '+' in the Twilio number ('+' is reserved in URLs).
//    3. If a whitelist is active, request 3× as many messages from the
//       API to ensure we have enough to fill `maxCount` after filtering.
//    4. Build the URL and perform an HTTPS GET.
//    5. Parse the JSON response.  The Twilio Messages resource returns
//       an object like: { "messages": [ {...}, {...}, … ] }
//    6. Iterate the messages array; apply whitelist filter; fill out[].
//    7. Return the count of messages written.
// ============================================================
int twilioFetchMessages(const char *accountSid,
                        const char *authToken,
                        const char *toNumber,
                        const char *fromWhitelist,
                        TwilioMessage out[],
                        int maxCount,
                        String *errorOut)
{
    // ---- 1. Build Basic Auth header ----------------------------------
    // HTTP Basic Auth encodes "username:password" in Base64.
    // For Twilio: username = Account SID, password = Auth Token.
    String credentials = String(accountSid) + ":" + String(authToken);
    String encoded = base64::encode(credentials);   // no line breaks needed

    // ---- 2. URL-encode the To number ---------------------------------
    // The '+' prefix of E.164 numbers is a reserved URL character that
    // means "space" in query strings.  We must encode it as "%2B".
    String toEncoded = String(toNumber);
    toEncoded.replace("+", "%2B");

    // ---- 3. Determine fetch page size ---------------------------------
    // If we're filtering by whitelist, many messages may be discarded.
    // Fetch up to 3× more than we need so we have enough after filtering.
    // Twilio's maximum PageSize is 1000, but 50 is a safe practical cap.
    bool hasWhitelist = fromWhitelist && fromWhitelist[0] != '\0';
    int fetchSize = hasWhitelist ? min(maxCount * 3, 50) : maxCount;

    // ---- 4. Build URL and issue HTTP GET ------------------------------
    // Twilio API endpoint for listing messages sent TO a given number.
    String url = "https://api.twilio.com/2010-04-01/Accounts/";
    url += accountSid;
    url += "/Messages.json?To=";
    url += toEncoded;
    url += "&PageSize=";
    url += fetchSize;

    HTTPClient http;
    http.begin(url);
    // Attach the Authorization header — Twilio rejects requests without it
    http.addHeader("Authorization", "Basic " + encoded);

    int httpCode = http.GET();
    if (httpCode != 200) {
        Serial.printf("[%s] HTTP error %d\n", TAG, httpCode);
        if (errorOut) *errorOut = "HTTP " + String(httpCode);
        http.end();
        return -1;
    }

    // Read the full JSON response into a String.
    // Twilio's message list responses are typically a few KB — manageable.
    String payload = http.getString();
    http.end();

    // ---- 5. Parse JSON ------------------------------------------------
    // The response top-level object has a "messages" array.
    // Each element has (among many fields): from, body, date_sent.
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        Serial.printf("[%s] JSON parse error: %s\n", TAG, err.c_str());
        if (errorOut) *errorOut = String("JSON: ") + err.c_str();
        return -1;
    }

    // ---- 6. Filter and fill output array ------------------------------
    // Messages are already sorted newest-first by Twilio.
    // We stop as soon as we have `maxCount` accepted messages.
    JsonArray messages = doc["messages"].as<JsonArray>();
    int count = 0;
    for (JsonObject msg : messages) {
        if (count >= maxCount) break;

        String from = msg["from"].as<String>();
        // Skip messages whose sender is not on the whitelist
        if (!inWhitelist(from, fromWhitelist)) continue;

        out[count].from     = from;
        out[count].body     = msg["body"].as<String>();
        out[count].dateSent = msg["date_sent"].as<String>();
        count++;
    }

    return count;   // number of messages written to out[]
}
