cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")

set(COMPONENTS main pid_controller)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(alco_unit_tests)