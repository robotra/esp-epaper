// ============================================================
//  main.cpp — ESP32 e-paper dashboard: entry point
//
//  This file owns the top-level lifecycle:
//    1. Initialise SPI and the IT8951 e-paper display.
//    2. Detect the 3-rapid-power-cycle reset gesture.
//    3. Load credentials from NVS; if missing:
//         a. Render a setup screen on the display containing a
//            scannable WiFi QR code and plain-text instructions.
//         b. Start the captive-portal web server (never returns).
//    4. Connect to WiFi.
//    5. Fetch weather + SMS data and render the dashboard.
//    6. Put the IT8951 into low-power hibernation.
//    7. Enter ESP32 deep sleep for 14 minutes.
//    8. The RTC timer fires, the chip resets, setup() runs again.
//
//  Because every cycle ends with esp_deep_sleep_start(), the
//  loop() function is never reached.
//
//  Key functions defined here:
//    drawSetupScreen() — QR code + text prompt for first-boot
//    wifiConnect()     — join saved WiFi, restart on timeout
//    refreshDashboard()— fetch all data sources, call uiDraw()
// ============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <SPI.h>
#include <Preferences.h>

#include <GxEPD2_BW.h>
#include <GxEPD2_it8951.h>
#include <Fonts/FreeMonoBold9pt7b.h>   // bold font used on the setup screen
#include <qrcode.h>                    // ricmoo/QRCode: QR module array generator

#include "config.h"
#include "credentials_store.h"
#include "config_portal.h"
#include "twilio_client.h"
#include "weather_client.h"
#include "ui_renderer.h"


// ------------------------------------------------------------
//  Display instance
//
//  GxEPD2_it8951<W, H> is the panel driver for IT8951-based
//  e-paper displays.  The three constructor arguments are the
//  SPI control pins defined in config.h.
//
//  DisplayType (the full template alias) is defined in
//  ui_renderer.h and wraps both the driver and the GxEPD2_BW
//  "black-and-white" graphics layer.
// ------------------------------------------------------------
GxEPD2_it8951<800, 480> epd(EPAPER_CS, EPAPER_RST, EPAPER_BUSY);
DisplayType display(epd);

// Credentials loaded from NVS at the start of setup().
// Declared at file scope so helper functions can share it.
static Credentials g_creds;


