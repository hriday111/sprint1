#ifndef UTILS_H
#define UTILS_H

/**
 * @file utils.h
 * @brief Shared macros and output paths for camera-sdk tools.
 */

#include <time.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <ctype.h>

#include "logger.h"

/**
 * @brief Log + perror + file:line and terminate the process.
 * @param source Label passed to perror.
 */
#define ERR(source)                                                            \
  do {                                                                         \
    int _err_errno = errno;                                                    \
    perror(source);                                                            \
    LOG_FATAL("%s: %s", (source), strerror(_err_errno));                       \
    logger_flush();                                                            \
    exit(EXIT_FAILURE);                                                        \
  } while (0)

/** Runtime directory for named FIFOs (shared with servo_calib). */
#define BASE "/run/sprint1"

/** Command FIFO: clients write, servo_calib reads. */
#define CMD_PIPE "/run/sprint1/data_pipe"

/** Position FIFO: servo_calib writes after CMD_GETPOS. */
#define POS_PIPE "/run/sprint1/pos_pipe"

/** Scan window file: three integers `start stop prep`. */
#define PRM_FILE "/var/lib/sprint1/saved.prm"

/** Overlay PNG written by @c calibrate (green ellipses on the crop). */
#define DETECTED_OUTPUT "./tmp/detected.png"

/** Raw grayscale crop written by @c calibrate before detection. */
#define GRAB_OUTPUT "./tmp/calib_frame.png"

/** By how much to decrease the oval size in rois */
#define ERROR 0.3
/**
 * @brief Sleep for @p milli milliseconds.
 *
 * @note Declared for API symmetry with the root utils; implemented in utils.c.
 */
void ms_sleep(unsigned int milli);

#endif
