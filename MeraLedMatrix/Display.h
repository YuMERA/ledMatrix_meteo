#pragma once
// Framebuffer i backend za hardver. Sav crtez ide u Frame; displayShow() ga
// prenosi na MAX7219 ili WS2812, zavisno od DISPLAY_BACKEND u config.h.

#include <Arduino.h>
#include "config.h"

typedef uint32_t Color;  // 0x00RRGGBB, 0 = ugaseno

static inline Color rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

struct Frame {
  Color px[MATRIX_H][MATRIX_W];

  void clear() { memset(px, 0, sizeof(px)); }

  void set(int x, int y, Color c) {
    if (x >= 0 && x < MATRIX_W && y >= 0 && y < MATRIX_H) px[y][x] = c;
  }

  Color get(int x, int y) const {
    return (x >= 0 && x < MATRIX_W && y >= 0 && y < MATRIX_H) ? px[y][x] : 0;
  }
};

#if DISPLAY_BACKEND == DISPLAY_MAX7219

#include <MD_MAX72xx.h>
#include <SPI.h>

static MD_MAX72XX mx(MAX_HW_TYPE, MAX_CS_PIN, MAX_DEVICES);

static void displayBegin() {
  mx.begin();
  // Automatsko osvezavanje je iskljuceno da se slika ne bi videla dok se
  // crta piksel po piksel; update() salje ceo kadar odjednom.
  mx.control(MD_MAX72XX::UPDATE, MD_MAX72XX::OFF);
  mx.control(MD_MAX72XX::INTENSITY, MAX_INTENSITY_DAY);
  mx.clear();
  mx.update();
}

static void displayShow(const Frame& f) {
  for (int y = 0; y < MATRIX_H; y++) {
    for (int x = 0; x < MATRIX_W; x++) {
      // MD_MAX72XX broji kolone od desne ivice.
      mx.setPoint(y, MATRIX_W - 1 - x, f.px[y][x] != 0);
    }
  }
  mx.update();
}

static void displaySetDay(bool day) {
  mx.control(MD_MAX72XX::INTENSITY, day ? MAX_INTENSITY_DAY : MAX_INTENSITY_NIGHT);
}

#elif DISPLAY_BACKEND == DISPLAY_WS2812

#include <Adafruit_NeoPixel.h>

static Adafruit_NeoPixel strip(MATRIX_W * MATRIX_H, WS_PIN, NEO_GRB + NEO_KHZ800);

static uint16_t wsIndex(int x, int y) {
  int m = x / 8;
  int lx = x % 8;
  int ly = WS_FLIP_Y ? 7 - y : y;
  if (WS_FLIP_X) lx = 7 - lx;
  if (WS_MODULE_REVERSE) m = (MATRIX_W / 8 - 1) - m;
  if (WS_SERPENTINE && (ly & 1)) lx = 7 - lx;
  return m * 64 + ly * 8 + lx;
}

static void displayBegin() {
  strip.begin();
  strip.setBrightness(WS_BRIGHTNESS_DAY);
  strip.clear();
  strip.show();
}

static void displayShow(const Frame& f) {
  for (int y = 0; y < MATRIX_H; y++) {
    for (int x = 0; x < MATRIX_W; x++) {
      strip.setPixelColor(wsIndex(x, y), f.px[y][x]);
    }
  }
  strip.show();
}

static void displaySetDay(bool day) {
  strip.setBrightness(day ? WS_BRIGHTNESS_DAY : WS_BRIGHTNESS_NIGHT);
}

#else
#error "Nepoznat DISPLAY_BACKEND u config.h"
#endif
