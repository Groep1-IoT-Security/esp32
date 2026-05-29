idf_component_register(SRCS "gattc_demo.c"
                       INCLUDE_DIRS "."
                       REQUIRES "driver" "esp_driver_spi" "esp_driver_i2c"
                       PRIV_REQUIRES "bt" "nvs_flash")

# Force the inclusion of the legacy driver path for the managed component
target_include_directories(${COMPONENT_LIB} PUBLIC 
    "${IDF_PATH}/components/driver/spi/include"
    "${IDF_PATH}/components/driver/i2c/include"
)