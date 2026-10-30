/* PsyX_Log* for the headless linux32 host: one line per event on stderr and
 * optionally a --log file, prefixed with the virtual vblank and a level
 * (docs/porting-plan.md §5). */
#include "host.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static FILE *s_log_file;

void host_log_open(const char *path) {
  s_log_file = fopen(path, "w");
  if (!s_log_file)
    fprintf(stderr, "host: cannot open log file %s\n", path);
}

void host_log_flush(void) {
  fflush(stderr);
  if (s_log_file)
    fflush(s_log_file);
}

static void emit(const char *level, const char *fmt, va_list ap) {
  char buf[1024];
  size_t n;
  vsnprintf(buf, sizeof buf, fmt, ap);
  n = strlen(buf);
  while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
    buf[--n] = 0;
  if (n == 0)
    return;
  fprintf(stderr, "[%6d] %s %s\n", host_vblank_count(), level, buf);
  if (s_log_file)
    fprintf(s_log_file, "[%6d] %s %s\n", host_vblank_count(), level, buf);
}

#define LOG_FN(name, level)                                                    \
  void name(const char *fmt, ...) {                                            \
    va_list ap;                                                                \
    va_start(ap, fmt);                                                         \
    emit(level, fmt, ap);                                                      \
    va_end(ap);                                                                \
  }

LOG_FN(host_log, "host")
LOG_FN(PsyX_Log, "psyx")
LOG_FN(PsyX_Log_Info, "info")
LOG_FN(PsyX_Log_Warning, "WARN")
LOG_FN(PsyX_Log_Error, "ERR ")
LOG_FN(PsyX_Log_Success, "ok  ")
