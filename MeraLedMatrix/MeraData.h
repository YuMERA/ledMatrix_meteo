#pragma once
// Citanje Output API-ja sa MERA servera. Radi u zasebnom FreeRTOS tasku na
// jezgru 0, da HTTP poziv (i do HTTP_TIMEOUT_MS) ne zamrzne animaciju na
// jezgru 1. Displej cita samo kopiju (dataSnapshot) uzetu pod mutex-om.

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "config.h"
#include "secrets.h"

enum ParamKind : uint8_t {
  KIND_TEMP, KIND_HUM, KIND_PRESS, KIND_WIND, KIND_RAIN, KIND_AIR, KIND_LIGHT, KIND_OTHER
};

struct Param {
  char key[24];
  char label[24];
  char unit[12];
  ParamKind kind;
  bool missing;
  bool numeric;
  float value;
  char text[24];
  // Prsten poslednjih vrednosti za trend; prezivljava osvezavanja jer se
  // prenosi po kljucu.
  float hist[TREND_WINDOW];
  uint8_t histCount;
  uint8_t histHead;
};

struct MeraData {
  Param params[MAX_PARAMS];
  uint8_t count;
  int8_t isDay;           // -1 = server ne zna
  int weatherCode;        // -1 = nema
  char weatherLabel[48];  // UTF-8, kako je stiglo
  bool everOk;
  uint32_t lastOkMs;
  int lastHttpCode;
};

static MeraData g_data;
static MeraData g_stage;  // koristi ga samo fetch task
static SemaphoreHandle_t g_dataMutex;

static void copyStr(char* dst, size_t n, const char* src) {
  strncpy(dst, src ? src : "", n - 1);
  dst[n - 1] = 0;
}

static bool containsCI(const char* hay, const char* needle) {
  if (!hay || !needle) return false;
  size_t nl = strlen(needle);
  for (const char* p = hay; *p; p++) {
    if (strncasecmp(p, needle, nl) == 0) return true;
  }
  return false;
}

// Vrsta se pogadja iz kljuca, izvornog polja i jedinice. Odatle ikona, boja i
// da li ima smisla prikazivati trend.
static ParamKind detectKind(const char* key, const char* source, const char* unit) {
  const char* s[2] = {key, source};
  for (int i = 0; i < 2; i++) {
    if (containsCI(s[i], "temp")) return KIND_TEMP;
    if (containsCI(s[i], "hum") || containsCI(s[i], "vlaz")) return KIND_HUM;
    if (containsCI(s[i], "press") || containsCI(s[i], "prit")) return KIND_PRESS;
    if (containsCI(s[i], "wind") || containsCI(s[i], "gust") || containsCI(s[i], "vetar")) return KIND_WIND;
    if (containsCI(s[i], "rain") || containsCI(s[i], "precip") || containsCI(s[i], "kisa")) return KIND_RAIN;
    if (containsCI(s[i], "pm") || containsCI(s[i], "aqi") || containsCI(s[i], "co2")) return KIND_AIR;
    if (containsCI(s[i], "lux") || containsCI(s[i], "light") || containsCI(s[i], "uv")) return KIND_LIGHT;
  }
  if (containsCI(unit, "hpa")) return KIND_PRESS;
  if (containsCI(unit, "C") && strstr(unit, "\xC2\xB0")) return KIND_TEMP;
  return KIND_OTHER;
}

static void pushHistory(Param& p) {
  if (p.missing || !p.numeric) return;
  p.hist[p.histHead] = p.value;
  p.histHead = (p.histHead + 1) % TREND_WINDOW;
  if (p.histCount < TREND_WINDOW) p.histCount++;
}

// -1 pada, 0 miruje, 1 raste. Samo za velicine kod kojih trend nesto znaci.
static int trendDir(const Param& p) {
  if (p.missing || !p.numeric || p.histCount < TREND_MIN_SAMPLES) return 0;
  float threshold;
  switch (p.kind) {
    case KIND_TEMP:  threshold = 0.3f; break;
    case KIND_HUM:   threshold = 2.0f; break;
    case KIND_PRESS: threshold = 0.7f; break;
    default: return 0;
  }
  uint8_t oldest = (p.histHead + TREND_WINDOW - p.histCount) % TREND_WINDOW;
  float diff = p.value - p.hist[oldest];
  if (diff >= threshold) return 1;
  if (diff <= -threshold) return -1;
  return 0;
}

