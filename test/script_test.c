/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * Host test of the profile language: the parser and the player against a fake PC.
 *
 *   cc -std=c11 -Wall -Wextra -Werror -I../main script_test.c ../main/script.c ../main/keys.c ../main/builtin.c && ./a.out
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "builtin.h"
#include "script.h"

static int fails;
#define CHECK(c)                                                              \
    do                                                                        \
    {                                                                         \
        if ( !(c) )                                                           \
        {                                                                     \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);               \
            fails++;                                                          \
        }                                                                     \
    } while ( 0 )

/* ---- a fake PC ------------------------------------------------------------------------- */

typedef struct {
    bool      up;
    int       n;
    kstroke_t k[512];
    uint16_t  down[512];
    uint32_t  at[512];
    uint32_t  now;
    int       ev[16];
    char      chain[64];
} pc_t;

static bool f_ready(void *c) { return ((pc_t *)c)->up; }
static int f_pending(void *c) { (void)c; return 0; }
static bool f_send(void *c, kstroke_t k, uint16_t down)
{
    pc_t *pc = c;
    if ( pc->n < 512 )
    {
        pc->k[pc->n] = k;
        pc->down[pc->n] = down;
        pc->at[pc->n] = pc->now;
        pc->n++;
    }
    return true;
}
static void f_flush(void *c) { (void)c; }
static void f_event(void *c, sc_event_t ev, const char *t)
{
    pc_t *pc = c;
    pc->ev[ev]++;
    if ( SC_EV_CHAIN == ev )
    {
        snprintf(pc->chain, sizeof(pc->chain), "%s", t);
    }
}

static pc_t pc;
static sc_prog_t prog;
static sc_player_t pl;

static void setup(const char *text, bool up)
{
    memset(&pc, 0, sizeof(pc));
    pc.up = up;
    const sc_io_t io = { f_ready, f_pending, f_send, f_flush, f_event, &pc };
    int e = sc_parse(&prog, text);
    if ( e )
    {
        printf("  parse errors in test text: %d (line %d: %s)\n", e, prog.err[0].line, prog.err[0].msg);
    }
    sc_player_init(&pl, &io);
    sc_load(&pl, &prog);
}

/* run for ms, 10 ms a tick */
static void run(uint32_t ms)
{
    for ( uint32_t t = 0; t < ms; t += 10 )
    {
        pc.now += 10;
        sc_tick(&pl, pc.now);
    }
}

static bool stroke(int i, uint8_t mod, uint8_t key)
{
    return (i < pc.n) && (pc.k[i].mod == mod) && (pc.k[i].key == key);
}

/* ---- tests ----------------------------------------------------------------------------- */

static void t_keys(void)
{
    kstroke_t k;
    CHECK(keys_parse("F10", &k) && (0x43 == k.key) && (0 == k.mod));
    CHECK(keys_parse("enter", &k) && (0x28 == k.key));
    CHECK(keys_parse("CTRL+ALT+DEL", &k) && (0x4C == k.key) && ((KEY_MOD_LCTRL | KEY_MOD_LALT) == k.mod));
    CHECK(keys_parse("WIN+r", &k) && (0x15 == k.key) && (KEY_MOD_LGUI == k.mod));
    CHECK(keys_parse("A", &k) && (0x04 == k.key) && (KEY_MOD_LSHIFT == k.mod));
    CHECK(keys_parse("?", &k) && (0x38 == k.key) && (KEY_MOD_LSHIFT == k.mod));
    CHECK(keys_parse("CTRL++", &k) && (KEY_MOD_LCTRL & k.mod) && (0x2E == k.key));
    CHECK(keys_parse("WIN", &k) && (0 == k.key) && (KEY_MOD_LGUI == k.mod));
    CHECK(!keys_parse("F99", &k));
    CHECK(!keys_parse("a+b", &k));
    CHECK(!keys_parse("CTRL+", &k));
    char nm[32];
    keys_parse("ctrl+alt+delete", &k);
    keys_name(k, nm, sizeof(nm));
    CHECK(0 == strcmp(nm, "CTRL+ALT+DEL"));
    keys_parse("x", &k);
    keys_name(k, nm, sizeof(nm));
    CHECK(0 == strcmp(nm, "X"));
}

static void t_parse_errors(void)
{
    int e = sc_parse(&prog, "title T\nkeys F10 BOGUS\nfrobnicate\nrepeat 2\nwait 1s\n");
    CHECK(3 == e);
    CHECK((2 == prog.err[0].line) && strstr(prog.err[0].msg, "BOGUS"));
    CHECK((3 == prog.err[1].line) && strstr(prog.err[1].msg, "frobnicate"));
    CHECK(strstr(prog.err[2].msg, "repeat without end"));
    CHECK(0 == strcmp(prog.title, "T"));
    e = sc_parse(&prog, "end\n");
    CHECK(1 == e);
    e = sc_parse(&prog, "arm delay 0\nafter chain\nconfirm maybe\ntimeout -1\nspam F10 0\nhold F1 5\n");
    CHECK(6 == e);
    e = sc_parse(&prog, "# only a comment\n\n   \n");
    CHECK((0 == e) && (0 == prog.n));
}

