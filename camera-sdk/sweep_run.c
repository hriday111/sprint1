/**
 * @file sweep_run.c
 * @brief Main measurement tool: disease-assignment TUI + intensity sweep.
 *
 * Normal workflow (ROI calibration is already done for the current optics):
 *   1. Start the driver: @c ./build/servo_calib
 *   2. Run this tool: @c ./camera-sdk/build/bin/sweep_run
 *
 * Flow: pick D1–D10 for blob 1 and blob 2 → Start/Reset screen → CSV.
 * Requires @c servo_calib already running.
 *
 * Usage: @c ./sweep_run [rois.json] [exposure_ms] [out.csv]
 *
 * Exit leaves the servo daemon running (FIFO closed without @c -1).
 * Recalibrate with @c calibrate2 only after lens moves; try exposures
 * 0.20, 0.14, and 0.10 if detection looks wrong.
 */

#define _DEFAULT_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cam_api.h"
#include "roi.h"
#include "ipc_client.h"
#include "logger.h"
#include "crash.h"
#include "sweep_tui.h"

#define PRM_FILE "/var/lib/sprint1/saved.prm"
#define DEG_PER_STEP 0.1125

typedef struct {
    cam_t *cam;
    rois_t *rois;
    disease_assign_t *assign;
    int start;
    int stop;
    int prep;
    const char *csv_path;
} sweep_ctx_t;

static int load_prm(const char *path, int *start, int *stop, int *prep)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        LOG_ERROR("%s: %s", path, strerror(errno));
        return -1;
    }
    int n = fscanf(f, "%d %d %d", start, stop, prep);
    fclose(f);
    if (n != 3) {
        LOG_ERROR("%s: expected 3 integers", path);
        return -1;
    }
    return 0;
}

static int do_reset(void *userdata)
{
    sweep_ctx_t *ctx = userdata;

    sweep_tui_set_running("Resetting…", "Moving back to the ready position.");
    sweep_tui_set_progress(0, NULL);

    if (fifo_move_and_wait(ctx->prep) != 0) {
        sweep_tui_set_error("Could not reset.",
                            "Is the motor controller (servo_calib) running?");
        return -1;
    }

    sweep_tui_set_progress(0, NULL);
    sweep_tui_set_ready("Ready to scan.", "Press Start when you want to begin.");
    return 0;
}

