#pragma once
#include "energy.h"

/* Alle ui_*-Funktionen erwarten, dass LVGL gesperrt ist (bsp_display_lock). */
void ui_create(void);
void ui_set_status(const char *text);

/* Adressen der gefundenen Geraete fuer die Statusseite. */
void ui_set_hosts(const char *plug, const char *em);

void ui_update(const energy_state_t *st);
