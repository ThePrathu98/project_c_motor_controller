idf_component_register(
    SRCS
        "pid.c"
        "control_task.c"
        "safety_monitor.c"

    INCLUDE_DIRS
        "."

    REQUIRES
        project_hal
        esp8266
        freertos
        log
)
