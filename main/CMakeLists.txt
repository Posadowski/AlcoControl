idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES sensors ds18b20 onewiretemp wifi http zero_cross triac_control pid_controller heater_manager ota_manager
)
