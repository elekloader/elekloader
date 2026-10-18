/* SPDX-License-Identifier: GPL-2.0-or-later */
/* DigiPrism's UI: its effect for fxkit, its Mod Menu entry, its screen, its keys and knobs, its MIDI CCs, its
 * project data. DIGIPRISM in the Mod Menu makes it fxkit's active effect and opens its page; MIDI leaves the
 * page with DigiPrism playing (its entry brings it back), NO turns it off. With the source INPUTS (the default)
 * it owns the audio through fxkit (the synths muted, fxkit's mixer around it); with DIGITONE it is an insert on
 * everything the Digitone plays (its own core_audio owner; fxkit's mixer is not in that path), with fxkit's
 * LFOs and p-locks on its controls all the same.
 *
 *   six pages, as the Digitone's own (LEFT, RIGHT or PAGE moves between them):
 *     SYN1  1 PRIMARY    TILT RATE TIME MIX / AMOUNT x4 (each named by its module's effect)   push: its default
 *     SYN2  2 SECONDARY  SENS DRIFT DRIFT OUT / VOL x4  (also FUNC + a knob on page 1)
 *     FLTR  3 MODULES    the four effects / ORDER, FILTER style, CAPTURE route, input LEVEL   push A-D: on/off
 *     AMP   the MIXER;  LFO  LFO 1, again LFO 2 (fxkit's: DEST lists DigiPrism's controls and the mixer's)
 *   RECORD     LOCK mode: hold a trig and turn a control, that step's p-lock (FUNC + trig clears it, PAGE the
 *              next 16 steps); the modules' effects, ORDER and the rest lock too
 *   T1-T4      the four module buttons: press for the module's next effect, hold to bypass it
 *   trig 13    GESTURE: record the knobs you turn; again to play them (FUNC + 13 erases every gesture)
 *   trig 15    the TAP footswitch: tap a tempo; hold to CAPTURE, release to play it; tap to stop it
 *   trig 16    the BYPASS footswitch (with DUAL BYPASS the chosen modules; twice quickly, everything)
 *   YES SETUP, NO back (from LOCK mode: out of it; from a page: DigiPrism off);  PLAY, STOP, TEMPO and
 *   LEVEL/DATA stay stock
 *   MIDI: its CCs, on the auto channel (or any, in SETUP), while DigiPrism is active (or
 *   always, in SETUP) */
#pragma GCC optimize ("no-tree-loop-distribute-patterns")
#include "dprism.h"

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
#define KEY_SYN1    21
#define KEY_LFO     25
#define KEY_STEP1   26
#define KEY_T1      42
#define KEY_PUSH_A  46
#define EV_KEY(ev)   (*(u32 *)((char *)(ev) + 12))
#define EV_FLAGS(ev) (*(u32 *)((char *)(ev) + 16))
#define EV_DELTA(ev) (*(s32 *)((char *)(ev) + 16))
#define COUNTS      4                       /* encoder counts a detent */

typedef void (*rect_fn)(void *bmp, int x0, int y0, int x1, int y1, int colour);
typedef void (*text_fn)(void *bmp, const void *font, int x, int y, int maxlen, const char *fmt, ...);
#define FILLRECT  ((rect_fn)fw_fillrect)
#define FRAMERECT ((rect_fn)fw_framerect)
#define TEXTF     ((text_fn)fw_textf)
#define FONT5     ((const void *)fw_font5)          /* 3 x 5: the stock value boxes' */
#define FONT_LBL  ((const void *)fw_font_label)     /* the stock labels' */
#define FONT_TTL  ((const void *)fw_font_title)     /* the stock title bar's */

enum { G_SOURCE, G_INPUT, G_CLOCK, G_BYPASS, G_DUAL, G_MIDI, G_CLOSED, G_LOCK, G_COUNT };   /* SETUP's rows */

static const u8 knob_def[8] = { 64, 48, 48, 127, 30, 40, 45, 64 };
static const u8 sec_def[8] = { 64, 0, 0, 64, 64, 64, 64, 64 };
static const u8 fx_def[4] = { FX_DRIVE, FX_DOUBLER, FX_SPACE, FX_FILTER };
static const char *const knob_lbl[4] = { "TILT", "RATE", "TIME", "MIX" };
static const char *const sec_lbl[8] = { "SENS", "DRFT", "DRFT", "OUT", "VOL", "VOL", "VOL", "VOL" };
static const char *const fx_lbl[4][5] = {
    { "DRIV", "WARM", "FUZZ", "WAIL", "SWEL" }, { "DUBL", "VIBR", "PHAS", "TREM", "PTCH" },
    { "ECHO", "TAPE", "SPAC", "SCAT", "REVR" }, { "FILT", "COMP", "LOFI", "GLCH", "STAT" } };
static const char *const fx_name[4][5] = {
    { "Drive", "Warmth", "Fuzz", "Wail", "Swell" }, { "Doubler", "Vibrato", "Phaser", "Tremolo", "Pitch" },
    { "Echoes", "Tape", "Space", "Scatter", "Reverse" },
    { "Filter", "Comp", "Lo-Fi", "Glitch", "Static" } };
static const char *const styles[3] = { "TILT", "LPF", "HPF" };
static const char *const levels[4] = { "LOW", "MED", "HIGH", "VHI" };
static const char *const pg3_lbl[8] = { "COLR", "MOTN", "AMBI", "GRIT", "ORDR", "FLTR", "CAPT", "LEVL" };
static const char *const cfg_lbl[G_COUNT] = { "SOURCE", "INPUT", "CLOCK", "BYPASS", "DUAL BYPASS", "MIDI CC",
                                              "CC CLOSED", "LOCK TRACK" };

