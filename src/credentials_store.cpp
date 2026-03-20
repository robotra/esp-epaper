#include "credentials_store.h"
#include <Preferences.h>

// ------------------------------------------------------------
//  NVS namespace — all credentials live under this single key
//  space.  Keeping them isolated means credentialsClear() can
//  wipe them without touching other NVS data (e.g. the boot
//  counter used for the 3-cycle reset detection).
// ------------------------------------------------------------
static const char *NVS_NS = "creds";


// ------------------------------------------------------------
//  credentialsLoad
//
//  The Preferences library maps to the ESP-IDF NVS partition.
//  Opening with readOnly=true prevents accidental writes and is
//  slightly faster.  Each getString() returns "" when the key
//  does not exist, so the struct is always fully initialised
//  even on first boot.
// ------------------------------------------------------------
bool credentialsLoad(Credentials &out)
{
    Preferences prefs;
    prefs.begin(NVS_NS, /*readOnly=*/true);

    // Read every stored key.  The second argument is the default
    // value returned when the key is absent (never-configured device).
    out.wifiSsid           = prefs.getString("wifi_ssid",      "");
    out.wifiPassword       = prefs.getString("wifi_pass",      "");
    out.twilioAccountSid   = prefs.getString("twilio_sid",     "");
    out.twilioAuthToken    = prefs.getString("twilio_token",   "");
    out.twilioToNumber     = prefs.getString("twilio_to",      "");
    out.smsWhitelist       = prefs.getString("sms_whitelist",  "");
    out.owmApiKey          = prefs.getString("owm_key",        "");
    out.owmCity            = prefs.getString("owm_city",       "");
    out.contactBook        = prefs.getString("contacts",       "");

    prefs.end();

    // wifiPassword is the only truly optional field — open networks
    // require no password.  Everything else must be non-empty for
    // the device to operate.  Return false to signal "go to portal".
    return out.wifiSsid.length()         > 0 &&
           out.twilioAccountSid.length() > 0 &&
           out.twilioAuthToken.length()  > 0 &&
           out.twilioToNumber.length()   > 0 &&
           out.owmApiKey.length()        > 0 &&
           out.owmCity.length()          > 0;
}


// ------------------------------------------------------------
//  credentialsSave
//
//  Opens in read/write mode (readOnly=false).  putString() will
//  create the key if it doesn't exist or overwrite it if it does.
//  The call to prefs.end() flushes and closes the handle — NVS
//  writes are not committed until the handle is closed.
// ------------------------------------------------------------
void credentialsSave(const Credentials &c)
{
    Preferences prefs;
    prefs.begin(NVS_NS, /*readOnly=*/false);

    prefs.putString("wifi_ssid",     c.wifiSsid);
    prefs.putString("wifi_pass",     c.wifiPassword);
    prefs.putString("twilio_sid",    c.twilioAccountSid);
    prefs.putString("twilio_token",  c.twilioAuthToken);
    prefs.putString("twilio_to",     c.twilioToNumber);
    prefs.putString("sms_whitelist", c.smsWhitelist);
    prefs.putString("owm_key",       c.owmApiKey);
    prefs.putString("owm_city",      c.owmCity);
    prefs.putString("contacts",      c.contactBook);

    prefs.end();   // flush + close — NVS is committed here
}


// ------------------------------------------------------------
//  credentialsClear
//
//  Preferences::clear() removes ALL keys in the namespace in one
//  call.  After this returns, the next credentialsLoad() will
//  find empty strings for every field and return false, causing
//  the setup portal to start on the next boot.
// ------------------------------------------------------------
void credentialsClear()
{
    Preferences prefs;
    prefs.begin(NVS_NS, false);
    prefs.clear();   // wipe every key under "creds"
    prefs.end();
}
