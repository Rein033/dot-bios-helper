/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file store.c
 * @brief The profile list (see store.h).
 */
#include "store.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "builtin.h"
#include "script.h"

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
static SemaphoreHandle_t s_mx;
#define LOCK()   xSemaphoreTakeRecursive(s_mx, portMAX_DELAY)
#define UNLOCK() xSemaphoreGiveRecursive(s_mx)
#define BIGALLOC(n) heap_caps_malloc((n), MALLOC_CAP_SPIRAM)
#else
#define LOCK()   ((void)0)
#define UNLOCK() ((void)0)
#define BIGALLOC(n) malloc(n)
#endif

typedef struct {
    store_row_t row;
    char       *text;   /* card profiles: our copy; built-ins: their constant */
    bool        used;
} entry_t;

static entry_t s_e[STORE_MAX];
static int s_n;
static bool s_changed = true;
static sc_prog_t *s_prog;   /* parse scratch, under the lock */

static void describe(entry_t *e)
{
    if ( !s_prog )
    {
        return;
    }
    (void)sc_parse(s_prog, e->text);
    store_row_t *r = &e->row;
    if ( s_prog->title[0] )
    {
        (void)snprintf(r->title, sizeof(r->title), "%s", s_prog->title);
    }
    else
    {
        (void)snprintf(r->title, sizeof(r->title), "%s", r->name);
    }
    (void)snprintf(r->desc, sizeof(r->desc), "%s", s_prog->desc);
    sc_arm_label(s_prog, r->arm, (int)sizeof(r->arm));
    r->nerr = s_prog->nerr;
    r->first_err_line = s_prog->nerr ? s_prog->err[0].line : 0;
    (void)snprintf(r->first_err, sizeof(r->first_err), "%s", s_prog->nerr ? s_prog->err[0].msg : "");
}

static int cmp(const void *a, const void *b)
{
    const entry_t *x = a;
    const entry_t *y = b;
    if ( x->row.on_card != y->row.on_card )
    {
        return x->row.on_card ? -1 : 1; /* the card first */
    }
    if ( !x->row.on_card )
    {
        return 0; /* built-ins keep their order (qsort is not stable: see rebuild) */
    }
    return strcasecmp(x->row.title, y->row.title);
}

/* The list again: card profiles sorted by title, then the built-ins no card file replaces. */
static void rebuild(void)
{
    static entry_t card[STORE_MAX]; /* 9 KB: not on a task stack; callers hold the lock */
    int nc = 0;
    for ( int i = 0; i < s_n; ++i )
    {
        if ( s_e[i].row.on_card )
        {
            card[nc++] = s_e[i];
        }
    }
    qsort(card, (size_t)nc, sizeof(card[0]), cmp);
    s_n = 0;
    for ( int i = 0; i < nc; ++i )
    {
        s_e[s_n++] = card[i];
    }
    for ( int b = 0; (b < builtin_count()) && (s_n < STORE_MAX); ++b )
    {
        bool replaced = false;
        for ( int i = 0; i < nc; ++i )
        {
            if ( 0 == strcmp(card[i].row.name, builtin_name(b)) )
            {
                replaced = true;
                s_e[i].row.builtin = true;
            }
        }
        if ( replaced )
        {
            continue;
        }
        entry_t *e = &s_e[s_n++];
        memset(e, 0, sizeof(*e));
        e->used = true;
        e->text = (char *)builtin_text(b);
        (void)snprintf(e->row.name, sizeof(e->row.name), "%s", builtin_name(b));
        e->row.builtin = true;
        describe(e);
    }
    s_changed = true;
}

void store_init(void)
{
#ifdef ESP_PLATFORM
    if ( !s_mx )
    {
        s_mx = xSemaphoreCreateRecursiveMutex();
    }
#endif
    if ( !s_prog )
    {
        s_prog = BIGALLOC(sizeof(sc_prog_t));
    }
    LOCK();
    s_n = 0;
    rebuild();
    UNLOCK();
}

void store_clear_card(void)
{
    LOCK();
    for ( int i = 0; i < s_n; ++i )
    {
        if ( s_e[i].row.on_card )
        {
            free(s_e[i].text);
            s_e[i].text = NULL;
            s_e[i].row.on_card = false;
            s_e[i].used = false;
        }
    }
    int k = 0;
    for ( int i = 0; i < s_n; ++i )
    {
        if ( s_e[i].used )
        {
            s_e[k++] = s_e[i];
        }
    }
    s_n = k;
    rebuild();
    UNLOCK();
}