static void t_builtins_parse(void)
{
    for ( int i = 0; i < builtin_count(); ++i )
    {
        int e = sc_parse(&prog, builtin_text(i));
        if ( e )
        {
            printf("  builtin %s: line %d %s\n", builtin_name(i), prog.err[0].line, prog.err[0].msg);
        }
        CHECK(0 == e);
        CHECK(prog.title[0]);
    }
}

static void t_manual_keys(void)
{
    setup("gap 100\nkeys RIGHT RIGHT UP ENTER DOWN*3\n", true);
    sc_start(&pl, pc.now);
    CHECK(SC_RUN == sc_state(&pl));
    run(2000);
    CHECK(SC_DONE == sc_state(&pl));
    CHECK(7 == pc.n);
    CHECK(stroke(0, 0, 0x4F) && stroke(2, 0, 0x52) && stroke(3, 0, 0x28) && stroke(6, 0, 0x51));
    CHECK((pc.at[1] - pc.at[0]) == 100);
    CHECK(1 == pc.ev[SC_EV_DONE]);
}

static void t_keys_wait_for_pc(void)
{
    setup("keys F1 F2\n", false);
    sc_start(&pl, pc.now);
    run(5000);
    CHECK(0 == pc.n);
    CHECK(SC_RUN == sc_state(&pl));
    CHECK(strstr(pl.text, "Waiting"));
    pc.up = true;
    run(2000);
    CHECK(2 == pc.n);
    CHECK(SC_DONE == sc_state(&pl));
}

static void t_spam(void)
{
    setup("spam F10 2\nkeys ENTER\n", false);
    sc_start(&pl, pc.now);
    run(3000);
    CHECK(0 == pc.n); /* the PC is off: nothing, and the clock waits */
    CHECK(SC_SPAM == sc_state(&pl));
    pc.up = true;
    run(1000);
    const int after1s = pc.n;
    CHECK((after1s >= 3) && (after1s <= 5)); /* every 250 ms */
    run(1500);
    CHECK(SC_DONE == sc_state(&pl));
    CHECK(stroke(pc.n - 1, 0, 0x28));
    CHECK(stroke(0, 0, 0x43));
}

static void t_spam_next(void)
{
    setup("spam DEL 30 100\nkeys ESC\n", true);
    sc_start(&pl, pc.now);
    run(500);
    sc_next(&pl, pc.now);
    run(1000);
    CHECK(SC_DONE == sc_state(&pl));
    CHECK(stroke(pc.n - 1, 0, 0x29));
}

static void t_plug_arm(void)
{
    setup("arm plug\ntimeout 1\nkeys F12\n", true);
    sc_start(&pl, pc.now);
    CHECK(SC_ARMED == sc_state(&pl));
    run(2000);
    CHECK(SC_ARMED == sc_state(&pl)); /* was connected at arm time: no run */
    CHECK(0 == pc.n);
    pc.up = false;
    run(500);
    CHECK(SC_ARMED == sc_state(&pl));
    pc.up = true; /* the PC boots */
    run(1000);
    CHECK(SC_DONE == sc_state(&pl));
    CHECK(1 == pc.n);

    setup("arm plug\ntimeout 1\nkeys F12\n", false);
    sc_start(&pl, pc.now);
    run(61000);
    CHECK(SC_IDLE == sc_state(&pl));
    CHECK(strstr(pl.note, "Disarmed"));
    CHECK(1 == pc.ev[SC_EV_DISARMED]);
}

static void t_rearm_batch(void)
{
    setup("arm plug\nafter rearm\nkeys F9\n", false);
    sc_start(&pl, pc.now);
    pc.up = true;
    run(500);
    CHECK(SC_DONE == sc_state(&pl));
    run(500);
    CHECK(SC_DONE == sc_state(&pl)); /* still plugged */
    pc.up = false;
    run(100);
    CHECK(SC_ARMED == sc_state(&pl));
    pc.up = true;
    run(500);
    CHECK(SC_DONE == sc_state(&pl));
    CHECK(2 == pl.runs);
    CHECK(2 == pc.n);
    sc_stop(&pl);
    CHECK(SC_IDLE == sc_state(&pl));
}

