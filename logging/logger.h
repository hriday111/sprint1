#ifndef LOGGER_H
#define LOGGER_H

/**
 * @file logger.h
 * @brief Thread-safe console/file logger used by @c servo_calib and camera-sdk.
 *
 * Prefer the @c LOG_* macros. Initialize with @c sprint1_logging_init (see crash.h)
 * or call @c logger_initFileLogger / @c logger_initConsoleLogger directly.
 */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <stdio.h>
#include <string.h>

#if defined(_WIN32) || defined(_WIN64)
 #define __FILENAME__ (strrchr(__FILE__, '\\') ? strrchr(__FILE__, '\\') + 1 : __FILE__)
#else
 /** Basename of @c __FILE__ for log lines (POSIX path separator). */
 #define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
#endif /* defined(_WIN32) || defined(_WIN64) */

/** @name Convenience macros (file:line attached automatically) */
/**@{*/
#define LOG_TRACE(fmt, ...) logger_log(LogLevel_TRACE, __FILENAME__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) logger_log(LogLevel_DEBUG, __FILENAME__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  logger_log(LogLevel_INFO , __FILENAME__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  logger_log(LogLevel_WARN , __FILENAME__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) logger_log(LogLevel_ERROR, __FILENAME__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_FATAL(fmt, ...) logger_log(LogLevel_FATAL, __FILENAME__, __LINE__, fmt, ##__VA_ARGS__)
/**@}*/

/**
 * @brief Severity levels; messages below the configured level are discarded.
 */
typedef enum {
    LogLevel_TRACE,
    LogLevel_DEBUG,
    LogLevel_INFO,
    LogLevel_WARN,
    LogLevel_ERROR,
    LogLevel_FATAL,
} LogLevel;

/**
 * @brief Enable console logging.
 * @param[in] output @c stdout, @c stderr, or NULL (defaults to stdout).
 * @return Non-zero on success, 0 on error.
 */
int logger_initConsoleLogger(FILE* output);

/**
 * @brief Close the file logger output stream (if open).
 */
void logger_exitFileLogger(void);

/**
 * @brief Enable rotating file logging.
 * @param[in] filename Output path (required; max 255 chars).
 * @param[in] maxFileSize Max bytes per file before rotation (default 1 MiB if ≤ 0).
 * @param[in] maxBackupFiles Number of rotated backups to keep (0–255).
 * @return Non-zero on success, 0 on error.
 */
int logger_initFileLogger(const char* filename, long maxFileSize, unsigned char maxBackupFiles);

/**
 * @brief Set the minimum severity that will be emitted (default INFO).
 * @param[in] level New threshold.
 */
void logger_setLevel(LogLevel level);

/**
 * @brief Return the current log level threshold.
 */
LogLevel logger_getLevel(void);

/**
 * @brief True if @p level would be written at the current threshold.
 * @param level Severity to test.
 * @return Non-zero if enabled.
 */
int logger_isEnabled(LogLevel level);

/**
 * @brief Periodically fflush sinks while logging.
 * @param[in] interval Flush interval in milliseconds; ≤ 0 disables auto-flush.
 */
void logger_autoFlush(long interval);

/**
 * @brief Flush buffered console/file log output immediately.
 */
void logger_flush(void);

/**
 * @brief Write one log line (prefer @c LOG_* macros).
 *
 * Call @c logger_initConsoleLogger and/or @c logger_initFileLogger first
 * (or @c sprint1_logging_init).
 *
 * @param[in] level Severity.
 * @param[in] file Source file label (usually @c __FILENAME__).
 * @param[in] line Source line number.
 * @param[in] fmt printf-style format string.
 * @param[in] ... Format arguments.
 */
void logger_log(LogLevel level, const char* file, int line, const char* fmt, ...);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* LOGGER_H */
