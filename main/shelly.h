#pragma once
#include "esp_err.h"
#include <stdbool.h>

#define SHELLY_HOST_LEN 64

/* Sucht per mDNS nach Shelly-Geraeten und ordnet sie anhand ihrer Antworten
 * zu -- nicht anhand der Modellnummer. Ein Geraet, das auf EM.GetStatus
 * antwortet, ist ein Energiezaehler; eins mit Switch.GetStatus ein Plug.
 * Damit funktioniert die Erkennung auch mit Geraeten, die wir nie gesehen haben.
 * Bereits gefuellte Puffer bleiben unangetastet (feste Konfiguration gewinnt). */
esp_err_t shelly_discover(char *plug_host, char *em_host);

typedef struct {
    float watt;        /* Erzeugung, positiv                       */
    float meter_kwh;   /* Zaehlerstand eingespeiste Energie        */
    float temp_c;      /* Gehaeusetemperatur des Plugs             */
} shelly_plug_t;

typedef struct {
    float total_w;     /* positiv = Netzbezug, negativ = Einspeisung */
    float voltage[3];  /* Spannung der drei Phasen                   */
} shelly_em_t;

/* Liest den Plug. Der Wechselrichter speist ein, apower ist deshalb negativ --
 * herausgegeben wird die Erzeugung als positiver Wert. */
esp_err_t shelly_read_plug(const char *host, shelly_plug_t *out);

/* Liest den Energiezaehler. */
esp_err_t shelly_read_em(const char *host, shelly_em_t *out);
