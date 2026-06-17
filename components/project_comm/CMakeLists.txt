idf_component_register(
    SRCS
        "command_server.c"
        "telemetry_server.c"

    INCLUDE_DIRS
        "."
        "../../main"

    REQUIRES
        project_control
        esp8266
        freertos
        log
        lwip
        tcpip_adapter
        nvs_flash
)
