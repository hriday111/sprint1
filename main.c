/**
 * @file main.c
 * @brief Daemon entry: device façade owns hardware lifecycle.
 *
 * Start this first, then run camera-sdk tools (sweep_run / calibrate*).
 */

#include <stdlib.h>

#include "device.h"
#include "logger.h"

int main(int argc, char **argv)
{
    device_t *device;
    int rc;

    (void)argv;

    device = device_open();
    if (!device)
        return EXIT_FAILURE;

    if (argc != 1)
        LOG_WARN("Invalid usage. Additional arguments are not supported. Being ignored.");

    rc = device_run(device);
    device_close(device);
    return rc == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