static int do_sweep(void *userdata)
{
    sweep_ctx_t *ctx = userdata;
    FILE *csv;
    unsigned char *pix;
    int w, h, pitch;
    double means[ROI_MAX];
    double sat[ROI_MAX];
    int total;
    int done_steps = 0;
    char d1[8], d2[8];

    disease_label(ctx->assign->blob1, d1, sizeof(d1));
    disease_label(ctx->assign->blob2, d2, sizeof(d2));

    sweep_tui_set_running("Starting scan…", "Moving to the first position.");
    sweep_tui_set_progress(0, NULL);

    csv = fopen(ctx->csv_path, "w");
    if (!csv) {
        LOG_ERROR("%s: %s", ctx->csv_path, strerror(errno));
        sweep_tui_set_error("Could not create the results file.", ctx->csv_path);
        return -1;
    }
    fprintf(csv, "Angle_deg");
    for (int i = 0; i < ctx->rois->n; i++)
        fprintf(csv, ",%s", ctx->rois->item[i].name);
    fprintf(csv, ",blob1_disease,blob2_disease\n");
    fflush(csv);

    if (fifo_move_and_wait(ctx->start) != 0) {
        LOG_ERROR("move_and_wait(%d) failed", ctx->start);
        fclose(csv);
        sweep_tui_set_error("Could not move into place.",
                            "Check that servo_calib is running.");
        return -1;
    }

    if (camera_api.grab(ctx->cam, &pix, &w, &h, &pitch) != 0) {
        fclose(csv);
        sweep_tui_set_error("Camera grab failed.", "Check the camera connection.");
        return -1;
    }
    roi_clip(ctx->rois, w, h);

    roi_means(pix, w, h, pitch, ctx->rois, means);
    roi_sat_frac(pix, w, h, pitch, ctx->rois, 250, sat);
    for (int i = 0; i < ctx->rois->n; i++) {
        if (sat[i] > 0.05)
            LOG_WARN("%s is %.0f%% saturated; lower exposure",
                     ctx->rois->item[i].name, sat[i] * 100.0);
    }

    total = ctx->stop - ctx->start + 1;
    if (total < 1)
        total = 1;

    LOG_INFO("Sweep starting: %d steps (blob1=%s blob2=%s)", total, d1, d2);
    for (int pos = ctx->start; pos <= ctx->stop; pos++) {
        char detail[160];

        if (fifo_move_and_wait(pos) != 0) {
            LOG_ERROR("move_and_wait(%d) failed", pos);
            fclose(csv);
            sweep_tui_set_error("Scan stopped — motor did not arrive.",
                                "Try Reset, then Start again.");
            return -1;
        }
        if (camera_api.grab(ctx->cam, &pix, &w, &h, &pitch) != 0) {
            fclose(csv);
            sweep_tui_set_error("Camera grab failed during the scan.",
                                "Try Reset, then Start again.");
            return -1;
        }
        roi_means(pix, w, h, pitch, ctx->rois, means);

        double angle = pos * DEG_PER_STEP;
        fprintf(csv, "%.4f", angle);
        for (int i = 0; i < ctx->rois->n; i++)
            fprintf(csv, ",%.4f", means[i]);
        fprintf(csv, ",%s,%s\n", d1, d2);
        fflush(csv);

        done_steps++;
        snprintf(detail, sizeof(detail), "Measuring… %d of %d  (%.1f°)",
                 done_steps, total, angle);
        sweep_tui_set_running("Scan in progress", detail);
        sweep_tui_set_progress((done_steps * 100) / total, detail);

        LOG_INFO("pos %d angle %.4f means[0]=%.1f", pos, angle,
                 ctx->rois->n > 0 ? means[0] : 0.0);
    }

    fclose(csv);
    {
        char detail[160];
        snprintf(detail, sizeof(detail), "Saved to %s", ctx->csv_path);
        sweep_tui_set_done("All done!", detail);
    }
    LOG_INFO("wrote %s", ctx->csv_path);
    return 0;
}

int main(int argc, char **argv)
{
    const char *roi_path = argc > 1 ? argv[1] : ROI_PATH;
    double exposure = argc > 2 ? strtod(argv[2], NULL) : 0.1;
    const char *csv_path = argc > 3 ? argv[3] : "../sensor_data.csv";

    cam_t *cam = NULL;
    rois_t rois;
    disease_assign_t assign;
    sweep_ctx_t ctx;
    sweep_tui_hooks_t hooks;
    double actual = 0.0;
    int rc;

    memset(&assign, 0, sizeof(assign));
    memset(&ctx, 0, sizeof(ctx));
    memset(&hooks, 0, sizeof(hooks));

    if (sprint1_logging_init("sweep_run", NULL, 0) != 0)
        return 1;

    if (roi_load_json(roi_path, &rois) != 0)
        return 1;

    if (load_prm(PRM_FILE, &ctx.start, &ctx.stop, &ctx.prep) != 0)
        return 1;
    LOG_INFO("scan %d .. %d (prep %d) from %s", ctx.start, ctx.stop, ctx.prep,
             PRM_FILE);

    if (camera_api.open(&cam) != 0)
        return 1;

    if (camera_api.set_exposure_ms(cam, exposure, &actual) != 0) {
        camera_api.close(cam);
        return 1;
    }

    if (fifo_open() != 0) {
        camera_api.close(cam);
        return 1;
    }

    ctx.cam = cam;
    ctx.rois = &rois;
    ctx.assign = &assign;
    ctx.csv_path = csv_path;

    hooks.on_start = do_sweep;
    hooks.on_reset = do_reset;
    hooks.userdata = &ctx;
    hooks.scan_start = ctx.start;
    hooks.scan_stop = ctx.stop;
    hooks.scan_prep = ctx.prep;

    rc = sweep_tui_run(&assign, &hooks);

    fifo_close();
    camera_api.close(cam);
    LOG_INFO("TUI exit (servo daemon left running)");
    logger_flush();
    logger_exitFileLogger();
    return rc;
}
