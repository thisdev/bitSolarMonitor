#include "ui.h"
#include "clock.h"
#include "history.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "sdkconfig.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "esp_log.h"

#define COL_BG        lv_color_black()
#define COL_TEXT      lv_color_hex(0xFFFFFF)
#define COL_DIM       lv_color_hex(0x707070)
#define COL_SUN       lv_color_hex(0xFFC400)   /* Erzeugung          */
#define COL_FEED      lv_color_hex(0x4CAF50)   /* Einspeisung, gut   */
#define COL_DRAW      lv_color_hex(0xFF5252)   /* Netzbezug          */
#define COL_TRACK     lv_color_hex(0x202020)

#define PAGES 3

/* Das Panel hat abgerundete Ecken. Alles, was zu weit nach aussen und
 * zugleich zu weit nach oben rutscht, wird angeschnitten. Diese beiden
 * Masse halten die Kopfzeile im sichtbaren Bereich. */
#define HEAD_Y      26     /* Abstand von oben   */
#define HEAD_INSET  44     /* Abstand vom Rand   */

/* --- Seite 1 --- */
static lv_obj_t *s_arc, *s_watt, *s_unit, *s_caption, *s_sub;
static lv_obj_t *s_status, *s_footer, *s_clock, *s_temp;
static lv_obj_t *s_tile_grid, *s_tile_grid_val, *s_tile_grid_cap;
static lv_obj_t *s_tile_house, *s_tile_house_val, *s_tile_house_cap;

/* --- Seite 2 --- */
static lv_obj_t *s_chart, *s_today, *s_today_cap;
static lv_chart_series_t *s_series;

/* --- Seite 3 --- */
static lv_obj_t *s_diag;

/* --- Seitenwechsel --- */
static lv_obj_t *s_pages[PAGES], *s_dots[PAGES];
static int       s_page;

static char s_plug_host[40], s_em_host[40];

/* Deutsches Dezimalkomma. printf kennt nur den Punkt. */
static void komma(char *buf)
{
    char *p = strchr(buf, '.');
    if (p) *p = ',';
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, lv_color_t col)
{
    lv_obj_t *l = lv_label_create(parent);
    if (font) lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, col, 0);
    return l;
}

