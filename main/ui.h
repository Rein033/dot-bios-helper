/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file ui.h
 * @brief The Dot's glass: what each view draws, and what a tap or a button means there.
 *
 * Pure C over gfx.h: the app fills a ui_model_t, ui_draw() paints it, ui_tap() / ui_button()
 * turn input into an action for the app to carry out. The host test renders it to PNGs.
 */
#ifndef UI_H_
#define UI_H_

#include <stdbool.h>
#include <stdint.h>

#include "gfx.h"
#include "script.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UI_W 410
#define UI_H 502
#define UI_LIST_ROWS 5
#define UI_MANUAL_KEYS 20
#define UI_SETTINGS 4

typedef enum {
    UI_LIST = 0,   /**< the profiles, Start */
    UI_RUN,        /**< what the player is doing (any state but idle) */
    UI_KEYS,       /**< the glass as a keyboard */
    UI_SETTINGS_V,
} ui_view_t;

typedef enum {
    UA_NONE = 0,
    UA_SELECT,      /**< arg: profile index */
    UA_START,
    UA_STOP,
    UA_GO,
    UA_NEXT,
    UA_YES,
    UA_NO,
    UA_DIGIT,       /**< arg: 0..9 */
    UA_BKSP,
    UA_OK,
    UA_KEY,         /**< arg: index into ui_manual_key() */
    UA_ARROW,       /**< arg: 0 up 1 down 2 left 3 right 4 enter: a key now (ask, keys view) */
    UA_VIEW,        /**< arg: ui_view_t */
    UA_SETTING,     /**< arg: row; dir in arg2 */
    UA_AGAIN,
} ui_act_kind_t;

typedef struct {
    int kind;
    int arg;
    int arg2;
} ui_act_t;

typedef enum {
    UB_UP = 0,
    UB_DOWN,
    UB_LEFT,
    UB_RIGHT,
    UB_TOP,
    UB_BOT,
} ui_btn_t;

typedef struct {
    int view;
    /* the list */
    int count;
    int sel;
    int top;
    /* settings */
    int set_sel;
    int port;          /**< hidkb_port_t */
    int port_next;     /**< what the next start uses */
    bool web_on;
    bool web_next;
    int card_files;
    bool card_ok;
    const char *version;
    /* the player */
    const sc_player_t *pl;
    uint32_t now;
    /* the world */
    bool kb_ok;        /**< the keyboard came up */
    bool host;         /**< a PC has taken it */
    bool wifi;
    bool web_up;
    char ip[16];
    /* the keypad */
    char entry[17];
    /* a line at the bottom for a while */
    char toast[64];
    int flash_key;     /**< keys view: the key just sent, -1 none */
} ui_model_t;

/** @brief Row data for the list; the app hands the store's rows through this. */
typedef struct {
    char title[48];
    char sub[64];
    int  nerr;
    bool on_card;
} ui_row_t;
typedef bool (*ui_row_fn)(int i, ui_row_t *out);

void ui_set_rows(ui_row_fn fn);
void ui_draw(gfx_surf_t *s, const ui_model_t *m);
ui_act_t ui_tap(const ui_model_t *m, int x, int y);
ui_act_t ui_button(const ui_model_t *m, int btn);
/** @brief The keys view's key @p i: its name for keys_parse(), and its label. */
const char *ui_manual_key(int i);
const char *ui_manual_label(int i);

#ifdef __cplusplus
}
#endif

#endif /* UI_H_ */