// ============================================================
//  drawSetupScreen — render the first-boot configuration prompt
//
//  Called once, before configPortalRun(), when no credentials are
//  found in NVS.  Draws two regions side-by-side:
//
//    Left  (0–399 px)  — QR code that encodes the WiFi join string
//                        for the "EPaper-Setup" open access point.
//                        Scanning it connects a phone directly to
//                        the AP without typing the SSID manually.
//
//    Right (420–792 px) — Plain-text instructions as a fallback for
//                         devices that can't scan QR codes.
//
//  QR code format (WiFi Easy Connect / ZXing spec):
//    WIFI:S:<SSID>;T:<auth-type>;P:<password>;;
//  For an open (no-password) network:
//    WIFI:S:EPaper-Setup;T:nopass;;
//
//  Rendering:
//    • QR version 3 (29×29 modules) at ECC_LOW fits the 31-character
//      string with room to spare and produces a good-sized code.
//    • Each module is drawn as a MODULE_PX × MODULE_PX filled rectangle.
//    • A QUIET_PX-pixel white border surrounds the code (the QR spec
//      requires ≥4 quiet modules; 3 modules × 10 px = 30 px here).
//    • The block is centred both vertically and horizontally within
//      the left 400-pixel column.
// ============================================================
static void drawSetupScreen(DisplayType &display)
{
    // ---- Build QR code -----------------------------------------------
    // The WiFi connection string for an open (password-less) network.
    // If you later add a portal password, change "T:nopass" to "T:WPA"
    // and append "P:<password>" before the closing ";;".
    static const char QR_TEXT[] = "WIFI:S:EPaper-Setup;T:nopass;;";

    QRCode qrcode;
    // qrcode_getBufferSize(version) is a compile-time macro; version 3
    // needs 106 bytes — well within stack budget.
    uint8_t qrData[qrcode_getBufferSize(3)];
    qrcode_initText(&qrcode, qrData, 3, ECC_LOW, QR_TEXT);

    // ---- Layout math -------------------------------------------------
    const int MODULE_PX = 10;                           // pixels per QR module
    const int QUIET_PX  = 30;                           // quiet-zone border (3 × MODULE_PX)
    const int QR_PX     = qrcode.size * MODULE_PX;     // 29 × 10 = 290 px
    const int BLOCK     = QR_PX + 2 * QUIET_PX;        // 290 + 60 = 350 px

    // Centre the entire QR block (incl. quiet zone) inside the left
    // 400-pixel half of the 800-wide display, and vertically on 480 px.
    const int QR_X = (400 - BLOCK) / 2;                // ≈ 25 px from left edge
    const int QR_Y = (480 - BLOCK) / 2;                // ≈ 65 px from top

    // Text column starts just past the vertical divider
    const int TX = 430;

    // ---- Draw --------------------------------------------------------
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);

        // -- QR code --------------------------------------------------
        // Flood the entire quiet-zone block with white first; this
        // guarantees clean contrast even if the panel has residual image.
        display.fillRect(QR_X, QR_Y, BLOCK, BLOCK, GxEPD_WHITE);

        // Draw each dark (1) module as a solid filled square
        for (int row = 0; row < qrcode.size; row++) {
            for (int col = 0; col < qrcode.size; col++) {
                if (qrcode_getModule(&qrcode, col, row)) {
                    display.fillRect(
                        QR_X + QUIET_PX + col * MODULE_PX,
                        QR_Y + QUIET_PX + row * MODULE_PX,
                        MODULE_PX, MODULE_PX,
                        GxEPD_BLACK);
                }
            }
        }

        // Thin vertical divider separating the two columns
        display.drawFastVLine(410, 40, 400, GxEPD_BLACK);

        // -- Text instructions ----------------------------------------
        // Heading (bold proportional font)
        display.setFont(&FreeMonoBold9pt7b);
        display.setTextColor(GxEPD_BLACK);
        display.setCursor(TX, 90);
        display.print("Setup required");

        // Body text (built-in 6×8 bitmap font — most readable at this size)
        display.setFont(nullptr);
        display.setCursor(TX, 115);
        display.print("Scan the QR code with your phone");
        display.setCursor(TX, 127);
        display.print("to join the setup network, or");
        display.setCursor(TX, 139);
        display.print("connect manually:");

        display.setCursor(TX, 159);
        display.print("  Wi-Fi: EPaper-Setup");
        display.setCursor(TX, 171);
        display.print("  Password: (none)");

        display.setCursor(TX, 195);
        display.print("Then open your browser to:");
        display.setCursor(TX, 207);
        display.print("  http://192.168.4.1");

        display.setCursor(TX, 231);
        display.print("Fill in your credentials and");
        display.setCursor(TX, 243);
        display.print("click  Save & Restart.");

        display.setCursor(TX, 267);
        display.print("The device will reboot and");
        display.setCursor(TX, 279);
        display.print("connect automatically.");

    } while (display.nextPage());
}


// ============================================================
//  wifiConnect — join the saved WiFi network
//
//  Polls WL_CONNECTED at 500 ms intervals.  After 40 failed
//  attempts (~20 seconds) it assumes the stored SSID/password
//  is wrong, clears credentials from NVS (forcing the setup
//  portal on next boot), and restarts.
//
//  This prevents the device from spinning forever if the router
//  is renamed or the password changes.
// ============================================================
static void wifiConnect()
{
    Serial.printf("Connecting to WiFi: %s", g_creds.wifiSsid.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.begin(g_creds.wifiSsid.c_str(), g_creds.wifiPassword.c_str());

    int retries = 0;
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print('.');
        if (++retries > 40) {
            // Too many failures — stored credentials are probably stale.
            // Clear them so the user is prompted to re-enter on next boot.
            Serial.println("\nWiFi timeout — clearing credentials and restarting");
            credentialsClear();
            ESP.restart();
        }
    }
    Serial.printf("\nConnected, IP: %s\n", WiFi.localIP().toString().c_str());
}


