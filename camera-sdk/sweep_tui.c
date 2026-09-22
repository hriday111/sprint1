/**
 * @file sweep_tui.c
 * @brief Disease picker (left) + Start/Reset run screen (tuibox).
 */

#include "sweep_tui.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../tuibox.h"

#define PANEL_MAX 56
#define PANEL_MIN 28
#define SCR_PICK 0
#define SCR_RUN 1

typedef enum {
    ST_READY = 0,
    ST_RUNNING,
    ST_DONE,
    ST_ERROR,
} state_t;

/** Which blob the next disease click assigns. */
typedef enum {
    PICK_BLOB1 = 0,
    PICK_BLOB2 = 1,
} pick_phase_t;

typedef struct {
    ui_t *ui;
    disease_assign_t *assign;
    sweep_tui_hooks_t hooks;

    state_t state;
    int quit;
    int progress;
    int panel_w;
    pick_phase_t phase;
    char status[160];
    char detail[160];
    char tick;
} tui_t;

static tui_t g;

void disease_label(int id, char *buf, size_t buflen)
{
    if (id >= 1 && id <= SWEEP_DISEASE_COUNT)
        snprintf(buf, buflen, "D%d", id);
    else
        snprintf(buf, buflen, "—");
}

static int panel_width(ui_t *ui)
{
    int w = ui->ws.ws_col - 4;
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

static int assignments_ready(void)
{
    return g.assign && g.assign->blob1 >= 1 && g.assign->blob2 >= 1;
}

/* ── Pick screen (SCR_PICK) ────────────────────────────────────────────── */

static void draw_pick_title(ui_box_t *b, char *out)
{
    (void)b;
    snprintf(out, MAXCACHESIZE,
             "\x1b[1;36m╔══════════════════════════╗\x1b[0m\n"
             "\x1b[1;36m║\x1b[0m \x1b[1;37m  Choose diseases      \x1b[0m \x1b[1;36m ║\x1b[0m\n"
             "\x1b[1;36m╚══════════════════════════╝\x1b[0m");
}

static void draw_pick_prompt(ui_box_t *b, char *out)
{
    (void)b;
    if (g.phase == PICK_BLOB1)
        snprintf(out, MAXCACHESIZE,
                 "\x1b[1;33m> Assign a disease to Blob 1\x1b[0m\n"
                 "\x1b[90m  (click D1-D10 on the left)\x1b[0m");
    else
        snprintf(out, MAXCACHESIZE,
                 "\x1b[1;33m> Assign a disease to Blob 2\x1b[0m\n"
                 "\x1b[90m  (click D1-D10 on the left)\x1b[0m");
}

static void draw_pick_summary(ui_box_t *b, char *out)
{
    char a[8], c[8];
    (void)b;
    disease_label(g.assign->blob1, a, sizeof(a));
    disease_label(g.assign->blob2, c, sizeof(c));
    snprintf(out, MAXCACHESIZE,
             "\x1b[1;37mBlob 1:\x1b[0m \x1b[1;32m%-4s\x1b[0m   "
             "\x1b[1;37mBlob 2:\x1b[0m \x1b[1;35m%-4s\x1b[0m",
             a, c);
}

static void draw_disease_btn(ui_box_t *b, char *out)
{
    int id = (int)(intptr_t)b->data1;
    int selected =
        (g.assign->blob1 == id) || (g.assign->blob2 == id);
    char label[8];

    disease_label(id, label, sizeof(label));
    if (selected)
        snprintf(out, MAXCACHESIZE,
                 "\x1b[42;1;30m  %-4s  \x1b[0m", label);
    else
        snprintf(out, MAXCACHESIZE,
                 "\x1b[44;1;37m  %-4s  \x1b[0m", label);
}

static void draw_confirm_btn(ui_box_t *b, char *out)
{
    (void)b;
    if (assignments_ready())
        snprintf(out, MAXCACHESIZE,
                 "\x1b[42;1;30m                    \x1b[0m\n"
                 "\x1b[42;1;30m   CONTINUE  ->      \x1b[0m\n"
                 "\x1b[42;1;30m                    \x1b[0m");
    else
        snprintf(out, MAXCACHESIZE,
                 "\x1b[100;37m                    \x1b[0m\n"
                 "\x1b[100;37m   CONTINUE  ->      \x1b[0m\n"
                 "\x1b[100;37m                    \x1b[0m");
}

static void draw_clear_btn(ui_box_t *b, char *out)
{
    (void)b;
    snprintf(out, MAXCACHESIZE,
             "\x1b[43;1;30m           \x1b[0m\n"
             "\x1b[43;1;30m   CLEAR   \x1b[0m\n"
             "\x1b[43;1;30m           \x1b[0m");
}

static void draw_pick_help(ui_box_t *b, char *out)
{
    (void)b;
    snprintf(out, MAXCACHESIZE,
             "\x1b[90m1,2,...,0  pick disease   C continue   X clear   Q quit\x1b[0m");
}

static void build_run_ui(ui_t *ui); /* forward */

static void go_run_screen(void)
{
    if (!assignments_ready())
        return;
    ui_screen(SCR_RUN, g.ui);
    set_status(ST_READY, "Ready to scan.", "Press Start when you want to begin.");
    g.progress = 0;
    bump();
}

static void on_disease_click(ui_box_t *b, int x, int y, int down)
{
    int id = (int)(intptr_t)b->data1;
    (void)x;
    (void)y;
    if (!down || !g.ui || g.ui->screen != SCR_PICK)
        return;

    if (g.phase == PICK_BLOB1) {
        g.assign->blob1 = id;
        /* Same disease on both blobs is allowed; clear blob2 if it matched old flow */
        g.phase = PICK_BLOB2;
        set_status(ST_READY, "Blob 1 set.", "Now pick a disease for Blob 2.");
    } else {
        g.assign->blob2 = id;
        set_status(ST_READY, "Blob 2 set.",
                   assignments_ready() ? "Press Continue to open Start/Reset."
                                       : "Pick again if needed.");
    }
    bump();
}

static void on_confirm_click(ui_box_t *b, int x, int y, int down)
{
    (void)b;
    (void)x;
    (void)y;
    if (!down || !g.ui || g.ui->screen != SCR_PICK || !assignments_ready())
        return;
    go_run_screen();
}

static void on_clear_click(ui_box_t *b, int x, int y, int down)
{
    (void)b;
    (void)x;
    (void)y;
    if (!down || !g.ui || g.ui->screen != SCR_PICK)
        return;
    g.assign->blob1 = 0;
    g.assign->blob2 = 0;
    g.phase = PICK_BLOB1;
    set_status(ST_READY, "Cleared.", "Pick a disease for Blob 1.");
    bump();
}

static void on_quit(void)
{
    g.quit = 1;
}

static void pick_disease(int id)
{
    ui_box_t fake = {0};
    if (!g.ui || g.ui->screen != SCR_PICK)
        return;
    fake.data1 = (void *)(intptr_t)id;
    on_disease_click(&fake, 0, 0, 1);
}

static void on_key_d1(void) { pick_disease(1); }
static void on_key_d2(void) { pick_disease(2); }
static void on_key_d3(void) { pick_disease(3); }
static void on_key_d4(void) { pick_disease(4); }
static void on_key_d5(void) { pick_disease(5); }
static void on_key_d6(void) { pick_disease(6); }
static void on_key_d7(void) { pick_disease(7); }
static void on_key_d8(void) { pick_disease(8); }
static void on_key_d9(void) { pick_disease(9); }
static void on_key_d10(void) { pick_disease(10); }

static void on_key_continue(void)
{
    if (g.ui && g.ui->screen == SCR_PICK && assignments_ready())
        go_run_screen();
}

static void on_key_clear(void)
{
    if (g.ui && g.ui->screen == SCR_PICK)
        on_clear_click(NULL, 0, 0, 1);
}

static void build_pick_ui(ui_t *ui)
{
    int left = 2;
    int top = 1;
    int i;

    ui->screen = SCR_PICK;

    ui_add(left, top, 28, 3, SCR_PICK, NULL, 0, draw_pick_title, NULL, NULL, NULL,
           NULL, ui);
    ui_add(left + 30, top + 1, 40, 2, SCR_PICK, &g.tick, 0, draw_pick_prompt, NULL,
           NULL, NULL, NULL, ui);
    ui_add(left + 30, top + 4, 40, 1, SCR_PICK, &g.tick, 0, draw_pick_summary, NULL,
           NULL, NULL, NULL, ui);

    for (i = 0; i < SWEEP_DISEASE_COUNT; i++) {
        int id = i + 1;
        int row = top + 4 + i;
        ui_add(left, row, 8, 1, SCR_PICK, &g.tick, 0, draw_disease_btn,
               on_disease_click, NULL, (void *)(intptr_t)id, NULL, ui);
    }

    ui_add(left + 30, top + 7, 20, 3, SCR_PICK, &g.tick, 0, draw_confirm_btn,
           on_confirm_click, NULL, NULL, NULL, ui);
    ui_add(left + 52, top + 7, 11, 3, SCR_PICK, &g.tick, 0, draw_clear_btn,
           on_clear_click, NULL, NULL, NULL, ui);
    ui_add(left, top + 15, 60, 1, SCR_PICK, NULL, 0, draw_pick_help, NULL, NULL,
           NULL, NULL, ui);

    ui_key("q", on_quit, ui);
    ui_key("Q", on_quit, ui);
    ui_key("c", on_key_continue, ui);
    ui_key("C", on_key_continue, ui);
    ui_key("x", on_key_clear, ui);
    ui_key("X", on_key_clear, ui);
    ui_key("1", on_key_d1, ui);
    ui_key("2", on_key_d2, ui);
    ui_key("3", on_key_d3, ui);
    ui_key("4", on_key_d4, ui);
    ui_key("5", on_key_d5, ui);
    ui_key("6", on_key_d6, ui);
    ui_key("7", on_key_d7, ui);
    ui_key("8", on_key_d8, ui);
    ui_key("9", on_key_d9, ui);
    ui_key("0", on_key_d10, ui);
}

/* ── Run screen (SCR_RUN) ──────────────────────────────────────────────── */

static void draw_title(ui_box_t *b, char *out)
{
    char top[256], bot[256], mid[128];
    int inner = g.panel_w - 2;
    int i;
    (void)b;
    if (inner < 10)
        inner = 10;
    top[0] = '\0';
    bot[0] = '\0';
    for (i = 0; i < inner; i++) {
        strcat(top, "═");
        strcat(bot, "═");
    }
    snprintf(mid, sizeof(mid), "%-*.*s", inner, inner, "  Prism Light Scan");
    snprintf(out, MAXCACHESIZE,
             "\x1b[1;36m╔%s╗\x1b[0m\n"
             "\x1b[1;36m║\x1b[0m\x1b[1;37m%s\x1b[0m\x1b[1;36m║\x1b[0m\n"
             "\x1b[1;36m╚%s╝\x1b[0m",
             top, mid, bot);
}

static void draw_start_btn(ui_box_t *b, char *out)
{
    int active = (g.state != ST_RUNNING) && assignments_ready();
    (void)b;
    if (active)
        snprintf(out, MAXCACHESIZE,
                 "\x1b[42;1;30m                \x1b[0m\n"
                 "\x1b[42;1;30m   >  START     \x1b[0m\n"
                 "\x1b[42;1;30m                \x1b[0m");
    else
        snprintf(out, MAXCACHESIZE,
                 "\x1b[100;37m                \x1b[0m\n"
                 "\x1b[100;37m   >  START     \x1b[0m\n"
                 "\x1b[100;37m                \x1b[0m");
}

static void draw_reset_btn(ui_box_t *b, char *out)
{
    int active = (g.state != ST_RUNNING);
    (void)b;
    if (active)
        snprintf(out, MAXCACHESIZE,
                 "\x1b[43;1;30m                \x1b[0m\n"
                 "\x1b[43;1;30m   <  RESET     \x1b[0m\n"
                 "\x1b[43;1;30m                \x1b[0m");
    else
        snprintf(out, MAXCACHESIZE,
                 "\x1b[100;37m                \x1b[0m\n"
                 "\x1b[100;37m   <  RESET     \x1b[0m\n"
                 "\x1b[100;37m                \x1b[0m");
}

static void draw_status(ui_box_t *b, char *out)
{
    const char *color = "\x1b[37m";
    char colored[256];
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
    snprintf(out, MAXCACHESIZE,
             "\x1b[90mStart\x1b[0m  begin (S)   \x1b[90mReset\x1b[0m  park (R)   "
             "\x1b[90mB\x1b[0m  change diseases   \x1b[1;37mQ\x1b[0m  quit");
}

static void draw_range(ui_box_t *b, char *out)
{
    char colored[256];
    (void)b;
    snprintf(colored, sizeof(colored),
             "\x1b[90mScan range: steps %d → %d   (park %d)\x1b[0m",
             g.hooks.scan_start, g.hooks.scan_stop, g.hooks.scan_prep);
    ui_wrap_text(colored, g.panel_w, out, MAXCACHESIZE);
}

static void draw_assign_footer(ui_box_t *b, char *out)
{
    char a[8], c[8];
    (void)b;
    disease_label(g.assign->blob1, a, sizeof(a));
    disease_label(g.assign->blob2, c, sizeof(c));
    snprintf(out, MAXCACHESIZE,
             "\x1b[1;36m┌──────────────────────────────────────────┐\x1b[0m\n"
             "\x1b[1;36m│\x1b[0m \x1b[1;37mBlob 1 →\x1b[0m \x1b[1;32m%-4s\x1b[0m"
             "     \x1b[1;37mBlob 2 →\x1b[0m \x1b[1;35m%-4s\x1b[0m"
             "          \x1b[1;36m│\x1b[0m\n"
             "\x1b[1;36m└──────────────────────────────────────────┘\x1b[0m",
             a, c);
}

void sweep_tui_set_running(const char *status, const char *detail)
{
    set_status(ST_RUNNING, status, detail);
    bump();
}

void sweep_tui_set_ready(const char *status, const char *detail)
{
    set_status(ST_READY, status, detail);
    bump();
}

void sweep_tui_set_done(const char *status, const char *detail)
{
    g.progress = 100;
    set_status(ST_DONE, status, detail);
    bump();
}

void sweep_tui_set_error(const char *status, const char *detail)
{
    set_status(ST_ERROR, status, detail);
    bump();
}

void sweep_tui_set_progress(int percent, const char *detail)
{
    g.progress = percent;
    if (detail)
        snprintf(g.detail, sizeof(g.detail), "%s", detail);
    bump();
}

int sweep_tui_is_running(void)
{
    return g.state == ST_RUNNING;
}

static void on_start(ui_box_t *b, int x, int y, int down)
{
    (void)b;
    (void)x;
    (void)y;
    if (!down || !g.ui || g.ui->screen != SCR_RUN || g.state == ST_RUNNING ||
        !assignments_ready())
        return;
    if (g.hooks.on_start)
        (void)g.hooks.on_start(g.hooks.userdata);
}

static void on_reset(ui_box_t *b, int x, int y, int down)
{
    (void)b;
    (void)x;
    (void)y;
    if (!down || !g.ui || g.ui->screen != SCR_RUN || g.state == ST_RUNNING)
        return;
    if (g.hooks.on_reset)
        (void)g.hooks.on_reset(g.hooks.userdata);
}

static void on_key_start(void)
{
    on_start(NULL, 0, 0, 1);
}

static void on_key_reset(void)
{
    on_reset(NULL, 0, 0, 1);
}

static void on_key_back(void)
{
    if (!g.ui || g.ui->screen != SCR_RUN || g.state == ST_RUNNING)
        return;
    ui_screen(SCR_PICK, g.ui);
    g.phase = g.assign->blob2 ? PICK_BLOB2 : PICK_BLOB1;
    set_status(ST_READY, "Change diseases.", "Edit picks, then Continue.");
    bump();
}

static void build_run_ui(ui_t *ui)
{
    int pw = g.panel_w;
    int cx;
    int cy;
    int btn_gap;

    ui->screen = SCR_RUN;
    cx = ui_center_x(pw, ui);
    cy = 1;
    if (cx < 1)
        cx = 1;
    btn_gap = (pw >= 40) ? (pw - 32) / 3 : 2;
    if (btn_gap < 2)
        btn_gap = 2;

    ui_add(cx, cy, pw, 3, SCR_RUN, &g.tick, 0, draw_title, NULL, NULL, NULL, NULL,
           ui);
    ui_add(cx + btn_gap, cy + 4, 16, 3, SCR_RUN, &g.tick, 0, draw_start_btn, on_start,
           NULL, NULL, NULL, ui);
    ui_add(cx + btn_gap + 16 + btn_gap, cy + 4, 16, 3, SCR_RUN, &g.tick, 0,
           draw_reset_btn, on_reset, NULL, NULL, NULL, ui);

    ui_add(cx, cy + 8, pw, 2, SCR_RUN, &g.tick, 0, draw_status, NULL, NULL, NULL, NULL,
           ui);
    ui_add(cx, cy + 10, pw, 2, SCR_RUN, &g.tick, 0, draw_detail, NULL, NULL, NULL,
           NULL, ui);
    ui_add(cx, cy + 12, pw, 1, SCR_RUN, &g.tick, 0, draw_progress, NULL, NULL, NULL,
           NULL, ui);
    ui_add(cx, cy + 14, pw, 1, SCR_RUN, NULL, 0, draw_help, NULL, NULL, NULL, NULL, ui);
    ui_add(cx, cy + 15, pw, 1, SCR_RUN, NULL, 0, draw_range, NULL, NULL, NULL, NULL, ui);
    ui_add(cx, cy + 17, 44, 3, SCR_RUN, &g.tick, 0, draw_assign_footer, NULL, NULL,
           NULL, NULL, ui);

    ui_key("q", on_quit, ui);
    ui_key("Q", on_quit, ui);
    ui_key("s", on_key_start, ui);
    ui_key("S", on_key_start, ui);
    ui_key("r", on_key_reset, ui);
    ui_key("R", on_key_reset, ui);
    ui_key("b", on_key_back, ui);
    ui_key("B", on_key_back, ui);
}

static void build_all(ui_t *ui)
{
    int saved = ui->screen;

    ui_reset(ui);
    ui->canscroll = 0;
    g.panel_w = panel_width(ui);

    /* Boxes store u->screen at add time — build each screen in turn. */
    build_pick_ui(ui);
    build_run_ui(ui);

    ui->screen = saved;
    /* Re-bind keys for active screen: ui_key stacks globally, both sets remain. */
}

int sweep_tui_run(disease_assign_t *assign, const sweep_tui_hooks_t *hooks)
{
    ui_t ui;
    int last_cols;
    int last_rows;

    memset(&g, 0, sizeof(g));
    g.assign = assign;
    if (hooks)
        g.hooks = *hooks;
    g.phase = PICK_BLOB1;
    assign->blob1 = 0;
    assign->blob2 = 0;

    ui_new(SCR_PICK, &ui);
    ui.canscroll = 0;
    g.ui = &ui;
    g.panel_w = panel_width(&ui);
    last_cols = ui.ws.ws_col;
    last_rows = ui.ws.ws_row;

    set_status(ST_READY, "Pick diseases first.", "Choose D1–D10 for Blob 1.");
    build_all(&ui);
    ui.screen = SCR_PICK;
    ui_draw(&ui);

    {
        char buf[64];
        int n;
        while (!g.quit && (n = read(STDIN_FILENO, buf, sizeof(buf))) > 0) {
            ui_refresh_size(&ui);
            if (ui.ws.ws_col != last_cols || ui.ws.ws_row != last_rows) {
                int keep = ui.screen;
                last_cols = ui.ws.ws_col;
                last_rows = ui.ws.ws_row;
                build_all(&ui);
                ui.screen = keep;
                ui_draw(&ui);
            }
            _ui_update(buf, n, &ui);
        }
    }

    g.ui = NULL;
    ui_free(&ui);
    return (g.state == ST_ERROR) ? 1 : 0;
}
