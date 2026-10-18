/* SPDX-License-Identifier: GPL-2.0-or-later */
/* DigiPrism's UI: its Mod Menu entry, its screen, its keys and knobs, its MIDI CCs, its project data. While
 * it is open it owns the screen. With the source INPUTS (the default) it owns the audio while it is open, as
 * DigiSwarm does, and NO gives it back; with DIGITONE the pedal stays on after NO (dprism_owner.on) until BYPASS.
 *
 *   three pages, as the Digitone's own (LEFT, RIGHT or PAGE moves between them):
 *     1 PRIMARY    TILT RATE TIME MIX / AMOUNT x4 (each named by its module's effect)   push: its default
 *     2 SECONDARY  SENS DRIFT DRIFT OUT / VOL x4  (also FUNC + a knob on page 1)
 *     3 MODULES    the four effects / ORDER, FILTER style, CAPTURE route, input LEVEL   push A-D: on/off
 *   T1-T4      the four module buttons: press for the module's next effect, hold to bypass it
 *   trig 13    GESTURE: record the knobs you turn; again to play them (FUNC + 13 erases every gesture)
 *   trig 15    the TAP footswitch: tap a tempo; hold to CAPTURE, release to play it; tap to stop it
 *   trig 16    the BYPASS footswitch (with DUAL BYPASS the chosen modules; twice quickly, everything)
 *   YES SETUP, NO back (from a page: close);  PLAY, STOP, RECORD, TEMPO and LEVEL/DATA stay stock
 *   presets: in SETUP a trig key 1-8 saves that preset; FUNC + a trig key 1-8 recalls one
 *   MIDI: its CCs, on the auto channel (or any, in SETUP), while the page is open (or
 *   always, in SETUP) */
#pragma GCC optimize ("no-tree-loop-distribute-patterns")
#include "dprism.h"

#define KEY_FUNC    1
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

enum { G_SOURCE, G_INPUT, G_CLOCK, G_BYPASS, G_DUAL, G_MIDI, G_CLOSED, G_COUNT };   /* SETUP's rows */

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
                                              "CC CLOSED" };

static u8 ui_open, ui_setup, ui_pg, ui_func, ui_ready, ui_inited, ui_row;
static u8 ui_flash_ticks, ui_flash_pg, ui_flash_i;   /* a control's value in the title bar after a turn */
#define NPAGE 3
static u8 ui_msg_ticks, ui_msg_preset, ui_msg_kind;   /* a message for a second: 1 saved, 2 recalled, 3 empty */
static u8 ui_fx_ticks, ui_fx_m;                       /* a module's new effect, by name, for a second */
static u8 cfg_midi_any, cfg_closed, dual;             /* SETUP: CCs on any channel, CCs while closed, DUAL BYPASS */
static u32 clear_at;                        /* the next address to clear, 0 when nothing is */
static s32 enc_acc[8];
static u8 t_down[4], t_held[4];             /* the track keys: pressed, and held long enough to bypass */
static u8 cap_down, cap_was_playing;        /* trig 15 */
static u32 cap_at, tap_last, tap_iv[3], tap_n;
static u32 byp_at;
static u8 byp_undo;                         /* trig 16: the modules the last press toggled (DUAL BYPASS) */

/* ---- project data: the pedal and eight presets, saved with the project --------------------------------
 * 136 bytes: the gap mods share in a project holds 480, digitables' tables take 328 of it with its header,
 * and this block 144 with its own. A preset is the effects, which run, their order, FILTER's style, the
 * CAPTURE route and the eight primary knobs; the secondary controls and SETUP are the project's. */
#define NPRESET 8
struct dh_save {
    u8 ver, order, style, croute;
    u8 fx[4], on[4];
    u8 knob[8], sec[8];
    u8 level, bypass, trails, clock;
    u8 cfg, pad[3];
    u32 tap;
    u8 preset[NPRESET][12];             /* [0] 0x80 once saved | order; [1] fx 0,1; [2] fx 2,3; [3] on bits,
                                           style, route; [4-11] the knobs */
} __attribute__((aligned(4)));
static struct dh_save save;
#define SAVE_VER 1

