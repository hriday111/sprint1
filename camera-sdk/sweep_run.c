/**
 * @file sweep_run.c
 * @brief Step-by-step intensity sweep over the scan window in @c saved.prm.
 *
 * For each step from @c start to @c stop inclusive: move via FIFO, grab a crop,
 * average each ROI, append one CSV row. Requires @c servo_calib already running.
 *
 * Usage: @c ./sweep_run [rois.json] [exposure_ms] [out.csv]
 *
 * @warning Closing the command FIFO sends @c -1, which exits @c servo_calib.
 */

#define _DEFAULT_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cam.h"
#include "roi.h"
#include "logger.h"
#include "crash.h"

#define CMD_PIPE "/run/sprint1/data_pipe"
#define POS_PIPE "/run/sprint1/pos_pipe"
#define PRM_FILE "/var/lib/sprint1/saved.prm"
#define CMD_GETPOS 4000
#define DEG_PER_STEP 0.1125

/**
 * Held open for the whole scan so @c servo_calib never sees EOF mid-sweep.
 * Open/close per command races with its reopen and can cause EPIPE.
 */
static int g_cmd_fd = -1;

/**
 * @brief Open the command FIFO for writing, retrying while the reader is absent.
 * @return 0 on success, -1 on failure.
 */
static int fifo_open_cmd(void)
{
    if (g_cmd_fd >= 0)
        return 0;
    /* Without this, EPIPE also delivers SIGPIPE and kills the process. */
    signal(SIGPIPE, SIG_IGN);
    for (int attempt = 0; attempt < 50; attempt++) {
        g_cmd_fd = open(CMD_PIPE, O_WRONLY);
        if (g_cmd_fd >= 0) {
            LOG_INFO("Opened command FIFO %s", CMD_PIPE);
            return 0;
        }
        if (errno != EINTR && errno != ENXIO) {
            LOG_ERROR("%s: %s", CMD_PIPE, strerror(errno));
            return -1;
        }
        usleep(50000);
    }
    LOG_ERROR("%s: no reader (is servo_calib running?)", CMD_PIPE);
    return -1;
}

/**
 * @brief Write one integer command line; reopen on transient EPIPE.
 * @param cmd Protocol integer (absolute position or control code).
 * @return 0 on success, -1 on failure.
 */
static int fifo_send(long cmd)
{
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "%ld\n", cmd);

    for (int attempt = 0; attempt < 20; attempt++) {
        if (fifo_open_cmd() != 0)
            return -1;
        ssize_t w = write(g_cmd_fd, buf, (size_t)n);
        if (w == (ssize_t)n) {
            LOG_DEBUG("FIFO send %ld", cmd);
            return 0;
        }
        if (w < 0 && (errno == EPIPE || errno == EINTR || errno == EAGAIN)) {
            close(g_cmd_fd);
            g_cmd_fd = -1;
            usleep(20000);
            continue;
        }
        LOG_ERROR("write cmd: %s", strerror(errno));
        return -1;
    }
    LOG_ERROR("write cmd: giving up after retries (cmd=%ld)", cmd);
    return -1;
}

/**
 * @brief Send exit command (-1) and close the command FIFO.
 */
static void fifo_close_cmd(void)
{
    if (g_cmd_fd >= 0) {
        LOG_INFO("Closing command FIFO (sending -1)");
        if (fifo_send(-1) != 0)
            exit(-1);
        close(g_cmd_fd);
        g_cmd_fd = -1;
    }
}

/**
 * @brief Request position once: open @c POS_PIPE, send CMD_GETPOS, poll for a line.
 * @param timeout_ms Poll timeout per wait.
 * @return Parsed position, or -1 on failure/timeout.
 */
