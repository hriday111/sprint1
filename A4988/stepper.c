/**
 * @file stepper.c
 * @brief A4988 motor backend: GPIO thread, command dispatch + motor_api.
 */

#include "stepper.h"
#include "motor_api.h"
#include "gpio_pins.h"
#include "utils.h"
#include "commands.h"
#include "logger.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wiringPi.h>

static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t g_thread;
static volatile int g_thread_run;
static motor_t *g_motor;

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
static void update_lid(motor_t *stepper)
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
static void check_endstop(motor_t *stepper)
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
static void account_step(motor_t *stepper, int dir)
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
static void pulse_step(motor_t *stepper, int dir)
{
    pulse_gpio(dir);
    account_step(stepper, dir);
}

/** @brief Deassert enable (active HIGH on pin), clear motion state. */
static void disable_motor(motor_t *stepper)
{
    gpio_write(PIN_ENABLE, HIGH);
    stepper->init = 0;
    stepper->ena = 0;
    stepper->error = 0;
    stepper->steps = 0;
}

/** @brief Assert enable (pin LOW). Does not home; caller must init separately. */
static void enable_motor(motor_t *stepper)
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
static void queue_goto(motor_t *stepper, int pos)
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
static void *motor_thread(void *arg)
{
    (void)arg;

    while (g_thread_run) {
        int do_step = 0;
        int dir = 0;

        pthread_mutex_lock(&g_mutex);
        if (g_motor != NULL && g_motor->ena && g_motor->steps > 0) {
            do_step = 1;
            dir = g_motor->dir;
        }
        pthread_mutex_unlock(&g_mutex);

        if (do_step) {
            pulse_gpio(dir);

            pthread_mutex_lock(&g_mutex);
            if (g_motor != NULL) {
                account_step(g_motor, dir);
                if (g_motor->steps > 0) {
                    g_motor->steps--;
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

static int a4988_open_session(const char *portName, motor_t *m)
{
    (void)portName;

    if (g_motor != NULL) {
        LOG_ERROR("a4988_open: already open");
        return -1;
    }

    if (wiringPiSetupPinType(WPI_PIN_BCM) < 0) {
        LOG_ERROR("wiringPiSetupPinType failed (run with sudo)");
        return -1;
    }
    LOG_INFO("WiringPi BCM pin mode ready");

    claim_output(PIN_STEP, LOW);
    claim_output(PIN_DIR, LOW);
    claim_output(PIN_ENABLE, HIGH);
    claim_output(PIN_MS1, LOW);
    claim_output(PIN_MS2, LOW);
    claim_output(PIN_MS3, LOW);
    claim_input_pullup(PIN_ENDSTOP);
    if (PIN_LID >= 0) {
        claim_input_pullup(PIN_LID);
    }

    set_microstep_16();

    m->fd = 1;
    m->zero = CMD_POS_ZERO;
    g_motor = m;

    g_thread_run = 1;
    if (pthread_create(&g_thread, NULL, motor_thread, NULL) != 0) {
        ERR("pthread_create");
    }

    pthread_mutex_lock(&g_mutex);
    disable_motor(m);
    update_lid(m);
    pthread_mutex_unlock(&g_mutex);

    LOG_INFO("Stepper opened (motor thread running)");
    return 0;
}

static void a4988_close_session(motor_t *m)
{
    if (g_motor == NULL) {
        LOG_DEBUG("a4988_close: nothing open");
        return;
    }

    LOG_INFO("Closing stepper");
    g_thread_run = 0;
    pthread_join(g_thread, NULL);

    pthread_mutex_lock(&g_mutex);
    disable_motor(m);
    pthread_mutex_unlock(&g_mutex);
    pthread_mutex_destroy(&g_mutex);
    m->fd = -1;
    g_motor = NULL;
    LOG_INFO("Stepper closed");
}

static int a4988_send(motor_t *m, int cmd)
{
    pthread_mutex_lock(&g_mutex);

    if (cmd >= CMD_ANGLE_MIN && cmd <= CMD_ANGLE_MAX) {
        queue_goto(m, cmd);
    } else if (cmd == CMD_DISABLE) {
        m->was_disabled = 1;
        disable_motor(m);
    } else if (cmd == CMD_ENABLE) {
        if (m->was_disabled == 1) {
            m->init = 1;
        }
        enable_motor(m);
    } else if (cmd == CMD_RESET) {
        /* Unreachable while CMD_RESET == CMD_DISABLE (both 1003). */
        m->steps = 0;
        m->error = 0;
    } else if (cmd == CMD_NEXT) {
        if (m->ena) {
            pulse_step(m, 1);
            m->dir = 1;
        }
    } else if (cmd == CMD_PREV) {
        if (m->ena) {
            pulse_step(m, 0);
            m->dir = 0;
        }
    } else if (cmd == CMD_UPDATE_LID) {
        update_lid(m);
    } else {
        LOG_WARN("a4988_send: unknown command %d", cmd);
    }

    pthread_mutex_unlock(&g_mutex);
    return 1;
}

static int a4988_read(motor_t *m)
{
    int result;

    pthread_mutex_lock(&g_mutex);
    update_lid(m);

    if (m->init) {
        result = m->pos;
    } else {
        result = -1;
    }

    pthread_mutex_unlock(&g_mutex);
    return result;
}

static int a4988_init(motor_t *m)
{
    int pos = -1;
    int attempts = 0;

    LOG_INFO("Homing: enable motor and seek endstop");
    a4988_send(m, CMD_ENABLE);
    ms_sleep(100);

    while (pos < 0 && attempts < 2000) {
        a4988_send(m, CMD_NEXT);
        pos = a4988_read(m);
        attempts++;
        if ((attempts % 50) == 0) {
            LOG_DEBUG("Homing... steps=%d endstop=%d", attempts,
                      digitalRead(PIN_ENDSTOP));
        }
    }
    a4988_send(m, CMD_NEXT);
    if (pos < 0) {
        LOG_ERROR("Homing failed after %d steps (endstop never HIGH on GPIO %d)",
                  attempts, PIN_ENDSTOP);
        return -1;
    }

    pthread_mutex_lock(&g_mutex);
    m->zero = pos;
    pthread_mutex_unlock(&g_mutex);

    LOG_INFO("Endstop hit after %d steps, position=%d", attempts, pos);
    return pos;
}

static void a4988_go_to(motor_t *m, int pos)
{
    a4988_send(m, pos);
}

static void a4988_go_rel(motor_t *m, int rel)
{
    int pos = a4988_read(m);
    if (pos < 0)
        return;
    a4988_send(m, pos + rel);
}

static void a4988_go_start(motor_t *m)
{
    a4988_send(m, m->start);
}

static void a4988_go_stop(motor_t *m)
{
    a4988_send(m, m->stop);
}

static void a4988_go_zero(motor_t *m)
{
    a4988_send(m, m->zero);
}

static void a4988_go_prepare(motor_t *m)
{
    a4988_send(m, m->prep);
}

static int a4988_get_percent(motor_t *m)
{
    int pos = a4988_read(m);
    int span;

    if (pos < 0)
        return 0;
    span = m->stop - m->start;
    if (span == 0)
        return 0;
    return (int)((100.0 * (pos - m->start)) / span);
}

static int a4988_next(motor_t *m)
{
    int pos = a4988_read(m);
    if (pos < 0 || pos >= m->stop)
        return -1;
    a4988_send(m, CMD_NEXT);
    return 1;
}

static int a4988_prev(motor_t *m)
{
    int pos = a4988_read(m);
    if (pos < 0 || pos <= m->start)
        return -1;
    a4988_send(m, CMD_PREV);
    return 1;
}

static int a4988_get_pos(motor_t *m)
{
    return a4988_read(m);
}

static double a4988_prism_angle(motor_t *m)
{
    int pos = a4988_read(m);
    if (pos < 0)
        return 0.0;
    return 0.1125 * pos;
}

static void a4988_set_scan_params(motor_t *m, int start, int stop, int prep)
{
    m->start = start;
    m->stop = stop;
    m->prep = prep;
}

static int a4988_get_start(const motor_t *m)
{
    return m->start;
}

static int a4988_get_stop(const motor_t *m)
{
    return m->stop;
}

static int a4988_get_prep(const motor_t *m)
{
    return m->prep;
}

static int a4988_open(motor_t **out, const char *portName)
{
    motor_t *m;

    if (!out)
        return -1;

    m = calloc(1, sizeof(*m));
    if (!m)
        return -1;

    m->fd = -1;
    m->zero = 400;

    if (a4988_open_session(portName, m) != 0) {
        free(m);
        return -1;
    }

    *out = m;
    return 0;
}

static void a4988_close(motor_t *m)
{
    if (!m)
        return;
    a4988_close_session(m);
    free(m);
}

const motor_api_t motor_api = {
    .open = a4988_open,
    .close = a4988_close,
    .send = a4988_send,
    .read = a4988_read,
    .init = a4988_init,
    .go_to = a4988_go_to,
    .go_rel = a4988_go_rel,
    .go_start = a4988_go_start,
    .go_stop = a4988_go_stop,
    .go_zero = a4988_go_zero,
    .go_prepare = a4988_go_prepare,
    .get_percent = a4988_get_percent,
    .next = a4988_next,
    .prev = a4988_prev,
    .get_pos = a4988_get_pos,
    .prism_angle = a4988_prism_angle,
    .set_scan_params = a4988_set_scan_params,
    .get_start = a4988_get_start,
    .get_stop = a4988_get_stop,
    .get_prep = a4988_get_prep,
};
