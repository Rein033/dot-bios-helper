/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file script.c
 * @brief The profile language: parser and player (see script.h).
 */
#include "script.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define DEFAULT_GAP_MS   300u
#define DEFAULT_TAP_MS   40u
#define DEFAULT_EVERY_MS 250u
#define DEFAULT_TIMEOUT  30

/* ---- parsing ---------------------------------------------------------------------------- */

static void err(sc_prog_t *p, int line, const char *fmt, const char *a)
{
    if ( p->nerr < SC_ERRS )
    {
        sc_err_t *e = &p->err[p->nerr];
        e->line = (uint16_t)line;
        (void)snprintf(e->msg, sizeof(e->msg), fmt, a ? a : "");
    }
    p->nerr++;
}

static const char *skip_ws(const char *s)
{
    while ( *s && isspace((unsigned char)*s) )
    {
        ++s;
    }
    return s;
}

/* The next word of *s into w (at most cap-1), *s past it and the spaces after. */
static bool word(const char **s, char *w, size_t cap)
{
    const char *p = skip_ws(*s);
    size_t n = 0;
    while ( *p && !isspace((unsigned char)*p) )
    {
        if ( n + 1 < cap )
        {
            w[n++] = *p;
        }
        ++p;
    }
    w[n] = 0;
    *s = skip_ws(p);
    return n > 0;
}

static bool number(const char *w, int32_t lo, int32_t hi, int32_t *out)
{
    if ( !w[0] )
    {
        return false;
    }
    char *end = NULL;
    const long v = strtol(w, &end, 10);
    if ( *end || (v < lo) || (v > hi) )
    {
        return false;
    }
    *out = (int32_t)v;
    return true;
}

/* "500", "500ms", "2s", "1.5s" */
static bool duration_ms(const char *w, int32_t *out)
{
    if ( !w[0] )
    {
        return false;
    }
    char *end = NULL;
    const double v = strtod(w, &end);
    double ms = v;
    if ( (0 == strcasecmp(end, "s")) || (0 == strcasecmp(end, "sec")) )
    {
        ms = v * 1000.0;
    }
    else if ( *end && (0 != strcasecmp(end, "ms")) )
    {
        return false;
    }
    if ( (ms < 0.0) || (ms > 600000.0) )
    {
        return false;
    }
    *out = (int32_t)(ms + 0.5);
    return true;
}

static bool valid_name(const char *n)
{
    if ( !n[0] || isdigit((unsigned char)n[0]) )
    {
        return false;
    }
    for ( const char *c = n; *c; ++c )
    {
        if ( !isalnum((unsigned char)*c) && ('_' != *c) )
        {
            return false;
        }
    }
    return true;
}

/* KEY or KEY*N; a word that starts with '$' is a variable, checked when it runs. */
static bool key_word(const char *w, kstroke_t *k, int *times)
{
    char base[32];
    (void)snprintf(base, sizeof(base), "%s", w);
    *times = 1;
    char *star = strrchr(base, '*');
    if ( star && (star != base) && star[1] )
    {
        int32_t n = 0;
        if ( !number(star + 1, 1, 99, &n) )
        {
            return false;
        }
        *star = 0;
        *times = (int)n;
    }
    return keys_parse(base, k);
}

static bool onoff(const char *w, bool *out)
{
    if ( (0 == strcasecmp(w, "on")) || (0 == strcasecmp(w, "yes")) || (0 == strcmp(w, "1")) )
    {
        *out = true;
        return true;
    }
    if ( (0 == strcasecmp(w, "off")) || (0 == strcasecmp(w, "no")) || (0 == strcmp(w, "0")) )
    {
        *out = false;
        return true;
    }
    return false;
}

