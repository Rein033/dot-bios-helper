/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file app.c
 * @brief The app's Active Object (see app.h).
 *
 * A tick every TICK_MS plays the profile (sc_tick), drains the web editor's commands and runs
 * the card's file jobs one at a time; every few ticks the glass is drawn again. What blocks -
 * starting the web server, keeping a setting - goes to a helper task.
 */
#include "app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "kaos.h"
#include "kdrv_buttons.h"
#include "kdrv_display.h"
#include "kdrv_wifi.h"
#include "kode.h"
#include "kode_sys.h"
#include "kqp_signals.h"

#include "gfx.h"
#include "hidkb.h"
#include "keys.h"
#include "script.h"
#include "store.h"
#include "ui.h"
#include "web.h"

#define TAG "bh.app"

#define TICK_MS     20
#define DRAW_EVERY  5            /* the glass at 10 frames a second */
#define TAP_MS      600
#define TAP_SLOP    24
#define TOAST_MS    3500
#define JOBS        24
#define CARD_FILES  24

enum AppSignals {
    APP_TICK_SIG = MAX_SIG,
};

/* ---- file jobs ------------------------------------------------------------------------- */

typedef enum { J_MKDIR = 0, J_LIST, J_READ, J_WRITE, J_DELETE } job_op_t;

typedef struct {
    uint8_t  op;
    uint32_t seq;
    char     name[STORE_NAME_MAX];
    char    *buf;     /* J_WRITE: the text, ours */
    size_t   len;
} job_t;

typedef struct {
    QActive    super;
    QTimeEvt   tick;
    uint32_t   ticks;
    /* the player */
    sc_prog_t  *prog;
    sc_player_t pl;
    char       sel_name[STORE_NAME_MAX];
    char       chain[48];
    bool       chain_pending;
    /* the glass */
    ui_model_t m;
    bool       glass_up;
    bool       in_flight;
    bool       touch_down;
    int        tx0;
    int        ty0;
    int        tx;
    int        ty;
    uint32_t   t_down;
    uint32_t   toast_until;
    uint32_t   flash_until;
    /* the card */
    job_t      job[JOBS];
    int        njob;
    bool       busy;
    job_t      cur;
    char       *readbuf;
    char       found[CARD_FILES][STORE_NAME_MAX];
    int        nfound;
    uint16_t   list_skip;
    uint32_t   retry_at;
    int        retries;
    /* Wi-Fi */
    bool       wifi_up;
    bool       wifi_asked;
    bool       web_asked;
} App;

static QEvtPtr     l_queue[64];
static StackType_t s_stack[KQP_AO_STACK_BYTES / sizeof(StackType_t)];
static App l_app;
static QActive * const AO_App = &l_app.super;

static QState App_initial(App * const me, void const * const par);
static QState App_running(App * const me, QEvt const * const e);

/* ---- what the web task sees ------------------------------------------------------------ */

static QueueHandle_t     s_cmdq;
static SemaphoreHandle_t s_snap_mx;
static uint32_t          s_seq;
static volatile uint32_t s_done_seq;
static volatile int      s_done_rc;

typedef struct {
    char     state[12];
    char     title[48];
    char     name[STORE_NAME_MAX];
    char     text[SC_TEXT];
    char     note[64];
    int      step;
    int      steps;
    uint32_t left_ms;
    int      runs;
    bool     need_unplug;
    bool     rearm_wait;
    int      entry_n;
    bool     entry_send;
    bool     host;
    bool     kb;
    int      port;
    bool     card_ok;
    int      card_files;
    char     ip[16];
} snap_t;

static snap_t s_snap;

uint32_t app_seq(void)
{
    return __atomic_add_fetch(&s_seq, 1u, __ATOMIC_RELAXED);
}

bool app_post(app_cmd_t *c)
{
    if ( !s_cmdq || (pdTRUE != xQueueSend(s_cmdq, c, pdMS_TO_TICKS(200))) )
    {
        free(c->text);
        c->text = NULL;
        return false;
    }
    return true;
}