static bool parsePayload(const String& body) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    Serial.printf("[data] JSON greska: %s\n", err.c_str());
    return false;
  }
  if (!doc["ok"].as<bool>()) return false;

  memset(&g_stage, 0, sizeof(g_stage));
  JsonObjectConst values = doc["values"].as<JsonObjectConst>();
  JsonObjectConst meta = doc["meta"].as<JsonObjectConst>();

  // Redosled polja je onaj iz Admin -> Output, server ga cuva u JSON-u.
  for (JsonPairConst kv : values) {
    if (g_stage.count >= MAX_PARAMS) break;
    Param& p = g_stage.params[g_stage.count++];
    JsonObjectConst m = meta[kv.key().c_str()].as<JsonObjectConst>();

    copyStr(p.key, sizeof(p.key), kv.key().c_str());
    copyStr(p.label, sizeof(p.label), m["label"] | kv.key().c_str());
    copyStr(p.unit, sizeof(p.unit), m["unit"] | "");
    p.kind = detectKind(p.key, m["source_field"] | "", p.unit);

    JsonVariantConst v = kv.value();
    p.missing = v.isNull() || (m["missing"] | false);
    if (!p.missing) {
      if (v.is<float>()) {
        p.numeric = true;
        p.value = v.as<float>();
      } else {
        copyStr(p.text, sizeof(p.text), v.as<const char*>());
      }
    }
  }

  g_stage.isDay = doc["is_day"].isNull() ? -1 : (int8_t)doc["is_day"].as<int>();
  g_stage.weatherCode = doc["weather_code"].isNull() ? -1 : doc["weather_code"].as<int>();
  copyStr(g_stage.weatherLabel, sizeof(g_stage.weatherLabel), doc["weather_label"] | "");
  return true;
}

static void mergeStage() {
  xSemaphoreTake(g_dataMutex, portMAX_DELAY);
  for (uint8_t i = 0; i < g_stage.count; i++) {
    Param& np = g_stage.params[i];
    for (uint8_t j = 0; j < g_data.count; j++) {
      const Param& op = g_data.params[j];
      if (strcmp(op.key, np.key) == 0) {
        memcpy(np.hist, op.hist, sizeof(np.hist));
        np.histCount = op.histCount;
        np.histHead = op.histHead;
        break;
      }
    }
    pushHistory(np);
  }
  memcpy(g_data.params, g_stage.params, sizeof(g_data.params));
  g_data.count = g_stage.count;
  g_data.isDay = g_stage.isDay;
  g_data.weatherCode = g_stage.weatherCode;
  memcpy(g_data.weatherLabel, g_stage.weatherLabel, sizeof(g_data.weatherLabel));
  g_data.everOk = true;
  g_data.lastOkMs = millis();
  g_data.lastHttpCode = 200;
  xSemaphoreGive(g_dataMutex);
}

static void setHttpCode(int code) {
  xSemaphoreTake(g_dataMutex, portMAX_DELAY);
  g_data.lastHttpCode = code;
  xSemaphoreGive(g_dataMutex);
}

static bool fetchOnce() {
  String url = String(MERA_API_BASE) + "/api/output/" + MERA_OUTPUT_ID;
  bool https = url.startsWith("https://");

  WiFiClientSecure secure;
  WiFiClient plain;
  HTTPClient http;
  // Bez provere sertifikata: matrica cita samo javno-bezopasan JSON, a pin
  // sertifikata bi pukao pri svakoj Let's Encrypt obnovi.
  if (https) secure.setInsecure();
  if (!(https ? http.begin(secure, url) : http.begin(plain, url))) {
    setHttpCode(-1);
    return false;
  }
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  // Token ide u zaglavlje, ne u URL - server prihvata oba.
  http.addHeader("x-mera-output-token", MERA_OUTPUT_TOKEN);

  int code = http.GET();
  bool ok = false;
  if (code == 200) {
    String body = http.getString();
    ok = parsePayload(body);
    if (ok) mergeStage();
  }
  http.end();

  if (!ok) {
    Serial.printf("[data] neuspeh, HTTP %d\n", code);
    setHttpCode(code);
  }
  return ok;
}

static void fetchTask(void*) {
  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }
    bool ok = fetchOnce();
    vTaskDelay(pdMS_TO_TICKS(ok ? FETCH_INTERVAL_MS : FETCH_RETRY_MS));
  }
}

static void dataBegin() {
  memset(&g_data, 0, sizeof(g_data));
  g_data.isDay = -1;
  g_data.weatherCode = -1;
  g_dataMutex = xSemaphoreCreateMutex();
  // TLS trazi dosta steka; 16 KB je sa rezervom.
  xTaskCreatePinnedToCore(fetchTask, "mera-fetch", 16384, nullptr, 1, nullptr, 0);
}

static void dataSnapshot(MeraData& out) {
  xSemaphoreTake(g_dataMutex, portMAX_DELAY);
  memcpy(&out, &g_data, sizeof(out));
  xSemaphoreGive(g_dataMutex);
}
