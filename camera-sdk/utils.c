/**
 * @file utils.c
 * @brief Optional helpers for camera-sdk (placeholder for @c ms_sleep).
 */

#include "utils.h"

/**
 * @brief Sleep for @p milli milliseconds using nanosleep.
 */
void ms_sleep(unsigned int milli)
{
    struct timespec ts;
    ts.tv_sec = milli / 1000U;
    ts.tv_nsec = (milli % 1000U) * 1000000L;
    nanosleep(&ts, NULL);
}
