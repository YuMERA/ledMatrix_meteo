#pragma once
// Sva podesavanja hardvera i ritma prikaza. Tajne (WiFi, token) su u secrets.h.

// ---- Izbor displeja ----
// Ceo crtez se pravi u 32x8 framebuffer-u u boji; backend ga samo prenosi na
// hardver. MAX7219 pali svaki piksel koji nije crn, WS2812 prikazuje boje.
#define DISPLAY_MAX7219 1
#define DISPLAY_WS2812  2
#define DISPLAY_BACKEND DISPLAY_MAX7219

#define MATRIX_W 32
#define MATRIX_H 8

// ---- MAX7219 (4 modula u lancu, hardverski SPI: DIN=GPIO23, CLK=GPIO18) ----
// FC16_HW je najcesci "4 in 1" modul sa AliExpress-a. Ako su slova u ogledalu
// ili rotirana, probati PAROLA_HW, GENERIC_HW ili ICSTATION_HW.
#define MAX_HW_TYPE         MD_MAX72XX::FC16_HW
#define MAX_CS_PIN          5
#define MAX_DEVICES         4
#define MAX_INTENSITY_DAY   6   // 0..15
#define MAX_INTENSITY_NIGHT 0

// ---- WS2812 (4 panela 8x8 u lancu) ----
// Raspored dioda se razlikuje od panela do panela; ako je slika ispreturana,
// menjati ove zastavice, ne kod.
#define WS_PIN              16
#define WS_SERPENTINE       false  // svaki drugi red ide unazad
#define WS_FLIP_X           false
#define WS_FLIP_Y           false
#define WS_MODULE_REVERSE   false  // prvi panel u lancu je desni
// Osvetljenje je i strujni limit: 256 dioda na punom belom vuku ~15 A.
// 40/255 je dovoljno za sobu i drzi se ispod ~2 A.
#define WS_BRIGHTNESS_DAY   40
#define WS_BRIGHTNESS_NIGHT 6

// ---- Vreme ----
// POSIX zona za Beograd (CET/CEST). Server salje IANA ime, ali ga ESP32 ne zna.
#define TZ_POSIX "CET-1CEST,M3.5.0,M10.5.0/3"
#define NTP_1    "pool.ntp.org"
#define NTP_2    "time.google.com"

// Rezervni dan/noc kad server ne javi is_day.
#define DAY_HOUR_FROM 7
#define DAY_HOUR_TO   21

// ---- Ritam ----
#define FETCH_INTERVAL_MS   60000UL
#define FETCH_RETRY_MS      15000UL
#define HTTP_TIMEOUT_MS     5000
#define FRAME_MS            25
#define SCREEN_HOLD_MS      2500UL  // ostali parametri i kratke poruke
#define TEMP_HOLD_MS        6000UL  // temperatura je glavni podatak, stoji duze
#define CLOCK_HOLD_MS       6000UL
#define SCROLL_STEP_MS      45      // ms po pikselu skrolovanja
#define STALE_AFTER_MS      (10UL * 60UL * 1000UL)

// Trend se racuna prema najstarijem uzorku u prozoru:
// TREND_WINDOW x FETCH_INTERVAL_MS = 30 min.
#define TREND_WINDOW        30
#define TREND_MIN_SAMPLES   5

#define MAX_PARAMS          10
