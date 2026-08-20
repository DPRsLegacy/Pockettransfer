#include "jsonutil.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *find_key(const char *json, const char *key)
{
    char pat[80];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = json;
    while ((p = strstr(p, pat)) != NULL) {
        const char *c = p + strlen(pat);
        while (*c && isspace((unsigned char)*c))
            c++;
        if (*c == ':')
            return c + 1;
        p++;
    }
    return NULL;
}

int json_get_string(const char *json, const char *key, char *out, size_t n)
{
    const char *v = find_key(json, key);
    size_t i = 0;
    if (!v || !out || n == 0)
        return 0;
    while (*v && isspace((unsigned char)*v))
        v++;
    if (*v != '"')
        return 0;
    v++;
    while (*v && *v != '"' && i + 1 < n) {
        if (*v == '\\' && v[1]) {
            v++;
            out[i++] = *v++;
        } else {
            out[i++] = *v++;
        }
    }
    out[i] = 0;
    return i > 0;
}

int json_get_int(const char *json, const char *key, int *out)
{
    const char *v = find_key(json, key);
    if (!v || !out)
        return 0;
    while (*v && isspace((unsigned char)*v))
        v++;
    *out = atoi(v);
    return 1;
}
