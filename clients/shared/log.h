#ifndef POCKETTRANSFER_LOG_H
#define POCKETTRANSFER_LOG_H

#ifdef PT_NO_LOG
#define pt_log_init(tag) ((void)0)
#define pt_log(...) ((void)0)
#define pt_log_shutdown() ((void)0)
#else
void pt_log_init(const char *tag);
void pt_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void pt_log_shutdown(void);
#endif

#endif
