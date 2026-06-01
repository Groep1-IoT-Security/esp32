idf_component_register(SRCS "gatts_demo.c" "vulnerabilities.c"
                    REQUIRES 
                    bt 
                    nvs_flash
                    esp_wifi 
                    esp_event 
                    esp_netif 
                    esp_http_server 
                    mqtt 
                    i2cdev 
                    bmp280
                    rogue_weather_codec
                    INCLUDE_DIRS ".")
