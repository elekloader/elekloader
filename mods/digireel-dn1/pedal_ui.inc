/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The pedal shell's UI (pedal.h): its effect for fxkit, its Mod Menu entry, its screen, its keys and knobs, its
 * MIDI CCs' gate, its project data. Included by a pedal's ui.c after it defines what makes it that pedal:
 *
 *   PD_TAG, PD_NAME, PD_SAVE_VER          the projdata tag, the Mod Menu's label, the save's version
 *   pd_icon[16]                           its Mod Menu icon
 *   pd_title[3]                           the three pages' names
 *   pd_klbl, pd_kdef, PD_KBIP             page 1: eight knobs' labels, defaults, the bipolar ones (a bit each)
 *   pd_slbl, pd_sdef, PD_SBIP             page 2 (also FUNC + a knob on page 1): the same
 *   pd_olbl, pd_onum, pd_odef, pd_oval    page 3: eight settings (pd_onum 0: an empty slot), value names
 *   PD_NCFG, pd_cfg_lbl/num/def/val       its own SETUP rows (up to 4), after the shell's
 *   hooks: fx_defaults() fx_opened() fx_tick() fx_sig() fx_tkey(t, ev) fx_trig(n) fx_cc(cc, v)
 *          fx_value(bmp, font, x, y, pg, i) (nonzero: drawn) fx_left(bmp) fx_status() fx_off(pg, i)
 *   PD_CLEAR_LO, PD_CLEAR_HI              the bulk region's part the render reads before it writes it
 *
 *   its Mod Menu entry makes it fxkit's active effect (the synths muted) and opens its page
 *   SYN1, SYN2, FLTR  its three pages;  AMP  the MIXER (fxkit's);  LFO  LFO 1, again LFO 2 (fxkit's: DEST lists
 *   only the pedal's controls and the mixer's);  LEFT, RIGHT or PAGE  the six in turn. Push a knob: its default,
 *   push a setting: its next value
 *   RECORD     LOCK mode: hold a trig and turn a control, that step's p-lock (FUNC + trig clears the step, PAGE
 *              the next 16 steps); the locks are the pattern's, in the LOCK TRACK's lock table (fxkit)
 *   T1-T4      the pedal's own (fx_tkey): pressed, tapped, held (core-dn1's ev_hold), released after a hold
 *   trig 1-14  the pedal's own (fx_trig), outside LOCK mode
 *   trig 15    TAP: tap a tempo (the clock becomes TAP)
 *   trig 16    BYPASS (the mixer's BYP; with TRAILS the tails ring out)
 *   YES SETUP, NO back (from LOCK mode: out of it; from a page: the pedal off);  MIDI  the stock UI, for the MIDI
 *   tracks, the pedal playing on (its entry brings the page back);  PLAY, STOP, TEMPO and LEVEL/DATA stay stock
 *   MIDI: the pedal's own CCs (fx_cc), on the auto channel (or any, in SETUP), while it is active (or always,
 *   with CC CLOSED)
 * The controls the page shows are the pedal's own values (a held step's lock in LOCK mode); ST's are what plays. */

#pragma GCC optimize ("Os")                /* the UI: small beats fast (the RAM the mods share is 128 KB) */

#define KEY_FUNC    1
#define KEY_MIDI    2
#define KEY_TEMPO   9
#define KEY_RECORD  10
#define KEY_PLAY    11
#define KEY_STOP    12
#define KEY_YES     13
#define KEY_NO      14
#define KEY_UP      15
#define KEY_DOWN    16
#define KEY_LEFT    17
#define KEY_RIGHT   18
#define KEY_PAGE    19
#define KEY_TRIG    20
#define KEY_SYN1    21
#define KEY_SYN2    22
#define KEY_FLTR    23
#define KEY_AMP     24
#define KEY_LFO     25
#define KEY_STEP1   26
#define KEY_T1      42
#define KEY_PUSH_A  46
#define EV_KEY(ev)   (*(u32 *)((char *)(ev) + 12))
#define EV_FLAGS(ev) (*(u32 *)((char *)(ev) + 16))
#define EV_DELTA(ev) (*(s32 *)((char *)(ev) + 16))
#define COUNTS      4                       /* encoder counts a detent */
#define ST          PD(st)
#define EFF         (&PD(effect))

/* the pedal's own values (not what plays): a hook that steps a setting starts from these */
#define KNOB(i)     ((int)(fx_get(EFF, PD_S_KNOB(i)) >> 8))
#define SEC(i)      ((int)(fx_get(EFF, PD_S_SEC(i)) >> 8))
#define OPT(i)      ((int)(fx_get(EFF, PD_S_OPT(i)) >> 8))

/* the pedal's hooks, defined after this file (they may use its helpers) */
static void fx_defaults(void);              /* its latches and own state, for a new project */
static void fx_opened(void);                /* the page opened */
static void fx_tick(void);                  /* every UI tick */
static u32 fx_sig(void);                    /* what else its screen shows, for the redraw */
static void fx_tkey(int t, int ev);         /* T1-T4: TK_* */
static void fx_trig(int n);                 /* trig 1-14 (0-13), without SETUP or FUNC */
static int fx_cc(int cc, int v);            /* a CC for the pedal: nonzero, taken */
static int fx_value(void *bmp, const void *font, int x, int y, int pg, int i);  /* a value's text: nonzero, drawn */
static void fx_left(void *bmp);             /* the left column's box, 0,27 .. 14,50 */
static const char *fx_status(void);         /* the title bar's text, 0 for the page's name */
static int fx_off(int pg, int i);           /* nonzero: that control does nothing now (drawn dotted) */

