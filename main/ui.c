/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file ui.c
 * @brief The glass (see ui.h). kodeOS's look: black, Light Grey text, yellow pills, Outfit.
 */
#include "ui.h"

#include <stdio.h>
#include <string.h>

#define INK    RGB(0xDE, 0xD7, 0xCD)
#define DIM    RGB(0xC2, 0xBA, 0xB3)
#define FAINT  RGB(0x8A, 0x86, 0x80)
#define CARD   RGB(0x1E, 0x1D, 0x1C)
#define CARD2  RGB(0x2C, 0x2A, 0x28)
#define YELLOW RGB(0xFF, 0xAC, 0x00)
#define ONYEL  RGB(0x19, 0x19, 0x19)
#define BLUE   RGB(0x02, 0x8A, 0xC2)
#define RED    RGB(0xE5, 0x48, 0x4D)
#define GREEN  RGB(0x3D, 0xBE, 0x6E)

/* shared geometry: draw and tap use the same rectangles */
#define LIST_Y     92
#define ROW_H      56
#define R_PRIMARY  gfx_rect(80, 380, 250, 54)
#define R_SECOND   gfx_rect(120, 444, 170, 42)
#define R_KEYS     gfx_rect(24, 444, 170, 42)
#define R_SETTINGS gfx_rect(216, 444, 170, 42)
#define R_START    gfx_rect(80, 382, 250, 52)
#define R_YES      gfx_rect(80, 330, 250, 54)
#define R_BACK     gfx_rect(120, 444, 170, 42)

#define PAD_X0 45
#define PAD_Y0 150
#define PAD_W  100
#define PAD_H  56
#define PAD_G  10

#define GRID_X0 17
#define GRID_Y0 84
#define GRID_W  88
#define GRID_H  52
#define GRID_G  8

static ui_row_fn s_rows;

static const char *const MANUAL[UI_MANUAL_KEYS] = {
    "ESC", "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9",
    "F10", "F11", "F12", "DEL", "TAB", "SPACE", "BKSP", "ENTER", "CTRL+ALT+DEL", "WIN",
};
static const char *const MANUAL_LABEL[UI_MANUAL_KEYS] = {
    "Esc", "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9",
    "F10", "F11", "F12", "Del", "Tab", "Space", "Bksp", "Enter", "C+A+Del", "Win",
};

const char *ui_manual_key(int i)
{
    return ((i >= 0) && (i < UI_MANUAL_KEYS)) ? MANUAL[i] : "";
}

const char *ui_manual_label(int i)
{
    return ((i >= 0) && (i < UI_MANUAL_KEYS)) ? MANUAL_LABEL[i] : "";
}

void ui_set_rows(ui_row_fn fn)
{
    s_rows = fn;
}

/* ---- drawing helpers ------------------------------------------------------------------- */

static void center(gfx_surf_t *s, int y, const aa_font_t *f, const char *t, uint16_t c)
{
    int w = gfx_atext_w(f, t);
    if ( w > UI_W - 24 )
    {
        w = UI_W - 24;
    }
    gfx_atext(s, (UI_W - w) / 2, y, f, t, c, UI_W - 24);
}

static void pill(gfx_surf_t *s, gfx_rect_t r, const char *t, bool primary)
{
    gfx_pill(s, r, primary ? YELLOW : CARD2);
    const aa_font_t *f = &font_bold;
    const int w = gfx_atext_w(f, t);
    gfx_atext(s, r.x + (r.w - w) / 2, r.y + (r.h - f->line_h) / 2, f, t, primary ? ONYEL : INK, r.w - 16);
}

static bool in(gfx_rect_t r, int x, int y)
{
    return gfx_contains(r, x, y);
}

