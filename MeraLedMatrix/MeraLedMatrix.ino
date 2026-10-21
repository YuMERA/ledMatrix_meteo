/*
  |======================================|
  |  MERA LED matrica v0.1 By me[R]a     |
  |  ESP32 Dev + 4x MAX7219 / 4x WS2812  |
  |======================================|

  Cita Output API sa MERA servera (Admin -> Output) i smenjuje sat i
  parametre stanice na 32x8 matrici, sa efektima prelaza.

  Svi moduli su header-only i ukljuceni samo ovde, pa je ceo firmware jedna
  prevodna jedinica - zato funkcije mogu biti static.
*/

#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <time.h>

#include "config.h"
#include "secrets.h"
#include "Display.h"
#include "Fonts.h"
#include "Icons.h"
#include "MeraData.h"
#include "Gfx.h"

// --- Verzija softvera ---
// Isti obrazac kao u MERA nodovima: verzija stoji samo u markeru, a VERSION_SW
// pokazuje unutar njega, pa linker ne moze da izbaci marker kao mrtav kod.
static const char MERA_FW_MARKER[] = "MERA_FW_VERSION=matrix-0.2.1";
static const char* const VERSION_SW = MERA_FW_MARKER + sizeof("MERA_FW_VERSION=") - 1;

static const char* HOSTNAME = "mera-matrix";

// --- Ekrani ---
enum ScreenType : uint8_t { SCR_CLOCK, SCR_PARAM, SCR_LABEL, SCR_MESSAGE };

struct Screen {
  ScreenType type;
  uint8_t index;
};

enum Transition : uint8_t { TR_UP, TR_LEFT, TR_DISSOLVE };
enum Phase : uint8_t { PH_TRANSITION, PH_HOLD };

static Frame g_out, g_from, g_to;
static MeraData g_view;            // kopija podataka uzeta na pocetku ekrana
static Screen g_screen = {SCR_CLOCK, 0};
static uint8_t g_seq = 0;
static char g_scrollText[64];

static Phase g_phase = PH_HOLD;
static Transition g_tr = TR_UP;
static uint32_t g_trStart = 0, g_trDur = 0, g_screenStart = 0;
static uint8_t g_dissolveOrder[MATRIX_W * MATRIX_H];

static uint32_t g_lastFrame = 0, g_lastBright = 0;
static bool g_isDay = true;
static bool g_otaReady = false;

static bool timeValid(struct tm& tm) {
  return getLocalTime(&tm, 0) && tm.tm_year + 1900 >= 2024;
}

// ---------------------------------------------------------------- crtanje

static void drawClock(Frame& f) {
  struct tm tm;
  bool ok = timeValid(tm);
  char s[8];
  if (ok) snprintf(s, sizeof(s), "%02d:%02d", tm.tm_hour, tm.tm_min);
  else strcpy(s, "--:--");
  // Dvotacka trepce; zamena prazninom iste sirine ne pomera cifre.
  if (ok && (tm.tm_sec & 1)) s[2] = CH_BLANK1;

  Color c = g_isDay ? rgb(255, 170, 60) : rgb(255, 60, 0);
  drawString(f, (MATRIX_W - textWidth(s)) / 2, 1, s, c);
}

// Vrednost ide krupnim fontom; jedinica malim 3x5 fontom pored nje, osim
// "°C" i "%" koji su deo krupnog teksta.
static void formatValue(const Param& p, char* out, size_t n, char* unit, size_t un) {
  unit[0] = 0;
  if (p.missing) { copyStr(out, n, "--"); return; }
  if (!p.numeric) { toMatrixText(p.text, out, n); return; }

  float v = p.value;
  if (fabsf(v) < 0.05f) v = 0;  // bez "-0.0"
  bool whole = fabsf(v - roundf(v)) < 0.05f;

  switch (p.kind) {
    case KIND_TEMP:
      snprintf(out, n, "%.1f%cC", v, CH_DEGREE);
      break;
    case KIND_HUM:
      snprintf(out, n, "%.0f%%", v);
      break;
    case KIND_PRESS:
      snprintf(out, n, "%.0f", v);
      toMatrixText(p.unit, unit, un);
      break;
    default:
      snprintf(out, n, (whole || fabsf(v) >= 100) ? "%.0f" : "%.1f", v);
      toMatrixText(p.unit, unit, un);
  }
}