static u8 ui_open, ui_setup, ui_pg, ui_func, ui_ready, ui_inited, ui_row, ui_pending;
static u8 ui_flash_ticks, ui_flash_pg, ui_flash_i;   /* a control's value in the title bar after a turn */
#define NPAGE 6                             /* PRIMARY, SECONDARY, MODULES, the MIXER, LFO 1, LFO 2 */
static u8 in_cc;                            /* the MIDI task: the controls change at the next tick */
#define EFF (&dprism_effect)
static u8 ui_fx_ticks, ui_fx_m;                       /* a module's new effect, by name, for a second */
static u8 cfg_midi_any, cfg_closed, dual;             /* SETUP: CCs on any channel, CCs while closed, DUAL BYPASS */
static u32 clear_at;                        /* the next address to clear, 0 when nothing is */
static s32 enc_acc[8];
static u8 t_down[4], t_held[4];             /* the track keys: pressed, and held long enough to bypass */
static u8 cap_down, cap_was_playing;        /* trig 15 */
static u32 cap_at, tap_last, tap_iv[3], tap_n;
static u32 byp_at;
static u8 byp_undo;                         /* trig 16: the modules the last press toggled (DUAL BYPASS) */

/* ---- the effect: DigiPrism's controls as fxkit's slots ---------------------------------------------- */
static const char *const off_on[2] = { "OFF", "ON" };
static const char *const routes[2] = { "POST", "PRE" };
static const struct fx_param dprism_params[] = {
    { DH_S_KNOB(K_TILT), FX_BIP, 0, 64, "TILT", "Tilt", 0 },
    { DH_S_KNOB(K_RATE), FX_KNOB, 0, 48, "RATE", "Rate", 0 },
    { DH_S_KNOB(K_TIME), FX_KNOB, 0, 48, "TIME", "Time", 0 },
    { DH_S_KNOB(K_MIX), FX_KNOB, 0, 127, "MIX", "Mix", 0 },
    { DH_S_KNOB(K_CAMT), FX_KNOB, 0, 30, "CAMT", "Color Amt", 0 },
    { DH_S_KNOB(K_MAMT), FX_KNOB, 0, 40, "MAMT", "Motion Amt", 0 },
    { DH_S_KNOB(K_AAMT), FX_KNOB, 0, 45, "AAMT", "Ambience Amt", 0 },
    { DH_S_KNOB(K_GAMT), FX_KNOB, 0, 64, "GAMT", "Grit Amt", 0 },
    { DH_S_SEC(S_SENS), FX_BIP, 0, 64, "SENS", "Sensitivity", 0 },
    { DH_S_SEC(S_MDRIFT), FX_KNOB, 0, 0, "MDRF", "Move Drift", 0 },
    { DH_S_SEC(S_DDRIFT), FX_KNOB, 0, 0, "ADRF", "Amb Drift", 0 },
    { DH_S_SEC(S_OUT), FX_BIP, 0, 64, "OUT", "Output", 0 },
    { DH_S_SEC(S_CVOL), FX_BIP, 0, 64, "CVOL", "Color Vol", 0 },
    { DH_S_SEC(S_MVOL), FX_BIP, 0, 64, "MVOL", "Motion Vol", 0 },
    { DH_S_SEC(S_AVOL), FX_BIP, 0, 64, "AVOL", "Ambience Vol", 0 },
    { DH_S_SEC(S_GVOL), FX_BIP, 0, 64, "GVOL", "Grit Vol", 0 },
    { DH_S_FX(M_CHAR), FX_OPT, 5, FX_DRIVE, "COLR", "Color", fx_lbl[M_CHAR] },
    { DH_S_FX(M_MOVE), FX_OPT, 5, FX_DOUBLER, "MOTN", "Motion", fx_lbl[M_MOVE] },
    { DH_S_FX(M_DIFF), FX_OPT, 5, FX_SPACE, "AMBI", "Ambience", fx_lbl[M_DIFF] },
    { DH_S_FX(M_TEX), FX_OPT, 5, FX_FILTER, "GRIT", "Grit", fx_lbl[M_TEX] },
    { DH_S_ON(M_CHAR), FX_OPT, 2, 1, "C ON", "Color On", off_on },
    { DH_S_ON(M_MOVE), FX_OPT, 2, 1, "M ON", "Motion On", off_on },
    { DH_S_ON(M_DIFF), FX_OPT, 2, 1, "D ON", "Ambience On", off_on },
    { DH_S_ON(M_TEX), FX_OPT, 2, 1, "T ON", "Grit On", off_on },
    { DH_S_ORDER, FX_OPT, 24, 0, "ORDR", "Order", 0 },
    { DH_S_STYLE, FX_OPT, 3, ST_TILT, "FLTR", "Filter Style", styles },
    { DH_S_CROUTE, FX_OPT, 2, 0, "CAPT", "Capture Route", routes },
    { DH_S_LEVEL, FX_OPT, 4, 2, "LEVL", "Input Level", levels },
    { 0 }
};

static s32 clamp(s32 v, s32 hi)
{
    return v < 0 ? 0 : v > hi ? hi : v;
}

/* The render's: this block's values (DigiPrism's own, a step's lock, an LFO on it) into its controls */
static void dprism_values(const int16_t *val, uint32_t flags)
{
    int i;
    for (i = 0; i < 8; i++) {
        dprism.knob[i] = (u8)clamp(val[DH_S_KNOB(i)] >> 8, 127);
        dprism.sec[i] = (u8)clamp(val[DH_S_SEC(i)] >> 8, 127);
    }
    for (i = 0; i < 4; i++) {
        dprism.fx[i] = (u8)clamp(val[DH_S_FX(i)] >> 8, 4);
        dprism.on[i] = (u8)(val[DH_S_ON(i)] >= 128);
    }
    dprism.order = (u8)clamp(val[DH_S_ORDER] >> 8, 23);
    dprism.style = (u8)clamp(val[DH_S_STYLE] >> 8, 2);
    dprism.croute = (u8)(val[DH_S_CROUTE] >= 128);
    dprism.level = (u8)clamp(val[DH_S_LEVEL] >> 8, 3);
    dprism.bypass = (u8)((flags & FX_V_BYPASS) != 0);
}

const struct fx_effect dprism_effect = {
    FX_ABI, "DIGIPRISM", dprism_params, FX_OWN_MIX | FX_OWN_BYPASS, 0, dprism_values, dprism_render,
};

static int own(s32 slot)                    /* a control's own value (what it plays without locks or LFOs) */
{
    return (int)(fx_get(EFF, slot) >> 8);
}

