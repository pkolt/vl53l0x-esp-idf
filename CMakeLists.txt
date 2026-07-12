idf_component_register(
    SRC_DIRS
        "src"
        "private/core/src"
        "private/platform/src"

    INCLUDE_DIRS
        "include"

    PRIV_INCLUDE_DIRS
        "private/core/inc"
        "private/platform/inc"

    REQUIRES
        esp_driver_i2c
        esp_driver_gpio
        esp_timer
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC
    #VL53L0X_LOG_ENABLE=1
    USE_I2C_2V8=1
    #USE_EMPTY_STRING=0
)
