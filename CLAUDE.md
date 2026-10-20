# CLAUDE.md

Projekat je na srpskom — komentari, dokumentacija i komunikacija sa korisnikom.
Tekst za korisnika ide sa dijakritikom; komentari u firmware-u su bez nje.

## Pravila

**Ne pokretati Arduino kompajliranje.** Korisnik ga radi sam. Piši izmene, ali
reci šta nisi mogao da proveriš.

**Korisnik radi sve `git push` operacije.** Commit je u redu, push nije.

**Server se ne menja odavde.** Matrica je samo čitalac Output API-ja iz
`../mera-HUB-app` (`GET /api/output/:id`, token u `x-mera-output-token`). Ako
nešto fali u odgovoru, to je izmena u tom projektu i njegovim pravilima.

## Arhitektura

- Svi moduli su header-only i uključeni samo iz `MeraLedMatrix.ino`, pa je ceo
  firmware jedna prevodna jedinica.
- `Display.h` — `Frame` (32×8, boja po pikselu) i backend MAX7219/WS2812 koji
  bira `DISPLAY_BACKEND`. Sav crtež ide kroz `Frame`; ne crtati direktno po
  hardveru, inače drugi backend prestaje da radi.
- `MeraData.h` — HTTP u FreeRTOS tasku na jezgru 0; displej čita samo kopiju
  uzetu pod mutex-om (`dataSnapshot`) na početku svakog ekrana.
- `MeraLedMatrix.ino` — redosled ekrana, prelazi i osvetljenje.

Verzija stoji samo u `MERA_FW_MARKER`, kao u MERA nodovima.
