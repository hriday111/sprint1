/**
 * @file stepper.c
 * @brief GPIO motor thread, laser PWM, and command dispatch for the A4988.
 */

#include "stepper.h"
#include "gpio_pins.h"
#include "utils.h"
#include "commands.h"
#include "logger.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <wiringPi.h>

static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t g_thread;
static volatile int g_thread_run;
static stepper_t *g_stepper;

/**
 * @brief Write @p level to @p pin; no-op if @p pin is negative.
 */
static void gpio_write(int pin, int level)
{
    if (pin < 0) {
        return;
    }
    digitalWrite(pin, level);
}

/**
 * @brief Clamp brightness and write hardware PWM (caller holds g_mutex or is in open).
 * @param stepper Driver state.
 * @param brightness Desired duty cycle.
 */
static void laser_set_unlocked(stepper_t *stepper, int brightness)
{
    if (brightness < 0) {
        brightness = 0;
    }
    if (brightness > LASER_PWM_RANGE) {
        brightness = LASER_PWM_RANGE;
    }
    pwmWrite(PIN_LASER, brightness);
    stepper->laser = brightness;
}

/**
 * @brief Configure mark-space PWM on PIN_LASER with duty starting at 0.
 */
static void claim_laser_pwm(void)
{
    pwmWrite(PIN_LASER, 0);
    pwmSetMode(PWM_MODE_MS);
    pwmSetRange(LASER_PWM_RANGE);
    pwmSetClock(LASER_PWM_CLOCK);
    pinMode(PIN_LASER, PWM_MS_OUTPUT);
    pwmWrite(PIN_LASER, 0);
}

/**
 * @brief Read @p pin, or 0 if the pin is unused (negative).
 */
static int gpio_read(int pin)
{
    if (pin < 0) {
        return 0;
    }
    return digitalRead(pin);
}

/** @brief Sleep for one half-period of the STEP pulse. */
static void step_delay(void)
{
    delayMicroseconds(STEP_DELAY_US);
}

/** @brief Force MS1/MS2/MS3 HIGH for 1/16 microstepping. */
static void set_microstep_16(void)
{
    gpio_write(PIN_MS1, HIGH);
    gpio_write(PIN_MS2, HIGH);
    gpio_write(PIN_MS3, HIGH);
}

/** @brief Refresh @c stepper->lid from PIN_LID (or clear if unused). */
static void update_lid(stepper_t *stepper)
{
    if (PIN_LID >= 0) {
        stepper->lid = gpio_read(PIN_LID) != 0;
    } else {
        stepper->lid = 0;
    }
}

/**
 * @brief On first endstop HIGH before init, mark homed and set position.
 *
 * Position becomes CMD_POS_ZERO + DELTA when the switch trips during motion.
 */
static void check_endstop(stepper_t *stepper)
{
    if (!stepper->init && gpio_read(PIN_ENDSTOP) != 0) {
        stepper->init = 1;
        stepper->pos = CMD_POS_ZERO+DELTA;
    }
}

/**
 * @brief Emit one STEP pulse on the GPIO pins (no shared-state updates).
 *
 * Safe to call with @c g_mutex released; most of the time is spent in delays.
 * @param dir Direction pin level (nonzero = forward).
 */
static void pulse_gpio(int dir)
{
    gpio_write(PIN_DIR, dir);
    gpio_write(PIN_STEP, HIGH);
    step_delay();
    gpio_write(PIN_STEP, LOW);
    step_delay();
}

/**
 * @brief Update position / endstop bookkeeping after a completed pulse.
 * @param stepper Driver state (caller holds g_mutex).
 * @param dir Direction used for the pulse.
 */
static void account_step(stepper_t *stepper, int dir)
{
    if (stepper->init) {
        if (dir) {
            stepper->pos++;
        } else {
            stepper->pos--;
        }
    }

    check_endstop(stepper);
    stepper->error = 0;
}

/**
 * @brief Pulse GPIO and account the step under the caller's lock.
 */
static void pulse_step(stepper_t *stepper, int dir)
{
    pulse_gpio(dir);
    account_step(stepper, dir);
}

/** @brief Deassert enable (active HIGH on pin), clear motion state. */
static void disable_motor(stepper_t *stepper)
{
    gpio_write(PIN_ENABLE, HIGH);
    stepper->init = 0;
    stepper->ena = 0;
    stepper->error = 0;
    stepper->steps = 0;
}

/** @brief Assert enable (pin LOW). Does not home; caller must init separately. */
static void enable_motor(stepper_t *stepper)
{
    gpio_write(PIN_ENABLE, LOW);
    stepper->error = 0;
    stepper->ena = 1;
}

