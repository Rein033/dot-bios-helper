/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * Host test: paint every view of the glass and write each as raw RGB565 (<out>/<name>.565);
 * tools/565png.py turns them into PNGs.
 *
 *   cc -std=c11 -I../main ui_render.c ../main/ui.c ../main/gfx.c ../main/fonts_aa.c ../main/icons_aa.c \
 *      ../main/script.c ../main/keys.c ../main/store.c ../main/builtin.c -o ui_render && ./ui_render out
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "store.h"
#include "ui.h"

static uint16_t fb[UI_W * UI_H];

static bool rows(int i, ui_row_t *out)
{
    store_row_t r;
    if ( !store_row(i, &r) )
    {
        return false;
    }
    snprintf(out->title, sizeof(out->title), "%s", r.title);
    char sub[160]; snprintf(sub, sizeof(sub), "%s%s%s", r.arm, r.desc[0] ? "  -  " : "", r.desc); snprintf(out->sub, sizeof(out->sub), "%.63s", sub);
    out->nerr = r.nerr;
    out->on_card = r.on_card;
    return true;
}

static void shot(const char *dir, const char *name, const ui_model_t *m)
{
    gfx_surf_t s;
    gfx_surf_init(&s, fb, UI_W, UI_H, UI_W);
    ui_draw(&s, m);
    char path[256];
    snprintf(path, sizeof(path), "%s/%s.565", dir, name);
    FILE *f = fopen(path, "wb");
    if ( f )
    {
        fwrite(fb, 2, UI_W * UI_H, f);
        fclose(f);
    }
}

static bool f_ready(void *c) { return *(bool *)c; }
static int f_pending(void *c) { (void)c; return 0; }
static bool f_send(void *c, kstroke_t k, uint16_t d) { (void)c; (void)k; (void)d; return true; }

int main(int argc, char **argv)
{
    const char *dir = (argc > 1) ? argv[1] : ".";
    store_init();
    const char *mine = "title Lab PCs: wake-on-LAN on\ndesc F10, Advanced, Power\narm plug\nafter rearm\nspam F10 10\n";
    store_put_card("lab-wol", mine, strlen(mine));
    store_put_card("broken", "title Broken one\nkeys F10 NOPE\n", 30);
    ui_set_rows(rows);

    static sc_prog_t prog;
    static sc_player_t pl;
    bool host = true;
    const sc_io_t io = { f_ready, f_pending, f_send, NULL, NULL, &host };
    sc_player_init(&pl, &io);

    ui_model_t m;
    memset(&m, 0, sizeof(m));
    m.count = store_count();
    m.sel = 2;
    m.kb_ok = true;
    m.host = true;
    m.wifi = true;
    m.web_on = m.web_next = true;
    m.web_up = true;
    strcpy(m.ip, "192.168.1.57");
    m.pl = &pl;
    m.flash_key = -1;
    m.version = "1.0.0";
    m.card_ok = true;
    m.card_files = 2;
    m.now = 1000;
    shot(dir, "list", &m);

    m.host = false;
    m.sel = 3;
    m.top = 2;
    snprintf(m.toast, sizeof(m.toast), "Disarmed: 30 min without a PC");
    shot(dir, "list_scrolled", &m);
    m.toast[0] = 0;

    char text[STORE_TEXT_MAX];
    store_text(store_find_name("hp-t550-secure-boot"), text, sizeof(text));
    sc_parse(&prog, text);
    sc_load(&pl, &prog);
    m.view = UI_RUN;
    m.host = true;
    sc_start(&pl, m.now);
    shot(dir, "confirm", &m);
    sc_confirm(&pl, true, m.now);
    shot(dir, "armed_unplug", &m);
    pl.need_unplug = false;
    m.now = 1500;
    shot(dir, "armed", &m);
    sc_go(&pl, m.now);
    sc_tick(&pl, m.now);
    for ( int i = 0; i < 300; ++i )
    {
        m.now += 10;
        sc_tick(&pl, m.now);
    }
    shot(dir, "spam", &m);
    sc_next(&pl, m.now);
    for ( int i = 0; i < 400; ++i )
    {
        m.now += 10;
        sc_tick(&pl, m.now);
    }
    shot(dir, "run", &m);

    sc_parse(&prog, "title Example: every command\nask Is the BIOS password screen showing?\ncode 4\ninput tag 6 Asset tag\n");
    sc_load(&pl, &prog);
    sc_start(&pl, m.now);
    sc_tick(&pl, m.now);
    shot(dir, "ask", &m);
    sc_next(&pl, m.now);
    sc_tick(&pl, m.now);
    strcpy(m.entry, "12");
    shot(dir, "code", &m);
    sc_entry(&pl, "1234", m.now);
    for ( int i = 0; i < 500; ++i )
    {
        m.now += 10;
        sc_tick(&pl, m.now);
    }
    strcpy(m.entry, "");
    shot(dir, "input", &m);

    sc_parse(&prog, "title Batch: F12 on every PC\narm delay 5\nafter rearm\nkeys F12\n");
    sc_load(&pl, &prog);
    sc_start(&pl, m.now);
    m.now += 1200;
    shot(dir, "countdown", &m);
    m.now += 5000;
    sc_tick(&pl, m.now);
    for ( int i = 0; i < 100; ++i )
    {
        m.now += 10;
        sc_tick(&pl, m.now);
    }
    shot(dir, "done_batch", &m);

    m.view = UI_KEYS;
    m.flash_key = 10;
    shot(dir, "keys", &m);
    m.view = UI_SETTINGS_V;
    m.set_sel = 0;
    m.port_next = 1;
    shot(dir, "settings", &m);
    return 0;
}