static void drawParam(Frame& f, const Param& p) {
  char txt[24], unit[12];
  formatValue(p, txt, sizeof(txt), unit, sizeof(unit));
  int textW = textWidth(txt);
  int unitW = smallWidth(unit);
  const uint8_t* icon = SHOW_ICONS ? iconFor(p.kind) : nullptr;

  // Ikona zauzima 8 kolona (njena desna je prazna), jedinica razmak + sirinu.
  // Kad nema mesta, prvo otpada ikona, pa jedinica - broj se nikad ne skracuje.
  bool showIcon = icon != nullptr;
  bool showUnit = unitW > 0;
  auto need = [&]() {
    return (showIcon ? 8 : 0) + textW + (showUnit ? unitW + 1 : 0);
  };
  if (need() > MATRIX_W) showIcon = false;
  if (need() > MATRIX_W) showUnit = false;

  int left = showIcon ? 8 : 0;
  int blockW = textW + (showUnit ? unitW + 1 : 0);
  int x = left + ((MATRIX_W - left) - blockW) / 2;
  if (x < left) x = left;

  Color c = paramColor(p);
  if (showIcon) drawIcon(f, 0, 0, icon, c);
  x = drawString(f, x, 1, txt, c);
  // Mala slova poravnata uz donju ivicu cifara (redovi 3..7).
  if (showUnit) drawSmall(f, x, 3, unit, c);
}

// Tekst koji staje stoji na sredini; duzi skroluje s desna na levo.
static bool drawScroll(Frame& f, const char* s, uint32_t t, Color c) {
  int w = textWidth(s);
  if (w <= MATRIX_W) {
    drawString(f, (MATRIX_W - w) / 2, 1, s, c);
    return t >= SCREEN_HOLD_MS;
  }
  int x = MATRIX_W - (int)(t / SCROLL_STEP_MS);
  drawString(f, x, 1, s, c);
  return x < -w;
}

static void drawStaleMark(Frame& f) {
  // Crveni treptaj u gornjem desnom uglu: podaci su stari ili ih jos nema.
  bool stale = !g_view.everOk || millis() - g_view.lastOkMs > STALE_AFTER_MS;
  if (stale && (millis() / 500) % 2) f.set(MATRIX_W - 1, 0, rgb(255, 0, 0));
}

// Vraca true kad je ekran zavrsio svoje.
static bool renderScreen(Frame& f, const Screen& s, uint32_t t) {
  bool done = false;
  switch (s.type) {
    case SCR_CLOCK:
      drawClock(f);
      done = t >= CLOCK_HOLD_MS;
      break;
    case SCR_PARAM:
      if (s.index >= g_view.count) return true;
      drawParam(f, g_view.params[s.index]);
      done = t >= (g_view.params[s.index].kind == KIND_TEMP ? TEMP_HOLD_MS : SCREEN_HOLD_MS);
      break;
    case SCR_LABEL:
      done = drawScroll(f, g_scrollText, t, rgb(120, 200, 255));
      break;
    case SCR_MESSAGE:
      done = drawScroll(f, g_scrollText, t, rgb(255, 80, 0));
      break;
  }
  drawStaleMark(f);
  return done;
}

// ---------------------------------------------------------------- redosled

static void prepareMessage() {
  if (WiFi.status() != WL_CONNECTED) copyStr(g_scrollText, sizeof(g_scrollText), "WiFi...");
  else if (g_view.lastHttpCode == 403) copyStr(g_scrollText, sizeof(g_scrollText), "Token nije validan");
  else if (g_view.lastHttpCode == 404) copyStr(g_scrollText, sizeof(g_scrollText), "Output ne postoji");
  else if (g_view.lastHttpCode != 0) snprintf(g_scrollText, sizeof(g_scrollText), "Server: %d", g_view.lastHttpCode);
  else copyStr(g_scrollText, sizeof(g_scrollText), "Cekam podatke");
}

// Ciklus: sat, pa parametri redom iz Admin -> Output, pa opis vremena.
// Dok podataka nema, smenjuju se sat i poruka o razlogu.
static Screen pickNext() {
  if (!g_view.everOk) {
    g_seq = (g_seq + 1) % 2;
    if (g_seq == 0) return {SCR_CLOCK, 0};
    prepareMessage();
    return {SCR_MESSAGE, 0};
  }

  bool hasLabel = g_view.weatherLabel[0] != 0;
  uint8_t len = 1 + g_view.count + (hasLabel ? 1 : 0);
  g_seq = (g_seq + 1) % len;
  if (g_seq == 0) return {SCR_CLOCK, 0};
  if (g_seq <= g_view.count) return {SCR_PARAM, (uint8_t)(g_seq - 1)};
  toMatrixText(g_view.weatherLabel, g_scrollText, sizeof(g_scrollText));
  return {SCR_LABEL, 0};
}

// ---------------------------------------------------------------- prelazi

