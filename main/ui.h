#pragma once
#include <stdbool.h>
#include "energy.h"

/* Alle ui_*-Funktionen erwarten, dass LVGL gesperrt ist (bsp_display_lock). */
void ui_create(void);
void ui_set_status(const char *text);

/* Seite 0 bis 2 anzeigen, ohne Wischgeste. */
void ui_show_page(int n);

/* Adressen der gefundenen Geraete fuer die Statusseite. */
void ui_set_hosts(const char *plug, const char *em);

void ui_update(const energy_state_t *st);

/* Vorfuehrbetrieb: zeigt runde Beispielwerte und ersetzt WLAN-Name und
 * Adressen durch Platzhalter. Gedacht fuer Abbildungen in Dokumentation
 * und Artikeln, damit dort keine echten Netzdaten landen. */
void ui_set_demo(bool an);
