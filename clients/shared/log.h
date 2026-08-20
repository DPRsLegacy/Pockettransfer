#ifndef POCKETTRANSFER_LOG_H
#define POCKETTRANSFER_LOG_H

void pt_log_init(const char *tag);
void pt_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void pt_log_shutdown(void);

#endif
