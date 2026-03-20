#include "ui_renderer.h"
#include <Fonts/FreeMono9pt7b.h>
#include <Fonts/FreeMonoBold9pt7b.h>

// Colour aliases to keep draw calls readable
#define C_BLACK GxEPD_BLACK
#define C_WHITE GxEPD_WHITE


// ============================================================
//  Layout constants
//
//  All Y values are the top pixel of each band.  The display is
//  800 × 480; bands are arranged top-to-bottom with thin horizontal
//  rules as dividers.
//
//  Band heights:
//    Current weather : Y_CURRENT  → Y_HOURLY  =  60 px
//    Hourly forecast : Y_HOURLY   → Y_DAILY   =  64 px
//    Daily forecast  : Y_DAILY    → Y_DIVIDER =  64 px
//    [thick rule at Y_DIVIDER, 2 px]
//    Messages        : Y_MESSAGES → 480       = 288 px
// ============================================================
static const int Y_CURRENT  =   0;
static const int Y_HOURLY   =  60;
static const int Y_DAILY    = 124;
static const int Y_DIVIDER  = 188;   // start of the 2-px thick separator rule
static const int Y_MESSAGES = 192;   // first content pixel below the divider

static const int MX = 8;   // horizontal margin — applied left and right


// ============================================================
//  Font helpers
//
//  Three text sizes are used:
//    "tiny"  — the display's built-in 6×8 pixel monospace bitmap
//              font (nullptr).  Fast to draw, no extra flash.
//              Baseline is 7 px below the cursor Y.
//    "small" — FreeMono9pt7b: antialiased ~13 px cap-height.
//    "bold"  — FreeMonoBold9pt7b: same size, heavier weight.
//              Used for section headings and the temperature.
//
//  Each helper sets the font AND the text colour so callers
//  don't have to repeat setTextColor() before every draw.
// ============================================================
static void setTiny(DisplayType &d)  { d.setFont(nullptr);            d.setTextColor(C_BLACK); }
static void setSmall(DisplayType &d) { d.setFont(&FreeMono9pt7b);     d.setTextColor(C_BLACK); }
static void setBold(DisplayType &d)  { d.setFont(&FreeMonoBold9pt7b); d.setTextColor(C_BLACK); }

// Convenience wrappers: set font + cursor, then print
static void tinyAt(DisplayType &d, int x, int y, const char *s)
{
    setTiny(d);
    d.setCursor(x, y);
    d.print(s);
}
// String overload so callers don't have to call .c_str() everywhere
static void tinyAt(DisplayType &d, int x, int y, const String &s) { tinyAt(d, x, y, s.c_str()); }

static void boldAt(DisplayType &d, int x, int y, const char *s)
{
    setBold(d);
    d.setCursor(x, y);
    d.print(s);
}

static void smallAt(DisplayType &d, int x, int y, const char *s)
{
    setSmall(d);
    d.setCursor(x, y);
    d.print(s);
}
static void smallAt(DisplayType &d, int x, int y, const String &s) { smallAt(d, x, y, s.c_str()); }


// ------------------------------------------------------------
//  hRule — draw a horizontal rule spanning the full display width
//           (minus the side margins MX on each side)
//
//  `thickness` draws multiple stacked lines for a heavier rule.
//  The 2-px divider above the messages panel uses thickness=2.
// ------------------------------------------------------------
static void hRule(DisplayType &d, int y, int thickness = 1)
{
    for (int t = 0; t < thickness; t++)
        d.drawFastHLine(MX, y + t, 800 - 2 * MX, C_BLACK);
}


// ============================================================
//  wrapTiny — word-wrap a string using the tiny (6-px) font
//
//  Breaks `text` into lines that fit within `maxW` pixels.
//  Prefers to break at the last space before the column limit;
//  falls back to hard-breaking at the character boundary if
//  there is no space (e.g. a very long URL).
//
//  Parameters:
//    text   — the string to wrap
//    x, y   — top-left position of the first line
//    maxW   — maximum line width in pixels
//    lineH  — vertical advance per line in pixels
//
//  Returns the Y coordinate of the pixel immediately below the
//  last line drawn, so the caller can continue placing content.
// ============================================================
static int wrapTiny(DisplayType &d, const String &text, int x, int y,
                    int maxW, int lineH)
{
    const int charW = 6;             // tiny font: every glyph is 6 px wide
    int cpl = maxW / charW;          // characters per line
    if (cpl < 1) cpl = 1;

    int start = 0;
    while (start < (int)text.length()) {
        int end = start + cpl;
        if (end >= (int)text.length()) {
            end = text.length();     // last line — take what's left
        } else {
            // Try to break at a word boundary (last space within the limit)
            int sp = text.lastIndexOf(' ', end);
            if (sp > start) end = sp + 1;   // include the space (trim() removes it)
        }
        String line = text.substring(start, end);
        line.trim();        // remove the trailing space at the word break
        tinyAt(d, x, y, line);
        y += lineH;
        start = end;
    }
    return y;   // Y position right after the last line
}


