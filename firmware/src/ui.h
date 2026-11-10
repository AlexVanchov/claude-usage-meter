#pragma once
#include <Arduino.h>

struct UsageWindow {
  bool    valid = false;
  float   pct = 0;          // 0..100
  time_t  resetsAt = 0;     // unix epoch seconds (0 = unknown)
};

struct MeterState {
  UsageWindow session;      // five_hour
  UsageWindow weekly;       // seven_day
  bool     haveData = false;
  uint32_t lastUpdateMs = 0;   // millis() of last accepted update
  uint32_t lastChangeMs = 0;   // millis() of last update that changed values
  bool     wifiUp = false;
  bool     timeValid = false;
};

namespace ui {
void begin();
void setBacklight(uint8_t pct);
void showBootMessage(const char* line1, const char* line2);
// Call often from loop(). Redraws whatever changed and animates.
void tick(const MeterState& st);
// Force a full redraw of the dashboard (e.g. after data arrives).
void invalidate();
bool isIdle();
}