static int shown(s32 slot)                  /* what its control shows: its own value, or the held step's lock */
{
    return (int)clamp(fx_view(slot, fx_get(EFF, slot)) >> 8, 127);
}

/* A control's own value (from the MIDI task, at the next tick); turned on the page in LOCK mode with a step held,
 * that step's lock */
static void set_slot(s32 slot, s32 v)
{
    if (in_cc)
        fx_set_later(EFF, slot, v << 8);
    else
        fx_set(EFF, slot, v << 8);
}

static void turn_slot(s32 slot, s32 v)
{
    if (fx_held_step() >= 0)
        fx_turned(EFF, slot, v << 8);
    else
        set_slot(slot, v);
}

static void set_on(int m, int on)
{
    dprism.on[m] = (u8)on;
    set_slot(DH_S_ON(m), on);
}

static void set_fx(int m, int fx)
{
    dprism.fx[m] = (u8)fx;
    set_slot(DH_S_FX(m), fx);
}

static void set_bypass(int on)
{
    dprism.bypass = (u8)on;
    if (!on)
        dprism.overload = 0;
    set_slot(FX_M_BYP, on);
}

/* ---- project data: the pedal, its mixer and LFOs, SETUP, saved with the project -------------------------
 * 56 bytes (the gap mods share in a project holds 480: core leaves out a block that does not fit). The p-locks
 * are the pattern's. */
struct dh_save {
    u8 ver, order, style, croute;
    u8 fxon[4];                         /* each module's effect | on << 3 */
    u8 knob[8], sec[8];
    u8 level, trails, clock, cfg;       /* cfg: MIDI ANY, CC CLOSED, SOURCE, MONO, DUAL BYPASS << 4 */
    u16 tap;
    u8 pad[2];
    u8 fx[FX_PACK];                     /* fxkit's: the mixer, the LFOs */
} __attribute__((aligned(4)));
static struct dh_save save;
#define SAVE_VER 2

static void defaults(void)
{
    int i;
    for (i = 0; i < 8; i++) {
        set_slot(DH_S_KNOB(i), knob_def[i]);
        set_slot(DH_S_SEC(i), sec_def[i]);
    }
    for (i = 0; i < 4; i++) {
        set_fx(i, fx_def[i]);
        set_on(i, 1);
    }
    set_slot(DH_S_ORDER, 0);
    set_slot(DH_S_STYLE, ST_TILT);
    set_slot(DH_S_CROUTE, 0);
    set_slot(DH_S_LEVEL, 2);
    fx_unpack(EFF, 0);
    dprism.trails = 1;
    dprism.clock = CL_FREE;
    dprism.tap = 14400;
    dprism.source = SRC_INPUTS;
    dprism.mono = 0;
    cfg_midi_any = cfg_closed = dual = 0;
}

static void dprism_loaded(int found)
{
    int i;
    if (!found || save.ver != SAVE_VER || save.order >= 24) {
        defaults();
    } else {
        for (i = 0; i < 8; i++) {
            set_slot(DH_S_KNOB(i), save.knob[i] & 127);
            set_slot(DH_S_SEC(i), save.sec[i] & 127);
        }
        for (i = 0; i < 4; i++) {
            set_fx(i, (save.fxon[i] & 7) < 5 ? save.fxon[i] & 7 : 0);
            set_on(i, (save.fxon[i] >> 3) & 1);
        }
        set_slot(DH_S_ORDER, save.order);
        set_slot(DH_S_STYLE, save.style < 3 ? save.style : 0);
        set_slot(DH_S_CROUTE, save.croute & 1);
        set_slot(DH_S_LEVEL, save.level & 3);
        fx_unpack(EFF, save.fx);
        dprism.trails = save.trails & 1;
        dprism.clock = save.clock < 3 ? save.clock : 0;
        dprism.tap = save.tap >= 2400 && save.tap <= 48000 ? save.tap : 14400;
        cfg_midi_any = save.cfg & 1;
        cfg_closed = (save.cfg >> 1) & 1;
        dprism.source = (save.cfg >> 2) & 1;
        dprism.mono = (save.cfg >> 3) & 1;
        dual = (save.cfg >> 4) & 15;
    }
    ui_inited = 1;
}

const struct core_projdata dprism_projdata = { 0x44434852u /* "DCHR" */, sizeof(struct dh_save), &save,
                                                dprism_loaded };

static u32 saved_serial = ~0u;

static void keep_save(void)                 /* the pedal into the project's copy (its controls when they moved) */
{
    int i;
    save.ver = SAVE_VER;
    if (fx_serial(EFF) != saved_serial) {
        saved_serial = fx_serial(EFF);
        for (i = 0; i < 8; i++) {
            save.knob[i] = (u8)own(DH_S_KNOB(i));
            save.sec[i] = (u8)own(DH_S_SEC(i));
        }
        for (i = 0; i < 4; i++)
            save.fxon[i] = (u8)(own(DH_S_FX(i)) | own(DH_S_ON(i)) << 3);
        save.order = (u8)own(DH_S_ORDER);
        save.style = (u8)own(DH_S_STYLE);
        save.croute = (u8)own(DH_S_CROUTE);
        save.level = (u8)own(DH_S_LEVEL);
        fx_pack(EFF, save.fx);
    }
    save.trails = dprism.trails;
    save.clock = dprism.clock;
    save.tap = (u16)dprism.tap;
    save.cfg = (u8)(cfg_midi_any | cfg_closed << 1 | dprism.source << 2 | dprism.mono << 3 | dual << 4);
}

/* ---- opening and closing -------------------------------------------------------------------------- */
static int can_insert(void);

static void chain(void)                     /* fxkit's audio for the inputs; SOURCE DIGITONE: DigiPrism's insert */
{
    int ins = dprism.source == SRC_DIGITONE && can_insert();
    fx_chain(EFF, !ins);
    dprism_owner.flags = ins ? CORE_AUDIO_INSERT : CORE_AUDIO_MUTE_VOICES;
    dprism_owner.on = ins && dprism_on && ui_ready;
}

static void activate(void)
{
    if (fx_active() != EFF) {               /* (back from MIDI: as it was) */
        set_bypass(0);                      /* switching it on steps on it */
        fx_activate(EFF);
    }
    dprism_on = 1;
    ui_pending = 0;
    chain();
}

