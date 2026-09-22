#ifndef SWEEP_TUI_H
#define SWEEP_TUI_H

/**
 * @file sweep_tui.h
 * @brief tuibox UI for disease assignment + Start/Reset sweep controls.
 *
 * Screen 0: pick D1–D10 for blob 1, then blob 2 (left list).
 * Screen 1: Start / Reset / progress; footer shows Blob → disease.
 *
 * Note: do not include tuibox.h here — it is a header-only library and must
 * be compiled into exactly one translation unit (@c sweep_tui.c).
 */

#include <stddef.h>

#define SWEEP_DISEASE_COUNT 10

/** Disease ids are 1..SWEEP_DISEASE_COUNT; 0 means unset. */
typedef struct {
    int blob1; /**< Disease for blob 1. */
    int blob2; /**< Disease for blob 2. */
} disease_assign_t;

/**
 * @brief Callbacks from the TUI into hardware / sweep logic.
 */
typedef struct {
    int (*on_start)(void *userdata);
    int (*on_reset)(void *userdata);
    void *userdata;
    int scan_start;
    int scan_stop;
    int scan_prep;
} sweep_tui_hooks_t;

void disease_label(int id, char *buf, size_t buflen);

/** Status helpers for use inside @c on_start / @c on_reset callbacks. */
void sweep_tui_set_running(const char *status, const char *detail);
void sweep_tui_set_ready(const char *status, const char *detail);
void sweep_tui_set_done(const char *status, const char *detail);
void sweep_tui_set_error(const char *status, const char *detail);
void sweep_tui_set_progress(int percent, const char *detail);
int sweep_tui_is_running(void);

/**
 * @brief Run the interactive TUI until Quit.
 * @return 0 on normal quit, 1 if the last operation left an error state.
 */
int sweep_tui_run(disease_assign_t *assign, const sweep_tui_hooks_t *hooks);

#endif
