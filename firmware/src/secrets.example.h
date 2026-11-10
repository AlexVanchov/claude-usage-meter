#pragma once
// Copy to secrets.h and fill in. secrets.h is git-ignored.
//   cp secrets.example.h secrets.h

// 2.4 GHz WiFi network the display joins.
#define WIFI_SSID "your-wifi-name"
#define WIFI_PASS "your-wifi-password"

// Shared secret between the Mac bridge and the display. POST /usage must send
// header "X-Meter-Token: <token>"; also the password for wireless (OTA) updates.
// Generate one with:  openssl rand -hex 12
#define METER_TOKEN "change-me"

// Local time zone for the clock. Pick a macro from the ESP8266 core's TZ.h
// (e.g. TZ_Europe_London, TZ_America_New_York, TZ_Asia_Tokyo). Default: UTC.
// #define METER_TZ TZ_Europe_London
