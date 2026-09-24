/**
 * @file main.c
 * @brief @c servo_calib — motor + illumination driver for the prism scan rig.
 *
 * Start this first for normal operation, then run
 * @c camera-sdk/build/bin/sweep_run for the main intensity measurements.
 * ROI calibration is already done; use @c calibrate2 only after optical changes.
 *
 * Initializes file+console logging under @c logs/, enables core-dump handlers,
 * creates world-writable FIFOs under /run/sprint1/, loads scan parameters from
 * @c PRM_FILE (values 0–999), parks at @c prep, and blocks in @c serve_fifo
 * until command -1.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "motor_api.h"
#include "illumination_api.h"
#include "gpio_pins.h"
#include "utils.h"
#include "commands.h"
#include "logger.h"
#include "crash.h"

/**
 * @brief Load @c start, @c stop, and @c prep from a parameter file.
 */
static void loadParams(char *filename, motor_t *motor)
{
    int fd = open(filename, O_RDONLY);
    char buffer[32];
    int count;
    int n;
    int start, stop, prep;

    if (fd == -1) {
        LOG_ERROR("loadParams: open(%s) failed: %s", filename, strerror(errno));
        ERR("open");
    }

    memset(buffer, 0, sizeof(buffer));
    count = read(fd, buffer, sizeof(buffer) - 1);
    close(fd);

    if (count <= 0) {
        LOG_FATAL("loadParams: empty or unreadable file %s", filename);
        exit(EXIT_FAILURE);
    }

    buffer[count] = '\0';
    n = sscanf(buffer, "%d %d %d", &start, &stop, &prep);
    if (n != 3) {
        LOG_FATAL("loadParams: expected 3 integers, got %d", n);
        exit(EXIT_FAILURE);
    }

    if (start < 0 || stop < 0 || prep < 0 ||
        start > 999 || stop > 999 || prep > 999) {
        LOG_FATAL("loadParams: unsupported values start=%d stop=%d prep=%d",
                  start, stop, prep);
        exit(EXIT_FAILURE);
    }
    motor_api.set_scan_params(motor, start, stop, prep);
    LOG_INFO("Params: start=%d stop=%d prep=%d", start, stop, prep);
}

static void make_fifo(const char *path)
{
    struct stat st = {0};
    if (stat(BASE, &st) == -1) {
        LOG_INFO("Creating runtime dir %s", BASE);
        if (mkdir(BASE, 0666) != 0 && errno != EEXIST)
            ERR("mkdir");
    }
    if (mkfifo(path, 0666) < 0 && errno != EEXIST) {
        LOG_ERROR("mkfifo(%s) failed", path);
        ERR("mkfifo");
    }
    if (chmod(path, 0666) < 0) {
        LOG_ERROR("chmod(%s) failed", path);
        ERR("chmod fifo");
    }
    LOG_DEBUG("FIFO ready: %s", path);
}

static void write_position(int pos)
{
    char buf[16];
    int n = snprintf(buf, sizeof(buf), "%d\n", pos);
    int fd = open(POS_PIPE, O_WRONLY | O_NONBLOCK);

    if (fd < 0) {
        if (errno == ENXIO) {
            LOG_DEBUG("write_position: no reader on %s", POS_PIPE);
            return;
        }
        ERR("open pos fifo");
    }
    if (write(fd, buf, (size_t)n) < 0 && errno != EAGAIN && errno != EPIPE)
        ERR("write pos fifo");
    close(fd);
    LOG_DEBUG("Published position %d", pos);
}

/**
 * @brief Parse and execute one trimmed command line.
 *
 * - @c bN / @c BN — illumination brightness 0..255
 * - @c CMD_LASER_ON / @c CMD_LASER_OFF — illumination on/off
 * - other integers — motor protocol / absolute goto
 * - @c -1 — request daemon exit
 */