// ============================================================
//  refreshDashboard — one complete data-fetch-and-render cycle
//
//  Calls the three data sources in sequence, then hands
//  everything to uiDraw().  Any individual fetch failure sets
//  the corresponding `valid` flag to false; uiDraw() displays
//  a graceful "unavailable" message for that section rather
//  than crashing.
//
//  The three fetches are intentionally sequential (not parallel)
//  because the ESP32 has only one WiFi radio and the overhead of
//  managing concurrent HTTP connections on a single-threaded
//  Arduino sketch outweighs any latency benefit.
// ============================================================
static void refreshDashboard()
{
    // ---- Fetch current weather ----------------------------------------
    WeatherData weather;
    weatherFetch(g_creds.owmApiKey.c_str(), g_creds.owmCity.c_str(), weather);

    // ---- Fetch 3-hour / 5-day forecast --------------------------------
    ForecastData forecast;
    forecastFetch(g_creds.owmApiKey.c_str(), g_creds.owmCity.c_str(), forecast);

    // ---- Fetch recent SMS messages from Twilio ------------------------
    // Allocate the message array on the stack; TWILIO_MSG_COUNT_MAX sets
    // the hard upper bound, TWILIO_MAX_SHOW limits how many to actually
    // display.  twilioError receives a human-readable description if the
    // fetch fails (e.g. "HTTP 403"); it stays empty on success.
    TwilioMessage messages[TWILIO_MSG_COUNT_MAX];
    String twilioError;
    int msgCount = twilioFetchMessages(
        g_creds.twilioAccountSid.c_str(),
        g_creds.twilioAuthToken.c_str(),
        g_creds.twilioToNumber.c_str(),
        g_creds.smsWhitelist.c_str(),
        messages, TWILIO_MAX_SHOW,
        &twilioError);
    if (msgCount < 0) msgCount = 0;

    // ---- Render everything to the e-paper display ---------------------
    // All three error strings (weather.errorMsg, forecast.errorMsg,
    // twilioError) are passed through to uiDraw so that any API failure
    // is visible directly on the screen without needing a serial monitor.
    uiDraw(display, weather, forecast, messages, msgCount, twilioError,
           g_creds.contactBook);
}


