/* ---- BIOS helper: the watch as a USB keyboard, playing key profiles into a PC ---- */
/* The profile language is Dot BIOS Helper's (script.c); see bios.h. */

static bool bi_manual;
static char bi_code[17];
static int bi_code_n;

static void bi_btn(int x, int y, int w, int h, const char *l, uint16_t bg, uint16_t fg)
{
    kk_round(x, y, w, h, 18, bg);
    text_c(&KF_BODY, x + w / 2, y + (h - KF_BODY.cap) / 2, l, fg);
}

/* greedy word wrap, centred, at most @p max lines */
static void bi_wrap(const kf_font_t *f, int top, int width, const char *s, int max, uint16_t c)
{
    char line[80];
    int n = 0;
    while ( *s && (n < max) )
    {
        int len = 0, fit = 0;
        while ( s[len] )
        {
            int e = len;
            while ( s[e] && (' ' != s[e]) ) { ++e; }
            char tmp[80];
            int const k = (e < (int)sizeof(tmp) - 1) ? e : (int)sizeof(tmp) - 1;
            memcpy(tmp, s, (size_t)k);
            tmp[k] = 0;
            if ( (kf_width(f, tmp) > width) && fit ) { break; }
            fit = k;
            len = e;
            if ( !s[len] ) { break; }
            ++len;
        }
        if ( !fit ) { fit = (int)strlen(s); }
        int const k = (fit < (int)sizeof(line) - 1) ? fit : (int)sizeof(line) - 1;
        memcpy(line, s, (size_t)k);
        line[k] = 0;
        text_c(f, CX, top + n * (f->cap + 16), line, c);
        s += fit;
        while ( ' ' == *s ) { ++s; }
        ++n;
    }
}

#define BI_PX(c) (30 + (c) * 120)
#define BI_PW 110
#define BI_PH 54
static const char *const BI_PAD[3][3] = { { "ESC", "UP", "F10" }, { "LEFT", "DOWN", "RIGHT" }, { "TAB", "ENTER", "SPACE" } };
static const char *const BI_PADL[3][3] = { { "Esc", "Up", "F10" }, { "Left", "Down", "Right" }, { "Tab", "Enter", "Space" } };

static void bi_draw_pad(int y0)
{
    for ( int r = 0; r < 3; ++r )
    {
        for ( int c = 0; c < 3; ++c ) { bi_btn(BI_PX(c), y0 + r * (BI_PH + 8), BI_PW, BI_PH, BI_PADL[r][c], C_ROW, C_LIGHT); }
    }
}

static bool bi_tap_pad(int x, int y, int y0)
{
    for ( int r = 0; r < 3; ++r )
    {
        for ( int c = 0; c < 3; ++c )
        {
            if ( hit(x, y, BI_PX(c), y0 + r * (BI_PH + 8), BI_PW, BI_PH) )
            {
                uint8_t mod;
                uint8_t const k = hidkb_code(BI_PAD[r][c], &mod);
                (void)hidkb_tap(mod, k);
                sound_play(SND_CLICK);
                return true;
            }
        }
    }
    return false;
}

static void bi_status(void)
{
    const char *s;
    uint16_t col;
    if ( !hidkb_active() ) { s = bios_arm_plug() ? "plug-in arms it" : "keyboard off"; col = C_REST; }
    else if ( hidkb_ready() ) { s = "PC connected"; col = C_OK; }
    else { s = "no PC (USB)"; col = C_YEL; }
    kk_disc(CX - kf_width(&KF_CAPTION, s) / 2 - 14, 72, 5, col);
    text_c(&KF_CAPTION, CX, 66, s, col);
}

static void bi_open(void)
{
    bi_manual = false;
    bi_code_n = 0;
    bi_code[0] = 0;
    bios_select((int)kk_best_get("bi_prof"));
    bios_stop();
    bi_dock_last = docked;   /* already on USB power: no auto arm until a fresh plug-in */
}

/* the keyboard only goes up when asked: a watch on a charger PC must not type into it */
static bool bi_kb(void) { return hidkb_active() || hidkb_start(); }

static void bi_start(void)
{
    if ( bios_errors() ) { sound_play(SND_NOTE); return; }
    if ( bi_kb() ) { bios_start(kk_ms()); }
    sound_play(SND_CLICK);
}