/* Word-wrap @p t into the box, centred line by line; returns the y after the last line. */
static int wrap(gfx_surf_t *s, int y, int max_lines, const aa_font_t *f, const char *t, uint16_t c)
{
    const int maxw = UI_W - 48;
    char line[128];
    const char *p = t;
    int lines = 0;
    while ( *p && (lines < max_lines) )
    {
        while ( ' ' == *p )
        {
            ++p;
        }
        if ( !*p )
        {
            break;
        }
        size_t fit = 0;
        for ( ;; )
        {
            size_t ws = fit;
            while ( ' ' == p[ws] )
            {
                ++ws;
            }
            size_t we = ws;
            while ( p[we] && (' ' != p[we]) )
            {
                ++we;
            }
            if ( (we == ws) || (we >= sizeof(line)) )
            {
                if ( !fit )
                {
                    fit = (we < sizeof(line)) ? we : sizeof(line) - 1;
                }
                break;
            }
            memcpy(line, p, we);
            line[we] = 0;
            if ( gfx_atext_w(f, line) > maxw )
            {
                if ( !fit )
                {
                    fit = we; /* one word wider than the glass: gfx_atext ends it in "..." */
                }
                break;
            }
            fit = we;
        }
        memcpy(line, p, fit);
        line[fit] = 0;
        center(s, y, f, line, c);
        y += f->line_h + 4;
        p += fit;
        ++lines;
    }
    return y;
}

static void status(gfx_surf_t *s, const ui_model_t *m, int y)
{
    char b[64];
    uint16_t dot = FAINT;
    if ( !m->kb_ok )
    {
        (void)snprintf(b, sizeof(b), "USB keyboard did not start");
        dot = RED;
    }
    else if ( m->host )
    {
        (void)snprintf(b, sizeof(b), "PC connected  -  %s", m->port ? "USB-C" : "dock");
        dot = GREEN;
    }
    else
    {
        (void)snprintf(b, sizeof(b), "No PC  -  %s", m->port ? "USB-C" : "dock");
    }
    const int w = gfx_atext_w(&font_ui, b) + 18;
    const int x = (UI_W - w) / 2;
    gfx_disc(s, x + 5, y + font_ui.line_h / 2, 5, dot, 255);
    gfx_atext(s, x + 18, y, &font_ui, b, DIM, 0);
}

static void header(gfx_surf_t *s, const ui_model_t *m, const char *title)
{
    center(s, 12, &font_title, title, INK);
    status(s, m, 48);
}

static void toast(gfx_surf_t *s, const ui_model_t *m, int y)
{
    if ( m->toast[0] )
    {
        center(s, y, &font_ui, m->toast, YELLOW);
    }
}

/* ---- the list --------------------------------------------------------------------------- */

