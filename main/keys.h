/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file keys.h
 * @brief Key names to USB HID usages (keyboard page 0x07), and text to keys on a US layout.
 *
 * Pure C, no SDK: the script parser checks names with it and the host test runs it.
 *
 * A name is one of the named keys below (case does not matter), one character
 * ("a", "Z", "7", "/"), or modifiers joined with '+': "CTRL+ALT+DEL", "SHIFT+TAB",
 * "WIN+R", "ALT+F4". A modifier on its own ("CTRL", "WIN") is a tap of just that modifier.
 */
#ifndef KEYS_H_
#define KEYS_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Modifier bits of the boot report's first byte. */
#define KEY_MOD_LCTRL  0x01u
#define KEY_MOD_LSHIFT 0x02u
#define KEY_MOD_LALT   0x04u
#define KEY_MOD_LGUI   0x08u
#define KEY_MOD_RCTRL  0x10u
#define KEY_MOD_RSHIFT 0x20u
#define KEY_MOD_RALT   0x40u
#define KEY_MOD_RGUI   0x80u

/** @brief One key stroke: modifiers and one usage (0 = the modifiers alone). */
typedef struct {
    uint8_t mod;
    uint8_t key;
} kstroke_t;

/**
 * @brief Look a name up.
 * @param name "F10", "enter", "CTRL+ALT+DEL", "a", "?"...
 * @param out  The stroke.
 * @return false for a name that is no key.
 */
bool keys_parse(const char *name, kstroke_t *out);

/**
 * @brief The stroke that types @p c on a US layout ('\n' is ENTER, '\t' TAB).
 * @return false for a character a US keyboard has no key for.
 */
bool keys_char(char c, kstroke_t *out);

/** @brief The canonical name of a stroke, for the screen ("CTRL+ALT+DEL"). */
void keys_name(kstroke_t k, char *buf, int cap);

/** @brief How many named keys there are, and the i-th name: the editor's list. */
int keys_named_count(void);
const char *keys_named(int i);

#ifdef __cplusplus
}
#endif

#endif /* KEYS_H_ */
