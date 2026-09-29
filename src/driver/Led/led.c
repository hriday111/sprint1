#include "led.h"
#include "gpio_pins.h"
#include "illumination_api.h"

#include <stdlib.h>
#include <wiringPi.h>

static int led_open(illumination_t **out, int pin)
{
    illumination_t *illum;

    if (!out)
        return -1;

    illum = calloc(1, sizeof(*illum));
    if (!illum)
        return -1;

    illum->pin = pin;
    if (pin >= 0) {
        pinMode(pin, PWM_OUTPUT);
        pwmWrite(pin, 0);
    }

    *out = illum;
    return 0;
}

static void led_close(illumination_t *illum)
{
    if (!illum)
        return;
    if (illum->pin >= 0)
        pwmWrite(illum->pin, 0);
    free(illum);
}

static void led_on(illumination_t *illum)
{
    if (illum && illum->pin >= 0)
        pwmWrite(illum->pin, LED_PWM_RANGE);
}
static void led_off(illumination_t *illum)
{
    if (illum && illum->pin >= 0)
        pwmWrite(illum->pin, 0);
}
static void led_set_brightness(illumination_t *illum, int brightness)
{
    if (!illum || illum->pin < 0)
        return;
    if (brightness < 0)
        brightness = 0;
    if (brightness > LED_PWM_RANGE)
        brightness = LED_PWM_RANGE;
    pwmWrite(illum->pin, brightness);
}
const illumination_api_t illumination_api = {
    .open = led_open,
    .close = led_close,
    .on = led_on,
    .off = led_off,
    .set_brightness = led_set_brightness,
};