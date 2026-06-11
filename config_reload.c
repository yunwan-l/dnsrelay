/*
 * config_reload.c -- 运行时配置重载支持
 */
#include "config_reload.h"
#include "dns_table.h"
#include "dns_cache.h"

#include <stdarg.h>
#include <sys/stat.h>

#ifndef _WIN32
#include <signal.h>
volatile sig_atomic_t g_reload_requested = 0;

static void sighup_handler(int signo)
{
    (void)signo;
    g_reload_requested = 1;
}
#endif

static time_t g_config_mtime = 0;

static void reload_log(FILE *log_file, const char *fmt, ...)
{
    va_list ap;
    va_list ap_file;

    va_start(ap, fmt);
    va_copy(ap_file, ap);
    vprintf(fmt, ap);
    if (log_file) {
        vfprintf(log_file, fmt, ap_file);
        fflush(log_file);
    }
    va_end(ap_file);
    va_end(ap);
}

static int get_file_mtime(const char *path, time_t *mtime_out)
{
    struct stat st;

    if (stat(path, &st) != 0)
        return -1;

    if (mtime_out)
        *mtime_out = st.st_mtime;
    return 0;
}

void runtime_reload_mark_loaded(const char *config_file)
{
    time_t mtime = 0;

    if (get_file_mtime(config_file, &mtime) == 0)
        g_config_mtime = mtime;
    else
        g_config_mtime = 0;
}

void runtime_reload_setup(void)
{
#ifndef _WIN32
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sighup_handler;
    sigaction(SIGHUP, &sa, NULL);
#else
    /* Windows下通过轮询文件修改时间自动重载，无需额外信号处理。 */
#endif
}

static int do_reload(const char *config_file, FILE *log_file, const char *reason)
{
    time_t old_mtime = g_config_mtime;

    if (reason)
        reload_log(log_file, "%s\n", reason);

    if (domain_table_reload(config_file) < 0) {
        reload_log(log_file, "[ERROR] Reload failed!\n");
        g_config_mtime = old_mtime;
        return -1;
    }

    dns_cache_free();
    runtime_reload_mark_loaded(config_file);
    reload_log(log_file, "--- Reload complete (%d records) ---\n\n", domain_count);
    return domain_count;
}

int runtime_reload_force(const char *config_file, FILE *log_file, const char *reason)
{
    return do_reload(config_file, log_file, reason);
}

int runtime_reload_poll(const char *config_file, FILE *log_file)
{
#ifndef _WIN32
    if (g_reload_requested) {
        g_reload_requested = 0;
        return do_reload(config_file, log_file,
                         "\n--- Reloading domain table from SIGHUP ---");
    }
#endif

    {
        time_t mtime = 0;

        if (get_file_mtime(config_file, &mtime) != 0)
            return 0;

        if (g_config_mtime != 0 && mtime != g_config_mtime) {
            return do_reload(config_file, log_file,
                             "\n--- Detected config file change, reloading ---");
        }
    }

    return 0;
}
