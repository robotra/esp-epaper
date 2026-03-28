#include "ui_renderer.h"

// ============================================================
//  Layout constants
//
//  Fonts:
//    Font20 — 14 px wide × 20 px tall — body text
//    Font24 — 17 px wide × 24 px tall — headings / temperature
//
//  Band heights:
//    Band 1 current weather  : Y=0   → Y=84   (84 px)
//    Band 2 hourly forecast  : Y=84  → Y=162  (78 px)
//    Band 3 daily forecast   : Y=162 → Y=234  (72 px)
//    2-px rule at Y=234
//    Band 4 messages         : Y=236 → Y=480  (244 px)
// ============================================================
static const int Y_CURRENT  =   0;
static const int Y_HOURLY   =  84;
static const int Y_DAILY    = 162;
static const int Y_DIVIDER  = 234;
static const int Y_MESSAGES = 236;

static const int MX = 6;


// ============================================================
//  Drawing helpers
// ============================================================
static void hLine(int x, int y, int w)
{
    Paint_DrawLine(x, y, x + w - 1, y, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
}

static void vLine(int x, int y, int h)
{
    Paint_DrawLine(x, y, x, y + h - 1, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
}

static void hRule(int y, int thickness = 1)
{
    for (int t = 0; t < thickness; t++)
        hLine(MX, y + t, 800 - 2 * MX);
}

// Body text — Font20 (14×20)
static void bodyAt(int x, int y, const char *s)
{
    Paint_DrawString_EN(x, y, s, &Font20, WHITE, BLACK);
}
static void bodyAt(int x, int y, const String &s) { bodyAt(x, y, s.c_str()); }

// Heading text — Font24 (17×24)
static void headAt(int x, int y, const char *s)
{
    Paint_DrawString_EN(x, y, s, &Font24, WHITE, BLACK);
}


// ============================================================
//  wrapBody — word-wrap using Font20 (14 px/char)
// ============================================================
static int wrapBody(const String &text, int x, int y, int maxW, int lineH)
{
    const int charW = 14;
    int cpl = maxW / charW;
    if (cpl < 1) cpl = 1;

    int start = 0;
    while (start < (int)text.length()) {
        int end = start + cpl;
        if (end >= (int)text.length()) {
            end = text.length();
        } else {
            int sp = text.lastIndexOf(' ', end);
            if (sp > start) end = sp + 1;
        }
        String line = text.substring(start, end);
        line.trim();
        bodyAt(x, y, line);
        y += lineH;
        start = end;
    }
    return y;
}


// ------------------------------------------------------------
//  resolveContact — look up a friendly name for a phone number
// ------------------------------------------------------------
static String resolveContact(const String &number, const String &book)
{
    if (book.isEmpty()) return number;

    int start = 0;
    while (start < (int)book.length()) {
        int comma = book.indexOf(',', start);
        if (comma < 0) comma = book.length();

        String entry = book.substring(start, comma);
        int pipe = entry.indexOf('|');
        if (pipe > 0) {
            String entryNumber = entry.substring(pipe + 1);
            entryNumber.trim();
            if (entryNumber == number)
                return entry.substring(0, pipe);
        }
        start = comma + 1;
    }
    return number;
}


// ------------------------------------------------------------
//  shortDesc — truncate a description to fit a narrow column
// ------------------------------------------------------------
static String shortDesc(const String &desc, int maxChars)
{
    if ((int)desc.length() <= maxChars) return desc;
    int sp = desc.lastIndexOf(' ', maxChars - 1);
    if (sp > 0) return desc.substring(0, sp);
    return desc.substring(0, maxChars);
}


// ============================================================
//  drawCurrent — Band 1: current weather (84 px)
//
//    +2   Font24  "City  72.3°F"
//    +28  Font20  "light rain  feels 69.1°F"
//    +52  Font20  "Humidity 85%   Wind 2.3 m/s"
// ============================================================
static void drawCurrent(const WeatherData &w, const String &lastUpdated)
{
    // "last updated" right-aligned in top-right — Font20 (14 px/char)
    int tsX = 800 - MX - (int)(lastUpdated.length() * 14);
    bodyAt(tsX, Y_CURRENT + 2, lastUpdated);

    if (!w.valid) {
        bodyAt(MX, Y_CURRENT + 2,  "Weather error:");
        bodyAt(MX, Y_CURRENT + 24, w.errorMsg.isEmpty() ? "unknown" : w.errorMsg);
        return;
    }

    char buf[96];

    snprintf(buf, sizeof(buf), "%s  %.1f F", w.city.c_str(), w.tempF);
    headAt(MX, Y_CURRENT + 2, buf);

    snprintf(buf, sizeof(buf), "%s  feels %.1f F", w.description.c_str(), w.feelsLikeF);
    bodyAt(MX, Y_CURRENT + 30, buf);

    snprintf(buf, sizeof(buf), "Humidity %d%%   Wind %.1f m/s", w.humidityPct, w.windSpeedMs);
    bodyAt(MX, Y_CURRENT + 54, buf);
}


// ============================================================
//  drawHourly — Band 2: 8-column hourly forecast (78 px)
//
//  Each 100 px column:
//    +2   Font20  "15:00"
//    +24  Font24  "72F"
//    +50  Font20  "Rain"
// ============================================================
static void drawHourly(const ForecastData &f)
{
    hRule(Y_HOURLY);

    const int slotW = 800 / HOURLY_SLOTS;   // 100 px per column

    for (int i = 0; i < HOURLY_SLOTS; i++) {
        const HourlyForecast &h = f.hourly[i];
        int x  = i * slotW;
        int cx = x + slotW / 2;

        char timeBuf[6];
        snprintf(timeBuf, sizeof(timeBuf), "%02d:00", h.hour);
        // Font20: 14px wide; 5 chars = 70 px; centre: cx-35
        bodyAt(cx - 35, Y_HOURLY + 2, timeBuf);

        char tempBuf[8];
        snprintf(tempBuf, sizeof(tempBuf), "%.0fF", h.tempF);
        // Font24: 17px wide; 3 chars = 51 px; centre: cx-25
        headAt(cx - 25, Y_HOURLY + 25, tempBuf);

        // Condition: truncate to 6 chars (6×14=84 px fits in 100 px slot)
        String sd = shortDesc(h.shortDesc, 6);
        int sdX = cx - (int)(sd.length() * 7);   // 7 = half of 14 px
        bodyAt(sdX, Y_HOURLY + 52, sd);

        if (i < HOURLY_SLOTS - 1)
            vLine(x + slotW - 1, Y_HOURLY + 1, 76);
    }
}


// ============================================================
//  drawDaily — Band 3: 3-column daily forecast (72 px)
//
//  Each 266 px column:
//    +2   Font24  "Mon"
//    +28  Font20  "H:78F  L:62F"
//    +52  Font20  "light rain"
// ============================================================
static void drawDaily(const ForecastData &f)
{
    hRule(Y_DAILY);

    const int slotW = 800 / DAILY_SLOTS;   // 266 px per column

    for (int i = 0; i < DAILY_SLOTS && i < 3; i++) {
        const DailyForecast &day = f.daily[i];
        int x  = i * slotW + MX;
        int y0 = Y_DAILY;

        headAt(x, y0 + 2, day.dayName.c_str());

        char buf[32];
        snprintf(buf, sizeof(buf), "H:%.0fF  L:%.0fF", day.highF, day.lowF);
        bodyAt(x, y0 + 30, buf);

        String sd = shortDesc(day.desc, 18);
        bodyAt(x, y0 + 52, sd);

        if (i < DAILY_SLOTS - 1)
            vLine(i * slotW + slotW - 1, Y_DAILY + 1, 70);
    }
}


// ============================================================
//  drawMessages — Band 4: up to 3 SMS messages (244 px)
// ============================================================
static void drawMessages(const TwilioMessage *msgs, int count,
                         const String &contactBook)
{
    hRule(Y_DIVIDER, 2);

    headAt(MX, Y_MESSAGES + 2, "Recent Messages");
    hRule(Y_MESSAGES + 28);

    const int LINE_H  = 22;   // Font20 height + 2 px leading
    const int MSG_GAP =  4;

    int y = Y_MESSAGES + 34;

    if (count == 0) {
        bodyAt(MX, y, "No messages");
        return;
    }

    for (int i = 0; i < count; i++) {
        if (y > 458) break;

        String sender = resolveContact(msgs[i].from, contactBook);
        String meta = sender + "  " + msgs[i].dateSent.substring(5, 22);
        bodyAt(MX, y, meta);
        y += LINE_H;

        y = wrapBody(msgs[i].body, MX + 14, y, 800 - 2 * MX - 14, LINE_H);
        y += MSG_GAP;

        if (i < count - 1) {
            for (int px = MX * 3; px < 800 - MX * 3; px += 6)
                Paint_DrawPoint(px, y, BLACK, DOT_PIXEL_1X1, DOT_STYLE_DFT);
            y += 8;
        }
    }
}


// ============================================================
//  uiDraw — public entry point
// ============================================================
void uiDraw(UBYTE                 *image,
            const WeatherData     &weather,
            const ForecastData    &forecast,
            const TwilioMessage    messages[],
            int                    messageCount,
            const String          &twilioError,
            const String          &contactBook,
            const String          &lastUpdated)
{
    Paint_SelectImage(image);
    Paint_Clear(WHITE);

    drawCurrent(weather, lastUpdated);

    if (forecast.valid) {
        drawHourly(forecast);
        drawDaily(forecast);
    } else {
        hRule(Y_HOURLY);
        bodyAt(MX, Y_HOURLY + 2,  "Forecast error:");
        bodyAt(MX, Y_HOURLY + 24, forecast.errorMsg.isEmpty() ? "unknown" : forecast.errorMsg);
        hRule(Y_DAILY);
    }

    if (!twilioError.isEmpty()) {
        hRule(Y_DIVIDER, 2);
        headAt(MX, Y_MESSAGES + 2, "Recent Messages");
        hRule(Y_MESSAGES + 28);
        bodyAt(MX, Y_MESSAGES + 34, "Messages error:");
        bodyAt(MX, Y_MESSAGES + 56, twilioError);
    } else {
        drawMessages(messages, messageCount, contactBook);
    }

    EPD_3IN97_Display_Base(image);
}
