/**
 * @file sweep_run.c
 * @brief Step-by-step intensity sweep over the scan window in @c saved.prm.
 *
 * For each step from @c start to @c stop inclusive: move via FIFO, grab a crop,
 * average each ROI, append one CSV row. Requires @c servo_calib already running.
 *
 * A small raylib window shows the current step position centered on screen.
 *
 * Usage: @c ./sweep_run [rois.json] [exposure_ms] [out.csv]
 *
 * Exit closes the command FIFO without @c -1 (servo daemon left running).
 */

#define _DEFAULT_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "raylib.h"

#include "cam.h"
#include "roi.h"
#include "servo_fifo.h"
#include "logger.h"
#include "crash.h"

#define PRM_FILE "/var/lib/sprint1/saved.prm"
#define DEG_PER_STEP 0.1125

/**
 * @brief Parse @c start @c stop @c prep from @p path.
 * @return 0 on success, -1 on failure.
 */
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

/**
 * @brief True after a successful @c InitWindow (safe to call raylib draw APIs).
 */
static int g_ui_ok;

/**
 * @brief Point GLFW at the Pi HDMI desktop (labwc → Xwayland on @c :0).
 *
 * The bundled raylib for sweep_run is built with @c GRAPHICS_API_OPENGL_ES2 and
 * GLFW/X11 (not Wayland). Prebuilt desktop @c libGL raylib fails on Pi with
 * @c GLXBadFBConfig; use @c make raylib in camera-sdk first.
 */
static void ensure_display(void)
{
    const char *disp = getenv("DISPLAY");
    const char *xauth = getenv("XAUTHORITY");
    char path[256];

    unsetenv("WAYLAND_DISPLAY");

    if (disp == NULL || disp[0] == '\0')
        setenv("DISPLAY", ":0", 1);

    if (xauth == NULL || xauth[0] == '\0') {
        const char *home = getenv("HOME");
        if (home && home[0]) {
            snprintf(path, sizeof(path), "%s/.Xauthority", home);
            if (access(path, R_OK) == 0)
                setenv("XAUTHORITY", path, 1);
        }
        if ((xauth = getenv("XAUTHORITY")) == NULL || xauth[0] == '\0') {
            if (access("/home/temp/.Xauthority", R_OK) == 0)
                setenv("XAUTHORITY", "/home/temp/.Xauthority", 1);
        }
    }

    LOG_INFO("UI using DISPLAY=%s XAUTHORITY=%s",
             getenv("DISPLAY") ? getenv("DISPLAY") : "(null)",
             getenv("XAUTHORITY") ? getenv("XAUTHORITY") : "(null)");
}

/**
 * @brief Open the position window; leave @c g_ui_ok clear on failure (no crash).
 */
static void ui_open(void)
{
    ensure_display();
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    SetConfigFlags(FLAG_FULLSCREEN_MODE);  
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(640, 360, "sweep_run");
    g_ui_ok = IsWindowReady();
    if (!g_ui_ok) {
        LOG_ERROR("raylib window failed (need Pi ES2 lib + HDMI desktop). "
                  "Run: make -C camera-sdk raylib && make -C camera-sdk sweep_run. "
                  "Then from the Pi desktop: sudo DISPLAY=:0 XAUTHORITY=/home/temp/.Xauthority "
                  "./build/bin/sweep_run");
        return;
    }
    SetTargetFPS(30);
}

static void ui_close(void)
{
    if (g_ui_ok) {
        CloseWindow();
        g_ui_ok = 0;
    }
}

/**
 * @brief Draw the current motor position centered in the raylib window.
 */
static void show_position(int pos)
{
    char msg[64];
    int font_size = 48;
    int tw;

    if (!g_ui_ok)
        return;

    snprintf(msg, sizeof(msg), "Position: %d", pos);
    tw = MeasureText(msg, font_size);

    BeginDrawing();
    ClearBackground(BLACK);
    DrawText(msg, (GetScreenWidth() - tw) / 2, (GetScreenHeight() - font_size) / 2,
             font_size, RAYWHITE);
    EndDrawing();
}

/**
 * @brief Block until the user presses S (or closes the window).
 *
 * @c GetCharPressed() only sees keys after GLFW polls events, which raylib does
 * inside @c BeginDrawing / @c EndDrawing (or @c WindowShouldClose). A bare
 * @c while (GetCharPressed()) spin never polls and never returns.
 */
static int wait_for_start(int pos)
{
    if (!g_ui_ok)
        return 0;

    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_S))
            return 0;
        show_position(pos);
    }
    return 1;
}

