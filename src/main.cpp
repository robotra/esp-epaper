// ============================================================
//  main.cpp — ESP32 e-paper dashboard: entry point
//
//  This file owns the top-level lifecycle:
//    1. Initialise GPIO/SPI and the EPD_3IN97 e-paper display.
//    2. Detect the 3-rapid-power-cycle reset gesture.
//    3. Load credentials from NVS; if missing:
//         a. Render a setup screen (QR code + text instructions).
//         b. Start the captive-portal web server (never returns).
//    4. Connect to WiFi.
//    5. Fetch weather + SMS data and render the dashboard.
//    6. Put the display into sleep mode.
//    7. Enter ESP32 deep sleep for 14 minutes.
//
//  Because every cycle ends with esp_deep_sleep_start(), the
//  loop() function is never reached.
// ============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <time.h>

#include "EPD_3in97.h"
#include "GUI_Paint.h"
#include "fonts.h"
#include <qrcode.h>

#include "config.h"
#include "credentials_store.h"
#include "config_portal.h"
#include "twilio_client.h"
#include "weather_client.h"
#include "ui_renderer.h"


// ------------------------------------------------------------
//  Image framebuffer
//
//  1-bit per pixel, 800×480 = 48 000 bytes.
//  Allocated once in setup() and freed before deep sleep.
// ------------------------------------------------------------
static UBYTE *g_image = nullptr;
static const UWORD IMAGE_SIZE =
    ((EPD_3IN97_WIDTH % 8 == 0) ? (EPD_3IN97_WIDTH / 8)
                                 : (EPD_3IN97_WIDTH / 8 + 1))
    * EPD_3IN97_HEIGHT;   // = 100 * 480 = 48 000 bytes

// Credentials loaded from NVS at the start of setup().
static Credentials g_creds;


// ============================================================
//  initDisplay — init GPIO/SPI, EPD panel, and framebuffer
// ============================================================
static void initDisplay()
{
    DEV_Module_Init();
    EPD_3IN97_Init();

    if (!g_image) {
        g_image = (UBYTE *)malloc(IMAGE_SIZE);
        if (!g_image) {
            Serial.println("FATAL: framebuffer malloc failed");
            while (1);
        }
        Paint_NewImage(g_image, EPD_3IN97_WIDTH, EPD_3IN97_HEIGHT, 0, WHITE);
        Paint_SetScale(2);
    }
}


// ============================================================
//  drawSetupScreen — render the first-boot configuration prompt
//
//  Left half  (0–399 px) — QR code encoding the WiFi join string
//                          for the "EPaper-Setup" open access point.
//  Right half (420–799 px) — plain-text instructions.
// ============================================================
static void drawSetupScreen()
{
    // ---- Build QR code -----------------------------------------------
    static const char QR_TEXT[] = "WIFI:S:EPaper-Setup;T:nopass;;";

    QRCode qrcode;
    uint8_t qrData[qrcode_getBufferSize(3)];
    qrcode_initText(&qrcode, qrData, 3, ECC_LOW, QR_TEXT);

    // ---- Layout math -------------------------------------------------
    const int MODULE_PX = 10;
    const int QUIET_PX  = 30;
    const int QR_PX     = qrcode.size * MODULE_PX;
    const int BLOCK     = QR_PX + 2 * QUIET_PX;

    const int QR_X = (400 - BLOCK) / 2;
    const int QR_Y = (480 - BLOCK) / 2;
    const int TX   = 430;

    // ---- Draw --------------------------------------------------------
    Paint_SelectImage(g_image);
    Paint_Clear(WHITE);

    // Quiet zone background (white — redundant but explicit)
    Paint_DrawRectangle(QR_X, QR_Y, QR_X + BLOCK - 1, QR_Y + BLOCK - 1,
                        WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);

    // QR modules
    for (int row = 0; row < qrcode.size; row++) {
        for (int col = 0; col < qrcode.size; col++) {
            if (qrcode_getModule(&qrcode, col, row)) {
                Paint_DrawRectangle(
                    QR_X + QUIET_PX + col * MODULE_PX,
                    QR_Y + QUIET_PX + row * MODULE_PX,
                    QR_X + QUIET_PX + col * MODULE_PX + MODULE_PX - 1,
                    QR_Y + QUIET_PX + row * MODULE_PX + MODULE_PX - 1,
                    BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
            }
        }
    }

    // Vertical divider
    Paint_DrawLine(410, 40, 410, 440, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);

    // Text instructions
    Paint_DrawString_EN(TX, 80,  "Setup required",      &Font16, WHITE, BLACK);
    Paint_DrawString_EN(TX, 105, "Scan the QR code with", &Font12, WHITE, BLACK);
    Paint_DrawString_EN(TX, 118, "your phone to join the", &Font12, WHITE, BLACK);
    Paint_DrawString_EN(TX, 131, "setup network, or",     &Font12, WHITE, BLACK);
    Paint_DrawString_EN(TX, 144, "connect manually:",     &Font12, WHITE, BLACK);

    Paint_DrawString_EN(TX, 165, "  Wi-Fi: EPaper-Setup", &Font12, WHITE, BLACK);
    Paint_DrawString_EN(TX, 178, "  Password: (none)",    &Font12, WHITE, BLACK);

    Paint_DrawString_EN(TX, 199, "Then open browser to:", &Font12, WHITE, BLACK);
    Paint_DrawString_EN(TX, 212, "  http://192.168.4.1",  &Font12, WHITE, BLACK);

    Paint_DrawString_EN(TX, 233, "Fill in credentials",   &Font12, WHITE, BLACK);
    Paint_DrawString_EN(TX, 246, "and click Save.",        &Font12, WHITE, BLACK);

    EPD_3IN97_Display_Base(g_image);
}


// ============================================================
//  wifiConnect — join the saved WiFi network
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
            Serial.println("\nWiFi timeout — clearing credentials and restarting");
            credentialsClear();
            ESP.restart();
        }
    }
    Serial.printf("\nConnected, IP: %s\n", WiFi.localIP().toString().c_str());
}