static void parse_line(sc_prog_t *p, int ln, const char *l, int *open_loops, int *loop_at)
{
    char cmd[12];
    const char *rest = l;
    (void)word(&rest, cmd, sizeof(cmd));
    for ( char *c = cmd; *c; ++c )
    {
        *c = (char)tolower((unsigned char)*c);
    }
    char w[48];

    /* settings */
    if ( 0 == strcmp(cmd, "title") )
    {
        (void)snprintf(p->title, sizeof(p->title), "%s", rest);
        return;
    }
    if ( 0 == strcmp(cmd, "desc") )
    {
        (void)snprintf(p->desc, sizeof(p->desc), "%s", rest);
        return;
    }
    if ( 0 == strcmp(cmd, "arm") )
    {
        (void)word(&rest, w, sizeof(w));
        if ( 0 == strcasecmp(w, "manual") )
        {
            p->arm = SC_ARM_MANUAL;
        }
        else if ( 0 == strcasecmp(w, "plug") )
        {
            p->arm = SC_ARM_PLUG;
        }
        else if ( 0 == strcasecmp(w, "delay") )
        {
            (void)word(&rest, w, sizeof(w));
            int32_t s = 0;
            if ( !number(w, 1, 3600, &s) )
            {
                err(p, ln, "arm delay: seconds 1..3600", NULL);
                return;
            }
            p->arm = SC_ARM_DELAY;
            p->arm_delay_s = s;
        }
        else
        {
            err(p, ln, "arm: manual, plug or delay <s>", NULL);
        }
        return;
    }
    if ( 0 == strcmp(cmd, "after") )
    {
        (void)word(&rest, w, sizeof(w));
        if ( 0 == strcasecmp(w, "stop") )
        {
            p->after = SC_AFTER_STOP;
        }
        else if ( 0 == strcasecmp(w, "rearm") )
        {
            p->after = SC_AFTER_REARM;
        }
        else if ( (0 == strcasecmp(w, "chain")) && *rest )
        {
            p->after = SC_AFTER_CHAIN;
            (void)snprintf(p->chain, sizeof(p->chain), "%s", rest);
        }
        else
        {
            err(p, ln, "after: stop, rearm or chain <profile>", NULL);
        }
        return;
    }
    if ( 0 == strcmp(cmd, "confirm") )
    {
        (void)word(&rest, w, sizeof(w));
        if ( !onoff(w, &p->confirm) )
        {
            err(p, ln, "confirm: on or off", NULL);
        }
        return;
    }
    if ( 0 == strcmp(cmd, "timeout") )
    {
        (void)word(&rest, w, sizeof(w));
        if ( !number(w, 0, 1440, &p->timeout_min) )
        {
            err(p, ln, "timeout: minutes 0..1440", NULL);
        }
        return;
    }

    /* commands */
    if ( p->n >= SC_CMDS )
    {
        err(p, ln, "more than 64 commands", NULL);
        return;
    }
    sc_cmd_t *c = &p->cmd[p->n];
    memset(c, 0, sizeof(*c));
    c->line = (uint16_t)ln;
    kstroke_t k;
    int times = 0;

    if ( 0 == strcmp(cmd, "spam") )
    {
        c->op = SC_OP_SPAM;
        (void)word(&rest, w, sizeof(w));
        if ( !keys_parse(w, &k) )
        {
            err(p, ln, "spam: unknown key '%s'", w);
            return;
        }
        (void)snprintf(c->s, sizeof(c->s), "%s", w);
        c->a = 10;
        c->b = (int32_t)DEFAULT_EVERY_MS;
        if ( word(&rest, w, sizeof(w)) && !number(w, 1, 600, &c->a) )
        {
            err(p, ln, "spam: seconds 1..600", NULL);
            return;
        }
        if ( word(&rest, w, sizeof(w)) && !number(w, 50, 5000, &c->b) )
        {
            err(p, ln, "spam: every 50..5000 ms", NULL);
            return;
        }
    }
    else if ( (0 == strcmp(cmd, "keys")) || (0 == strcmp(cmd, "key")) || (0 == strcmp(cmd, "press")) )
    {
        c->op = SC_OP_KEYS;
        if ( !*rest )
        {
            err(p, ln, "keys: which keys?", NULL);
            return;
        }
        (void)snprintf(c->s, sizeof(c->s), "%s", rest);
        const char *r = rest;
        while ( word(&r, w, sizeof(w)) )
        {
            if ( ('$' != w[0]) && !key_word(w, &k, &times) )
            {
                err(p, ln, "unknown key '%s'", w);
                return;
            }
        }
    }
    else if ( 0 == strcmp(cmd, "hold") )
    {
        c->op = SC_OP_HOLD;
        (void)word(&rest, w, sizeof(w));
        if ( !keys_parse(w, &k) )
        {
            err(p, ln, "hold: unknown key '%s'", w);
            return;
        }
        (void)snprintf(c->s, sizeof(c->s), "%s", w);
        (void)word(&rest, w, sizeof(w));
        if ( !duration_ms(w, &c->a) || (c->a < 10) || (c->a > 30000) )
        {
            err(p, ln, "hold: 10 ms .. 30 s", NULL);
            return;
        }
    }
    else if ( (0 == strcmp(cmd, "type")) || (0 == strcmp(cmd, "line")) )
    {
        c->op = ('t' == cmd[0]) ? SC_OP_TYPE : SC_OP_LINE;
        (void)snprintf(c->s, sizeof(c->s), "%s", rest);
        for ( const char *ch = rest; *ch; ++ch )
        {
            if ( !keys_char(*ch, &k) )
            {
                err(p, ln, "type: a character a US keyboard has not", NULL);
                return;
            }
        }
        if ( (SC_OP_TYPE == c->op) && !*rest )
        {
            err(p, ln, "type: what?", NULL);
            return;
        }
    }
    else if ( 0 == strcmp(cmd, "wait") )
    {
        c->op = SC_OP_WAIT;
        (void)word(&rest, w, sizeof(w));
        if ( !duration_ms(w, &c->a) )
        {
            err(p, ln, "wait: ms, or seconds as 2s", NULL);
            return;
        }
    }
    else if ( 0 == strcmp(cmd, "waitpc") )
    {
        c->op = SC_OP_WAITPC;
        c->a = 0;
        if ( word(&rest, w, sizeof(w)) && !number(w, 0, 3600, &c->a) )
        {
            err(p, ln, "waitpc: seconds 0..3600", NULL);
            return;
        }
    }
    else if ( (0 == strcmp(cmd, "ask")) || (0 == strcmp(cmd, "msg")) )
    {
        c->op = ('a' == cmd[0]) ? SC_OP_ASK : SC_OP_MSG;
        (void)snprintf(c->s, sizeof(c->s), "%s", *rest ? rest : ((SC_OP_ASK == c->op) ? "Next?" : ""));
    }
    else if ( 0 == strcmp(cmd, "code") )
    {
        c->op = SC_OP_CODE;
        c->a = 4;
        const char *r = rest;
        if ( word(&r, w, sizeof(w)) && isdigit((unsigned char)w[0]) )
        {
            if ( !number(w, 1, 16, &c->a) )
            {
                err(p, ln, "code: 1..16 digits", NULL);
                return;
            }
            rest = r;
        }
        (void)snprintf(c->s, sizeof(c->s), "%s", *rest ? rest : "Type the code you see");
    }
    else if ( 0 == strcmp(cmd, "input") )
    {
        c->op = SC_OP_INPUT;
        (void)word(&rest, w, sizeof(w));
        if ( !valid_name(w) || (strlen(w) >= SC_NAME) )
        {
            err(p, ln, "input: a name (letters, digits, _)", NULL);
            return;
        }
        memcpy(c->name, w, strlen(w) + 1u); /* length checked above */
        c->a = 8;
        const char *r = rest;
        if ( word(&r, w, sizeof(w)) && isdigit((unsigned char)w[0]) )
        {
            if ( !number(w, 1, 16, &c->a) )
            {
                err(p, ln, "input: 1..16 digits", NULL);
                return;
            }
            rest = r;
        }
        (void)snprintf(c->s, sizeof(c->s), "%s", *rest ? rest : c->name);
    }
    else if ( 0 == strcmp(cmd, "set") )
    {
        c->op = SC_OP_SET;
        (void)word(&rest, w, sizeof(w));
        if ( !valid_name(w) || (strlen(w) >= SC_NAME) )
        {
            err(p, ln, "set: a name (letters, digits, _)", NULL);
            return;
        }
        memcpy(c->name, w, strlen(w) + 1u); /* length checked above */
        (void)snprintf(c->s, sizeof(c->s), "%s", rest);
    }
    else if ( 0 == strcmp(cmd, "repeat") )
    {
        c->op = SC_OP_REPEAT;
        (void)word(&rest, w, sizeof(w));
        if ( !number(w, 0, 9999, &c->a) )
        {
            err(p, ln, "repeat: a count 0..9999 (0 = until Stop)", NULL);
            return;
        }
        if ( *open_loops >= SC_LOOPS )
        {
            err(p, ln, "repeat: at most 4 inside each other", NULL);
            return;
        }
        loop_at[(*open_loops)++] = p->n;
    }
    else if ( 0 == strcmp(cmd, "end") )
    {
        c->op = SC_OP_END;
        if ( 0 == *open_loops )
        {
            err(p, ln, "end without repeat", NULL);
            return;
        }
        const int r = loop_at[--(*open_loops)];
        c->b = r;
        p->cmd[r].b = p->n;
    }
    else if ( (0 == strcmp(cmd, "gap")) || (0 == strcmp(cmd, "tap")) )
    {
        c->op = ('g' == cmd[0]) ? SC_OP_GAP : SC_OP_TAP;
        (void)word(&rest, w, sizeof(w));
        if ( !number(w, (SC_OP_TAP == c->op) ? 10 : 0, 10000, &c->a) )
        {
            err(p, ln, "%s: ms", cmd);
            return;
        }
    }
    else if ( 0 == strcmp(cmd, "beep") )
    {
        c->op = SC_OP_BEEP;
    }
    else
    {
        err(p, ln, "unknown command '%s'", cmd);
        return;
    }
    p->n++;
}

