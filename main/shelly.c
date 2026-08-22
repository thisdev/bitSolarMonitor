#include "shelly.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "mdns.h"

static const char *TAG = "shelly";

/* Antwortpuffer kommen auf den Heap, nicht auf den Stack: Der main-Task
 * hat per Vorgabe nur 3584 Bytes, und der HTTP-Client braucht davon selbst
 * einen guten Teil. Ein lokales char[2048] reisst ihm den Boden weg. */
#define RESP_MAX 2048

/* Holt eine RPC-Antwort als Text. Shelly-Antworten sind klein genug,
 * um sie am Stueck einzulesen. */
static esp_err_t rpc_get(const char *host, const char *method, char *buf, size_t buf_len)
{
    char url[160];
    snprintf(url, sizeof(url), "http://%s/rpc/%s", host, method);

    esp_http_client_config_t cfg = {
        .url                 = url,
        .method              = HTTP_METHOD_GET,
        .timeout_ms          = 4000,
        .disable_auto_redirect = true,
    };
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    if (!cli) return ESP_FAIL;

    esp_err_t err = esp_http_client_open(cli, 0);
    if (err != ESP_OK) goto out;

    esp_http_client_fetch_headers(cli);
    if (esp_http_client_get_status_code(cli) != 200) {
        err = ESP_ERR_INVALID_RESPONSE;
        goto out;
    }

    int len = esp_http_client_read_response(cli, buf, (int)buf_len - 1);
    if (len <= 0) {
        err = ESP_ERR_INVALID_SIZE;
        goto out;
    }
    buf[len] = '\0';
    err = ESP_OK;

out:
    esp_http_client_close(cli);
    esp_http_client_cleanup(cli);
    return err;
}

/* Verschachteltes Feld lesen, etwa ret_aenergy.total */
static bool json_nested(cJSON *root, const char *outer, const char *inner, float *out)
{
    cJSON *o = cJSON_GetObjectItemCaseSensitive(root, outer);
    cJSON *i = o ? cJSON_GetObjectItemCaseSensitive(o, inner) : NULL;
    if (!cJSON_IsNumber(i)) return false;
    *out = (float)i->valuedouble;
    return true;
}

esp_err_t shelly_read_plug(const char *host, shelly_plug_t *out)
{
    char *buf = malloc(RESP_MAX);
    if (!buf) return ESP_ERR_NO_MEM;

    esp_err_t err = rpc_get(host, "Switch.GetStatus?id=0", buf, RESP_MAX);
    if (err != ESP_OK) { free(buf); return err; }

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) return ESP_ERR_INVALID_RESPONSE;

    cJSON *ap = cJSON_GetObjectItemCaseSensitive(root, "apower");
    if (!cJSON_IsNumber(ap)) { cJSON_Delete(root); return ESP_ERR_NOT_FOUND; }

    /* Eingespeiste Leistung ist negativ -> als Erzeugung positiv fuehren.
     * Haengt am Plug doch ein Verbraucher, bleibt der Wert schlicht negativ
     * und die Anzeige sagt die Wahrheit. */
    out->watt = -(float)ap->valuedouble;

    float wh = 0.0f;
    out->meter_kwh = json_nested(root, "ret_aenergy", "total", &wh) ? wh / 1000.0f : 0.0f;

    float t = 0.0f;
    out->temp_c = json_nested(root, "temperature", "tC", &t) ? t : 0.0f;

    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t shelly_read_em(const char *host, shelly_em_t *out)
{
    char *buf = malloc(RESP_MAX);
    if (!buf) return ESP_ERR_NO_MEM;

    esp_err_t err = rpc_get(host, "EM.GetStatus?id=0", buf, RESP_MAX);
    if (err != ESP_OK) { free(buf); return err; }

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) return ESP_ERR_INVALID_RESPONSE;

    cJSON *tot = cJSON_GetObjectItemCaseSensitive(root, "total_act_power");
    if (!cJSON_IsNumber(tot)) { cJSON_Delete(root); return ESP_ERR_NOT_FOUND; }
    out->total_w = (float)tot->valuedouble;

    static const char *keys[3] = { "a_voltage", "b_voltage", "c_voltage" };
    for (int i = 0; i < 3; i++) {
        cJSON *v = cJSON_GetObjectItemCaseSensitive(root, keys[i]);
        out->voltage[i] = cJSON_IsNumber(v) ? (float)v->valuedouble : 0.0f;
    }

    cJSON_Delete(root);
    return ESP_OK;
}

/* --- Automatische Suche ------------------------------------------------- */

static bool probe(const char *ip, const char *method, const char *expect_key)
{
    char *buf = malloc(RESP_MAX);
    if (!buf) return false;

    bool ok = (rpc_get(ip, method, buf, RESP_MAX) == ESP_OK) &&
              (strstr(buf, expect_key) != NULL);
    free(buf);
    return ok;
}

esp_err_t shelly_discover(char *plug_host, char *em_host)
{
    bool need_plug = (plug_host[0] == '\0');
    bool need_em   = (em_host   != NULL && em_host[0] == '\0');
    if (!need_plug && !need_em) return ESP_OK;

    ESP_LOGI(TAG, "Suche Shelly-Geraete per mDNS ...");
    mdns_result_t *results = NULL;
    esp_err_t err = mdns_query_ptr("_shelly", "_tcp", 3000, 20, &results);
    if (err != ESP_OK) return err;

    for (mdns_result_t *r = results; r; r = r->next) {
        if (!r->addr) continue;

        char ip[16];
        esp_ip4_addr_t a = r->addr->addr.u_addr.ip4;
        snprintf(ip, sizeof(ip), IPSTR, IP2STR(&a));

        ESP_LOGI(TAG, "  gefunden: %s -> %s", r->instance_name ? r->instance_name : "?", ip);

        if (need_em && probe(ip, "EM.GetStatus?id=0", "total_act_power")) {
            strlcpy(em_host, ip, SHELLY_HOST_LEN);
            need_em = false;
            ESP_LOGI(TAG, "  -> Energiezaehler");
            continue;
        }
        if (need_plug && probe(ip, "Switch.GetStatus?id=0", "apower")) {
            strlcpy(plug_host, ip, SHELLY_HOST_LEN);
            need_plug = false;
            ESP_LOGI(TAG, "  -> Plug");
        }
    }
    mdns_query_results_free(results);

    return (plug_host[0] || (em_host && em_host[0])) ? ESP_OK : ESP_ERR_NOT_FOUND;
}