/**
 * @brief Queue an absolute move; sets @c error if not yet initialized.
 * @param stepper Driver state (caller holds g_mutex).
 * @param pos Target step position.
 */
static void queue_goto(stepper_t *stepper, int pos)
{
    int diff;

    if (!stepper->init) {
        stepper->error = 1;
        return;
    }

    diff = pos - stepper->pos;
    if (diff > 0) {
        stepper->dir = 1;
        stepper->steps = diff;
    } else if (diff < 0) {
        stepper->dir = 0;
        stepper->steps = -diff;
    } else {
        stepper->steps = 0;
    }
    stepper->error = 0;
}

/**
 * @brief Background worker: execute queued steps without holding the mutex during delays.
 * @param arg Unused.
 * @return Always NULL.
 */
static void *stepper_thread(void *arg)
{
    (void)arg;

    while (g_thread_run) {
        int do_step = 0;
        int dir = 0;

        pthread_mutex_lock(&g_mutex);
        if (g_stepper != NULL && g_stepper->ena && g_stepper->steps > 0) {
            do_step = 1;
            dir = g_stepper->dir;
        }
        pthread_mutex_unlock(&g_mutex);

        if (do_step) {
            pulse_gpio(dir);

            pthread_mutex_lock(&g_mutex);
            if (g_stepper != NULL) {
                account_step(g_stepper, dir);
                if (g_stepper->steps > 0) {
                    g_stepper->steps--;
                }
            }
            pthread_mutex_unlock(&g_mutex);
        } else {
            usleep(1000);
        }
    }

    return NULL;
}

/** @brief Configure @p pin as OUTPUT with @p initial level. */
static void claim_output(int pin, int initial)
{
    pinMode(pin, OUTPUT);
    digitalWrite(pin, initial);
}

/** @brief Configure @p pin as INPUT with pull-up. */
static void claim_input_pullup(int pin)
{
    pinMode(pin, INPUT);
    pullUpDnControl(pin, PUD_UP);
}

int stepperOpen(const char *portName, stepper_t *stepper)
{
    (void)portName;

    if (g_stepper != NULL) {
        LOG_ERROR("stepperOpen: already open");
        return 0;
    }

    /* Direct register access is required for hardware PWM on GPIO 18. */
    if (wiringPiSetupPinType(WPI_PIN_BCM) < 0) {
        LOG_ERROR("wiringPiSetupPinType failed (run with sudo for hardware PWM)");
        return 0;
    }
    LOG_INFO("WiringPi BCM pin mode ready");

    claim_output(PIN_STEP, LOW);
    claim_output(PIN_DIR, LOW);
    claim_output(PIN_ENABLE, HIGH);
    claim_laser_pwm();
    claim_output(PIN_MS1, LOW);
    claim_output(PIN_MS2, LOW);
    claim_output(PIN_MS3, LOW);
    claim_input_pullup(PIN_ENDSTOP);
    if (PIN_LID >= 0) {
        claim_input_pullup(PIN_LID);
    }

    set_microstep_16();
    laser_set_unlocked(stepper, 0);

    stepper->fd = 1;
    stepper->zero = CMD_POS_ZERO;
    g_stepper = stepper;

    g_thread_run = 1;
    if (pthread_create(&g_thread, NULL, stepper_thread, NULL) != 0) {
        ERR("pthread_create");
    }

    pthread_mutex_lock(&g_mutex);
    disable_motor(stepper);
    update_lid(stepper);
    pthread_mutex_unlock(&g_mutex);

    LOG_INFO("Stepper opened (motor thread running)");
    return 1;
}

void stepperClose(stepper_t *stepper)
{
    if (g_stepper == NULL) {
        LOG_DEBUG("stepperClose: nothing open");
        return;
    }

    LOG_INFO("Closing stepper");
    g_thread_run = 0;
    pthread_join(g_thread, NULL);

    pthread_mutex_lock(&g_mutex);
    disable_motor(stepper);
    laser_set_unlocked(stepper, 0);
    pinMode(PIN_LASER, PM_OFF);
    pthread_mutex_unlock(&g_mutex);
    pthread_mutex_destroy(&g_mutex);
    stepper->fd = -1;
    g_stepper = NULL;
    LOG_INFO("Stepper closed");
}