static void composeTransition(float p) {
  switch (g_tr) {
    case TR_UP: {
      // Novi ekran ulazi odozdo, kao preklopna tabla.
      int off = (int)(p * MATRIX_H);
      for (int y = 0; y < MATRIX_H; y++) {
        int sy = y + off;
        for (int x = 0; x < MATRIX_W; x++) {
          g_out.px[y][x] = sy < MATRIX_H ? g_from.px[sy][x] : g_to.px[sy - MATRIX_H][x];
        }
      }
      break;
    }
    case TR_LEFT: {
      int off = (int)(p * MATRIX_W);
      for (int y = 0; y < MATRIX_H; y++) {
        for (int x = 0; x < MATRIX_W; x++) {
          int sx = x + off;
          g_out.px[y][x] = sx < MATRIX_W ? g_from.px[y][sx] : g_to.px[y][sx - MATRIX_W];
        }
      }
      break;
    }
    case TR_DISSOLVE: {
      int k = (int)(p * MATRIX_W * MATRIX_H);
      memcpy(&g_out, &g_from, sizeof(Frame));
      for (int i = 0; i < k; i++) {
        uint8_t idx = g_dissolveOrder[i];
        int x = idx % MATRIX_W, y = idx / MATRIX_W;
        g_out.px[y][x] = g_to.px[y][x];
      }
      break;
    }
  }
}

static void startScreen(const Screen& s, uint32_t now) {
  memcpy(&g_from, &g_out, sizeof(Frame));
  g_screen = s;
  g_to.clear();
  renderScreen(g_to, s, 0);

  switch (s.type) {
    case SCR_PARAM:
      g_tr = TR_UP;
      g_trDur = 350;
      break;
    case SCR_CLOCK:
      g_tr = TR_DISSOLVE;
      g_trDur = 600;
      // Fisher-Yates: svaki piksel se otkrije tacno jednom, nasumicnim redom.
      for (int i = 0; i < MATRIX_W * MATRIX_H; i++) g_dissolveOrder[i] = i;
      for (int i = MATRIX_W * MATRIX_H - 1; i > 0; i--) {
        int j = esp_random() % (i + 1);
        uint8_t tmp = g_dissolveOrder[i];
        g_dissolveOrder[i] = g_dissolveOrder[j];
        g_dissolveOrder[j] = tmp;
      }
      break;
    default:
      // Skrolujuci tekst pocinje praznim ekranom, pa stari sadrzaj izlazi
      // ulevo i tekst se nastavlja istim pokretom.
      g_tr = TR_LEFT;
      g_trDur = MATRIX_W * SCROLL_STEP_MS;
      break;
  }
  g_trStart = now;
  g_phase = PH_TRANSITION;
  composeTransition(0);
}

// ---------------------------------------------------------------- ostalo

static bool isDayNow() {
  if (g_view.isDay >= 0) return g_view.isDay == 1;
  struct tm tm;
  if (timeValid(tm)) return tm.tm_hour >= DAY_HOUR_FROM && tm.tm_hour < DAY_HOUR_TO;
  return true;
}

static void showStatic(const char* s, Color c) {
  g_out.clear();
  drawString(g_out, (MATRIX_W - textWidth(s)) / 2, 1, s, c);
  displayShow(g_out);
}

static void startOta() {
  ArduinoOTA.setHostname(HOSTNAME);
  if (strlen(OTA_PASSWORD) > 0) ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.onStart([]() { showStatic("OTA", rgb(0, 255, 0)); });
  ArduinoOTA.begin();
  g_otaReady = true;
  Serial.printf("[wifi] povezan, IP %s\n", WiFi.localIP().toString().c_str());
}

void setup() {
  Serial.begin(115200);
  Serial.printf("\nMERA LED matrica %s\n", VERSION_SW);

  displayBegin();
  showStatic("MERA", rgb(0, 200, 255));

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  configTzTime(TZ_POSIX, NTP_1, NTP_2);
  dataBegin();

  delay(1200);
  dataSnapshot(g_view);
  g_screenStart = millis();
}

void loop() {
  if (!g_otaReady && WiFi.status() == WL_CONNECTED) startOta();
  if (g_otaReady) ArduinoOTA.handle();

  uint32_t now = millis();
  if (now - g_lastFrame < FRAME_MS) {
    delay(1);
    return;
  }
  g_lastFrame = now;

  if (now - g_lastBright > 5000) {
    g_lastBright = now;
    bool day = isDayNow();
    if (day != g_isDay) {
      g_isDay = day;
      displaySetDay(day);
    }
  }

  if (g_phase == PH_TRANSITION) {
    uint32_t el = now - g_trStart;
    if (el >= g_trDur) {
      g_phase = PH_HOLD;
      g_screenStart = now;
    } else {
      composeTransition((float)el / g_trDur);
    }
  }

  if (g_phase == PH_HOLD) {
    g_out.clear();
    if (renderScreen(g_out, g_screen, now - g_screenStart)) {
      dataSnapshot(g_view);
      startScreen(pickNext(), now);
    }
  }

  displayShow(g_out);
}
