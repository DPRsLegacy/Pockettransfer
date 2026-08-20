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

int json_escape(const char *in, char *out, size_t n)
{
    size_t o = 0;
    if (!in || !out || n == 0)
        return 0;
    while (*in && o + 2 < n) {
        unsigned char c = (unsigned char)*in++;
        if (c == '"' || c == '\\') {
            if (o + 3 >= n)
                break;
            out[o++] = '\\';
            out[o++] = (char)c;
        } else if (c == '\n') {
            if (o + 3 >= n)
                break;
            out[o++] = '\\';
            out[o++] = 'n';
        } else if (c >= 0x20) {
            out[o++] = (char)c;
        }
    }
    out[o] = 0;
    return (int)o;
}

int json_auth_body(char *out, size_t n, const char *username, const char *password,
                   const char *name, const char *platform)
{
    char u[80], p[160], nm[40], pl[24];
    int wrote;
    if (!out || n == 0)
        return 0;
    json_escape(username ? username : "", u, sizeof(u));
    json_escape(password ? password : "", p, sizeof(p));
    json_escape(name ? name : "console", nm, sizeof(nm));
    json_escape(platform ? platform : "unknown", pl, sizeof(pl));
    wrote = snprintf(out, n,
                     "{\"username\":\"%s\",\"password\":\"%s\",\"name\":\"%s\",\"platform\":\"%s\"}",
                     u, p, nm, pl);
    return wrote > 0 && (size_t)wrote < n;
}
