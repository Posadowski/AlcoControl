idf_component_register(
    SRCS "src/triac_control.cpp"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_timer 
)
