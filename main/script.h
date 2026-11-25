/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file script.h
 * @brief Profiles: a small key-script language, its parser and its player.
 *
 * Pure C, no SDK: the player talks to the keyboard through sc_io_t, so the host
 * test drives it with a fake PC. docs/profiles.md is the language's reference.
 *
 * A profile is text, one command a line, '#' starts a comment:
 *
 *   Settings (anywhere, usually at the top)
 *     title <text>               name in the list
 *     desc <text>                one line under the name
 *     arm manual|plug|delay <s>  how Start begins (see below)
 *     after stop|rearm|chain <profile title>
 *     confirm on|off             ask "Run on this PC?" before arming
 *     timeout <min>              an armed profile disarms itself after this (0 = never; default 30)
 *
 *   Commands
 *     spam <KEY> <secs> [every_ms]   tap KEY while the PC boots; the seconds count from the first
 *                                    key the PC takes (Next ends it early, 120 s cap)
 *     keys <KEY> [KEY*3] ...         tap keys one by one; KEY*N taps it N times
 *     hold <KEY> <ms>                press and hold
 *     type <text> / line <text>      type text (line adds ENTER); $name puts a variable in
 *     wait <ms|Ns>                   pause
 *     waitpc [secs]                  wait until a PC takes the keyboard (0 or none = for ever)
 *     ask <text>                     pause until Next; the pad works as arrows meanwhile
 *     msg <text>                     show a line and go on
 *     code [n] [prompt]              keypad on the glass; the digits are typed with ENTER
 *     input <name> [n] [prompt]      keypad into a variable, nothing typed
 *     set <name> <value>             a variable
 *     repeat <n> ... end             a loop (0 = until Stop)
 *     gap <ms>                       time between keys (default 300)
 *     tap <ms>                       how long a key is down (default 40)
 *     beep                           a buzz on the Dot
 *
 *   arm manual  Start runs at once (keys wait for a PC to take them).
 *   arm plug    Start arms: the script begins when a PC takes the keyboard, the moment it boots
 *               or the cable goes in. Already connected when armed: unplug or power the PC off first.
 *   arm delay N Start counts N seconds down, then runs.
 *   after rearm the profile arms again once the cable comes out: one PC after another.
 */
#ifndef SCRIPT_H_
#define SCRIPT_H_

#include <stdbool.h>
#include <stdint.h>

#include "keys.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SC_CMDS      64     /**< commands in one profile */
#define SC_TEXT      96     /**< longest text of one command */
#define SC_NAME      16     /**< a variable's name */
#define SC_ERRS      8      /**< parse errors kept */
#define SC_VARS      8
#define SC_QUEUE     128    /**< strokes one command can queue */
#define SC_LOOPS     4      /**< nested repeats */
#define SC_SPAM_CAP_MS 120000u

typedef enum {
    SC_ARM_MANUAL = 0,
    SC_ARM_PLUG,
    SC_ARM_DELAY,
} sc_arm_t;

typedef enum {
    SC_AFTER_STOP = 0,
    SC_AFTER_REARM,
    SC_AFTER_CHAIN,
} sc_after_t;

typedef enum {
    SC_OP_SPAM = 0,
    SC_OP_KEYS,
    SC_OP_HOLD,
    SC_OP_TYPE,
    SC_OP_LINE,
    SC_OP_WAIT,
    SC_OP_WAITPC,
    SC_OP_ASK,
    SC_OP_MSG,
    SC_OP_CODE,
    SC_OP_INPUT,
    SC_OP_SET,
    SC_OP_REPEAT,
    SC_OP_END,
    SC_OP_GAP,
    SC_OP_TAP,
    SC_OP_BEEP,
} sc_op_t;

typedef struct {
    uint8_t  op;
    uint16_t line;            /**< 1-based line in the text */
    int32_t  a;
    int32_t  b;
    char     name[SC_NAME];
    char     s[SC_TEXT];
} sc_cmd_t;

typedef struct {
    uint16_t line;
    char     msg[56];
} sc_err_t;

typedef struct {
    char       title[48];
    char       desc[SC_TEXT];
    uint8_t    arm;           /**< sc_arm_t */
    int32_t    arm_delay_s;
    uint8_t    after;         /**< sc_after_t */
    char       chain[48];
    bool       confirm;
    int32_t    timeout_min;
    int        n;
    sc_cmd_t   cmd[SC_CMDS];
    int        nerr;          /**< all errors, even past SC_ERRS */
    sc_err_t   err[SC_ERRS];
} sc_prog_t;

