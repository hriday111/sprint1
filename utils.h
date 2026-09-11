#ifndef UTILS_H
#define UTILS_H

/**
 * @file utils.h
 * @brief Shared helpers and runtime paths for the servo daemon.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <time.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <ctype.h>

/**
 * @brief Print perror + file:line and terminate the process.
 * @param source Label passed to perror (often a syscall or context string).
 */
#define ERR(source)                                                            \
  (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__),             \
   exit(EXIT_FAILURE))

/** Runtime directory for named FIFOs. */
#define BASE "/run/sprint1"

/** Client writes newline-terminated commands here (servo reads). */
#define CMD_PIPE "/run/sprint1/data_pipe"

/** Servo writes current position here when commanded with CMD_GETPOS. */
#define POS_PIPE "/run/sprint1/pos_pipe"

/** Scan window file: three integers `start stop prep`. */
#define PRM_FILE "/var/lib/sprint1/saved.prm"

/**
 * @brief Sleep for @p milli milliseconds using nanosleep.
 */
void ms_sleep(unsigned int milli);

#endif