bool store_put_card(const char *name, const char *text, size_t len)
{
    if ( !store_name_ok(name) || (len >= STORE_TEXT_MAX) )
    {
        return false;
    }
    char *copy = BIGALLOC(len + 1u);
    if ( !copy )
    {
        return false;
    }
    memcpy(copy, text, len);
    copy[len] = 0;
    LOCK();
    int at = -1;
    for ( int i = 0; i < s_n; ++i )
    {
        if ( s_e[i].row.on_card && (0 == strcmp(s_e[i].row.name, name)) )
        {
            at = i;
        }
    }
    if ( at < 0 )
    {
        int cards = 0;
        for ( int i = 0; i < s_n; ++i )
        {
            cards += s_e[i].row.on_card ? 1 : 0;
        }
        if ( cards >= STORE_MAX - builtin_count() )
        {
            UNLOCK();
            free(copy);
            return false;
        }
        /* drop the built-ins, add, rebuild puts them back */
        int k = 0;
        for ( int i = 0; i < s_n; ++i )
        {
            if ( s_e[i].row.on_card )
            {
                s_e[k++] = s_e[i];
            }
        }
        s_n = k;
        at = s_n++;
        memset(&s_e[at], 0, sizeof(s_e[at]));
    }
    else
    {
        free(s_e[at].text);
    }
    entry_t *e = &s_e[at];
    e->used = true;
    e->text = copy;
    memset(&e->row, 0, sizeof(e->row));
    (void)snprintf(e->row.name, sizeof(e->row.name), "%s", name);
    e->row.on_card = true;
    describe(e);
    /* rebuild() wants only card entries before it */
    int k = 0;
    for ( int i = 0; i < s_n; ++i )
    {
        if ( s_e[i].row.on_card )
        {
            s_e[k++] = s_e[i];
        }
    }
    s_n = k;
    rebuild();
    UNLOCK();
    return true;
}

void store_remove_card(const char *name)
{
    LOCK();
    int k = 0;
    for ( int i = 0; i < s_n; ++i )
    {
        if ( s_e[i].row.on_card && (0 == strcmp(s_e[i].row.name, name)) )
        {
            free(s_e[i].text);
            continue;
        }
        if ( s_e[i].row.on_card )
        {
            s_e[k++] = s_e[i];
        }
    }
    s_n = k;
    rebuild();
    UNLOCK();
}

int store_count(void)
{
    LOCK();
    const int n = s_n;
    UNLOCK();
    return n;
}

bool store_row(int i, store_row_t *out)
{
    LOCK();
    const bool ok = (i >= 0) && (i < s_n);
    if ( ok )
    {
        *out = s_e[i].row;
    }
    UNLOCK();
    return ok;
}

int store_text(int i, char *buf, size_t cap)
{
    if ( !cap )
    {
        return -1;
    }
    LOCK();
    int n = -1;
    if ( (i >= 0) && (i < s_n) && s_e[i].text )
    {
        n = snprintf(buf, cap, "%s", s_e[i].text);
        if ( (size_t)n >= cap )
        {
            n = (int)cap - 1;
        }
    }
    else
    {
        buf[0] = 0;
    }
    UNLOCK();
    return n;
}

int store_find_name(const char *name)
{
    LOCK();
    int at = -1;
    for ( int i = 0; (i < s_n) && (at < 0); ++i )
    {
        if ( 0 == strcmp(s_e[i].row.name, name) )
        {
            at = i;
        }
    }
    UNLOCK();
    return at;
}

int store_find_title(const char *title)
{
    LOCK();
    int at = -1;
    for ( int i = 0; (i < s_n) && (at < 0); ++i )
    {
        if ( (0 == strcasecmp(s_e[i].row.title, title)) || (0 == strcasecmp(s_e[i].row.name, title)) )
        {
            at = i;
        }
    }
    UNLOCK();
    return at;
}

bool store_changed(void)
{
    LOCK();
    const bool c = s_changed;
    s_changed = false;
    UNLOCK();
    return c;
}

bool store_name_ok(const char *name)
{
    const size_t n = name ? strlen(name) : 0u;
    if ( (n < 1u) || (n >= STORE_NAME_MAX) )
    {
        return false;
    }
    for ( size_t i = 0; i < n; ++i )
    {
        const char c = name[i];
        if ( !(((c >= 'a') && (c <= 'z')) || ((c >= '0') && (c <= '9')) || ('-' == c) || ('_' == c)) )
        {
            return false;
        }
    }
    return true;
}

void store_slug(const char *title, char *out, size_t cap)
{
    size_t n = 0;
    bool dash = false;
    for ( const char *c = title; *c && (n + 1 < cap) && (n + 1 < STORE_NAME_MAX); ++c )
    {
        const char l = (char)tolower((unsigned char)*c);
        if ( ((l >= 'a') && (l <= 'z')) || ((l >= '0') && (l <= '9')) )
        {
            if ( dash && n )
            {
                out[n++] = '-';
                if ( n + 1 >= cap )
                {
                    break;
                }
            }
            out[n++] = l;
            dash = false;
        }
        else
        {
            dash = true;
        }
    }
    out[n] = 0;
    if ( !n && (cap > 8) )
    {
        (void)snprintf(out, cap, "profile");
    }
}