#define BI_PRIMARY 30, 300, 350, 76
#define BI_SECOND 30, 400, 350, 64

static void draw_bios(void)
{
    uint32_t const now = kk_ms();
    const sc_player_t *pl = bios_player();
    int const st = bios_state();
    uint32_t const left = sc_left_ms(pl, now);
    char t[64];
    bi_status();
    if ( bi_manual )
    {
        text_c(&KF_BODY, CX, 110, "Manual keys", C_LIGHT);
        text_c(&KF_CAPTION, CX, 150, "every tap goes to the PC", C_REST);
        bi_draw_pad(200);
        bi_btn(BI_SECOND, "Back to profiles", C_OFF, C_LIGHT);
        back_bar("BIOS helper");
        return;
    }
    switch ( st )
    {
    case SC_CONFIRM:
        bi_wrap(&KF_BODY, 110, 350, bios_title(), 2, C_LIGHT);
        text_c(&KF_NAME, CX, 200, "Run on this PC?", C_YEL);
        bi_btn(BI_PRIMARY, "Yes, go", C_OK, KK_BLACK);
        bi_btn(BI_SECOND, "No", C_ROW, C_LIGHT);
        break;
    case SC_ARMED: {
        bool const blink = ((now / 500U) & 1U) != 0U;
        uint16_t const ring = pl->need_unplug ? C_YEL : (blink ? C_RED : C_OFF);
        kk_disc(CX, 190, 86, ring);
        kk_disc(CX, 190, 76, KK_BLACK);
        text_c(&KF_NAME, CX, 190 - KF_NAME.cap / 2, "ARMED", C_LIGHT);
        if ( pl->runs )
        {
            (void)snprintf(t, sizeof(t), "%d PC%s done", pl->runs, (1 == pl->runs) ? "" : "s");
            text_c(&KF_CAPTION, CX, 228, t, C_OK);
        }
        bi_wrap(&KF_CAPTION, 286, 360, pl->need_unplug ? "PC has the keyboard: unplug or switch it off first"
                                                        : "plug in or switch the PC on", 2, pl->need_unplug ? C_YEL : C_MED);
        if ( left )
        {
            (void)snprintf(t, sizeof(t), "disarms in %u min", (unsigned)((left + 59999U) / 60000U));
            text_c(&KF_CAPTION, CX, 360, t, C_REST);
        }
        bi_btn(30, 400, 170, 64, "Go now", C_BLUE, C_LIGHT);
        bi_btn(210, 400, 170, 64, "Disarm", C_RED, C_LIGHT);
        break;
    }
    case SC_COUNTDOWN:
        text_c(&KF_CAPTION, CX, 110, "starting in", C_REST);
        (void)snprintf(t, sizeof(t), "%u", (unsigned)((left + 999U) / 1000U));
        text_c(&KF_NAME, CX, 160, t, C_YEL);
        bi_btn(30, 400, 170, 64, "Go now", C_BLUE, C_LIGHT);
        bi_btn(210, 400, 170, 64, "Cancel", C_ROW, C_LIGHT);
        break;
    case SC_SPAM:
        (void)snprintf(t, sizeof(t), "Pressing %s", pl->text);
        text_c(&KF_NAME, CX, 110, t, ((now / 250U) % 2U) ? C_YEL : C_LIGHT);
        if ( !hidkb_ready() || !pl->until ) { text_c(&KF_BODY, CX, 160, "turn the PC on now", C_MED); }
        else
        {
            (void)snprintf(t, sizeof(t), "%u s", (unsigned)((left + 999U) / 1000U));
            text_c(&KF_BODY, CX, 160, t, C_MED);
        }
        (void)snprintf(t, sizeof(t), "step %d / %d", pl->pc + 1, pl->p->n);
        text_c(&KF_CAPTION, CX, 200, t, C_REST);
        bi_btn(30, 270, 350, 80, "In setup: Next", C_BLUE, C_LIGHT);
        bi_btn(BI_SECOND, "Stop", C_RED, C_LIGHT);
        break;
    case SC_ASK:
        (void)snprintf(t, sizeof(t), "step %d / %d", pl->pc + 1, pl->p->n);
        text_c(&KF_CAPTION, CX, 96, t, C_REST);
        bi_wrap(&KF_BODY, 124, 360, pl->text, 2, C_LIGHT);
        bi_draw_pad(196);
        bi_btn(30, 396, 350, 74, "Next", C_BLUE, C_LIGHT);
        break;
    case SC_WAITPC:
        text_c(&KF_NAME, CX, 140, "Waiting for a PC", C_LIGHT);
        bi_btn(30, 400, 170, 64, "Skip", C_BLUE, C_LIGHT);
        bi_btn(210, 400, 170, 64, "Stop", C_RED, C_LIGHT);
        break;
    case SC_ENTRY: {
        int const want = pl->entry_send ? pl->entry_n : 0;
        char shown[20];
        int i = 0;
        for ( ; (i < bi_code_n) && (i < (int)sizeof(shown) - 1); ++i ) { shown[i] = bi_code[i]; }
        for ( ; (i < want) && (i < (int)sizeof(shown) - 1); ++i ) { shown[i] = '-'; }
        shown[i] = 0;
        text_c(&KF_NAME, CX, 94, i ? shown : " ", C_LIGHT);
        text_c(&KF_CAPTION, CX, 140, pl->text, C_REST);
        bool const ready = pl->entry_send ? (bi_code_n >= want) : (bi_code_n > 0);
        static const char *const K[4][3] = { { "1", "2", "3" }, { "4", "5", "6" }, { "7", "8", "9" }, { "Del", "0", "OK" } };
        for ( int r = 0; r < 4; ++r )
        {
            for ( int c = 0; c < 3; ++c )
            {
                bool const ok = (3 == r) && (2 == c);
                bi_btn(BI_PX(c), 172 + r * 80, BI_PW, 72, K[r][c], ok ? (ready ? C_OK : C_OFF) : C_ROW, ok && ready ? KK_BLACK : C_LIGHT);
            }
        }
        break;
    }
    case SC_DONE:
        text_c(&KF_NAME, CX, 120, "Done", C_OK);
        (void)snprintf(t, sizeof(t), "ran %d time%s", pl->runs, (1 == pl->runs) ? "" : "s");
        text_c(&KF_CAPTION, CX, 168, t, C_REST);
        if ( pl->rearm_wait )
        {
            bi_wrap(&KF_BODY, 220, 350, "Unplug: it arms again for the next PC", 2, C_YEL);
            bi_btn(BI_SECOND, "Stop batch", C_RED, C_LIGHT);
        }
        else
        {
            text_c(&KF_CAPTION, CX, 200, "leave the app to get USB back", C_REST);
            bi_btn(30, 290, 350, 72, "Again", C_ROW, C_LIGHT);
            bi_btn(30, 400, 170, 64, "Manual", C_ROW, C_LIGHT);
            bi_btn(210, 400, 170, 64, "Profiles", C_ROW, C_LIGHT);
        }
        break;
    case SC_RUN:
        (void)snprintf(t, sizeof(t), "step %d / %d", ((pl->pc < pl->p->n) ? pl->pc : pl->p->n - 1) + 1, pl->p->n);
        text_c(&KF_NAME, CX, 140, t, C_LIGHT);
        bi_wrap(&KF_BODY, 200, 360, pl->text, 2, C_MED);
        if ( !hidkb_ready() ) { text_c(&KF_CAPTION, CX, 300, "no PC yet: the keys wait", C_YEL); }
        bi_btn(BI_SECOND, "Stop", C_RED, C_LIGHT);
        break;
    default: { /* SC_IDLE: the profile */
        bi_wrap(&KF_BODY, 100, 350, bios_title(), 2, C_LIGHT);
        if ( bios_errors() )
        {
            (void)snprintf(t, sizeof(t), "line %d: %s", bios_error_line(), bios_error());
            bi_wrap(&KF_CAPTION, 166, 360, t, 2, C_RED);
        }
        else
        {
            char arm[24];
            bios_arm_label(arm, sizeof(arm));
            (void)snprintf(t, sizeof(t), "%s  -  %d steps", arm, pl->p ? pl->p->n : 0);
            text_c(&KF_CAPTION, CX, 166, t, C_REST);
            if ( pl->note[0] ) { text_c(&KF_CAPTION, CX, 196, pl->note, C_YEL); }
        }
        int const pn = bios_profiles();
        if ( pn > 1 )   /* one dot per profile; swipe left/right */
        {
            int const step = (pn > 16) ? 12 : 18;
            int const x0 = CX - (pn - 1) * step / 2;
            for ( int i = 0; i < pn; ++i ) { kk_disc(x0 + i * step, 378, (i == bios_profile()) ? 5 : 3, (i == bios_profile()) ? C_LIGHT : C_REST); }
        }
        kk_disc(CX, 290, 70, bios_errors() ? C_OFF : C_OK);
        text_c(&KF_NAME, CX, 290 - KF_NAME.cap / 2, bios_arm_plug() ? "Arm" : "Start", KK_BLACK);
        bi_btn(30, 400, 170, 64, "Manual", C_ROW, C_LIGHT);
        bi_btn(210, 400, 170, 64, "Reload", C_ROW, C_LIGHT);
        break;
    }
    }
    back_bar("BIOS helper");
}

