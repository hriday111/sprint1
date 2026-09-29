cmake_minimum_required(VERSION 3.10)
project(sprint1 C CXX)

set(SPRINT1_SRC_ROOT ${CMAKE_CURRENT_SOURCE_DIR} CACHE INTERNAL "")

add_subdirectory(driver)
add_subdirectory(camera-sdk)