static void dprism_open(void *brain, void *ev, int track)
{
    (void)brain, (void)ev, (void)track;
    ui_open = 1;
    ui_setup = 0;
    ui_pg = 0;
    ui_func = 0;
    if (ui_ready)
        activate();
    else
        ui_pending = 1;                     /* the buffers are not cleared yet: at the tick that ends it */
}

static void dprism_hide(void)              /* the page gone (MIDI), DigiPrism playing on */
{
    ui_open = 0;
    fx_lock_mode_set(0);
    dprism.grec = 0;
    cap_down = 0;
}

static void dprism_close(void)             /* NO: DigiPrism off */
{
    dprism_hide();
    ui_pending = 0;
    fx_deactivate(EFF);
    dprism_on = 0;
    chain();
}

/* The Mod Menu's DIGIPRISM, with its icon: a pedal, eight knobs over four buttons and two footswitches. */
static const u16 dprism_icon[16] = {
    0x0000, 0x7ffe, 0x4002, 0x76da, 0x76da, 0x4002, 0x76da, 0x76da,
    0x4002, 0x524a, 0x4002, 0x5c3a, 0x5c3a, 0x4002, 0x7ffe, 0x0000
};
const struct core_menu_item dprism_menu = { "DIGIPRISM", dprism_open, CORE_MENU_ICON, dprism_icon };

/* ---- knobs and keys ------------------------------------------------------------------------------- */
static void flash(int pg, int i)              /* the control's value in the title bar, a second */
{
    ui_fx_ticks = 0;
    ui_flash_pg = (u8)pg;
    ui_flash_i = (u8)i;
    ui_flash_ticks = 30;
}

/* A primary knob moved, by hand or by MIDI: GESTURE records it, or the move deletes its gesture. */
static void knob_moved(int i)
{
    if (dprism.grec)
        dprism.gtouch[i] = 1;
    else if (dprism.gstate[i] != G_NONE)
        dprism.gkill[i] = 1;
}

static void set_knob(int i, int sec, s32 v)   /* a knob's own value */
{
    if (v < 0) v = 0;
    if (v > 127) v = 127;
    if (sec) {
        dprism.sec[i] = (u8)v;
    } else {
        dprism.knob[i] = (u8)v;
        knob_moved(i);
    }
    set_slot(sec ? DH_S_SEC(i) : DH_S_KNOB(i), v);
}

static void turn_knob(int i, int sec, s32 v)  /* turned on the page: in LOCK mode with a step held, its lock */
{
    if (fx_held_step() < 0)
        set_knob(i, sec, v);
    else
        turn_slot(sec ? DH_S_SEC(i) : DH_S_KNOB(i), clamp(v, 127));
}

static int can_insert(void)                   /* the core takes the source DIGITONE (core-dn1 3.4) */
{
    return (core_audio_caps & CORE_AUDIO_INSERT) != 0;
}

static int page_now(void)                    /* FUNC held on DigiPrism's pages shows page 2 */
{
    return ui_func && ui_pg < 3 ? 1 : ui_pg;
}

static void show_fx(int m)
{
    ui_fx_m = (u8)m;
    ui_fx_ticks = 30;
    ui_flash_ticks = 0;
}

static void next_fx(int m)                    /* a module's button: its next effect, and it runs */
{
    set_fx(m, (own(DH_S_FX(m)) + 1) % 5);
    set_on(m, 1);
    show_fx(m);
}

static void step_setting(int i, int d)        /* page 3's controls (a lock, with a step held in LOCK mode) */
{
    if (i < 4) {
        turn_slot(DH_S_FX(i), (shown(DH_S_FX(i)) + 5 + d) % 5);
        show_fx(i);
    } else if (i == 4) {
        turn_slot(DH_S_ORDER, (shown(DH_S_ORDER) + 24 + d) % 24);
    } else if (i == 5) {
        turn_slot(DH_S_STYLE, (shown(DH_S_STYLE) + 3 + d) % 3);
    } else if (i == 6) {
        turn_slot(DH_S_CROUTE, d > 0);
    } else {
        turn_slot(DH_S_LEVEL, clamp(shown(DH_S_LEVEL) + d, 3));
    }
}

static void push(int i)                      /* an encoder pushed */
{
    int pg = page_now();
    if (pg >= 3) {
        fx_page_push(EFF, pg - 3, i);       /* its value under the knob, as the stock's */
    } else if (pg < 2) {
        turn_knob(i, pg, pg ? sec_def[i] : knob_def[i]);
        flash(pg, i);
    } else if (i < 4) {
        turn_slot(DH_S_ON(i), !shown(DH_S_ON(i)));
    }
}

static void tap(void)                        /* the TAP footswitch: the mean of the last taps */
{
    u32 now = dprism.blocks, iv = now - tap_last, sum = 0, k;
    tap_last = now;
    if (iv < 225 || iv > 4500) {             /* 400 .. 20 BPM: a new series */
        tap_n = 0;
        return;
    }
    tap_iv[tap_n % 3] = iv;
    tap_n++;
    for (k = 0; k < (tap_n < 3 ? tap_n : 3); k++)
        sum += tap_iv[k];
    dprism.tap = 90000u * 120u * (tap_n < 3 ? tap_n : 3) / sum;    /* 1500 blocks a second: BPM x 120 */
    dprism.clock = CL_TAP;
}

static void bypass_key(void)                 /* trig 16 */
{
    u32 now = dprism.blocks;
    int m, b = own(FX_M_BYP);
    if (dual && now - byp_at < 600 && byp_undo) {   /* a double press: everything */
        for (m = 0; m < 4; m++)
            if (byp_undo & (1 << m))
                set_on(m, !own(DH_S_ON(m)));
        byp_undo = 0;
        set_bypass(!b);
    } else if (dual && !b) {
        for (m = 0; m < 4; m++)
            if (dual & (1 << m))
                set_on(m, !own(DH_S_ON(m)));
        byp_undo = dual;
    } else {
        set_bypass(!b);
        byp_undo = 0;
    }
    byp_at = now;
}

