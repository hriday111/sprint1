#ifndef STEPPER_H
#define STEPPER_H

/**
 * @file stepper.h
 * @brief A4988 stepper + laser PWM driver used by @c servo_calib.
 */

/**
 * @brief Runtime state for the GPIO stepper and laser.
 *
 * Position is only valid after successful homing (@c init != 0).
 * Absolute moves are queued as pending @c steps executed by a background thread.
 */
typedef struct {
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
    int laser;   /**< Current PWM duty 0..LASER_PWM_RANGE. */
    int error;   /**< Non-zero if last goto was rejected (e.g. not initialized). */
    int lid;     /**< Cached lid-switch state (0 if PIN_LID unused). */
} stepper_t;

/**
 * @brief Dispatch a protocol command or absolute goto.
 * @param cmd Integer command (0..1000 goto, or CMD_*).
 * @param stepper Driver state.
 * @return Always 1 (legacy API).
 */
int stepperSend(int cmd, stepper_t *stepper);

/**
 * @brief Read current position, or -1 if not yet homed.
 * @param stepper Driver state.
 * @return Step position, or -1.
 */
int stepperRead(stepper_t *stepper);

/**
 * @brief Claim GPIO, configure PWM/microstepping, start the motor thread.
 * @param portName Unused (kept for API compatibility); pass NULL.
 * @param stepper Driver state to bind (must outlive the open session).
 * @return 1 on success, 0 on failure.
 */
int stepperOpen(const char *portName, stepper_t *stepper);

/**
 * @brief Stop the motor thread, disable the driver, release laser PWM.
 * @param stepper Driver state previously passed to stepperOpen.
 */
void stepperClose(stepper_t *stepper);

/**
 * @brief Home by stepping forward until the endstop goes HIGH.
 * @param stepper Driver state (must already be open).
 * @return Homed position on success, -1 on failure.
 */
int stepperInit(stepper_t *stepper);

/** @brief Queue an absolute move to @p pos. */
void stepper_goTo(int pos, stepper_t *stepper);

/** @brief Queue a relative move by @p rel steps from the current position. */
void stepper_goRel(int rel, stepper_t *stepper);

/** @brief Move to @c stepper->start. */
void stepper_goStart(stepper_t *stepper);

/** @brief Move to @c stepper->stop. */
void stepper_goStop(stepper_t *stepper);

/** @brief Move to @c stepper->zero. */
void stepper_goZero(stepper_t *stepper);

/** @brief Move to @c stepper->prep. */
void stepper_goPrepare(stepper_t *stepper);

/**
 * @brief Progress through the scan window as a percentage.
 * @return 0..100 (or 0 if start == stop).
 */
int stepper_getPercent(stepper_t *stepper);

/**
 * @brief Single step forward if below @c stop.
 * @return 1 on success, -1 if at/ past stop or not homed.
 */
int stepper_next(stepper_t *stepper);

/**
 * @brief Single step backward if above @c start.
 * @return 1 on success, -1 if at/ past start or not homed.
 */
int stepper_prev(stepper_t *stepper);

/** @brief Set laser PWM to full scale. */
void stepper_laserOn(stepper_t *stepper);

/** @brief Set laser PWM to 0. */
void stepper_laserOff(stepper_t *stepper);

/**
 * @brief Clamp and apply laser PWM duty.
 * @param stepper Driver state.
 * @param brightness Requested duty 0..LASER_PWM_RANGE.
 */
void stepper_setBrightness(stepper_t *stepper, int brightness);

/** @brief Alias for stepperRead. */
int stepper_getPos(stepper_t *stepper);

/**
 * @brief Convert current step position to prism angle in degrees.
 * @return 0.1125 * pos, or 0.0 if not homed.
 */
double stepper_prismAngle(stepper_t *stepper);

#endif
