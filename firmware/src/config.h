#pragma once
#include <TZ.h>

#include "secrets.h"  // WiFi, token and (optionally) METER_TZ

// ---- Network -------------------------------------------------------------
#define METER_HOSTNAME   "claude-meter"        // -> http://claude-meter.local
#ifndef METER_TZ
#define METER_TZ         TZ_Etc_UTC            // override in secrets.h, e.g. TZ_Europe_London
#endif
#define NTP_SERVER_1     "pool.ntp.org"
#define NTP_SERVER_2     "time.google.com"

// ---- Hardware (GeekMagic SmallTV-style ESP8266 board) ---------------------
#define PIN_BACKLIGHT    5
#define BACKLIGHT_ACTIVE_LOW 1                 // PWM is inverted on this board
#define BACKLIGHT_LEVEL  80                    // 0-100 %
#define DISPLAY_ROTATION 0                     // 0..3 (matches stock "USB" option)

// ---- Behaviour -----------------------------------------------------------
#define STALE_AFTER_MIN  15    // header dot turns red if no update for this long
#define IDLE_AFTER_MIN   10    // switch to the idle scene after N min without change
#define VERB_ROTATE_MS   4000  // footer status word rotation
#define SPINNER_FRAME_MS 80
#define DOTS_FRAME_MS    400