typedef void (*rect_fn)(void *bmp, int x0, int y0, int x1, int y1, int colour);
typedef void (*text_fn)(void *bmp, const void *font, int x, int y, int maxlen, const char *fmt, ...);
#define FILLRECT  ((rect_fn)fw_fillrect)
#define FRAMERECT ((rect_fn)fw_framerect)
#define TEXTF     ((text_fn)fw_textf)
#define FONT5     ((const void *)fw_font5)          /* 3 x 5: the stock value boxes' */
#define FONT_LBL  ((const void *)fw_font_label)     /* the stock labels' */
#define FONT_TTL  ((const void *)fw_font_title)     /* the stock title bar's */

enum { G_INPUT, G_CLOCK, G_BYPASS, G_MIDI, G_CLOSED, G_LOCK, G_SHELL };     /* the shell's SETUP rows */
#define G_COUNT (G_SHELL + PD_NCFG)
static const char *const shell_lbl[G_SHELL] = { "INPUT", "CLOCK", "BYPASS", "MIDI CC", "CC CLOSED", "LOCK TRACK" };

static u8 ui_open, ui_setup, ui_pg, ui_func, ui_ready, ui_inited, ui_row, ui_pending;
static u8 ui_flash_ticks, ui_flash_pg, ui_flash_i;   /* a control's value in the title bar after a turn */
#define NPAGE 6                             /* the pedal's three, the MIXER, LFO 1, LFO 2 */
static u8 cfg_midi_any, cfg_closed, in_cc;
static u32 clear_at;                        /* the next address to clear, 0 when nothing is */
static s32 enc_acc[8];
static u8 t_down[4], t_held[4];             /* the track keys: pressed, and held (ev_hold took them) */
static u32 t_at[4];                         /* when each went down, in blocks */
static u32 tap_last, tap_iv[3], tap_n;

static void send_cmd(int c)                 /* a one-shot command for the render's next block */
{
    u8 w = ST.cw;
    if ((u8)(w - ST.cr) >= PD_NCMD)
        return;                             /* full: the render is not running */
    ST.cmdq[w % PD_NCMD] = (u8)c;
    ST.cw = (u8)(w + 1);
}

/* ---- the effect: the pedal's controls as fxkit's slots ---------------------------------------------- */
static struct fx_param pd_params[25];

static void pd_fill(void)                   /* the controls (the labels: a pedal may change them) */
{
    int i, n = 0;
    for (i = 0; i < 8; i++) {
        struct fx_param *p = &pd_params[n++];
        p->slot = (u8)PD_S_KNOB(i);
        p->kind = (u8)(((PD_KBIP >> i) & 1) ? FX_BIP : FX_KNOB);
        p->def = pd_kdef[i];
        p->name = p->lname = pd_klbl[i];
    }
    for (i = 0; i < 8; i++) {
        struct fx_param *p = &pd_params[n++];
        p->slot = (u8)PD_S_SEC(i);
        p->kind = (u8)(((PD_SBIP >> i) & 1) ? FX_BIP : FX_KNOB);
        p->def = pd_sdef[i];
        p->name = p->lname = pd_slbl[i];
    }
    for (i = 0; i < 8; i++) {
        struct fx_param *p;
        if (!pd_onum[i])
            continue;
        p = &pd_params[n++];
        p->slot = (u8)PD_S_OPT(i);
        p->kind = FX_OPT;
        p->n = pd_onum[i];
        p->def = pd_odef[i];
        p->name = p->lname = pd_olbl[i];
        p->opts = pd_oval[i];
    }
    pd_params[n].slot = 0;
}

static int labels_moved(void)               /* a label the pedal changed since pd_fill */
{
    int i;
    for (i = 0; i < 8; i++)
        if (pd_params[i].name != pd_klbl[i] || pd_params[8 + i].name != pd_slbl[i])
            return 1;
    return 0;
}

/* The render's: this block's values (the pedal's own, a step's lock, an LFO on it) into ST */
static void pd_values(const int16_t *val, uint32_t flags)
{
    int i;
    for (i = 0; i < 8; i++) {
        s32 k = val[PD_S_KNOB(i)] >> 8, s = val[PD_S_SEC(i)] >> 8, o = val[PD_S_OPT(i)] >> 8;
        ST.knob[i] = (u8)(k < 0 ? 0 : k > 127 ? 127 : k);
        ST.sec[i] = (u8)(s < 0 ? 0 : s > 127 ? 127 : s);
        if (pd_onum[i])
            ST.opt[i] = (u8)(o < 0 ? 0 : o >= pd_onum[i] ? pd_onum[i] - 1 : o);
    }
    ST.bypass = (u8)((flags & FX_V_BYPASS) != 0);
}

const struct fx_effect PD(effect) = {
    FX_ABI, PD_NAME, pd_params, FX_OWN_MIX | FX_OWN_BYPASS, pd_fill, pd_values, PD(render),
};

static int slot_of(int pg, int i)
{
    return pg == 0 ? PD_S_KNOB(i) : pg == 1 ? PD_S_SEC(i) : PD_S_OPT(i);
}

static int ctl(int pg, int i)               /* what a control shows: its value, or the held step's lock */
{
    int k = slot_of(pg, i), v = fx_view(k, fx_get(EFF, k)) >> 8;
    if (pg == 2)
        return pd_onum[i] ? (v < 0 ? 0 : v >= pd_onum[i] ? pd_onum[i] - 1 : v) : 0;
    return v < 0 ? 0 : v > 127 ? 127 : v;
}

/* A knob (pg 0), a secondary (1) or a setting (2): the pedal's own value; played from the next block. From the
 * MIDI task (fx_cc), at the UI task's next tick. */