static int fifo_position_once(int timeout_ms)
{
    int fd = open(POS_PIPE, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        if (errno != ENXIO)
            LOG_ERROR("%s: %s", POS_PIPE, strerror(errno));
        return -1;
    }
    if (fifo_send(CMD_GETPOS) != 0) {
        close(fd);
        return -1;
    }

    char buf[64] ={};
    size_t len = 0;
    while (memchr(buf, '\n', len) == NULL) {
        struct pollfd pfd = { .fd = fd, .events = POLLIN };
        int pr = poll(&pfd, 1, timeout_ms);
        if (pr <= 0) {
            close(fd);
            return -1;
        }
        ssize_t n = read(fd, buf + len, sizeof(buf) - 1 - len);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                usleep(5000);
                continue;
            }
            close(fd);
            return -1;
        }
        if (n == 0) {
            usleep(5000);
            continue;
        }
        len += (size_t)n;
        if (len >= sizeof(buf) - 1) {
            close(fd);
            return -1;
        }
    }
    close(fd);
    buf[len] = '\0';
    return atoi(buf);
}

/**
 * @brief Read position with retries (replies can be dropped if not listening yet).
 * @return Position >= 0, or -1 after exhausting retries.
 */
static int fifo_position(void)
{
    for (int attempt = 0; attempt < 10; attempt++) {
        int cur = fifo_position_once(500);
        if (cur >= 0)
            return cur;
        usleep(20000);
    }
    LOG_ERROR("position: no reply after retries");
    return -1;
}

/**
 * @brief Command an absolute move and poll until reported position matches.
 * @param pos Target step.
 * @return 0 on arrival, -1 on communication failure or timeout (~2 min).
 */
static int move_and_wait(int pos)
{
    LOG_DEBUG("move_and_wait(%d)", pos);
    if (fifo_send(pos) != 0)
        return -1;
    for (int spins = 0; spins < 6000; spins++) {
        int cur = fifo_position();
        if (cur < 0)
            return -1;
        if (cur == pos)
            return 0;
        usleep(20000);
    }
    LOG_ERROR("move_and_wait(%d): timed out", pos);
    return -1;
}

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
 * @brief Run the intensity sweep and write CSV.
 * @return 0 on success, non-zero on failure.
 */
int main(int argc, char **argv)
{
    const char *roi_path = argc > 1 ? argv[1] : ROI_PATH;
    double exposure = argc > 2 ? strtod(argv[2], NULL) : 0.1;
    const char *csv_path = argc > 3 ? argv[3] : "../sensor_data.csv";

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

    if (fifo_open_cmd() != 0) {
        cam_close(&cam);
        return 1;
    }

    FILE *csv = fopen(csv_path, "w");
    if (!csv) {
        LOG_ERROR("%s: %s", csv_path, strerror(errno));
        fifo_close_cmd();
        cam_close(&cam);
        return 1;
    }
    fprintf(csv, "Angle_deg");
    for (int i = 0; i < rois.n; i++)
        fprintf(csv, ",%s", rois.item[i].name);
    fprintf(csv, "\n");
    fflush(csv);

    if (move_and_wait(start) != 0) {
        LOG_ERROR("move_and_wait(%d) failed — is servo_calib running?", start);
        fclose(csv);
        fifo_close_cmd();
        cam_close(&cam);
        return 1;
    }

    unsigned char *pix;
    int w, h, pitch;
    if (cam_grab(&cam, &pix, &w, &h, &pitch) != 0) {
        fclose(csv);
        fifo_close_cmd();
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
        if (move_and_wait(pos) != 0) {
            LOG_ERROR("move_and_wait(%d) failed", pos);
            fclose(csv);
            fifo_close_cmd();
            cam_close(&cam);
            return 1;
        }
        if (cam_grab(&cam, &pix, &w, &h, &pitch) != 0) {
            fclose(csv);
            fifo_close_cmd();
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
    fifo_close_cmd();
    cam_close(&cam);
    LOG_INFO("wrote %s", csv_path);
    logger_flush();
    logger_exitFileLogger();
    return 0;
}
