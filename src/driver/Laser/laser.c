/**
 * @file laser.c
 * @brief Laser illumination backend + illumination_api vtable.
 */

#include "laser.h"
#include "illumination_api.h"

#include <stdlib.h>
#include <wiringPi.h>


static int laser_open(illumination_t **out, int pin)
{
    illumination_t *illum;

    if (!out)
        return -1;

    illum = calloc(1, sizeof(*illum));
    if (!illum)
        return -1;

    illum->pin = pin;
    if (pin >= 0) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, LOW);
    }

    *out = illum;
    return 0;
}

static void laser_close(illumination_t *illum)
{
    if (!illum)
        return;
    if (illum->pin >= 0)
        digitalWrite(illum->pin, LOW);
    free(illum);
}

static void laser_on(illumination_t *illum)
{
    if (illum && illum->pin >= 0)
        digitalWrite(illum->pin, HIGH);
}

static void laser_off(illumination_t *illum)
{
    if (illum && illum->pin >= 0)
        digitalWrite(illum->pin, LOW);
}

static void laser_set_brightness(illumination_t *illum, int brightness)
{
    (void)illum;
    (void)brightness;
    /* Digital laser: on/off only. Keep stub for API compatibility with LED. */
}


const illumination_api_t illumination_api = {
    .open = laser_open,
    .close = laser_close,
    .on = laser_on,
    .off = laser_off,
    .set_brightness = laser_set_brightness,
};
