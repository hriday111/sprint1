#ifndef DEVICE_H
#define DEVICE_H

/**
 * @file device.h
 * @brief Device façade: motor + illumination lifecycle for the daemon binary.
 */

typedef struct device device_t;

/**
 * @brief Init logging, open motor + illumination, home, load params, park.
 * @return Opaque device, or NULL on failure (resources cleaned up).
 */
device_t *device_open(void);

/**
 * @brief Serve the command FIFO until the client requests exit.
 * @return 0 on clean quit, -1 if boot/serve setup failed.
 */
int device_run(device_t *device);

/**
 * @brief Close illumination + motor, free @p device, flush logging.
 * Safe with NULL or a partially opened device.
 */
void device_close(device_t *device);

#endif
