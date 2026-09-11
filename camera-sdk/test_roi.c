/**
 * @file test_roi.c
 * @brief Offline sanity check: load rois.json and average a synthetic flat frame.
 *
 * No camera required. Expects means near 120 on a uniform image filled with 120.
 *
 * Usage: @c ./test_roi [rois.json]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "roi.h"
#include "logger.h"
#include "crash.h"

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : ROI_PATH;

    if (sprint1_logging_init("test_roi", NULL, 0) != 0)
        return 1;

    rois_t rois;
    if (roi_load_json(path, &rois) != 0)
        return 1;
    for (int i = 0; i < rois.n; i++)
        LOG_INFO("  %s (%d,%d %dx%d)", rois.item[i].name, rois.item[i].x,
                 rois.item[i].y, rois.item[i].w, rois.item[i].h);

    int w = 1280, h = 1024, pitch = 1280;
    roi_clip(&rois, w, h);

    unsigned char *img = malloc((size_t)pitch * (size_t)h);
    if (!img) {
        LOG_FATAL("out of memory");
        return 1;
    }
    memset(img, 120, (size_t)pitch * (size_t)h);

    double means[ROI_MAX];
    roi_means(img, w, h, pitch, &rois, means);
    int ok = 1;
    for (int i = 0; i < rois.n; i++) {
        LOG_INFO("  %s mean=%.2f pixels=%d", rois.item[i].name, means[i],
                 rois.item[i].pixels);
        if (rois.item[i].pixels < 20 || means[i] < 119.5 || means[i] > 120.5)
            ok = 0;
    }
    free(img);
    if (!ok) {
        LOG_ERROR("box/mean sanity check failed");
        return 1;
    }
    LOG_INFO("ok");
    logger_flush();
    logger_exitFileLogger();
    return 0;
}
