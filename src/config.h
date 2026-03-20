#pragma once

// ============================================================
//  config.h — Hardware constants and tuneable parameters
//
//  This file is the single place to adjust pin assignments,
//  sleep duration, and display VCOM voltage.  It intentionally
//  contains NO credentials — those are entered by the user at
//  first boot via the captive-portal web page and persisted to
//  the ESP32's Non-Volatile Storage (NVS / Preferences).
// ============================================================


// ------------------------------------------------------------
//  SMS display limit
//  How many of the most-recent inbound Twilio SMS messages to
//  fetch and show in the "Recent Messages" panel.
//  Keep this ≤ TWILIO_MSG_COUNT_MAX (defined in twilio_client.h).
//  Larger values increase the API payload size slightly but
//  don't meaningfully affect memory usage because the JSON is
//  parsed directly from the HTTP stream.
// ------------------------------------------------------------
#define TWILIO_MAX_SHOW     7


// ------------------------------------------------------------
//  Deep-sleep duration
//  After each successful display refresh the ESP32 enters deep
//  sleep.  The RTC timer fires after this interval and the chip
//  resets, running setup() again for the next refresh cycle.
//
//  Units: microseconds (esp_sleep_enable_timer_wakeup expects µs)
//  14 minutes = 14 × 60 × 1,000,000 µs
//
//  Tradeoff: shorter = fresher data, higher average current draw;
//  longer = staler data, better battery life.
// ------------------------------------------------------------
#define SLEEP_DURATION_US  (14ULL * 60ULL * 1000000ULL)   // 14 minutes


// ============================================================
//  Pin assignments — 3.97" 800×480 IT8951 e-paper display
//
//  The IT8951 controller speaks SPI.  Three extra GPIOs are
//  needed beyond the standard SPI bus:
//    CS   – chip-select (active LOW, held HIGH when idle)
//    RST  – hardware reset (active LOW pulse during init)
//    BUSY – output from the display; LOW while the controller
//           is processing a command (driver polls this pin)
//
//  SPI bus pins are configured explicitly in main.cpp:
//    SPI.begin(CLK=12, MISO=13, MOSI=11, SS=EPAPER_CS)
//  Adjust all five pins to match your PCB / wiring.
// ============================================================
#define EPAPER_CS    10   // SPI chip-select for the IT8951
#define EPAPER_RST   46   // Hardware reset line
#define EPAPER_BUSY   3   // Busy signal from the IT8951 controller
// SPI bus (HSPI on ESP32-S3): MOSI=11, CLK=12, MISO=13


// ------------------------------------------------------------
//  VCOM voltage
//  The IT8951 e-paper controller requires the VCOM bias voltage
//  to be set in software to match the physical display panel.
//  The correct value is printed on a label attached to the
//  display's FPC (flexible printed circuit) cable — look for a
//  negative number like "-2.00V" and multiply by 1000 to get
//  millivolts.
//
//  Getting this wrong will not damage the display but may cause
//  poor contrast, ghosting, or uneven greyscale.
//  Units: millivolts (negative integer)
// ------------------------------------------------------------
#define EPAPER_VCOM_MV  (-2000)   // -2.00 V — read the label on your FPC cable
