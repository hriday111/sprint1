/**
 * @file calibrate.cpp
 * @brief One-shot ROI recalibration after optical changes (lens move, remount).
 *
 * Day-to-day measurements do not need this: start @c build/driver, then
 * @c sweep_run. Prefer @c calibrate2 when recalibrating; if detection fails,
 * try exposures 0.20, 0.14, and 0.10 and keep the best overlay.
 *
 * Grabs a single crop, runs @c detect_boxes_cv, writes @c rois.json and
 * diagnostic images under @c tmp/. Does not stop @c build/driver.
 *
 * Usage: @c ./calibrate [exposure_ms=0.1] [n_blobs=3] [roi_path]
 */

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

extern "C" {
#include "cam_api.h"
#include "roi.h"
#include "utils.h"
#include "logger.h"
#include "crash.h"
#include "ipc_client.h"
}
#include "detect.hpp"

/**
 * @brief Request full laser brightness via the command FIFO if a reader exists.
 *
 * If the FIFO cannot be opened, prints a note and continues (laser may already
 * be on from @c build/driver).
 */
static void illum_on(void)
{
    if (fifo_open() != 0) {
        LOG_WARN("no command FIFO; assuming illumination already on");
        return;
    }
    fifo_set_brightness(255);
    fifo_illum_on();
    fifo_close();
}

/**
 * @brief Capture, detect marks, save overlay + JSON, print saturation warnings.
 * @return 0 if at least one ROI was written, else non-zero.
 */
int main(int argc, char **argv)
{
    double exposure = argc > 1 ? strtod(argv[1], NULL) : 0.1;
    int nwant = argc > 2 ? atoi(argv[2]) : 3;
    const char *roi_path = argc > 3 ? argv[3] : ROI_PATH;
    const char *overlay_path = DETECTED_OUTPUT;
    const char *frame_path = GRAB_OUTPUT;

    if (sprint1_logging_init("calibrate", NULL, 0) != 0)
        return 1;

    illum_on();

    cam_t *cam = NULL;
    if (camera_api.open(&cam) != 0)
        return 1;
    double actual = 0.0;
    if (camera_api.set_exposure_ms(cam, exposure, &actual) != 0) {
        camera_api.close(cam);
        return 1;
    }
    LOG_INFO("camera %s %dx%d exposure %.3f ms", camera_api.sensor(cam),
             camera_api.width(cam), camera_api.height(cam), actual);

    unsigned char *pix;
    int w, h, pitch;
    if (camera_api.grab(cam, &pix, &w, &h, &pitch) != 0) {
        camera_api.close(cam);
        return 1;
    }
    LOG_INFO("crop %dx%d", w, h);

    cv::Mat gray = frame_mat(pix, w, h, pitch);
    cv::imwrite(frame_path, gray);
    LOG_INFO("wrote frame %s", frame_path);

    rois_t rois;
    if (detect_boxes_cv(gray, nwant, &rois) != 0) {
        LOG_ERROR("detect_boxes_cv failed");
        camera_api.close(cam);
        return 1;
    }
    LOG_INFO("found %d mark(s)", rois.n);
    if (rois.n < nwant)
        LOG_WARN("expected %d marks, got %d — check %s", nwant, rois.n, frame_path);

    double means[ROI_MAX], sat[ROI_MAX];
    roi_means(pix, w, h, pitch, &rois, means);
    roi_sat_frac(pix, w, h, pitch, &rois, 250, sat);
    for (int i = 0; i < rois.n; i++) {
        LOG_INFO("%s blob=(cx=%.1f,cy=%.1f rx=%.1f,ry=%.1f ang=%.1f) mean=%.1f sat=%.0f%%",
                 rois.item[i].name, rois.item[i].cx, rois.item[i].cy, rois.item[i].rx,
                 rois.item[i].ry, rois.item[i].angle, means[i], sat[i] * 100.0);
        if (sat[i] > 0.05)
            LOG_WARN("%s clipped; lower exposure (now %.3f ms)", rois.item[i].name,
                     actual);
    }

    cv::Mat vis;
    cv::cvtColor(gray, vis, cv::COLOR_GRAY2BGR);
    for (int i = 0; i < rois.n; i++) {
        const roi_t *r = &rois.item[i];
        cv::ellipse(vis, cv::Point((int)std::lround(r->cx), (int)std::lround(r->cy)),
                    cv::Size(std::max(1, (int)std::lround(r->rx)),
                             std::max(1, (int)std::lround(r->ry))),
                    r->angle, 0, 360, cv::Scalar(0, 255, 0), 2);
        cv::putText(vis, r->name,
                    cv::Point((int)std::lround(r->cx - r->rx),
                              std::max(12, (int)std::lround(r->cy - r->ry) - 4)),
                    cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 255, 255), 2);
    }
    cv::imwrite(overlay_path, vis);
    LOG_INFO("wrote %s", overlay_path);

    roi_save_json(roi_path, &rois);
    LOG_INFO("wrote %s and %s", roi_path, frame_path);

    camera_api.close(cam);
    logger_flush();
    logger_exitFileLogger();
    return rois.n > 0 ? 0 : 1;
}
