#include "history.h"
#include "clock.h"
#include "lvgl.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "verlauf";

static int32_t s_points[HIST_SLOTS];

/* Mittelung innerhalb des laufenden Fuenf-Minuten-Fensters */
static int   s_slot     = -1;
static float s_sum      = 0.0f;
static int   s_count    = 0;

/* Tagesertrag aus der Differenz des Zaehlerstands */
static int   s_day      = 0;
static float s_ref_kwh  = -1.0f;
static float s_today    = 0.0f;
static bool  s_full_day = false;

void history_init(void)
{
    for (int i = 0; i < HIST_SLOTS; i++) s_points[i] = LV_CHART_POINT_NONE;
}

/* Ohne gestellte Uhr zaehlt die Laufzeit, dann bedeutet die Kurve
 * "seit dem Einschalten" statt "heute". */
static int current_slot(void)
{
    int min = clock_minute_of_day();
    if (min >= 0) return min / 5;
    return (int)(esp_timer_get_time() / 1000000 / 300) % HIST_SLOTS;
}

static void flush(void)
{
    if (s_slot >= 0 && s_count > 0)
        s_points[s_slot] = (int32_t)(s_sum / s_count + 0.5f);
}

void history_add(float watt)
{
    int slot = current_slot();

    if (slot != s_slot) {
        flush();
        /* Sprung ueber Mitternacht: alte Kurve verwerfen und neu anfangen. */
        if (s_slot > slot) history_init();
        s_slot  = slot;
        s_sum   = 0.0f;
        s_count = 0;
    }

    s_sum += watt;
    s_count++;
    /* Auch innerhalb des Fensters fortschreiben, damit die Kurve
     * nicht fuenf Minuten lang stehenbleibt. */
    s_points[s_slot] = (int32_t)(s_sum / s_count + 0.5f);
}

void history_fill_demo(void)
{
    /* Ein plausibler Sommertag: Sonnenaufgang gegen halb sieben, Scheitel
     * kurz nach eins, Untergang gegen halb neun. Dazu zwei Wolkenphasen,
     * die ueber mehrere Faecher laufen. Einzelne abgesenkte Punkte saehen
     * nach Messfehlern aus, nicht nach Wetter. */
    const int von = 78, bis = 246, hoch = 158;      /* Faecher zu fuenf Minuten */

    for (int i = 0; i < HIST_SLOTS; i++) {
        if (i < von || i > bis) { s_points[i] = LV_CHART_POINT_NONE; continue; }

        float x = (float)(i - hoch) / (float)((bis - von) / 2);
        float y = 780.0f * (1.0f - x * x);
        if (y < 0.0f) y = 0.0f;

        /* Zwei Wolkenphasen von je gut einer Stunde */
        if (i >= 108 && i <= 122) y *= 0.55f;
        if (i >= 186 && i <= 196) y *= 0.70f;

        /* Leichte Unruhe, damit es nicht nach Lehrbuch aussieht */
        y *= 0.97f + 0.03f * (float)((i * 7) % 5);

        if (y > 800.0f) y = 800.0f;
        s_points[i] = (int32_t)(y / 5.0f + 0.5f) * 5;   /* auf Fuenfer runden */
    }
    s_slot = -1;
}

int32_t *history_data(void)
{
    return s_points;
}

void history_meter(float meter_kwh)
{
    int day = clock_day_key();

    if (s_ref_kwh < 0.0f) {
        s_ref_kwh = meter_kwh;
        s_day     = day;
    } else if (day != 0 && day != s_day) {
        bool had_day = (s_day != 0);
        s_day      = day;
        s_ref_kwh  = meter_kwh;
        s_full_day = had_day;      /* echter Tageswechsel mitgelaufen */
        ESP_LOGI(TAG, "neuer Tag %d, Referenz %.2f kWh", day, meter_kwh);
    }

    /* Zaehler zurueckgesetzt? Referenz nachziehen statt Unsinn anzeigen. */
    if (meter_kwh < s_ref_kwh) {
        s_ref_kwh  = meter_kwh;
        s_full_day = false;
    }

    s_today = meter_kwh - s_ref_kwh;
}

float history_today_kwh(void) { return s_today; }
bool  history_is_full_day(void) { return s_full_day && clock_valid(); }
