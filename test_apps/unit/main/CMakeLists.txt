set(SRC_DIR ${CMAKE_CURRENT_LIST_DIR}/../../../main)

idf_component_register(
    SRCS "test_ds18b20.cpp" "test_main.cpp"
         "test_pid.cpp" "test_sensor_manager.cpp"
    INCLUDE_DIRS "."
    REQUIRES unity pid_controller ds18b20 sensors
    WHOLE_ARCHIVE
)