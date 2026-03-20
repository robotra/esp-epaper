#pragma once
#include <Arduino.h>

// ============================================================
//  credentials_store.h — Persistent credential storage via NVS
//
//  All user-supplied secrets (WiFi password, API keys, etc.) are
//  stored in the ESP32's Non-Volatile Storage (NVS), which
//  survives deep sleep and power cycles but is erased by a full
//  flash erase.
//
//  The NVS namespace used is "creds" (see credentials_store.cpp).
//  Keys are short strings to stay within NVS's 15-character key
//  limit.
//
//  Credentials are NEVER compiled into the firmware.  They are
//  entered by the user through the captive-portal web page on
//  first boot (or after a 3-rapid-power-cycle reset).
// ============================================================


// ------------------------------------------------------------
//  Credentials — fields entered by the user via the setup portal
//
//  Required fields (credentialsLoad returns false if any are empty):
//    wifiSsid         — the 802.11 network name to connect to
//    twilioAccountSid — starts with "AC", found in the Twilio Console
//    twilioAuthToken  — secret token; treat like a password
//    twilioToNumber   — the Twilio phone number that receives SMS,
//                       in E.164 format (e.g. "+15550001234")
//    owmApiKey        — OpenWeatherMap "Current Weather Data" API key
//    owmCity          — city name ("London") OR GPS query string
//                       ("lat=51.5&lon=-0.1").  City names are
//                       URL-encoded internally (spaces → '+').
//
//  Optional fields (may be empty; device operates without them):
//    wifiPassword     — WPA2 passphrase; empty = open network
//    smsWhitelist     — comma-separated E.164 numbers; only messages
//                       FROM these senders are shown.  Empty = all.
//    contactBook      — pipe-and-comma store of friendly names:
//                       "Alice|+15550001234,Bob Smith|+15550005678"
//                       Names replace E.164 numbers in the messages
//                       panel.  Empty = show raw numbers.
// ------------------------------------------------------------
struct Credentials {
    String wifiSsid;
    String wifiPassword;
    String twilioAccountSid;
    String twilioAuthToken;
    String twilioToNumber;
    String smsWhitelist;   // comma-separated E.164 numbers; empty = show all
    String owmApiKey;
    String owmCity;
    // Address book: pipe-separated name|number pairs joined by commas,
    // e.g. "Alice|+15550001234,Bob Smith|+15550005678".
    // Empty = show raw E.164 numbers.  Optional field — never required.
    String contactBook;
};


// ------------------------------------------------------------
//  credentialsLoad — read all credentials from NVS
//
//  Opens the "creds" NVS namespace in read-only mode, reads each
//  key into `out`, then returns true if every *required* field
//  is non-empty.  (wifiPassword is optional — open networks have
//  no password.)
//
//  Returns false if NVS has never been written (first boot) or
//  if a previous save was incomplete.  Caller should then start
//  the setup portal.
// ------------------------------------------------------------
bool credentialsLoad(Credentials &out);


// ------------------------------------------------------------
//  credentialsSave — persist credentials to NVS
//
//  Opens the "creds" NVS namespace in read/write mode and writes
//  all nine fields.  Existing values are overwritten.
//  Called by config_portal.cpp after the user submits the form.
// ------------------------------------------------------------
void credentialsSave(const Credentials &creds);


// ------------------------------------------------------------
//  credentialsClear — erase all saved credentials
//
//  Calls Preferences::clear() on the "creds" namespace, removing
//  every key.  On the next reboot credentialsLoad() will return
//  false and the setup portal will launch automatically.
//
//  This is triggered either by:
//    • the user power-cycling the device 3 times in quick
//      succession (detected in main.cpp's boot-count logic), or
//    • a WiFi connection timeout (too many retries → assume the
//      saved SSID/password is wrong → force re-setup).
// ------------------------------------------------------------
void credentialsClear();
