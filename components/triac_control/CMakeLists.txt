idf_component_register(
    SRCS "src/triac_control.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio esp_timer 
)
