idf_component_register(
    SRCS "src/GpioOneWireTempBus.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio sensors
)
