#pragma once
// Kopirati u secrets.h i popuniti. secrets.h je u .gitignore.

#define WIFI_SSID         "moj-wifi"
#define WIFI_PASS         "lozinka"

// Adresa MERA servera, bez kose crte na kraju. Moze http:// ili https://.
#define MERA_API_BASE     "https://meteo.example.rs"

// Output ID i token iz Admin -> Output.
#define MERA_OUTPUT_ID    "led-matrix"
#define MERA_OUTPUT_TOKEN "promeni-me"

// Lozinka za ArduinoOTA u lokalnoj mrezi; prazno = bez lozinke.
#define OTA_PASSWORD      ""