// ------------------------------------------------------------
//  resolveContact — look up a friendly name for a phone number
//
//  Searches `book` (format: "Name|+E164,Name2|+E164") for an entry
//  whose number matches `number`.  Returns the name if found, or
//  `number` unchanged if not — so the display always shows something.
// ------------------------------------------------------------
static String resolveContact(const String &number, const String &book)
{
    if (book.isEmpty()) return number;   // no address book — show raw number

    int start = 0;
    while (start < (int)book.length()) {
        int comma = book.indexOf(',', start);
        if (comma < 0) comma = book.length();   // last entry

        String entry = book.substring(start, comma);
        int pipe = entry.indexOf('|');
        if (pipe > 0) {
            String entryNumber = entry.substring(pipe + 1);
            entryNumber.trim();
            if (entryNumber == number) {
                return entry.substring(0, pipe);   // return the friendly name
            }
        }
        start = comma + 1;
    }
    return number;   // no match — fall back to raw E.164
}

// ------------------------------------------------------------
//  shortDesc — truncate a description to fit a narrow column
//
//  Tries to cut at a word boundary; falls back to a hard cut.
//  Used in the hourly and daily forecast columns where space is
//  tight.
// ------------------------------------------------------------
static String shortDesc(const String &desc, int maxChars)
{
    if ((int)desc.length() <= maxChars) return desc;   // already fits
    int sp = desc.lastIndexOf(' ', maxChars - 1);       // last space before limit
    if (sp > 0) return desc.substring(0, sp);
    return desc.substring(0, maxChars);                 // hard cut
}


// ============================================================
//  drawCurrent — Band 1: current weather conditions
//
//  Layout within the band (Y_CURRENT = 0, height = 60 px):
//
//    baseline +14  [BOLD]  "New York  72.3°F"
//    baseline +30  [tiny]  "light rain  feels 69.1°F"
//    baseline +42  [tiny]  "Humidity 85%   Wind 2.3 m/s"
//
//  If weather.valid is false (API call failed), show a short
//  "Weather unavailable" message and return early.
// ============================================================
static void drawCurrent(DisplayType &d, const WeatherData &w)
{
    if (!w.valid) {
        // Show a two-line error: label on the first line, the specific
        // error code/message (e.g. "HTTP 401") on the second line so the
        // user can diagnose the problem without connecting a serial monitor.
        tinyAt(d, MX, Y_CURRENT + 10, "Weather error:");
        tinyAt(d, MX, Y_CURRENT + 22, w.errorMsg.isEmpty() ? "unknown" : w.errorMsg);
        return;
    }

    char buf[96];

    // Line 1: city name + current temperature, bold for prominence
    snprintf(buf, sizeof(buf), "%s  %.1f°F", w.city.c_str(), w.tempF);
    boldAt(d, MX, Y_CURRENT + 14, buf);

    // Line 2: weather description + apparent temperature
    snprintf(buf, sizeof(buf), "%s  feels %.1f°F", w.description.c_str(), w.feelsLikeF);
    tinyAt(d, MX, Y_CURRENT + 30, buf);

    // Line 3: humidity percentage + wind speed
    snprintf(buf, sizeof(buf), "Humidity %d%%   Wind %.1f m/s", w.humidityPct, w.windSpeedMs);
    tinyAt(d, MX, Y_CURRENT + 42, buf);
}


