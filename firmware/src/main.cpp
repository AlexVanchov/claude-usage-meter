// Claude Usage Meter - ESP8266 firmware
//
// Receives Claude usage (5-hour session + weekly limits) over HTTP from the
// Mac (Claude Code status line hook, see ../bridge) and renders it on the
// 240x240 ST7789 display.
//
//   POST /usage   {"rate_limits":{"five_hour":{"used_percentage":65,"resets_at":1762790000},
//                                 "seven_day":{"used_percentage":12,"resets_at":1763200000}},
//                  "now":1762780000}
//                 header  X-Meter-Token: <METER_TOKEN>
//   GET  /usage   current state as JSON
//   GET  /        tiny status page
//   POST /wake    leave the idle screen
//
// OTA updates: `METER_TOKEN=<token> pio run -e ota -t upload` (see platformio.ini)

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <LittleFS.h>
#include <sys/time.h>
#include <time.h>

#include "config.h"
#include "secrets.h"
#include "ui.h"

ESP8266WebServer server(80);
MeterState st;

static bool readWindow(JsonVariantConst v, UsageWindow& w) {
  if (!v.is<JsonObjectConst>()) return false;
  JsonVariantConst p = v["used_percentage"];
  if (p.isNull()) p = v["utilization"];  // tolerate API-style field name
  if (p.isNull()) return false;
  UsageWindow n;
  n.valid = true;
  n.pct = constrain(p.as<float>(), 0.0f, 100.0f);
  JsonVariantConst r = v["resets_at"];
  n.resetsAt = r.is<long long>() ? (time_t)r.as<long long>() : 0;
  bool changed = !w.valid || (int)(w.pct + 0.5f) != (int)(n.pct + 0.5f) || w.resetsAt != n.resetsAt;
  w = n;
  return changed;
}

// ---- Persistence: last known usage survives power loss --------------------
static const char* STORE = "/usage.json";
static bool fsReady = false;

static void saveUsage(long long capturedAt) {
  if (!fsReady) return;
  JsonDocument d;
  auto put = [&](const char* k, const UsageWindow& w) {
    if (!w.valid) return;
    d[k]["used_percentage"] = w.pct;
    d[k]["resets_at"] = (long long)w.resetsAt;
  };
  put("five_hour", st.session);
  put("seven_day", st.weekly);
  d["captured_at"] = capturedAt;
  File f = LittleFS.open(STORE, "w");
  if (!f) return;
  serializeJson(d, f);
  f.close();
}

static void loadUsage() {
  if (!fsReady) return;
  File f = LittleFS.open(STORE, "r");
  if (!f) return;
  JsonDocument d;
  bool ok = !deserializeJson(d, f);
  f.close();
  if (!ok) return;
  readWindow(d["five_hour"], st.session);
  readWindow(d["seven_day"], st.weekly);
  if (st.session.valid || st.weekly.valid) {
    st.haveData = true;
    // Age unknown until the Mac reports in: show it as stale.
    st.lastUpdateMs = millis() - (uint32_t)(STALE_AFTER_MIN + 1) * 60000UL;
    Serial.printf("[store] restored session=%.1f%% weekly=%.1f%%\n", st.session.pct, st.weekly.pct);
  }
}

static bool authorized() {
  if (strlen(METER_TOKEN) == 0) return true;
  return server.header("X-Meter-Token") == METER_TOKEN;
}

static void handlePostUsage() {
  if (!authorized()) {
    server.send(401, "application/json", "{\"ok\":false,\"error\":\"bad token\"}");
    return;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, server.arg("plain"));
  if (err) {
    server.send(400, "application/json", String("{\"ok\":false,\"error\":\"") + err.c_str() + "\"}");
    return;
  }
  JsonVariantConst root = doc.as<JsonVariantConst>();
  JsonVariantConst rl = root["rate_limits"].isNull() ? root : root["rate_limits"];

  bool changed = false;
  bool any = false;
  if (!rl["five_hour"].isNull()) { any = true; changed |= readWindow(rl["five_hour"], st.session); }
  if (!rl["seven_day"].isNull()) { any = true; changed |= readWindow(rl["seven_day"], st.weekly); }

  // Use the sender's clock if NTP hasn't synced yet.
  long long senderNow = doc["now"] | 0LL;
  if (!st.timeValid && senderNow > 1700000000LL) {
    timeval tv{(time_t)senderNow, 0};
    settimeofday(&tv, nullptr);
  }

  if (any) {
    st.haveData = true;
    // captured_at = when Claude Code reported these numbers (keep-alive resends
    // carry the original time, so the stale dot reflects the real data age).
    long long capturedAt = doc["captured_at"] | senderNow;
    long long age = (capturedAt > 0 && senderNow >= capturedAt) ? senderNow - capturedAt : 0;
    if (age > 40LL * 86400) age = 40LL * 86400;
    st.lastUpdateMs = millis() - (uint32_t)(age * 1000);
    if (changed) saveUsage(capturedAt);
    // Activity = Claude Code produced a new report (newer captured_at), even if
    // the percentages are unchanged. Keep-alive resends of the same report don't count.
    bool fresh = capturedAt > st.lastCapturedAt && st.lastCapturedAt != 0;
    if (capturedAt > st.lastCapturedAt) st.lastCapturedAt = capturedAt;
    if (changed || fresh || doc["wake"] == true) {
      st.lastChangeMs = millis();
      ui::invalidate();
    }
  }
  Serial.printf("[usage] session=%.1f%% weekly=%.1f%% changed=%d\n", st.session.pct, st.weekly.pct, changed);
  server.send(200, "application/json", String("{\"ok\":true,\"changed\":") + (changed ? "true" : "false") + "}");
}