int sc_parse(sc_prog_t *p, const char *text)
{
    memset(p, 0, sizeof(*p));
    p->timeout_min = DEFAULT_TIMEOUT;
    int open_loops = 0;
    int loop_at[SC_LOOPS] = { 0 };
    int ln = 0;
    const char *src = text ? text : "";
    while ( *src )
    {
        ++ln;
        const char *e = strchr(src, '\n');
        const size_t len = e ? (size_t)(e - src) : strlen(src);
        char l[SC_TEXT + 32];
        const size_t c = (len < sizeof(l) - 1) ? len : sizeof(l) - 1;
        memcpy(l, src, c);
        l[c] = 0;
        src += len + (e ? 1 : 0);
        if ( len >= sizeof(l) - 1 )
        {
            err(p, ln, "line too long", NULL);
            continue;
        }
        char *h = strchr(l, '#');
        if ( h )
        {
            *h = 0;
        }
        size_t k = strlen(l);
        while ( k && isspace((unsigned char)l[k - 1]) )
        {
            l[--k] = 0;
        }
        const char *s = skip_ws(l);
        if ( *s )
        {
            parse_line(p, ln, s, &open_loops, loop_at);
        }
    }
    if ( open_loops )
    {
        err(p, ln, "repeat without end", NULL);
    }
    return p->nerr;
}

