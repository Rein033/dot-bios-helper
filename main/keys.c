/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file keys.c
 * @brief Key names and the US layout (see keys.h).
 */
#include "keys.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

typedef struct {
    const char *n;
    uint8_t     k;
} named_t;

/* HID usage table 0x07. Aliases follow the name they stand for. */
static const named_t NAMED[] = {
    { "ENTER", 0x28 }, { "RETURN", 0x28 }, { "ESC", 0x29 }, { "ESCAPE", 0x29 },
    { "BKSP", 0x2A }, { "BACKSPACE", 0x2A }, { "TAB", 0x2B }, { "SPACE", 0x2C },
    { "CAPSLOCK", 0x39 }, { "CAPS", 0x39 },
    { "F1", 0x3A }, { "F2", 0x3B }, { "F3", 0x3C }, { "F4", 0x3D }, { "F5", 0x3E }, { "F6", 0x3F },
    { "F7", 0x40 }, { "F8", 0x41 }, { "F9", 0x42 }, { "F10", 0x43 }, { "F11", 0x44 }, { "F12", 0x45 },
    { "PRTSC", 0x46 }, { "PRINTSCREEN", 0x46 }, { "SCROLLLOCK", 0x47 }, { "PAUSE", 0x48 }, { "BREAK", 0x48 },
    { "INSERT", 0x49 }, { "INS", 0x49 }, { "HOME", 0x4A }, { "PGUP", 0x4B }, { "PAGEUP", 0x4B },
    { "DEL", 0x4C }, { "DELETE", 0x4C }, { "END", 0x4D }, { "PGDN", 0x4E }, { "PAGEDOWN", 0x4E },
    { "RIGHT", 0x4F }, { "LEFT", 0x50 }, { "DOWN", 0x51 }, { "UP", 0x52 },
    { "NUMLOCK", 0x53 }, { "KP/", 0x54 }, { "KP*", 0x55 }, { "KP-", 0x56 }, { "MINUS", 0x56 },
    { "KP+", 0x57 }, { "PLUS", 0x57 }, { "KPENTER", 0x58 },
    { "KP1", 0x59 }, { "KP2", 0x5A }, { "KP3", 0x5B }, { "KP4", 0x5C }, { "KP5", 0x5D },
    { "KP6", 0x5E }, { "KP7", 0x5F }, { "KP8", 0x60 }, { "KP9", 0x61 }, { "KP0", 0x62 }, { "KP.", 0x63 },
    { "MENU", 0x65 }, { "APP", 0x65 },
    { "F13", 0x68 }, { "F14", 0x69 }, { "F15", 0x6A }, { "F16", 0x6B }, { "F17", 0x6C }, { "F18", 0x6D },
    { "F19", 0x6E }, { "F20", 0x6F }, { "F21", 0x70 }, { "F22", 0x71 }, { "F23", 0x72 }, { "F24", 0x73 },
};

typedef struct {
    const char *n;
    uint8_t     m;
} mod_t;

static const mod_t MODS[] = {
    { "CTRL", KEY_MOD_LCTRL }, { "CONTROL", KEY_MOD_LCTRL }, { "SHIFT", KEY_MOD_LSHIFT },
    { "ALT", KEY_MOD_LALT }, { "WIN", KEY_MOD_LGUI }, { "GUI", KEY_MOD_LGUI }, { "CMD", KEY_MOD_LGUI },
    { "RCTRL", KEY_MOD_RCTRL }, { "RSHIFT", KEY_MOD_RSHIFT }, { "RALT", KEY_MOD_RALT }, { "ALTGR", KEY_MOD_RALT },
    { "RWIN", KEY_MOD_RGUI },
};

/* US layout, ASCII 32..126: usage, plus 0x80 when SHIFT is needed. */
static const uint8_t US[95] = {
    /* ' ' */ 0x2C, /* ! */ 0x9E, /* " */ 0xB4, /* # */ 0xA0, /* $ */ 0xA1, /* % */ 0xA2, /* & */ 0xA4,
    /* ' */ 0x34, /* ( */ 0xA6, /* ) */ 0xA7, /* * */ 0xA5, /* + */ 0xAE, /* , */ 0x36, /* - */ 0x2D,
    /* . */ 0x37, /* / */ 0x38,
    /* 0-9 */ 0x27, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26,
    /* : */ 0xB3, /* ; */ 0x33, /* < */ 0xB6, /* = */ 0x2E, /* > */ 0xB7, /* ? */ 0xB8, /* @ */ 0x9F,
    /* A-Z */ 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F, 0x90,
    0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9D,
    /* [ */ 0x2F, /* \ */ 0x31, /* ] */ 0x30, /* ^ */ 0xA3, /* _ */ 0xAD, /* ` */ 0x35,
    /* a-z */ 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D,
    /* { */ 0xAF, /* | */ 0xB1, /* } */ 0xB0, /* ~ */ 0xB5,
};