static void draw_list(gfx_surf_t *s, const ui_model_t *m)
{
    header(s, m, "BIOS Helper");
    char b[64];
    if ( m->toast[0] )
    {
        center(s, 68, &font_ui, m->toast, YELLOW);
    }
    else if ( m->web_up )
    {
        (void)snprintf(b, sizeof(b), "Edit profiles: http://%s", m->ip);
        center(s, 68, &font_ui, b, BLUE);
    }
    else if ( m->web_on && !m->wifi )
    {
        center(s, 68, &font_ui, "Editor: join Wi-Fi in Settings", FAINT);
    }
    for ( int r = 0; r < UI_LIST_ROWS; ++r )
    {
        const int i = m->top + r;
        if ( i >= m->count )
        {
            break;
        }
        ui_row_t row;
        memset(&row, 0, sizeof(row));
        if ( !s_rows || !s_rows(i, &row) )
        {
            break;
        }
        const gfx_rect_t rr = gfx_rect(14, LIST_Y + r * ROW_H, UI_W - 28, ROW_H - 6);
        const bool sel = (i == m->sel);
        if ( sel )
        {
            gfx_round(s, rr, 12, CARD2);
            gfx_round(s, gfx_rect(rr.x, rr.y + 8, 5, rr.h - 16), 2, YELLOW);
        }
        gfx_atext(s, rr.x + 16, rr.y + 4, &font_bold, row.title, sel ? INK : DIM, rr.w - 28);
        if ( row.nerr )
        {
            (void)snprintf(b, sizeof(b), "%d error%s - fix it in the editor", row.nerr, (1 == row.nerr) ? "" : "s");
            gfx_atext(s, rr.x + 16, rr.y + 27, &font_ui, b, RED, rr.w - 28);
        }
        else
        {
            gfx_atext(s, rr.x + 16, rr.y + 27, &font_ui, row.sub, FAINT, rr.w - 28);
        }
    }
    if ( m->count > UI_LIST_ROWS )
    {
        /* a thin scroll bar */
        const int h = UI_LIST_ROWS * ROW_H - 6;
        const int bh = h * UI_LIST_ROWS / m->count;
        const int by = LIST_Y + (h - bh) * m->top / (m->count - UI_LIST_ROWS);
        gfx_round(s, gfx_rect(UI_W - 8, LIST_Y, 3, h), 1, CARD);
        gfx_round(s, gfx_rect(UI_W - 8, by, 3, bh), 1, FAINT);
    }
    if ( 0 == m->count )
    {
        center(s, 200, &font_ui, "No profiles", FAINT);
    }
    const char *go = "Start";
    if ( m->count && s_rows )
    {
        ui_row_t row;
        if ( s_rows(m->sel, &row) && strstr(row.sub, "plug") == row.sub )
        {
            go = "Arm";
        }
    }
    pill(s, R_START, go, true);
    pill(s, R_KEYS, "Keyboard", false);
    pill(s, R_SETTINGS, "Settings", false);
}

/* ---- the run ---------------------------------------------------------------------------- */

static void ring(gfx_surf_t *s, int cx, int cy, int r, int w, uint16_t c)
{
    gfx_disc(s, cx, cy, r, c, 255);
    gfx_disc(s, cx, cy, r - w, 0, 255);
}

static void draw_keypad(gfx_surf_t *s, const ui_model_t *m)
{
    const sc_player_t *pl = m->pl;
    wrap(s, 76, 1, &font_bold, pl->text, INK);
    char shown[24];
    int n = 0;
    const int typed = (int)strlen(m->entry);
    for ( int i = 0; (i < typed) && (n < 20); ++i )
    {
        shown[n++] = m->entry[i];
    }
    if ( pl->entry_send )
    {
        for ( int i = typed; (i < pl->entry_n) && (n < 20); ++i )
        {
            shown[n++] = '-';
        }
    }
    shown[n] = 0;
    center(s, 104, &font_title, n ? shown : " ", YELLOW);
    for ( int k = 0; k < 12; ++k )
    {
        const int c = k % 3;
        const int r = k / 3;
        const gfx_rect_t rr = gfx_rect(PAD_X0 + c * (PAD_W + PAD_G), PAD_Y0 + r * (PAD_H + PAD_G), PAD_W, PAD_H);
        char lab[8];
        bool prim = false;
        if ( k < 9 )
        {
            (void)snprintf(lab, sizeof(lab), "%d", k + 1);
        }
        else if ( 9 == k )
        {
            (void)snprintf(lab, sizeof(lab), "<");
        }
        else if ( 10 == k )
        {
            (void)snprintf(lab, sizeof(lab), "0");
        }
        else
        {
            (void)snprintf(lab, sizeof(lab), "OK");
            prim = pl->entry_send ? (typed >= pl->entry_n) : (typed > 0);
        }
        gfx_round(s, rr, 14, prim ? YELLOW : CARD2);
        const int w = gfx_atext_w(&font_title, lab);
        gfx_atext(s, rr.x + (rr.w - w) / 2, rr.y + (rr.h - font_title.line_h) / 2, &font_title, lab,
                  prim ? ONYEL : INK, 0);
    }
    pill(s, R_SECOND, "Stop", false);
}