void sc_arm_label(const sc_prog_t *p, char *buf, int cap)
{
    if ( SC_ARM_PLUG == p->arm )
    {
        (void)snprintf(buf, (size_t)cap, "%s", (SC_AFTER_REARM == p->after) ? "plug, batch" : "plug");
    }
    else if ( SC_ARM_DELAY == p->arm )
    {
        (void)snprintf(buf, (size_t)cap, "delay %d s", (int)p->arm_delay_s);
    }
    else
    {
        (void)snprintf(buf, (size_t)cap, "%s", (SC_AFTER_REARM == p->after) ? "manual, batch" : "manual");
    }
}

/* ---- playing ----------------------------------------------------------------------------- */

static bool host(const sc_player_t *pl)
{
    return pl->io.ready && pl->io.ready(pl->io.ctx);
}

static int pending(const sc_player_t *pl)
{
    return pl->io.pending ? pl->io.pending(pl->io.ctx) : 0;
}

static void flush(sc_player_t *pl)
{
    if ( pl->io.flush )
    {
        pl->io.flush(pl->io.ctx);
    }
}

static void event(sc_player_t *pl, sc_event_t ev, const char *t)
{
    if ( pl->io.event )
    {
        pl->io.event(pl->io.ctx, ev, t ? t : "");
    }
}

