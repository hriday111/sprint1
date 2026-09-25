#include "ipc_client.h"
#include "utils.h"
#include "logger.h"
#include "commands.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int g_cmd_fd = -1;
static int g_pos_fd = -1;
static int g_motor_enabled = 0;

/**
 * @brief Write one already-formatted command line with open/retry on EPIPE.
 */
static int fifo_write_line(const char *buf, int n)
{
    for (int attempt = 0; attempt < 20; attempt++) {
        if (fifo_open() != 0)
            return -1;
        ssize_t w = write(g_cmd_fd, buf, (size_t)n);
        if (w == (ssize_t)n) {
            LOG_DEBUG("FIFO write %.*s", n - (n > 0 && buf[n - 1] == '\n'), buf);
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
    LOG_ERROR("write cmd: giving up after retries");
    return -1;
}

int fifo_open(void)
{
    if (g_cmd_fd >= 0)
        return 0;
    signal(SIGPIPE, SIG_IGN);
    for (int attempt = 0; attempt < 50; attempt++) {
        g_cmd_fd = open(CMD_PIPE, O_WRONLY);
        if (g_cmd_fd >= 0) {
            LOG_INFO("Opened command FIFO %s", CMD_PIPE);
            break;
        }
        if (errno != EINTR && errno != ENXIO) {
            LOG_ERROR("%s: %s", CMD_PIPE, strerror(errno));
            return -1;
        }
        usleep(50000);
    }
    if (g_cmd_fd < 0) {
        LOG_ERROR("%s: no reader (is servo_calib running?)", CMD_PIPE);
        return -1;
    }
    if (g_pos_fd < 0) {
        for (int attempt = 0; attempt < 50; attempt++) {
            g_pos_fd = open(POS_PIPE, O_RDONLY | O_NONBLOCK);
            if (g_pos_fd >= 0) break;
            if (errno != EINTR && errno != ENXIO && errno != EBADF) {
                LOG_ERROR("%s: %s", POS_PIPE, strerror(errno));
                close(g_cmd_fd);
                g_cmd_fd = -1;
                return -1;
            }
            usleep(50000);
        }
        if (g_pos_fd < 0) {
            LOG_ERROR("%s: no reader", POS_PIPE);
            close(g_cmd_fd);
            g_cmd_fd = -1;
            return -1;
        }
    }
    return 0;
}
int fifo_send(long cmd)
{
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "%ld\n", cmd);
    return fifo_write_line(buf, n);
}

static int position_once(int timeout_ms)
{
    if (g_pos_fd < 0) {
        if (fifo_open() != 0)
            return -1;
    }
    char junk[256];
    for (;;) {
        ssize_t n = read(g_pos_fd, junk, sizeof(junk));
        if (n <= 0) break;
    }
    if (fifo_send(CMD_GETPOS) != 0) {
        return -1;
    }

    char buf[64]={};
    size_t len = 0;
    while (memchr(buf, '\n', len) == NULL) {
        struct pollfd pfd = {.fd = g_pos_fd, .events = POLLIN};
        int pr = poll(&pfd, 1, timeout_ms);
        if (pr <= 0) {
            return -1;
        }
        ssize_t n = read(g_pos_fd, buf + len, sizeof(buf) - 1 - len);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                usleep(5000);
                continue;
            }
            return -1;
        }
        if (n == 0) {
            usleep(5000);
            continue;
        }
        len += (size_t)n;
        if (len >= sizeof(buf) - 1) {
            return -1;
        }
    }
    buf[len] = '\0';
    return atoi(buf);
}

int fifo_get_position(void)
{
    for (int attempt = 0; attempt < 10; attempt++) {
        int cur = position_once(500);
        if (cur >= 0)
            return cur;
        usleep(20000);
    }
    LOG_ERROR("position: no reply after retries");
    return -1;
}
int fifo_move_and_wait(int pos)
{
    LOG_DEBUG("fifo_move_and_wait(%d)", pos);
    if (fifo_send(pos) != 0)
        return -1;
    for (int spins = 0; spins < 6000; spins++) {
        int cur = fifo_get_position();
        if (cur < 0)
            return -1;
        if (cur == pos)
            return 0;
        usleep(20000);
    }
    LOG_ERROR("fifo_move_and_wait(%d): timed out", pos);
    return -1;
}

int fifo_is_enabled(void)
{
    return g_motor_enabled;
}

int fifo_enable(void)
{
    long cmd = g_motor_enabled ? CMD_DISABLE : CMD_ENABLE;

    if (fifo_send(cmd) != 0)
        return -1;

    g_motor_enabled = !g_motor_enabled;
    LOG_INFO("Motor %s", g_motor_enabled ? "enabled" : "disabled");
    return 0;
}

int fifo_illum_on(void)
{
    if (fifo_send(CMD_LASER_ON) != 0)
        return -1;
    LOG_INFO("Illumination on");
    return 0;
}

int fifo_illum_off(void)
{
    if (fifo_send(CMD_LASER_OFF) != 0)
        return -1;
    LOG_INFO("Illumination off");
    return 0;
}

/**
 * @brief Set illumination brightness.
 *
 * Daemon parses lines starting with @c b / @c B (see main handle_command),
 * e.g. @c "b255\\n". A bare integer would be treated as a motor goto — do not
 * use @c fifo_send for brightness.
 */
int fifo_set_brightness(int brightness)
{
    char buf[32];
    int n;

    if (brightness < 0)
        brightness = 0;
    if (brightness > 255)
        brightness = 255;

    n = snprintf(buf, sizeof(buf), "b%d\n", brightness);
    if (fifo_write_line(buf, n) != 0)
        return -1;

    LOG_INFO("Brightness -> %d", brightness);
    return 0;
}

void fifo_close(void)
{
    if (g_cmd_fd >= 0) {
        close(g_cmd_fd);
        g_cmd_fd = -1;
    }
    if (g_pos_fd >= 0) {
        close(g_pos_fd);
        g_pos_fd = -1;
    }
}

void fifo_shutdown(void)
{
    if (g_cmd_fd >= 0) {
        LOG_INFO("Shutting down calib via -1");
        (void)fifo_send(-1);
    }
    if (g_cmd_fd >= 0) {
        close(g_cmd_fd);
        g_cmd_fd = -1;
    }
    if (g_pos_fd >= 0) {
        close(g_pos_fd);
        g_pos_fd = -1;
    }
}