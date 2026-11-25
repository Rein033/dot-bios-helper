/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bios.c
 * @brief The BIOS helper's profiles and player (see bios.h).
 */
#include "bios.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "builtin.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "hidkb.h"

static const char *TAG = "bios";

#define TEXT_MAX 4096

/* HP t550, order from Thomas (2026-11-24). Bump DEFAULT_TAG when it changes: a /data/bios.txt
 * that still carries an older default tag gets replaced. v6: the profile language's arm plug. */
#define DEFAULT_TAG "# default v6"
static const char DEFAULT_SCRIPT[] =
    "title HP t550: Secure Boot off\n"
    DEFAULT_TAG " (edit freely; remove this line to keep your own version)\n"
    "desc F10 into setup, the Secure Boot page, save with F10, then F9\n"
    "arm plug\n"
    "gap 200\n"
    "spam F10 10\n"
    "wait 2000\n"
    "keys RIGHT RIGHT UP ENTER F10 RIGHT F10 F10 ENTER ENTER\n"
    "spam F9 10\n";

/* profiles: files first, then the built-ins (the t550 one is /data/bios.txt already) */
#define BP_MAX 24
EXT_RAM_BSS_ATTR static char s_path[BP_MAX][64];   /* "" = built in */
EXT_RAM_BSS_ATTR static int8_t s_builtin[BP_MAX];
static int s_n = 1, s_prof;

EXT_RAM_BSS_ATTR static sc_prog_t s_prog;
EXT_RAM_BSS_ATTR static sc_player_t s_pl;
EXT_RAM_BSS_ATTR static char s_text[TEXT_MAX];
static unsigned s_ev;
static char s_chain[48];
static bool s_chain_pending;

/* ---- the player's world ---------------------------------------------------------------- */

static bool io_ready(void *c) { (void)c; return hidkb_ready(); }
static int io_pending(void *c) { (void)c; return hidkb_pending(); }
static bool io_send(void *c, kstroke_t k, uint16_t down) { (void)c; return hidkb_tap_hold(k.mod, k.key, down); }
static void io_flush(void *c) { (void)c; hidkb_flush(); }

static void io_event(void *c, sc_event_t ev, const char *t)
{
    (void)c;
    switch ( ev )
    {
    case SC_EV_BEEP: s_ev |= BIOS_EV_BEEP; break;
    case SC_EV_START: s_ev |= BIOS_EV_START; break;
    case SC_EV_DONE: s_ev |= BIOS_EV_DONE; break;
    case SC_EV_ARMED: s_ev |= BIOS_EV_ARMED; break;
    case SC_EV_CHAIN:
        (void)snprintf(s_chain, sizeof(s_chain), "%s", t);
        s_chain_pending = true;
        break;
    default: break;
    }
}

/* ---- files ------------------------------------------------------------------------------- */

static void read_file(const char *path)
{
    s_text[0] = 0;
    FILE *f = fopen(path, "r");
    if ( !f ) { return; }
    size_t const n = fread(s_text, 1, sizeof(s_text) - 1, f);
    fclose(f);
    s_text[n] = 0;
}

static void write_default(void)
{
    FILE *f = fopen(BIOS_PATH, "w");
    if ( f ) { fputs(DEFAULT_SCRIPT, f); fclose(f); }
    (void)snprintf(s_text, sizeof(s_text), "%s", DEFAULT_SCRIPT);
}

static void load_default_file(void)
{
    read_file(BIOS_PATH);
    if ( !s_text[0] || strstr(s_text, "VOORLOPIG") || (strstr(s_text, "# default v") && !strstr(s_text, DEFAULT_TAG)) )
    {
        write_default();
        ESP_LOGI(TAG, "default profile written");
    }
}

