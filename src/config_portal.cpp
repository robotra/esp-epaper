#include "config_portal.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>

// ------------------------------------------------------------
//  Access-point configuration
//  AP_PASSWORD is empty → open network (no WPA2 key required).
//  Keeping it open means any phone/laptop can join without
//  knowing a password, which is exactly what we want for a
//  first-time setup flow.
// ------------------------------------------------------------
static const char *AP_SSID     = "EPaper-Setup";
static const char *AP_PASSWORD = "";            // open network — change if desired
static const IPAddress AP_IP(192, 168, 4, 1);  // well-known AP default gateway IP
static const byte DNS_PORT = 53;               // standard DNS port

// Portal times out and restarts after this period with no submission
static const unsigned long PORTAL_TIMEOUT_MS = 5UL * 60UL * 1000UL;   // 5 minutes

// HTTP server on port 80; DNS sinkhole on UDP 53
static WebServer server(80);
static DNSServer  dns;


// ============================================================
//  HTML_PAGE — the setup form
//
//  Stored in flash (PROGMEM) via R"rawhtml(…)" + PROGMEM to
//  avoid occupying ~3 KB of precious heap.  The page:
//    • Is self-contained — no external CSS/JS resources, so it
//      loads even with no real internet connection.
//    • Uses %PLACEHOLDER% tokens that buildPage() replaces with
//      current NVS values so the form is pre-filled on revisit.
//    • Passwords are intentionally NOT echoed back (security):
//      %WIFI_PASS% and %TWILIO_TOKEN% are replaced with "".
//    • The whitelist textarea stores one number per line for
//      readability; handleSave() normalises it to comma-separated.
//    • A small JS snippet shows the "Saved!" banner immediately
//      on submit so the user knows the POST was sent (the device
//      will restart a moment later).
// ============================================================
static const char HTML_PAGE[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ePaper Dashboard Setup</title>
<style>
  *{box-sizing:border-box}
  body{font-family:sans-serif;background:#f0f0f0;display:flex;
       justify-content:center;padding:24px 8px}
  .card{background:#fff;border-radius:10px;padding:28px;
        width:100%;max-width:480px;box-shadow:0 2px 12px #0002}
  h1{margin:0 0 6px;font-size:1.4rem}
  p.sub{color:#666;margin:0 0 24px;font-size:.9rem}
  h2{font-size:1rem;border-bottom:1px solid #ddd;
     padding-bottom:6px;margin:20px 0 12px}
  label{display:block;font-size:.85rem;color:#444;margin-bottom:3px}
  input{width:100%;padding:8px 10px;border:1px solid #ccc;
        border-radius:6px;font-size:.95rem;margin-bottom:14px}
  input:focus{outline:none;border-color:#0078d4}
  .hint{font-size:.78rem;color:#888;margin-top:-10px;margin-bottom:14px}
  button{width:100%;padding:11px;background:#0078d4;color:#fff;
         border:none;border-radius:7px;font-size:1rem;cursor:pointer;
         margin-top:8px}
  button:hover{background:#005fa3}
  .ok{background:#e8f5e9;color:#2e7d32;border:1px solid #a5d6a7;
      border-radius:7px;padding:12px 16px;margin-bottom:20px;display:none}
</style>
</head>
<body>
<div class="card">
  <h1>ePaper Dashboard Setup</h1>
  <p class="sub">Fill in your credentials. The device will restart and connect automatically.</p>
  <p class="sub" style="color:#b45309">&#9201; This page will close in 5 minutes.</p>

  <div class="ok" id="ok">Saved! The device is restarting&hellip;</div>

  <form method="POST" action="/save" id="frm">

    <h2>Wi-Fi</h2>
    <label>Network name (SSID)</label>
    <input name="wifi_ssid" required placeholder="MyHomeWiFi" value="%WIFI_SSID%">
    <label>Password</label>
    <input name="wifi_pass" type="password" placeholder="(leave blank if open)" value="%WIFI_PASS%">

    <h2>Twilio</h2>
    <label>Account SID</label>
    <input name="twilio_sid" required placeholder="ACxxxxxxxxxxxxxxxx" value="%TWILIO_SID%">
    <label>Auth Token</label>
    <input name="twilio_token" required type="password" placeholder="your auth token" value="%TWILIO_TOKEN%">
    <label>Your Twilio number (E.164)</label>
    <input name="twilio_to" required placeholder="+15550001234" value="%TWILIO_TO%">
    <label>SMS sender whitelist (optional)</label>
    <textarea name="sms_whitelist" rows="3" style="width:100%;padding:8px 10px;border:1px solid #ccc;border-radius:6px;font-size:.95rem;margin-bottom:4px;resize:vertical">%SMS_WHITELIST%</textarea>
    <p class="hint">One E.164 number per line (e.g. +15550001234). Leave blank to show messages from anyone.</p>

    <h2>Address Book (optional)</h2>
    <label>Contacts</label>
    <textarea name="contacts" rows="5" style="width:100%;padding:8px 10px;border:1px solid #ccc;border-radius:6px;font-size:.95rem;margin-bottom:4px;resize:vertical">%CONTACTS%</textarea>
    <p class="hint">One contact per line: <em>Name: +E164number</em> (e.g. <code>Alice: +15550001234</code>). Names replace phone numbers on the display.</p>

    <h2>OpenWeatherMap</h2>
    <label>API Key</label>
    <input name="owm_key" required placeholder="your OWM API key" value="%OWM_KEY%">
    <label>City or coordinates</label>
    <input name="owm_city" required placeholder='New York  or  lat=40.7&amp;lon=-74.0' value="%OWM_CITY%">
    <p class="hint">Use a city name or <em>lat=XX.X&amp;lon=YY.Y</em> for GPS coordinates.</p>

    <button type="submit">Save &amp; Restart</button>
  </form>
</div>
<script>
  document.getElementById('frm').addEventListener('submit',function(){
    document.getElementById('ok').style.display='block';
  });
</script>
</body>
</html>
)rawhtml";


// ------------------------------------------------------------
//  buildPage — substitute placeholders with current NVS values
//
//  Called each time the root URL is requested so that if the
//  user visits the portal a second time (e.g. to correct a
//  typo) the form already contains what was saved before.
//
//  Sensitive fields (WiFi password, Twilio Auth Token, OWM key)
//  are replaced with empty strings — they are never echoed back
//  to the browser.  Non-sensitive fields (SSID, Account SID,
//  phone number, city, whitelist) are pre-filled for convenience.
//
//  The whitelist is stored internally as "num1,num2" but
//  displayed one-per-line in the textarea for readability.
// ------------------------------------------------------------
static String buildPage()
{
    Credentials cur;
    credentialsLoad(cur);   // may return false / empty fields — that's fine

    String page = FPSTR(HTML_PAGE);   // copy from PROGMEM into heap String

    // Convert internal comma-separated whitelist to one-per-line for the textarea
    String whitelistDisplay = cur.smsWhitelist;
    whitelistDisplay.replace(",", "\n");

    // Replace each placeholder with the current saved value (or empty for passwords)
    page.replace("%WIFI_SSID%",      cur.wifiSsid);
    page.replace("%WIFI_PASS%",      "");           // never echo passwords back
    page.replace("%TWILIO_SID%",     cur.twilioAccountSid);
    page.replace("%TWILIO_TOKEN%",   "");           // never echo passwords back
    page.replace("%TWILIO_TO%",      cur.twilioToNumber);
    page.replace("%SMS_WHITELIST%",  whitelistDisplay);
    page.replace("%OWM_KEY%",        "");           // never echo passwords back
    page.replace("%OWM_CITY%",       cur.owmCity);

    // Convert stored "Name|+num,Name2|+num2" back to one-per-line "Name: +num"
    // for display in the textarea.
    String contactsDisplay;
    {
        String book = cur.contactBook;
        int start = 0;
        while (start < (int)book.length()) {
            int comma = book.indexOf(',', start);
            if (comma < 0) comma = book.length();
            String entry = book.substring(start, comma);
            int pipe = entry.indexOf('|');
            if (pipe > 0) {
                if (contactsDisplay.length() > 0) contactsDisplay += "\n";
                // Reconstruct the human-readable "Name: +number" form
                contactsDisplay += entry.substring(0, pipe) + ": " + entry.substring(pipe + 1);
            }
            start = comma + 1;
        }
    }
    page.replace("%CONTACTS%", contactsDisplay);
    return page;
}


// ============================================================
//  HTTP route handlers
// ============================================================

// GET / — serve the form page
static void handleRoot()
{
    server.send(200, "text/html", buildPage());
}

// POST /save — validate, persist, then restart
static void handleSave()
{
    // Pull all form fields from the POST body
    Credentials c;
    c.wifiSsid         = server.arg("wifi_ssid");
    c.wifiPassword     = server.arg("wifi_pass");
    c.twilioAccountSid = server.arg("twilio_sid");
    c.twilioAuthToken  = server.arg("twilio_token");
    c.twilioToNumber   = server.arg("twilio_to");
    c.owmApiKey        = server.arg("owm_key");
    c.owmCity          = server.arg("owm_city");

    // ---- Normalise whitelist ----------------------------------------
    // The textarea sends newline-separated numbers (and potentially
    // CR+LF on Windows browsers).  Internally we store a single
    // comma-separated string.  This block collapses all line endings
    // to commas, then iterates the tokens to strip blanks / whitespace.
    {
        String raw = server.arg("sms_whitelist");
        raw.replace("\r\n", ",");   // Windows line endings → comma
        raw.replace("\r",   ",");   // old Mac line endings → comma
        raw.replace("\n",   ",");   // Unix line endings → comma

        String normalized;
        int start = 0;
        while (start < (int)raw.length()) {
            int comma = raw.indexOf(',', start);
            if (comma < 0) comma = raw.length();   // last token
            String entry = raw.substring(start, comma);
            entry.trim();   // strip leading/trailing whitespace
            if (entry.length() > 0) {
                if (normalized.length() > 0) normalized += ',';
                normalized += entry;
            }
            start = comma + 1;
        }
        c.smsWhitelist = normalized;
    }

    // Strip accidental leading/trailing spaces from text fields
    // ---- Normalise address book ----------------------------------------
    // The textarea holds one "Name: +number" entry per line.
    // Convert to the internal "Name|+number,Name2|+number2" storage format,
    // skipping blank lines and lines that don't contain a colon.
    {
        String raw = server.arg("contacts");
        raw.replace("\r\n", "\n");
        raw.replace("\r",   "\n");
        String normalized;
        int start = 0;
        while (start < (int)raw.length()) {
            int nl = raw.indexOf('\n', start);
            if (nl < 0) nl = raw.length();
            String line = raw.substring(start, nl);
            line.trim();
            int colon = line.indexOf(':');
            if (colon > 0) {
                String name   = line.substring(0, colon);
                String number = line.substring(colon + 1);
                name.trim();
                number.trim();
                if (name.length() > 0 && number.length() > 0) {
                    if (normalized.length() > 0) normalized += ',';
                    normalized += name + "|" + number;
                }
            }
            start = nl + 1;
        }
        c.contactBook = normalized;
    }

    c.wifiSsid.trim();
    c.twilioAccountSid.trim();
    c.twilioToNumber.trim();
    c.owmCity.trim();

    // ---- Server-side validation ------------------------------------
    // The HTML form already has `required` attributes for client-side
    // checks, but we validate here too in case JavaScript is disabled
    // or the request is crafted manually.
    if (c.wifiSsid.isEmpty() || c.twilioAccountSid.isEmpty() ||
        c.twilioAuthToken.isEmpty() || c.twilioToNumber.isEmpty() ||
        c.owmApiKey.isEmpty() || c.owmCity.isEmpty())
    {
        server.send(400, "text/plain", "Missing required fields.");
        return;
    }

    // Persist to NVS
    credentialsSave(c);

    // Send a confirmation page, then wait 3 s so the browser can
    // display it before the device disappears from the network.
    server.send(200, "text/html",
        "<html><body style='font-family:sans-serif;padding:40px'>"
        "<h2>Saved! Restarting in 3 seconds&hellip;</h2>"
        "</body></html>");

    delay(3000);
    ESP.restart();   // reboot into normal operation with new credentials
}

// Catch-all route — redirect every unknown URL back to the root.
// This is the second half of the captive-portal trick: the OS's
// connectivity probe hits an arbitrary URL, gets a 302 to the
// portal IP, and pops up the browser automatically.
static void handleNotFound()
{
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain", "");
}


// ============================================================
//  configPortalRun — public entry point
//
//  Execution flow:
//    1. Switch WiFi to AP-only mode and bring up the soft-AP.
//    2. Start the DNS sinkhole (all queries → 192.168.4.1).
//    3. Register HTTP routes and start the web server.
//    4. Spin in an event loop handling DNS and HTTP requests.
//    5. On form submit: handleSave() validates + saves + restarts.
//    6. On timeout: restart (NVS is unchanged).
//
//  This function never returns.
// ============================================================
void configPortalRun()
{
    Serial.println("[portal] Starting configuration portal...");

    // ---- WiFi soft-AP setup ----------------------------------------
    WiFi.mode(WIFI_AP);
    // softAPConfig(local_ip, gateway, subnet) — gateway = local_ip
    // means the AP itself is the default gateway, which is required
    // for the captive-portal DNS redirect to work on most devices.
    WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0));
    WiFi.softAP(AP_SSID, AP_PASSWORD);

    Serial.printf("[portal] AP SSID: %s\n", AP_SSID);
    Serial.printf("[portal] Open http://%s in your browser\n", AP_IP.toString().c_str());

    // ---- DNS sinkhole ----------------------------------------------
    // The wildcard "*" catches every domain name and resolves it to
    // AP_IP.  When a phone joins the open network and its OS fires a
    // background connectivity check (e.g. http://connectivitycheck.gstatic.com),
    // the DNS returns 192.168.4.1, the HTTP server returns the setup
    // form, and the OS shows a "Sign in to network" notification.
    dns.start(DNS_PORT, "*", AP_IP);

    // ---- HTTP routes ------------------------------------------------
    server.on("/",     HTTP_GET,  handleRoot);     // serve the form
    server.on("/save", HTTP_POST, handleSave);     // process submitted form
    server.onNotFound(handleNotFound);             // captive portal redirect
    server.begin();

    // ---- Event loop -------------------------------------------------
    // Process DNS and HTTP events until the form is submitted
    // (handleSave → restarts) or the 5-minute timeout elapses.
    unsigned long startMs = millis();
    for (;;) {
        dns.processNextRequest();   // handle one queued DNS query
        server.handleClient();      // handle one HTTP request/response
        delay(2);                   // yield to background WiFi stack tasks

        if (millis() - startMs >= PORTAL_TIMEOUT_MS) {
            Serial.println("[portal] Timeout — no credentials submitted, restarting");
            ESP.restart();
        }
    }
}