bool keys_char(char c, kstroke_t *out)
{
    if ( '\n' == c )
    {
        *out = (kstroke_t){ 0, 0x28 };
        return true;
    }
    if ( '\t' == c )
    {
        *out = (kstroke_t){ 0, 0x2B };
        return true;
    }
    const unsigned char u = (unsigned char)c;
    if ( (u < 32u) || (u > 126u) )
    {
        return false;
    }
    const uint8_t v = US[u - 32u];
    out->key = v & 0x7Fu;
    out->mod = (v & 0x80u) ? KEY_MOD_LSHIFT : 0u;
    return true;
}

static bool mod_of(const char *s, size_t len, uint8_t *m)
{
    for ( size_t i = 0; i < sizeof(MODS) / sizeof(MODS[0]); ++i )
    {
        if ( (strlen(MODS[i].n) == len) && (0 == strncasecmp(s, MODS[i].n, len)) )
        {
            *m = MODS[i].m;
            return true;
        }
    }
    return false;
}

static bool one(const char *s, size_t len, kstroke_t *out)
{
    if ( 0 == len )
    {
        return false;
    }
    for ( size_t i = 0; i < sizeof(NAMED) / sizeof(NAMED[0]); ++i )
    {
        if ( (strlen(NAMED[i].n) == len) && (0 == strncasecmp(s, NAMED[i].n, len)) )
        {
            *out = (kstroke_t){ 0, NAMED[i].k };
            return true;
        }
    }
    uint8_t m = 0;
    if ( mod_of(s, len, &m) )
    {
        *out = (kstroke_t){ m, 0 };
        return true;
    }
    if ( 1 == len )
    {
        return keys_char(s[0], out);
    }
    return false;
}

bool keys_parse(const char *name, kstroke_t *out)
{
    if ( !name || !name[0] )
    {
        return false;
    }
    kstroke_t acc = { 0, 0 };
    const char *p = name;
    for ( ;; )
    {
        /* "+" itself as the last part ("CTRL++") is the plus key. */
        const char *plus = strchr((p[0] == '+') ? p + 1 : p, '+');
        const size_t len = plus ? (size_t)(plus - p) : strlen(p);
        kstroke_t k;
        if ( !one(p, len, &k) )
        {
            return false;
        }
        acc.mod |= k.mod;
        if ( k.key )
        {
            if ( acc.key )
            {
                return false; /* two keys that are not modifiers: not a stroke */
            }
            acc.key = k.key;
        }
        if ( !plus )
        {
            break;
        }
        p = plus + 1;
        if ( !*p )
        {
            return false;
        }
    }
    *out = acc;
    return true;
}

void keys_name(kstroke_t k, char *buf, int cap)
{
    if ( cap <= 0 )
    {
        return;
    }
    buf[0] = 0;
    static const char *const MN[] = { "CTRL", "SHIFT", "ALT", "WIN", "RCTRL", "RSHIFT", "RALT", "RWIN" };
    int n = 0;
    for ( int i = 0; i < 8; ++i )
    {
        if ( k.mod & (1u << i) )
        {
            n += snprintf(buf + n, (size_t)((cap > n) ? cap - n : 0), "%s%s", n ? "+" : "", MN[i]);
        }
    }
    if ( !k.key || (n >= cap) )
    {
        return;
    }
    const char *nm = NULL;
    for ( size_t i = 0; i < sizeof(NAMED) / sizeof(NAMED[0]); ++i )
    {
        if ( NAMED[i].k == k.key )
        {
            nm = NAMED[i].n;
            break;
        }
    }
    char one_c[2] = { 0, 0 };
    if ( !nm )
    {
        for ( int c = 33; c < 127; ++c )
        {
            if ( (US[c - 32] & 0x7Fu) == k.key && !(US[c - 32] & 0x80u) )
            {
                one_c[0] = (char)(((c >= 'a') && (c <= 'z')) ? c - 32 : c);
                nm = one_c;
                break;
            }
        }
    }
    (void)snprintf(buf + n, (size_t)(cap - n), "%s%s", n ? "+" : "", nm ? nm : "?");
}

int keys_named_count(void)
{
    return (int)(sizeof(NAMED) / sizeof(NAMED[0]));
}

const char *keys_named(int i)
{
    return ((i >= 0) && (i < keys_named_count())) ? NAMED[i].n : "";
}
