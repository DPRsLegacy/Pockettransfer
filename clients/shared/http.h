#ifndef POCKETTRANSFER_HTTP_H
#define POCKETTRANSFER_HTTP_H

#include <stddef.h>

typedef struct {
    char *data;
    size_t len;
} HttpBuffer;

int pt_http_init(const char *ca_path);
void pt_http_shutdown(void);
int pt_http_set_token(const char *token);
/* method: GET/POST/DELETE. body may be NULL. mime e.g. application/json or NULL */
int pt_http_request(const char *method, const char *url, const char *body, const char *mime,
                    HttpBuffer *out, long *status);
int pt_http_upload(const char *url, const char *field, const char *filename,
                   const void *filedata, size_t filelen, HttpBuffer *out, long *status);
void http_buffer_free(HttpBuffer *b);

#endif