int dprism_key(void *brain, void *ev)
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
            int m = (int)(key - KEY_T1);
            if (t_down[m] && !t_held[m])
                next_fx(m);
            t_down[m] = t_held[m] = 0;
        } else if (key == KEY_STEP1 + 14 && cap_down) {
            cap_down = 0;
            if (dprism.blocks - cap_at >= 450) {
                dprism.ccmd = CC_PLAY;     /* held: the recording plays */
            } else {
                dprism.ccmd = CC_STOP;     /* a tap: no recording */
                if (!cap_was_playing)
                    tap();
            }
        }
        return 1;
    }
    if (!press)
        return 1;                           /* repeats; ev_hold sees the track keys held */
    if (key == KEY_MIDI) {
        dprism_hide();
        return 0;                           /* the stock's MIDI mode, DigiPrism playing on */
    } else if (key == KEY_RECORD) {
        if (!ui_setup)
            fx_lock_mode_set(!fx_lock_mode());
    } else if (key >= KEY_STEP1 && key < KEY_STEP1 + 8 && ui_setup) {
        ;                                   /* (0.1's presets) */
    } else if (key >= KEY_T1 && key < KEY_T1 + 4) {
        t_down[key - KEY_T1] = 1;
        t_held[key - KEY_T1] = 0;
    } else if (key == KEY_STEP1 + 12) {     /* GESTURE */
        int k;
        if (ui_func) {
            for (k = 0; k < 8; k++)
                dprism.gkill[k] = 1;
            dprism.grec = 0;
        } else {
            dprism.grec = !dprism.grec;
        }
    } else if (key == KEY_STEP1 + 14) {     /* TAP / CAPTURE */
        cap_was_playing = dprism.cstate == C_PLAY;
        cap_down = 1;
        cap_at = dprism.blocks;
        dprism.ccmd = CC_REC;              /* from the press: a hold keeps it, a tap drops it */
    } else if (key == KEY_STEP1 + 15) {     /* BYPASS */
        bypass_key();
    } else if (key == KEY_YES) {
        ui_setup = !ui_setup;
    } else if (key == KEY_NO) {
        if (ui_setup)
            ui_setup = 0;
        else if (fx_lock_mode())
            fx_lock_mode_set(0);
        else
            dprism_close();
    } else if (ui_setup && (key == KEY_UP || key == KEY_DOWN)) {
        ui_row = (u8)((ui_row + (key == KEY_UP ? G_COUNT - 1 : 1)) % G_COUNT);
    } else if (ui_setup && (key == KEY_LEFT || key == KEY_RIGHT)) {
        int d = key == KEY_RIGHT ? 1 : -1;
        if (ui_row == G_SOURCE)
            dprism.source = can_insert() && !dprism.source;
        else if (ui_row == G_INPUT)
            dprism.mono = !dprism.mono;
        else if (ui_row == G_CLOCK)
            dprism.clock = (u8)((dprism.clock + 3 + d) % 3);
        else if (ui_row == G_BYPASS)
            dprism.trails = !dprism.trails;
        else if (ui_row == G_DUAL)
            dual = (u8)((dual + 16 + d) % 16);
        else if (ui_row == G_MIDI)
            cfg_midi_any = !cfg_midi_any;
        else if (ui_row == G_CLOSED)
            cfg_closed = !cfg_closed;
        else
            fx_lock_track_set((fx_lock_track() + 4 + d) % 4);
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

/* core-dn1's ev_hold, a track key held on its own: while open, that module's bypass. */
int dprism_hold(void *brain, void *ev, int track)
{
    (void)brain, (void)ev;
    if (!ui_open || track < 0 || track > 3)
        return 0;
    set_on(track, !own(DH_S_ON(track)));
    t_held[track] = 1;
    return 1;
}

int dprism_enc(void *brain, void *ev)
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
    } else if (pg < 2) {
        turn_knob(i, pg, shown(pg ? DH_S_SEC(i) : DH_S_KNOB(i)) + d);
        flash(pg, i);
    } else {
        step_setting(i, d > 0 ? 1 : -1);
    }
    return 1;
}

/* ---- MIDI: its CC chart (core-dn1 3.2's ev_midi_cc, the MIDI task) --------------------
 * While DigiPrism is active (or always, with SETUP's CC CLOSED on) the CCs below are its own when they come
 * on the auto channel (or on a track's channel too, with SETUP's MIDI CC: ANY CH): the Digitone does not
 * apply them. Every other CC goes on to the Digitone. */
static const u8 cc_knob[8] = { K_TILT, K_CAMT, K_RATE, K_MAMT, K_TIME, K_AAMT, K_MIX, K_GAMT };      /* 64-71 */
static const u8 cc_sec[8] = { S_SENS, S_CVOL, S_MDRIFT, S_MVOL, S_DDRIFT, S_AVOL, S_OUT, S_GVOL };  /* 72-79 */

static int cc_take(int cc, int value);

int dprism_cc(int track, int cc, int value, int flags)
{
    int took;
    (void)track;
    if (!ui_ready || (!dprism_on && !cfg_closed) || (!(flags & CORE_MIDI_CC_AUTO) && !cfg_midi_any))
        return 0;
    if (value < 0 || value > 127)
        return 0;
    in_cc = 1;
    took = cc_take(cc, value);
    in_cc = 0;
    return took;
}

