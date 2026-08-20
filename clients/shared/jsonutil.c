#include "jsonutil.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *find_mem(const char *hay, size_t n, const char *needle)
{
    size_t m = strlen(needle);
    size_t i;
    if (m == 0 || m > n)
        return NULL;
    for (i = 0; i + m <= n; i++) {
        if (memcmp(hay + i, needle, m) == 0)
            return hay + i;
    }
    return NULL;
}

static const char *find_key_n(const char *json, size_t len, const char *key)
{
    char pat[80];
    size_t plen;
    const char *p, *limit, *hit, *c;
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    plen = strlen(pat);
    p = json;
    limit = json + len;
    while (p + plen <= limit) {
        hit = find_mem(p, (size_t)(limit - p), pat);
        if (!hit)
            return NULL;
        c = hit + plen;
        while (c < limit && isspace((unsigned char)*c))
            c++;
        if (c < limit && *c == ':')
            return c + 1;
        p = hit + 1;
    }
    return NULL;
}

static const char *skip_ws_n(const char *v, const char *limit)
{
    while (v < limit && isspace((unsigned char)*v))
        v++;
    return v;
}

int json_get_string_n(const char *json, size_t len, const char *key, char *out, size_t n)
{
    const char *limit = json + len;
    const char *v = find_key_n(json, len, key);
    size_t i = 0;
    if (!v || !out || n == 0)
        return 0;
    v = skip_ws_n(v, limit);
    if (v >= limit || *v != '"')
        return 0;
    v++;
    while (v < limit && *v && *v != '"' && i + 1 < n) {
        if (*v == '\\' && v + 1 < limit) {
            v++;
            out[i++] = *v++;
        } else {
            out[i++] = *v++;
        }
    }
    out[i] = 0;
    return i > 0;
}

int json_get_int_n(const char *json, size_t len, const char *key, int *out)
{
    const char *limit = json + len;
    const char *v = find_key_n(json, len, key);
    if (!v || !out)
        return 0;
    v = skip_ws_n(v, limit);
    if (v >= limit)
        return 0;
    *out = atoi(v);
    return 1;
}

int json_get_bool_n(const char *json, size_t len, const char *key, int *out)
{
    const char *limit = json + len;
    const char *v = find_key_n(json, len, key);
    if (!v || !out)
        return 0;
    v = skip_ws_n(v, limit);
    if (v >= limit)
        return 0;
    if (v + 4 <= limit && strncmp(v, "true", 4) == 0) {
        *out = 1;
        return 1;
    }
    if (v + 5 <= limit && strncmp(v, "false", 5) == 0) {
        *out = 0;
        return 1;
    }
    if (*v == '1') {
        *out = 1;
        return 1;
    }
    if (*v == '0') {
        *out = 0;
        return 1;
    }
    return 0;
}

const char *json_find_array_n(const char *json, size_t len, const char *key)
{
    const char *limit = json + len;
    const char *v = find_key_n(json, len, key);
    if (!v)
        return NULL;
    v = skip_ws_n(v, limit);
    if (v < limit && *v == '[')
        return v;
    return NULL;
}

static const char *skip_string_z(const char *p)
{
    if (*p != '"')
        return p;
    p++;
    while (*p && *p != '"') {
        if (*p == '\\' && p[1])
            p += 2;
        else
            p++;
    }
    return *p ? p + 1 : p;
}

int json_array_nth_object(const char *arr, int index, const char **start, const char **end)
{
    const char *p;
    int depth = 0, idx = -1;
    if (!arr || !start || !end || index < 0)
        return 0;
    p = arr;
    while (*p && isspace((unsigned char)*p))
        p++;
    if (*p != '[')
        return 0;
    p++;
    while (*p) {
        if (*p == '"') {
            p = skip_string_z(p);
            continue;
        }
        if (*p == '{') {
            if (depth == 0) {
                idx++;
                if (idx == index)
                    *start = p;
            }
            depth++;
            p++;
            continue;
        }
        if (*p == '}') {
            depth--;
            p++;
            if (depth == 0 && idx == index) {
                *end = p;
                return 1;
            }
            continue;
        }
        if (*p == ']' && depth == 0)
            return 0;
        p++;
    }
    return 0;
}

int json_get_string(const char *json, const char *key, char *out, size_t n)
{
    if (!json)
        return 0;
    return json_get_string_n(json, strlen(json), key, out, n);
}

int json_get_int(const char *json, const char *key, int *out)
{
    if (!json)
        return 0;
    return json_get_int_n(json, strlen(json), key, out);
}

int json_get_bool(const char *json, const char *key, int *out)
{
    if (!json)
        return 0;
    return json_get_bool_n(json, strlen(json), key, out);
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