static String stateJson() {
  JsonDocument d;
  auto win = [&](const char* k, const UsageWindow& w) {
    JsonObject o = d[k].to<JsonObject>();
    o["valid"] = w.valid;
    o["used_percentage"] = w.pct;
    o["resets_at"] = (long long)w.resetsAt;
  };
  win("five_hour", st.session);
  win("seven_day", st.weekly);
  d["have_data"] = st.haveData;
  d["seconds_since_update"] = st.haveData ? (long)((millis() - st.lastUpdateMs) / 1000) : -1L;
  d["idle"] = ui::isIdle();
  d["time_valid"] = st.timeValid;
  d["now"] = (long long)time(nullptr);
  d["free_heap"] = ESP.getFreeHeap();
  d["rssi"] = WiFi.RSSI();
  String out;
  serializeJson(d, out);
  return out;
}

static void handleRoot() {
  String html = F("<!doctype html><meta name=viewport content='width=device-width'>"
                  "<title>Claude Meter</title><body style='font:15px system-ui;background:#0e0e0e;color:#eee;padding:16px'>"
                  "<h2 style='color:#f07a2b'>Claude Meter</h2><pre id=s style='white-space:pre-wrap'></pre>"
                  "<script>fetch('/usage').then(r=>r.json()).then(j=>s.textContent=JSON.stringify(j,null,2))</script>");
  server.send(200, "text/html", html);
}

static void setupWifi() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.hostname(METER_HOSTNAME);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("[wifi] connecting to %s\n", WIFI_SSID);
}

static void onWifiUp() {
  Serial.printf("[wifi] connected, IP %s\n", WiFi.localIP().toString().c_str());
  MDNS.begin(METER_HOSTNAME);
  MDNS.addService("http", "tcp", 80);
  ArduinoOTA.setHostname(METER_HOSTNAME);
  if (strlen(METER_TOKEN)) ArduinoOTA.setPassword(METER_TOKEN);
  ArduinoOTA.onStart([] { ui::showBootMessage("Updating...", "Do not unplug"); });
  ArduinoOTA.begin();
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("[boot] Claude Usage Meter");

  ui::begin();
  ui::showBootMessage("Claude Meter", "Connecting to WiFi...");

  fsReady = LittleFS.begin();
  if (!fsReady) {
    Serial.println("[store] formatting LittleFS");
    fsReady = LittleFS.format() && LittleFS.begin();
  }
  loadUsage();

  setupWifi();
  configTime(METER_TZ, NTP_SERVER_1, NTP_SERVER_2);

  server.collectHeaders("X-Meter-Token");
  server.on("/", HTTP_GET, handleRoot);
  server.on("/usage", HTTP_GET, [] { server.send(200, "application/json", stateJson()); });
  server.on("/usage", HTTP_POST, handlePostUsage);
  server.on("/wake", HTTP_POST, [] {
    st.lastChangeMs = millis();
    ui::invalidate();
    server.send(200, "application/json", "{\"ok\":true}");
  });
  server.begin();

  // Give WiFi a few seconds so the dashboard appears with a clock.
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 8000) delay(100);
  st.lastChangeMs = millis();
}

void loop() {
  static bool wasUp = false;
  bool up = WiFi.status() == WL_CONNECTED;
  if (up && !wasUp) onWifiUp();
  if (!up && wasUp) Serial.println("[wifi] disconnected");
  wasUp = up;
  st.wifiUp = up;
  st.timeValid = time(nullptr) > 1700000000;

  server.handleClient();
  if (up) {
    MDNS.update();
    ArduinoOTA.handle();
  }
  ui::tick(st);

  static uint32_t lastLog = 0;
  if (millis() - lastLog > 30000) {
    lastLog = millis();
    Serial.printf("[stat] wifi=%d time=%d data=%d heap=%u idle=%d\n", up, st.timeValid, st.haveData,
                  ESP.getFreeHeap(), ui::isIdle());
  }
  delay(2);
}
