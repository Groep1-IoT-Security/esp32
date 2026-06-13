idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_timer nvs_flash esp_wifi esp_http_server json bt
                    EMBED_FILES "../malicious_payload.bin")

