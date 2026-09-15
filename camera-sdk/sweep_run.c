/**
 * @file sweep_run.c
 * @brief Friendly TUI for a step-by-step intensity sweep over @c saved.prm.
 *
 * Shows a colorful Start / Reset screen for non-technical operators. Start runs
 * the scan (FIFO move + ROI means → CSV). Reset parks at @c prep and prepares
 * for another run. Requires @c servo_calib already running.
 *
 * Text is wrapped with vt100utils (via tuibox) so colored lines reflow when the
 * terminal is resized.
 *
 * Usage: @c ./sweep_run [rois.json] [exposure_ms] [out.csv]
 *
 * Exit leaves the servo daemon running (FIFO closed without @c -1).
 */

#define _DEFAULT_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cam.h"
#include "roi.h"
#include "servo_fifo.h"
#include "logger.h"
#include "crash.h"
#include "../tuibox.h"

#define PRM_FILE "/var/lib/sprint1/saved.prm"
#define DEG_PER_STEP 0.1125
#define PANEL_MAX 56
#define PANEL_MIN 28

/* ── App state ─────────────────────────────────────────────────────────── */

typedef enum {
    ST_READY = 0,
    ST_RUNNING,
    ST_DONE,
    ST_ERROR,
} state_t;

typedef struct {
    ui_t *ui;
    cam_t *cam;
    rois_t *rois;
    int start;
    int stop;
    int prep;
    const char *csv_path;
    double exposure;

    state_t state;
    int quit;
    int progress; /* 0..100 */
    int panel_w;  /* content width; tracks terminal */
    char status[160];
    char detail[160];
    char tick; /* bump to invalidate watched boxes */
} app_t;

static app_t g;

static int panel_width(ui_t *ui)
{
    int w = ui->ws.ws_col - 6;
    if (w > PANEL_MAX)
        w = PANEL_MAX;
    if (w < PANEL_MIN)
        w = PANEL_MIN;
    return w;
}

static void bump(void)
{
    g.tick++;
    if (g.ui) {
        ui_refresh_size(g.ui);
        g.panel_w = panel_width(g.ui);
        ui_redraw(g.ui);
    }
}

static void set_status(state_t st, const char *status, const char *detail)
{
    g.state = st;
    snprintf(g.status, sizeof(g.status), "%s", status ? status : "");
    snprintf(g.detail, sizeof(g.detail), "%s", detail ? detail : "");
}

/* ── Sweep / reset ─────────────────────────────────────────────────────── */

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

static int do_reset(void)
{
    set_status(ST_RUNNING, "Resetting…", "Moving back to the ready position.");
    g.progress = 0;
    bump();

    if (servo_fifo_move_and_wait(g.prep) != 0) {
        set_status(ST_ERROR, "Could not reset.",
                   "Is the motor controller (servo_calib) running?");
        bump();
        return -1;
    }

    g.progress = 0;
    set_status(ST_READY, "Ready to scan.",
               "Press Start when you want to begin.");
    bump();
    return 0;
}