static void scan(void)
{
    snprintf(s_path[0], sizeof(s_path[0]), "%s", BIOS_PATH);
    s_builtin[0] = -1;
    s_n = 1;
    DIR *d = opendir(BIOS_DIR);
    if ( d )
    {
        struct dirent *e;
        while ( (e = readdir(d)) && (s_n < BP_MAX) )
        {
            size_t const l = strlen(e->d_name);
            if ( (l < 5) || (l > 40) || (0 != strcasecmp(e->d_name + l - 4, ".txt")) ) { continue; }
            snprintf(s_path[s_n], sizeof(s_path[0]), "%s/%s", BIOS_DIR, e->d_name);
            s_builtin[s_n++] = -1;
        }
        closedir(d);
        for ( int i = 2; i < s_n; ++i )   /* insertion sort, /data/bios.txt stays first */
        {
            for ( int j = i; (j > 1) && (strcasecmp(s_path[j - 1], s_path[j]) > 0); --j )
            {
                char t[64];
                memcpy(t, s_path[j], sizeof(t));
                memcpy(s_path[j], s_path[j - 1], sizeof(t));
                memcpy(s_path[j - 1], t, sizeof(t));
            }
        }
    }
    for ( int b = 0; (b < builtin_count()) && (s_n < BP_MAX); ++b )
    {
        if ( 0 == strcmp(builtin_name(b), "hp-t550-secure-boot") ) { continue; }
        /* a file /data/bios/<name>.txt replaces the built-in of that name */
        char want[64];
        snprintf(want, sizeof(want), "%s/%s.txt", BIOS_DIR, builtin_name(b));
        bool shadowed = false;
        for ( int i = 1; i < s_n; ++i ) { if ( 0 == strcasecmp(s_path[i], want) ) { shadowed = true; } }
        if ( shadowed ) { continue; }
        s_path[s_n][0] = 0;
        s_builtin[s_n++] = (int8_t)b;
    }
}

void bios_load(void)
{
    static bool init;
    if ( !init )
    {
        const sc_io_t io = { io_ready, io_pending, io_send, io_flush, io_event, NULL };
        sc_player_init(&s_pl, &io);
        init = true;
    }
    scan();
    if ( (s_prof < 0) || (s_prof >= s_n) ) { s_prof = 0; }
    if ( 0 == s_prof ) { load_default_file(); }
    else if ( s_builtin[s_prof] >= 0 ) { snprintf(s_text, sizeof(s_text), "%s", builtin_text(s_builtin[s_prof])); }
    else { read_file(s_path[s_prof]); }
    (void)sc_parse(&s_prog, s_text);
    if ( !s_prog.title[0] )   /* no title line: the file name */
    {
        const char *b = strrchr(s_path[s_prof], '/');
        snprintf(s_prog.title, sizeof(s_prog.title), "%s", b ? b + 1 : s_path[s_prof]);
        char *dot = strrchr(s_prog.title, '.');
        if ( dot ) { *dot = 0; }
    }
    sc_load(&s_pl, &s_prog);
    ESP_LOGI(TAG, "%s: %d commands, %d errors", s_prog.title, s_prog.n, s_prog.nerr);
}

void bios_select(int i)
{
    s_prof = i;
    bios_load();
}

int bios_profile(void) { return s_prof; }
int bios_profiles(void) { return s_n; }
const char *bios_path(void) { return s_path[s_prof][0] ? s_path[s_prof] : "built in"; }
const char *bios_title(void) { return s_prog.title; }
const char *bios_desc(void) { return s_prog.desc; }
int bios_errors(void) { return s_prog.nerr; }
int bios_error_line(void) { return s_prog.nerr ? s_prog.err[0].line : 0; }
const char *bios_error(void) { return s_prog.nerr ? s_prog.err[0].msg : ""; }
void bios_arm_label(char *buf, int cap) { sc_arm_label(&s_prog, buf, cap); }
bool bios_arm_plug(void) { return SC_ARM_PLUG == s_prog.arm; }

/* ---- playing ----------------------------------------------------------------------------- */

void bios_start(uint32_t now) { sc_start(&s_pl, now); }
void bios_confirm(bool yes, uint32_t now) { sc_confirm(&s_pl, yes, now); }
void bios_go(uint32_t now) { sc_go(&s_pl, now); }
void bios_stop(void) { if ( SC_IDLE != sc_state(&s_pl) ) { sc_stop(&s_pl); } }
void bios_next(uint32_t now) { sc_next(&s_pl, now); }
void bios_entry(const char *digits, uint32_t now) { sc_entry(&s_pl, digits, now); }
int bios_state(void) { return sc_state(&s_pl); }
const sc_player_t *bios_player(void) { return &s_pl; }

unsigned bios_take_events(void)
{
    unsigned const e = s_ev;
    s_ev = 0;
    return e;
}

void bios_tick(uint32_t now)
{
    sc_tick(&s_pl, now);
    if ( s_chain_pending )
    {
        s_chain_pending = false;
        for ( int i = 0; i < s_n; ++i )
        {
            int const keep = s_prof;
            s_prof = i;
            bios_load();
            if ( 0 == strcasecmp(s_prog.title, s_chain) )
            {
                sc_start(&s_pl, now);
                sc_go(&s_pl, now);   /* the same PC: no arming again */
                return;
            }
            s_prof = keep;
        }
        bios_load();
        ESP_LOGW(TAG, "after chain: no profile '%s'", s_chain);
    }
}
