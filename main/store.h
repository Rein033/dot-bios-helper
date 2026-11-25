/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file store.h
 * @brief The profiles the app knows: the ones on the microSD card (/sdcard/bioshelper/<name>.txt)
 *        and the built-in ones. A card file with a built-in's name replaces it.
 *
 * Shared by the app's Active Object and the web server's task, so every call takes a lock and
 * copies out. Pure C apart from the lock (FreeRTOS on the board, nothing on the host).
 */
#ifndef STORE_H_
#define STORE_H_

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STORE_MAX      32      /**< profiles in all */
#define STORE_TEXT_MAX 4096    /**< bytes of one profile */
#define STORE_NAME_MAX 40      /**< a file name without .txt */
#define STORE_DIR      "/sdcard/bioshelper"

typedef struct {
    char name[STORE_NAME_MAX];
    char title[48];
    char desc[96];
    char arm[24];      /**< "plug", "delay 5 s", ... */
    int  nerr;
    int  first_err_line;
    char first_err[56];
    bool on_card;      /**< false: built in */
    bool builtin;      /**< a built-in exists by this name (true for a card copy of one too) */
} store_row_t;

void store_init(void);
/** @brief Forget every card profile (a new listing begins). */
void store_clear_card(void);
/** @brief Add or replace a card profile. false when full or too long. */
bool store_put_card(const char *name, const char *text, size_t len);
void store_remove_card(const char *name);
int store_count(void);
/** @brief Row @p i for the list. false past the end. */
bool store_row(int i, store_row_t *out);
/** @brief Profile @p i's text into @p buf (NUL-terminated). The length, -1 past the end. */
int store_text(int i, char *buf, size_t cap);
/** @brief The index of the profile with this file name, or this title; -1 when none. */
int store_find_name(const char *name);
int store_find_title(const char *title);
/** @brief Something changed since the last call (the list should be drawn again). */
bool store_changed(void);
/** @brief A name a file may have: 1..39 of a-z 0-9 - _ */
bool store_name_ok(const char *name);
/** @brief A file name from a title: "HP t550: Secure Boot" -> "hp-t550-secure-boot". */
void store_slug(const char *title, char *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* STORE_H_ */
