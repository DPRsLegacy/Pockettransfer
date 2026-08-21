#include "log.h"

#ifndef PT_NO_LOG

#include <stdarg.h>
#include <stdio.h>

#define PT_LOG_PATH "sdmc:/pockettransfer.log"
#define PT_LOG_MAX (512 * 1024)

static FILE *g_log;

void pt_log_init(const char *tag)
{
    FILE *f;
    long sz = 0;

    f = fopen(PT_LOG_PATH, "rb");
    if (f) {
        if (fseek(f, 0, SEEK_END) == 0)
            sz = ftell(f);
        fclose(f);
    }

    g_log = fopen(PT_LOG_PATH, sz > PT_LOG_MAX ? "wb" : "ab");
    if (!g_log)
        return;
    pt_log("---- %s start ----", tag ? tag : "pockettransfer");
}

void pt_log(const char *fmt, ...)
{
    va_list ap;
    if (!g_log || !fmt)
        return;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

void pt_log_shutdown(void)
{
    if (!g_log)
        return;
    pt_log("---- stop ----");
    fclose(g_log);
    g_log = NULL;
}

#endif
