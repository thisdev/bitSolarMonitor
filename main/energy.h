#pragma once
#include <stdbool.h>
#include <stdint.h>

/*
 * Ein Messwert weiss selbst, ob er gilt. Genau daran haengt die Stufigkeit
 * des Projekts: Wer nur einen Shelly Plug hat, bekommt eben nur die
 * Erzeugung zu sehen -- die Oberflaeche blendet alles Ungueltige aus,
 * ohne dass irgendwo im Code eine Sonderbehandlung noetig waere.
 */
typedef struct {
    bool  valid;
    float value;
} measurement_t;

typedef struct {
    /* Stufe 1 -- Shelly Plug am Wechselrichter/Speicher-Ausgang */
    measurement_t production_w;      /* Erzeugung, positiv                  */
    measurement_t production_kwh;    /* Zaehlerstand eingespeiste Energie   */
    measurement_t plug_temp_c;       /* Gehaeusetemperatur des Plugs        */

    /* Stufe 2 -- Shelly Pro 3EM am Netzuebergabepunkt */
    measurement_t grid_w;            /* positiv = Bezug, negativ = Einspeisung */
    measurement_t grid_voltage[3];   /* Spannung der drei Phasen            */

    /* Stufe 3 -- Speicher ueber Home Assistant */
    measurement_t soc_pct;           /* Ladezustand in Prozent              */
    measurement_t reserve_wh;        /* daraus errechneter Vorrat           */
    measurement_t battery_w;         /* positiv = laedt, negativ = entlaedt */
    int8_t        battery_dir;       /* +1 laedt, -1 entlaedt, 0 in Ruhe    */

    /* Abgeleitet, sobald beide Quellen gelten */
    measurement_t house_w;           /* Hausverbrauch = Erzeugung + Netz    */
} energy_state_t;

void            energy_init(void);
void            energy_get(energy_state_t *out);
void            energy_set_production(float watt, float kwh, float temp_c);
void            energy_set_grid(float watt, const float voltage[3]);
void            energy_set_soc(float percent);
void            energy_set_battery_power(float watt);
void            energy_set_capacity_wh(float wh);
void            energy_invalidate_soc(void);
void            energy_invalidate_production(void);
void            energy_invalidate_grid(void);
