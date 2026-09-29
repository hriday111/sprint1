#ifndef CRASH_H
#define CRASH_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

/**
 * @brief Raise @c RLIMIT_CORE and install handlers for fatal signals.
 *
 * On SIGSEGV/SIGABRT/SIGBUS/SIGFPE/SIGILL/SIGQUIT/SIGSYS the handler logs
 * where the core dump is expected, flushes the logger, then re-raises so the
 * kernel writes the dump.
 *
 * Call after the file logger is initialized.
 */
void crash_setup(void);

/**
 * @brief Create @c logs/, open a dated log file, set DEBUG, enable crash dumps.
 *
 * Log path pattern: logs/APP-EPOCH.log (APP from @p app_name, EPOCH = unix time).
 * @param app_name Short name used in the log filename (e.g. @c driver).
 * @param path_out Optional buffer for the chosen log path.
 * @param path_sz Size of @p path_out.
 * @return 0 on success, -1 on failure.
 */
int sprint1_logging_init(const char *app_name, char *path_out, size_t path_sz);

#ifdef __cplusplus
}
#endif

#endif /* CRASH_H */
