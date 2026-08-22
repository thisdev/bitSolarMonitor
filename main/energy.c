#include "energy.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"

static energy_state_t    s_state;
static SemaphoreHandle_t s_lock;

static void set(measurement_t *m, float v)
{
    m->value = v;
    m->valid = true;
}

/* Haus und Reserve ergeben sich aus den Rohwerten -- einmal zentral
 * gerechnet, damit die Oberflaeche keine Physik kennen muss. */
static void derive(void)
{
#if CONFIG_PVMON_STORAGE_BETWEEN
    /* Sitzt ein Speicher zwischen Messpunkt und Netz, puffert er dazwischen:
     *   Netz         = Hausverbrauch - Speicherausgang
     *   Speicheraus. = Erzeugung - Ladeleistung
     * Zwei Unbekannte, eine Gleichung. Der Hausverbrauch ist damit nicht
     * ableitbar -- also wird er auch nicht behauptet. Er kommt in Stufe 3
     * zurueck, sobald der Speicher selbst Auskunft gibt. */
    s_state.house_w.valid = false;
#else
    if (s_state.production_w.valid && s_state.grid_w.valid) {
        set(&s_state.house_w, s_state.production_w.value + s_state.grid_w.value);
    } else {
        s_state.house_w.valid = false;
    }
#endif
}

void energy_init(void)
{
    s_lock = xSemaphoreCreateMutex();
}

void energy_get(energy_state_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_state;
    xSemaphoreGive(s_lock);
}

void energy_set_production(float watt, float kwh, float temp_c)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    set(&s_state.production_w, watt);
    set(&s_state.production_kwh, kwh);
    if (temp_c > 0.0f) set(&s_state.plug_temp_c, temp_c);
    derive();
    xSemaphoreGive(s_lock);
}

void energy_set_grid(float watt, const float voltage[3])
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    set(&s_state.grid_w, watt);
    for (int i = 0; i < 3; i++)
        if (voltage[i] > 0.0f) set(&s_state.grid_voltage[i], voltage[i]);
    derive();
    xSemaphoreGive(s_lock);
}

void energy_invalidate_production(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_state.production_w.valid = false;
    derive();
    xSemaphoreGive(s_lock);
}

void energy_invalidate_grid(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_state.grid_w.valid = false;
    derive();
    xSemaphoreGive(s_lock);
}

