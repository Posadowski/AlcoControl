set(SRC_DIR ${CMAKE_CURRENT_LIST_DIR}/../../../main)

idf_component_register(
    SRCS "test_main.cpp"
         "test_pid.cpp"
    INCLUDE_DIRS "."
    REQUIRES unity pid_controller
    WHOLE_ARCHIVE
)