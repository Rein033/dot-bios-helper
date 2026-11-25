/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file web.c
 * @brief The profile editor's HTTP server (see web.h).
 */
#include "web.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"

#include "app.h"
#include "keys.h"
#include "script.h"
#include "store.h"

static const char *TAG = "bh.web";

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");

static httpd_handle_t s_srv;

bool web_running(void)
{
    return NULL != s_srv;
}

/* ---- helpers ----------------------------------------------------------------------------- */

static void jstr(char *out, size_t cap, const char *s)
{
    size_t n = 0;
    for ( ; *s && (n + 7 < cap); ++s )
    {
        const unsigned char c = (unsigned char)*s;
        if ( ('"' == c) || ('\\' == c) )
        {
            out[n++] = '\\';
            out[n++] = (char)c;
        }
        else if ( c < 0x20u )
        {
            n += (size_t)snprintf(out + n, cap - n, "\\u%04x", c);
        }
        else
        {
            out[n++] = (char)c;
        }
    }
    out[n] = 0;
}

static esp_err_t send_json(httpd_req_t *req, const char *json)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, json);
}

/* The body, NUL-terminated, in PSRAM; NULL (and an answer sent) when too long or broken. */
static char *body(httpd_req_t *req, size_t *len)
{
    if ( req->content_len >= STORE_TEXT_MAX )
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "A profile is at most 4095 bytes");
        return NULL;
    }
    char *b = heap_caps_malloc(req->content_len + 1u, MALLOC_CAP_SPIRAM);
    if ( !b )
    {
        httpd_resp_send_500(req);
        return NULL;
    }
    size_t got = 0;
    while ( got < req->content_len )
    {
        const int r = httpd_req_recv(req, b + got, req->content_len - got);
        if ( r <= 0 )
        {
            free(b);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Body cut short");
            return NULL;
        }
        got += (size_t)r;
    }
    b[got] = 0;
    /* CR LF from a browser's textarea: keep the LF */
    size_t k = 0;
    for ( size_t i = 0; i < got; ++i )
    {
        if ( '\r' != b[i] )
        {
            b[k++] = b[i];
        }
    }
    b[k] = 0;
    *len = k;
    return b;
}

static bool query_name(httpd_req_t *req, char *name, size_t cap)
{
    char q[96];
    if ( (ESP_OK != httpd_req_get_url_query_str(req, q, sizeof(q)))
         || (ESP_OK != httpd_query_key_value(q, "name", name, cap)) )
    {
        return false;
    }
    return store_name_ok(name);
}

/* ---- handlers ------------------------------------------------------------------------------ */

static esp_err_t h_index(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, index_html_start, index_html_end - index_html_start);
}

static esp_err_t h_state(httpd_req_t *req)
{
    char b[900];
    (void)app_state_json(b, sizeof(b));
    return send_json(req, b);
}

static esp_err_t h_profiles(httpd_req_t *req)
{
    const size_t cap = 12000;
    char *b = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM);
    if ( !b )
    {
        return httpd_resp_send_500(req);
    }
    size_t n = (size_t)snprintf(b, cap, "[");
    for ( int i = 0; i < store_count() && (n + 400 < cap); ++i )
    {
        store_row_t r;
        if ( !store_row(i, &r) )
        {
            break;
        }
        char t[100];
        char d[200];
        char e[120];
        jstr(t, sizeof(t), r.title);
        jstr(d, sizeof(d), r.desc);
        jstr(e, sizeof(e), r.first_err);
        n += (size_t)snprintf(b + n, cap - n,
                              "%s{\"name\":\"%s\",\"title\":\"%s\",\"desc\":\"%s\",\"arm\":\"%s\",\"errors\":%d,"
                              "\"first_error\":\"%s\",\"error_line\":%d,\"card\":%s,\"builtin\":%s}",
                              i ? "," : "", r.name, t, d, r.arm, r.nerr, e, r.first_err_line,
                              r.on_card ? "true" : "false", r.builtin ? "true" : "false");
    }
    (void)snprintf(b + n, cap - n, "]");
    const esp_err_t rc = send_json(req, b);
    free(b);
    return rc;
}