static void set_ctl(int pg, int i, s32 v)
{
    if (pg == 2) {
        if (!pd_onum[i])
            return;
        v = v < 0 ? 0 : v >= pd_onum[i] ? pd_onum[i] - 1 : v;
        ST.opt[i] = (u8)v;
    } else {
        v = v < 0 ? 0 : v > 127 ? 127 : v;
        if (pg)
            ST.sec[i] = (u8)v;
        else
            ST.knob[i] = (u8)v;
    }
    if (in_cc)
        fx_set_later(EFF, slot_of(pg, i), v << 8);
    else
        fx_set(EFF, slot_of(pg, i), v << 8);
}

static void set_opt(int i, int v)           /* a pedal's own change of a setting */
{
    set_ctl(2, i, v);
}

static void turn_ctl(int pg, int i, s32 v)  /* a control turned or pushed: in LOCK mode with a step held, its lock */
{
    if (fx_held_step() < 0) {
        set_ctl(pg, i, v);
        return;
    }
    if (pg == 2 && !pd_onum[i])
        return;
    v = v < 0 ? 0 : pg == 2 ? (v >= pd_onum[i] ? pd_onum[i] - 1 : v) : v > 127 ? 127 : v;
    fx_turned(EFF, slot_of(pg, i), v << 8);
}

/* ---- project data: the pedal, its mixer and its LFOs, saved with the project ---------------------------
 * 52 bytes (the gap mods share in a project holds 480: core leaves out a block that does not fit). The p-locks
 * are the pattern's. */
#define SAVE_VER (PD_SAVE_VER | 0x40)       /* 0.2's: packed, the mixer and the LFOs, no presets */
struct pd_save {
    u8 ver, cfgb, clock, sw;                /* cfgb: MIDI CC ANY, CC CLOSED, MONO, TRAILS; sw: the latches */
    u16 tap;
    u8 cfg[2];                              /* the pedal's SETUP rows, 4 bits each */
    u8 knob[8], sec[8];
    u8 opt[4];                              /* the settings, 4 bits each */
    u8 fx[FX_PACK];                         /* fxkit's: the mixer, the LFOs */
} __attribute__((aligned(4)));
static struct pd_save save;

static void defaults(void)
{
    int i;
    for (i = 0; i < 8; i++) {
        set_ctl(0, i, pd_kdef[i]);
        set_ctl(1, i, pd_sdef[i]);
        set_ctl(2, i, pd_odef[i]);
    }
    fx_unpack(EFF, 0);
    for (i = 0; i < 4; i++)
        ST.sw[i] = 0;
    for (i = 0; i < PD_NCFG; i++)
        ST.cfg[i] = pd_cfg_def[i];
    ST.trails = 1;
    ST.clock = CL_TEMPO;
    ST.tap = 14400;
    ST.mono = 0;
    cfg_midi_any = cfg_closed = 0;
    fx_defaults();
}

static void loaded(int found)
{
    int i;
    if (!found || save.ver != SAVE_VER) {
        defaults();
    } else {
        for (i = 0; i < 8; i++) {
            int o = (save.opt[i / 2] >> (4 * (i & 1))) & 15;
            set_ctl(0, i, save.knob[i] & 127);
            set_ctl(1, i, save.sec[i] & 127);
            set_ctl(2, i, pd_onum[i] && o < pd_onum[i] ? o : pd_odef[i]);
        }
        fx_unpack(EFF, save.fx);
        for (i = 0; i < 4; i++)
            ST.sw[i] = (save.sw >> i) & 1;
        for (i = 0; i < PD_NCFG; i++) {
            int c = (save.cfg[i / 2] >> (4 * (i & 1))) & 15;
            ST.cfg[i] = (u8)(c < pd_cfg_num[i] ? c : pd_cfg_def[i]);
        }
        cfg_midi_any = save.cfgb & 1;
        cfg_closed = (save.cfgb >> 1) & 1;
        ST.mono = (save.cfgb >> 2) & 1;
        ST.trails = (save.cfgb >> 3) & 1;
        ST.clock = save.clock < 3 ? save.clock : CL_TEMPO;
        ST.tap = save.tap >= 2400 && save.tap <= 48000 ? save.tap : 14400;
    }
    ui_inited = 1;
}

const struct core_projdata PD(projdata) = { PD_TAG, sizeof(struct pd_save), &save, loaded };

static u32 saved_serial = ~0u;

static void keep_save(void)                 /* the pedal into the project's copy (its controls when they moved) */
{
    int i;
    save.ver = SAVE_VER;
    if (fx_serial(EFF) != saved_serial) {
        saved_serial = fx_serial(EFF);
        for (i = 0; i < 8; i++) {
            save.knob[i] = (u8)KNOB(i);
            save.sec[i] = (u8)SEC(i);
        }
        for (i = 0; i < 4; i++) {
            int a = pd_onum[2 * i] ? OPT(2 * i) : 0, b = pd_onum[2 * i + 1] ? OPT(2 * i + 1) : 0;
            save.opt[i] = (u8)((a & 15) | (b & 15) << 4);
        }
        fx_pack(EFF, save.fx);
    }
    save.sw = (u8)(ST.sw[0] | ST.sw[1] << 1 | ST.sw[2] << 2 | ST.sw[3] << 3);
    save.cfg[0] = (u8)((ST.cfg[0] & 15) | (ST.cfg[1] & 15) << 4);
    save.cfg[1] = (u8)((ST.cfg[2] & 15) | (ST.cfg[3] & 15) << 4);
    save.cfgb = (u8)(cfg_midi_any | cfg_closed << 1 | ST.mono << 2 | ST.trails << 3);
    save.clock = ST.clock;
    save.tap = (u16)ST.tap;
}

