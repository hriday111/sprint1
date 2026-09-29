
set(ILLUMINATION "laser" CACHE STRING "Illumination backend: laser or led")
set(MOTOR "a4988" CACHE STRING "Motor backend: a4988 or sg90")

if(ILLUMINATION STREQUAL "laser")
    set(ILLUM_SRC ${CMAKE_CURRENT_SOURCE_DIR}/Laser/laser.c)
elseif(ILLUMINATION STREQUAL "led")
    set(ILLUM_SRC ${CMAKE_CURRENT_SOURCE_DIR}/Led/led.c)
else()
    message(FATAL_ERROR "Unknown ILLUMINATION=${ILLUMINATION}; use laser or led")
endif()

if(MOTOR STREQUAL "a4988")
    set(MOTOR_SRC ${CMAKE_CURRENT_SOURCE_DIR}/A4988/stepper.c)
elseif(MOTOR STREQUAL "sg90")
    set(MOTOR_SRC ${CMAKE_CURRENT_SOURCE_DIR}/SG-90/servo.c)
else()
    message(FATAL_ERROR "Unknown MOTOR=${MOTOR}; use a4988 or sg90")
endif()

add_executable(driver
    ${CMAKE_CURRENT_SOURCE_DIR}/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/device.c
    ${SPRINT1_SRC_ROOT}/common/utils.c
    ${SPRINT1_SRC_ROOT}/logging/logger.c
    ${SPRINT1_SRC_ROOT}/logging/loggerconf.c
    ${SPRINT1_SRC_ROOT}/logging/crash.c
    ${SPRINT1_SRC_ROOT}/ipc/ipc_server.c
    ${ILLUM_SRC}
    ${MOTOR_SRC}
)

target_compile_features(driver PRIVATE c_std_17)
target_compile_definitions(driver PRIVATE _GNU_SOURCE)

target_include_directories(driver PRIVATE
    ${SPRINT1_SRC_ROOT}
    ${SPRINT1_SRC_ROOT}/common
    ${SPRINT1_SRC_ROOT}/logging
    ${SPRINT1_SRC_ROOT}/ipc
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/Laser
    ${CMAKE_CURRENT_SOURCE_DIR}/Led
    ${CMAKE_CURRENT_SOURCE_DIR}/A4988
    ${CMAKE_CURRENT_SOURCE_DIR}/SG-90
)

target_link_libraries(driver PRIVATE
    pthread
    atomic
    wiringPi
)
