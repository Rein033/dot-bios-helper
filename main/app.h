/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file app.h
 * @brief The app's Active Object: the player, the glass, the keyboard, the card, and the
 *        commands the web editor sends.
 */
#ifndef APP_H_
#define APP_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APP_VERSION "1.0.0"

typedef enum {
    CMD_START = 0,
    CMD_STOP,
    CMD_GO,
    CMD_NEXT,
    CMD_YES,
    CMD_NO,
    CMD_SELECT,   /**< s: profile file name */
    CMD_CODE,     /**< s: digits for the keypad */
    CMD_KEY,      /**< s: a key name, sent now */
    CMD_TYPE,     /**< s: text, typed now */
    CMD_SAVE,     /**< s: file name; text: owned by the app from now on */
    CMD_DELETE,   /**< s: file name */
    CMD_RELOAD,
} app_cmd_op_t;

typedef struct {
    int      op;
    uint32_t seq;     /**< for app_wait(); 0 = nobody waits */
    char     s[128];
    char    *text;    /**< CMD_SAVE: heap, freed by the app */
    size_t   len;
} app_cmd_t;

/** @brief From any task: queue a command. A save's text is the app's even when this fails. */
bool app_post(app_cmd_t *c);
/** @brief A number for app_post()'s seq. */
uint32_t app_seq(void);
/**
 * @brief Wait for the card operation numbered @p seq.
 * @return 0 when it worked, a kode_status_t when not, -1 on timeout.
 */
int app_wait(uint32_t seq, uint32_t timeout_ms);
/** @brief The state as JSON, for the web page. */
int app_state_json(char *buf, size_t cap);

void App_ctor(void);
void App_start(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_H_ */