void sc_player_init(sc_player_t *pl, const sc_io_t *io)
{
    memset(pl, 0, sizeof(*pl));
    pl->io = *io;
}

void sc_load(sc_player_t *pl, const sc_prog_t *p)
{
    flush(pl);
    pl->p = p;
    pl->state = SC_IDLE;
    pl->runs = 0;
    pl->text[0] = 0;
    pl->note[0] = 0;
}

static const char *var_get(const sc_player_t *pl, const char *name)
{
    for ( int i = 0; i < pl->nvar; ++i )
    {
        if ( 0 == strcmp(pl->var[i].name, name) )
        {
            return pl->var[i].val;
        }
    }
    return NULL;
}

static void var_set(sc_player_t *pl, const char *name, const char *val)
{
    int i = 0;
    while ( (i < pl->nvar) && (0 != strcmp(pl->var[i].name, name)) )
    {
        ++i;
    }
    if ( i == pl->nvar )
    {
        if ( pl->nvar >= SC_VARS )
        {
            return;
        }
        pl->nvar++;
    }
    (void)snprintf(pl->var[i].name, sizeof(pl->var[i].name), "%s", name);
    (void)snprintf(pl->var[i].val, sizeof(pl->var[i].val), "%s", val);
}

/* $name into its value; $$ is a dollar; an unknown name stays as it was written. */
static void expand(const sc_player_t *pl, const char *in, char *out, size_t cap)
{
    size_t n = 0;
    while ( *in && (n + 1 < cap) )
    {
        if ( ('$' == in[0]) && ('$' == in[1]) )
        {
            out[n++] = '$';
            in += 2;
            continue;
        }
        if ( ('$' == in[0]) && (isalpha((unsigned char)in[1]) || ('_' == in[1])) )
        {
            char name[SC_NAME];
            size_t k = 0;
            const char *p = in + 1;
            while ( (isalnum((unsigned char)*p) || ('_' == *p)) && (k + 1 < sizeof(name)) )
            {
                name[k++] = *p++;
            }
            name[k] = 0;
            const char *v = var_get(pl, name);
            if ( v )
            {
                while ( *v && (n + 1 < cap) )
                {
                    out[n++] = *v++;
                }
                in = p;
                continue;
            }
        }
        out[n++] = *in++;
    }
    out[n] = 0;
}

static void queue_text(sc_player_t *pl, const char *t)
{
    kstroke_t k;
    for ( ; *t && (pl->qn < SC_QUEUE); ++t )
    {
        if ( keys_char(*t, &k) )
        {
            pl->q[pl->qn++] = k;
        }
    }
}

static void queue_keys(sc_player_t *pl, const char *spec)
{
    char w[48];
    const char *r = spec;
    while ( word(&r, w, sizeof(w)) && (pl->qn < SC_QUEUE) )
    {
        if ( '$' == w[0] )
        {
            char v[64];
            expand(pl, w, v, sizeof(v));
            queue_text(pl, v);
            continue;
        }
        kstroke_t k;
        int times = 1;
        if ( !key_word(w, &k, &times) )
        {
            continue;
        }
        for ( int i = 0; (i < times) && (pl->qn < SC_QUEUE); ++i )
        {
            pl->q[pl->qn++] = k;
        }
    }
}

static void begin_queue(sc_player_t *pl, uint32_t now, uint16_t down_ms)
{
    pl->qi = 0;
    pl->qdown = down_ms;
    pl->next_tap = now;
    if ( 0 == pl->qn )
    {
        pl->pc++;
    }
}

static void begin_run(sc_player_t *pl, uint32_t now)
{
    flush(pl);
    pl->state = SC_RUN;
    pl->pc = 0;
    pl->until = 0;
    pl->qn = 0;
    pl->qi = 0;
    pl->q_counted = false;
    pl->depth = 0;
    pl->nvar = 0;
    pl->gap = DEFAULT_GAP_MS;
    pl->tap = DEFAULT_TAP_MS;
    pl->text[0] = 0;
    pl->note[0] = 0;
    pl->rearm_wait = false;
    (void)now;
    event(pl, SC_EV_START, pl->p ? pl->p->title : "");
}

