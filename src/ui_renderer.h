#pragma once

#include <GxEPD2_BW.h>
#include <GxEPD2_it8951.h>
#include "weather_client.h"
#include "twilio_client.h"

// ============================================================
//  ui_renderer.h — E-paper display layout and rendering
//
//  The display is 800 × 480 pixels (landscape orientation).
//  The screen is divided into four horizontal bands:
//
//    Y=0   ┌──────────────────────────────────────────────────┐
//          │  Band 1 — Current weather (60 px tall)           │
//          │  City name + temp (bold), description + feels    │
//          │  like (tiny), humidity + wind speed (tiny)       │
//    Y=60  ├──────────────────────────────────────────────────┤
//          │  Band 2 — Hourly forecast (64 px)                │
//          │  8 columns × 100 px: time / temp / condition     │
//    Y=124 ├──────────────────────────────────────────────────┤
//          │  Band 3 — 3-day forecast (64 px)                 │
//          │  3 columns × 266 px: day / H:xx° L:xx° / desc   │
//    Y=188 ╠══════════════════════════════════════════════════╣ (2px rule)
//          │  Band 4 — Recent SMS messages (292 px)           │
//          │  Sender + timestamp, word-wrapped body,          │
//          │  dashed dividers between messages                │
//    Y=480 └──────────────────────────────────────────────────┘
//
//  Fonts in use:
//    nullptr (built-in)  — 6×8 px monospace; used for most text
//    FreeMono9pt7b       — proportional ~13 px cap-height
//    FreeMonoBold9pt7b   — bold variant of FreeMono9pt
//
//  The GxEPD2 library uses a page-buffer pattern: setFullWindow()
//  + firstPage() / nextPage() loop.  Every draw call inside the
//  loop writes to an internal buffer which is flushed to the
//  e-paper controller in chunks.  We redraw the entire screen on
//  every cycle (no partial updates), which gives the cleanest
//  result on IT8951-based panels.
// ============================================================


// DisplayType alias — keeps the long template instantiation out of
// every file that includes this header.
using DisplayType = GxEPD2_BW<GxEPD2_it8951<800, 480>, GxEPD2_it8951<800, 480>::HEIGHT>;


// ------------------------------------------------------------
//  uiDraw — render the complete dashboard to the display
//
//  Clears the screen to white, then draws all four bands in a
//  single GxEPD2 page loop.
//
//  Error display:
//    • If weather.valid is false, the current-weather band shows
//      "Weather error: <weather.errorMsg>" instead of data.
//    • If forecast.valid is false, the hourly/daily bands show
//      "Forecast error: <forecast.errorMsg>".
//    • If twilioError is non-empty (twilioFetchMessages returned -1),
//      the messages band shows "Messages error: <twilioError>".
//
//  Parameters:
//    display      — GxEPD2 display object (must be initialised)
//    weather      — current conditions from weatherFetch()
//    forecast     — hourly + daily forecast from forecastFetch()
//    messages[]   — array of TwilioMessage structs from twilioFetchMessages()
//    messageCount — number of valid entries in messages[] (0 on error)
//    twilioError  — error string from twilioFetchMessages; empty on success
//    contactBook  — address book from Credentials::contactBook;
//                   format: "Name|+E164,Name2|+E164" — used to replace raw
//                   phone numbers with friendly names in the messages band.
//                   Pass an empty string to show raw numbers.
// ------------------------------------------------------------
void uiDraw(DisplayType           &display,
            const WeatherData     &weather,
            const ForecastData    &forecast,
            const TwilioMessage    messages[],
            int                    messageCount,
            const String          &twilioError,
            const String          &contactBook);
