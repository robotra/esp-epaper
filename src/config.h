#pragma once

// ============================================================
//  config.h — Tuneable parameters
//
//  Hardware pin assignments are now owned by DEV_Config.h
//  (the Waveshare driver file copied into src/).
//  Adjust EPD_SCK_PIN, EPD_MOSI_PIN, EPD_CS_PIN, EPD_RST_PIN,
//  EPD_DC_PIN, EPD_BUSY_PIN there if your wiring differs.
// ============================================================


// ------------------------------------------------------------
//  SMS display limit
// ------------------------------------------------------------
#define TWILIO_MAX_SHOW     3


// ------------------------------------------------------------
//  Timezone (POSIX TZ string — used for "last updated" timestamp)
//  Examples:
//    US Eastern : "EST5EDT,M3.2.0,M11.1.0"
//    US Central : "CST6CDT,M3.2.0,M11.1.0"
//    US Mountain: "MST7MDT,M3.2.0,M11.1.0"
//    US Pacific : "PST8PDT,M3.2.0,M11.1.0"
//    UK         : "GMT0BST,M3.5.0/1,M10.5.0"
// ------------------------------------------------------------
#define TIMEZONE  "EST5EDT,M3.2.0,M11.1.0"


// ------------------------------------------------------------
//  Deep-sleep duration (microseconds)
//  14 minutes = 14 × 60 × 1,000,000 µs
// ------------------------------------------------------------
#define SLEEP_DURATION_US  (14ULL * 60ULL * 1000000ULL)
