#pragma once

/* Kleiner HTTP-Dienst, der Bildschirmaufnahmen ausliefert. Gedacht zum
 * Erstellen sauberer Abbildungen fuer Dokumentation und Blog, ohne das
 * Display abfotografieren zu muessen.
 *
 *   GET /shot          rohe RGB565-Daten des aktuellen Bildschirms
 *   GET /page?n=0..2   Seite umschalten
 *
 * Bewusst nur lesend und ohne Anmeldung: Es gibt nichts zu holen ausser
 * dem, was ohnehin auf dem Display steht.
 */
void shot_start(void);