static esp_err_t h_profile(httpd_req_t *req)
{
    char name[STORE_NAME_MAX];
    const int i = query_name(req, name, sizeof(name)) ? store_find_name(name) : -1;
    if ( i < 0 )
    {
        return httpd_resp_send_404(req);
    }
    char *t = heap_caps_malloc(STORE_TEXT_MAX, MALLOC_CAP_SPIRAM);
    if ( !t )
    {
        return httpd_resp_send_500(req);
    }
    (void)store_text(i, t, STORE_TEXT_MAX);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    const esp_err_t rc = httpd_resp_sendstr(req, t);
    free(t);
    return rc;
}

static esp_err_t h_keys(httpd_req_t *req)
{
    char b[2048];
    size_t n = (size_t)snprintf(b, sizeof(b), "[");
    for ( int i = 0; (i < keys_named_count()) && (n + 24 < sizeof(b)); ++i )
    {
        n += (size_t)snprintf(b + n, sizeof(b) - n, "%s\"%s\"", i ? "," : "", keys_named(i));
    }
    (void)snprintf(b + n, sizeof(b) - n, "]");
    return send_json(req, b);
}

/* The errors of a text as JSON. */
static esp_err_t check_reply(httpd_req_t *req, const char *text, const char *extra)
{
    sc_prog_t *p = heap_caps_malloc(sizeof(sc_prog_t), MALLOC_CAP_SPIRAM);
    if ( !p )
    {
        return httpd_resp_send_500(req);
    }
    (void)sc_parse(p, text);
    char b[1400];
    char t[100];
    char arm[24];
    jstr(t, sizeof(t), p->title);
    sc_arm_label(p, arm, sizeof(arm));
    size_t n = (size_t)snprintf(b, sizeof(b), "{%s\"title\":\"%s\",\"commands\":%d,\"arm\":\"%s\",\"error_count\":%d,\"errors\":[",
                                extra ? extra : "", t, p->n, arm, p->nerr);
    const int shown = (p->nerr < SC_ERRS) ? p->nerr : SC_ERRS;
    for ( int i = 0; i < shown; ++i )
    {
        char m[120];
        jstr(m, sizeof(m), p->err[i].msg);
        n += (size_t)snprintf(b + n, sizeof(b) - n, "%s{\"line\":%d,\"msg\":\"%s\"}", i ? "," : "", p->err[i].line, m);
    }
    (void)snprintf(b + n, sizeof(b) - n, "]}");
    free(p);
    return send_json(req, b);
}

static esp_err_t h_check(httpd_req_t *req)
{
    size_t len = 0;
    char *t = body(req, &len);
    if ( !t )
    {
        return ESP_OK;
    }
    const esp_err_t rc = check_reply(req, t, NULL);
    free(t);
    return rc;
}

static esp_err_t h_save(httpd_req_t *req)
{
    char name[STORE_NAME_MAX];
    if ( !query_name(req, name, sizeof(name)) )
    {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "name: 1-39 of a-z 0-9 - _");
    }
    size_t len = 0;
    char *t = body(req, &len);
    if ( !t )
    {
        return ESP_OK;
    }
    char *copy = heap_caps_malloc(len + 1u, MALLOC_CAP_SPIRAM);
    if ( !copy )
    {
        free(t);
        return httpd_resp_send_500(req);
    }
    memcpy(copy, t, len + 1u);
    app_cmd_t c = { .op = CMD_SAVE, .seq = app_seq(), .text = copy, .len = len };
    (void)snprintf(c.s, sizeof(c.s), "%s", name);
    const uint32_t seq = c.seq;
    if ( !app_post(&c) )
    {
        free(t);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "The app is busy");
    }
    const int rc = app_wait(seq, 8000);
    char extra[96];
    (void)snprintf(extra, sizeof(extra), "\"saved\":%s,\"rc\":%d,", (0 == rc) ? "true" : "false", rc);
    const esp_err_t e = check_reply(req, t, extra);
    free(t);
    return e;
}

