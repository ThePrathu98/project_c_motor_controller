idf_component_register(
    SRCS
        "motor_hal.c"
        "encoder_hal.c"
        "adc_hal.c"
        "led_hal.c"

    INCLUDE_DIRS
        "."

    REQUIRES
        esp8266
        log
        pthread
)