// ============================================================
//  drawHourly — Band 2: 8-column hourly forecast grid
//
//  The band spans Y_HOURLY (60) to Y_DAILY (124) = 64 px.
//  It is divided into 8 equal columns of 100 px each:
//
//    col 0: x=0..99    col 1: x=100..199   …   col 7: x=700..799
//
//  Within each column:
//    +12 px  time label  "15:00"  (tiny, centred)
//    +29 px  temperature "72°"    (bold, centred)
//    +43 px  condition   "Rain"   (tiny, centred, truncated to 9 chars)
//
//  Thin vertical lines separate adjacent columns.
// ============================================================
static void drawHourly(DisplayType &d, const ForecastData &f)
{
    hRule(d, Y_HOURLY);   // top rule of this band

    const int slotW = 800 / HOURLY_SLOTS;   // 100 px per column

    for (int i = 0; i < HOURLY_SLOTS; i++) {
        const HourlyForecast &h = f.hourly[i];
        int x  = i * slotW;
        int cx = x + slotW / 2;   // horizontal centre of this column

        // Format the time as "HH:00"
        char timeBuf[6];
        snprintf(timeBuf, sizeof(timeBuf), "%02d:00", h.hour);

        // Time label — the tiny font is 6 px/char; 5 chars = 30 px wide.
        // Offset by 15 from centre to visually centre the string.
        tinyAt(d, cx - 15, Y_HOURLY + 12, timeBuf);

        // Temperature — bold for readability; "72°" is ~28 px wide
        char tempBuf[8];
        snprintf(tempBuf, sizeof(tempBuf), "%.0f°", h.tempF);
        boldAt(d, cx - 14, Y_HOURLY + 29, tempBuf);

        // Short weather category — truncate to 9 chars, rough-centred
        String sd = shortDesc(h.shortDesc, 9);
        int sdX = cx - (int)(sd.length() * 3);   // 3 = half of 6-px char width
        tinyAt(d, sdX, Y_HOURLY + 43, sd);

        // Vertical column divider (skip after the last column)
        if (i < HOURLY_SLOTS - 1)
            d.drawFastVLine(x + slotW - 1, Y_HOURLY + 1, 62, C_BLACK);
    }
}


// ============================================================
//  drawDaily — Band 3: 3-column daily forecast
//
//  The band spans Y_DAILY (124) to Y_DIVIDER (188) = 64 px.
//  It is divided into 3 columns of 266 px each.
//
//  Within each column (left-aligned with MX margin):
//    +16 px  day name  "Mon"          (bold)
//    +33 px  temps     "H:78°  L:62°" (tiny)
//    +47 px  desc      "light rain"   (tiny, truncated to 26 chars)
// ============================================================
static void drawDaily(DisplayType &d, const ForecastData &f)
{
    hRule(d, Y_DAILY);   // top rule of this band

    const int slotW = 800 / DAILY_SLOTS;   // 266 px per column

    for (int i = 0; i < DAILY_SLOTS && i < 3; i++) {
        const DailyForecast &day = f.daily[i];
        int x  = i * slotW + MX;   // left edge with margin
        int y0 = Y_DAILY;

        // Day abbreviation — bold
        boldAt(d, x, y0 + 16, day.dayName.c_str());

        // High / low on one line — format matches "H:78°  L:62°"
        char buf[32];
        snprintf(buf, sizeof(buf), "H:%.0f°  L:%.0f°", day.highF, day.lowF);
        tinyAt(d, x, y0 + 33, buf);

        // Weather description, truncated to fit the column (~26 chars at 6 px/char)
        String sd = shortDesc(day.desc, 26);
        tinyAt(d, x, y0 + 47, sd);

        // Vertical divider between columns
        if (i < DAILY_SLOTS - 1)
            d.drawFastVLine(i * slotW + slotW - 1, Y_DAILY + 1, 62, C_BLACK);
    }
}