static void draw_run(gfx_surf_t *s, const ui_model_t *m)
{
    const sc_player_t *pl = m->pl;
    const sc_prog_t *p = pl->p;
    header(s, m, p && p->title[0] ? p->title : "Profile");
    char b[96];
    const int st = sc_state(pl);
    const uint32_t left = sc_left_ms(pl, m->now);
    switch ( st )
    {
    case SC_CONFIRM:
        (void)snprintf(b, sizeof(b), "Run \"%s\" on this PC?", p->title);
        wrap(s, 130, 3, &font_title, b, INK);
        if ( p->desc[0] )
        {
            wrap(s, 250, 2, &font_ui, p->desc, FAINT);
        }
        pill(s, R_YES, "Yes, go", true);
        pill(s, R_SECOND, "No", false);
        break;
    case SC_ARMED: {
        const bool blink = ((m->now / 500u) & 1u) != 0u;
        ring(s, UI_W / 2, 196, 88, 10, pl->need_unplug ? YELLOW : (blink ? RED : RGB(0x8C, 0x2A, 0x2D)));
        center(s, 172, &font_title, "ARMED", INK);
        if ( pl->runs )
        {
            (void)snprintf(b, sizeof(b), "%d PC%s done", pl->runs, (1 == pl->runs) ? "" : "s");
            center(s, 206, &font_ui, b, GREEN);
        }
        if ( pl->need_unplug )
        {
            wrap(s, 296, 2, &font_ui, "A PC has the keyboard now. Unplug it or switch the PC off, then back on.",
                 YELLOW);
        }
        else
        {
            wrap(s, 296, 2, &font_ui, "Plug into the PC, or switch it on: the keys start as it boots.", DIM);
        }
        if ( left )
        {
            (void)snprintf(b, sizeof(b), "Disarms in %u min", (unsigned)((left + 59999u) / 60000u));
            center(s, 346, &font_ui, b, FAINT);
        }
        pill(s, R_PRIMARY, "Go now", true);
        pill(s, R_SECOND, "Disarm", false);
        break;
    }
    case SC_COUNTDOWN:
        center(s, 120, &font_ui, "Starting in", DIM);
        (void)snprintf(b, sizeof(b), "%u", (unsigned)((left + 999u) / 1000u));
        center(s, 150, &font_big, b, YELLOW);
        center(s, 240, &font_ui, "Put the focus where the keys should go", FAINT);
        pill(s, R_PRIMARY, "Go now", true);
        pill(s, R_SECOND, "Cancel", false);
        break;
    case SC_SPAM:
        center(s, 104, &font_ui, "Pressing", DIM);
        center(s, 130, &font_big, pl->text, YELLOW);
        if ( !m->host || !pl->until )
        {
            wrap(s, 220, 2, &font_ui, "Waiting for the PC to take the keyboard. Switch it on.", DIM);
        }
        else
        {
            (void)snprintf(b, sizeof(b), "%u s", (unsigned)((left + 999u) / 1000u));
            center(s, 220, &font_title, b, INK);
        }
        (void)snprintf(b, sizeof(b), "Step %d of %d", pl->pc + 1, p->n);
        center(s, 300, &font_ui, b, FAINT);
        pill(s, R_PRIMARY, "Next", true);
        pill(s, R_SECOND, "Stop", false);
        break;
    case SC_ASK:
        wrap(s, 110, 5, &font_title, pl->text, INK);
        center(s, 316, &font_ui, "Pad: arrow keys   TOP: Enter", FAINT);
        pill(s, R_PRIMARY, "Next", true);
        pill(s, R_SECOND, "Stop", false);
        break;
    case SC_ENTRY:
        draw_keypad(s, m);
        break;
    case SC_WAITPC:
        center(s, 150, &font_title, "Waiting for a PC", INK);
        wrap(s, 200, 2, &font_ui, "Plug the Dot in, or switch the PC on.", DIM);
        if ( left )
        {
            (void)snprintf(b, sizeof(b), "Going on in %u s", (unsigned)((left + 999u) / 1000u));
            center(s, 260, &font_ui, b, FAINT);
        }
        pill(s, R_PRIMARY, "Skip", true);
        pill(s, R_SECOND, "Stop", false);
        break;
    case SC_DONE:
        gfx_disc(s, UI_W / 2, 176, 56, GREEN, 255);
        center(s, 160, &font_title, "Done", ONYEL);
        (void)snprintf(b, sizeof(b), "Ran %d time%s", pl->runs, (1 == pl->runs) ? "" : "s");
        center(s, 250, &font_ui, b, DIM);
        if ( pl->rearm_wait )
        {
            wrap(s, 280, 2, &font_bold, "Unplug for the next PC: it arms again by itself.", YELLOW);
            pill(s, R_PRIMARY, "Stop batch", true);
        }
        else
        {
            pill(s, R_PRIMARY, "Again", true);
        }
        pill(s, R_SECOND, "Back", false);
        break;
    default: { /* SC_RUN */
        (void)snprintf(b, sizeof(b), "Step %d of %d", ((pl->pc < p->n) ? pl->pc : p->n - 1) + 1, p->n);
        center(s, 100, &font_ui, b, FAINT);
        wrap(s, 140, 3, &font_title, pl->text[0] ? pl->text : "...", INK);
        const int bw = UI_W - 80;
        gfx_round(s, gfx_rect(40, 300, bw, 10), 5, CARD2);
        const int done = p->n ? (bw * pl->pc / p->n) : 0;
        if ( done > 0 )
        {
            gfx_round(s, gfx_rect(40, 300, done, 10), 5, YELLOW);
        }
        if ( !m->host )
        {
            center(s, 326, &font_ui, "No PC yet: the keys wait for one", YELLOW);
        }
        pill(s, R_PRIMARY, "Stop", false);
        break;
    }
    }
    if ( SC_ENTRY != st )
    {
        toast(s, m, 352);
    }
}

