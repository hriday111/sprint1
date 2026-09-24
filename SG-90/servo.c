/**
 * @file servo.c
 * @brief SG-90 servo backend stub + motor_api vtable (blank implementations).
 */

#include "servo.h"
#include "motor_api.h"

#include <stdlib.h>

static int sg90_open(motor_t **out, const char *portName)
{
    motor_t *m;

    (void)portName;
    if (!out)
        return -1;

    m = calloc(1, sizeof(*m));
    if (!m)
        return -1;

    *out = m;
    return 0;
}

static void sg90_close(motor_t *m)
{
    free(m);
}

static int sg90_send(motor_t *m, int cmd)
{
    (void)m;
    (void)cmd;
    return 1;
}

static int sg90_read(motor_t *m)
{
    (void)m;
    return -1;
}

static int sg90_init(motor_t *m)
{
    (void)m;
    return -1;
}

static void sg90_go_to(motor_t *m, int pos)
{
    (void)m;
    (void)pos;
}

static void sg90_go_rel(motor_t *m, int rel)
{
    (void)m;
    (void)rel;
}

static void sg90_go_start(motor_t *m)
{
    (void)m;
}

static void sg90_go_stop(motor_t *m)
{
    (void)m;
}

static void sg90_go_zero(motor_t *m)
{
    (void)m;
}

static void sg90_go_prepare(motor_t *m)
{
    (void)m;
}

static int sg90_get_percent(motor_t *m)
{
    (void)m;
    return 0;
}

static int sg90_next(motor_t *m)
{
    (void)m;
    return -1;
}

static int sg90_prev(motor_t *m)
{
    (void)m;
    return -1;
}

static int sg90_get_pos(motor_t *m)
{
    (void)m;
    return -1;
}

static double sg90_prism_angle(motor_t *m)
{
    (void)m;
    return 0.0;
}

static void sg90_set_scan_params(motor_t *m, int start, int stop, int prep)
{
    (void)m;
    (void)start;
    (void)stop;
    (void)prep;
}

static int sg90_get_start(const motor_t *m)
{
    (void)m;
    return 0;
}

static int sg90_get_stop(const motor_t *m)
{
    (void)m;
    return 0;
}

static int sg90_get_prep(const motor_t *m)
{
    (void)m;
    return 0;
}

const motor_api_t motor_api = {
    .open = sg90_open,
    .close = sg90_close,
    .send = sg90_send,
    .read = sg90_read,
    .init = sg90_init,
    .go_to = sg90_go_to,
    .go_rel = sg90_go_rel,
    .go_start = sg90_go_start,
    .go_stop = sg90_go_stop,
    .go_zero = sg90_go_zero,
    .go_prepare = sg90_go_prepare,
    .get_percent = sg90_get_percent,
    .next = sg90_next,
    .prev = sg90_prev,
    .get_pos = sg90_get_pos,
    .prism_angle = sg90_prism_angle,
    .set_scan_params = sg90_set_scan_params,
    .get_start = sg90_get_start,
    .get_stop = sg90_get_stop,
    .get_prep = sg90_get_prep,
};
