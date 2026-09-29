

find_package(PkgConfig REQUIRED)
pkg_check_modules(OPENCV REQUIRED opencv4)

set(CAM_SRC ${CMAKE_CURRENT_SOURCE_DIR}/thorlabs-dcc1545m/cam.c)

set(CAMERA_COMMON_SOURCES
    ${CAM_SRC}
    ${CMAKE_CURRENT_SOURCE_DIR}/vision/roi.c
    ${SPRINT1_SRC_ROOT}/common/utils.c
    ${SPRINT1_SRC_ROOT}/ipc/ipc_client.c
    ${SPRINT1_SRC_ROOT}/logging/logger.c
    ${SPRINT1_SRC_ROOT}/logging/loggerconf.c
    ${SPRINT1_SRC_ROOT}/logging/crash.c
)

set(CAMERA_INCLUDE_DIRS
    ${SPRINT1_SRC_ROOT}
    ${SPRINT1_SRC_ROOT}/common
    ${SPRINT1_SRC_ROOT}/logging
    ${SPRINT1_SRC_ROOT}/ipc
    ${SPRINT1_SRC_ROOT}/TUI
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/app
    ${CMAKE_CURRENT_SOURCE_DIR}/vision
    ${CMAKE_CURRENT_SOURCE_DIR}/thorlabs-dcc1545m
    ${OPENCV_INCLUDE_DIRS}
)

set(CAMERA_LIBS
    ueye_api
    m
    pthread
)

add_executable(sweep_run
    ${CMAKE_CURRENT_SOURCE_DIR}/app/sweep_run.c
    ${CMAKE_CURRENT_SOURCE_DIR}/app/sweep_tui.c
    ${CAMERA_COMMON_SOURCES}
)

target_compile_features(sweep_run PRIVATE c_std_17)
target_compile_definitions(sweep_run PRIVATE _GNU_SOURCE)
target_include_directories(sweep_run PRIVATE ${CAMERA_INCLUDE_DIRS})
target_link_libraries(sweep_run PRIVATE ${CAMERA_LIBS})

add_executable(calibrate
    ${CMAKE_CURRENT_SOURCE_DIR}/app/calibrate.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/vision/detect.cpp
    ${CAMERA_COMMON_SOURCES}
)

target_compile_features(calibrate PRIVATE c_std_17 cxx_std_17)
target_compile_definitions(calibrate PRIVATE _GNU_SOURCE)
target_include_directories(calibrate PRIVATE ${CAMERA_INCLUDE_DIRS})
target_link_directories(calibrate PRIVATE ${OPENCV_LIBRARY_DIRS})
target_link_libraries(calibrate PRIVATE
    ${CAMERA_LIBS}
    ${OPENCV_LIBRARIES}
)

add_executable(calibrate2
    ${CMAKE_CURRENT_SOURCE_DIR}/app/calibrate2.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/vision/detect.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/vision/roi_interp.c
    ${CAMERA_COMMON_SOURCES}
)

target_compile_features(calibrate2 PRIVATE c_std_17 cxx_std_17)
target_compile_definitions(calibrate2 PRIVATE _GNU_SOURCE)
target_include_directories(calibrate2 PRIVATE ${CAMERA_INCLUDE_DIRS})
target_link_directories(calibrate2 PRIVATE ${OPENCV_LIBRARY_DIRS})
target_link_libraries(calibrate2 PRIVATE
    ${CAMERA_LIBS}
    ${OPENCV_LIBRARIES}
)