static void t_delay_confirm(void)
{
    setup("arm delay 3\nconfirm on\nkeys A\n", true);
    sc_start(&pl, pc.now);
    CHECK(SC_CONFIRM == sc_state(&pl));
    sc_confirm(&pl, false, pc.now);
    CHECK(SC_IDLE == sc_state(&pl));
    sc_start(&pl, pc.now);
    sc_confirm(&pl, true, pc.now);
    CHECK(SC_COUNTDOWN == sc_state(&pl));
    run(2500);
    CHECK(0 == pc.n);
    CHECK(sc_left_ms(&pl, pc.now) <= 600);
    run(1000);
    CHECK(SC_DONE == sc_state(&pl));
    CHECK(stroke(0, KEY_MOD_LSHIFT, 0x04));
}

static void t_code_input_vars(void)
{
    setup("code 4\ninput tag 6 Asset tag\nset who IT-$tag\nline $who\n", true);
    sc_start(&pl, pc.now);
    run(50);
    CHECK(SC_ENTRY == sc_state(&pl));
    CHECK(4 == pl.entry_n);
    sc_entry(&pl, "1234", pc.now);
    run(5000);
    CHECK(SC_ENTRY == sc_state(&pl));
    CHECK(0 == strcmp(pl.text, "Asset tag"));
    CHECK(5 == pc.n); /* 1 2 3 4 ENTER */
    CHECK(stroke(0, 0, 0x1E) && stroke(4, 0, 0x28));
    sc_entry(&pl, "99", pc.now);
    run(10000);
    CHECK(SC_DONE == sc_state(&pl));
    /* "IT-99" + ENTER */
    CHECK(11 == pc.n);
    CHECK(stroke(5, KEY_MOD_LSHIFT, 0x0C) && stroke(7, 0, 0x2D) && stroke(9, 0, 0x26) && stroke(10, 0, 0x28));
}

static void t_repeat(void)
{
    setup("gap 0\nrepeat 3\nkeys TAB\nrepeat 2\nkeys DOWN\nend\nend\nkeys ENTER\n", true);
    sc_start(&pl, pc.now);
    run(3000);
    CHECK(SC_DONE == sc_state(&pl));
    CHECK(10 == pc.n); /* 3 x (TAB + 2 DOWN) + ENTER */
    CHECK(stroke(0, 0, 0x2B) && stroke(1, 0, 0x51) && stroke(3, 0, 0x2B) && stroke(9, 0, 0x28));

    setup("gap 0\nrepeat 0\nkeys F5\nend\n", true);
    sc_start(&pl, pc.now);
    run(3000);
    CHECK(SC_RUN == sc_state(&pl));
    CHECK(pc.n > 50);
    sc_stop(&pl);
    CHECK(SC_IDLE == sc_state(&pl));
}

static void t_type_hold_ask(void)
{
    setup("type Ab!\nhold F2 1500\nask Is the menu open?\nkeys ESC\n", true);
    sc_start(&pl, pc.now);
    run(3000);
    CHECK(stroke(0, KEY_MOD_LSHIFT, 0x04) && stroke(1, 0, 0x05) && stroke(2, KEY_MOD_LSHIFT, 0x1E));
    CHECK(stroke(3, 0, 0x3B) && (1500 == pc.down[3]));
    CHECK(SC_ASK == sc_state(&pl));
    CHECK(0 == strcmp(pl.text, "Is the menu open?"));
    sc_next(&pl, pc.now);
    run(1000);
    CHECK(SC_DONE == sc_state(&pl));
    CHECK(stroke(4, 0, 0x29));
}

static void t_waitpc_chain_beep(void)
{
    setup("waitpc 2\nbeep\nkeys F1\nafter chain Next one\n", false);
    sc_start(&pl, pc.now);
    run(1000);
    CHECK(SC_WAITPC == sc_state(&pl));
    run(1500); /* the timeout passes */
    CHECK(1 == pc.ev[SC_EV_BEEP]);
    pc.up = true;
    run(1000);
    CHECK(SC_DONE == sc_state(&pl));
    CHECK(1 == pc.ev[SC_EV_CHAIN]);
    CHECK(0 == strcmp(pc.chain, "Next one"));
}

static void t_wait_units(void)
{
    setup("wait 1.5s\nkeys F1\n", true);
    sc_start(&pl, pc.now);
    run(1400);
    CHECK(0 == pc.n);
    run(300);
    CHECK(1 == pc.n);
}

static void t_errors_block_start(void)
{
    setup("keys NOPE\n", true);
    sc_start(&pl, pc.now);
    CHECK(SC_IDLE == sc_state(&pl));
}

int main(void)
{
    t_keys();
    t_parse_errors();
    t_builtins_parse();
    t_manual_keys();
    t_keys_wait_for_pc();
    t_spam();
    t_spam_next();
    t_plug_arm();
    t_rearm_batch();
    t_delay_confirm();
    t_code_input_vars();
    t_repeat();
    t_type_hold_ask();
    t_waitpc_chain_beep();
    t_wait_units();
    t_errors_block_start();
    printf("%s (%d failed)\n", fails ? "FAILED" : "all passed", fails);
    return fails ? 1 : 0;
}