// ============================================================
//  setup — runs once per wake cycle (power-on OR RTC wakeup)
//
//  Execution order:
//    1. Serial + SPI + display init
//    2. 3-power-cycle detection (triggers credential reset + portal)
//    3. Load credentials from NVS
//         • If missing → drawSetupScreen() (QR code + text),
//           then configPortalRun() — neither returns
//    4. WiFi connect
//    5. Refresh dashboard (fetch + render)
//    6. Hibernate display (IT8951 low-power mode)
//    7. Deep sleep for SLEEP_DURATION_US (RTC timer wakeup)
// ============================================================
void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println("epaper-dashboard starting");

    // ---- Initialise SPI -----------------------------------------------
    // ESP32-S3 HSPI pins: CLK=12, MISO=13, MOSI=11, CS=EPAPER_CS
    // The MISO line is unused by the IT8951 (SPI write-only to the host)
    // but must be specified to configure the SPI peripheral correctly.
    SPI.begin(12, 13, 11, EPAPER_CS);

    // ---- Initialise e-paper display ------------------------------------
    // Arguments: baud (debug serial speed), initial (true = hardware reset),
    //            reset_duration (ms), pulldown_rst_mode (false = active-low reset)
    display.init(115200, true, 2, false);
    display.setRotation(0);          // landscape, USB connector on the left
    display.setVcom(EPAPER_VCOM_MV); // set VCOM bias — must match your panel label


    // ================================================================
    //  3-rapid-power-cycle reset detection
    //
    //  Some devices have a dedicated factory-reset button.  This one
    //  uses a software trick instead: power-cycling the device 3 times
    //  within ~3 seconds of each other clears credentials and forces
    //  the setup portal.  This is useful if:
    //    • The WiFi password changed and the device can no longer connect.
    //    • The user wants to reconfigure everything from scratch.
    //
    //  Implementation:
    //    • A counter ("cnt") is stored in a separate NVS namespace
    //      ("boot") that is distinct from the credentials namespace.
    //    • Every boot that is NOT a timer-wakeup increments the counter.
    //    • After a 3-second wait with no further reset, the counter is
    //      zeroed.  (The 3-second delay also gives the radio time to
    //      settle before we start connecting to WiFi.)
    //    • Timer wakeups (the normal 14-minute refresh cycle) are
    //      explicitly excluded — we don't want routine refreshes to
    //      accidentally accumulate toward the reset threshold.
    //    • When cnt reaches 3, clear credentials and open the portal.
    // ================================================================
    {
        esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
        if (cause != ESP_SLEEP_WAKEUP_TIMER) {
            // This is a genuine power-on (or manual reset) — count it
            Preferences bootPrefs;
            bootPrefs.begin("boot", false);
            int cnt = bootPrefs.getInt("cnt", 0) + 1;
            bootPrefs.putInt("cnt", cnt);
            bootPrefs.end();

            if (cnt >= 3) {
                // Third rapid power cycle — wipe credentials and open portal
                bootPrefs.begin("boot", false);
                bootPrefs.clear();   // reset the boot counter too
                bootPrefs.end();
                credentialsClear();
                Serial.println("3 rapid power cycles detected — credentials cleared, starting portal");
                configPortalRun();   // never returns
            } else {
                // Wait 3 s; if another reset doesn't come in that window,
                // clear the counter so normal operation continues cleanly.
                delay(3000);
                bootPrefs.begin("boot", false);
                bootPrefs.putInt("cnt", 0);
                bootPrefs.end();
            }
        }
        // Timer wakeup: skip the power-cycle detection entirely
    }


    // ================================================================
    //  Credentials check
    //
    //  If no credentials are stored (first boot, or after a credential
    //  clear), show setup instructions on the e-paper display and then
    //  start the captive-portal.  configPortalRun() never returns — it
    //  will restart the device once the user has saved their credentials.
    //
    //  Showing instructions on the display is important because the
    //  user may not be watching the serial output.
    // ================================================================
    if (!credentialsLoad(g_creds)) {
        Serial.println("No credentials found — starting setup portal");

        // Draw a QR code + text instructions so the user knows what to do.
        // The QR code encodes the WiFi join string for the "EPaper-Setup"
        // access point; scanning it with a phone connects automatically.
        drawSetupScreen(display);

        configPortalRun();   // blocks here — restarts the device when done
    }


    // ================================================================
    //  Normal operation
    // ================================================================

    wifiConnect();        // join the saved WiFi network
    refreshDashboard();   // fetch data and update the display

    // Put the IT8951 controller into its low-power sleep mode.
    // This must happen BEFORE esp_deep_sleep_start() so the display
    // is not left drawing current while the ESP32 is asleep.
    display.hibernate();

    // Configure the RTC timer wakeup source and enter deep sleep.
    // Deep sleep draws ~10–20 µA.  The chip fully resets on wake and
    // runs setup() again for the next refresh cycle.
    Serial.printf("Sleeping for %llu s\n", SLEEP_DURATION_US / 1000000ULL);
    esp_sleep_enable_timer_wakeup(SLEEP_DURATION_US);
    esp_deep_sleep_start();   // does not return
}


// ------------------------------------------------------------
//  loop — never reached
//
//  setup() always ends with esp_deep_sleep_start(), which
//  terminates execution.  The Arduino framework calls loop()
//  in an infinite loop only while setup() has returned, which
//  never happens in this firmware.
// ------------------------------------------------------------
void loop()
{
    // Intentionally empty — execution ends in setup() with deep sleep
}
