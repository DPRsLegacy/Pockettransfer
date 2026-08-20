#ifndef POCKETTRANSFER_JSONUTIL_H
#define POCKETTRANSFER_JSONUTIL_H

#include <stddef.h>

int json_get_string(const char *json, const char *key, char *out, size_t n);
int json_get_int(const char *json, const char *key, int *out);
int json_get_bool(const char *json, const char *key, int *out);
int json_escape(const char *in, char *out, size_t n);
int json_auth_body(char *out, size_t n, const char *username, const char *password,
                   const char *name, const char *platform);

int json_get_string_n(const char *json, size_t len, const char *key, char *out, size_t n);
int json_get_int_n(const char *json, size_t len, const char *key, int *out);
int json_get_bool_n(const char *json, size_t len, const char *key, int *out);
const char *json_find_array_n(const char *json, size_t len, const char *key);
int json_array_nth_object(const char *arr, int index, const char **start, const char **end);

#endif