int stepperSend(int cmd, stepper_t *stepper)
{
    pthread_mutex_lock(&g_mutex);

    if (cmd >= CMD_ANGLE_MIN && cmd <= CMD_ANGLE_MAX) {
        queue_goto(stepper, cmd);
    } else if (cmd == CMD_LASER_OFF) {
        laser_set_unlocked(stepper, 0);
    } else if (cmd == CMD_LASER_ON) {
        laser_set_unlocked(stepper, LASER_PWM_RANGE);
    } else if (cmd == CMD_DISABLE) {
        disable_motor(stepper);
    } else if (cmd == CMD_ENABLE) {
        enable_motor(stepper);
    } else if (cmd == CMD_RESET) {
        /* Unreachable while CMD_RESET == CMD_DISABLE (both 1003). */
        stepper->steps = 0;
        stepper->error = 0;
    } else if (cmd == CMD_NEXT) {
        if (stepper->ena) {
            pulse_step(stepper, 1);
            stepper->dir = 1;
        }
    } else if (cmd == CMD_PREV) {
        if (stepper->ena) {
            pulse_step(stepper, 0);
            stepper->dir = 0;
        }
    } else if (cmd == CMD_UPDATE_LID) {
        update_lid(stepper);
    } else {
        LOG_WARN("stepperSend: unknown command %d", cmd);
    }

    pthread_mutex_unlock(&g_mutex);
    return 1;
}

int stepperRead(stepper_t *stepper)
{
    int result;

    pthread_mutex_lock(&g_mutex);
    update_lid(stepper);

    if (stepper->init) {
        result = stepper->pos;
    } else {
        result = -1;
    }

    pthread_mutex_unlock(&g_mutex);
    return result;
}

void stepper_goTo(int pos, stepper_t *stepper)
{
    stepperSend(pos, stepper);
}

void stepper_goRel(int rel, stepper_t *stepper)
{
    int pos = stepperRead(stepper);
    if (pos >= 0) {
        stepperSend(pos + rel, stepper);
    }
}

void stepper_goStart(stepper_t *stepper)
{
    stepperSend(stepper->start, stepper);
}

void stepper_goStop(stepper_t *stepper)
{
    stepperSend(stepper->stop, stepper);
}

void stepper_goZero(stepper_t *stepper)
{
    stepperSend(stepper->zero, stepper);
}

void stepper_goPrepare(stepper_t *stepper)
{
    stepperSend(stepper->prep, stepper);
}

int stepper_getPercent(stepper_t *stepper)
{
    int percent;

    pthread_mutex_lock(&g_mutex);
    if (stepper->stop == stepper->start) {
        percent = 0;
    } else {
        percent = (stepper->pos - stepper->start) * 100 /
                  (stepper->stop - stepper->start);
    }
    pthread_mutex_unlock(&g_mutex);
    return percent;
}

int stepper_next(stepper_t *stepper)
{
    int pos = stepperRead(stepper);
    if (pos < 0 || pos >= stepper->stop) {
        return -1;
    }
    stepperSend(CMD_NEXT, stepper);
    return 1;
}

int stepper_prev(stepper_t *stepper)
{
    int pos = stepperRead(stepper);
    if (pos < 0 || pos <= stepper->start) {
        return -1;
    }
    stepperSend(CMD_PREV, stepper);
    return 1;
}

int stepperInit(stepper_t *stepper)
{
    int pos = -1;
    int attempts = 0;

    LOG_INFO("Homing: enable motor and seek endstop");
    stepperSend(CMD_ENABLE, stepper);
    ms_sleep(100);

    while (pos < 0 && attempts < 2000) {
        stepperSend(CMD_NEXT, stepper);
        pos = stepperRead(stepper);
        attempts++;
        if ((attempts % 50) == 0) {
            LOG_DEBUG("Homing... steps=%d endstop=%d", attempts,
                      digitalRead(PIN_ENDSTOP));
        }
    }
    stepperSend(CMD_NEXT, stepper);
    if (pos < 0) {
        LOG_ERROR("Homing failed after %d steps (endstop never HIGH on GPIO %d)",
                  attempts, PIN_ENDSTOP);
        return -1;
    }

    pthread_mutex_lock(&g_mutex);
    stepper->zero = pos;
    pthread_mutex_unlock(&g_mutex);

    LOG_INFO("Endstop hit after %d steps, position=%d", attempts, pos);
    return pos;
}

void stepper_laserOn(stepper_t *stepper)
{
    stepper_setBrightness(stepper, LASER_PWM_RANGE);
}

void stepper_laserOff(stepper_t *stepper)
{
    stepper_setBrightness(stepper, 0);
}

void stepper_setBrightness(stepper_t *stepper, int brightness)
{
    LOG_DEBUG("Laser brightness -> %d", brightness);
    pthread_mutex_lock(&g_mutex);
    laser_set_unlocked(stepper, brightness);
    pthread_mutex_unlock(&g_mutex);
}

int stepper_getPos(stepper_t *stepper)
{
    return stepperRead(stepper);
}

double stepper_prismAngle(stepper_t *stepper)
{
    int pos = stepperRead(stepper);
    if (pos < 0) {
        return 0.0;
    }
    return 0.1125 * pos;
}