static int do_sweep(void)
{
    FILE *csv;
    unsigned char *pix;
    int w, h, pitch;
    double means[ROI_MAX];
    double sat[ROI_MAX];
    int total;
    int done_steps = 0;

    set_status(ST_RUNNING, "Starting scan…", "Moving to the first position.");
    g.progress = 0;
    bump();

    csv = fopen(g.csv_path, "w");
    if (!csv) {
        LOG_ERROR("%s: %s", g.csv_path, strerror(errno));
        set_status(ST_ERROR, "Could not create the results file.", g.csv_path);
        bump();
        return -1;
    }
    fprintf(csv, "Angle_deg");
    for (int i = 0; i < g.rois->n; i++)
        fprintf(csv, ",%s", g.rois->item[i].name);
    fprintf(csv, "\n");
    fflush(csv);

    if (servo_fifo_move_and_wait(g.start) != 0) {
        LOG_ERROR("move_and_wait(%d) failed", g.start);
        fclose(csv);
        set_status(ST_ERROR, "Could not move into place.",
                   "Check that servo_calib is running.");
        bump();
        return -1;
    }

    if (cam_grab(g.cam, &pix, &w, &h, &pitch) != 0) {
        fclose(csv);
        set_status(ST_ERROR, "Camera grab failed.", "Check the camera connection.");
        bump();
        return -1;
    }
    roi_clip(g.rois, w, h);

    roi_means(pix, w, h, pitch, g.rois, means);
    roi_sat_frac(pix, w, h, pitch, g.rois, 250, sat);
    for (int i = 0; i < g.rois->n; i++) {
        if (sat[i] > 0.05)
            LOG_WARN("%s is %.0f%% saturated; lower exposure", g.rois->item[i].name,
                     sat[i] * 100.0);
    }

    total = g.stop - g.start + 1;
    if (total < 1)
        total = 1;

    LOG_INFO("Sweep starting: %d steps", total);
    for (int pos = g.start; pos <= g.stop; pos++) {
        char detail[160];

        if (servo_fifo_move_and_wait(pos) != 0) {
            LOG_ERROR("move_and_wait(%d) failed", pos);
            fclose(csv);
            set_status(ST_ERROR, "Scan stopped — motor did not arrive.",
                       "Try Reset, then Start again.");
            bump();
            return -1;
        }
        if (cam_grab(g.cam, &pix, &w, &h, &pitch) != 0) {
            fclose(csv);
            set_status(ST_ERROR, "Camera grab failed during the scan.",
                       "Try Reset, then Start again.");
            bump();
            return -1;
        }
        roi_means(pix, w, h, pitch, g.rois, means);

        double angle = pos * DEG_PER_STEP;
        fprintf(csv, "%.4f", angle);
        for (int i = 0; i < g.rois->n; i++)
            fprintf(csv, ",%.4f", means[i]);
        fprintf(csv, "\n");
        fflush(csv);

        done_steps++;
        g.progress = (done_steps * 100) / total;
        snprintf(detail, sizeof(detail), "Measuring… %d of %d  (%.1f°)",
                 done_steps, total, angle);
        set_status(ST_RUNNING, "Scan in progress", detail);
        bump();

        LOG_INFO("pos %d angle %.4f means[0]=%.1f", pos, angle,
                 g.rois->n > 0 ? means[0] : 0.0);
    }

    fclose(csv);
    g.progress = 100;
    {
        char detail[160];
        snprintf(detail, sizeof(detail), "Saved to %s", g.csv_path);
        set_status(ST_DONE, "All done!", detail);
    }
    bump();
    LOG_INFO("wrote %s", g.csv_path);
    return 0;
}

/* ── Drawing helpers (vt100-aware wrap) ────────────────────────────────── */

static void hline(char *dst, int n, const char *ch)
{
    int i;
    dst[0] = '\0';
    for (i = 0; i < n; i++)
        strcat(dst, ch);
}

static void draw_title(ui_box_t *b, char *out)
{
    char top[256], bot[256], mid[128], sub[128];
    int inner = g.panel_w - 2;
    if (inner < 10)
        inner = 10;

    (void)b;
    hline(top, inner, "═");
    hline(bot, inner, "═");
    snprintf(mid, sizeof(mid), "%-*.*s", inner, inner, "  sweep_run program test tui");
    snprintf(out, MAXCACHESIZE,
             "\x1b[1;36m╔%s╗\x1b[0m\n"
             "\x1b[1;36m║\x1b[0m\x1b[1;37m%s\x1b[0m\x1b[1;36m║\x1b[0m\n"
             "\x1b[1;36m╚%s╝\x1b[0m",
             top, mid, bot);
}

static void draw_start_btn(ui_box_t *b, char *out)
{
    char raw[128];
    int active = (g.state != ST_RUNNING);
    (void)b;
    if (active)
        snprintf(raw, sizeof(raw),
                 "\x1b[42;1;30m                \x1b[0m\n"
                 "\x1b[42;1;30m   >  START     \x1b[0m\n"
                 "\x1b[42;1;30m                \x1b[0m");
    else
        snprintf(raw, sizeof(raw),
                 "\x1b[100;37m                \x1b[0m\n"
                 "\x1b[100;37m   >  START     \x1b[0m\n"
                 "\x1b[100;37m                \x1b[0m");
    snprintf(out, MAXCACHESIZE, "%s", raw);
}

static void draw_reset_btn(ui_box_t *b, char *out)
{
    char raw[128];
    int active = (g.state != ST_RUNNING);
    (void)b;
    if (active)
        snprintf(raw, sizeof(raw),
                 "\x1b[43;1;30m                \x1b[0m\n"
                 "\x1b[43;1;30m   <  RESET     \x1b[0m\n"
                 "\x1b[43;1;30m                \x1b[0m");
    else
        snprintf(raw, sizeof(raw),
                 "\x1b[100;37m                \x1b[0m\n"
                 "\x1b[100;37m   <  RESET     \x1b[0m\n"
                 "\x1b[100;37m                \x1b[0m");
    snprintf(out, MAXCACHESIZE, "%s", raw);
}