/* ---- the keyboard view ------------------------------------------------------------------- */

static void draw_keys(gfx_surf_t *s, const ui_model_t *m)
{
    header(s, m, "Keyboard");
    for ( int k = 0; k < UI_MANUAL_KEYS; ++k )
    {
        const int c = k % 4;
        const int r = k / 4;
        const gfx_rect_t rr = gfx_rect(GRID_X0 + c * (GRID_W + GRID_G), GRID_Y0 + r * (GRID_H + GRID_G), GRID_W, GRID_H);
        const bool hot = (k == m->flash_key);
        gfx_round(s, rr, 12, hot ? YELLOW : CARD2);
        const int w = gfx_atext_w(&font_bold, MANUAL_LABEL[k]);
        gfx_atext(s, rr.x + (rr.w - w) / 2, rr.y + (rr.h - font_bold.line_h) / 2, &font_bold, MANUAL_LABEL[k],
                  hot ? ONYEL : INK, rr.w - 6);
    }
    center(s, 392, &font_ui, "Pad: arrow keys   TOP: Enter", FAINT);
    toast(s, m, 416);
    pill(s, R_BACK, "Back", false);
}

/* ---- settings ------------------------------------------------------------------------------ */

static void draw_settings(gfx_surf_t *s, const ui_model_t *m)
{
    header(s, m, "Settings");
    char v[64];
    for ( int r = 0; r < UI_SETTINGS; ++r )
    {
        const gfx_rect_t rr = gfx_rect(14, 84 + r * 66, UI_W - 28, 58);
        const bool sel = (r == m->set_sel);
        gfx_round(s, rr, 12, sel ? CARD2 : CARD);
        const char *k = "";
        uint16_t vc = YELLOW;
        switch ( r )
        {
        case 0:
            k = "USB port";
            (void)snprintf(v, sizeof(v), "%s%s", m->port_next ? "USB-C socket (test)" : "Dock pads",
                           (m->port_next != m->port) ? "  - at next start" : "");
            break;
        case 1:
            k = "Web editor";
            (void)snprintf(v, sizeof(v), "%s%s", m->web_next ? "On" : "Off",
                           (m->web_next != m->web_on) ? "  - at next start" : "");
            break;
        case 2:
            k = "Profiles on the card";
            if ( m->card_ok )
            {
                (void)snprintf(v, sizeof(v), "%d in /bioshelper  - tap to read again", m->card_files);
            }
            else
            {
                (void)snprintf(v, sizeof(v), "No card - tap to try again");
                vc = FAINT;
            }
            break;
        default:
            k = "About";
            (void)snprintf(v, sizeof(v), "Dot BIOS Helper %s, GPL-3.0", m->version ? m->version : "");
            vc = FAINT;
            break;
        }
        gfx_atext(s, rr.x + 16, rr.y + 6, &font_bold, k, INK, rr.w - 28);
        gfx_atext(s, rr.x + 16, rr.y + 31, &font_ui, v, vc, rr.w - 28);
    }
    toast(s, m, 352);
    pill(s, R_BACK, "Back", false);
}

