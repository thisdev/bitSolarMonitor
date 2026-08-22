idf_component_register(
    SRCS "bitsolarmonitor.c" "energy.c" "shelly.c" "net.c" "ui.c" "backlight.c" "clock.c" "history.c" "ha.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_http_client nvs_flash json esp_netif esp_timer esp_driver_gpio
)
