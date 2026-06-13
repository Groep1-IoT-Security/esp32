idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_wifi nvs_flash esp_http_client esp_https_ota)
