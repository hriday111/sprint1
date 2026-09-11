/**
 * @file grab_check.c
 * @brief Minimal camera smoke test: open, set exposure, grab one crop, print stats.
 *
 * Not built by the default Makefile; compile manually if needed:
 * @code
 *   gcc -std=c17 -D_GNU_SOURCE -I. -I.. -o grab_check grab_check.c cam.c \\
 *       ../logger.c ../loggerconf.c ../crash.c -lueye_api -lpthread
 * @endcode
 */

#include "cam.h"
#include "logger.h"
#include "crash.h"
#include <stdio.h>

int main(void)
{
    if (sprint1_logging_init("grab_check", NULL, 0) != 0)
        return 1;

    cam_t cam;
    if (cam_open(&cam))
        return 1;
    double a = 0;
    cam_set_exposure_ms(&cam, 0.05, &a);
    LOG_INFO("sensor %s %dx%d pitch=%d exp=%.3f", cam.sensor, cam.width, cam.height,
             (int)cam.pitch, a);
    unsigned char *p;
    int w, h, pitch;
    if (cam_grab(&cam, &p, &w, &h, &pitch))
        return 1;
    LOG_INFO("grab %dx%d pitch=%d", w, h, pitch);
    unsigned long sum = 0;
    int n = 0, mn = 255, mx = 0;
    for (int y = 0; y < h; y++) {
        unsigned char *row = p + y * pitch;
        for (int x = 0; x < w; x++) {
            int v = row[x];
            sum += v;
            n++;
            if (v < mn)
                mn = v;
            if (v > mx)
                mx = v;
        }
    }
    LOG_INFO("mean=%.1f min=%d max=%d", (double)sum / n, mn, mx);
    cam_close(&cam);
    logger_flush();
    logger_exitFileLogger();
    return 0;
}