/* ---- opening and closing -------------------------------------------------------------------------- */
static void bypass_set(int b)               /* the mixer's BYP */
{
    ST.bypass = (u8)b;
    ST.overload = 0;
    if (in_cc)
        fx_set_later(EFF, FX_M_BYP, b << 8);
    else
        fx_set(EFF, FX_M_BYP, b << 8);
}

static void activate(void)
{
    if (fx_active() != EFF) {               /* (back from MIDI: as it was) */
        bypass_set(0);                      /* switching it on steps on it */
        fx_activate(EFF);
    }
    PD(on) = 1;
    ui_pending = 0;
}

static void pd_open(void *brain, void *ev, int track)
{
    (void)brain, (void)ev, (void)track;
    ui_open = 1;
    ui_setup = 0;
    ui_pg = 0;
    ui_func = 0;
    if (ui_ready)
        activate();
    else
        ui_pending = 1;                     /* the render's buffers are not cleared yet: at the tick that ends it */
    fx_opened();
}

static void pd_hide(void)                   /* the page gone; the keys still down as if released */
{
    int t;
    ui_open = 0;
    fx_lock_mode_set(0);
    for (t = 0; t < 4; t++) {
        if (t_down[t] || ST.held[t]) {
            ST.held[t] = 0;
            fx_tkey(t, t_held[t] ? TK_UP : TK_TAP);
        }
        t_down[t] = t_held[t] = 0;
    }
}

static void pd_close(void)                  /* NO: the pedal off, the synths again */
{
    pd_hide();
    ui_pending = 0;
    fx_deactivate(EFF);
    PD(on) = 0;
}

const struct core_menu_item PD(menu) = { PD_NAME, pd_open, CORE_MENU_ICON, pd_icon };

/* ---- knobs and keys ------------------------------------------------------------------------------- */
static void flash(int pg, int i)              /* the control's value in the title bar, a second */
{
    ui_flash_pg = (u8)pg;
    ui_flash_i = (u8)i;
    ui_flash_ticks = 30;
}

static void cc_opt(int i, int v)              /* a setting from a CC: its count's bands over 0-127 */
{
    if (pd_onum[i])
        set_ctl(2, i, v * pd_onum[i] / 128);
}

static int page_now(void)                    /* FUNC held on the pedal's pages shows page 2 */
{
    return ui_func && ui_pg < 3 ? 1 : ui_pg;
}

static void push(int i)                      /* an encoder pushed */
{
    int pg = page_now();
    if (pg >= 3) {
        fx_page_push(EFF, pg - 3, i);       /* its value under the knob, as the stock's */
        return;
    } else if (pg < 2)
        turn_ctl(pg, i, pg ? pd_sdef[i] : pd_kdef[i]);
    else if (pd_onum[i])
        turn_ctl(2, i, (ctl(2, i) + 1) % pd_onum[i]);
    else
        return;
    flash(pg, i);
}

static void tap(void)                        /* the TAP footswitch: the mean of the last taps */
{
    u32 now = ST.blocks, iv = now - tap_last, sum = 0, k;
    tap_last = now;
    if (iv < 225 || iv > 4500) {             /* 400 .. 20 BPM: a new series */
        tap_n = 0;
        return;
    }
    tap_iv[tap_n % 3] = iv;
    tap_n++;
    for (k = 0; k < (tap_n < 3 ? tap_n : 3); k++)
        sum += tap_iv[k];
    ST.tap = 90000u * 120u * (tap_n < 3 ? tap_n : 3) / sum;    /* 1500 blocks a second: BPM x 120 */
    ST.clock = CL_TAP;
}