static void arm(sc_player_t *pl, uint32_t now)
{
    const sc_prog_t *p = pl->p;
    pl->note[0] = 0;
    if ( SC_ARM_PLUG == p->arm )
    {
        pl->state = SC_ARMED;
        pl->armed_at = now;
        pl->need_unplug = host(pl);
        event(pl, SC_EV_ARMED, p->title);
    }
    else if ( SC_ARM_DELAY == p->arm )
    {
        pl->state = SC_COUNTDOWN;
        pl->until = now + (uint32_t)p->arm_delay_s * 1000u;
    }
    else
    {
        begin_run(pl, now);
    }
}

void sc_start(sc_player_t *pl, uint32_t now)
{
    if ( !pl->p || pl->p->nerr || ((SC_IDLE != pl->state) && (SC_DONE != pl->state)) )
    {
        return;
    }
    pl->runs = 0;
    pl->rearm_wait = false;
    if ( pl->p->confirm )
    {
        pl->state = SC_CONFIRM;
        return;
    }
    arm(pl, now);
}

void sc_confirm(sc_player_t *pl, bool yes, uint32_t now)
{
    if ( SC_CONFIRM != pl->state )
    {
        return;
    }
    if ( yes )
    {
        arm(pl, now);
    }
    else
    {
        pl->state = SC_IDLE;
    }
}

void sc_go(sc_player_t *pl, uint32_t now)
{
    if ( (SC_ARMED == pl->state) || (SC_COUNTDOWN == pl->state) )
    {
        begin_run(pl, now);
    }
}

void sc_stop(sc_player_t *pl)
{
    const bool armed = (SC_ARMED == pl->state) || (SC_COUNTDOWN == pl->state) || pl->rearm_wait;
    flush(pl);
    pl->qn = 0;
    pl->state = SC_IDLE;
    pl->rearm_wait = false;
    (void)snprintf(pl->note, sizeof(pl->note), "%s", armed ? "Disarmed" : "Stopped");
    if ( armed )
    {
        event(pl, SC_EV_DISARMED, "stop");
    }
}

void sc_next(sc_player_t *pl, uint32_t now)
{
    (void)now;
    if ( SC_SPAM == pl->state )
    {
        flush(pl);
    }
    if ( (SC_SPAM == pl->state) || (SC_ASK == pl->state) || (SC_WAITPC == pl->state) )
    {
        pl->state = SC_RUN;
        pl->until = 0;
        pl->pc++;
    }
}

void sc_entry(sc_player_t *pl, const char *digits, uint32_t now)
{
    if ( SC_ENTRY != pl->state )
    {
        return;
    }
    pl->state = SC_RUN;
    pl->pc++;
    if ( !pl->entry_send )
    {
        var_set(pl, pl->entry_var, digits);
        return;
    }
    pl->qn = 0;
    queue_text(pl, digits);
    if ( pl->qn < SC_QUEUE )
    {
        pl->q[pl->qn++] = (kstroke_t){ 0, 0x28 }; /* ENTER */
    }
    pl->qi = 0;
    pl->qdown = (uint16_t)pl->tap;
    pl->next_tap = now;
    pl->q_counted = true;
}

static void finish(sc_player_t *pl)
{
    const sc_prog_t *p = pl->p;
    pl->runs++;
    pl->state = SC_DONE;
    pl->text[0] = 0;
    event(pl, SC_EV_DONE, p->title);
    if ( SC_AFTER_REARM == p->after )
    {
        pl->rearm_wait = true;
    }
    else if ( SC_AFTER_CHAIN == p->after )
    {
        event(pl, SC_EV_CHAIN, p->chain);
    }
}