static void draw_status(ui_box_t *b, char *out)
{
    char colored[256];
    const char *color = "\x1b[37m";
    (void)b;
    switch (g.state) {
    case ST_READY:
        color = "\x1b[1;32m";
        break;
    case ST_RUNNING:
        color = "\x1b[1;33m";
        break;
    case ST_DONE:
        color = "\x1b[1;36m";
        break;
    case ST_ERROR:
        color = "\x1b[1;31m";
        break;
    }
    snprintf(colored, sizeof(colored), "%s%s\x1b[0m", color, g.status);
    ui_wrap_text(colored, g.panel_w, out, MAXCACHESIZE);
}

static void draw_detail(ui_box_t *b, char *out)
{
    char colored[256];
    (void)b;
    snprintf(colored, sizeof(colored), "\x1b[37m%s\x1b[0m", g.detail);
    ui_wrap_text(colored, g.panel_w, out, MAXCACHESIZE);
}

static void draw_progress(ui_box_t *b, char *out)
{
    char bar[64];
    char colored[128];
    int bar_w = g.panel_w - 8;
    int filled;
    int i;

    (void)b;
    if (bar_w > 40)
        bar_w = 40;
    if (bar_w < 10)
        bar_w = 10;

    filled = (g.progress * bar_w) / 100;
    if (filled > bar_w)
        filled = bar_w;
    if (filled < 0)
        filled = 0;
    for (i = 0; i < bar_w; i++)
        bar[i] = (i < filled) ? '#' : '-';
    bar[bar_w] = '\0';

    if (g.state == ST_RUNNING || g.progress > 0)
        snprintf(colored, sizeof(colored),
                 "\x1b[1;34m[%s]\x1b[0m \x1b[1;37m%3d%%\x1b[0m", bar, g.progress);
    else
        snprintf(colored, sizeof(colored), "\x1b[90m[%s]   0%%\x1b[0m", bar);

    ui_wrap_text(colored, g.panel_w, out, MAXCACHESIZE);
}

static void draw_help(ui_box_t *b, char *out)
{
    (void)b;
    /* Manual line breaks — ui_wrap_text splits mid-word. */
    snprintf(out, MAXCACHESIZE,
             "\x1b[90mStart\x1b[0m  \x1b[1;32mbegin scan\x1b[0m  (click or S)\n"
             "\x1b[90mReset\x1b[0m  \x1b[1;33mgo back to ready\x1b[0m  (click or R)\n"
             "\x1b[1;37mQ\x1b[0m       quit");
}

static void draw_range(ui_box_t *b, char *out)
{
    char colored[256];
    (void)b;
    snprintf(colored, sizeof(colored),
             "\x1b[90mScan range: steps %d → %d   (park %d)\x1b[0m", g.start, g.stop,
             g.prep);
    ui_wrap_text(colored, g.panel_w, out, MAXCACHESIZE);
}

/* ── Clicks / keys ─────────────────────────────────────────────────────── */

static void on_start(ui_box_t *b, int x, int y, int down)
{
    (void)b;
    (void)x;
    (void)y;
    if (!down || g.state == ST_RUNNING)
        return;
    (void)do_sweep();
}

static void on_reset(ui_box_t *b, int x, int y, int down)
{
    (void)b;
    (void)x;
    (void)y;
    if (!down || g.state == ST_RUNNING)
        return;
    (void)do_reset();
}

static void on_enable(ui_box_t *b, int x, int y, int down)
{
    (void)b;
    (void)x;
    (void)y;
    if (!down || g.state == ST_RUNNING)
        return;
    if (servo_fifo_enable() != 0) {
        set_status(ST_ERROR, "Could not toggle the motor.",
                   "Is the motor controller (servo_calib) running?");
        bump();
        return;
    }

    if (servo_fifo_is_enabled()) {
        set_status(ST_READY, "Motor enabled.", "Ready to move.");
    } else {
        set_status(ST_READY, "Motor disabled.", "Power is off.");
    }
    bump();
}
static void on_quit(void)
{
    g.quit = 1;
}

static void on_key_start(void)
{
    if (g.state == ST_RUNNING)
        return;
    (void)do_sweep();
}

static void on_key_reset(void)
{
    if (g.state == ST_RUNNING)
        return;
    (void)do_reset();
}