int PD(key)(void *brain, void *ev)
{
    u32 key = EV_KEY(ev), fl = EV_FLAGS(ev);
    int press = (fl & 1) && !(fl & 8), released = (fl & 0x10) != 0;
    (void)brain;
    if (!ui_open)
        return 0;
    if (key == KEY_FUNC) {                  /* down: flags 1 (5 for a double press); up: 0x10, or 0 after
                                               another key went down meanwhile */
        ui_func = (u8)(fl & 1);
        return 1;
    }
    if (fl & 2)                             /* another key's event: FUNC is held */
        ui_func = 1;
    if (key == KEY_PLAY || key == KEY_STOP || key == KEY_TEMPO)
        return 0;                           /* the sequencer and its tempo stay the Digitone's */
    if (!ui_setup && page_now() >= 3 && fx_page_key(EFF, page_now() - 3, key, fl))
        return 1;                           /* DEST's list on an LFO page */
    if (!ui_setup && fx_lock_key(EFF, key, fl, ui_func))
        return 1;                           /* LOCK mode's trigs and PAGE */
    if (released) {
        if (key >= KEY_T1 && key < KEY_T1 + 4) {
            int t = (int)(key - KEY_T1);
            if (t_down[t]) {
                ST.held[t] = 0;
                if (!t_held[t] && ST.blocks - t_at[t] >= 600) {     /* down 0.4 s with no ev_hold: a hold all
                                                                   the same, never a tap */
                    t_held[t] = 1;
                    fx_tkey(t, TK_HOLD);
                }
                fx_tkey(t, t_held[t] ? TK_UP : TK_TAP);
            }
            t_down[t] = t_held[t] = 0;
        }
        return 1;
    }
    if (!press)
        return 1;                           /* repeats; ev_hold sees the track keys held */
    if (key == KEY_MIDI) {
        pd_hide();
        return 0;                           /* the stock's MIDI mode, the pedal playing on */
    } else if (key == KEY_RECORD) {
        if (!ui_setup)
            fx_lock_mode_set(!fx_lock_mode());
    } else if (key >= KEY_STEP1 && key < KEY_STEP1 + 16 && (ui_setup || ui_func)) {
        ;                                   /* (0.1's presets) */
    } else if (key >= KEY_T1 && key < KEY_T1 + 4) {
        int t = (int)(key - KEY_T1);
        t_down[t] = 1;
        t_held[t] = 0;
        ST.held[t] = 1;
        t_at[t] = ST.blocks;
        fx_tkey(t, TK_PRESS);
    } else if (key == KEY_STEP1 + 14) {     /* TAP */
        tap();
    } else if (key == KEY_STEP1 + 15) {     /* BYPASS */
        bypass_set(!ST.bypass);
    } else if (key >= KEY_STEP1 && key < KEY_STEP1 + 14) {
        fx_trig((int)(key - KEY_STEP1));
    } else if (key == KEY_YES) {
        ui_setup = !ui_setup;
    } else if (key == KEY_NO) {
        if (ui_setup)
            ui_setup = 0;
        else if (fx_lock_mode())
            fx_lock_mode_set(0);
        else
            pd_close();
    } else if (ui_setup && (key == KEY_UP || key == KEY_DOWN)) {
        ui_row = (u8)((ui_row + (key == KEY_UP ? G_COUNT - 1 : 1)) % G_COUNT);
    } else if (ui_setup && (key == KEY_LEFT || key == KEY_RIGHT)) {
        int d = key == KEY_RIGHT ? 1 : -1;
        if (ui_row == G_INPUT)
            ST.mono = !ST.mono;
        else if (ui_row == G_CLOCK)
            ST.clock = (u8)((ST.clock + 3 + d) % 3);
        else if (ui_row == G_BYPASS)
            ST.trails = !ST.trails;
        else if (ui_row == G_MIDI)
            cfg_midi_any = !cfg_midi_any;
        else if (ui_row == G_CLOSED)
            cfg_closed = !cfg_closed;
        else if (ui_row == G_LOCK)
            fx_lock_track_set((fx_lock_track() + 4 + d) % 4);
        else {
            int r = ui_row - G_SHELL;
            ST.cfg[r] = (u8)((ST.cfg[r] + pd_cfg_num[r] + d) % pd_cfg_num[r]);
        }
    } else if (ui_setup) {
        ;
    } else if (key >= KEY_SYN1 && key <= KEY_LFO) {
        ui_pg = (u8)(key == KEY_LFO ? (ui_pg == 4 ? 5 : 4) : key - KEY_SYN1);
        ui_flash_ticks = 0;
    } else if (key == KEY_LEFT || key == KEY_RIGHT || key == KEY_PAGE) {
        ui_pg = (u8)((ui_pg + (key == KEY_LEFT ? NPAGE - 1 : 1)) % NPAGE);
        ui_flash_ticks = 0;
    } else if (key >= KEY_PUSH_A && key < KEY_PUSH_A + 8) {
        push((int)(key - KEY_PUSH_A));
    }
    return 1;
}

/* core-dn1's ev_hold, a track key held on its own: while open, the pedal's hold of T1-T4. */
int PD(hold)(void *brain, void *ev, int track)
{
    (void)brain, (void)ev;
    if (!ui_open || track < 0 || track > 3)
        return 0;
    if (t_down[track] && !t_held[track]) {
        t_held[track] = 1;
        fx_tkey(track, TK_HOLD);
    }
    return 1;
}

int PD(enc)(void *brain, void *ev)
{
    u32 e = EV_KEY(ev);
    s32 d;
    int i, pg;
    (void)brain;
    if (!ui_open || e < 1 || e > 8)
        return 0;                           /* LEVEL/DATA stays the Digitone's */
    if (ui_setup)
        return 1;
    i = (int)e - 1;
    enc_acc[i] += EV_DELTA(ev);
    d = enc_acc[i] / COUNTS;
    enc_acc[i] -= d * COUNTS;
    if (!d)
        return 1;
    pg = page_now();
    if (pg >= 3) {
        fx_page_turn(EFF, pg - 3, i, d);    /* its value under the knob, as the stock's */
        return 1;
    } else if (pg < 2) {
        turn_ctl(pg, i, ctl(pg, i) + d);
    } else if (pd_onum[i]) {
        turn_ctl(2, i, ctl(2, i) + (d > 0 ? 1 : -1));
    } else {
        return 1;
    }
    flash(pg, i);
    return 1;
}

/* ---- MIDI (core-dn1 3.2's ev_midi_cc, the MIDI task) -----------------------------------------------------
 * While the pedal is active (or always, with SETUP's CC CLOSED on) its CCs (fx_cc) are its own when they come
 * on the auto channel (or on a track's channel too, with SETUP's MIDI CC: ANY CH): the Digitone does not apply
 * them. Every other CC goes on to the Digitone. */
int PD(cc)(int track, int cc, int value, int flags)
{
    int took;
    (void)track;
    if (!ui_ready || (!PD(on) && !cfg_closed) || (!(flags & CORE_MIDI_CC_AUTO) && !cfg_midi_any))
        return 0;
    if (value < 0 || value > 127)
        return 0;
    in_cc = 1;
    took = fx_cc(cc, value);
    in_cc = 0;
    return took;
}

/* ---- the UI task: clearing, the effect, redraws ------------------------------------------------------ */
static u32 ui_sig, ui_ticks;

