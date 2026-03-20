# ESP32 E-Paper Dashboard

A battery-friendly smart dashboard built on an ESP32-S3 and a 3.97" IT8951 e-paper display. It wakes every 14 minutes, fetches live weather data from OpenWeatherMap and recent SMS messages from Twilio, renders everything to the display, then goes back to deep sleep.

---

## Features

- **Current weather** — city, temperature (°F), feels-like, humidity, wind speed
- **Hourly forecast** — next 8 × 3-hour slots (~24 h ahead)
- **3-day forecast** — high/low/description for the next 3 full calendar days
- **Recent SMS messages** — up to 7 most-recent inbound Twilio messages, with optional sender whitelist
- **Address book** — map E.164 numbers to friendly names; names appear on the display instead of raw numbers
- **QR code setup screen** — on first boot a scannable WiFi QR code is shown on the display; point your phone camera at it to join the setup network instantly
- **Captive-portal setup** — first-boot WiFi and API configuration via browser (no flashing secrets)
- **3-power-cycle reset** — power-cycle 3 times to clear credentials and re-run setup
- **Deep sleep between refreshes** — ~10–20 µA idle current; 14-minute refresh interval

---

## Hardware

| Component | Details |
|---|---|
| Microcontroller | ESP32-S3-DevKitC-1 |
| Display | 3.97" 800×480 IT8951 e-paper |
| Interface | SPI (HSPI) |

### Pin Wiring

| Signal | ESP32-S3 GPIO |
|---|---|
| SPI MOSI | 11 |
| SPI CLK | 12 |
| SPI MISO | 13 |
| Display CS | 10 |
| Display RST | 46 |
| Display BUSY | 3 |

> All pins are configurable in [src/config.h](src/config.h).

---

## Getting Started

### 1. Prerequisites

