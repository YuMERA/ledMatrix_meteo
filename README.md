# MERA LED matrica

Prikaz podataka MERA meteo stanice na LED matrici 32×8: sat, temperatura,
vlažnost, pritisak, vetar… sa jedinicama, strelicom trenda i efektima prelaza.

Firmware je u `MeraLedMatrix/`, a pravljen je za **ESP32 Dev Module**.

## Hardver

Crtež se pravi u framebuffer-u u boji, a `DISPLAY_BACKEND` u `config.h` bira
hardver. Isti firmware zato radi sa obe vrste matrica.

| | 4× MAX7219 8×8 | 4× WS2812 8×8 |
|---|---|---|
| Boje | jedna (crvena) | pune RGB boje: temperatura menja boju |
| Struja | ~100–300 mA | do ~2 A na `WS_BRIGHTNESS_DAY 40`, a teoretski do 15 A |
| Napajanje | USB je dovoljan | zaseban 5 V / 3 A+ |
| Čitljivost na danu | odlična | dobra, ali zavisi od osvetljenja |

### MAX7219 (VSPI)

| MAX7219 | ESP32 |
|---|---|
| VCC | 5 V (VIN) |
| GND | GND |
| DIN | GPIO23 |
| CLK | GPIO18 |
| CS | GPIO5 |

Ako su slova u ogledalu ili rotirana, promeni `MAX_HW_TYPE` u `config.h`. Ako
na ekranu iskače „smeće", 3.3 V signal nije dovoljan; ubaci level shifter na
DIN, CLK i CS.

### WS2812

| WS2812 | ESP32 |
|---|---|
| 5 V | spoljno 5 V napajanje |
| GND | GND napajanja **i** GND ESP32 |
| DIN | GPIO16 preko otpornika od 330 Ω |

Uz napajanje ide kondenzator od 1000 µF. Kod četiri panela u lancu dovedi 5 V
i na sredinu lanca, ne samo na početak. Ako je slika ispreturana, menjaj
`WS_SERPENTINE`, `WS_FLIP_X`, `WS_FLIP_Y` i `WS_MODULE_REVERSE`.

## Biblioteke (Library Manager)

- **ArduinoJson** 7.x (Benoit Blanchon);
- **MD_MAX72XX** (majicDesigns), za MAX7219;
- **Adafruit NeoPixel**, za WS2812.

Instaliraj obe biblioteke za displej. Kompajlira se samo ona koju bira
`DISPLAY_BACKEND`.

## Podešavanje

1. U MERA Admin → **Output** napravi output uređaj (npr. `led-matrix`) i dodaj
   polja koja želiš na matrici. Redosled polja je redosled prikaza. Decimale
   se podešavaju tamo.
2. Kopiraj `MeraLedMatrix/secrets.example.h` u `secrets.h` i upiši WiFi, adresu
   servera, Output ID i token.
3. Board: *ESP32 Dev Module*, pa upload.

Posle prvog upload-a može se preko mreže (ArduinoOTA, host `mera-matrix`).

## Šta se prikazuje

Ciklus ide ovako: sat → polja iz Output-a redom → opis vremena (`weather_label`),
ako ga server šalje.

- Vrsta polja se pogađa iz ključa, izvornog polja i jedinice (`temp`, `hum`,
  `press`, `wind`, `rain`, `pm`, `lux`…). Od nje zavise format, jedinica, boja i trend. Jedinica (`hPa`, `m/s`…) dolazi iz Admin → Output i ispisuje se malim fontom. Ikone su isključene (`SHOW_ICONS` u `config.h`) jer su na MAX7219 bile nečitljive.
- Trend ↑↓ se računa za temperaturu, vlažnost i pritisak, prema vrednosti od
  pre ~30 min. Strelica se pojavi tek posle 5 očitavanja.
- Ako ne stigne do servera, u gornjem desnom uglu trepće crveni piksel. Dok
  podataka uopšte nema, matrica skroluje razlog (WiFi, token, kod greške).
- Osvetljenje prati `is_day` sa servera. Ako ga server ne pošalje, važi
  raspored od 7 do 21 h.
- Mala slova č, ć, š, ž i đ imaju svoje znakove; velika (Č, Ć, Š, Ž, Đ) nemaju mesta za kvačicu u 7 redova, pa se prikazuju kao C, S, Z i Dj.