static u32 signature(void)                  /* what the page shows, but the meter, folded into a word */
{
    int k, pg = page_now();
    u32 h = (u32)(pg | ui_setup << 3 | ui_row << 4 | (ui_flash_ticks != 0) << 8 | ST.bypass << 9
                  | ST.overload << 10 | ST.clock << 11 | ST.trails << 13 | ST.mono << 14 | cfg_midi_any << 15
                  | cfg_closed << 16 | fx_lock_mode() << 17 | fx_overload() << 18 | fx_lock_page() << 19
                  | fx_lock_track() << 21 | (u32)(fx_held_step() + 1) << 23);
    for (k = 0; k < 8; k++) {
        if (pg >= 3) {
            int s = fx_page_slot(pg - 3, k);
            h = h * 31 + (u32)fx_view(s, fx_get(EFF, s));
        } else {
            h = h * 31 + (u32)(ctl(0, k) + (ctl(1, k) << 8) + (ctl(2, k) << 16));
        }
    }
    for (k = 0; k < 4; k++)
        h = h * 31 + ST.sw[k] + (ST.held[k] << 1) + (ST.cfg[k] << 2);
    h = h * 31 + (fx_lock_mode() ? fx_lock_mask(EFF, fx_lock_page()) : 0) + (pg >= 3 ? fx_page_state() : 0);
    h = h * 31 + ST.load / 10 + ((u32)fw_tempo << 8) + (ST.tap << 3);
    return h * 31 + fx_sig();
}

void PD(tick)(void *ctrl)
{
    if (!ui_inited) {                       /* no project load reached us */
        defaults();
        ui_inited = 1;
    }
    if (!ui_ready) {                        /* the first time: clear what the render reads, 256 KB a tick */
        u32 *p, *end;
        if (!clear_at)
            clear_at = PD_CLEAR_LO;
        p = (u32 *)clear_at;
        end = p + 65536;
        if ((u32)end > PD_CLEAR_HI)
            end = (u32 *)PD_CLEAR_HI;
        while (p < end)
            *p++ = 0;
        clear_at = (u32)end;
        if (clear_at >= PD_CLEAR_HI) {
            PD(dsp_reset)();
            ST.active = 1;
            ui_ready = 1;
        }
    }
    if (ui_pending && ui_ready)
        activate();
    PD(on) = (u8)(fx_active() == EFF);
    if (ui_open && !PD(on) && !ui_pending)
        pd_hide();                          /* another effect took the audio */
    if (labels_moved())                     /* the pedal renamed a control: fxkit's DEST names too */
        pd_fill();
    ST.meter[0] = (u16)(ST.meter[0] - (ST.meter[0] >> 3));
    ST.meter[1] = (u16)(ST.meter[1] - (ST.meter[1] >> 3));
    keep_save();
    fx_tick();
    if (ui_flash_ticks)
        ui_flash_ticks--;
    if (ui_open) {                          /* a redraw when what the page shows changed, or every fourth tick
                                               for the meter: the render leaves the UI task less time than stock */
        u32 sg = signature();
        if (sg != ui_sig || (++ui_ticks & 3) == 0) {
            ui_sig = sg;
            *((unsigned char *)ctrl + 0x20) = 1;
        }
    }
}

/* ---- the screen: 128 x 64, y = 0 the bottom row ------------------------------------------------------ */
static void value_text(void *bmp, const void *font, int x, int y, int pg, int i)
{
    int v;
    char buf[8];
    if (pg >= 3) {
        int s = fx_page_slot(pg - 3, i);
        fx_text(EFF, s, fx_view(s, fx_get(EFF, s)), buf);
        TEXTF(bmp, font, x, y, -1, "%s", buf);
        return;
    }
    if (fx_value(bmp, font, x, y, pg, i))
        return;
    v = ctl(pg, i);
    if (pg == 2) {
        TEXTF(bmp, font, x, y, -1, "%s", pd_oval[i][v % (pd_onum[i] ? pd_onum[i] : 1)]);
        return;
    }
    if (((pg ? PD_SBIP : PD_KBIP) >> i) & 1)
        TEXTF(bmp, font, x, y, -1, "%c%d", v >= 64 ? '+' : '-', v >= 64 ? v - 64 : 64 - v);
    else
        TEXTF(bmp, font, x, y, -1, "%d", v);
}

/* As the Digitone's own parameter pages (traced from its FLTR, AMP and LFO pages): an inverted box (here the
 * tempo) and title bar on top, a column on the left (the pedal's state, the output's meter, as SYN and LEV) and
 * two rows of four controls, each 17 pixels square over its label: knobs and value boxes. */
static const u8 col_x[4] = { 33, 59, 86, 112 };
#define ROW_Y(r)    ((r) ? 15 : 42)         /* a control's centre, rows 1 and 2 */
#define LBL_Y(r)    ((r) ? 0 : 27)          /* its label's bottom row */
static const signed char ring[12][2] = { {0, 8}, {1, 8}, {2, 8}, {3, 7}, {4, 7}, {5, 6}, {6, 5}, {7, 4}, {7, 3}, {8, 2},
                                {8, 1}, {8, 0} };             /* a knob's circle, one quadrant */
static const signed char needle[29][2] = {             /* its pointer: 270 degrees from 7:30 to 4:30, radius 6 */
    {-4, -4}, {-5, -3}, {-5, -3}, {-6, -2}, {-6, -1}, {-6, 0}, {-6, 1}, {-6, 2}, {-5, 3}, {-4, 4}, {-4, 5},
    {-3, 5}, {-2, 6}, {-1, 6}, {0, 6}, {1, 6}, {2, 6}, {3, 5}, {4, 5}, {4, 4}, {5, 3}, {6, 2}, {6, 1}, {6, 0},
    {6, -1}, {6, -2}, {5, -3}, {5, -3}, {4, -4} };

