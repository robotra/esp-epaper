# ESP32 E-Paper Dashboard

A battery-friendly smart dashboard built on an ESP32-S3 and a Waveshare 3.97" 800×480 e-paper display. It wakes every 14 minutes, fetches live weather data from OpenWeatherMap and recent SMS messages from Twilio, renders everything to the display, then goes back to deep sleep.

---

## Features

- **Current weather** — city, temperature (°F), feels-like, humidity, wind speed
- **Last updated timestamp** — NTP-synced time shown top-right of the weather band
- **Hourly forecast** — next 8 × 3-hour slots (~24 h ahead)
- **3-day forecast** — high/low/description for the next 3 full calendar days
- **Recent SMS messages** — 3 most-recent inbound Twilio messages, with optional sender whitelist
- **Address book** — map E.164 numbers to friendly names on the display
- **QR code setup screen** — on first boot a scannable WiFi QR code is shown; point your phone camera at it to join the setup network instantly
- **Captive-portal setup** — first-boot WiFi, API keys, and timezone configuration via browser (no flashing secrets)
- **3-power-cycle reset** — power-cycle 3 times within 3 seconds to clear credentials and re-run setup
- **Deep sleep between refreshes** — ~10–20 µA idle current; 14-minute refresh interval

---

## Hardware

| Component | Details |
|---|---|
| Microcontroller | ESP32-S3-DevKitM-1 |
| Display | Waveshare 3.97" 800×480 e-paper (direct SPI, EPD_3IN97 driver) |
| Interface | Bit-bang SPI |

### Pin Wiring

| Signal | ESP32-S3 GPIO |
|---|---|
| SPI CLK | 11 |
| SPI MOSI | 12 |
| Display CS | 10 |
| Display RST | 46 |
| Display DC | 9 |
| Display BUSY | 3 |

> All pins are defined in [src/DEV_Config.h](src/DEV_Config.h).

---

## Getting Started

### 1. Prerequisites

