#ifndef POCKETTRANSFER_JSONUTIL_H
#define POCKETTRANSFER_JSONUTIL_H

#include <stddef.h>

int json_get_string(const char *json, const char *key, char *out, size_t n);
int json_get_int(const char *json, const char *key, int *out);
int json_escape(const char *in, char *out, size_t n);
int json_auth_body(char *out, size_t n, const char *username, const char *password,
                   const char *name, const char *platform);

#endif
