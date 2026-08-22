#pragma once
#include "esp_err.h"

/* Home Assistant als Datenquelle fuer alles, was die Shellys nicht wissen.
 * Der Speicher meldet seinen Ladezustand dort ohnehin, also holen wir ihn
 * von da, statt fuer jedes Speichermodell eine eigene Anbindung zu bauen.
 *
 * Abgefragt wird die REST-Schnittstelle:
 *   GET /api/states/<entity_id>
 *   Authorization: Bearer <token>
 * Die Antwort enthaelt den Messwert als Zeichenkette im Feld "state".
 */
esp_err_t ha_read_number(const char *entity_id, float *out);