/* Kachel fuer einen Zusatzwert. Sichtbar nur, wenn der Wert gilt. */
static lv_obj_t *make_tile(lv_obj_t *parent, lv_obj_t **val, lv_obj_t **cap)
{
    lv_obj_t *t = lv_obj_create(parent);
    lv_obj_set_size(t, 160, 78);
    lv_obj_set_style_bg_color(t, lv_color_hex(0x111111), 0);
    lv_obj_set_style_border_width(t, 0, 0);
    lv_obj_set_style_radius(t, 12, 0);
    lv_obj_set_style_pad_all(t, 8, 0);
    lv_obj_clear_flag(t, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(t, LV_OBJ_FLAG_GESTURE_BUBBLE);

    *val = label(t, &lv_font_montserrat_28, COL_TEXT);
    lv_obj_align(*val, LV_ALIGN_TOP_MID, 0, 0);
    *cap = label(t, NULL, COL_DIM);
    lv_obj_align(*cap, LV_ALIGN_BOTTOM_MID, 0, 0);

    lv_obj_add_flag(t, LV_OBJ_FLAG_HIDDEN);
    return t;
}

/* Seiten werden hart umgeschaltet statt gescrollt. Eine Wischanimation
 * zeichnet den kompletten Bildschirm dutzende Male neu; ueber die QSPI-
 * Anbindung reicht die Bandbreite dafuer nicht, es ruckelt und einzelne
 * Teilbilder mischen sich sichtbar. Ein harter Wechsel kostet genau ein
 * Neuzeichnen und sieht dadurch sauber aus. */
static void show_page(int n)
{
    if (n < 0) n = 0;
    if (n >= PAGES) n = PAGES - 1;
    s_page = n;

    ESP_LOGI("ui", "Seite %d", n + 1);

    for (int i = 0; i < PAGES; i++) {
        if (i == n) lv_obj_clear_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
        else        lv_obj_add_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_color(s_dots[i], i == n ? COL_TEXT : COL_TRACK, 0);
    }
}

static void on_gesture(lv_event_t *e)
{
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
    if      (dir == LV_DIR_LEFT)  show_page(s_page + 1);
    else if (dir == LV_DIR_RIGHT) show_page(s_page - 1);
}

static void build_page_main(lv_obj_t *p)
{
    /* Kurz gehalten: Neben Temperatur und Uhr bleibt in der Kopfzeile
     * kein Platz fuer einen langen Namen, und "PV-Monitor" erklaert
     * ohnehin nur, was man darunter sieht. */
    lv_obj_t *t = label(p, NULL, COL_DIM);
    lv_label_set_text(t, "bitSolarMonitor");
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, HEAD_Y);

    s_temp = label(p, NULL, COL_DIM);
    lv_label_set_text(s_temp, "");
    lv_obj_align(s_temp, LV_ALIGN_TOP_LEFT, HEAD_INSET, HEAD_Y);

    s_clock = label(p, NULL, COL_DIM);
    lv_label_set_text(s_clock, "--:--");
    lv_obj_align(s_clock, LV_ALIGN_TOP_RIGHT, -HEAD_INSET, HEAD_Y);

    s_arc = lv_arc_create(p);
    lv_obj_set_size(s_arc, 270, 270);
    lv_obj_align(s_arc, LV_ALIGN_TOP_MID, 0, 52);
    lv_arc_set_rotation(s_arc, 135);
    lv_arc_set_bg_angles(s_arc, 0, 270);
    lv_arc_set_range(s_arc, 0, CONFIG_PVMON_INVERTER_LIMIT_W);
    lv_arc_set_value(s_arc, 0);
    lv_obj_remove_style(s_arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(s_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(s_arc, 16, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arc, 16, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_arc, COL_TRACK, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_arc, COL_SUN, LV_PART_INDICATOR);

    s_caption = label(p, NULL, COL_DIM);
    lv_label_set_text(s_caption, "Erzeugung");
    lv_obj_align(s_caption, LV_ALIGN_TOP_MID, 0, 120);

    s_watt = label(p, &lv_font_montserrat_48, COL_TEXT);
    lv_label_set_text(s_watt, "--");
    lv_obj_align(s_watt, LV_ALIGN_TOP_MID, 0, 160);

    s_unit = label(p, &lv_font_montserrat_28, COL_SUN);
    lv_label_set_text(s_unit, "Watt");
    lv_obj_align(s_unit, LV_ALIGN_TOP_MID, 0, 214);

    /* Bleibt im Sonnenbetrieb leer und traegt abends den Vorrat. */
    s_sub = label(p, NULL, COL_DIM);
    lv_label_set_text(s_sub, "");
    lv_obj_align(s_sub, LV_ALIGN_TOP_MID, 0, 252);

    s_tile_grid  = make_tile(p, &s_tile_grid_val,  &s_tile_grid_cap);
    s_tile_house = make_tile(p, &s_tile_house_val, &s_tile_house_cap);
    lv_label_set_text(s_tile_grid_cap,  "Netz");
    lv_label_set_text(s_tile_house_cap, "Haus");

    s_footer = label(p, NULL, COL_DIM);
    lv_label_set_text(s_footer, "");
    lv_obj_align(s_footer, LV_ALIGN_BOTTOM_MID, 0, -34);

    s_status = label(p, NULL, COL_DIM);
    lv_label_set_text(s_status, "Starte ...");
    lv_obj_align(s_status, LV_ALIGN_BOTTOM_MID, 0, -18);
}

static void build_page_chart(lv_obj_t *p)
{
    lv_obj_t *t = label(p, NULL, COL_DIM);
    lv_label_set_text(t, "Tagesverlauf");
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, HEAD_Y);

    s_today = label(p, &lv_font_montserrat_48, COL_SUN);
    lv_label_set_text(s_today, "0,00");
    lv_obj_align(s_today, LV_ALIGN_TOP_MID, 0, 54);

    s_today_cap = label(p, NULL, COL_DIM);
    lv_label_set_text(s_today_cap, "kWh seit Start");
    lv_obj_align(s_today_cap, LV_ALIGN_TOP_MID, 0, 110);

    s_chart = lv_chart_create(p);
    lv_obj_set_size(s_chart, 330, 230);
    lv_obj_align(s_chart, LV_ALIGN_TOP_MID, 0, 144);
    lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(s_chart, HIST_SLOTS);
    lv_chart_set_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, 0, CONFIG_PVMON_INVERTER_LIMIT_W);
    lv_chart_set_div_line_count(s_chart, 5, 5);
    lv_obj_set_style_bg_color(s_chart, lv_color_hex(0x0A0A0A), 0);
    lv_obj_set_style_border_width(s_chart, 0, 0);
    lv_obj_set_style_line_color(s_chart, COL_TRACK, LV_PART_MAIN);
    lv_obj_set_style_size(s_chart, 0, 0, LV_PART_INDICATOR);   /* keine Punkte */
    lv_obj_set_style_line_width(s_chart, 2, LV_PART_ITEMS);

    s_series = lv_chart_add_series(s_chart, COL_SUN, LV_CHART_AXIS_PRIMARY_Y);
    /* LVGL zeichnet direkt aus unserem Ringpuffer, kein Umkopieren noetig. */
    lv_chart_set_ext_y_array(s_chart, s_series, history_data());

    static const char *marks[] = { "0", "6", "12", "18", "24" };
    for (int i = 0; i < 5; i++) {
        lv_obj_t *l = label(p, NULL, COL_DIM);
        lv_label_set_text(l, marks[i]);
        lv_obj_align(l, LV_ALIGN_TOP_LEFT, 30 + i * 78, 378);
    }
    lv_obj_t *h = label(p, NULL, COL_DIM);
    lv_label_set_text(h, "Uhrzeit");
    lv_obj_align(h, LV_ALIGN_BOTTOM_MID, 0, -34);
}