/**
 * @brief Run the intensity sweep and write CSV.
 * @return 0 on success, non-zero on failure.
 */
int main(int argc, char **argv)
{
    const char *roi_path = argc > 1 ? argv[1] : ROI_PATH;
    double exposure = argc > 2 ? strtod(argv[2], NULL) : 0.1;
    const char *csv_path = argc > 3 ? argv[3] : "../sensor_data.csv";
    int aborted = 0;

    if (sprint1_logging_init("sweep_run", NULL, 0) != 0)
        return 1;

    rois_t rois;
    if (roi_load_json(roi_path, &rois) != 0)
        return 1;

    int start, stop, prep;
    if (load_prm(PRM_FILE, &start, &stop, &prep) != 0)
        return 1;
    LOG_INFO("scan %d .. %d (prep %d) from %s", start, stop, prep, PRM_FILE);

    cam_t cam;
    if (cam_open(&cam) != 0)
        return 1;

    double actual = 0.0;
    if (cam_set_exposure_ms(&cam, exposure, &actual) != 0) {
        cam_close(&cam);
        return 1;
    }

    if (servo_fifo_open() != 0) {
        cam_close(&cam);
        return 1;
    }

    ui_open();
    if (g_ui_ok && wait_for_start(start)) {
        ui_close();
        servo_fifo_close();
        cam_close(&cam);
        return 1;
    }

    FILE *csv = fopen(csv_path, "w");
    if (!csv) {
        LOG_ERROR("%s: %s", csv_path, strerror(errno));
        ui_close();
        servo_fifo_close();
        cam_close(&cam);
        return 1;
    }
    fprintf(csv, "Angle_deg");
    for (int i = 0; i < rois.n; i++)
        fprintf(csv, ",%s", rois.item[i].name);
    fprintf(csv, "\n");
    fflush(csv);

    if (servo_fifo_move_and_wait(start) != 0) {
        LOG_ERROR("move_and_wait(%d) failed — is servo_calib running?", start);
        fclose(csv);
        ui_close();
        servo_fifo_close();
        cam_close(&cam);
        return 1;
    }
    show_position(start);

    unsigned char *pix;
    int w, h, pitch;
    if (cam_grab(&cam, &pix, &w, &h, &pitch) != 0) {
        fclose(csv);
        ui_close();
        servo_fifo_close();
        cam_close(&cam);
        return 1;
    }
    roi_clip(&rois, w, h);

    double means[ROI_MAX];
    double sat[ROI_MAX];
    roi_means(pix, w, h, pitch, &rois, means);
    roi_sat_frac(pix, w, h, pitch, &rois, 250, sat);
    for (int i = 0; i < rois.n; i++) {
        if (sat[i] > 0.05)
            LOG_WARN("%s is %.0f%% saturated; lower exposure", rois.item[i].name,
                     sat[i] * 100.0);
    }

    LOG_INFO("Sweep starting: %d steps", stop - start + 1);
    for (int pos = start; pos <= stop; pos++) {
        if (g_ui_ok && WindowShouldClose()) {
            LOG_INFO("Window closed — aborting sweep at pos %d", pos);
            aborted = 1;
            break;
        }

        if (servo_fifo_move_and_wait(pos) != 0) {
            LOG_ERROR("move_and_wait(%d) failed", pos);
            fclose(csv);
            ui_close();
            servo_fifo_close();
            cam_close(&cam);
            return 1;
        }
        show_position(pos);
        if (g_ui_ok && IsKeyPressed(KEY_Q)) {
            LOG_INFO("User pressed Q — aborting sweep at pos %d", pos);
            aborted = 1;
            break;
        }
        if (cam_grab(&cam, &pix, &w, &h, &pitch) != 0) {
            fclose(csv);
            ui_close();
            servo_fifo_close();
            cam_close(&cam);
            return 1;
        }
        roi_means(pix, w, h, pitch, &rois, means);

        double angle = pos * DEG_PER_STEP;
        fprintf(csv, "%.4f", angle);
        for (int i = 0; i < rois.n; i++)
            fprintf(csv, ",%.4f", means[i]);
        fprintf(csv, "\n");
        fflush(csv);

        LOG_INFO("pos %d angle %.4f means[0]=%.1f", pos, angle,
                 rois.n > 0 ? means[0] : 0.0);
    }

    fclose(csv);
    usleep(10000);
    ui_close();
    servo_fifo_close();
    cam_close(&cam);
    LOG_INFO("wrote %s%s", csv_path, aborted ? " (aborted early)" : "");
    logger_flush();
    logger_exitFileLogger();
    return aborted ? 1 : 0;
}
