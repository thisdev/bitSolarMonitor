#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Tagesverlauf im 5-Minuten-Raster: 288 Werte decken 24 Stunden ab und
 * passen mit rund einem Punkt je Bildpunkt sauber auf das Display.
 * Feiner waere Rechenarbeit ohne sichtbaren Gewinn. */
#define HIST_SLOTS 288

void history_init(void);

/* Aktuelle Leistung einsortieren. Innerhalb eines Fuenf-Minuten-Fensters
 * wird gemittelt, damit einzelne Ausreisser die Kurve nicht verzerren. */
void history_add(float watt);

/* Zeiger auf das Array fuer LVGL. Leere Faecher stehen auf
 * LV_CHART_POINT_NONE und werden von LVGL uebersprungen. */
int32_t *history_data(void);

/* Ertrag seit Tagesbeginn beziehungsweise seit dem Einschalten. */
void  history_meter(float meter_kwh);
float history_today_kwh(void);

/* true, sobald ein echter Mitternachtswechsel mitgelaufen ist. Vorher zeigt
 * der Wert nur, was seit dem Einschalten dazukam. */
bool  history_is_full_day(void);
