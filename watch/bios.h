/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bios.h
 * @brief BIOS helper: plays key profiles into a PC through the watch's USB keyboard.
 *
 * The profile language and its player are Dot BIOS Helper's (script.c, docs in its README:
 * github.com/Rein033/dot-bios-helper). Profiles: /data/bios.txt first, then every
 * /data/bios/<name>.txt (sorted), then the built-in ones. Edit them with the Files app or the
 * hub's /api/fs.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "script.h"

#define BIOS_PATH "/data/bios.txt"
#define BIOS_DIR "/data/bios"

/* bios_take_events() bits */
#define BIOS_EV_BEEP  0x01u
#define BIOS_EV_START 0x02u
#define BIOS_EV_DONE  0x04u
#define BIOS_EV_ARMED 0x08u

void bios_load(void);                 /* scan the profiles and load the chosen one */
void bios_select(int i);
int bios_profile(void);
int bios_profiles(void);
const char *bios_path(void);          /* the file, or "built in" */
const char *bios_title(void);
const char *bios_desc(void);
int bios_errors(void);                /* parse errors of the loaded profile */
int bios_error_line(void);
const char *bios_error(void);
void bios_arm_label(char *buf, int cap);
bool bios_arm_plug(void);             /* arm plug: the dock's plug-in arms it by itself */

void bios_start(uint32_t now);        /* confirm / arm / count down / run, as the profile says */
void bios_confirm(bool yes, uint32_t now);
void bios_go(uint32_t now);           /* armed or counting down: now */
void bios_stop(void);
void bios_next(uint32_t now);         /* spam, ask, waitpc: go on */
void bios_entry(const char *digits, uint32_t now);
void bios_tick(uint32_t now);
int bios_state(void);                 /* sc_state_t */
const sc_player_t *bios_player(void);
unsigned bios_take_events(void);