static void exec(sc_player_t *pl, const sc_cmd_t *c, uint32_t now)
{
    char buf[SC_TEXT + 64];
    switch ( c->op )
    {
    case SC_OP_SPAM:
        (void)keys_parse(c->s, &pl->spam_key);
        (void)snprintf(pl->text, sizeof(pl->text), "%s", c->s);
        pl->spam_ms = (uint32_t)c->a * 1000u;
        pl->spam_every = (uint32_t)c->b;
        pl->until = 0;
        pl->spam_cap = now + SC_SPAM_CAP_MS;
        pl->next_tap = now;
        pl->state = SC_SPAM;
        break;
    case SC_OP_KEYS:
        pl->qn = 0;
        queue_keys(pl, c->s);
        begin_queue(pl, now, (uint16_t)pl->tap);
        break;
    case SC_OP_HOLD:
        pl->qn = 0;
        (void)keys_parse(c->s, &pl->q[0]);
        pl->qn = 1;
        begin_queue(pl, now, (uint16_t)c->a);
        break;
    case SC_OP_TYPE:
    case SC_OP_LINE:
        pl->qn = 0;
        expand(pl, c->s, buf, sizeof(buf));
        queue_text(pl, buf);
        if ( (SC_OP_LINE == c->op) && (pl->qn < SC_QUEUE) )
        {
            pl->q[pl->qn++] = (kstroke_t){ 0, 0x28 };
        }
        begin_queue(pl, now, (uint16_t)pl->tap);
        break;
    case SC_OP_WAIT:
        (void)snprintf(pl->text, sizeof(pl->text), "Waiting");
        pl->until = now + (uint32_t)c->a;
        if ( 0 == pl->until )
        {
            pl->until = 1; /* 0 means "no wait" */
        }
        pl->pc++;
        break;
    case SC_OP_WAITPC:
        if ( host(pl) )
        {
            pl->pc++;
            break;
        }
        (void)snprintf(pl->text, sizeof(pl->text), "Waiting for the PC");
        pl->until = c->a ? now + (uint32_t)c->a * 1000u : 0;
        pl->state = SC_WAITPC;
        break;
    case SC_OP_ASK:
        expand(pl, c->s, pl->text, sizeof(pl->text));
        pl->state = SC_ASK;
        break;
    case SC_OP_MSG:
        expand(pl, c->s, pl->text, sizeof(pl->text));
        pl->pc++;
        break;
    case SC_OP_CODE:
    case SC_OP_INPUT:
        expand(pl, c->s, pl->text, sizeof(pl->text));
        pl->entry_n = (int)c->a;
        pl->entry_send = (SC_OP_CODE == c->op);
        (void)snprintf(pl->entry_var, sizeof(pl->entry_var), "%s", c->name);
        pl->state = SC_ENTRY;
        break;
    case SC_OP_SET:
        expand(pl, c->s, buf, sizeof(buf));
        var_set(pl, c->name, buf);
        pl->pc++;
        break;
    case SC_OP_REPEAT:
        if ( pl->depth < SC_LOOPS )
        {
            pl->loop[pl->depth].start = pl->pc + 1;
            pl->loop[pl->depth].left = c->a ? c->a : -1;
            pl->depth++;
        }
        pl->pc++;
        break;
    case SC_OP_END:
        if ( pl->depth > 0 )
        {
            int32_t * const left = &pl->loop[pl->depth - 1].left;
            if ( *left > 0 )
            {
                --*left;
            }
            if ( 0 != *left )
            {
                pl->pc = pl->loop[pl->depth - 1].start;
                break;
            }
            pl->depth--;
        }
        pl->pc++;
        break;
    case SC_OP_GAP:
        pl->gap = (uint32_t)c->a;
        pl->pc++;
        break;
    case SC_OP_TAP:
        pl->tap = (uint32_t)c->a;
        pl->pc++;
        break;
    case SC_OP_BEEP:
        event(pl, SC_EV_BEEP, "");
        pl->pc++;
        break;
    default:
        pl->pc++;
        break;
    }
}

static bool past(uint32_t now, uint32_t t)
{
    return (int32_t)(now - t) >= 0;
}

