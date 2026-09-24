#ifndef A4988_STEPPER_H
#define A4988_STEPPER_H

/**
 * @file stepper.h
 * @brief A4988 backend private state. Tools use @c motor_api.h only.
 */

/** Completes the opaque @c motor_t from motor_api.h. */
struct motor {
    int fd;      /**< 1 when WiringPi is initialized, -1 when closed. */
    int start;   /**< Inclusive scan start step (from saved.prm). */
    int stop;    /**< Inclusive scan stop step (from saved.prm). */
    int prep;    /**< Parking / prepare step after boot. */
    int zero;    /**< Homed reference step. */
    int init;    /**< Non-zero once endstop has been seen. */
    int steps;   /**< Remaining steps for the current queued move. */
    int pos;     /**< Current step position after init. */
    int ena;     /**< Non-zero when motor enable is asserted (active LOW pin). */
    int dir;     /**< Direction for queued/manual steps (1 = forward). */
    int error;   /**< Non-zero if last goto was rejected (e.g. not initialized). */
    int lid;     /**< Cached lid-switch state (0 if PIN_LID unused). */
    int was_disabled; /**< Non-zero if the motor was disabled by a previous CMD_DISABLE. */
};

#endif
