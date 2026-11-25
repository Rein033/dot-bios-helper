/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file builtin.c
 * @brief The profiles that come with the app (see builtin.h).
 *
 * Only the spam profiles are generic. The HP t550 order is one technician's
 * notes for that model and firmware; check a sequence on one machine before
 * you run it on many.
 */
#include "builtin.h"

typedef struct {
    const char *name;
    const char *text;
} builtin_t;

static const builtin_t B[] = {
    { "hp-t550-secure-boot",
      "title HP t550: Secure Boot off\n"
      "desc F10 into setup, the Secure Boot page, save with F10, then F9\n"
      "arm plug\n"
      "confirm on\n"
      "gap 200\n"
      "spam F10 10\n"
      "wait 2000\n"
      "keys RIGHT RIGHT UP ENTER F10 RIGHT F10 F10 ENTER ENTER\n"
      "spam F9 10\n" },
    { "boot-menu-f12",
      "title Boot menu (F12)\n"
      "desc Tap F12 while the PC starts: Dell, Lenovo, Acer and many more\n"
      "arm plug\n"
      "spam F12 15\n"
      "beep\n" },
    { "setup-del",
      "title BIOS setup (DEL)\n"
      "desc Tap DEL while the PC starts: most desktop boards\n"
      "arm plug\n"
      "spam DEL 15\n"
      "beep\n" },
    { "setup-f2",
      "title BIOS setup (F2)\n"
      "desc Tap F2 while the PC starts: many laptops\n"
      "arm plug\n"
      "spam F2 15\n"
      "beep\n" },
    { "hp-boot-menu-f9",
      "title HP: boot menu (F9)\n"
      "desc Tap F9 while an HP starts\n"
      "arm plug\n"
      "spam F9 15\n"
      "beep\n" },
    { "batch-f12",
      "title Batch: F12 on every PC\n"
      "desc Plug into one PC after another: each gets F12 as it boots\n"
      "arm plug\n"
      "after rearm\n"
      "timeout 120\n"
      "spam F12 15\n"
      "beep\n" },
    { "example-tour",
      "title Example: every command\n"
      "desc A tour of the language; open it in the editor\n"
      "arm delay 3\n"
      "confirm on\n"
      "# Start counts 3 seconds down, after a yes.\n"
      "waitpc 30\n"
      "msg Opening Run\n"
      "keys WIN+r\n"
      "wait 1s\n"
      "line notepad\n"
      "wait 2s\n"
      "input tag 6 Asset tag\n"
      "set who Checked by IT, tag $tag\n"
      "line $who\n"
      "repeat 3\n"
      "  type .\n"
      "  wait 300\n"
      "end\n"
      "keys ENTER\n"
      "ask Close Notepad without saving?\n"
      "keys ALT+F4 TAB ENTER\n"
      "beep\n" },
};

int builtin_count(void)
{
    return (int)(sizeof(B) / sizeof(B[0]));
}

const char *builtin_name(int i)
{
    return ((i >= 0) && (i < builtin_count())) ? B[i].name : "";
}

const char *builtin_text(int i)
{
    return ((i >= 0) && (i < builtin_count())) ? B[i].text : "";
}