- [PlatformIO](https://platformio.org/) (VS Code extension or CLI)
- An [OpenWeatherMap](https://openweathermap.org/api) account with a free API key ("Current Weather Data" plan)
- A [Twilio](https://www.twilio.com/) account with a phone number capable of receiving SMS

### 2. Build and Flash

No credential configuration is needed before flashing — all secrets are entered at runtime.

```bash
pio run --target upload
```

Or use the PlatformIO toolbar: **Build** → **Upload**.

### 3. First-Boot Setup

On the very first boot (or after a credential reset) the device starts in setup mode:

1. The e-paper display shows a **WiFi QR code** (left half) and plain-text instructions (right half).
2. The ESP32 creates an open WiFi network named **`EPaper-Setup`**.
3. **Scan the QR code** with your phone camera — it connects automatically. Or join manually from WiFi settings.
4. Your device should automatically open the setup page (captive portal). If not, navigate to **http://192.168.4.1**.
5. Fill in:
   - **Wi-Fi** — your home network SSID and password
   - **Twilio** — Account SID, Auth Token, and your Twilio phone number in E.164 format (e.g. `+15550001234`)
   - **SMS whitelist** *(optional)* — one E.164 number per line; only messages from these senders will be shown
   - **Address book** *(optional)* — one `Name: +E164number` entry per line; names replace raw numbers on the display
   - **Time zone** — select your region from the dropdown for correct "last updated" timestamps
   - **OpenWeatherMap** — API key and city name (or `lat=XX.X&lon=YY.Y` for GPS)
6. Click **Save & Restart**. The device reboots and begins normal operation.

> The setup portal times out after **5 minutes** and restarts if no credentials are submitted.

### 4. Reset / Reconfigure

To clear all saved credentials and re-run setup, **power-cycle the device 3 times within ~3 seconds**:

1. Unplug power (or press reset)
2. Unplug again within 3 seconds
3. Unplug again within 3 seconds → device clears credentials and opens the portal

Useful if your WiFi password changes or you want to reconfigure API keys or timezone.

---

## Display Layout

```
Y=0   ┌─────────────────────────────────────────────────────────┐
      │  New York  65.2°F                       28 Mar 14:32    │  ← Font24 / Font20
      │  scattered clouds  feels 62.5°F                         │  ← Font20
      │  Humidity 62%   Wind 7.2 m/s                            │  ← Font20
Y=84  ├──────────┬──────────┬──────────┬───  ···  ───┬──────────┤
      │  12:00   │  15:00   │  18:00   │             │  09:00   │
      │   65F    │   63F    │   59F    │             │   61F    │
      │  Clouds  │   Rain   │   Rain   │             │   Sun    │
Y=162 ├──────────────────────┬──────────────────────┬───────────┤
      │  Wed                 │  Thu                 │  Fri      │
      │  H:67F  L:54F        │  H:63F  L:52F        │  H:72F   │
      │  Partly cloudy       │  Light rain          │  Sunny   │
Y=234 ╠═════════════════════════════════════════════════════════╣
      │  Recent Messages                                        │
      │  Alice  18 Mar 2026 09:14                               │
      │    Hey! Reminder: meeting at 3 pm today.                │
      │ · · · · · · · · · · · · · · · ·                        │
      │  Bob  17 Mar 2026 22:03                                 │
      │    Verification code: 482917.                           │
Y=480 └─────────────────────────────────────────────────────────┘
```

Open [preview/screen_preview.html](preview/screen_preview.html) in a browser at 100% zoom for a pixel-accurate mockup.

---

## Project Structure

```
esp32_epaper/
├── platformio.ini               # Build config: ESP32-S3-DevKitM-1, ArduinoJson, QRCode
├── src/
│   ├── main.cpp                 # Setup, sleep cycle, 3-cycle reset, orchestration
│   ├── config.h                 # Sleep duration, SMS count limit, fallback timezone
│   ├── DEV_Config.h/.cpp        # Waveshare GPIO + bit-bang SPI init (pin assignments here)
│   ├── EPD_3in97.h/.cpp         # Waveshare direct e-paper panel driver
│   ├── GUI_Paint.h/.cpp         # Waveshare framebuffer drawing API
│   ├── fonts.h + font*.cpp      # Bitmap font tables (Font8, Font12, Font16, Font20, Font24)
│   ├── Debug.h                  # Serial debug macro
│   ├── credentials_store.h/.cpp # NVS read/write/clear for all secrets + timezone
│   ├── config_portal.h/.cpp     # Captive-portal AP + web server (includes timezone dropdown)
│   ├── weather_client.h/.cpp    # OpenWeatherMap current + forecast API
│   ├── twilio_client.h/.cpp     # Twilio SMS list API
│   └── ui_renderer.h/.cpp       # E-paper layout and drawing
└── preview/
    └── screen_preview.html      # Browser mockup of the dashboard
```

---

## Configuration Reference

| Constant | Default | Description |
|---|---|---|
| `TWILIO_MAX_SHOW` | `3` | Number of SMS messages to fetch and display |
| `SLEEP_DURATION_US` | `14 * 60 * 1,000,000` | Deep-sleep interval in microseconds |
| `TIMEZONE` | `EST5EDT,M3.2.0,M11.1.0` | Fallback POSIX TZ string if none saved in NVS |

Timezone is normally set via the setup portal dropdown and stored in NVS — the `TIMEZONE` define in [src/config.h](src/config.h) is only used if NVS has no saved value.

Pin assignments and SPI configuration are in [src/DEV_Config.h](src/DEV_Config.h).

---

## Dependencies

Managed automatically by PlatformIO via `platformio.ini`:

| Library | Purpose |
|---|---|
| `bblanchon/ArduinoJson ^7.0.0` | JSON parsing for API responses |
| `ricmoo/QRCode ^0.0.1` | QR code module generation for the setup screen |
| `WiFiClientSecure` | TLS/HTTPS (built into ESP32 core) |
| `WebServer`, `DNSServer` | Captive portal (built into ESP32 core) |
| `Preferences` | NVS credential storage (built into ESP32 core) |

The Waveshare EPD driver (`EPD_3in97`, `GUI_Paint`, `DEV_Config`) is copied directly into `src/` — no library manager needed.

---

## Security Notes

- Credentials are **never compiled into the firmware** — entered at runtime, stored in NVS.
- Passwords (WiFi, Twilio Auth Token, OWM key) are **never echoed back** by the portal form.
- The setup AP is an open network by design (to allow any device to connect without a password).
- The Twilio Auth Token and OWM API key are transmitted over HTTPS only.

---

## API Error Display

When any data fetch fails, the specific error is shown on the display — no serial monitor required.

| Section | Error shown when… | Example |
|---|---|---|
| Current weather | `weatherFetch()` returns false | `Weather error: HTTP 401` |
| Hourly / daily | `forecastFetch()` returns false | `Forecast error: HTTP 404` |
| Messages | `twilioFetchMessages()` returns -1 | `Messages error: HTTP 403` |

HTTP 401 = bad API key · HTTP 404 = city not found · HTTP 429 = rate limit exceeded

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| Setup screen on every boot | Credentials not saving | Check serial output; try full flash erase |
| "WiFi timeout" on screen | Wrong SSID or password | Triple power-cycle → re-enter via portal |
| `Weather error: HTTP 401` | Invalid OWM API key | Triple power-cycle → re-enter correct key |
| `Weather error: HTTP 404` | City name not recognised | Use a valid name or `lat=XX.X&lon=YY.Y` |
| `Messages error: HTTP 401` | Wrong Twilio credentials | Triple power-cycle → re-enter credentials |
| Timestamp shows wrong time | Wrong timezone set | Triple power-cycle → select correct timezone |
| No messages shown | No inbound messages or whitelist too strict | Check whitelist; send a test SMS |
| Triple power-cycle not triggering | Cycling too slowly | Each cycle must happen within 3 s; watch serial for `Boot count:` |
| Portal page doesn't auto-open | Captive-portal detection varies by OS | Navigate manually to http://192.168.4.1 |
