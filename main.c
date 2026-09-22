/**
 * @file main.c
 * @brief @c servo_calib — stepper/laser driver for the prism scan rig.
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

#include "stepper.h"
#include "utils.h"
#include "commands.h"
#include "logger.h"
#include "crash.h"

/**
 * @brief Load @c start, @c stop, and @c prep from a parameter file.
 *
 * Expects three integers in 0–999. The read buffer is 32 bytes; longer lines
 * are truncated before parsing.
 * @param filename Path to a file containing three integers.
 * @param stepper Destination for the three fields.
 */
static void loadParams(char *filename, stepper_t *stepper)
{
    int fd = open(filename, O_RDONLY);
    char buffer[32];
    int count;
    int n;

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
    n = sscanf(buffer, "%d %d %d", &stepper->start, &stepper->stop, &stepper->prep);
    if (n != 3) {
        LOG_FATAL("loadParams: expected 3 integers, got %d", n);
        exit(EXIT_FAILURE);
    }

    /*
     * Guard against nonsense / truncated reads. Keep in production until the
     * loader is hardened (longer buffer, stricter parsing).
     */
    if (stepper->start < 0 || stepper->stop < 0 || stepper->prep < 0 ||
        stepper->start > 999 || stepper->stop > 999 || stepper->prep > 999) {
        LOG_FATAL("loadParams: unsupported values start=%d stop=%d prep=%d",
                  stepper->start, stepper->stop, stepper->prep);
        exit(EXIT_FAILURE);
    }
    LOG_INFO("Params: start=%d stop=%d prep=%d", stepper->start, stepper->stop,
             stepper->prep);
}

/**
 * @brief Ensure @c BASE exists and create a FIFO at @p path with mode 0666.
 *
 * The daemon typically runs as root; chmod keeps camera-sdk clients usable
 * without elevated privileges.
 * @param path Absolute FIFO path.
 */
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

/**
 * @brief Publish @p pos on @c POS_PIPE if a reader is present.
 *
 * Opens non-blocking so a missing reader does not stall the command loop.
 * @param pos Step position to write as ASCII plus newline.
 */
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
 * Supported forms:
 * - @c bN / @c BN — laser brightness 0..255
 * - integer — protocol command or absolute goto (see commands.h)
 * - @c -1 — request daemon exit
 *
 * @param line Mutable NUL-terminated line (no trailing newline required).
 * @param stepper Driver state.
 * @return 1 if the process should exit, 0 otherwise.
 */
static int handle_command(char *line, stepper_t *stepper)
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
        stepper_setBrightness(stepper, (int)value);
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
    if (value == CMD_LOADFILE) {
        LOG_INFO("Reloading params from %s", PRM_FILE);
        loadParams(PRM_FILE, stepper);
    } else if (value == CMD_GETPOS) {
        int pos = stepper_getPos(stepper);
        LOG_INFO("Current position: %d", pos);
        write_position(pos);
    } else {
        stepperSend((int)value, stepper);
        LOG_INFO("Command %ld sent (position %d)", value, stepper_getPos(stepper));
    }
    return 0;
}

/**
 * @brief Create FIFOs and serve newline-delimited commands until exit.
 *
 * @c open(O_RDONLY) blocks until a writer connects. A zero-length @c read means
 * all writers closed; the loop reopens and waits. Partial lines are buffered
 * across reads so split writes still parse correctly.
 *
 * @param path Command FIFO path (typically @c CMD_PIPE).
 * @param stepper Driver state.
 */
static void serve_fifo(const char *path, stepper_t *stepper)
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
                if (handle_command(start, stepper)) {
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

/**
 * @brief Boot logging, GPIO, home, load params, park at prep, then serve commands.
 * @param argc Extra argv entries are ignored (warned); no CLI options.
 * @param argv Unused beyond program name.
 * @return EXIT_SUCCESS, or EXIT_FAILURE on logger/open/homing failure.
 */
int main(int argc, char **argv)
{
    stepper_t stepper;
    int homed;

    if (sprint1_logging_init("servo_calib", NULL, 0) != 0)
        return EXIT_FAILURE;

    if (argc != 1)
        LOG_WARN("Invalid usage. Additional arguments are not supported. Being ignored.");

    memset(&stepper, 0, sizeof(stepper));
    stepper.fd = -1;
    stepper.zero = 400;

    LOG_INFO("Opening GPIO stepper driver (WiringPi)...");
    if (!stepperOpen(NULL, &stepper)) {
        LOG_FATAL("Failed to open stepper");
        logger_flush();
        return EXIT_FAILURE;
    }

    ms_sleep(100);
    LOG_INFO("Homing (drive toward endstop)...");
    homed = stepperInit(&stepper);
        if (homed < 0) {
        LOG_FATAL("Homing failed");
        logger_flush();
        stepperClose(&stepper);
        return EXIT_FAILURE;
    }
    LOG_INFO("Homed position: %d (angle %.2f deg)", homed,
             stepper_prismAngle(&stepper));
    loadParams(PRM_FILE, &stepper);
    stepper_setBrightness(&stepper, 255);
    stepperSend(stepper.prep, &stepper);
    LOG_INFO("Parked at prep=%d", stepper.prep);

    serve_fifo(CMD_PIPE, &stepper);

    stepperClose(&stepper);
    LOG_INFO("Exiting");
    logger_flush();
    logger_exitFileLogger();
    return 0;
}