static void px(void *bmp, int x, int y, int c)
{
    FILLRECT(bmp, x, y, x, y, c);
}

static void corners(void *bmp, int x0, int y0, int x1, int y1, int c)
{
    px(bmp, x0, y0, c);
    px(bmp, x1, y0, c);
    px(bmp, x0, y1, c);
    px(bmp, x1, y1, c);
}

static void rframe(void *bmp, int x0, int y0, int x1, int y1)    /* a frame with the corners off */
{
    FRAMERECT(bmp, x0, y0, x1, y1, 1);
    corners(bmp, x0, y0, x1, y1, 0);
}

static void line(void *bmp, int x0, int y0, int x1, int y1)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;
    for (;;) {
        px(bmp, x0, y0, 1);
        if (x0 == x1 && y0 == y1)
            break;
        e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static int text_w(const char *s)            /* FONT5's capitals, near enough */
{
    int n = 0;
    while (s[n])
        n++;
    return n ? 5 * n - 1 : 0;
}

static void small_text(void *bmp, int cx, int cy, const char *s)    /* font5, centred on cx, cy */
{
    int n = 0;
    while (s[n])
        n++;
    TEXTF(bmp, FONT5, cx - (4 * n - 1) / 2, cy - 2, -1, "%s", s);
}

static void label(void *bmp, int cx, int y, const char *s)
{
    if (*s)
        TEXTF(bmp, FONT_LBL, cx - text_w(s) / 2, y, -1, "%s", s);
}

static void knob(void *bmp, int cx, int cy, int v, int bipolar, int off)
{
    int i, k = (v * 28 + 63) / 127;
    if (off) {                              /* an inactive knob: its ring dotted */
        for (i = 0; i < 12; i += 2) {
            int dx = ring[i][0], dy = ring[i][1];
            px(bmp, cx + dx, cy + dy, 1);
            px(bmp, cx - dx, cy + dy, 1);
            px(bmp, cx + dx, cy - dy, 1);
            px(bmp, cx - dx, cy - dy, 1);
        }
    } else {                                /* the ring's pixels (ring[]) in runs: 20 fills, not 48 */
        FILLRECT(bmp, cx - 2, cy + 8, cx + 2, cy + 8, 1);
        FILLRECT(bmp, cx - 2, cy - 8, cx + 2, cy - 8, 1);
        FILLRECT(bmp, cx - 8, cy - 2, cx - 8, cy + 2, 1);
        FILLRECT(bmp, cx + 8, cy - 2, cx + 8, cy + 2, 1);
        for (i = -1; i <= 1; i += 2) {
            FILLRECT(bmp, cx + 3 * i, cy + 7, cx + 4 * i, cy + 7, 1);
            FILLRECT(bmp, cx + 3 * i, cy - 7, cx + 4 * i, cy - 7, 1);
            FILLRECT(bmp, cx + 7 * i, cy + 3, cx + 7 * i, cy + 4, 1);
            FILLRECT(bmp, cx + 7 * i, cy - 4, cx + 7 * i, cy - 3, 1);
            px(bmp, cx + 5 * i, cy + 6, 1);
            px(bmp, cx + 5 * i, cy - 6, 1);
            px(bmp, cx + 6 * i, cy + 5, 1);
            px(bmp, cx + 6 * i, cy - 5, 1);
        }
    }
    line(bmp, cx, cy, cx + needle[k][0], cy + needle[k][1]);
    if (bipolar) {                          /* the stock - and + below */
        FILLRECT(bmp, cx - 10, cy - 7, cx - 8, cy - 7, 1);
        FILLRECT(bmp, cx + 8, cy - 7, cx + 10, cy - 7, 1);
        FILLRECT(bmp, cx + 9, cy - 8, cx + 9, cy - 6, 1);
    }
}

static void box(void *bmp, int cx, int cy, const char *s, int on)    /* a value box; on: inverted */
{
    rframe(bmp, cx - 8, cy - 8, cx + 8, cy + 8);
    if (s)
        small_text(bmp, cx, cy, s);
    if (on)
        FILLRECT(bmp, cx - 7, cy - 7, cx + 7, cy + 7, -1);
}

static const char *page_name(int pg)
{
    return pg < 3 ? pd_title[pg] : fx_page_title(pg - 3);
}

static void title_bar(void *bmp, int pg)
{
    int t = (ST.clock == CL_TAP ? (int)ST.tap : fw_tempo) / 120;
    const char *st = fx_status();
    TEXTF(bmp, FONT_TTL, t >= 100 ? 1 : 3, 56, -1, "%d", t);   /* the box: the tempo, as the stock's A01 */
    FILLRECT(bmp, 0, 54, 14, 63, -1);
    corners(bmp, 0, 54, 14, 63, 0);
    if (ui_flash_ticks && !ui_setup) {
        const char *l = ui_flash_pg >= 3 ? fx_page_label(EFF, ui_flash_pg - 3, ui_flash_i)
                        : ui_flash_pg == 0 ? pd_klbl[ui_flash_i] : ui_flash_pg == 1 ? pd_slbl[ui_flash_i]
                                                                                   : pd_olbl[ui_flash_i];
        TEXTF(bmp, FONT_TTL, 19, 56, -1, "%s", l);
        value_text(bmp, FONT_TTL, 19 + text_w(l) + 6, 56, ui_flash_pg, ui_flash_i);
    } else if (ui_setup) {
        TEXTF(bmp, FONT_TTL, 19, 56, -1, "Setup");
    } else if (fx_lock_mode()) {
        TEXTF(bmp, FONT_TTL, 19, 56, -1, "Lock %d/%d", fx_lock_page() + 1, fx_lock_pages());
    } else if (ST.overload || fx_overload()) {
        TEXTF(bmp, FONT_TTL, 19, 56, -1, "CPU full: bypassed");
    } else if (pg >= 4) {
        TEXTF(bmp, FONT_TTL, 19, 56, -1, "%s", fx_page_title(pg - 3));     /* "LFO (1/2)", as the stock's */
    } else if (st) {
        TEXTF(bmp, FONT_TTL, 19, 56, -1, "%s", st);
    } else if (ST.bypass) {
        TEXTF(bmp, FONT_TTL, 19, 56, -1, "Bypassed (%d/%d)", pg + 1, NPAGE);
    } else {
        TEXTF(bmp, FONT_TTL, 19, 56, -1, "%s (%d/%d)", page_name(pg), pg + 1, NPAGE);
    }
    if (fx_lock_mode() && !ui_setup)
        fx_lock_strip(EFF, bmp, 62, 55);    /* the page's 16 steps: filled, a lock of the pedal's */
    else
        TEXTF(bmp, FONT_TTL, 110, 56, -1, "C%d", ST.load / 10);   /* the whole render's share */
    FILLRECT(bmp, 17, 54, 127, 63, -1);
    corners(bmp, 17, 54, 127, 63, 0);
}

static void left_column(void *bmp)
{
    int k, l, r;
    rframe(bmp, 0, 27, 14, 50);             /* the pedal's state, as the stock's SYN box */
    fx_left(bmp);
    if (ST.bypass)
        small_text(bmp, 10, 31, "B");
    for (k = 0; k < 5; k++) {               /* the output's meter, as LEV: L and R */
        FILLRECT(bmp, 0, 7 + 4 * k, 1, 7 + 4 * k, 1);
        FILLRECT(bmp, 13, 7 + 4 * k, 14, 7 + 4 * k, 1);
    }
    rframe(bmp, 4, 7, 10, 23);
    l = (ST.meter[0] * 13) / 32767;
    r = (ST.meter[1] * 13) / 32767;
    if (l)
        FILLRECT(bmp, 6, 9, 6, 8 + l, 1);
    if (r)
        FILLRECT(bmp, 8, 9, 8, 8 + r, 1);
    label(bmp, 7, -1, "OUT");
}

static void control(void *bmp, int pg, int i)
{
    int cx = col_x[i & 3], r = i >> 2, cy = ROW_Y(r);
    if (pg == 0) {
        knob(bmp, cx, cy, ctl(0, i), (PD_KBIP >> i) & 1, fx_off(0, i));
        label(bmp, cx, LBL_Y(r), pd_klbl[i]);
    } else if (pg == 1) {
        knob(bmp, cx, cy, ctl(1, i), (PD_SBIP >> i) & 1, fx_off(1, i));
        label(bmp, cx, LBL_Y(r), pd_slbl[i]);
    } else if (pd_onum[i]) {
        box(bmp, cx, cy, pd_oval[i][ctl(2, i)], 0);
        if (fx_off(2, i))
            line(bmp, cx - 7, cy - 7, cx + 7, cy + 7);
        label(bmp, cx, LBL_Y(r), pd_olbl[i]);
    }
}

static void draw_setup(void *bmp)
{
    static const char *const clocks[3] = { "FREE", "DIGITONE", "TAP" };
    static const char *const tracks[4] = { "T1", "T2", "T3", "T4" };
    int i, first = ui_row >= 7 ? ui_row - 6 : 0;    /* seven rows on the screen, scrolled */
    for (i = first; i < G_COUNT && i < first + 7; i++) {
        int y = 47 - 7 * (i - first);
        const char *v;
        if (i == G_INPUT)
            v = ST.mono ? "MONO (L)" : "STEREO";
        else if (i == G_CLOCK)
            v = clocks[ST.clock % 3];
        else if (i == G_BYPASS)
            v = ST.trails ? "TRAILS" : "CUT";
        else if (i == G_MIDI)
            v = cfg_midi_any ? "ANY CH" : "AUTO CH";
        else if (i == G_CLOSED)
            v = cfg_closed ? "ON" : "OFF";
        else if (i == G_LOCK)
            v = tracks[fx_lock_track() & 3];
        else
            v = pd_cfg_val[i - G_SHELL][ST.cfg[i - G_SHELL] % pd_cfg_num[i - G_SHELL]];
        TEXTF(bmp, FONT5, 3, y, -1, "%s", i < G_SHELL ? shell_lbl[i] : pd_cfg_lbl[i - G_SHELL]);
        TEXTF(bmp, FONT5, 76, y, -1, "%s", v);
        if (i == ui_row)
            FILLRECT(bmp, 1, y - 1, 126, y + 4, -1);
    }
}

void PD(draw)(void *bmp, void *ctrl)
{
    int i, pg = page_now();
    (void)ctrl;
    if (!ui_open)
        return;
    FILLRECT(bmp, 0, 0, 127, 63, 0);
    if (!ui_ready) {
        TEXTF(bmp, FONT5, 30, 30, -1, "%s...", PD_NAME);
        return;
    }
    title_bar(bmp, pg);
    if (ui_setup) {
        draw_setup(bmp);
        return;
    }
    left_column(bmp);
    if (pg >= 3)
        fx_page_draw(EFF, bmp, pg - 3);
    else
        for (i = 0; i < 8; i++)
            control(bmp, pg, i);
}
