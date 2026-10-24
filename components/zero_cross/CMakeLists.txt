idf_component_register(
    SRCS "src/zero_cross.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio esp_timer
)
