idf_component_register(
    SRCS "src/DS18B20Sensor.cpp"
    INCLUDE_DIRS "include"
    REQUIRES sensors onewiretemp esp_timer
)
