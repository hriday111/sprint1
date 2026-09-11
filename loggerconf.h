#ifndef LOGGERCONF_H
#define LOGGERCONF_H

/**
 * @file loggerconf.h
 * @brief Optional key=value configuration loader for the logger.
 *
 * Sprint1 tools normally call @c sprint1_logging_init instead; this API remains
 * for custom setups.
 */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @brief Configure the logger from a text file of @c key=value lines.
 *
 * Supported keys:
 * | Key | Value |
 * |:----|:------|
 * | level | TRACE, DEBUG, INFO, WARN, ERROR, or FATAL |
 * | autoFlush | Flush interval in ms (≤ 0 disables) |
 * | logger | console or file (may appear more than once) |
 * | logger.console.output | stdout or stderr |
 * | logger.file.filename | Output path (max 255 bytes) |
 * | logger.file.maxFileSize | Bytes per file (default 1 MiB if ≤ 0) |
 * | logger.file.maxBackupFiles | 0–255 |
 *
 * Lines may contain @c # comments. Blank lines are ignored.
 *
 * @param[in] filename Path to the configuration file (must not be NULL).
 * @return Non-zero on success, 0 on error.
 */
int logger_configure(const char* filename);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* LOGGERCONF_H */