static void defaults(void)
{
    int i;
    for (i = 0; i < 8; i++) {
        dprism.knob[i] = knob_def[i];
        dprism.sec[i] = sec_def[i];
    }
    for (i = 0; i < 4; i++) {
        dprism.fx[i] = fx_def[i];
        dprism.on[i] = 1;
    }
    dprism.order = 0;
    dprism.style = ST_TILT;
    dprism.croute = 0;
    dprism.level = 2;
    dprism.bypass = 1;                     /* off until it is opened: a project keeps its sound */
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
        for (i = 0; i < NPRESET; i++)
            save.preset[i][0] = 0;
    } else {
        for (i = 0; i < 8; i++) {
            dprism.knob[i] = save.knob[i] & 127;
            dprism.sec[i] = save.sec[i] & 127;
        }
        for (i = 0; i < 4; i++) {
            dprism.fx[i] = save.fx[i] < 5 ? save.fx[i] : 0;
            dprism.on[i] = save.on[i] & 1;
        }
        dprism.order = save.order;
        dprism.style = save.style < 3 ? save.style : 0;
        dprism.croute = save.croute & 1;
        dprism.level = save.level & 3;
        dprism.bypass = save.bypass & 1;
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

static void keep_save(void)                 /* the pedal into the project's copy (the presets are there) */
{
    int i;
    save.ver = SAVE_VER;
    for (i = 0; i < 8; i++) {
        save.knob[i] = dprism.knob[i];
        save.sec[i] = dprism.sec[i];
    }
    for (i = 0; i < 4; i++) {
        save.fx[i] = dprism.fx[i];
        save.on[i] = dprism.on[i];
    }
    save.order = dprism.order;
    save.style = dprism.style;
    save.croute = dprism.croute;
    save.level = dprism.level;
    save.bypass = dprism.bypass;
    save.trails = dprism.trails;
    save.clock = dprism.clock;
    save.tap = dprism.tap;
    save.cfg = (u8)(cfg_midi_any | cfg_closed << 1 | dprism.source << 2 | dprism.mono << 3 | dual << 4);
}

static void preset_save(int n)
{
    u8 *p = save.preset[n];
    int i;
    p[0] = (u8)(0x80 | dprism.order);
    p[1] = (u8)(dprism.fx[0] | dprism.fx[1] << 3);
    p[2] = (u8)(dprism.fx[2] | dprism.fx[3] << 3);
    p[3] = (u8)(dprism.on[0] | dprism.on[1] << 1 | dprism.on[2] << 2 | dprism.on[3] << 3 | dprism.style << 4
                | dprism.croute << 6);
    for (i = 0; i < 8; i++)
        p[4 + i] = dprism.knob[i];
}

static int preset_recall(int n)             /* -> 0 when the slot is empty */
{
    const u8 *p = save.preset[n];
    int i;
    if (!(p[0] & 0x80) || (p[0] & 0x7f) >= 24)
        return 0;
    dprism.order = p[0] & 0x7f;
    dprism.fx[0] = p[1] & 7;
    dprism.fx[1] = (p[1] >> 3) & 7;
    dprism.fx[2] = p[2] & 7;
    dprism.fx[3] = (p[2] >> 3) & 7;
    for (i = 0; i < 4; i++) {
        if (dprism.fx[i] > 4)
            dprism.fx[i] = 0;
        dprism.on[i] = (p[3] >> i) & 1;
    }
    dprism.style = (p[3] >> 4) & 3;
    if (dprism.style > 2)
        dprism.style = 0;
    dprism.croute = (p[3] >> 6) & 1;
    for (i = 0; i < 8; i++) {
        dprism.knob[i] = p[4 + i] & 127;
        dprism.gkill[i] = 1;               /* a preset's knobs, not the gestures over the old ones */
    }
    return 1;
}

static void message(int kind, int n)
{
    ui_msg_kind = (u8)kind;
    ui_msg_preset = (u8)(n + 1);
    ui_msg_ticks = 30;
}

/* ---- opening and closing -------------------------------------------------------------------------- */
static void dprism_open(void *brain, void *ev, int track)
{
    (void)brain, (void)ev, (void)track;
    ui_open = 1;
    ui_setup = 0;
    ui_pg = 0;
    ui_func = 0;
    dprism.bypass = 0;                     /* opening it steps on it */
    dprism.overload = 0;
}

static void dprism_close(void)
{
    ui_open = 0;
    dprism.grec = 0;
    cap_down = 0;
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

static void set_knob(int i, int sec, s32 v)
{
    if (v < 0) v = 0;
    if (v > 127) v = 127;
    if (sec) {
        dprism.sec[i] = (u8)v;
    } else {
        dprism.knob[i] = (u8)v;
        knob_moved(i);
    }
}

static int can_insert(void)                   /* the core takes the source DIGITONE (core-dn1 3.4) */
{
    return (core_audio_caps & CORE_AUDIO_INSERT) != 0;
}

static int page_now(void)                    /* FUNC held shows page 2 */
{
    return ui_func ? 1 : ui_pg;
}

static void show_fx(int m)
{
    ui_fx_m = (u8)m;
    ui_fx_ticks = 30;
    ui_flash_ticks = 0;
}

static void next_fx(int m)                    /* a module's button: its next effect, and it runs */
{
    dprism.fx[m] = (u8)((dprism.fx[m] + 1) % 5);
    dprism.on[m] = 1;
    show_fx(m);
}

static void step_setting(int i, int d)        /* page 3's controls */
{
    if (i < 4) {
        dprism.fx[i] = (u8)((dprism.fx[i] + 5 + d) % 5);
        show_fx(i);
    } else if (i == 4) {
        dprism.order = (u8)((dprism.order + 24 + d) % 24);
    } else if (i == 5) {
        dprism.style = (u8)((dprism.style + 3 + d) % 3);
    } else if (i == 6) {
        dprism.croute = (u8)(d > 0);
    } else {
        s32 v = dprism.level + d;
        dprism.level = (u8)(v < 0 ? 0 : v > 3 ? 3 : v);
    }
}

static void push(int i)                      /* an encoder pushed */
{
    int pg = page_now();
    if (pg < 2) {
        set_knob(i, pg, pg ? sec_def[i] : knob_def[i]);
        flash(pg, i);
    } else if (i < 4) {
        dprism.on[i] = !dprism.on[i];
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
    int m;
    if (dual && now - byp_at < 600 && byp_undo) {   /* a double press: everything */
        for (m = 0; m < 4; m++)
            if (byp_undo & (1 << m))
                dprism.on[m] = !dprism.on[m];
        byp_undo = 0;
        dprism.bypass = !dprism.bypass;
    } else if (dual && !dprism.bypass) {
        for (m = 0; m < 4; m++)
            if (dual & (1 << m))
                dprism.on[m] = !dprism.on[m];
        byp_undo = dual;
    } else {
        dprism.bypass = !dprism.bypass;
        byp_undo = 0;
    }
    if (!dprism.bypass)
        dprism.overload = 0;
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
    if (key == KEY_PLAY || key == KEY_STOP || key == KEY_TEMPO || key == KEY_RECORD)
        return 0;                           /* the sequencer and its tempo stay the Digitone's */
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
    if (key >= KEY_STEP1 && key < KEY_STEP1 + NPRESET && (ui_setup || ui_func)) {
        int n = (int)(key - KEY_STEP1);
        if (ui_setup) {                     /* SETUP: save the pedal as preset n */
            preset_save(n);
            message(1, n);
        } else {                            /* FUNC: recall it */
            message(preset_recall(n) ? 2 : 3, n);
        }
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
        else
            cfg_closed = !cfg_closed;
    } else if (key == KEY_LEFT || key == KEY_RIGHT || key == KEY_PAGE) {
        ui_pg = (u8)((ui_pg + (key == KEY_LEFT ? NPAGE - 1 : 1)) % NPAGE);
        ui_flash_ticks = 0;
    } else if (key >= KEY_PUSH_A && key < KEY_PUSH_A + 8 && !ui_setup) {
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
    dprism.on[track] = !dprism.on[track];
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
    if (pg < 2) {
        set_knob(i, pg, (pg ? dprism.sec[i] : dprism.knob[i]) + d);
        flash(pg, i);
    } else {
        step_setting(i, d > 0 ? 1 : -1);
    }
    return 1;
}

/* ---- MIDI: its CC chart (core-dn1 3.2's ev_midi_cc, the MIDI task) --------------------
 * While the page is open (or always, with SETUP's CC CLOSED on) the CCs below are DigiPrism's when they come
 * on the auto channel (or on a track's channel too, with SETUP's MIDI CC: ANY CH): the Digitone does not
 * apply them. Every other CC goes on to the Digitone. */
static const u8 cc_knob[8] = { K_TILT, K_CAMT, K_RATE, K_MAMT, K_TIME, K_AAMT, K_MIX, K_GAMT };      /* 64-71 */
static const u8 cc_sec[8] = { S_SENS, S_CVOL, S_MDRIFT, S_MVOL, S_DDRIFT, S_AVOL, S_OUT, S_GVOL };  /* 72-79 */

int dprism_cc(int track, int cc, int value, int flags)
{
    int on = value >= 64;
    (void)track;
    if (!ui_ready || (!ui_open && !cfg_closed) || (!(flags & CORE_MIDI_CC_AUTO) && !cfg_midi_any))
        return 0;
    if (value < 0 || value > 127)
        return 0;
    if (cc >= 64 && cc <= 71) {
        int k = cc_knob[cc - 64];
        dprism.knob[k] = (u8)value;
        knob_moved(k);
    } else if (cc >= 72 && cc <= 79) {
        dprism.sec[cc_sec[cc - 72]] = (u8)value;
    } else if (cc >= 16 && cc <= 19) {          /* a module's effect: five bands of 22, then off */
        int m = cc - 16;
        if (value >= 110) {
            dprism.on[m] = 0;
        } else {
            dprism.fx[m] = (u8)(value / 22);
            dprism.on[m] = 1;
        }
    } else if (cc == 91) {                      /* bypass below 64 */
        dprism.bypass = (u8)!on;
        if (on)
            dprism.overload = 0;
    } else if (cc == 92) {                      /* total bypass, dual bypass, total engage */
        int m;
        if (value < 32) {
            dprism.bypass = 1;
        } else if (value < 64) {
            for (m = 0; m < 4; m++)
                if (dual & (1 << m))
                    dprism.on[m] = 0;
        } else {
            dprism.bypass = 0;
            dprism.overload = 0;
            for (m = 0; m < 4; m++)
                dprism.on[m] = 1;
        }
    } else if (cc >= 103 && cc <= 106) {        /* a module's bypass */
        dprism.on[cc - 103] = (u8)on;
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
        dprism.croute = (u8)on;
    } else if (cc == 84) {                      /* FILTER: LPF, TILT, HPF */
        dprism.style = (u8)(value < 44 ? ST_LPF : value < 88 ? ST_TILT : ST_HPF);
    } else if (cc == 93) {                      /* TAP */
        tap();
    } else if (cc == 94) {                      /* the input level, in four bands */
        dprism.level = (u8)(value >> 5);
    } else if (cc == 95) {
        /* the calibration menu: DigiPrism has none (LEVL on page 3 sets the input level), but taken, so the
         * Digitone does not apply it to the active track meanwhile */
    } else {
        return 0;
    }
    return 1;
}

/* ---- the UI task: clearing, the owner, the footswitches, redraws ------------------------------------ */
static u32 ui_sig, ui_ticks;

static u32 signature(void)                  /* what the page shows, but the meter, folded into a word */
{
    u32 h = (u32)(page_now() | ui_setup << 2 | ui_row << 3 | (ui_flash_ticks != 0) << 6 | (ui_msg_ticks != 0) << 7
                  | (ui_fx_ticks != 0) << 8 | dprism.order << 9 | dprism.style << 14 | dprism.croute << 16
                  | dprism.level << 17 | dprism.bypass << 19 | dprism.overload << 20 | dprism.grec << 21
                  | dprism.cstate << 22 | dual << 24 | dprism.clock << 28 | dprism.trails << 30
                  | (u32)dprism.source << 31);
    int k;
    for (k = 0; k < 8; k++)
        h = h * 31 + dprism.eff[k] + (dprism.sec[k] << 8) + (dprism.gstate[k] << 16);
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
    if (dprism.source == SRC_DIGITONE && can_insert()) {   /* an insert: on until BYPASS, open or not */
        dprism_owner.flags = CORE_AUDIO_INSERT;
        dprism_owner.on = ui_ready && (!dprism.bypass || dprism.active);
    } else {                                /* the inputs: the audio is DigiPrism's while the page is open */
        dprism_owner.flags = CORE_AUDIO_MUTE_VOICES;
        dprism_owner.on = ui_ready && ui_open;
    }
    dprism.meter[0] = (u16)(dprism.meter[0] - (dprism.meter[0] >> 3));
    dprism.meter[1] = (u16)(dprism.meter[1] - (dprism.meter[1] >> 3));
    keep_save();
    if (ui_msg_ticks)
        ui_msg_ticks--;
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
    int v = sec ? dprism.sec[i] : dprism.eff[i];
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
    } else if (ui_msg_ticks) {
        TEXTF(bmp, FONT_TTL, 19, 56, -1, "%s P%d", ui_msg_kind == 1 ? "Saved" : ui_msg_kind == 2 ? "Loaded" : "Empty",
              ui_msg_preset);
    } else if (ui_flash_ticks && !ui_setup) {
        if (ui_flash_pg < 2) {
            const char *l = ui_flash_pg ? sec_lbl[ui_flash_i]
                                        : ui_flash_i < 4 ? knob_lbl[ui_flash_i] : fx_lbl[ui_flash_i - 4][dprism.fx[ui_flash_i - 4]];
            TEXTF(bmp, FONT_TTL, 19, 56, -1, "%s", l);
            value_text(bmp, FONT_TTL, 19 + text_w(l) + 6, 56, ui_flash_i, ui_flash_pg);
        }
    } else if (ui_setup) {
        TEXTF(bmp, FONT_TTL, 19, 56, -1, "Setup (trig: save)");
    } else if (dprism.overload) {
        TEXTF(bmp, FONT_TTL, 19, 56, -1, "CPU full: bypassed");
    } else if (dprism.grec) {
        TEXTF(bmp, FONT_TTL, 19, 56, -1, "Gesture: rec");
    } else if (dprism.bypass) {
        TEXTF(bmp, FONT_TTL, 19, 56, -1, "Bypassed (%d/%d)", pg + 1, NPAGE);
    } else {
        TEXTF(bmp, FONT_TTL, 19, 56, -1, "%s (%d/%d)", pg == 0 ? "Prism" : pg == 1 ? "Secondary" : "Modules",
              pg + 1, NPAGE);
    }
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

static void order_text(char *b)              /* the order as four letters: CMAG */
{
    static const char L[4] = { 'C', 'M', 'A', 'G' };
    int k;
    for (k = 0; k < 4; k++)
        b[k] = L[ch_order[dprism.order % 24][k]];
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
        knob(bmp, cx, cy, dprism.eff[i], i == K_TILT || (i == K_RATE && dprism.fx[M_MOVE] == FX_PITCH)
             || (i == K_TIME && dprism.fx[M_DIFF] == FX_REVERSE), off);
        if (dprism.gstate[i] == G_PLAY)    /* a gesture plays it: a dot over the knob */
            FILLRECT(bmp, cx - 1, cy + 10, cx + 1, cy + 11, 1);
    } else if (pg == 1) {
        lbl = sec_lbl[i];
        knob(bmp, cx, cy, dprism.sec[i], i == S_SENS || i >= S_OUT, 0);
    } else {
        char b[5];
        lbl = pg3_lbl[i];
        if (i < 4) {
            box(bmp, cx, cy, fx_lbl[i][dprism.fx[i]], 0);
            if (!dprism.on[i])
                line(bmp, cx - 7, cy - 7, cx + 7, cy + 7);
        } else if (i == 4) {
            order_text(b);
            box(bmp, cx, cy, b, 0);
        } else if (i == 5) {
            box(bmp, cx, cy, styles[dprism.style % 3], 0);
        } else if (i == 6) {
            box(bmp, cx, cy, dprism.croute ? "PRE" : "POST", 0);
        } else {
            box(bmp, cx, cy, levels[dprism.level & 3], 0);
        }
    }
    label(bmp, cx, LBL_Y(r), lbl);
}

static void draw_setup(void *bmp)
{
    static const char *const clocks[3] = { "FREE", "DIGITONE", "TAP" };
    int i;
    for (i = 0; i < G_COUNT; i++) {
        int y = 47 - 7 * i;
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
        } else {
            TEXTF(bmp, FONT5, 76, y, -1, "%s", cfg_closed ? "ON" : "OFF");
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
    for (i = 0; i < 8; i++)
        control(bmp, pg, i);
}