static int handle_command(char *line, motor_t *motor, illumination_t *illum)
{
    char *end;
    long value;

    while (isspace((unsigned char)*line))
        line++;
    end = line + strlen(line);
    while (end > line && isspace((unsigned char)end[-1]))
        *--end = '\0';
    if (*line == '\0')
        return 0;

    if (line[0] == 'b' || line[0] == 'B') {
        errno = 0;
        value = strtol(line + 1, &end, 10);
        if (end == line + 1 || *end != '\0' || errno != 0) {
            LOG_WARN("Ignoring bad brightness command: '%s'", line);
            return 0;
        }
        LOG_INFO("Brightness -> %ld", value);
        illumination_api.set_brightness(illum, (int)value);
        return 0;
    }

    errno = 0;
    value = strtol(line, &end, 10);
    if (end == line || *end != '\0' || errno != 0) {
        LOG_WARN("Ignoring bad command: '%s'", line);
        return 0;
    }

    if (value == -1) {
        LOG_INFO("Exit command received");
        return 1;
    }
    if (value == CMD_LASER_ON) {
        illumination_api.on(illum);
        LOG_INFO("Illumination on");
        return 0;
    }
    if (value == CMD_LASER_OFF) {
        illumination_api.off(illum);
        LOG_INFO("Illumination off");
        return 0;
    }
    if (value == CMD_LOADFILE) {
        LOG_INFO("Reloading params from %s", PRM_FILE);
        loadParams(PRM_FILE, motor);
    } else if (value == CMD_GETPOS) {
        int pos = motor_api.get_pos(motor);
        LOG_INFO("Current position: %d", pos);
        write_position(pos);
    } else {
        motor_api.send(motor, (int)value);
        LOG_INFO("Command %ld sent (position %d)", value, motor_api.get_pos(motor));
    }
    return 0;
}

static void serve_fifo(const char *path, motor_t *motor, illumination_t *illum)
{
    char buf[128];
    size_t len = 0;
    int done = 0;

    make_fifo(path);
    make_fifo(POS_PIPE);
    LOG_INFO("Serving commands on %s", path);

    while (!done) {
        int fifo = open(path, O_RDONLY);
        if (fifo < 0) {
            LOG_ERROR("Open fifo failed (%s), retrying", strerror(errno));
            usleep(100000);
            continue;
        }
        LOG_DEBUG("Writer connected on command FIFO");

        for (;;) {
            ssize_t count = read(fifo, buf + len, sizeof(buf) - 1 - len);
            if (count < 0) {
                if (errno == EINTR)
                    continue;
                ERR("read fifo");
            }
            if (count == 0) {
                LOG_DEBUG("All writers closed; reopening FIFO");
                break;
            }

            len += (size_t)count;
            buf[len] = '\0';

            char *start = buf;
            char *nl;
            while ((nl = strchr(start, '\n')) != NULL) {
                *nl = '\0';
                if (handle_command(start, motor, illum)) {
                    done = 1;
                    break;
                }
                start = nl + 1;
            }
            if (done)
                break;

            len = strlen(start);
            memmove(buf, start, len + 1);
            if (len == sizeof(buf) - 1) {
                LOG_WARN("Command buffer full without newline; dropping");
                len = 0;
            }
        }
        close(fifo);
    }
}

int main(int argc, char **argv)
{
    motor_t *motor = NULL;
    illumination_t *illum = NULL;
    int homed;
    int prep;

    if (sprint1_logging_init("servo_calib", NULL, 0) != 0)
        return EXIT_FAILURE;

    if (argc != 1)
        LOG_WARN("Invalid usage. Additional arguments are not supported. Being ignored.");

    LOG_INFO("Opening motor driver...");
    if (motor_api.open(&motor, NULL) != 0) {
        LOG_FATAL("Failed to open motor");
        logger_flush();
        return EXIT_FAILURE;
    }

    LOG_INFO("Opening illumination on pin %d...", PIN_LASER);
    if (illumination_api.open(&illum, PIN_LASER) != 0) {
        LOG_FATAL("Failed to open illumination");
        motor_api.close(motor);
        logger_flush();
        return EXIT_FAILURE;
    }

    ms_sleep(100);
    LOG_INFO("Homing (drive toward endstop)...");
    homed = motor_api.init(motor);
    if (homed < 0) {
        LOG_FATAL("Homing failed");
        logger_flush();
        illumination_api.close(illum);
        motor_api.close(motor);
        return EXIT_FAILURE;
    }
    LOG_INFO("Homed position: %d (angle %.2f deg)", homed,
             motor_api.prism_angle(motor));
    loadParams(PRM_FILE, motor);
    illumination_api.set_brightness(illum, 255);
    illumination_api.on(illum);
    prep = motor_api.get_prep(motor);
    motor_api.send(motor, prep);
    LOG_INFO("Parked at prep=%d", prep);

    serve_fifo(CMD_PIPE, motor, illum);

    illumination_api.close(illum);
    motor_api.close(motor);
    LOG_INFO("Exiting");
    logger_flush();
    logger_exitFileLogger();
    return 0;
}
