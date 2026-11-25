/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file hidkb.h
 * @brief The watch as a USB keyboard (TinyUSB HID, boot protocol, so a BIOS
 *        takes it). Starting it moves the S3's one USB PHY from the USB
 *        Serial/JTAG console to USB OTG; stopping gives it back.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

bool hidkb_start(void);          /* PHY to OTG, keyboard up; false on failure */
void hidkb_stop(void);           /* keyboard down, PHY back to the console */
bool hidkb_active(void);
bool hidkb_ready(void);          /* a host has configured us (not suspended) */
int hidkb_pending(void);         /* key taps still queued */
void hidkb_flush(void);          /* drop what is queued */
bool hidkb_tap(uint8_t mod, uint8_t key);   /* queue one press + release */
bool hidkb_tap_hold(uint8_t mod, uint8_t key, uint16_t down_ms);  /* the same, held down_ms (0 = 40); key 0 = modifiers alone */
uint8_t hidkb_code(const char *name, uint8_t *mod);  /* "F10", "ENTER", "a", "7"... -> HID usage, 0 if unknown */
