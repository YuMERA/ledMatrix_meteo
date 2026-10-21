#pragma once
// Crtanje teksta, ikona i boja nad Frame-om.

#include <Arduino.h>
#include "Display.h"
#include "Fonts.h"
#include "Icons.h"
#include "MeraData.h"

static int drawGlyph(Frame& f, int x, int y, const Glyph& g, Color c) {
  for (int col = 0; col < g.w; col++) {
    uint8_t bits = g.cols[col];
    for (int row = 0; row < 7; row++) {
      if (bits & (1 << row)) f.set(x + col, y + row, c);
    }
  }
  return g.w;
}

static int textWidth(const char* s) {
  int w = 0;
  for (; *s; s++) w += glyphFor(*s).w + 1;
  return w > 0 ? w - 1 : 0;
}

// Vraca x posle poslednjeg znaka.
static int drawString(Frame& f, int x, int y, const char* s, Color c) {
  for (; *s; s++) {
    Glyph g = glyphFor(*s);
    // Znakove potpuno van ekrana preskacemo - bitno za dug skrolujuci tekst.
    if (x + g.w >= 0 && x < MATRIX_W) drawGlyph(f, x, y, g, c);
    x += g.w + 1;
  }
  return x;
}

static int smallWidth(const char* s) {
  int w = 0;
  for (; *s; s++) {
    uint8_t gw = smallGlyphFor(*s).w;
    if (gw) w += gw + 1;
  }
  return w > 0 ? w - 1 : 0;
}

static int drawSmall(Frame& f, int x, int y, const char* s, Color c) {
  for (; *s; s++) {
    Glyph g = smallGlyphFor(*s);
    if (!g.w) continue;
    drawGlyph(f, x, y, g, c);
    x += g.w + 1;
  }
  return x;
}

static void drawIcon(Frame& f, int x, int y, const uint8_t* rows, Color c) {
  for (int r = 0; r < 8; r++) {
    for (int col = 0; col < 8; col++) {
      if (rows[r] & (0x80 >> col)) f.set(x + col, y + r, c);
    }
  }
}

// UTF-8 u nas skup znakova: mala slova sa kvacicom u interne kodove iz
// Fonts.h, "°" u CH_DEGREE. Velika slova sa kvacicom nemaju mesta u 7 redova,
// pa ostaju bez nje.
static void toMatrixText(const char* in, char* out, size_t n) {
  size_t o = 0;
  const uint8_t* p = (const uint8_t*)in;
  while (*p && o + 2 < n) {
    uint8_t a = p[0];
    if (a < 0x80) { out[o++] = (char)a; p++; continue; }
    uint8_t b = p[1];
    if (!b) break;
    const char* rep = "?";
    if (a == 0xC2 && b == 0xB0) { out[o++] = CH_DEGREE; p += 2; continue; }
    if (a == 0xC4) {
      switch (b) {
        case 0x8C: case 0x86: rep = "C"; break;   // Č Ć
        case 0x8D: rep = "\x80"; break;           // č
        case 0x87: rep = "\x81"; break;           // ć
        case 0x90: rep = "Dj"; break;             // Đ
        case 0x91: rep = "\x84"; break;           // đ
      }
    } else if (a == 0xC5) {
      switch (b) {
        case 0xA0: rep = "S"; break;     // Š
        case 0xA1: rep = "\x82"; break;  // š
        case 0xBD: rep = "Z"; break;     // Ž
        case 0xBE: rep = "\x83"; break;  // ž
      }
    }
    for (const char* r = rep; *r && o + 1 < n; r++) out[o++] = *r;
    // Preskacemo ceo UTF-8 niz, koliko god bajtova imao.
    p++;
    while ((*p & 0xC0) == 0x80) p++;
  }
  out[o] = 0;
}

static Color lerpColor(Color a, Color b, float t) {
  if (t < 0) t = 0;
  if (t > 1) t = 1;
  uint8_t r = ((a >> 16) & 0xFF) + (((int)((b >> 16) & 0xFF) - (int)((a >> 16) & 0xFF)) * t);
  uint8_t g = ((a >> 8) & 0xFF) + (((int)((b >> 8) & 0xFF) - (int)((a >> 8) & 0xFF)) * t);
  uint8_t bl = (a & 0xFF) + (((int)(b & 0xFF) - (int)(a & 0xFF)) * t);
  return rgb(r, g, bl);
}

// Temperatura kao boja: ljubicasta po mrazu, preko plave i zelene do crvene.
static Color temperatureColor(float t) {
  static const float stops[] = {-15, -5, 5, 15, 22, 30, 38};
  static const Color cols[] = {
    0x7800FF, 0x0050FF, 0x00C8FF, 0x00FF50, 0xFFDC00, 0xFF6400, 0xFF0000
  };
  const int n = sizeof(stops) / sizeof(stops[0]);
  if (t <= stops[0]) return cols[0];
  for (int i = 1; i < n; i++) {
    if (t <= stops[i]) return lerpColor(cols[i - 1], cols[i], (t - stops[i - 1]) / (stops[i] - stops[i - 1]));
  }
  return cols[n - 1];
}

static Color kindColor(ParamKind k) {
  switch (k) {
    case KIND_HUM:   return rgb(0, 140, 255);
    case KIND_PRESS: return rgb(180, 0, 255);
    case KIND_WIND:  return rgb(190, 255, 255);
    case KIND_RAIN:  return rgb(0, 90, 255);
    case KIND_AIR:   return rgb(0, 255, 120);
    case KIND_LIGHT: return rgb(255, 200, 0);
    default:         return rgb(255, 190, 120);
  }
}

static Color paramColor(const Param& p) {
  if (p.missing) return rgb(90, 90, 90);
  if (p.kind == KIND_TEMP && p.numeric) return temperatureColor(p.value);
  return kindColor(p.kind);
}

static const uint8_t* iconFor(ParamKind k) {
  switch (k) {
    case KIND_TEMP:  return ICON_TEMP;
    case KIND_HUM:   return ICON_HUM;
    case KIND_PRESS: return ICON_PRESS;
    case KIND_WIND:  return ICON_WIND;
    case KIND_RAIN:  return ICON_RAIN;
    case KIND_AIR:   return ICON_AIR;
    case KIND_LIGHT: return ICON_LIGHT;
    default:         return nullptr;
  }
}
