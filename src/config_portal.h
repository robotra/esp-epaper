#pragma once

#include "credentials_store.h"

// ============================================================
//  config_portal.h — First-boot / credential-reset web portal
//
//  When the device has no saved credentials (or they have been
//  deliberately cleared), it cannot connect to WiFi or call any
//  APIs.  Instead of failing silently, it becomes its own WiFi
//  access point and serves a web page where the user can enter
//  all required settings.
//
//  How it works (high-level):
//    1. The ESP32 starts in AP (access-point) mode with the SSID
//       "EPaper-Setup" — no password required.
//    2. A DNS server answers ALL domain queries with the AP's IP
//       address (192.168.4.1).  This is the "captive portal trick":
//       when a device joins the open network and its OS fires a
//       connectivity check, the check is intercepted and the OS
//       automatically opens the portal URL in a browser.
//    3. The user fills in WiFi, Twilio, and OpenWeatherMap fields
//       and clicks "Save & Restart".
//    4. The form data is validated, saved to NVS via
//       credentialsSave(), and the ESP32 restarts.
//    5. On reboot, credentialsLoad() succeeds and normal
//       operation begins.
//
//  If no credentials are submitted within 5 minutes the portal
//  times out and restarts the device (preserving whatever was
//  already in NVS).
// ============================================================


// ------------------------------------------------------------
//  configPortalRun — start the portal and block until done
//
//  This function NEVER returns.  It either:
//    • restarts the device after saving valid credentials, or
//    • restarts the device after the 5-minute timeout.
//
//  Call it when credentialsLoad() returns false, or after the
//  3-rapid-power-cycle reset sequence clears credentials.
// ------------------------------------------------------------
void configPortalRun();