static void build_page_diag(lv_obj_t *p)
{
    lv_obj_t *t = label(p, NULL, COL_DIM);
    lv_label_set_text(t, "Status");
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, HEAD_Y);

    s_diag = label(p, NULL, COL_TEXT);
    lv_label_set_text(s_diag, "");
    lv_obj_set_style_text_line_space(s_diag, 6, 0);
    lv_obj_align(s_diag, LV_ALIGN_TOP_LEFT, 40, 58);
}

void ui_create(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, COL_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scr, on_gesture, LV_EVENT_GESTURE, NULL);

    for (int i = 0; i < PAGES; i++) {
        lv_obj_t *p = lv_obj_create(scr);
        lv_obj_set_size(p, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_color(p, COL_BG, 0);
        lv_obj_set_style_border_width(p, 0, 0);
        lv_obj_set_style_pad_all(p, 0, 0);
        lv_obj_set_style_radius(p, 0, 0);
        lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
        /* Wischgesten sollen bis zum Bildschirm durchreichen. */
        lv_obj_add_flag(p, LV_OBJ_FLAG_GESTURE_BUBBLE);
        s_pages[i] = p;
    }

    build_page_main(s_pages[0]);
    build_page_chart(s_pages[1]);
    build_page_diag(s_pages[2]);

    /* Drei Punkte am unteren Rand: sonst ahnt niemand, dass es mehr
     * als eine Seite gibt. */
    for (int i = 0; i < PAGES; i++) {
        s_dots[i] = lv_label_create(scr);
        lv_label_set_text(s_dots[i], "*");
        lv_obj_set_style_text_color(s_dots[i], i == 0 ? COL_TEXT : COL_TRACK, 0);
        lv_obj_align(s_dots[i], LV_ALIGN_BOTTOM_MID, (i - 1) * 16, -10);
        lv_obj_move_foreground(s_dots[i]);
    }

    show_page(0);
}

void ui_set_status(const char *text)
{
    lv_label_set_text(s_status, text);
}

void ui_set_hosts(const char *plug, const char *em)
{
    strlcpy(s_plug_host, plug ? plug : "", sizeof(s_plug_host));
    strlcpy(s_em_host,   em   ? em   : "", sizeof(s_em_host));
}

