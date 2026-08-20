#include "http.h"
#include "log.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __3DS__
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#endif

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

static int url_is_auth(const char *url)
{
    return url && strstr(url, "/auth/") != NULL;
}

static void log_http(const char *method, const char *url, CURLcode rc, const char *curlerr,
                     long status, const HttpBuffer *out)
{
    pt_log("http %s %s curl=%d %s status=%ld bytes=%zu",
           method ? method : "?", url ? url : "?", (int)rc,
           (rc != CURLE_OK && curlerr[0]) ? curlerr : curl_easy_strerror(rc),
           status, out && out->data ? out->len : 0);
    if (out && out->data && out->len && !url_is_auth(url))
        pt_log("http resp %.240s", out->data);
    else if (out && out->data && out->len && url_is_auth(url))
        pt_log("http auth resp len=%zu status=%ld", out->len, status);
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
    pt_log("http init ca=%s", g_ca[0] ? g_ca : "(none)");
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

#ifdef __3DS__
/* 3DS clocks are often years behind. Keep CA checks; ignore notBefore/notAfter. */
static int ssl_ignore_clock(void *ctx, mbedtls_x509_crt *crt, int depth, uint32_t *flags)
{
    (void)ctx;
    (void)crt;
    (void)depth;
    if (flags)
        *flags &= ~(MBEDTLS_X509_BADCERT_EXPIRED | MBEDTLS_X509_BADCERT_FUTURE |
                    MBEDTLS_X509_BADCRL_EXPIRED | MBEDTLS_X509_BADCRL_FUTURE);
    return 0;
}

static CURLcode sslctx_3ds(CURL *curl, void *sslctx, void *parm)
{
    (void)curl;
    (void)parm;
    mbedtls_ssl_conf_verify((mbedtls_ssl_config *)sslctx, ssl_ignore_clock, NULL);
    return CURLE_OK;
}
#endif

static void http_set_tls(CURL *c)
{
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(c, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
#ifdef __3DS__
    curl_easy_setopt(c, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(c, CURLOPT_SSL_CTX_FUNCTION, sslctx_3ds);
#endif
    if (g_ca[0])
        curl_easy_setopt(c, CURLOPT_CAINFO, g_ca);
}

int pt_http_request(const char *method, const char *url, const char *body, const char *mime,
                    HttpBuffer *out, long *status)
{
    CURL *c = curl_easy_init();
    CURLcode rc;
    struct curl_slist *headers = NULL;
    char auth[200];
    char errbuf[CURL_ERROR_SIZE];
    memset(out, 0, sizeof(*out));
    errbuf[0] = 0;
    if (!c)
        return -1;
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, method);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, out);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "Pockettransfer/1.0");
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, errbuf);
    http_set_tls(c);
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
    log_http(method, url, rc, errbuf, status ? *status : 0, out);
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
    char errbuf[CURL_ERROR_SIZE];
    memset(out, 0, sizeof(*out));
    errbuf[0] = 0;
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
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, errbuf);
    http_set_tls(c);
    if (g_token[0]) {
        snprintf(auth, sizeof(auth), "Authorization: Bearer %s", g_token);
        headers = curl_slist_append(headers, auth);
        curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
    }
    rc = curl_easy_perform(c);
    if (status)
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, status);
    log_http("UPLOAD", url, rc, errbuf, status ? *status : 0, out);
    if (headers)
        curl_slist_free_all(headers);
    curl_mime_free(mime);
    curl_easy_cleanup(c);
    return rc == CURLE_OK ? 0 : -1;
}
