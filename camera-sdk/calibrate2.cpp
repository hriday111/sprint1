/**
 * @file calibrate2.cpp
 * @brief Two-point blob calibration + linear ROI interpolation check.
 *
 * Moves the servo to @c pos0 (default 400) and @c pos1 (default 420), detects
 * the three disc blobs at each end, and writes an interpolation model. Then
 * steps through every position in between, evaluates the linear formula for
 * blob centres, measures intensity, and writes a CSV for plotting.
 *
 * Usage:
 *   ./calibrate2 [exposure_ms=0.1] [pos0=400] [pos1=420] [n_blobs=3]
 *                [interp_path] [csv_path]
 *
 * If detection fails or ellipses miss the discs, retry with several exposures
 * (for example 0.20, 0.14, and 0.10) and keep the run whose
 * @c tmp/detected_*.png overlays look best.
 *
 * Does not stop @c servo_calib (FIFO closed without sending -1).
 * Day-to-day measurements use @c servo_calib + @c sweep_run; recalibrate only
 * after optical changes (ROI calibration is normally already done).
 */

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <cmath>
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
#include "roi_interp.h"
#include "servo_fifo.h"
#include "utils.h"
#include "logger.h"
#include "crash.h"
}
#include "detect.hpp"

static void laser_on(void)
{
    int fd = open(CMD_PIPE, O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        LOG_WARN("no command FIFO; assuming the laser is already on");
        return;
    }
    const char cmd[] = "b255\n1001\n";
    if (write(fd, cmd, sizeof(cmd) - 1) < 0 && errno != EAGAIN && errno != EPIPE)
        LOG_WARN("laser_on write failed: %s", strerror(errno));
    close(fd);
    usleep(30000);
    LOG_INFO("Requested laser on via FIFO");
}

static void draw_overlay(const cv::Mat &gray, const rois_t &rois, const char *path)
{
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
    cv::imwrite(path, vis);
    LOG_INFO("wrote %s", path);
}

/**
 * @brief Move to @p pos, grab, detect @p nwant blobs into @p out.
 */
static int detect_at_pos(cam_t *cam, int pos, int nwant, rois_t *out,
                         const char *overlay_path, const char *frame_path)
{
    if (servo_fifo_move_and_wait(pos) != 0) {
        LOG_ERROR("move to %d failed", pos);
        return -1;
    }
    usleep(50000); /* settle */

    unsigned char *pix;
    int w, h, pitch;
    if (camera_api.grab(cam, &pix, &w, &h, &pitch) != 0)
        return -1;

    cv::Mat gray = frame_mat(pix, w, h, pitch);
    if (frame_path)
        cv::imwrite(frame_path, gray);

    if (detect_boxes_cv(gray, nwant, out) != 0) {
        LOG_ERROR("detect_boxes_cv failed at pos %d", pos);
        return -1;
    }
    LOG_INFO("pos %d: found %d blob(s)", pos, out->n);
    for (int i = 0; i < out->n; i++) {
        LOG_INFO("  %s cx=%.1f cy=%.1f rx=%.1f ry=%.1f ang=%.1f", out->item[i].name,
                 out->item[i].cx, out->item[i].cy, out->item[i].rx, out->item[i].ry,
                 out->item[i].angle);
    }
    if (overlay_path)
        draw_overlay(gray, *out, overlay_path);
    return out->n > 0 ? 0 : -1;
}

/**
 * @brief Step pos0..pos1, measure with interpolated ROIs (+ fixed-at-pos0 for compare).
 */
static int write_interp_check_csv(cam_t *cam, const roi_interp_t *model, const char *csv_path)
{
    FILE *csv = fopen(csv_path, "w");
    if (!csv) {
        LOG_ERROR("%s: %s", csv_path, strerror(errno));
        return -1;
    }

    fprintf(csv, "pos");
    for (int i = 0; i < model->at0.n; i++)
        fprintf(csv, ",%s_interp", model->at0.item[i].name);
    for (int i = 0; i < model->at0.n; i++)
        fprintf(csv, ",%s_fixed", model->at0.item[i].name);
    for (int i = 0; i < model->at0.n; i++)
        fprintf(csv, ",%s_cx,%s_cy", model->at0.item[i].name, model->at0.item[i].name);
    fprintf(csv, "\n");

    int lo = model->pos0 < model->pos1 ? model->pos0 : model->pos1;
    int hi = model->pos0 < model->pos1 ? model->pos1 : model->pos0;

    for (int pos = lo; pos <= hi; pos++) {
        if (servo_fifo_move_and_wait(pos) != 0) {
            LOG_ERROR("move to %d failed during check", pos);
            fclose(csv);
            return -1;
        }
        usleep(20000);

        unsigned char *pix;
        int w, h, pitch;
        if (camera_api.grab(cam, &pix, &w, &h, &pitch) != 0) {
            fclose(csv);
            return -1;
        }

        rois_t interp;
        if (roi_interp_at(model, pos, &interp) != 0) {
            fclose(csv);
            return -1;
        }
        roi_clip(&interp, w, h);

        rois_t fixed = model->at0;
        roi_clip(&fixed, w, h);

        double m_interp[ROI_MAX], m_fixed[ROI_MAX];
        roi_means(pix, w, h, pitch, &interp, m_interp);
        roi_means(pix, w, h, pitch, &fixed, m_fixed);

        fprintf(csv, "%d", pos);
        for (int i = 0; i < interp.n; i++)
            fprintf(csv, ",%.4f", m_interp[i]);
        for (int i = 0; i < fixed.n; i++)
            fprintf(csv, ",%.4f", m_fixed[i]);
        for (int i = 0; i < interp.n; i++)
            fprintf(csv, ",%.3f,%.3f", interp.item[i].cx, interp.item[i].cy);
        fprintf(csv, "\n");
        fflush(csv);

        LOG_INFO("check pos %d  %s_interp=%.1f  %s_fixed=%.1f", pos,
                 interp.item[0].name, m_interp[0], fixed.item[0].name, m_fixed[0]);
    }

    fclose(csv);
    LOG_INFO("wrote intensity check CSV %s", csv_path);
    return 0;
}

