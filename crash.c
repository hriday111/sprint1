/**
 * @file crash.c
 * @brief File-logger bootstrap and fatal-signal / core-dump handling.
 */

#include "crash.h"
#include "logger.h"

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static char g_core_hint[512];
static volatile sig_atomic_t g_handling_crash;

/**
 * @brief Read @c /proc/sys/kernel/core_pattern into @p out (NUL-terminated).
 */
static void read_core_pattern(char *out, size_t out_sz)
{
    FILE *f;
    size_t n;

    if (out_sz == 0)
        return;
    out[0] = '\0';
    f = fopen("/proc/sys/kernel/core_pattern", "r");
    if (!f) {
        snprintf(out, out_sz, "core");
        return;
    }
    if (!fgets(out, (int)out_sz, f))
        snprintf(out, out_sz, "core");
    fclose(f);
    n = strlen(out);
    while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r'))
        out[--n] = '\0';
}

/**
 * @brief Expand a few common @c core_pattern placeholders into @p out.
 */
static void expand_core_path(char *out, size_t out_sz, const char *pattern, pid_t pid)
{
    char cwd[PATH_MAX];
    char expanded[PATH_MAX];
    size_t ei = 0;
    const char *p;
    time_t now = time(NULL);

    if (pattern[0] == '|') {
        snprintf(out, out_sz,
                 "piped via core_pattern '%s' (inspect with: coredumpctl info %d)",
                 pattern, (int)pid);
        return;
    }

    if (!getcwd(cwd, sizeof(cwd)))
        snprintf(cwd, sizeof(cwd), ".");

    for (p = pattern; *p && ei + 1 < sizeof(expanded); p++) {
        if (*p == '%' && p[1]) {
            char tmp[64];
            size_t tl;

            p++;
            switch (*p) {
            case 'p':
                snprintf(tmp, sizeof(tmp), "%d", (int)pid);
                break;
            case 'e':
                snprintf(tmp, sizeof(tmp), "%s", program_invocation_short_name);
                break;
            case 't':
                snprintf(tmp, sizeof(tmp), "%ld", (long)now);
                break;
            case 'h': {
                char host[128];
                if (gethostname(host, sizeof(host)) != 0)
                    snprintf(host, sizeof(host), "localhost");
                host[sizeof(host) - 1] = '\0';
                /* Keep placeholder expansions short for the hint buffer. */
                snprintf(tmp, sizeof(tmp), "%.60s", host);
                break;
            }
            case '%':
                snprintf(tmp, sizeof(tmp), "%%");
                break;
            default:
                snprintf(tmp, sizeof(tmp), "%%%c", *p);
                break;
            }
            tl = strlen(tmp);
            if (ei + tl >= sizeof(expanded))
                break;
            memcpy(expanded + ei, tmp, tl);
            ei += tl;
        } else {
            expanded[ei++] = *p;
        }
    }
    expanded[ei] = '\0';

    if (expanded[0] == '/')
        snprintf(out, out_sz, "%.500s", expanded);
    else
        snprintf(out, out_sz, "%.200s/%.300s", cwd, expanded);
}

static void refresh_core_hint(void)
{
    char pattern[256];

    read_core_pattern(pattern, sizeof(pattern));
    expand_core_path(g_core_hint, sizeof(g_core_hint), pattern, getpid());
}

static const char *signal_name(int sig)
{
    switch (sig) {
    case SIGSEGV:
        return "SIGSEGV";
    case SIGABRT:
        return "SIGABRT";
    case SIGBUS:
        return "SIGBUS";
    case SIGFPE:
        return "SIGFPE";
    case SIGILL:
        return "SIGILL";
    case SIGQUIT:
        return "SIGQUIT";
    case SIGSYS:
        return "SIGSYS";
    case SIGTRAP:
        return "SIGTRAP";
    default:
        return "signal";
    }
}

static void crash_signal_handler(int sig)
{
    if (g_handling_crash) {
        signal(sig, SIG_DFL);
        raise(sig);
        return;
    }
    g_handling_crash = 1;

    refresh_core_hint();
    LOG_FATAL("Caught %s (%d); core dump expected at: %s", signal_name(sig), sig,
              g_core_hint);
    logger_flush();

    signal(sig, SIG_DFL);
    raise(sig);
}

void crash_setup(void)
{
    struct rlimit rl;
    struct sigaction sa;
    static const int fatals[] = {SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL,
                                 SIGQUIT, SIGSYS, SIGTRAP};
    size_t i;

    rl.rlim_cur = RLIM_INFINITY;
    rl.rlim_max = RLIM_INFINITY;
    if (setrlimit(RLIMIT_CORE, &rl) != 0)
        LOG_WARN("setrlimit(RLIMIT_CORE) failed: %s (core dumps may be truncated)",
                 strerror(errno));

    /* Ensure this process is dumpable even if it changed uid. */
    if (prctl(PR_SET_DUMPABLE, 1L) != 0)
        LOG_WARN("prctl(PR_SET_DUMPABLE) failed: %s", strerror(errno));

    refresh_core_hint();
    LOG_INFO("Core dumps enabled; expected path/hint: %s", g_core_hint);

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = crash_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESETHAND;
    for (i = 0; i < sizeof(fatals) / sizeof(fatals[0]); i++) {
        if (sigaction(fatals[i], &sa, NULL) != 0)
            LOG_WARN("sigaction(%s) failed: %s", signal_name(fatals[i]),
                     strerror(errno));
    }
}

int sprint1_logging_init(const char *app_name, char *path_out, size_t path_sz)
{
    char path[256];
    const char *name = app_name && app_name[0] ? app_name : "sprint1";

    if (mkdir("logs", 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "mkdir logs: %s\n", strerror(errno));
        return -1;
    }

    snprintf(path, sizeof(path), "logs/%s-%ld.log", name, (long)time(NULL));
    if (!logger_initFileLogger(path, 1024 * 1024, 5)) {
        fprintf(stderr, "logger_initFileLogger(%s) failed\n", path);
        return -1;
    }
    logger_initConsoleLogger(stderr);
    logger_setLevel(LogLevel_DEBUG);
    logger_autoFlush(100);

    if (path_out && path_sz > 0)
        snprintf(path_out, path_sz, "%s", path);

    LOG_INFO("Logger started (%s)", path);
    crash_setup();
    return 0;
}
