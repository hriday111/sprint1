#include "utils.h"

/**
 * @brief Sleep for @p milli milliseconds.
 */
void ms_sleep(unsigned int milli) {
    struct timespec ts;
    ts.tv_sec = milli / 1000U;
    ts.tv_nsec = (milli % 1000U) * 1000000L;
    nanosleep(&ts, NULL);
}