void ui_draw(gfx_surf_t *s, const ui_model_t *m)
{
    gfx_fill(s, gfx_rect(0, 0, UI_W, UI_H), 0);
    switch ( m->view )
    {
    case UI_RUN:
        draw_run(s, m);
        break;
    case UI_KEYS:
        draw_keys(s, m);
        break;
    case UI_SETTINGS_V:
        draw_settings(s, m);
        break;
    default:
        draw_list(s, m);
        break;
    }
}

/* ---- input ------------------------------------------------------------------------------------ */

static ui_act_t act(int kind, int arg)
{
    const ui_act_t a = { kind, arg, 0 };
    return a;
}

ui_act_t ui_tap(const ui_model_t *m, int x, int y)
{
    switch ( m->view )
    {
    case UI_LIST:
        if ( in(R_START, x, y) )
        {
            return act(UA_START, 0);
        }
        if ( in(R_KEYS, x, y) )
        {
            return act(UA_VIEW, UI_KEYS);
        }
        if ( in(R_SETTINGS, x, y) )
        {
            return act(UA_VIEW, UI_SETTINGS_V);
        }
        if ( (y >= LIST_Y) && (y < LIST_Y + UI_LIST_ROWS * ROW_H) )
        {
            const int i = m->top + (y - LIST_Y) / ROW_H;
            if ( i < m->count )
            {
                return act(UA_SELECT, i);
            }
        }
        break;
    case UI_KEYS:
        if ( in(R_BACK, x, y) )
        {
            return act(UA_VIEW, UI_LIST);
        }
        for ( int k = 0; k < UI_MANUAL_KEYS; ++k )
        {
            const gfx_rect_t rr = gfx_rect(GRID_X0 + (k % 4) * (GRID_W + GRID_G), GRID_Y0 + (k / 4) * (GRID_H + GRID_G),
                                           GRID_W, GRID_H);
            if ( in(rr, x, y) )
            {
                return act(UA_KEY, k);
            }
        }
        break;
    case UI_SETTINGS_V:
        if ( in(R_BACK, x, y) )
        {
            return act(UA_VIEW, UI_LIST);
        }
        for ( int r = 0; r < UI_SETTINGS; ++r )
        {
            if ( in(gfx_rect(14, 84 + r * 66, UI_W - 28, 58), x, y) )
            {
                ui_act_t a = act(UA_SETTING, r);
                a.arg2 = 1;
                return a;
            }
        }
        break;
    default: { /* UI_RUN */
        const int st = sc_state(m->pl);
        if ( SC_ENTRY == st )
        {
            if ( in(R_SECOND, x, y) )
            {
                return act(UA_STOP, 0);
            }
            for ( int k = 0; k < 12; ++k )
            {
                const gfx_rect_t rr = gfx_rect(PAD_X0 + (k % 3) * (PAD_W + PAD_G), PAD_Y0 + (k / 3) * (PAD_H + PAD_G),
                                               PAD_W, PAD_H);
                if ( in(rr, x, y) )
                {
                    if ( k < 9 )
                    {
                        return act(UA_DIGIT, k + 1);
                    }
                    if ( 9 == k )
                    {
                        return act(UA_BKSP, 0);
                    }
                    if ( 10 == k )
                    {
                        return act(UA_DIGIT, 0);
                    }
                    return act(UA_OK, 0);
                }
            }
            break;
        }
        if ( SC_CONFIRM == st )
        {
            if ( in(R_YES, x, y) )
            {
                return act(UA_YES, 0);
            }
            if ( in(R_SECOND, x, y) )
            {
                return act(UA_NO, 0);
            }
            break;
        }
        if ( in(R_PRIMARY, x, y) )
        {
            switch ( st )
            {
            case SC_ARMED:
            case SC_COUNTDOWN:
                return act(UA_GO, 0);
            case SC_SPAM:
            case SC_ASK:
            case SC_WAITPC:
                return act(UA_NEXT, 0);
            case SC_DONE:
                return act(m->pl->rearm_wait ? UA_STOP : UA_AGAIN, 0);
            default:
                return act(UA_STOP, 0);
            }
        }
        if ( in(R_SECOND, x, y) )
        {
            return act((SC_DONE == st) ? UA_VIEW : UA_STOP, UI_LIST);
        }
        break;
    }
    }
    return act(UA_NONE, 0);
}