static void update_main(const energy_state_t *st)
{
    char buf[24];
    clock_hhmm(buf, sizeof(buf));
    lv_label_set_text(s_clock, buf);

    if (st->plug_temp_c.valid) {
        snprintf(buf, sizeof(buf), "%.1f C", st->plug_temp_c.value);
        komma(buf);
        lv_label_set_text(s_temp, buf);
    }

    /* Ein Ring, zwei Aufgaben. Tagsueber gehoert er der Sonne. Liefert sie
     * nichts mehr, waere ein Ring auf null verschenkte Flaeche: dann
     * uebernimmt der Speicher, mit seinem Ladezustand als Fuellstand und
     * seiner Leistung in der Mitte. */
    bool pv_laeuft   = st->production_w.valid && st->production_w.value > 5.0f;
    bool speicher_da = st->soc_pct.valid;

    if (pv_laeuft || !speicher_da) {
        lv_arc_set_range(s_arc, 0, CONFIG_PVMON_INVERTER_LIMIT_W);
        lv_obj_set_style_arc_color(s_arc, COL_SUN, LV_PART_INDICATOR);
        lv_label_set_text(s_caption, "Erzeugung");
        lv_obj_set_style_text_color(s_unit, COL_SUN, 0);
        lv_label_set_text(s_sub, "");

        if (st->production_w.valid) {
            int w = (int)(st->production_w.value + 0.5f);
            lv_label_set_text_fmt(s_watt, "%d", w);
            lv_arc_set_value(s_arc, w > 0 ? w : 0);
            lv_obj_set_style_text_color(s_watt, COL_TEXT, 0);
        } else {
            lv_label_set_text(s_watt, "--");
            lv_arc_set_value(s_arc, 0);
            lv_obj_set_style_text_color(s_watt, COL_DIM, 0);
        }
    } else {
        lv_color_t farbe = st->battery_dir > 0 ? COL_FEED
                         : st->battery_dir < 0 ? COL_DRAW : COL_DIM;

        /* Der Ring zeigt jetzt den Ladezustand, nicht die Leistung. */
        lv_arc_set_range(s_arc, 0, 100);
        lv_arc_set_value(s_arc, (int)(st->soc_pct.value + 0.5f));
        lv_obj_set_style_arc_color(s_arc, farbe, LV_PART_INDICATOR);

        /* Umlautfrei: die eingebauten Montserrat-Schriften von LVGL
         * decken nur den ASCII-Bereich ab. */
        lv_label_set_text(s_caption, st->battery_dir > 0 ? "Laden"
                                   : st->battery_dir < 0 ? "Entladen"
                                                         : "Speicher");
        lv_obj_set_style_text_color(s_unit, farbe, 0);

        int w = st->battery_w.valid ? (int)(fabsf(st->battery_w.value) + 0.5f) : 0;
        lv_label_set_text_fmt(s_watt, "%d", w);
        lv_obj_set_style_text_color(s_watt, COL_TEXT, 0);

        if (st->reserve_wh.valid) {
            char kwh[16];
            snprintf(kwh, sizeof(kwh), "%.1f", st->reserve_wh.value / 1000.0f);
            komma(kwh);
            lv_label_set_text_fmt(s_sub, "%d%%   %s kWh",
                                  (int)(st->soc_pct.value + 0.5f), kwh);
        }
    }
    lv_obj_align(s_watt, LV_ALIGN_TOP_MID, 0, 160);
    lv_obj_align(s_sub,  LV_ALIGN_TOP_MID, 0, 252);

    if (st->grid_w.valid) {
        int g = (int)(st->grid_w.value + (st->grid_w.value < 0 ? -0.5f : 0.5f));
        lv_label_set_text_fmt(s_tile_grid_val, "%d", g);
        lv_obj_set_style_text_color(s_tile_grid_val, g > 5 ? COL_DRAW : COL_FEED, 0);
        lv_label_set_text(s_tile_grid_cap, g > 5 ? "Netzbezug" : "Einspeisung");
        lv_obj_clear_flag(s_tile_grid, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_tile_grid, LV_OBJ_FLAG_HIDDEN);
    }

    if (st->house_w.valid) {
        lv_label_set_text_fmt(s_tile_house_val, "%d", (int)(st->house_w.value + 0.5f));
        lv_obj_set_style_text_color(s_tile_house_val, COL_TEXT, 0);
        lv_label_set_text(s_tile_house_cap, "Haus");
        lv_obj_clear_flag(s_tile_house, LV_OBJ_FLAG_HIDDEN);
    } else if (st->grid_w.valid) {
        /* Sitzt ein Speicher dazwischen, ist der Hausverbrauch nicht
         * ableitbar. Die ehrliche Aussage lautet dann nicht "wie viel",
         * sondern "reicht es gerade". */
        bool carried = st->grid_w.value <= 5.0f;
        lv_label_set_text(s_tile_house_val, carried ? "OK" : "Netz");
        lv_obj_set_style_text_color(s_tile_house_val, carried ? COL_FEED : COL_DRAW, 0);
        lv_label_set_text(s_tile_house_cap, "Versorgung");
        lv_obj_clear_flag(s_tile_house, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_tile_house, LV_OBJ_FLAG_HIDDEN);
    }

    bool both = !lv_obj_has_flag(s_tile_grid,  LV_OBJ_FLAG_HIDDEN) &&
                !lv_obj_has_flag(s_tile_house, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(s_tile_grid,  LV_ALIGN_BOTTOM_MID, both ? -84 : 0, -60);
    lv_obj_align(s_tile_house, LV_ALIGN_BOTTOM_MID, both ?  84 : 0, -60);

    /* Solange der Speicher Auskunft gibt, ist sein Vorrat die nuetzlichere
     * Zahl. Ohne ihn bleibt der Zaehlerstand stehen. */
    if (!pv_laeuft && speicher_da) {
        /* Vorrat steht bereits im Ring, hier also die ruhende Erzeugung. */
        lv_obj_set_style_text_color(s_footer, COL_DIM, 0);
        lv_label_set_text(s_footer, "Erzeugung 0 W");
    } else if (st->soc_pct.valid && st->reserve_wh.valid) {
        char kwh[16];
        snprintf(kwh, sizeof(kwh), "%.1f", st->reserve_wh.value / 1000.0f);
        komma(kwh);

        /* Gruen mit Pfeil nach oben heisst laden, rot mit Pfeil nach unten
         * entladen. In Ruhe bleibt es unauffaellig. */
        const char *pfeil = "";
        lv_color_t  farbe = COL_DIM;
        if (st->battery_dir > 0) { pfeil = LV_SYMBOL_UP "  ";   farbe = COL_FEED; }
        else if (st->battery_dir < 0) { pfeil = LV_SYMBOL_DOWN "  "; farbe = COL_DRAW; }

        lv_obj_set_style_text_color(s_footer, farbe, 0);
        lv_label_set_text_fmt(s_footer, "%sSpeicher %d%%   %s kWh",
                              pfeil, (int)(st->soc_pct.value + 0.5f), kwh);
    } else if (st->production_kwh.valid) {
        lv_obj_set_style_text_color(s_footer, COL_DIM, 0);
        snprintf(buf, sizeof(buf), "%.1f", st->production_kwh.value);
        komma(buf);
        lv_label_set_text_fmt(s_footer, "%s kWh gesamt", buf);
    }
}

static void update_chart(void)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%.2f", history_today_kwh());
    komma(buf);
    lv_label_set_text(s_today, buf);
    lv_label_set_text(s_today_cap,
                      history_is_full_day() ? "kWh heute" : "kWh seit Start");
    lv_chart_refresh(s_chart);
}