void sc_tick(sc_player_t *pl, uint32_t now)
{
    const sc_prog_t *p = pl->p;
    if ( !p )
    {
        return;
    }
    const bool h = host(pl);
    switch ( pl->state )
    {
    case SC_ARMED:
        if ( p->timeout_min && past(now, pl->armed_at + (uint32_t)p->timeout_min * 60000u) )
        {
            pl->state = SC_IDLE;
            pl->rearm_wait = false;
            (void)snprintf(pl->note, sizeof(pl->note), "Disarmed: %d min without a PC", (int)p->timeout_min);
            event(pl, SC_EV_DISARMED, "timeout");
            return;
        }
        if ( pl->need_unplug )
        {
            if ( !h )
            {
                pl->need_unplug = false;
            }
            return;
        }
        if ( h )
        {
            begin_run(pl, now);
        }
        return;
    case SC_COUNTDOWN:
        if ( past(now, pl->until) )
        {
            pl->until = 0;
            begin_run(pl, now);
        }
        return;
    case SC_DONE:
        if ( pl->rearm_wait && !h )
        {
            /* the cable is out: arm again for the next PC */
            pl->rearm_wait = false;
            pl->state = SC_ARMED;
            pl->armed_at = now;
            pl->need_unplug = false;
            event(pl, SC_EV_ARMED, p->title);
        }
        return;
    case SC_SPAM:
        if ( !pl->until && h )
        {
            pl->until = now + pl->spam_ms; /* the PC is listening: start the clock */
        }
        if ( pl->until ? past(now, pl->until) : past(now, pl->spam_cap) )
        {
            pl->state = SC_RUN;
            pl->until = 0;
            pl->pc++;
            return;
        }
        if ( past(now, pl->next_tap) && h && (0 == pending(pl)) )
        {
            (void)pl->io.send(pl->io.ctx, pl->spam_key, (uint16_t)pl->tap);
            pl->next_tap = now + pl->spam_every;
        }
        return;
    case SC_WAITPC:
        if ( h || (pl->until && past(now, pl->until)) )
        {
            pl->state = SC_RUN;
            pl->until = 0;
            pl->pc++;
        }
        return;
    case SC_RUN:
        break;
    default:
        return;
    }

    /* SC_RUN */
    if ( pl->until )
    {
        if ( !past(now, pl->until) )
        {
            return;
        }
        pl->until = 0;
    }
    if ( pending(pl) > 0 )
    {
        return; /* the last key is still going out */
    }
    if ( pl->qn > 0 )
    {
        if ( pl->qi >= pl->qn )
        {
            pl->qn = 0;
            if ( !pl->q_counted )
            {
                pl->pc++;
            }
            pl->q_counted = false;
            return;
        }
        if ( !past(now, pl->next_tap) )
        {
            return;
        }
        if ( !h )
        {
            (void)snprintf(pl->text, sizeof(pl->text), "Waiting for the PC");
            return;
        }
        const kstroke_t k = pl->q[pl->qi];
        if ( pl->io.send(pl->io.ctx, k, pl->qdown) )
        {
            pl->qi++;
            char nm[32];
            keys_name(k, nm, sizeof(nm));
            (void)snprintf(pl->text, sizeof(pl->text), "%d/%d  %s", pl->qi, pl->qn, nm);
            pl->next_tap = now + pl->gap;
        }
        return;
    }
    /* A few commands that do not wait run in the same tick. */
    for ( int guard = 0; (guard < 16) && (SC_RUN == pl->state) && !pl->until && (0 == pl->qn); ++guard )
    {
        if ( pl->pc >= p->n )
        {
            finish(pl);
            return;
        }
        exec(pl, &p->cmd[pl->pc], now);
    }
}

int sc_state(const sc_player_t *pl)
{
    return pl->state;
}

uint32_t sc_left_ms(const sc_player_t *pl, uint32_t now)
{
    const uint32_t t = pl->until;
    if ( (SC_ARMED == pl->state) && pl->p && pl->p->timeout_min )
    {
        const uint32_t end = pl->armed_at + (uint32_t)pl->p->timeout_min * 60000u;
        return ((int32_t)(end - now) > 0) ? end - now : 0u;
    }
    if ( SC_SPAM == pl->state )
    {
        if ( !t )
        {
            return pl->spam_ms; /* waiting for the PC */
        }
    }
    if ( !t )
    {
        return 0u;
    }
    return ((int32_t)(t - now) > 0) ? t - now : 0u;
}

const char *sc_state_name(int st)
{
    static const char *const N[] = { "idle", "confirm", "armed", "countdown", "run",
                                     "spam", "ask", "entry", "waitpc", "done" };
    return ((st >= 0) && (st < (int)(sizeof(N) / sizeof(N[0])))) ? N[st] : "?";
}
