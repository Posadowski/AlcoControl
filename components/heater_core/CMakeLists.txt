idf_component_register(
    SRCS "src/burst_scheduler.cpp" "src/heater_safety.cpp" "src/heater_controller.cpp"
    INCLUDE_DIRS "include"
    REQUIRES pid_controller
)