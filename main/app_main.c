/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file app_main.c
 * @brief Dot BIOS Helper: the Kode Dot as a USB keyboard that plays key scripts into a PC's
 *        BIOS, with profiles on the microSD card and an editor in the browser.
 *
 * kode_init() brings kodeOS up; the profile list is built, then the app's Active Object
 * (app.c) takes over. Any reset returns to kodeOS (CONFIG_KODEOS_BOOT_ROLE_APP).
 */
#include "kode.h"

#include "app.h"
#include "store.h"

KODE_APP_MANIFEST("BIOS Helper", "tools");

void app_main(void)
{
    kode_init();
    store_init();
    App_ctor();
    App_start();
}
