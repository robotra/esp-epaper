#pragma once

#include "EPD_3in97.h"
#include "GUI_Paint.h"
#include "fonts.h"
#include "weather_client.h"
#include "twilio_client.h"

// ============================================================
//  ui_renderer.h — E-paper display layout and rendering
//
//  The display is 800 × 480 pixels (landscape orientation).
//  The screen is divided into four horizontal bands:
//
//    Y=0   ┌──────────────────────────────────────────────────┐
//          │  Band 1 — Current weather (84 px)                │
//          │  "last updated" timestamp right-aligned top-right │
//    Y=84  ├──────────────────────────────────────────────────┤
//          │  Band 2 — Hourly forecast, 8 columns (78 px)     │
//    Y=162 ├──────────────────────────────────────────────────┤
//          │  Band 3 — 3-day forecast (72 px)                 │
//    Y=234 ╠══════════════════════════════════════════════════╣
//          │  Band 4 — Recent SMS messages (246 px)           │
//    Y=480 └──────────────────────────────────────────────────┘
//
//  Fonts in use (Waveshare Paint library):
//    Font20  — 14×20 px; body / label text
//    Font24  — 17×24 px; headings and temperature
//
//  Rendering model: caller allocates a UBYTE framebuffer sized
//  (EPD_3IN97_WIDTH/8) * EPD_3IN97_HEIGHT bytes, passes it to
//  uiDraw(), which fills it via Paint_Draw* calls and then
//  pushes it to the display with EPD_3IN97_Display_Base().
// ============================================================


// ------------------------------------------------------------
//  uiDraw — render the complete dashboard to the display
//
//  Initialises the Paint layer onto `image`, clears it white,
//  draws all four bands, then calls EPD_3IN97_Display_Base()
//  to push the buffer to the panel.
//
//  Parameters:
//    image        — pre-allocated framebuffer
//                   (EPD_3IN97_WIDTH/8 * EPD_3IN97_HEIGHT bytes)
//    weather      — current conditions from weatherFetch()
//    forecast     — hourly + daily forecast from forecastFetch()
//    messages[]   — array of TwilioMessage structs
//    messageCount — number of valid entries in messages[]
//    twilioError  — error string; empty on success
//    contactBook  — "Name|+E164,Name2|+E164" address book
// ------------------------------------------------------------
void uiDraw(UBYTE                 *image,
            const WeatherData     &weather,
            const ForecastData    &forecast,
            const TwilioMessage    messages[],
            int                    messageCount,
            const String          &twilioError,
            const String          &contactBook,
            const String          &lastUpdated);
