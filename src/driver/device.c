/**
 * @file device.c
 * @brief Compose motor_api + illumination_api + ipc_server for the daemon.
 */

#include "device.h"

#include "crash.h"
#include "gpio_pins.h"
#include "illumination_api.h"
#include "ipc_server.h"
#include "logger.h"
#include "motor_api.h"
#include "utils.h"

#include <stdlib.h>

struct device {
    motor_t *motor;
    illumination_t *illumination;
};
void load_params(char *filename, motor_t *motor)
{
    int fd = open(filename, O_RDONLY);
    char buffer[32];
    int count;
    int n;
    int start, stop, prep;

    if (fd == -1) {
        LOG_ERROR("load_params: open(%s) failed: %s", filename, strerror(errno));
        ERR("open");
    }

    memset(buffer, 0, sizeof(buffer));
    count = read(fd, buffer, sizeof(buffer) - 1);
    close(fd);

    if (count <= 0) {
        LOG_FATAL("load_params: empty or unreadable file %s", filename);
        exit(EXIT_FAILURE);
    }

    buffer[count] = '\0';
    n = sscanf(buffer, "%d %d %d", &start, &stop, &prep);
    if (n != 3) {
        LOG_FATAL("load_params: expected 3 integers, got %d", n);
        exit(EXIT_FAILURE);
    }

    if (start < 0 || stop < 0 || prep < 0 ||
        start > 999 || stop > 999 || prep > 999) {
        LOG_FATAL("load_params: unsupported values start=%d stop=%d prep=%d",
                  start, stop, prep);
        exit(EXIT_FAILURE);
    }
    motor_api.set_scan_params(motor, start, stop, prep);
    LOG_INFO("Params: start=%d stop=%d prep=%d", start, stop, prep);
}

void device_close(device_t *device)
{
    if (!device)
        return;

    if (device->illumination) {
        illumination_api.close(device->illumination);
        device->illumination = NULL;
    }
    if (device->motor) {
        motor_api.close(device->motor);
        device->motor = NULL;
    }

    free(device);

    LOG_INFO("Exiting");
    logger_flush();
    logger_exitFileLogger();
}

device_t *device_open(void)
{
    device_t *device = calloc(1, sizeof(*device));
    if (!device)
        return NULL;

    if (sprint1_logging_init("driver", NULL, 0) != 0) {
        free(device);
        return NULL;
    }

    LOG_INFO("Opening motor driver...");
    if (motor_api.open(&device->motor, NULL) != 0) {
        LOG_FATAL("Failed to open motor");
        device_close(device);
        return NULL;
    }

    LOG_INFO("Opening illumination on pin %d...", PIN_LASER);
    if (illumination_api.open(&device->illumination, PIN_LASER) != 0) {
        LOG_FATAL("Failed to open illumination");
        device_close(device);
        return NULL;
    }

    ms_sleep(100);

    LOG_INFO("Homing (drive toward endstop)...");
    {
        int homed = motor_api.init(device->motor);
        if (homed < 0) {
            LOG_FATAL("Homing failed");
            device_close(device);
            return NULL;
        }
        LOG_INFO("Homed position: %d (angle %.2f deg)", homed,
                 motor_api.prism_angle(device->motor));
    }

    load_params(PRM_FILE, device->motor);
    illumination_api.set_brightness(device->illumination, 255);
    illumination_api.on(device->illumination);

    {
        int prep = motor_api.get_prep(device->motor);
        motor_api.send(device->motor, prep);
        LOG_INFO("Parked at prep=%d", prep);
    }

    return device;
}

int device_run(device_t *device)
{
    if (!device || !device->motor || !device->illumination)
        return -1;

    serve_fifo(CMD_PIPE, device->motor, device->illumination);
    return 0;
}