- [PlatformIO](https://platformio.org/) (VS Code extension or CLI)
- An [OpenWeatherMap](https://openweathermap.org/api) account with a free API key ("Current Weather Data" plan)
- A [Twilio](https://www.twilio.com/) account with a phone number capable of receiving SMS

### 2. Build and Flash

No credential configuration is needed before flashing — all secrets are entered at runtime.

```bash
# Clone and open in VS Code with PlatformIO, then:
pio run --target upload
```

Or use the PlatformIO toolbar: **Build** → **Upload**.

### 3. First-Boot Setup

On the very first boot (or after a credential reset) the device starts in setup mode:

1. The e-paper display shows a **WiFi QR code** (left half) and plain-text instructions (right half).
2. The ESP32 creates an open WiFi network named **`EPaper-Setup`**.
3. **Scan the QR code** with your phone camera — it encodes `WIFI:S:EPaper-Setup;T:nopass;;` and connects automatically. Alternatively, join the network manually from your device's WiFi settings.
4. Your device should automatically open the setup page (captive portal). If not, navigate to **http://192.168.4.1** manually.
5. Fill in:
   - **WiFi** — your home network SSID and password
   - **Twilio** — Account SID, Auth Token, and your Twilio phone number in E.164 format (e.g. `+15550001234`)
   - **SMS whitelist** *(optional)* — one E.164 number per line; only messages from these senders will be shown. Leave blank to show all.
   - **Address book** *(optional)* — one `Name: +E164number` entry per line (e.g. `Alice: +15550001234`). Names appear on the display instead of raw phone numbers.
   - **OpenWeatherMap** — API key and city name (or `lat=XX.X&lon=YY.Y` for GPS)
6. Click **Save & Restart**. The device reboots and begins normal operation.

> The setup portal times out after **5 minutes** and restarts the device if no credentials are submitted.

### 4. Reset / Reconfigure

To clear all saved credentials and re-run the setup wizard, **power-cycle the device 3 times within ~3 seconds**:

1. Unplug power (or press reset) → wait ~0.5 s → plug in again
2. Repeat twice more
3. The device clears its NVS credentials and opens the setup portal

This is useful if your WiFi password changes or you want to point the device at a different account.

---

## Display Layout

```
Y=0   ┌─────────────────────────────────────────────────────────┐
      │  New York  72.3°F                                       │  ← bold
      │  light rain  feels 69.1°F                               │  ← tiny
      │  Humidity 85%   Wind 2.3 m/s                            │  ← tiny
Y=60  ├──────────┬──────────┬──────────┬───  ···  ───┬──────────┤
      │  00:00   │  03:00   │  06:00   │             │  21:00   │
      │   70°    │   68°    │   65°    │             │   72°    │
      │  Clouds  │   Rain   │   Rain   │             │  Clear   │
Y=124 ├──────────────────────┬─────────────────────────────────-┤
      │  Mon                 │  Tue                 │  Wed      │
      │  H:78°  L:62°        │  H:75°  L:60°        │  H:80°   │
      │  light rain          │  overcast clouds     │  clear   │
Y=188 ╠═════════════════════════════════════════════════════════╣
      │  Recent Messages                                        │
      │  +15550001234  17 Mar 2026 14:32                        │
      │    Hey, how's it going? …                               │
      │ · · · · · · · · · · · · · · · ·                        │
      │  +15550005678  16 Mar 2026 09:15                        │
      │    Can you call me back?                                │
Y=480 └─────────────────────────────────────────────────────────┘
```

---

## Project Structure

```
esp32_epaper/
├── platformio.ini          # Build config: ESP32-S3, GxEPD2, ArduinoJson, QRCode
├── src/
│   ├── main.cpp            # Setup, QR screen, sleep cycle, orchestration
│   ├── config.h            # Pin assignments, sleep duration, VCOM voltage
│   ├── credentials_store.h/.cpp  # NVS read/write/clear for all secrets
│   ├── config_portal.h/.cpp      # Captive-portal AP + web server
│   ├── weather_client.h/.cpp     # OpenWeatherMap current + forecast API
│   ├── twilio_client.h/.cpp      # Twilio SMS list API
│   └── ui_renderer.h/.cpp        # E-paper layout and drawing
└── preview/
    ├── portal_preview.html        # Browser mockup of the setup form
    ├── screen_preview.html        # Browser mockup of the normal dashboard
    ├── setup_preview.html         # Browser mockup of the first-boot QR screen
    └── twilio_error_preview.html  # Browser mockup of the Twilio error state
```

---

## Configuration Reference

All tuneable constants live in [src/config.h](src/config.h):

| Constant | Default | Description |
|---|---|---|
| `TWILIO_MAX_SHOW` | `5` | Number of SMS messages to display |
| `SLEEP_DURATION_US` | `14 * 60 * 1,000,000` | Deep-sleep interval in microseconds |
| `EPAPER_CS` | `10` | SPI chip-select GPIO |
| `EPAPER_RST` | `46` | Display reset GPIO |
| `EPAPER_BUSY` | `3` | Display busy-signal GPIO |
| `EPAPER_VCOM_MV` | `-2000` | VCOM voltage in mV — read from your FPC cable label |

---

## Dependencies

Managed automatically by PlatformIO via `platformio.ini`:

| Library | Purpose |
|---|---|
| `ZinggJM/GxEPD2 ^1.6.0` | IT8951 e-paper driver + graphics |
| `bblanchon/ArduinoJson ^7.0.0` | JSON parsing for API responses |
| `ricmoo/QRCode ^0.0.1` | QR code module generation for the setup screen |
| `WiFiClientSecure` | TLS/HTTPS (built into ESP32 core) |
| `WebServer`, `DNSServer` | Captive portal (built into ESP32 core) |
| `Preferences` | NVS credential storage (built into ESP32 core) |

---

## Security Notes

- Credentials are **never compiled into the firmware** — they are entered at runtime and stored in the ESP32's NVS partition.
- Passwords (WiFi, Twilio Auth Token, OWM key) are **never echoed back** by the setup portal form.
- The setup AP is an open network by design (to allow any device to connect easily). Consider adding a password in `config_portal.cpp` if your environment requires it.
- The Twilio Auth Token and OWM API key are transmitted over HTTPS to the respective APIs.

---

## API Error Display

When any data fetch fails, the device shows the specific error code directly on the e-paper display — no serial monitor required.

| Display section | Error shown when… | Example message |
|---|---|---|
| Current weather band | `weatherFetch()` returns false | `Weather error: HTTP 401` |
| Hourly / daily bands | `forecastFetch()` returns false | `Forecast error: HTTP 404` |
| Messages band | `twilioFetchMessages()` returns -1 | `Messages error: HTTP 403` |

HTTP errors include the status code (e.g. `HTTP 401` = bad API key, `HTTP 429` = rate limit exceeded). JSON parse errors show `JSON: <detail>`.

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| Display shows setup instructions on every boot | Credentials not saving to NVS | Check serial output for NVS errors; try a full flash erase |
| "WiFi timeout" / portal keeps reopening | Wrong SSID or password | Power-cycle 3× to reset credentials, re-enter via portal |
| `Weather error: HTTP 401` on screen | Invalid OWM API key | Re-enter the correct key via the setup portal |
| `Weather error: HTTP 404` on screen | City name not recognised by OWM | Use a valid city name or `lat=XX.X&lon=YY.Y` coordinates |
| `Messages error: HTTP 401` on screen | Wrong Twilio Account SID or Auth Token | Re-enter credentials via the setup portal |
| `Messages error: HTTP 403` on screen | Twilio number not owned by this account | Verify `twilioToNumber` is the correct E.164 number |
| No SMS messages / `No messages` shown | No inbound messages, or whitelist too strict | Check whitelist in the portal; send a test SMS |
| Display looks faded or ghosted | Wrong VCOM setting | Update `EPAPER_VCOM_MV` in config.h to match your panel's FPC label |
| Portal page doesn't auto-open | Captive-portal detection varies by OS | Navigate manually to http://192.168.4.1 |
