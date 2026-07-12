idf_component_register(
    SRCS
        "main.c"

    INCLUDE_DIRS
        "."

    PRIV_REQUIRES
        vl53l0x
        esp_driver_i2c
        esp_driver_gpio
)