static void tap_bios(int x, int y)
{
    uint32_t const now = kk_ms();
    int const st = bios_state();
    if ( bi_manual )
    {
        if ( bi_tap_pad(x, y, 200) ) { return; }
        if ( hit(x, y, BI_SECOND) ) { bi_manual = false; }
        return;
    }
    switch ( st )
    {
    case SC_IDLE:
        if ( (x - CX) * (x - CX) + (y - 290) * (y - 290) < 76 * 76 ) { bi_start(); }
        else if ( hit(x, y, 30, 400, 170, 64) ) { bi_manual = bi_kb(); }
        else if ( hit(x, y, 210, 400, 170, 64) ) { bios_select(bios_profile()); sound_play(SND_CLICK); }
        break;
    case SC_CONFIRM:
        if ( hit(x, y, BI_PRIMARY) ) { bios_confirm(true, now); sound_play(SND_CLICK); }
        else if ( hit(x, y, BI_SECOND) ) { bios_confirm(false, now); }
        break;
    case SC_ARMED:
    case SC_COUNTDOWN:
        if ( hit(x, y, 30, 400, 170, 64) ) { bios_go(now); sound_play(SND_CLICK); }
        else if ( hit(x, y, 210, 400, 170, 64) ) { bios_stop(); }
        break;
    case SC_SPAM:
        if ( hit(x, y, 30, 270, 350, 80) ) { bios_next(now); sound_play(SND_CLICK); }
        else if ( hit(x, y, BI_SECOND) ) { bios_stop(); }
        break;
    case SC_ASK:
        if ( bi_tap_pad(x, y, 196) ) { return; }
        if ( hit(x, y, 30, 396, 350, 74) ) { bios_next(now); sound_play(SND_CLICK); }
        break;
    case SC_WAITPC:
        if ( hit(x, y, 30, 400, 170, 64) ) { bios_next(now); }
        else if ( hit(x, y, 210, 400, 170, 64) ) { bios_stop(); }
        break;
    case SC_ENTRY: {
        const sc_player_t *pl = bios_player();
        int const max = pl->entry_send ? pl->entry_n : 16;
        for ( int r = 0; r < 4; ++r )
        {
            for ( int c = 0; c < 3; ++c )
            {
                if ( !hit(x, y, BI_PX(c), 172 + r * 80, BI_PW, 72) ) { continue; }
                sound_play(SND_CLICK);
                if ( r < 3 ) { if ( bi_code_n < max ) { bi_code[bi_code_n++] = (char)('1' + r * 3 + c); } }
                else if ( 0 == c ) { if ( bi_code_n > 0 ) { --bi_code_n; } }
                else if ( 1 == c ) { if ( bi_code_n < max ) { bi_code[bi_code_n++] = '0'; } }
                else if ( pl->entry_send ? (bi_code_n >= max) : (bi_code_n > 0) )
                {
                    bi_code[bi_code_n] = 0;
                    bios_entry(bi_code, now);
                    bi_code_n = 0;
                }
                bi_code[bi_code_n] = 0;
                return;
            }
        }
        break;
    }
    case SC_DONE:
        if ( bios_player()->rearm_wait ) { if ( hit(x, y, BI_SECOND) ) { bios_stop(); } }
        else if ( hit(x, y, 30, 290, 350, 72) ) { bi_start(); }
        else if ( hit(x, y, 30, 400, 170, 64) ) { bi_manual = bi_kb(); }
        else if ( hit(x, y, 210, 400, 170, 64) ) { bios_select(bios_profile()); }
        break;
    default:
        if ( hit(x, y, BI_SECOND) ) { bios_stop(); }
        break;
    }
}