static int cc_take(int cc, int value)
{
    int on = value >= 64;
    if (cc >= 64 && cc <= 71) {
        set_knob(cc_knob[cc - 64], 0, value);
    } else if (cc >= 72 && cc <= 79) {
        set_knob(cc_sec[cc - 72], 1, value);
    } else if (cc >= 16 && cc <= 19) {          /* a module's effect: five bands of 22, then off */
        int m = cc - 16;
        if (value >= 110) {
            set_on(m, 0);
        } else {
            set_fx(m, value / 22);
            set_on(m, 1);
        }
    } else if (cc == 91) {                      /* bypass below 64 */
        set_bypass(!on);
    } else if (cc == 92) {                      /* total bypass, dual bypass, total engage */
        int m;
        if (value < 32) {
            set_bypass(1);
        } else if (value < 64) {
            for (m = 0; m < 4; m++)
                if (dual & (1 << m))
                    set_on(m, 0);
        } else {
            set_bypass(0);
            for (m = 0; m < 4; m++)
                set_on(m, 1);
        }
    } else if (cc >= 103 && cc <= 106) {        /* a module's bypass */
        set_on(cc - 103, on);
    } else if (cc == 80) {                      /* GESTURE: record from 64, play below */
        dprism.grec = (u8)on;
    } else if (cc == 81) {                      /* GESTURE: erase */
        int k;
        dprism.grec = 0;
        for (k = 0; k < 8; k++)
            dprism.gkill[k] = 1;
    } else if (cc == 82) {                      /* CAPTURE: stop/clear, play, record */
        if (value < 44)
            dprism.ccmd = CC_STOP;
        else if (value < 88)
            dprism.ccmd = dprism.cstate == C_REC ? CC_PLAY : CC_NONE;
        else
            dprism.ccmd = CC_REC;
    } else if (cc == 83) {                      /* CAPTURE route: post below 64 */
        set_slot(DH_S_CROUTE, on);
    } else if (cc == 84) {                      /* FILTER: LPF, TILT, HPF */
        set_slot(DH_S_STYLE, value < 44 ? ST_LPF : value < 88 ? ST_TILT : ST_HPF);
    } else if (cc == 93) {                      /* TAP */
        tap();
    } else if (cc == 94) {                      /* the input level, in four bands */
        set_slot(DH_S_LEVEL, value >> 5);
    } else if (cc == 95) {
        /* the calibration menu: DigiPrism has none (LEVL on page 3 sets the input level), but taken, so the
         * Digitone does not apply it to the active track meanwhile */
    } else {
        return 0;
    }
    return 1;
}

/* ---- the UI task: clearing, the effect, the footswitches, redraws ------------------------------------ */
static u32 ui_sig, ui_ticks;