// ============================================================
//  refreshDashboard — one complete data-fetch-and-render cycle
// ============================================================
static void refreshDashboard()
{
    // ---- Sync time via NTP and format "last updated" string ----------
    const char *tz = g_creds.timezone.isEmpty() ? TIMEZONE : g_creds.timezone.c_str();
    configTzTime(tz, "pool.ntp.org", "time.nist.gov");
    struct tm ti = {};
    String lastUpdated = "--:--";
    if (getLocalTime(&ti, 5000)) {
        static const char *MON[] = {
            "Jan","Feb","Mar","Apr","May","Jun",
            "Jul","Aug","Sep","Oct","Nov","Dec"
        };
        char buf[20];
        snprintf(buf, sizeof(buf), "%d %s %02d:%02d",
                 ti.tm_mday, MON[ti.tm_mon], ti.tm_hour, ti.tm_min);
        lastUpdated = buf;
    }

    WeatherData weather;
    weatherFetch(g_creds.owmApiKey.c_str(), g_creds.owmCity.c_str(), weather);

    ForecastData forecast;
    forecastFetch(g_creds.owmApiKey.c_str(), g_creds.owmCity.c_str(), forecast);

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

    uiDraw(g_image, weather, forecast, messages, msgCount, twilioError,
           g_creds.contactBook, lastUpdated);
}


// ============================================================
//  setup — runs once per wake cycle
// ============================================================
void setup()
{
    Serial.begin(115200);
    Serial.println("epaper-dashboard starting");

    // ================================================================
    //  3-rapid-power-cycle reset detection
    //
    //  This runs BEFORE display init so the 3-second window starts
    //  immediately at power-on.  Display init (especially EPD_Clear)
    //  can take 2-3 s and would otherwise eat the entire window.
    // ================================================================
    {
        esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
        if (cause != ESP_SLEEP_WAKEUP_TIMER) {
            Preferences bootPrefs;
            bootPrefs.begin("boot", false);
            int cnt = bootPrefs.getInt("cnt", 0) + 1;
            bootPrefs.putInt("cnt", cnt);
            bootPrefs.end();
            Serial.printf("Boot count: %d\n", cnt);

            if (cnt >= 3) {
                bootPrefs.begin("boot", false);
                bootPrefs.clear();
                bootPrefs.end();
                credentialsClear();
                Serial.println("3 rapid power cycles — clearing credentials, starting portal");
                // Now init the display to show the setup screen
                initDisplay();
                drawSetupScreen();
                configPortalRun();   // never returns
            } else {
                // Wait 3 s from power-on; if no further reset occurs in
                // that window, zero the counter and continue normally.
                delay(3000);
                bootPrefs.begin("boot", false);
                bootPrefs.putInt("cnt", 0);
                bootPrefs.end();
            }
        }
    }


    // ---- Initialise display for normal operation --------------------
    initDisplay();


    // ================================================================
    //  Credentials check
    // ================================================================
    if (!credentialsLoad(g_creds)) {
        Serial.println("No credentials — starting setup portal");
        drawSetupScreen();
        configPortalRun();   // never returns
    }


    // ================================================================
    //  Normal operation
    // ================================================================
    wifiConnect();
    refreshDashboard();

    EPD_3IN97_Sleep();
    free(g_image);
    g_image = nullptr;

    Serial.printf("Sleeping for %llu s\n", SLEEP_DURATION_US / 1000000ULL);
    esp_sleep_enable_timer_wakeup(SLEEP_DURATION_US);
    esp_deep_sleep_start();
}


void loop()
{
    // Never reached — setup() ends with deep sleep
}
