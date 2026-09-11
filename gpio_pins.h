#ifndef GPIO_PINS_H
#define GPIO_PINS_H

/**
 * @file gpio_pins.h
 * @brief BCM GPIO mapping for Raspberry Pi 4 to A4988 driver and laser PWM.
 *
 * Wiring summary:
 * - ENABLE is active LOW (motor powered when pin is LOW).
 * - Microstepping MS1/MS2/MS3 all HIGH selects 1/16 step.
 * - PIN_LASER uses hardware PWM on BCM 18 (PWM0).
 * - PIN_ENDSTOP is active HIGH when the switch is pressed.
 *
 * Change these values to match your board before running on hardware.
 */

#define PIN_STEP     27
#define PIN_DIR      22
#define PIN_ENABLE   20
#define PIN_LASER    18

/**
 * Hardware PWM duty range is 0..LASER_PWM_RANGE.
 * Approximate frequency: 19.2 MHz / LASER_PWM_CLOCK / LASER_PWM_RANGE (~2.35 kHz).
 */
#define LASER_PWM_RANGE 255
#define LASER_PWM_CLOCK 32

#define PIN_MS1      13
#define PIN_MS2      19
#define PIN_MS3      26
#define PIN_ENDSTOP  17

/** BCM pin for a lid switch, or -1 if unused. */
#define PIN_LID      (-1)

/** Half-period delay per STEP edge, in microseconds (full pulse = 2x this). */
#define STEP_DELAY_US 20000

#endif
