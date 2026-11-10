// Claude Usage Meter - display rendering (240x240 ST7789)
#include "ui.h"

#include <TFT_eSPI.h>
#include <time.h>

#include "config.h"
#include "gen_fonts.h"
#include "gen_sprites.h"

namespace {

TFT_eSPI tft;
TFT_eSprite footer(&tft);

constexpr uint16_t rgb(uint32_t c) {
  return ((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F);
}
uint16_t lerp565(uint32_t a, uint32_t b, float t) {  // a->b, t in 0..1
  auto ch = [&](int s) {
    float x = ((a >> s) & 255) * (1 - t) + ((b >> s) & 255) * t;
    return (uint32_t)(x + 0.5f);
  };
  return rgb((ch(16) << 16) | (ch(8) << 8) | ch(0));
}

// ---- Palette (spec) --------------------------------------------------------
constexpr uint32_t HEX_ACCENT = 0xF07A2B, HEX_BG = 0x0E0E0E, HEX_CARD = 0x1C1C1C;
const uint16_t C_ACCENT = rgb(HEX_ACCENT);
const uint16_t C_AMBER = rgb(0xF5A524);
const uint16_t C_RED = rgb(0xE5484D);
const uint16_t C_BG = rgb(HEX_BG);
const uint16_t C_CARD = rgb(HEX_CARD);
const uint16_t C_TRACK = rgb(0x2E2E2E);
const uint16_t C_PILL = rgb(0x2A2A2A);
const uint16_t C_PILL_TXT = rgb(0xD8D8D8);
const uint16_t C_TEXT = rgb(0xFFFFFF);
const uint16_t C_TEXT2 = rgb(0xBDBDBD);
const uint16_t C_BATT = rgb(0xCFCFCF);
const uint16_t C_MUTED = rgb(0x5A5A5A);

// ---- Layout (240x240) ------------------------------------------------------
constexpr int SCR = 240;
constexpr int PAD = 8;
constexpr int CW = SCR - 2 * PAD;          // content width 224
constexpr int HEADER_Y = 4, HEADER_H = 30;
constexpr int CARD_Y[2] = {40, 122};
constexpr int CARD_H = 76, CARD_R = 8, CARD_PAD = 10;
constexpr int IN_X = PAD + CARD_PAD;       // 18
constexpr int IN_W = CW - 2 * CARD_PAD;    // 204
constexpr int ROW1_DY = 8, ROW1_H = 28;
constexpr int BAR_DY = 41, BAR_H = 8;
constexpr int ROW3_DY = 55, ROW3_H = 16;
constexpr int FOOT_Y = 206, FOOT_H = 28;
constexpr int CLOCK_W = 110;

const char* const VERBS[] = {"Reticulating", "Computing", "Pondering",  "Noodling",
                             "Brewing",      "Tinkering", "Synthesizing", "Percolating",
                             "Musing",       "Crunching", "Conjuring",  "Cogitating"};
constexpr int NVERBS = sizeof(VERBS) / sizeof(VERBS[0]);

// ---- Render cache (only redraw what changed) -------------------------------
struct CardCache {
  int pct = -999;
  String sub;
  uint16_t barColor = 0;
  bool valid = false;
};
bool needFull = true;
bool idle = false;
String lastClock;
int lastStale = -1;
CardCache cards[2];

uint32_t lastSpin = 0, lastDots = 0, lastVerb = 0;
int spinFrame = 0, dots = 1, verbIdx = 0;
String footerWord;

// idle animation
int idleFrame = -1;
uint32_t idleNext = 0, blinkAt = 0;
uint8_t idleStep = 0;

// ---- Helpers ---------------------------------------------------------------
void drawIndexed(TFT_eSPI& g, const uint8_t* spr, int w, int h, int x, int y, int scale) {
  for (int r = 0; r < h; r++) {
    for (int c = 0; c < w; c++) {
      uint8_t idx = pgm_read_byte(spr + r * w + c);
      if (!idx) continue;
      g.fillRect(x + c * scale, y + r * scale, scale, scale, pgm_read_word(SPRITE_PALETTE + idx));
    }
  }
}

uint16_t barColorFor(float pct) {
  if (pct > 95) return C_RED;
  if (pct > 80) return C_AMBER;
  return C_ACCENT;
}

String formatCountdown(time_t resetsAt, bool timeValid) {
  if (!resetsAt || !timeValid) return "Resets in --";
  long s = (long)(resetsAt - time(nullptr));
  if (s <= 0) return "Window reset";
  long m = (s + 59) / 60;  // round up so "0m" never shows
  long d = m / 1440, h = (m % 1440) / 60, mm = m % 60;
  char buf[32];
  if (d > 0) snprintf(buf, sizeof buf, "Resets in %ldd %ldh", d, h);
  else if (h > 0) snprintf(buf, sizeof buf, "Resets in %ldh %ldm", h, mm);
  else snprintf(buf, sizeof buf, "Resets in %ldm", mm);
  return buf;
}

// ---- Header ----------------------------------------------------------------
void drawHeaderStatic() {
  // mascot 24x18
  drawIndexed(tft, SPR_MASCOT, SPR_MASCOT_W, SPR_MASCOT_H, PAD + 2,
              HEADER_Y + (HEADER_H - 18) / 2, 2);
  // battery (USB powered -> shown full)
  const int bw = 19, bh = 11, bx = SCR - PAD - 2 - bw - 3, by = HEADER_Y + (HEADER_H - bh) / 2;
  tft.fillSmoothRoundRect(bx, by, bw, bh, 3, C_BATT, C_BG);
  tft.fillSmoothRoundRect(bx + 1, by + 1, bw - 2, bh - 2, 2, C_BG, C_BATT);
  tft.fillRect(bx + 3, by + 3, bw - 6, bh - 6, C_BATT);
  tft.fillSmoothRoundRect(bx + bw + 1, by + 3, 3, bh - 6, 1, C_BATT, C_BG);
}

void drawClock(const String& s) {
  TFT_eSprite spr(&tft);
  if (!spr.createSprite(CLOCK_W, HEADER_H)) return;
  spr.fillSprite(C_BG);
  spr.loadFont(FONT_CLOCK);
  spr.setTextColor(C_TEXT, C_BG);
  spr.setTextDatum(MC_DATUM);
  spr.drawString(s, CLOCK_W / 2, HEADER_H / 2 + 1);
  spr.unloadFont();
  spr.pushSprite((SCR - CLOCK_W) / 2, HEADER_Y);
  spr.deleteSprite();
}

void drawStaleDot(int state) {  // 0 fresh, 1 stale, 2 no data yet
  const int x = SCR - PAD - 2 - 19 - 3 - 10, y = HEADER_Y + HEADER_H / 2;
  tft.fillRect(x - 5, y - 5, 11, 11, C_BG);
  if (state == 1) tft.fillSmoothCircle(x, y, 3, C_RED, C_BG);
  else if (state == 2) tft.fillSmoothCircle(x, y, 3, C_MUTED, C_BG);
}

// ---- Cards -----------------------------------------------------------------
void drawCardFrame(int i) {
  tft.fillSmoothRoundRect(PAD, CARD_Y[i], CW, CARD_H, CARD_R, C_CARD, C_BG);
}

void drawCardRow1(int i, const char* pctText, const char* label) {
  TFT_eSprite spr(&tft);
  if (!spr.createSprite(IN_W, ROW1_H)) return;
  spr.fillSprite(C_CARD);

  spr.loadFont(FONT_PCT);
  spr.setTextColor(C_TEXT, C_CARD);
  spr.setTextDatum(ML_DATUM);
  spr.drawString(pctText, 0, ROW1_H / 2 + 1);
  spr.unloadFont();

  spr.loadFont(FONT_PILL);
  const int ph = 20, pw = spr.textWidth(label) + 16;
  const int px = IN_W - pw, py = (ROW1_H - ph) / 2;
  spr.fillSmoothRoundRect(px, py, pw, ph, ph / 2, C_PILL, C_CARD);
  spr.setTextColor(C_PILL_TXT, C_PILL);
  spr.setTextDatum(MC_DATUM);
  spr.drawString(label, px + pw / 2, py + ph / 2 + 1);
  spr.unloadFont();

  spr.pushSprite(IN_X, CARD_Y[i] + ROW1_DY);
  spr.deleteSprite();
}

void drawCardBar(int i, float pct, uint16_t color) {
  TFT_eSprite spr(&tft);
  if (!spr.createSprite(IN_W, BAR_H)) return;
  spr.fillSprite(C_CARD);
  spr.fillSmoothRoundRect(0, 0, IN_W, BAR_H, BAR_H / 2, C_TRACK, C_CARD);
  int fw = (int)(IN_W * constrain(pct, 0.0f, 100.0f) / 100.0f + 0.5f);
  if (fw > 0) {
    if (fw < BAR_H) fw = BAR_H;  // keep a visible rounded nub for small values
    spr.fillSmoothRoundRect(0, 0, fw, BAR_H, BAR_H / 2, color, C_TRACK);
  }
  spr.pushSprite(IN_X, CARD_Y[i] + BAR_DY);
  spr.deleteSprite();
}

void drawCardRow3(int i, const String& text) {
  TFT_eSprite spr(&tft);
  if (!spr.createSprite(IN_W, ROW3_H)) return;
  spr.fillSprite(C_CARD);
  spr.loadFont(FONT_BODY);
  spr.setTextColor(C_TEXT2, C_CARD);
  spr.setTextDatum(ML_DATUM);
  spr.drawString(text, 0, ROW3_H / 2);
  spr.unloadFont();
  spr.pushSprite(IN_X, CARD_Y[i] + ROW3_DY);
  spr.deleteSprite();
}

void updateCard(int i, const UsageWindow& w, bool timeValid) {
  static const char* LABELS[2] = {"Current", "Weekly"};
  CardCache& c = cards[i];

  bool valid = w.valid;
  float pct = w.pct;
  String sub;
  if (!valid) {
    sub = "No data yet";
  } else {
    if (timeValid && w.resetsAt && w.resetsAt <= time(nullptr)) pct = 0;  // window rolled over
    sub = formatCountdown(w.resetsAt, timeValid);
  }
  int ipct = valid ? (int)(pct + 0.5f) : -1;
  uint16_t col = barColorFor(pct);

  if (ipct != c.pct || valid != c.valid) {
    char buf[8];
    if (valid) snprintf(buf, sizeof buf, "%d%%", ipct);
    else strcpy(buf, "--%");
    drawCardRow1(i, buf, LABELS[i]);
    drawCardBar(i, valid ? pct : 0, col);
  } else if (col != c.barColor) {
    drawCardBar(i, pct, col);
  }
  if (sub != c.sub) drawCardRow3(i, sub);

  c.pct = ipct;
  c.valid = valid;
  c.barColor = col;
  c.sub = sub;
}

// ---- Footer ----------------------------------------------------------------
void drawFooter(const char* word) {
  footer.fillSprite(C_BG);
  const int icon = 14, gap1 = 6, gap2 = 9, spin = 14;
  // reserve width for "..." so the layout doesn't jump while dots animate
  String full = String(word) + "...";
  int tw = footer.textWidth(full);
  int total = icon + gap1 + tw + gap2 + spin;
  int x = (CW - total) / 2;
  int cy = FOOT_H / 2;

  drawIndexed(footer, SPR_MASCOT_MINI, SPR_MASCOT_MINI_W, SPR_MASCOT_MINI_H, x, cy - 7, 2);
  x += icon + gap1;

  String shown = String(word);
  for (int d = 0; d < dots; d++) shown += '.';
  footer.setTextColor(C_ACCENT, C_BG);
  footer.setTextDatum(ML_DATUM);
  footer.drawString(shown, x, cy + 1);
  x += tw + gap2;

  // radial spinner: 10 dashes, brightest at the head, fading tail
  const int N = 10;
  float scx = x + spin / 2.0f, scy = cy;
  for (int k = 0; k < N; k++) {
    int age = (spinFrame - k + N) % N;  // 0 = head
    float t = age / (float)(N - 1);
    uint16_t col = lerp565(HEX_ACCENT, HEX_BG, 0.12f + 0.78f * t);
    float a = k * (2 * PI / N) - PI / 2;
    float ca = cosf(a), sa = sinf(a);
    footer.drawWideLine(scx + ca * 3.2f, scy + sa * 3.2f, scx + ca * 6.6f, scy + sa * 6.6f,
                        1.8f, col, C_BG);
  }
  footer.pushSprite(PAD, FOOT_Y);
}

const char* footerWordFor(const MeterState& st) {
  if (!st.wifiUp) return "Connecting";
  if (!st.haveData) return "Awaiting data";
  return VERBS[verbIdx];
}

// ---- Idle scene ------------------------------------------------------------
void drawIdleFrame(int f) {
  const int S = 8, ox = (SCR - SPR_IDLE_W * S) / 2, oy = (SCR - SPR_IDLE_H * S) / 2 + 4;
  const uint8_t* cur = SPR_IDLE[f];
  const uint8_t* prev = idleFrame >= 0 ? SPR_IDLE[idleFrame] : nullptr;
  for (int i = 0; i < SPR_IDLE_W * SPR_IDLE_H; i++) {
    uint8_t v = pgm_read_byte(cur + i);
    if (prev && pgm_read_byte(prev + i) == v) continue;
    uint16_t col = v ? pgm_read_word(SPRITE_PALETTE + v) : TFT_BLACK;
    tft.fillRect(ox + (i % SPR_IDLE_W) * S, oy + (i / SPR_IDLE_W) * S, S, S, col);
  }
  idleFrame = f;
}

void enterIdle() {
  idle = true;
  idleFrame = -1;
  tft.fillScreen(TFT_BLACK);
  drawIdleFrame(0);
  idleNext = millis() + 400;
  blinkAt = millis() + 3000;
  idleStep = 0;
}

void tickIdle() {
  uint32_t now = millis();
  if ((int32_t)(now - idleNext) < 0) return;
  if ((int32_t)(now - blinkAt) >= 0) {
    drawIdleFrame(1);  // blink
    idleNext = now + 160;
    blinkAt = now + 2500 + random(3500);
    return;
  }
  // typing: alternate hands, with an occasional pause
  idleStep++;
  int f = (idleStep % 12) >= 9 ? 0 : (2 + (idleStep & 1));
  drawIdleFrame(f);
  idleNext = now + 260;
}

void fullRedraw() {
  tft.fillScreen(C_BG);
  drawHeaderStatic();
  lastClock = "";
  lastStale = -1;
  for (int i = 0; i < 2; i++) {
    drawCardFrame(i);
    cards[i] = CardCache();
  }
  footerWord = "";
  needFull = false;
}

}  // namespace

// ---- Public API ------------------------------------------------------------
namespace ui {

void begin() {
  pinMode(PIN_BACKLIGHT, OUTPUT);
  analogWriteRange(255);
  analogWriteFreq(1000);
  setBacklight(0);
  tft.init();
  tft.setRotation(DISPLAY_ROTATION);
  tft.fillScreen(C_BG);
  setBacklight(BACKLIGHT_LEVEL);

  footer.createSprite(CW, FOOT_H);
  footer.loadFont(FONT_MONO);
}

void setBacklight(uint8_t pct) {
  int v = (int)pct * 255 / 100;
  analogWrite(PIN_BACKLIGHT, BACKLIGHT_ACTIVE_LOW ? 255 - v : v);
}

void showBootMessage(const char* l1, const char* l2) {
  tft.fillScreen(C_BG);
  drawIndexed(tft, SPR_IDLE[0], SPR_IDLE_W, SPR_IDLE_H, (SCR - 96) / 2, 30, 4);
  tft.loadFont(FONT_BIG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_TEXT, C_BG);
  tft.drawString(l1, SCR / 2, 150);
  tft.unloadFont();
  tft.loadFont(FONT_BODY);
  tft.setTextColor(C_TEXT2, C_BG);
  tft.drawString(l2, SCR / 2, 178);
  tft.unloadFont();
  needFull = true;
}

void invalidate() {
  if (idle) {
    idle = false;
    needFull = true;
  }
}

bool isIdle() { return idle; }

void tick(const MeterState& st) {
  uint32_t now = millis();

  // idle switching
  bool shouldIdle = (now - st.lastChangeMs) > (uint32_t)IDLE_AFTER_MIN * 60000UL;
  if (shouldIdle && !idle) enterIdle();
  if (idle) {
    if (!shouldIdle) invalidate();
    else {
      tickIdle();
      return;
    }
  }

  if (needFull) fullRedraw();

  // header: clock + stale dot
  String clk = "--:--";
  if (st.timeValid) {
    time_t t = time(nullptr);
    struct tm lt;
    localtime_r(&t, &lt);
    char buf[6];
    snprintf(buf, sizeof buf, "%02d:%02d", lt.tm_hour, lt.tm_min);
    clk = buf;
  }
  if (clk != lastClock) {
    drawClock(clk);
    lastClock = clk;
  }
  int stale = !st.haveData ? 2
              : (now - st.lastUpdateMs) > (uint32_t)STALE_AFTER_MIN * 60000UL ? 1 : 0;
  if (stale != lastStale) {
    drawStaleDot(stale);
    lastStale = stale;
  }

  // cards
  updateCard(0, st.session, st.timeValid);
  updateCard(1, st.weekly, st.timeValid);

  // footer animation
  bool dirty = false;
  if (now - lastVerb >= VERB_ROTATE_MS) {
    lastVerb = now;
    verbIdx = (verbIdx + 1 + random(NVERBS - 1)) % NVERBS;
    dirty = true;
  }
  if (now - lastDots >= DOTS_FRAME_MS) {
    lastDots = now;
    dots = dots % 3 + 1;
    dirty = true;
  }
  if (now - lastSpin >= SPINNER_FRAME_MS) {
    lastSpin = now;
    spinFrame = (spinFrame + 1) % 10;
    dirty = true;
  }
  const char* w = footerWordFor(st);
  if (footerWord != w) {
    footerWord = w;
    dirty = true;
  }
  if (dirty) drawFooter(w);
}

}  // namespace ui