// ============================================================
//  drawMessages — Band 4: recent SMS messages
//
//  Starts at Y_MESSAGES (192) and fills to the bottom of the
//  screen (480 px).  Each message occupies:
//    • One tiny-font line: "from  DD Mon HH:MM"
//    • One or more wrapped lines: message body (indented 10 px)
//    • A dashed horizontal rule between consecutive messages
//
//  The dashed rule is drawn as individual pixels spaced 6 px
//  apart (simpler than a dash pattern but effective on e-paper).
//
//  If count == 0, prints "No messages" and returns.
//  Rendering stops when y would exceed 476 px (bottom margin).
// ============================================================
static void drawMessages(DisplayType &d, const TwilioMessage *msgs, int count,
                         const String &contactBook)
{
    // 2-px thick horizontal divider at the top of this section
    hRule(d, Y_DIVIDER, 2);

    // Section heading
    boldAt(d, MX, Y_MESSAGES + 14, "Recent Messages");
    hRule(d, Y_MESSAGES + 17);   // thin rule under the heading

    const int LINE_H  = 10;   // pixel height of one tiny-font line
    const int MSG_GAP =  4;   // vertical gap between message body and next divider

    int y = Y_MESSAGES + 22;   // start below the heading rule

    if (count == 0) {
        tinyAt(d, MX, y, "No messages");
        return;
    }
    // Note: the error case (count == 0 with a non-empty twilioError) is
    // handled by drawMessages' caller (uiDraw), which passes count == 0
    // only after already printing the error line above the message list.

    for (int i = 0; i < count; i++) {
        if (y > 476) break;   // no room for more messages

        // Meta line: resolved sender name (or raw number if not in address book)
        // + truncated date.  dateSent example: "Wed, 17 Mar 2026 14:32:00 +0000"
        // substring(5, 22) extracts "17 Mar 2026 14:32" (17 chars).
        String sender = resolveContact(msgs[i].from, contactBook);
        String meta = sender + "  " + msgs[i].dateSent.substring(5, 22);
        tinyAt(d, MX, y, meta);
        y += LINE_H;

        // Message body — word-wrapped, indented 10 px from the left margin
        // to visually separate it from the meta line above
        y = wrapTiny(d, msgs[i].body, MX + 10, y,
                     800 - 2 * MX - 10, LINE_H);
        y += MSG_GAP;

        // Dashed divider between messages (not after the last one)
        if (i < count - 1) {
            for (int px = MX * 3; px < 800 - MX * 3; px += 6)
                d.drawPixel(px, y, C_BLACK);
            y += 6;
        }
    }
}


// ============================================================
//  uiDraw — public entry point: render the full dashboard
//
//  GxEPD2 page-buffer pattern:
//    setFullWindow()    — next refresh covers the entire screen
//    firstPage()        — allocates the page buffer, returns true
//    do { … draw … }
//    while (nextPage()) — flushes buffer to display, returns true
//                         if more pages remain; false when done
//
//  All draw calls must happen inside the do-while loop.
//  Drawing outside the loop has no effect.
//
//  Graceful degradation: if forecast data is unavailable (API
//  error), placeholder text is drawn instead of crashing.
// ============================================================
void uiDraw(DisplayType           &display,
            const WeatherData     &weather,
            const ForecastData    &forecast,
            const TwilioMessage    messages[],
            int                    messageCount,
            const String          &twilioError,
            const String          &contactBook)
{
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(C_WHITE);   // clear entire display to white

        // Band 1: current conditions (or error message if fetch failed)
        drawCurrent(display, weather);

        if (forecast.valid) {
            drawHourly(display, forecast);   // Band 2: hourly grid
            drawDaily(display, forecast);    // Band 3: daily grid
        } else {
            // Forecast fetch failed — draw ruled empty bands and show the
            // specific error (e.g. "HTTP 401") so it's visible on the screen.
            hRule(display, Y_HOURLY);
            tinyAt(display, MX, Y_HOURLY + 12, "Forecast error:");
            tinyAt(display, MX, Y_HOURLY + 24, forecast.errorMsg.isEmpty()
                                                  ? "unknown"
                                                  : forecast.errorMsg);
            hRule(display, Y_DAILY);
        }

        // Band 4: SMS messages, or an error line if the Twilio fetch failed.
        // drawMessages() shows "No messages" when messageCount == 0 and
        // twilioError is empty; here we pre-print the error above that so
        // the user can distinguish "no messages" from "fetch failed".
        if (!twilioError.isEmpty()) {
            // Draw the section header and the error line, then fall through
            // to drawMessages() with count=0 so the rest of the band is drawn.
            hRule(display, Y_DIVIDER, 2);
            boldAt(display, MX, Y_MESSAGES + 14, "Recent Messages");
            hRule(display, Y_MESSAGES + 17);
            tinyAt(display, MX, Y_MESSAGES + 28, "Messages error:");
            tinyAt(display, MX, Y_MESSAGES + 40, twilioError);
        } else {
            drawMessages(display, messages, messageCount, contactBook);
        }

    } while (display.nextPage());
}