int app_wait(uint32_t seq, uint32_t timeout_ms)
{
    for ( uint32_t t = 0; t < timeout_ms; t += 20 )
    {
        if ( s_done_seq == seq )
        {
            return s_done_rc;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return -1;
}

static void json_str(char *out, size_t cap, const char *s)
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

int app_state_json(char *buf, size_t cap)
{
    snap_t s;
    xSemaphoreTake(s_snap_mx, portMAX_DELAY);
    s = s_snap;
    xSemaphoreGive(s_snap_mx);
    char title[100];
    char text[200];
    char note[130];
    char name[90];
    json_str(title, sizeof(title), s.title);
    json_str(text, sizeof(text), s.text);
    json_str(note, sizeof(note), s.note);
    json_str(name, sizeof(name), s.name);
    return snprintf(buf, cap,
                    "{\"state\":\"%s\",\"title\":\"%s\",\"name\":\"%s\",\"text\":\"%s\",\"note\":\"%s\","
                    "\"step\":%d,\"steps\":%d,\"left_ms\":%u,\"runs\":%d,\"need_unplug\":%s,\"rearm_wait\":%s,"
                    "\"entry_n\":%d,\"entry_send\":%s,\"host\":%s,\"keyboard\":%s,\"port\":\"%s\","
                    "\"card\":%s,\"card_files\":%d,\"ip\":\"%s\",\"version\":\"%s\"}",
                    s.state, title, name, text, note, s.step, s.steps, (unsigned)s.left_ms, s.runs,
                    s.need_unplug ? "true" : "false", s.rearm_wait ? "true" : "false", s.entry_n,
                    s.entry_send ? "true" : "false", s.host ? "true" : "false", s.kb ? "true" : "false",
                    s.port ? "usb-c" : "dock", s.card_ok ? "true" : "false", s.card_files, s.ip, APP_VERSION);
}

/* ---- the helper task: what blocks -------------------------------------------------------- */

typedef struct {
    char key[16];
    char value[48];   /* "" with key "web": start the server */
} help_t;

static QueueHandle_t s_help;

static void helper_task(void *arg)
{
    (void)arg;
    help_t h;
    for ( ;; )
    {
        if ( pdTRUE != xQueueReceive(s_help, &h, portMAX_DELAY) )
        {
            continue;
        }
        if ( 0 == strcmp(h.key, "web") )
        {
            (void)web_start();
        }
        else if ( !kode_setting_set(h.key, h.value) )
        {
            KLOG_W(TAG, "setting %s not kept: %s", h.key, kode_error());
        }
    }
}

static void help(const char *key, const char *value)
{
    help_t h;
    (void)snprintf(h.key, sizeof(h.key), "%s", key);
    (void)snprintf(h.value, sizeof(h.value), "%s", value ? value : "");
    (void)xQueueSend(s_help, &h, 0);
}

static int setting_int(const char *key, int def)
{
    const char *v = kode_setting_get(key);
    return (v && v[0]) ? atoi(v) : def;
}

/* ---- small things ------------------------------------------------------------------------- */

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void toast(App * const me, const char *t)
{
    (void)snprintf(me->m.toast, sizeof(me->m.toast), "%s", t);
    me->toast_until = now_ms() + TOAST_MS;
}

static void buzz(uint16_t ms)
{
    (void)kode_sys_haptic_play_buzz(ms, 80);
}

/* ---- the player's world ------------------------------------------------------------------- */

static bool io_ready(void *c)
{
    (void)c;
    return hidkb_ready();
}

static int io_pending(void *c)
{
    (void)c;
    return hidkb_pending();
}

static bool io_send(void *c, kstroke_t k, uint16_t down)
{
    (void)c;
    return hidkb_send(k, down);
}

static void io_flush(void *c)
{
    (void)c;
    hidkb_flush();
}

static void io_event(void *c, sc_event_t ev, const char *t)
{
    App * const me = c;
    switch ( ev )
    {
    case SC_EV_BEEP:
        buzz(150);
        break;
    case SC_EV_ARMED:
        buzz(60);
        break;
    case SC_EV_START:
        buzz(40);
        KLOG_I(TAG, "running %s", t);
        break;
    case SC_EV_DONE:
        buzz(250);
        KLOG_I(TAG, "done: %s", t);
        break;
    case SC_EV_CHAIN:
        (void)snprintf(me->chain, sizeof(me->chain), "%s", t);
        me->chain_pending = true;
        break;
    case SC_EV_DISARMED:
        KLOG_I(TAG, "disarmed: %s", t);
        break;
    default:
        break;
    }
}

/** @brief Load profile @p i into the player (only while it is idle or done). */
static void load(App * const me, int i)
{
    if ( (i < 0) || (i >= store_count()) )
    {
        i = 0;
    }
    store_row_t r;
    if ( !store_row(i, &r) )
    {
        return;
    }
    char *text = heap_caps_malloc(STORE_TEXT_MAX, MALLOC_CAP_SPIRAM);
    if ( !text )
    {
        return;
    }
    (void)store_text(i, text, STORE_TEXT_MAX);
    (void)sc_parse(me->prog, text);
    free(text);
    if ( !me->prog->title[0] )
    {
        (void)snprintf(me->prog->title, sizeof(me->prog->title), "%s", r.title);
    }
    sc_load(&me->pl, me->prog);
    me->m.sel = i;
    if ( me->m.sel < me->m.top )
    {
        me->m.top = me->m.sel;
    }
    if ( me->m.sel >= me->m.top + UI_LIST_ROWS )
    {
        me->m.top = me->m.sel - UI_LIST_ROWS + 1;
    }
    if ( 0 != strcmp(me->sel_name, r.name) )
    {
        (void)snprintf(me->sel_name, sizeof(me->sel_name), "%s", r.name);
        help("bh_sel", r.name);
    }
}

static bool player_busy(const App * const me)
{
    const int st = sc_state(&me->pl);
    return (SC_IDLE != st) && (SC_DONE != st);
}

static void start(App * const me)
{
    if ( me->prog->nerr )
    {
        toast(me, "This profile has errors: fix it in the editor");
        return;
    }
    if ( !hidkb_active() )
    {
        toast(me, "The USB keyboard did not start");
        return;
    }
    sc_start(&me->pl, now_ms());
    if ( SC_IDLE != sc_state(&me->pl) )
    {
        me->m.view = UI_RUN;
        me->m.entry[0] = 0;
    }
}

static void send_now(App * const me, const char *name)
{
    kstroke_t k;
    if ( !keys_parse(name, &k) )
    {
        return;
    }
    if ( !hidkb_ready() )
    {
        toast(me, "No PC has the keyboard");
        return;
    }
    (void)hidkb_send(k, 40);
}

/* ---- the card ------------------------------------------------------------------------------- */

static void job_add(App * const me, job_op_t op, const char *name, char *buf, size_t len, uint32_t seq)
{
    if ( me->njob >= JOBS )
    {
        free(buf);
        if ( seq )
        {
            s_done_rc = KODE_NO_RESOURCES;
            s_done_seq = seq;
        }
        return;
    }
    job_t *j = &me->job[me->njob++];
    memset(j, 0, sizeof(*j));
    j->op = (uint8_t)op;
    j->seq = seq;
    j->buf = buf;
    j->len = len;
    if ( name )
    {
        (void)snprintf(j->name, sizeof(j->name), "%s", name);
    }
}

static void path_of(const char *name, char *out, size_t cap)
{
    (void)snprintf(out, cap, "%s/%s.txt", STORE_DIR, name);
}

static void job_done(App * const me, int rc)
{
    if ( me->cur.seq )
    {
        s_done_rc = rc;
        s_done_seq = me->cur.seq;
    }
    free(me->cur.buf);
    me->cur.buf = NULL;
    me->busy = false;
}

static void job_next(App * const me)
{
    if ( me->busy || (0 == me->njob) )
    {
        return;
    }
    me->cur = me->job[0];
    memmove(&me->job[0], &me->job[1], (size_t)(me->njob - 1) * sizeof(job_t));
    me->njob--;
    char path[KODE_SYS_FILE_PATH_MAX];
    kode_status_t rc = KODE_ERROR;
    switch ( me->cur.op )
    {
    case J_MKDIR:
        rc = kode_sys_file_create_folder(&me->super, STORE_DIR);
        break;
    case J_LIST:
        if ( 0 == me->list_skip )
        {
            me->nfound = 0;
        }
        rc = kode_sys_file_list(&me->super, STORE_DIR, me->list_skip);
        break;
    case J_READ:
        path_of(me->cur.name, path, sizeof(path));
        rc = kode_sys_file_read(&me->super, path, 0, me->readbuf, STORE_TEXT_MAX - 1u);
        break;
    case J_WRITE:
        path_of(me->cur.name, path, sizeof(path));
        rc = kode_sys_file_write(&me->super, path, me->cur.buf, (uint32_t)me->cur.len, false);
        break;
    case J_DELETE:
        path_of(me->cur.name, path, sizeof(path));
        rc = kode_sys_file_delete(&me->super, path);
        break;
    default:
        break;
    }
    me->busy = true;
    if ( KODE_OK != rc )
    {
        KLOG_W(TAG, "file job %d not taken: %d", me->cur.op, (int)rc);
        job_done(me, (int)rc);
    }
}

static void reload_card(App * const me)
{
    me->list_skip = 0;
    job_add(me, J_MKDIR, NULL, NULL, 0, 0);
    job_add(me, J_LIST, NULL, NULL, 0, 0);
}

static void after_store_change(App * const me)
{
    me->m.count = store_count();
    if ( !player_busy(me) )
    {
        const int i = store_find_name(me->sel_name);
        load(me, (i >= 0) ? i : me->m.sel);
    }
}

/* ---- commands from the web page ------------------------------------------------------------ */

static void do_cmd(App * const me, app_cmd_t *c)
{
    const uint32_t now = now_ms();
    switch ( c->op )
    {
    case CMD_START:
        start(me);
        break;
    case CMD_STOP:
        sc_stop(&me->pl);
        break;
    case CMD_GO:
        sc_go(&me->pl, now);
        break;
    case CMD_NEXT:
        sc_next(&me->pl, now);
        break;
    case CMD_YES:
    case CMD_NO:
        sc_confirm(&me->pl, CMD_YES == c->op, now);
        break;
    case CMD_SELECT:
        if ( player_busy(me) )
        {
            break;
        }
        {
            const int i = store_find_name(c->s);
            if ( i >= 0 )
            {
                load(me, i);
                me->m.view = UI_LIST;
            }
        }
        break;
    case CMD_CODE:
        sc_entry(&me->pl, c->s, now);
        me->m.entry[0] = 0;
        break;
    case CMD_KEY:
        send_now(me, c->s);
        break;
    case CMD_TYPE:
        if ( hidkb_ready() )
        {
            kstroke_t k;
            for ( const char *t = c->s; *t; ++t )
            {
                if ( keys_char(*t, &k) )
                {
                    (void)hidkb_send(k, 40);
                }
            }
        }
        break;
    case CMD_SAVE:
        job_add(me, J_WRITE, c->s, c->text, c->len, c->seq);
        c->text = NULL;
        break;
    case CMD_DELETE:
        job_add(me, J_DELETE, c->s, NULL, 0, c->seq);
        break;
    case CMD_RELOAD:
        reload_card(me);
        break;
    default:
        break;
    }
    free(c->text);
}

/* ---- the glass ------------------------------------------------------------------------------- */

static bool rows(int i, ui_row_t *out)
{
    store_row_t r;
    if ( !store_row(i, &r) )
    {
        return false;
    }
    (void)snprintf(out->title, sizeof(out->title), "%s", r.title);
    char sub[160];
    (void)snprintf(sub, sizeof(sub), "%s%s%s", r.arm, r.desc[0] ? "  -  " : "", r.desc);
    (void)snprintf(out->sub, sizeof(out->sub), "%.63s", sub);
    out->nerr = r.nerr;
    out->on_card = r.on_card;
    return true;
}

static void glass(App * const me)
{
    if ( !me->glass_up )
    {
        return;
    }
    if ( me->in_flight )
    {
        if ( !kdrv_display_flush_wait(0) )
        {
            return;
        }
        me->in_flight = false;
    }
    uint16_t * const back = kdrv_display_back_buffer();
    if ( !back )
    {
        return;
    }
    gfx_surf_t s;
    gfx_surf_init(&s, back, UI_W, UI_H, UI_W);
    me->m.now = now_ms();
    ui_draw(&s, &me->m);
    FlushEvt * const ev = Q_NEW_X(FlushEvt, 0U, DISPLAY_FLUSH_SIG);
    if ( !ev )
    {
        return;
    }
    ev->x1 = 0;
    ev->y1 = 0;
    ev->x2 = UI_W;
    ev->y2 = UI_H;
    ev->pixels = (uint8_t *)back;
    ev->last = true;
    QACTIVE_POST(AO_Display, &ev->super, &me->super);
    me->in_flight = true;
}

static void led(App * const me)
{
    const int st = sc_state(&me->pl);
    const bool blink = ((now_ms() / 500u) & 1u) != 0u;
    static int last = -1;
    int want;
    if ( (SC_ARMED == st) || me->pl.rearm_wait )
    {
        want = blink ? 1 : 0;
    }
    else if ( (SC_IDLE == st) || (SC_DONE == st) )
    {
        want = (SC_DONE == st) ? 3 : 0;
    }
    else
    {
        want = 2;
    }
    if ( want == last )
    {
        return;
    }
    last = want;
    static const uint8_t RGBS[4][3] = { { 0, 0, 0 }, { 60, 0, 0 }, { 60, 34, 0 }, { 0, 50, 10 } };
    (void)kode_sys_led_set_rgb(RGBS[want][0], RGBS[want][1], RGBS[want][2]);
}

static void snapshot(App * const me)
{
    snap_t s;
    memset(&s, 0, sizeof(s));
    const sc_player_t *pl = &me->pl;
    (void)snprintf(s.state, sizeof(s.state), "%s", sc_state_name(sc_state(pl)));
    (void)snprintf(s.title, sizeof(s.title), "%s", me->prog->title);
    (void)snprintf(s.name, sizeof(s.name), "%s", me->sel_name);
    (void)snprintf(s.text, sizeof(s.text), "%s", pl->text);
    (void)snprintf(s.note, sizeof(s.note), "%s", pl->note);
    s.step = pl->pc;
    s.steps = me->prog->n;
    s.left_ms = sc_left_ms(pl, now_ms());
    s.runs = pl->runs;
    s.need_unplug = pl->need_unplug;
    s.rearm_wait = pl->rearm_wait;
    s.entry_n = pl->entry_n;
    s.entry_send = pl->entry_send;
    s.host = hidkb_ready();
    s.kb = hidkb_active();
    s.port = (int)hidkb_port();
    s.card_ok = me->m.card_ok;
    s.card_files = me->m.card_files;
    (void)snprintf(s.ip, sizeof(s.ip), "%s", me->m.ip);
    xSemaphoreTake(s_snap_mx, portMAX_DELAY);
    s_snap = s;
    xSemaphoreGive(s_snap_mx);
}

/* ---- input ------------------------------------------------------------------------------------- */

static void act(App * const me, ui_act_t a)
{
    const uint32_t now = now_ms();
    static const char *const ARROW[5] = { "UP", "DOWN", "LEFT", "RIGHT", "ENTER" };
    switch ( a.kind )
    {
    case UA_SELECT:
        if ( a.arg == me->m.sel && (me->m.view == UI_LIST) && (UA_SELECT == a.kind) && (a.arg2 == 1) )
        {
            start(me);
        }
        else
        {
            load(me, a.arg);
        }
        break;
    case UA_START:
        start(me);
        break;
    case UA_AGAIN:
        start(me);
        break;
    case UA_STOP:
        sc_stop(&me->pl);
        me->m.view = UI_LIST;
        break;
    case UA_GO:
        sc_go(&me->pl, now);
        break;
    case UA_NEXT:
        sc_next(&me->pl, now);
        break;
    case UA_YES:
    case UA_NO:
        sc_confirm(&me->pl, UA_YES == a.kind, now);
        if ( UA_NO == a.kind )
        {
            me->m.view = UI_LIST;
        }
        break;
    case UA_DIGIT: {
        const size_t n = strlen(me->m.entry);
        const int max = me->pl.entry_send ? me->pl.entry_n : 16;
        if ( ((int)n < max) && (n + 1 < sizeof(me->m.entry)) )
        {
            me->m.entry[n] = (char)('0' + a.arg);
            me->m.entry[n + 1] = 0;
        }
        break;
    }
    case UA_BKSP: {
        const size_t n = strlen(me->m.entry);
        if ( n )
        {
            me->m.entry[n - 1] = 0;
        }
        break;
    }
    case UA_OK: {
        const int n = (int)strlen(me->m.entry);
        if ( me->pl.entry_send ? (n >= me->pl.entry_n) : (n > 0) )
        {
            sc_entry(&me->pl, me->m.entry, now);
            me->m.entry[0] = 0;
        }
        break;
    }
    case UA_KEY:
        send_now(me, ui_manual_key(a.arg));
        me->m.flash_key = a.arg;
        me->flash_until = now + 200u;
        break;
    case UA_ARROW:
        if ( (a.arg >= 0) && (a.arg < 5) )
        {
            send_now(me, ARROW[a.arg]);
        }
        break;
    case UA_VIEW:
        if ( (UI_LIST == a.arg) && (SC_DONE == sc_state(&me->pl)) )
        {
            if ( me->pl.rearm_wait )
            {
                sc_stop(&me->pl);
            }
            else
            {
                sc_load(&me->pl, me->prog);
            }
        }
        me->m.view = a.arg;
        break;
    case UA_SETTING:
        if ( -1 == a.arg )
        {
            me->m.set_sel = (me->m.set_sel + UI_SETTINGS - 1) % UI_SETTINGS;
            break;
        }
        if ( -2 == a.arg )
        {
            me->m.set_sel = (me->m.set_sel + 1) % UI_SETTINGS;
            break;
        }
        me->m.set_sel = a.arg;
        if ( 0 == a.arg )
        {
            me->m.port_next = !me->m.port_next;
            help("bh_port", me->m.port_next ? "1" : "0");
            toast(me, "The USB port changes when the app starts again");
        }
        else if ( 1 == a.arg )
        {
            me->m.web_next = !me->m.web_next;
            help("bh_web", me->m.web_next ? "1" : "0");
            if ( me->m.web_next && !web_running() && me->wifi_up )
            {
                help("web", NULL);
                me->m.web_on = true;
            }
            else
            {
                toast(me, "The editor changes when the app starts again");
            }
        }
        else if ( 2 == a.arg )
        {
            reload_card(me);
            toast(me, "Reading the card");
        }
        break;
    default:
        break;
    }
}

static void on_touch(App * const me, TouchEvt const *t)
{
    const uint32_t now = now_ms();
    if ( t->pressed && !me->touch_down )
    {
        me->touch_down = true;
        me->tx = me->tx0 = t->x;
        me->ty = me->ty0 = t->y;
        me->t_down = now;
        return;
    }
    if ( t->pressed )
    {
        me->tx = t->x;
        me->ty = t->y;
        return;
    }
    if ( !me->touch_down )
    {
        return;
    }
    me->touch_down = false;
    if ( t->stale || (now - me->t_down > TAP_MS) || (abs(me->tx - me->tx0) > TAP_SLOP)
         || (abs(me->ty - me->ty0) > TAP_SLOP) )
    {
        /* a swipe up or down on the list scrolls it */
        if ( (UI_LIST == me->m.view) && (abs(me->ty - me->ty0) > 40) && !player_busy(me) )
        {
            const int step = (me->ty < me->ty0) ? UI_LIST_ROWS - 1 : -(UI_LIST_ROWS - 1);
            int top = me->m.top + step;
            if ( top > me->m.count - UI_LIST_ROWS )
            {
                top = me->m.count - UI_LIST_ROWS;
            }
            me->m.top = (top < 0) ? 0 : top;
        }
        return;
    }
    act(me, ui_tap(&me->m, me->tx0, me->ty0));
}

/* ---- Wi-Fi ------------------------------------------------------------------------------------- */

static void on_wifi(App * const me, WifiStateEvt const *w)
{
    const bool up = (KDRV_WIFI_GOT_IP == w->state) && (0u != w->ip4);
    if ( up )
    {
        const uint8_t *ip = (const uint8_t *)&w->ip4;
        (void)snprintf(me->m.ip, sizeof(me->m.ip), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
        if ( !me->wifi_up && me->m.web_on && !me->web_asked )
        {
            me->web_asked = true;
            KLOG_I(TAG, "Wi-Fi up: the editor is at http://%s/", me->m.ip);
            help("web", NULL);
        }
    }
    me->wifi_up = up;
    me->m.wifi = up;
    if ( me->m.web_on && !up && !me->wifi_asked && !w->joining && !w->reconnecting )
    {
        me->wifi_asked = true;
        if ( !w->enabled )
        {
            wifi_start_request();
        }
        (void)kode_sys_wifi_connect(NULL, NULL);
    }
}

/* ---- the AO -------------------------------------------------------------------------------------- */

void App_ctor(void)
{
    App * const me = &l_app;
    QActive_ctor(&me->super, Q_STATE_CAST(&App_initial));
    QTimeEvt_ctorX(&me->tick, &me->super, APP_TICK_SIG, 0U);
}

void App_start(void)
{
    s_cmdq = xQueueCreate(8, sizeof(app_cmd_t));
    s_snap_mx = xSemaphoreCreateMutex();
    s_help = xQueueCreate(8, sizeof(help_t));
    xTaskCreatePinnedToCore(helper_task, "bh_help", 6144, NULL, 2, NULL, 1);
    QActive_setAttr(AO_App, TASK_NAME_ATTR, "bh_app");
    QACTIVE_START(AO_App, KODE_PRIO_USER_GUI, l_queue, Q_DIM(l_queue), s_stack, sizeof(s_stack), (void *)0);
}

static QState App_initial(App * const me, void const * const par)
{
    (void)par;
    me->prog = heap_caps_calloc(1, sizeof(sc_prog_t), MALLOC_CAP_SPIRAM);
    me->readbuf = heap_caps_malloc(STORE_TEXT_MAX, MALLOC_CAP_SPIRAM);
    const sc_io_t io = { io_ready, io_pending, io_send, io_flush, io_event, me };
    sc_player_init(&me->pl, &io);

    me->m.version = APP_VERSION;
    me->m.flash_key = -1;
    me->m.port = me->m.port_next = (1 == setting_int("bh_port", 0)) ? 1 : 0;
    me->m.web_on = me->m.web_next = (0 != setting_int("bh_web", 1));
    ui_set_rows(rows);

    me->m.kb_ok = hidkb_start((hidkb_port_t)me->m.port);
    if ( !me->m.kb_ok )
    {
        KLOG_E(TAG, "the USB keyboard did not start");
    }

    const char *sel = kode_setting_get("bh_sel");
    (void)snprintf(me->sel_name, sizeof(me->sel_name), "%s", sel ? sel : "");
    me->m.count = store_count();
    {
        const int i = store_find_name(me->sel_name);
        load(me, (i >= 0) ? i : 0);
    }
    reload_card(me);

    QActive_subscribe(&me->super, BUTTON_EVT_SIG);
    QActive_subscribe(&me->super, TOUCH_EVT_SIG);
    QActive_subscribe(&me->super, DISPLAY_STATE_SIG);
    QActive_subscribe(&me->super, WIFI_STATE_SIG);
    display_state_request();
    wifi_state_request();
    KLOG_I(TAG, "Dot BIOS Helper %s: %d profiles, keyboard %s", APP_VERSION, me->m.count,
           me->m.kb_ok ? "up" : "DOWN");
    QTimeEvt_armX(&me->tick, KQP_TICKS_PER_MS(TICK_MS), KQP_TICKS_PER_MS(TICK_MS));
    return Q_TRAN(&App_running);
}

static void tick(App * const me)
{
    const uint32_t now = now_ms();
    me->ticks++;

    app_cmd_t c;
    while ( pdTRUE == xQueueReceive(s_cmdq, &c, 0) )
    {
        do_cmd(me, &c);
    }

    const int before = sc_state(&me->pl);
    sc_tick(&me->pl, now);
    if ( me->chain_pending )
    {
        me->chain_pending = false;
        const int i = store_find_title(me->chain);
        if ( i < 0 )
        {
            toast(me, "after chain: no profile by that name");
        }
        else
        {
            load(me, i);
            sc_start(&me->pl, now);
            sc_go(&me->pl, now); /* same PC: no arming again */
        }
    }
    const int st = sc_state(&me->pl);
    if ( (SC_IDLE == st) && (SC_IDLE != before) )
    {
        if ( me->pl.note[0] )
        {
            toast(me, me->pl.note);
        }
        if ( UI_RUN == me->m.view )
        {
            me->m.view = UI_LIST;
        }
    }
    if ( (SC_IDLE != st) && (UI_LIST == me->m.view) )
    {
        me->m.view = UI_RUN; /* started from the web page */
    }
    if ( (SC_ENTRY != st) && me->m.entry[0] )
    {
        me->m.entry[0] = 0;
    }

    if ( store_changed() )
    {
        after_store_change(me);
    }
    if ( me->retry_at && ((int32_t)(now - me->retry_at) >= 0) )
    {
        me->retry_at = 0;
        reload_card(me);
    }
    job_next(me);

    if ( me->toast_until && ((int32_t)(now - me->toast_until) >= 0) )
    {
        me->toast_until = 0;
        me->m.toast[0] = 0;
    }
    if ( me->flash_until && ((int32_t)(now - me->flash_until) >= 0) )
    {
        me->flash_until = 0;
        me->m.flash_key = -1;
    }
    me->m.host = hidkb_ready();
    me->m.web_up = web_running() && me->wifi_up;
    me->m.pl = &me->pl;
    if ( 0 == me->ticks % DRAW_EVERY )
    {
        glass(me);
        led(me);
        snapshot(me);
    }
}

static QState App_running(App * const me, QEvt const * const e)
{
    switch ( e->sig )
    {
    case APP_TICK_SIG:
        tick(me);
        return Q_HANDLED();
    case BUTTON_EVT_SIG: {
        ButtonEvt const *b = (ButtonEvt const *)e;
        if ( !b->pressed || (b->id > KDRV_BTN_BOT) )
        {
            return Q_HANDLED();
        }
        if ( KDRV_BTN_BOT == b->id )
        {
            /* BOT: back to the list from the keyboard and the settings (held, kodeOS takes it home) */
            if ( (UI_KEYS == me->m.view) || (UI_SETTINGS_V == me->m.view) )
            {
                me->m.view = UI_LIST;
            }
            return Q_HANDLED();
        }
        act(me, ui_button(&me->m, (int)b->id));
        return Q_HANDLED();
    }
    case TOUCH_EVT_SIG:
        on_touch(me, (TouchEvt const *)e);
        return Q_HANDLED();
    case DISPLAY_STATE_SIG:
        me->glass_up = ((DisplayStateEvt const *)e)->up;
        return Q_HANDLED();
    case WIFI_STATE_SIG:
        on_wifi(me, (WifiStateEvt const *)e);
        return Q_HANDLED();
    case FILE_FOLDER_RSP_SIG:
        job_done(me, 0); /* "already there" is as good */
        return Q_HANDLED();
    case STORAGE_ENTRY_SIG: {
        StorageEntryEvt const *s = (StorageEntryEvt const *)e;
        const size_t n = strlen(s->name);
        if ( !s->is_dir && (n > 4u) && (0 == strcmp(s->name + n - 4u, ".txt")) && (me->nfound < CARD_FILES)
             && (n - 4u < STORE_NAME_MAX) )
        {
            char nm[STORE_NAME_MAX];
            memcpy(nm, s->name, n - 4u);
            nm[n - 4u] = 0;
            if ( store_name_ok(nm) )
            {
                (void)snprintf(me->found[me->nfound++], STORE_NAME_MAX, "%s", nm);
            }
        }
        return Q_HANDLED();
    }
    case FILE_LIST_END_SIG: {
        FileRspEvt const *r = (FileRspEvt const *)e;
        job_done(me, r->rc);
        if ( KODE_OK != r->rc )
        {
            /* the card is unmounted while the radio comes up: ask again in a while */
            me->m.card_ok = false;
            if ( me->retries < 20 )
            {
                me->retries++;
                me->retry_at = now_ms() + 3000u;
            }
            return Q_HANDLED();
        }
        me->retries = 0;
        if ( r->more )
        {
            me->list_skip = (uint16_t)(me->list_skip + r->count);
            job_add(me, J_LIST, NULL, NULL, 0, 0);
            return Q_HANDLED();
        }
        me->list_skip = 0;
        me->m.card_ok = true;
        me->m.card_files = me->nfound;
        store_clear_card();
        for ( int i = 0; i < me->nfound; ++i )
        {
            job_add(me, J_READ, me->found[i], NULL, 0, 0);
        }
        KLOG_I(TAG, "card: %d profiles in %s", me->nfound, STORE_DIR);
        return Q_HANDLED();
    }
    case FILE_READ_RSP_SIG: {
        FileRspEvt const *r = (FileRspEvt const *)e;
        if ( (KODE_OK == r->rc) && !store_put_card(me->cur.name, me->readbuf, r->got) )
        {
            KLOG_W(TAG, "%s not taken (too long, or the list is full)", me->cur.name);
        }
        job_done(me, r->rc);
        return Q_HANDLED();
    }
    case FILE_WRITE_RSP_SIG: {
        FileRspEvt const *r = (FileRspEvt const *)e;
        if ( KODE_OK == r->rc )
        {
            (void)store_put_card(me->cur.name, me->cur.buf, me->cur.len);
            me->m.card_ok = true;
            me->m.card_files = 0;
            for ( int i = 0; i < store_count(); ++i )
            {
                store_row_t row;
                if ( store_row(i, &row) && row.on_card )
                {
                    me->m.card_files++;
                }
            }
            if ( !player_busy(me) )
            {
                const int i = store_find_name(me->cur.name);
                if ( i >= 0 )
                {
                    load(me, i);
                }
            }
            toast(me, "Profile saved");
        }
        job_done(me, r->rc);
        return Q_HANDLED();
    }
    case FILE_DELETE_RSP_SIG: {
        FileRspEvt const *r = (FileRspEvt const *)e;
        if ( KODE_OK == r->rc )
        {
            store_remove_card(me->cur.name);
            if ( me->m.card_files > 0 )
            {
                me->m.card_files--;
            }
        }
        job_done(me, r->rc);
        return Q_HANDLED();
    }
    default:
        return Q_SUPER(&QHsm_top);
    }
}
