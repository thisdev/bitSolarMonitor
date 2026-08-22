#include "ha.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "sdkconfig.h"

static const char *TAG = "ha";

/* Die Einstellungen gibt es nur, wenn Stufe 3 eingeschaltet ist. Damit die
 * Datei trotzdem immer uebersetzt, hier leere Vorgaben. */
#ifndef CONFIG_PVMON_HA_HOST
#define CONFIG_PVMON_HA_HOST  ""
#endif
#ifndef CONFIG_PVMON_HA_TOKEN
#define CONFIG_PVMON_HA_TOKEN ""
#endif

#define RESP_MAX 1536

esp_err_t ha_read_number(const char *entity_id, float *out)
{
    if (!entity_id || !entity_id[0]) return ESP_ERR_INVALID_ARG;

    char url[192];
    snprintf(url, sizeof(url), "http://%s/api/states/%s",
             CONFIG_PVMON_HA_HOST, entity_id);

    char auth[320];
    snprintf(auth, sizeof(auth), "Bearer %s", CONFIG_PVMON_HA_TOKEN);

    char *buf = malloc(RESP_MAX);
    if (!buf) return ESP_ERR_NO_MEM;

    esp_http_client_config_t cfg = {
        .url        = url,
        .method     = HTTP_METHOD_GET,
        .timeout_ms = 5000,
    };
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    if (!cli) { free(buf); return ESP_FAIL; }

    esp_http_client_set_header(cli, "Authorization", auth);

    esp_err_t err = esp_http_client_open(cli, 0);
    if (err != ESP_OK) goto out;

    esp_http_client_fetch_headers(cli);
    int status = esp_http_client_get_status_code(cli);
    if (status != 200) {
        /* 401 heisst fast immer: Token abgelaufen oder falsch kopiert.
         * 404 heisst: Entitaet gibt es nicht, meist ein Tippfehler im Namen. */
        ESP_LOGW(TAG, "%s antwortet mit HTTP %d", entity_id, status);
        err = ESP_ERR_INVALID_RESPONSE;
        goto out;
    }

    int len = esp_http_client_read_response(cli, buf, RESP_MAX - 1);
    if (len <= 0) { err = ESP_ERR_INVALID_SIZE; goto out; }
    buf[len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) { err = ESP_ERR_INVALID_RESPONSE; goto out; }

    cJSON *state = cJSON_GetObjectItemCaseSensitive(root, "state");
    if (cJSON_IsString(state) && state->valuestring) {
        /* "unavailable" und "unknown" sind gueltige Zustaende in Home
         * Assistant, aber eben keine Zahlen. */
        char *end = NULL;
        float v = strtof(state->valuestring, &end);
        if (end && end != state->valuestring) {
            *out = v;
            err  = ESP_OK;
        } else {
            ESP_LOGW(TAG, "%s liefert \"%s\"", entity_id, state->valuestring);
            err = ESP_ERR_INVALID_STATE;
        }
    } else {
        err = ESP_ERR_NOT_FOUND;
    }
    cJSON_Delete(root);

out:
    esp_http_client_close(cli);
    esp_http_client_cleanup(cli);
    free(buf);
    return err;
}
