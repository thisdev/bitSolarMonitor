#include "energy.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"

static energy_state_t    s_state;
static SemaphoreHandle_t s_lock;

/* Meldet der Speicher seine Kapazitaet selbst, gilt dieser Wert. Sonst
 * bleibt es beim fest eingetragenen. */
static float             s_capacity_wh = 0.0f;

static void set(measurement_t *m, float v)
{
    m->value = v;
    m->valid = true;
}

/* Haus und Reserve ergeben sich aus den Rohwerten -- einmal zentral
 * gerechnet, damit die Oberflaeche keine Physik kennen muss. */
static void derive(void)
{
#if CONFIG_PVMON_HA_ENABLE
    /* Aus Prozent wird erst mit der Kapazitaet eine Aussage, mit der man
     * etwas anfangen kann. "78 Prozent" beantwortet nicht die Frage, ob es
     * noch fuer eine Waschmaschine reicht, "1,6 kWh" schon. */
    if (s_state.soc_pct.valid) {
        /* Nur der Teil oberhalb der Entladegrenze ist wirklich abrufbar.
         * Steht der Speicher auf seiner Untergrenze, sind es ehrliche
         * 0,0 kWh, auch wenn die Prozentanzeige noch etwas anderes
         * suggeriert. */
        float kapazitaet = s_capacity_wh > 0.0f
                         ? s_capacity_wh
                         : (float)CONFIG_PVMON_HA_CAPACITY_WH;
        float nutzbar = (s_state.soc_pct.value - (float)CONFIG_PVMON_HA_RESERVE_PCT)
                        / 100.0f * kapazitaet;
        set(&s_state.reserve_wh, nutzbar > 0.0f ? nutzbar : 0.0f);
    } else {
        s_state.reserve_wh.valid = false;
    }
#endif

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

/* Unterhalb dieser Leistung gilt der Speicher als in Ruhe. Darunter ist es
 * meist nur die Eigenversorgung der Elektronik. */
#define BATT_IDLE_W 15.0f

void energy_set_soc(float percent)
{
    static float last = -1.0f;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    set(&s_state.soc_pct, percent);

    /* Ersatzweg, wenn keine Leistung gemeldet wird: die Veraenderung des
     * Ladezustands. Traeger, aber besser als gar keine Richtung. */
    if (!s_state.battery_w.valid && last >= 0.0f) {
        if      (percent > last + 0.05f) s_state.battery_dir =  1;
        else if (percent < last - 0.05f) s_state.battery_dir = -1;
    }
    last = percent;

    derive();
    xSemaphoreGive(s_lock);
}

void energy_set_capacity_wh(float wh)
{
    if (wh > 0.0f) s_capacity_wh = wh;
}

void energy_set_battery_power(float watt)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    set(&s_state.battery_w, watt);
    if      (watt >  BATT_IDLE_W) s_state.battery_dir =  1;
    else if (watt < -BATT_IDLE_W) s_state.battery_dir = -1;
    else                          s_state.battery_dir =  0;
    xSemaphoreGive(s_lock);
}

void energy_invalidate_soc(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_state.soc_pct.valid = false;
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

