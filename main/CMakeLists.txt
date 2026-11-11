idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES app_core http sensors ds18b20 onewiretemp wifi ota_manager heater_manager
)