int main(int argc, char **argv)
{
    double exposure = argc > 1 ? strtod(argv[1], NULL) : 0.1;
    int pos0 = argc > 2 ? atoi(argv[2]) : 400;
    int pos1 = argc > 3 ? atoi(argv[3]) : 420;
    int nwant = argc > 4 ? atoi(argv[4]) : 3;
    const char *interp_path = argc > 5 ? argv[5] : ROI_INTERP_PATH;
    const char *csv_path = argc > 6 ? argv[6] : "./tmp/interp_intensity.csv";

    if (sprint1_logging_init("calibrate2", NULL, 0) != 0)
        return 1;

    if (pos0 == pos1) {
        LOG_ERROR("pos0 and pos1 must differ");
        return 1;
    }

    laser_on();

    if (servo_fifo_open() != 0)
        return 1;

    cam_t *cam = NULL;
    if (camera_api.open(&cam) != 0) {
        servo_fifo_close();
        return 1;
    }
    double actual = 0.0;
    if (camera_api.set_exposure_ms(cam, exposure, &actual) != 0) {
        camera_api.close(cam);
        servo_fifo_close();
        return 1;
    }
    LOG_INFO("camera %s exposure %.3f ms; calibrate2 at %d and %d",
             camera_api.sensor(cam), actual, pos0, pos1);

    roi_interp_t model;
    memset(&model, 0, sizeof(model));
    model.pos0 = pos0;
    model.pos1 = pos1;

    char overlay0[128], overlay1[128], frame0[128], frame1[128];
    snprintf(overlay0, sizeof(overlay0), "./tmp/detected_%d.png", pos0);
    snprintf(overlay1, sizeof(overlay1), "./tmp/detected_%d.png", pos1);
    snprintf(frame0, sizeof(frame0), "./tmp/calib_frame_%d.png", pos0);
    snprintf(frame1, sizeof(frame1), "./tmp/calib_frame_%d.png", pos1);

    if (detect_at_pos(cam, pos0, nwant, &model.at0, overlay0, frame0) != 0) {
        camera_api.close(cam);
        servo_fifo_close();
        return 1;
    }
    if (detect_at_pos(cam, pos1, nwant, &model.at1, overlay1, frame1) != 0) {
        camera_api.close(cam);
        servo_fifo_close();
        return 1;
    }
    if (model.at0.n != model.at1.n) {
        LOG_ERROR("blob count mismatch: %d at %d vs %d at %d", model.at0.n, pos0,
                  model.at1.n, pos1);
        camera_api.close(cam);
        servo_fifo_close();
        return 1;
    }

    /* Log per-blob centre delta (should be small / nearly linear over 20 steps). */
    for (int i = 0; i < model.at0.n; i++) {
        LOG_INFO("%s delta: dcx=%.2f dcy=%.2f (%.2f px/step)", model.at0.item[i].name,
                 model.at1.item[i].cx - model.at0.item[i].cx,
                 model.at1.item[i].cy - model.at0.item[i].cy,
                 hypot(model.at1.item[i].cx - model.at0.item[i].cx,
                       model.at1.item[i].cy - model.at0.item[i].cy) /
                     (double)(pos1 - pos0));
    }

    if (roi_interp_save(interp_path, &model) != 0) {
        camera_api.close(cam);
        servo_fifo_close();
        return 1;
    }
    /* Also write a mid-point rois.json snapshot for manual inspection (not used by sweep yet). */
    {
        rois_t mid;
        int midpos = (pos0 + pos1) / 2;
        if (roi_interp_at(&model, midpos, &mid) == 0)
            roi_save_json(ROI_PATH, &mid);
    }

    LOG_INFO("Sampling intensities pos %d..%d with interpolated ROIs (CSV for plot)",
             pos0 < pos1 ? pos0 : pos1, pos0 < pos1 ? pos1 : pos0);
    int rc = write_interp_check_csv(cam, &model, csv_path);

    camera_api.close(cam);
    servo_fifo_close();
    logger_flush();
    logger_exitFileLogger();

    if (rc == 0) {
        fprintf(stderr,
                "\nNext: plot intensity vs position to check for a flat slope:\n"
                "  python3 plot_interp_check.py %s ./tmp/interp_intensity.png\n\n",
                csv_path);
    }
    return rc == 0 ? 0 : 1;
}