static esp_err_t h_delete(httpd_req_t *req)
{
    char name[STORE_NAME_MAX];
    if ( !query_name(req, name, sizeof(name)) )
    {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "name?");
    }
    app_cmd_t c = { .op = CMD_DELETE, .seq = app_seq() };
    (void)snprintf(c.s, sizeof(c.s), "%s", name);
    const uint32_t seq = c.seq;
    if ( !app_post(&c) )
    {
        return httpd_resp_send_500(req);
    }
    const int rc = app_wait(seq, 8000);
    char b[64];
    (void)snprintf(b, sizeof(b), "{\"deleted\":%s,\"rc\":%d}", (0 == rc) ? "true" : "false", rc);
    return send_json(req, b);
}

static esp_err_t h_cmd(httpd_req_t *req)
{
    char b[160];
    const int n = httpd_req_recv(req, b, (req->content_len < sizeof(b) - 1) ? req->content_len : sizeof(b) - 1);
    if ( n <= 0 )
    {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "command?");
    }
    b[n] = 0;
    char *arg = strchr(b, ' ');
    if ( arg )
    {
        *arg++ = 0;
    }
    static const struct {
        const char *w;
        int op;
    } W[] = {
        { "start", CMD_START }, { "stop", CMD_STOP }, { "go", CMD_GO }, { "next", CMD_NEXT },
        { "yes", CMD_YES }, { "no", CMD_NO }, { "select", CMD_SELECT }, { "code", CMD_CODE },
        { "key", CMD_KEY }, { "type", CMD_TYPE }, { "reload", CMD_RELOAD },
    };
    for ( size_t i = 0; i < sizeof(W) / sizeof(W[0]); ++i )
    {
        if ( 0 == strcmp(b, W[i].w) )
        {
            app_cmd_t c = { .op = W[i].op };
            (void)snprintf(c.s, sizeof(c.s), "%s", arg ? arg : "");
            if ( !app_post(&c) )
            {
                return httpd_resp_send_500(req);
            }
            return send_json(req, "{\"ok\":true}");
        }
    }
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "unknown command");
}

bool web_start(void)
{
    if ( s_srv )
    {
        return true;
    }
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = 80;
    cfg.stack_size = 8192;
    cfg.max_uri_handlers = 12;
    cfg.lru_purge_enable = true;
    if ( ESP_OK != httpd_start(&s_srv, &cfg) )
    {
        ESP_LOGE(TAG, "the server would not start");
        s_srv = NULL;
        return false;
    }
    const httpd_uri_t U[] = {
        { .uri = "/", .method = HTTP_GET, .handler = h_index },
        { .uri = "/api/state", .method = HTTP_GET, .handler = h_state },
        { .uri = "/api/profiles", .method = HTTP_GET, .handler = h_profiles },
        { .uri = "/api/profile", .method = HTTP_GET, .handler = h_profile },
        { .uri = "/api/keys", .method = HTTP_GET, .handler = h_keys },
        { .uri = "/api/check", .method = HTTP_POST, .handler = h_check },
        { .uri = "/api/save", .method = HTTP_POST, .handler = h_save },
        { .uri = "/api/delete", .method = HTTP_POST, .handler = h_delete },
        { .uri = "/api/cmd", .method = HTTP_POST, .handler = h_cmd },
    };
    for ( size_t i = 0; i < sizeof(U) / sizeof(U[0]); ++i )
    {
        (void)httpd_register_uri_handler(s_srv, &U[i]);
    }
    ESP_LOGI(TAG, "editor serving on port 80");
    return true;
}