/**
 * @brief Parse a profile.
 * @param p    Filled in; on errors the good commands are kept, but a profile with errors should
 *             not be run.
 * @param text NUL-terminated.
 * @return The number of errors (p->nerr).
 */
int sc_parse(sc_prog_t *p, const char *text);

/** @brief One-word name of the arm mode, for the list: "manual", "plug", "delay 5 s". */
void sc_arm_label(const sc_prog_t *p, char *buf, int cap);

/* ---- the player ----------------------------------------------------------------------- */

typedef enum {
    SC_IDLE = 0,
    SC_CONFIRM,     /**< waiting for yes / no */
    SC_ARMED,       /**< waiting for a PC to take the keyboard */
    SC_COUNTDOWN,   /**< arm delay */
    SC_RUN,         /**< sending keys, waiting */
    SC_SPAM,
    SC_ASK,
    SC_ENTRY,       /**< code / input: the keypad */
    SC_WAITPC,
    SC_DONE,
} sc_state_t;

typedef enum {
    SC_EV_BEEP = 0,
    SC_EV_ARMED,
    SC_EV_START,
    SC_EV_DONE,
    SC_EV_CHAIN,     /**< text: the profile to run next */
    SC_EV_DISARMED,  /**< text: why */
} sc_event_t;

/** @brief The keyboard and the app, as the player sees them. */
typedef struct {
    bool (*ready)(void *ctx);                                  /**< a PC has taken the keyboard */
    int (*pending)(void *ctx);                                 /**< strokes not yet sent */
    bool (*send)(void *ctx, kstroke_t k, uint16_t down_ms);    /**< queue one press and release */
    void (*flush)(void *ctx);
    void (*event)(void *ctx, sc_event_t ev, const char *text); /**< may be NULL */
    void *ctx;
} sc_io_t;

typedef struct {
    const sc_prog_t *p;
    sc_io_t  io;
    int      state;
    int      pc;
    uint32_t until;
    uint32_t gap;
    uint32_t tap;
    /* spam */
    kstroke_t spam_key;
    uint32_t spam_ms;
    uint32_t spam_every;
    uint32_t spam_cap;
    uint32_t next_tap;
    /* strokes of the current command */
    kstroke_t q[SC_QUEUE];
    uint16_t qdown;
    int      qn;
    int      qi;
    bool     q_counted;      /**< the command's pc++ was done already (code) */
    /* repeats */
    struct {
        int     start;
        int32_t left;        /**< -1: for ever */
    } loop[SC_LOOPS];
    int      depth;
    /* variables */
    struct {
        char name[SC_NAME];
        char val[40];
    } var[SC_VARS];
    int      nvar;
    /* the keypad */
    int      entry_n;
    bool     entry_send;
    char     entry_var[SC_NAME];
    /* arming */
    bool     need_unplug;    /**< plug mode: connected at arm time, wait for it to go first */
    uint32_t armed_at;
    bool     rearm_wait;     /**< done, after rearm: waiting for the cable to come out */
    int      runs;           /**< finished runs since Start */
    char     text[SC_TEXT];  /**< what the screen shows: the ask, the key, the message */
    char     note[64];       /**< the last thing worth saying: "Disarmed: timeout" */
} sc_player_t;

void sc_player_init(sc_player_t *pl, const sc_io_t *io);
/** @brief Take a parsed profile (kept by the caller) and go idle. */
void sc_load(sc_player_t *pl, const sc_prog_t *p);
/** @brief Start: confirm, arm, count down or run, as the profile says. */
void sc_start(sc_player_t *pl, uint32_t now);
/** @brief The confirm question's answer. */
void sc_confirm(sc_player_t *pl, bool yes, uint32_t now);
/** @brief Armed or counting down: run now. */
void sc_go(sc_player_t *pl, uint32_t now);
/** @brief Stop or disarm, back to idle. */
void sc_stop(sc_player_t *pl);
/** @brief Spam, ask, waitpc: go on. */
void sc_next(sc_player_t *pl, uint32_t now);
/** @brief The keypad's digits (code or input). */
void sc_entry(sc_player_t *pl, const char *digits, uint32_t now);
void sc_tick(sc_player_t *pl, uint32_t now);

int sc_state(const sc_player_t *pl);
/** @brief Milliseconds left of a spam, a wait, a countdown or the armed timeout; 0 if none. */
uint32_t sc_left_ms(const sc_player_t *pl, uint32_t now);
/** @brief A one-word name of a state, for the web page's JSON. */
const char *sc_state_name(int st);

#ifdef __cplusplus
}
#endif

#endif /* SCRIPT_H_ */