ui_act_t ui_button(const ui_model_t *m, int btn)
{
    switch ( m->view )
    {
    case UI_LIST:
        if ( UB_UP == btn )
        {
            return act(UA_SELECT, (m->sel > 0) ? m->sel - 1 : m->sel);
        }
        if ( UB_DOWN == btn )
        {
            return act(UA_SELECT, (m->sel + 1 < m->count) ? m->sel + 1 : m->sel);
        }
        if ( UB_TOP == btn )
        {
            return act(UA_START, 0);
        }
        if ( UB_RIGHT == btn )
        {
            return act(UA_VIEW, UI_KEYS);
        }
        if ( UB_LEFT == btn )
        {
            return act(UA_VIEW, UI_SETTINGS_V);
        }
        break;
    case UI_KEYS:
        if ( btn <= UB_RIGHT )
        {
            return act(UA_ARROW, btn);
        }
        if ( UB_TOP == btn )
        {
            return act(UA_ARROW, 4);
        }
        break;
    case UI_SETTINGS_V:
        if ( UB_UP == btn )
        {
            return act(UA_SETTING, -1);
        }
        if ( UB_DOWN == btn )
        {
            return act(UA_SETTING, -2);
        }
        if ( (UB_LEFT == btn) || (UB_RIGHT == btn) || (UB_TOP == btn) )
        {
            ui_act_t a = act(UA_SETTING, m->set_sel);
            a.arg2 = (UB_LEFT == btn) ? -1 : 1;
            return a;
        }
        break;
    default: {
        const int st = sc_state(m->pl);
        if ( SC_ASK == st )
        {
            if ( btn <= UB_RIGHT )
            {
                return act(UA_ARROW, btn);
            }
            if ( UB_TOP == btn )
            {
                return act(UA_ARROW, 4);
            }
        }
        if ( UB_TOP == btn )
        {
            if ( (SC_ARMED == st) || (SC_COUNTDOWN == st) )
            {
                return act(UA_GO, 0);
            }
            if ( (SC_SPAM == st) || (SC_WAITPC == st) )
            {
                return act(UA_NEXT, 0);
            }
            if ( SC_CONFIRM == st )
            {
                return act(UA_YES, 0);
            }
            if ( SC_ENTRY == st )
            {
                return act(UA_OK, 0);
            }
            if ( SC_DONE == st )
            {
                return act(m->pl->rearm_wait ? UA_NONE : UA_AGAIN, 0);
            }
        }
        break;
    }
    }
    return act(UA_NONE, 0);
}