static void build_ui(ui_t *ui)
{
    int pw = g.panel_w;
    int cx = ui_center_x(pw, ui);
    int cy = ui_center_y(16, ui);
    int btn_gap;
    int help_lines;

    if (cy < 1)
        cy = 1;
    if (cx < 1)
        cx = 1;

    btn_gap = (pw >= 40) ? (pw - 32) / 3 : 2;
    if (btn_gap < 2)
        btn_gap = 2;

    help_lines = 3;

    ui_add(cx, cy, pw, 4, 0, &g.tick, 0, draw_title, NULL, NULL, NULL, NULL, ui);

    ui_add(cx + btn_gap, cy + 5, 16, 3, 0, &g.tick, 0, draw_start_btn, on_start, NULL,
           NULL, NULL, ui);
    ui_add(cx + btn_gap + 16 + btn_gap, cy + 5, 16, 3, 0, &g.tick, 0, draw_reset_btn,
           on_reset, NULL, NULL, NULL, ui);

    ui_add(cx, cy + 9, pw, 2, 0, &g.tick, 0, draw_status, NULL, NULL, NULL, NULL, ui);
    ui_add(cx, cy + 11, pw, 2, 0, &g.tick, 0, draw_detail, NULL, NULL, NULL, NULL, ui);
    ui_add(cx, cy + 13, pw, 1, 0, &g.tick, 0, draw_progress, NULL, NULL, NULL, NULL,
           ui);

    ui_add(cx, cy + 15, pw, help_lines, 0, &g.tick, 0, draw_help, NULL, NULL, NULL,
           NULL, ui);
    ui_add(cx, cy + 15 + help_lines + 1, pw, 2, 0, &g.tick, 0, draw_range, NULL, NULL,
           NULL, NULL, ui);

    ui_key("q", on_quit, ui);
    ui_key("Q", on_quit, ui);
    ui_key("s", on_key_start, ui);
    ui_key("S", on_key_start, ui);
    ui_key("r", on_key_reset, ui);
    ui_key("R", on_key_reset, ui);
}

static void relayout(ui_t *ui)
{
    ui_reset(ui);
    ui->canscroll = 0;
    g.panel_w = panel_width(ui);
    build_ui(ui);
    ui_draw(ui);
}

/* ── main ──────────────────────────────────────────────────────────────── */

int main(int argc, char **argv)
{
    const char *roi_path = argc > 1 ? argv[1] : ROI_PATH;
    double exposure = argc > 2 ? strtod(argv[2], NULL) : 0.1;
    const char *csv_path = argc > 3 ? argv[3] : "../sensor_data.csv";

    cam_t cam;
    rois_t rois;
    ui_t ui;
    double actual = 0.0;
    int last_cols = 0;
    int last_rows = 0;

    memset(&g, 0, sizeof(g));
    g.csv_path = csv_path;
    g.exposure = exposure;

    if (sprint1_logging_init("sweep_run", NULL, 0) != 0)
        return 1;

    if (roi_load_json(roi_path, &rois) != 0)
        return 1;
    g.rois = &rois;

    if (load_prm(PRM_FILE, &g.start, &g.stop, &g.prep) != 0)
        return 1;
    LOG_INFO("scan %d .. %d (prep %d) from %s", g.start, g.stop, g.prep, PRM_FILE);

    if (cam_open(&cam) != 0)
        return 1;
    g.cam = &cam;

    if (cam_set_exposure_ms(&cam, exposure, &actual) != 0) {
        cam_close(&cam);
        return 1;
    }

    if (servo_fifo_open() != 0) {
        cam_close(&cam);
        return 1;
    }

    ui_new(0, &ui);
    ui.canscroll = 0;
    g.ui = &ui;
    g.panel_w = panel_width(&ui);
    last_cols = ui.ws.ws_col;
    last_rows = ui.ws.ws_row;

    set_status(ST_READY, "Ready to scan.", "Press Start or S to let it move");
    build_ui(&ui);
    ui_draw(&ui);

    {
        char buf[64];
        int n;
        while (!g.quit && (n = read(STDIN_FILENO, buf, sizeof(buf))) > 0) {
            ui_refresh_size(&ui);
            if (ui.ws.ws_col != last_cols || ui.ws.ws_row != last_rows) {
                last_cols = ui.ws.ws_col;
                last_rows = ui.ws.ws_row;
                relayout(&ui);
            }
            _ui_update(buf, n, &ui);
        }
    }

    g.ui = NULL;
    ui_free(&ui);

    servo_fifo_close();
    cam_close(&cam);
    LOG_INFO("TUI exit (servo daemon left running)");
    logger_flush();
    logger_exitFileLogger();
    return (g.state == ST_ERROR) ? 1 : 0;
}
