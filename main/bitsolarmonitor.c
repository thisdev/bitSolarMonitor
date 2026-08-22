/*
 * PV-Monitor auf dem Waveshare ESP32-S3-Touch-AMOLED-1.8
 *
 * Stufe 1: Erzeugung vom Shelly Plug am Wechselrichter-Ausgang.
 * Stufe 2: zusaetzlich Netzbilanz vom Shelly Pro 3EM (in menuconfig zuschaltbar).
 *
 * Was nicht konfiguriert ist, wird per mDNS gesucht. Was nicht antwortet,
 * wird nicht angezeigt -- der Monitor laeuft in jeder Ausbaustufe.
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "mdns.h"
#include "bsp/esp-bsp.h"
#include "sdkconfig.h"

#include "energy.h"
#include "shelly.h"
#include "net.h"
#include "ui.h"
#include "backlight.h"
#include "clock.h"
#include "history.h"

static const char *TAG = "pvmon";

static char s_plug_host[SHELLY_HOST_LEN];
static char s_em_host[SHELLY_HOST_LEN];

static void status(const char *text)
{
    bsp_display_lock(0);
    ui_set_status(text);
    bsp_display_unlock();
}

static void poll_task(void *arg)
{
    energy_state_t st;
    int fails = 0;

    while (1) {
        shelly_plug_t plug;
        shelly_em_t   em;

        if (s_plug_host[0] && shelly_read_plug(s_plug_host, &plug) == ESP_OK) {
            energy_set_production(plug.watt, plug.meter_kwh, plug.temp_c);
            history_add(plug.watt);
            history_meter(plug.meter_kwh);
            fails = 0;
        } else {
            /* Erst nach mehreren Fehlschlaegen aufgeben -- ein einzelner
             * verpasster Abruf soll die Anzeige nicht leer raeumen. */
            if (++fails >= 3) energy_invalidate_production();
        }

#if CONFIG_PVMON_EM_ENABLE
        if (s_em_host[0] && shelly_read_em(s_em_host, &em) == ESP_OK) {
            energy_set_grid(em.total_w, em.voltage);
        } else {
            energy_invalidate_grid();
        }
#endif

        energy_get(&st);

        bsp_display_lock(0);
        ui_update(&st);
        if (st.production_w.valid) {
            ui_set_status("");
        } else {
            ui_set_status("keine Verbindung zum Plug");
        }
        bsp_display_unlock();

        if (st.production_w.valid) {
            char hhmm[8];
            clock_hhmm(hhmm, sizeof(hhmm));
            ESP_LOGI(TAG, "[%s] Erzeugung %.0f W | Netz %s%.0f W | Haus %s%.0f W",
                     hhmm, st.production_w.value,
                     st.grid_w.valid  ? "" : "n/a ", st.grid_w.valid  ? st.grid_w.value  : 0,
                     st.house_w.valid ? "" : "n/a ", st.house_w.valid ? st.house_w.value : 0);
        }

        vTaskDelay(pdMS_TO_TICKS(CONFIG_PVMON_POLL_INTERVAL_MS));
    }
}

void app_main(void)
{
    energy_init();
    history_init();

    bsp_display_start();
    bsp_display_backlight_on();
    bsp_display_lock(0);
    ui_create();
    bsp_display_unlock();

    /* Frueh starten: Taste und Ruheabsenkung sollen auch dann arbeiten,
     * wenn es mit dem WLAN nicht klappt und man am Geraet steht. */
    backlight_start();

    status("verbinde mit WLAN ...");
    if (net_connect() != ESP_OK) {
        status("WLAN fehlgeschlagen (local.defaults pruefen)");
        return;
    }

    ESP_ERROR_CHECK(mdns_init());

    /* Uhr laeuft im Hintergrund an, die Anzeige wartet nicht darauf. */
    clock_start();

    strlcpy(s_plug_host, CONFIG_PVMON_PLUG_HOST, sizeof(s_plug_host));
#if CONFIG_PVMON_EM_ENABLE
    strlcpy(s_em_host, CONFIG_PVMON_EM_HOST, sizeof(s_em_host));
    bool want_em = true;
#else
    bool want_em = false;
#endif

    if (!s_plug_host[0] || (want_em && !s_em_host[0])) {
        status("suche Shelly-Geraete ...");
        shelly_discover(s_plug_host, want_em ? s_em_host : NULL);
    }

    if (!s_plug_host[0]) {
        status("kein Shelly Plug gefunden");
        ESP_LOGE(TAG, "Kein Plug gefunden. IP in menuconfig eintragen.");
    } else {
        ESP_LOGI(TAG, "Plug: %s", s_plug_host);
        if (s_em_host[0]) ESP_LOGI(TAG, "Zaehler: %s", s_em_host);
    }

    bsp_display_lock(0);
    ui_set_hosts(s_plug_host, s_em_host);
    bsp_display_unlock();

    xTaskCreate(poll_task, "poll", 4096, NULL, 5, NULL);
}