static u32 signature(void)                  /* what the page shows, but the meter, folded into a word */
{
    int pg = page_now();
    u32 h = (u32)(pg | ui_setup << 3 | ui_row << 4 | (ui_flash_ticks != 0) << 7
                  | (ui_fx_ticks != 0) << 8 | dprism.order << 9 | dprism.style << 14 | dprism.croute << 16
                  | dprism.level << 17 | dprism.bypass << 19 | dprism.overload << 20 | dprism.grec << 21
                  | dprism.cstate << 22 | dual << 24 | dprism.clock << 28 | dprism.trails << 30
                  | (u32)dprism.source << 31);
    int k;
    for (k = 0; k < 8; k++)
        h = h * 31 + dprism.eff[k] + (shown(DH_S_SEC(k)) << 8) + (dprism.gstate[k] << 16)
            + ((u32)shown(DH_S_KNOB(k)) << 20);
    if (pg >= 3)
        h = h * 31 + fx_page_state();
    for (k = 0; k < 8 && pg >= 3; k++) {
        s32 s = fx_page_slot(pg - 3, k);
        h = h * 31 + (u32)fx_view(s, fx_get(EFF, s));
    }
    h = h * 31 + (u32)(fx_lock_mode() | fx_overload() << 1 | fx_lock_page() << 2 | fx_lock_track() << 4
                       | (fx_held_step() + 1) << 6) + (fx_lock_mode() ? fx_lock_mask(EFF, fx_lock_page()) << 13 : 0);
    for (k = 0; k < 11; k++)
        h = h * 31 + (u32)shown(k < 4 ? DH_S_FX(k) : k < 8 ? DH_S_ON(k - 4) : k == 8 ? DH_S_ORDER
                                : k == 9 ? DH_S_STYLE : DH_S_CROUTE);
    h = h * 31 + (u32)shown(DH_S_LEVEL);
    for (k = 0; k < 4; k++)
        h = h * 31 + dprism.fx[k] + (dprism.on[k] << 4);
    h = h * 31 + (dprism.cstate == C_REC ? dprism.crec : dprism.clen) / 48000;
    h = h * 31 + dprism.load / 10 + ((u32)fw_tempo << 8) + (cfg_midi_any << 24) + (cfg_closed << 25)
        + (dprism.mono << 26);
    return h;
}
void dprism_tick(void *ctrl)
{
    if (!ui_inited) {                       /* no project load reached us */
        defaults();
        ui_inited = 1;
    }
    if (!ui_ready) {                        /* the first time: clear what the render reads, 256 KB a tick */
        u32 *p, *end;
        if (!clear_at)
            clear_at = DH_BULK;
        p = (u32 *)clear_at;
        end = p + 65536;
        if ((u32)end > DH_CLEAR_END)
            end = (u32 *)DH_CLEAR_END;
        while (p < end)
            *p++ = 0;
        clear_at = (u32)end;
        if (clear_at >= DH_CLEAR_END) {
            dprism_dsp_reset();
            dprism.active = 1;
            ui_ready = 1;
        }
    }
    if (ui_pending && ui_ready)
        activate();
    dprism_on = (u8)(fx_active() == EFF);
    if (ui_open && !dprism_on && !ui_pending)
        dprism_hide();                     /* another effect took the audio */
    chain();                                /* the inputs (fxkit's audio) or the insert, as SOURCE says */
    dprism.meter[0] = (u16)(dprism.meter[0] - (dprism.meter[0] >> 3));
    dprism.meter[1] = (u16)(dprism.meter[1] - (dprism.meter[1] >> 3));
    keep_save();
    if (ui_flash_ticks)
        ui_flash_ticks--;
    if (ui_fx_ticks)
        ui_fx_ticks--;
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
static void value_text(void *bmp, const void *font, int x, int y, int i, int sec)
{
    int v;
    if (sec >= 3) {                             /* the MIXER's, the LFOs' */
        char buf[8];
        s32 s = fx_page_slot(sec - 3, i);
        fx_text(EFF, s, fx_view(s, fx_get(EFF, s)), buf);
        TEXTF(bmp, font, x, y, -1, "%s", buf);
        return;
    }
    v = sec ? shown(DH_S_SEC(i)) : fx_held_step() >= 0 ? shown(DH_S_KNOB(i)) : dprism.eff[i];
    if (!sec && i == K_TILT) {
        TEXTF(bmp, font, x, y, -1, "%c%d", v >= 64 ? '+' : '-', v >= 64 ? v - 64 : 64 - v);
    } else if (!sec && i == K_RATE && dprism.fx[M_MOVE] == FX_PITCH) {
        int st = ((v - 64) * 12 + (v >= 64 ? 32 : -32)) / 64;
        TEXTF(bmp, font, x, y, -1, "%c%dST", st >= 0 ? '+' : '-', st >= 0 ? st : -st);
    } else if (!sec && i == K_MIX) {
        TEXTF(bmp, font, x, y, -1, "%d%%", v * 100 / 127);
    } else if (sec && (i == S_SENS || i >= S_OUT)) {
        TEXTF(bmp, font, x, y, -1, "%c%d", v >= 64 ? '+' : '-', v >= 64 ? v - 64 : 64 - v);
    } else {
        TEXTF(bmp, font, x, y, -1, "%d", v);
    }
}

/* As the Digitone's own parameter pages (traced from its FLTR, AMP and LFO pages): an inverted box (here the
 * tempo) and title bar on top, a column on the left (CAPTURE, the output's meter, as SYN and LEV) and two
 * rows of four controls, each 17 pixels square over its label: knobs and value boxes. */
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
    if (off) {                              /* a bypassed module's knob: its ring dotted */
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

static void cap_icon(void *bmp, int cx, int cy, int s)       /* CAPTURE's state, 7 pixels */
{
    int k;
    if (s == C_REC) {                       /* a dot */
        FILLRECT(bmp, cx - 1, cy - 3, cx + 1, cy + 3, 1);
        FILLRECT(bmp, cx - 2, cy - 2, cx + 2, cy + 2, 1);
        FILLRECT(bmp, cx - 3, cy - 1, cx + 3, cy + 1, 1);
    } else if (s == C_PLAY) {               /* a play arrow */
        for (k = -3; k <= 3; k++)
            FILLRECT(bmp, cx - 2, cy + k, cx - 2 + 3 - (k < 0 ? -k : k), cy + k, 1);
    } else {                                /* nothing captured */
        FILLRECT(bmp, cx - 4, cy, cx - 2, cy, 1);
        FILLRECT(bmp, cx + 2, cy, cx + 4, cy, 1);
    }
}

static void title_bar(void *bmp, int pg)
{
    int t = (dprism.clock == CL_TAP ? (int)dprism.tap : fw_tempo) / 120;
    TEXTF(bmp, FONT_TTL, t >= 100 ? 1 : 3, 56, -1, "%d", t);   /* the box: the tempo, as the stock's A01 */
    FILLRECT(bmp, 0, 54, 14, 63, -1);
    corners(bmp, 0, 54, 14, 63, 0);
    if (ui_fx_ticks && !ui_setup) {
        TEXTF(bmp, FONT_TTL, 19, 56, -1, "%s%s", dprism.on[ui_fx_m] ? "" : "(off) ",
              fx_name[ui_fx_m][dprism.fx[ui_fx_m] % 5]);
    } else if (ui_flash_ticks && !ui_setup && ui_flash_pg >= 3) {
        const char *l = fx_page_label(EFF, ui_flash_pg - 3, ui_flash_i);
        TEXTF(bmp, FONT_TTL, 19, 56, -1, "%s", l);
        value_text(bmp, FONT_TTL, 19 + text_w(l) + 6, 56, ui_flash_i, ui_flash_pg);
    } else if (ui_flash_ticks && !ui_setup) {
        if (ui_flash_pg < 2) {
            const char *l = ui_flash_pg ? sec_lbl[ui_flash_i]
                                        : ui_flash_i < 4 ? knob_lbl[ui_flash_i] : fx_lbl[ui_flash_i - 4][dprism.fx[ui_flash_i - 4]];
            TEXTF(bmp, FONT_TTL, 19, 56, -1, "%s", l);
            value_text(bmp, FONT_TTL, 19 + text_w(l) + 6, 56, ui_flash_i, ui_flash_pg);
        }
    } else if (ui_setup) {
        TEXTF(bmp, FONT_TTL, 19, 56, -1, "Setup");
    } else if (fx_lock_mode()) {
        TEXTF(bmp, FONT_TTL, 19, 56, -1, "Lock %d/%d", fx_lock_page() + 1, fx_lock_pages());
    } else if (dprism.overload || fx_overload()) {
        TEXTF(bmp, FONT_TTL, 19, 56, -1, "CPU full: bypassed");
    } else if (pg >= 4) {
        TEXTF(bmp, FONT_TTL, 19, 56, -1, "%s", fx_page_title(pg - 3));     /* "LFO (1/2)", as the stock's */
    } else if (dprism.grec) {
        TEXTF(bmp, FONT_TTL, 19, 56, -1, "Gesture: rec");
    } else if (dprism.bypass) {
        TEXTF(bmp, FONT_TTL, 19, 56, -1, "Bypassed (%d/%d)", pg + 1, NPAGE);
    } else {
        TEXTF(bmp, FONT_TTL, 19, 56, -1, "%s (%d/%d)", pg == 0 ? "Prism" : pg == 1 ? "Secondary" : pg == 2 ? "Modules"
              : pg == 3 && dprism.source == SRC_DIGITONE && can_insert() ? "Mixer: inputs" : fx_page_title(pg - 3),
              pg + 1, NPAGE);
    }
    if (fx_lock_mode() && !ui_setup)
        fx_lock_strip(EFF, bmp, 62, 55);    /* the page's 16 steps: filled, a lock of DigiPrism's */
    else
        TEXTF(bmp, FONT_TTL, 110, 56, -1, "C%d", dprism.load / 10);   /* the whole render's share */
    FILLRECT(bmp, 17, 54, 127, 63, -1);
    corners(bmp, 17, 54, 127, 63, 0);
}

static void left_column(void *bmp)
{
    int s = dprism.cstate, k, l, r, secs, g = 0;
    rframe(bmp, 0, 27, 14, 50);             /* CAPTURE, as the stock's SYN box */
    cap_icon(bmp, 7, 44, s);
    if (s != C_EMPTY) {
        secs = (int)((s == C_REC ? dprism.crec : dprism.clen) / 48000);
        TEXTF(bmp, FONT5, secs >= 10 ? 3 : 5, 35, -1, "%d", secs);
    }
    for (k = 0; k < 8; k++)
        g |= dprism.gstate[k] != G_NONE;
    if (g || dprism.grec)
        small_text(bmp, 4, 31, "G");
    if (dprism.bypass)
        small_text(bmp, 10, 31, "B");
    for (k = 0; k < 5; k++) {               /* the output's meter, as LEV: L and R */
        FILLRECT(bmp, 0, 7 + 4 * k, 1, 7 + 4 * k, 1);
        FILLRECT(bmp, 13, 7 + 4 * k, 14, 7 + 4 * k, 1);
    }
    rframe(bmp, 4, 7, 10, 23);
    l = (dprism.meter[0] * 13) / 32767;
    r = (dprism.meter[1] * 13) / 32767;
    if (l)
        FILLRECT(bmp, 6, 9, 6, 8 + l, 1);
    if (r)
        FILLRECT(bmp, 8, 9, 8, 8 + r, 1);
    label(bmp, 7, -1, "OUT");
}

static void order_text(char *b, int order)   /* the order as four letters: CMAG */
{
    static const char L[4] = { 'C', 'M', 'A', 'G' };
    int k;
    for (k = 0; k < 4; k++)
        b[k] = L[ch_order[order % 24][k]];
    b[4] = 0;
}

static void control(void *bmp, int pg, int i)
{
    int cx = col_x[i & 3], r = i >> 2, cy = ROW_Y(r), m = i & 3;
    const char *lbl;
    if (pg == 0) {
        int off = r == 1 ? !dprism.on[m] : 0;
        if (r == 0 && i < 3 && !dprism.on[i])
            off = 1;
        lbl = r ? fx_lbl[m][dprism.fx[m]] : knob_lbl[i];
        knob(bmp, cx, cy, fx_held_step() >= 0 ? shown(DH_S_KNOB(i)) : dprism.eff[i], i == K_TILT || (i == K_RATE && dprism.fx[M_MOVE] == FX_PITCH)
             || (i == K_TIME && dprism.fx[M_DIFF] == FX_REVERSE), off);
        if (dprism.gstate[i] == G_PLAY)    /* a gesture plays it: a dot over the knob */
            FILLRECT(bmp, cx - 1, cy + 10, cx + 1, cy + 11, 1);
    } else if (pg == 1) {
        lbl = sec_lbl[i];
        knob(bmp, cx, cy, shown(DH_S_SEC(i)), i == S_SENS || i >= S_OUT, 0);
    } else {
        char b[5];
        lbl = pg3_lbl[i];
        if (i < 4) {                        /* the module's own setting (a held step's lock in LOCK mode) */
            box(bmp, cx, cy, fx_lbl[i][shown(DH_S_FX(i)) % 5], 0);
            if (!shown(DH_S_ON(i)))
                line(bmp, cx - 7, cy - 7, cx + 7, cy + 7);
        } else if (i == 4) {
            order_text(b, shown(DH_S_ORDER));
            box(bmp, cx, cy, b, 0);
        } else if (i == 5) {
            box(bmp, cx, cy, styles[shown(DH_S_STYLE) % 3], 0);
        } else if (i == 6) {
            box(bmp, cx, cy, shown(DH_S_CROUTE) ? "PRE" : "POST", 0);
        } else {
            box(bmp, cx, cy, levels[shown(DH_S_LEVEL) & 3], 0);
        }
    }
    label(bmp, cx, LBL_Y(r), lbl);
}

static void draw_setup(void *bmp)
{
    static const char *const clocks[3] = { "FREE", "DIGITONE", "TAP" };
    static const char *const tracks[4] = { "T1", "T2", "T3", "T4" };
    int i;
    for (i = 0; i < G_COUNT; i++) {
        int y = 49 - 6 * i;
        TEXTF(bmp, FONT5, 3, y, -1, "%s", cfg_lbl[i]);
        if (i == G_SOURCE) {
            TEXTF(bmp, FONT5, 76, y, -1, "%s", !can_insert() ? "INPUTS ONLY" : dprism.source ? "DIGITONE" : "INPUTS");
        } else if (i == G_INPUT) {
            TEXTF(bmp, FONT5, 76, y, -1, "%s", dprism.mono ? "MONO (L)" : "STEREO");
        } else if (i == G_CLOCK) {
            TEXTF(bmp, FONT5, 76, y, -1, "%s", clocks[dprism.clock % 3]);
        } else if (i == G_BYPASS) {
            TEXTF(bmp, FONT5, 76, y, -1, "%s", dprism.trails ? "TRAILS" : "CUT");
        } else if (i == G_DUAL) {
            if (!dual)
                TEXTF(bmp, FONT5, 76, y, -1, "OFF");
            else
                TEXTF(bmp, FONT5, 76, y, -1, "%c%c%c%c", dual & 1 ? 'C' : '-', dual & 2 ? 'M' : '-',
                      dual & 4 ? 'D' : '-', dual & 8 ? 'T' : '-');
        } else if (i == G_MIDI) {
            TEXTF(bmp, FONT5, 76, y, -1, "%s", cfg_midi_any ? "ANY CH" : "AUTO CH");
        } else if (i == G_CLOSED) {
            TEXTF(bmp, FONT5, 76, y, -1, "%s", cfg_closed ? "ON" : "OFF");
        } else {
            TEXTF(bmp, FONT5, 76, y, -1, "%s", tracks[fx_lock_track() & 3]);
        }
        if (i == ui_row)
            FILLRECT(bmp, 1, y - 1, 126, y + 4, -1);
    }
}

void dprism_draw(void *bmp, void *ctrl)
{
    int i, pg = page_now();
    (void)ctrl;
    if (!ui_open)
        return;
    FILLRECT(bmp, 0, 0, 127, 63, 0);
    if (!ui_ready) {
        TEXTF(bmp, FONT5, 30, 30, -1, "DIGIPRISM...");
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
