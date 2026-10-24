set(SRC_DIR ${CMAKE_CURRENT_LIST_DIR}/../../../main)

idf_component_register(
    SRCS "test_ds18b20.cpp" "test_main.cpp"
         "test_pid.cpp" "test_sensor_manager.cpp"
         "test_heater_safety.cpp" "test_burst_scheduler.cpp" 
         "test_heater_controller.cpp"
    INCLUDE_DIRS "."
    REQUIRES unity pid_controller ds18b20 sensors heater_core
    WHOLE_ARCHIVE
)