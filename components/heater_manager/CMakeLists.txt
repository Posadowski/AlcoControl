idf_component_register(
    SRCS "src/heater_manager.cpp"
    INCLUDE_DIRS "include"
    REQUIRES zero_cross triac_control pid_controller heater_core esp_timer
)
