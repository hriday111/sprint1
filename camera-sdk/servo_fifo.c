/**
 * @file servo_fifo.c
 * @brief Named-FIFO client for servo_calib (move / getpos).
 */

#include "servo_fifo.h"
#include "utils.h"
#include "logger.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int g_cmd_fd = -1;

int servo_fifo_open(void)
{
    if (g_cmd_fd >= 0)
        return 0;
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

int servo_fifo_send(long cmd)
{
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "%ld\n", cmd);

    for (int attempt = 0; attempt < 20; attempt++) {
        if (servo_fifo_open() != 0)
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

static int position_once(int timeout_ms)
{
    int fd = open(POS_PIPE, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        if (errno != ENXIO)
            LOG_ERROR("%s: %s", POS_PIPE, strerror(errno));
        return -1;
    }
    if (servo_fifo_send(SERVO_CMD_GETPOS) != 0) {
        close(fd);
        return -1;
    }

    char buf[64];
    size_t len = 0;
    while (memchr(buf, '\n', len) == NULL) {
        struct pollfd pfd = {.fd = fd, .events = POLLIN};
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

int servo_fifo_position(void)
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

int servo_fifo_move_and_wait(int pos)
{
    LOG_DEBUG("servo_fifo_move_and_wait(%d)", pos);
    if (servo_fifo_send(pos) != 0)
        return -1;
    for (int spins = 0; spins < 6000; spins++) {
        int cur = servo_fifo_position();
        if (cur < 0)
            return -1;
        if (cur == pos)
            return 0;
        usleep(20000);
    }
    LOG_ERROR("servo_fifo_move_and_wait(%d): timed out", pos);
    return -1;
}

void servo_fifo_close(void)
{
    if (g_cmd_fd >= 0) {
        close(g_cmd_fd);
        g_cmd_fd = -1;
        LOG_INFO("Closed command FIFO (daemon left running)");
    }
}

void servo_fifo_shutdown(void)
{
    if (g_cmd_fd >= 0) {
        LOG_INFO("Shutting down servo_calib via -1");
        (void)servo_fifo_send(-1);
        close(g_cmd_fd);
        g_cmd_fd = -1;
    }
}
