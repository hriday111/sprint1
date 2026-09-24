#ifndef LASER_H
#define LASER_H

/**
 * @file laser.h
 * @brief Laser backend private state. Do not include from tools — use illumination_api.h.
 */

/** Completes the opaque @c illumination_t from illumination_api.h. */
struct illumination {
    int pin; /**< wiringPi pin, or -1 if unused. */
};

#endif
