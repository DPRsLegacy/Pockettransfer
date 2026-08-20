#include "http.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char g_token[160];
static char g_ca[256];

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    HttpBuffer *b = userdata;
    size_t n = size * nmemb;
    char *p = realloc(b->data, b->len + n + 1);
    if (!p)
        return 0;
    b->data = p;
    memcpy(b->data + b->len, ptr, n);
    b->len += n;
    b->data[b->len] = 0;
    return n;
}

void http_buffer_free(HttpBuffer *b)
{
    if (!b)
        return;
    free(b->data);
    b->data = NULL;
    b->len = 0;
}

int pt_http_init(const char *ca_path)
{
    memset(g_token, 0, sizeof(g_token));
    memset(g_ca, 0, sizeof(g_ca));
    if (ca_path)
        snprintf(g_ca, sizeof(g_ca), "%s", ca_path);
    return curl_global_init(CURL_GLOBAL_DEFAULT);
}

void pt_http_shutdown(void)
{
    curl_global_cleanup();
}

int pt_http_set_token(const char *token)
{
    snprintf(g_token, sizeof(g_token), "%s", token ? token : "");
    return 0;
}

int pt_http_request(const char *method, const char *url, const char *body, const char *mime,
                    HttpBuffer *out, long *status)
{
    CURL *c = curl_easy_init();
    CURLcode rc;
    struct curl_slist *headers = NULL;
    char auth[200];
    memset(out, 0, sizeof(*out));
    if (!c)
        return -1;
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, method);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, out);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "Pockettransfer/1.0");
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(c, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
    if (g_ca[0])
        curl_easy_setopt(c, CURLOPT_CAINFO, g_ca);
    if (mime) {
        char ct[80];
        snprintf(ct, sizeof(ct), "Content-Type: %s", mime);
        headers = curl_slist_append(headers, ct);
    }
    if (g_token[0]) {
        snprintf(auth, sizeof(auth), "Authorization: Bearer %s", g_token);
        headers = curl_slist_append(headers, auth);
    }
    if (headers)
        curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
    if (body)
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, body);
    rc = curl_easy_perform(c);
    if (status)
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, status);
    if (headers)
        curl_slist_free_all(headers);
    curl_easy_cleanup(c);
    return rc == CURLE_OK ? 0 : -1;
}

int pt_http_upload(const char *url, const char *field, const char *filename,
                   const void *filedata, size_t filelen, HttpBuffer *out, long *status)
{
    CURL *c = curl_easy_init();
    curl_mime *mime;
    curl_mimepart *part;
    char auth[200];
    struct curl_slist *headers = NULL;
    CURLcode rc;
    memset(out, 0, sizeof(*out));
    if (!c)
        return -1;
    mime = curl_mime_init(c);
    part = curl_mime_addpart(mime);
    curl_mime_name(part, field);
    curl_mime_filename(part, filename);
    curl_mime_data(part, filedata, filelen);
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_MIMEPOST, mime);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, out);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(c, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
    if (g_ca[0])
        curl_easy_setopt(c, CURLOPT_CAINFO, g_ca);
    if (g_token[0]) {
        snprintf(auth, sizeof(auth), "Authorization: Bearer %s", g_token);
        headers = curl_slist_append(headers, auth);
        curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
    }
    rc = curl_easy_perform(c);
    if (status)
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, status);
    if (headers)
        curl_slist_free_all(headers);
    curl_mime_free(mime);
    curl_easy_cleanup(c);
    return rc == CURLE_OK ? 0 : -1;
}