static void update_diag(const energy_state_t *st)
{
    char text[320];
    int n = 0;

    n += snprintf(text + n, sizeof(text) - n, "Netzspannung\n");
    if (st->grid_voltage[0].valid) {
        for (int i = 0; i < 3; i++)
            n += snprintf(text + n, sizeof(text) - n, "  L%d   %.1f V\n",
                          i + 1, st->grid_voltage[i].value);
    } else {
        n += snprintf(text + n, sizeof(text) - n, "  keine Daten\n");
    }

    wifi_ap_record_t ap;
    n += snprintf(text + n, sizeof(text) - n, "\nWLAN\n");
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        n += snprintf(text + n, sizeof(text) - n, "  %s\n  %d dBm\n",
                      (const char *)ap.ssid, ap.rssi);
    } else {
        n += snprintf(text + n, sizeof(text) - n, "  nicht verbunden\n");
    }

    int64_t up = esp_timer_get_time() / 1000000;
    n += snprintf(text + n, sizeof(text) - n,
                  "\nLaufzeit\n  %lldd %02lld:%02lld:%02lld\n",
                  up / 86400, (up % 86400) / 3600, (up % 3600) / 60, up % 60);

    n += snprintf(text + n, sizeof(text) - n, "\nGeraete\n  %s\n",
                  s_plug_host[0] ? s_plug_host : "kein Plug");
    if (s_em_host[0])
        n += snprintf(text + n, sizeof(text) - n, "  %s\n", s_em_host);

    snprintf(text + n, sizeof(text) - n, "\nSpeicher frei\n  %u KB",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));

    lv_label_set_text(s_diag, text);
}

void ui_update(const energy_state_t *st)
{
    /* Was nicht zu sehen ist, muss auch nicht gezeichnet werden. Vor allem
     * die Kurve mit ihren 288 Punkten kostet sonst bei jedem Abruf Zeit,
     * ohne dass es jemand bemerkt. */
    switch (s_page) {
        case 0: update_main(st);  break;
        case 1: update_chart();   break;
        case 2: update_diag(st);  break;
    }
}
