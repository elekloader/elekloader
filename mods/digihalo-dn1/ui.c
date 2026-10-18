/* SPDX-License-Identifier: GPL-2.0-or-later */
/* DigiHalo's UI, on the pedal shell (pedal_ui.inc: the pages, fxkit's MIXER, LFOs and LOCK mode, SETUP, TAP, BYPASS):
 *
 *   1 (the machine's name)  DECAY PRE-DELAY MIX TONE / PARAM 1 PARAM 2 MOD LOW END
 *                           (TAPE and NONLINEAR: TIME and FEEDBACK for DECAY and PRE-DELAY)
 *   2 PARAMS                the machine's other parameters (up to three) BOOST / DIFFUSION MOD RATE WIDTH DRY
 *   3 SETUP                 TYPE HOLD (FREEZE, INFINITE) PERSIST SYNC / CAB SPILLOVER
 *   PARAM 1, PARAM 2 and the extras are the machine's own: their labels follow TYPE,
 *   and choosing a machine puts them at that machine's starting values (a project loaded keeps its own).
 *   T1 the footswitch's hold: held, HOLD while held; tapped, HOLD latched on or off (FREEZE or INFINITE)
 *   T2 tapped: the next machine (0.1: the next saved preset); held: the wet out, swelling back on release
 *   T3, T4 the machine down, up
 *   trig 1-12 the machines;  trig 13 HOLD latched on or off;  trig 14 HOLD's mode
 *   MIDI (the chart in README.md): TYPE 19, DECAY 17, PRE-DELAY 18, MIX 15, TONE 3, MOD 14, PARAM 1 9,
 *   PARAM 2 16, BOOST 23, PERSIST 22, HOLD MODE 70, MIDI CLOCK (SYNC) 71, HOLD 97, TAP 93, BYPASS 102,
 *   footswitches A, B, C 80, 82, 81 (taken: 0.1's presets), and each machine's parameters (24-69) */
#pragma GCC optimize ("no-tree-loop-distribute-patterns")
#include "dhalo.h"
#include "tables.h"

#define PD_TAG      0x44534B59u             /* "DSKY" */
#define PD_NAME     "DIGIHALO"
#define PD_SAVE_VER 1
#define PD_KBIP     0
#define PD_SBIP     (1 << S_BOOST)
#define PD_NCFG     0

/* The Mod Menu's DIGIHALO: a cloud over falling sparks */
static const u16 pd_icon[16] = {
    0x0000, 0x0380, 0x0C60, 0x1010, 0x7008, 0x8004, 0x8004, 0x8004,
    0x7FF8, 0x0000, 0x2492, 0x0000, 0x4924, 0x0000, 0x2492, 0x0000
};
static const char *const mc_name[MC_N] = { "Room", "Hall", "Plate", "Spring", "Swell", "Swirl", "Nebula", "Vowel",
                                           "Shimmer", "Tape", "Nonlinear", "Reflections" };
static const char *const mc_abbr[MC_N] = { "RM", "HL", "PL", "SP", "SW", "BL", "CL", "CH", "SH", "MG", "NL", "RF" };
static const char *pd_title[3] = { "Hall", "Params", "Setup" };
static const char *pd_klbl[8] = { "DECY", "PRE", "MIX", "TONE", "SIZE", "MID", "MOD", "LOW" };
static const u8 pd_kdef[8] = PD_KDEF;
static const char *pd_slbl[8] = { "", "", "", "BOST", "DIFF", "MRAT", "WIDE", "DRY" };
static const u8 pd_sdef[8] = PD_SDEF;
static const char *const pd_olbl[8] = { "TYPE", "HOLD", "PRST", "SYNC", "CAB", "SPIL", "", "" };
static const u8 pd_onum[8] = { MC_N, 2, 2, 2, 2, 2, 0, 0 };
static const u8 pd_odef[8] = PD_ODEF;
static const char *const o_type[MC_N] = { "ROOM", "HALL", "PLAT", "SPRG", "SWEL", "SWRL", "NEBU", "VOWL", "SHIM",
                                          "TAPE", "NLIN", "REFL" };
static const char *const o_hold[2] = { "FRZ", "INF" };
static const char *const o_onoff[2] = { "OFF", "ON" };
static const char *const *const pd_oval[8] = { o_type, o_hold, o_onoff, o_onoff, o_onoff, o_onoff, 0, 0 };
static const char *const pd_cfg_lbl[1] = { "" };
static const u8 pd_cfg_num[1] = { 1 }, pd_cfg_def[1] = { 0 };
static const char *const pd_cfg_none[1] = { "" };
static const char *const *const pd_cfg_val[1] = { pd_cfg_none };

#include "pedal_ui.inc"

/* ---- the machines' parameters: labels, starting steps, value names, CCs ------------------------------- */
static const char *const p_lbl[MC_N][5] = {
    { "SIZE", "DIFF" }, { "SIZE", "MID" }, { "SIZE", "" }, { "DWEL", "SPRS" }, { "RISE", "MODE" },
    { "LEN", "FDBK" }, { "DIFF", "" }, { "VOWL", "RESO" }, { "SHF1", "SHF2", "AMT", "MODE" },
    { "HEAD", "SPAC", "DIFF" }, { "SHAP", "DIFF", "LDCY", "LLVL", "MSPD" }, { "LOCX", "LOCY", "SHAP" } };
static const u8 p_def[MC_N][5] = {
    { 0, 12 }, { 0, 10 }, { 1, 0 }, { 1, 1 }, { 15, 0 }, { 9, 6 }, { 12, 0 }, { 0, 1 }, { 25, 21, 9, 1 },
    { 1, 0, 8 }, { 1, 10, 8, 6, 8 }, { 3, 3, 1 } };
static const char *const v_size[3][2] = { { "STUDIO", "CLUB" }, { "CONCERT", "ARENA" }, { "SMALL", "LARGE" } };
static const char *const v_dwell[4] = { "CLEAN", "COMBO", "TUBE", "OVERDRIVE" };
static const char *const v_swmode[2] = { "SWELL WET", "SWELL DRY" };
static const char *const v_vowel[7] = { "AAHHOO", "AAHH", "AAHHOH", "OH", "OOOHOH", "OOOO", "RANDOM" };
static const char *const v_reso[3] = { "MILD", "MEDIUM", "HIGH" };
static const char *const v_shift[28] = { "-Oct", "-M7", "-m7", "-M6", "-m6", "-P5", "-TT", "-P4", "-M3", "-m3",
    "-M2", "-m2", "-10ct", "+10ct", "+m2", "+M2", "+m3", "+M3", "+P4", "+TT", "+P5", "+m6", "+M6", "+m7", "+M7",
    "+Oct", "+Oct+5", "+2 Oct" };
static const char *const v_shmode[3] = { "INPUT", "REGEN", "IN+REG" };
static const char *const v_heads[3] = { "3", "4", "6" };
static const char *const v_spacing[2] = { "EVEN", "UNEVEN" };
static const char *const v_shape[6] = { "SWOOSH", "REVERSE", "RAMP", "GATE", "GAUSS", "BOUNCE" };
static const char *const v_room[3] = { "SQUARE", "RECT", "OBLONG" };
static const char *const pre_names[9] = { "1/32", "1/16T", "1/16", "1/8T", "1/8", "1/8D", "1/4", "1/4D", "1/2" };
/* each machine's own CCs (the chart in README.md): the CC, the machine, the slot (5: LOW END) */
static const u8 p_cc[][3] = {
    { 61, MC_ROOM, 5 }, { 59, MC_ROOM, P_P1 }, { 58, MC_ROOM, P_P2 },
    { 39, MC_HALL, 5 }, { 40, MC_HALL, P_P1 }, { 42, MC_HALL, P_P2 },
    { 69, MC_PLATE, 5 }, { 68, MC_PLATE, P_P1 },
    { 64, MC_SPRING, 5 }, { 63, MC_SPRING, P_P1 }, { 62, MC_SPRING, P_P2 },
    { 65, MC_SWELL, 5 }, { 66, MC_SWELL, P_P1 }, { 67, MC_SWELL, P_P2 },
    { 31, MC_SWIRL, 5 }, { 32, MC_SWIRL, P_P1 }, { 30, MC_SWIRL, P_P2 },
    { 38, MC_NEBULA, 5 }, { 37, MC_NEBULA, P_P1 },
    { 33, MC_VOWEL, P_P1 }, { 34, MC_VOWEL, P_P2 },
    { 25, MC_SHIMMER, P_P1 }, { 26, MC_SHIMMER, P_P2 }, { 27, MC_SHIMMER, P_X1 }, { 28, MC_SHIMMER, P_X2 },
    { 24, MC_SHIMMER, 5 },
    { 55, MC_TAPE, 5 }, { 57, MC_TAPE, P_P1 }, { 54, MC_TAPE, P_P2 }, { 56, MC_TAPE, P_X1 },
    { 44, MC_NONLIN, 5 }, { 46, MC_NONLIN, P_P1 }, { 45, MC_NONLIN, P_P2 }, { 47, MC_NONLIN, P_X1 },
    { 48, MC_NONLIN, P_X2 }, { 43, MC_NONLIN, P_X3 },
    { 52, MC_REFL, 5 }, { 49, MC_REFL, P_P1 }, { 50, MC_REFL, P_P2 }, { 51, MC_REFL, P_X1 } };

static int mc_now(void)                         /* the machine the page shows (the pedal's own TYPE) */
{
    int t = OPT(O_TYPE);
    return t < 0 ? 0 : t % MC_N;
}

static void slot_set(int slot, int v)           /* a slot's control: knob E, F, page 2's A-C, or LOW END */
{
    if (slot == P_P1 || slot == P_P2 || slot == 5)
        set_ctl(0, slot == P_P1 ? K_P1 : slot == P_P2 ? K_P2 : K_LOW, v);
    else
        set_ctl(1, S_X1 + slot - P_X1, v);
}

static void labels(int mc)
{
    int k;
    pd_title[0] = mc_name[mc];
    pd_klbl[K_DECAY] = mc == MC_TAPE || mc == MC_NONLIN ? "TIME" : "DECY";
    pd_klbl[K_PRE] = mc == MC_TAPE || mc == MC_NONLIN ? "FDBK" : "PRE";
    pd_klbl[K_P1] = p_lbl[mc][P_P1] ? p_lbl[mc][P_P1] : "";
    pd_klbl[K_P2] = p_lbl[mc][P_P2] ? p_lbl[mc][P_P2] : "";
    for (k = 0; k < 3; k++)
        pd_slbl[S_X1 + k] = p_lbl[mc][P_X1 + k] ? p_lbl[mc][P_X1 + k] : "";
}

/* A machine chosen: its extras at its starting steps, and PARAM 1 and 2 too unless the knobs came with it (a
 * project loaded) */
static void machine_defaults(int mc, int params)
{
    int s;
    for (s = params ? P_P1 : P_X1; s <= P_X3; s++) {
        int n = sky_pn[mc][s];
        slot_set(s, n ? sky_knob(p_def[mc][s], n) : 0);
    }
}

static u8 last_type = 0xff, last_knob[8], last_prst = 0xff, last_trails = 0xff;
static u32 t1_at;                              /* T1's press, in blocks */

static void fx_defaults(void)
{
    last_type = 0xff;
}

static void fx_opened(void) {}

static void fx_tick(void)
{
    int k, same = 1, mc = mc_now();
    if (mc != last_type) {
        if (last_type != 0xff) {
            for (k = 0; k < 8; k++)
                if (KNOB(k) != last_knob[k])
                    same = 0;
            machine_defaults(mc, same);
        }
        last_type = (u8)mc;
        labels(mc);
    }
    for (k = 0; k < 8; k++)
        last_knob[k] = (u8)KNOB(k);
    /* PERSIST (page 3) is the shell's TRAILS (SETUP's BYPASS): whichever moved */
    if (OPT(O_PERSIST) != last_prst)
        ST.trails = (u8)OPT(O_PERSIST);
    else if (ST.trails != last_trails)
        set_opt(O_PERSIST, ST.trails);
    last_prst = (u8)OPT(O_PERSIST);
    last_trails = ST.trails;
}

static u32 fx_sig(void)
{
    return (u32)(ST.flag[F_HOLD] | ST.flag[F_SWELL] << 2 | ST.flag[F_SWITCH] << 3 | ST.flag[F_MC] << 4);
}

static void type_set(int mc)
{
    set_ctl(2, O_TYPE, mc < 0 ? MC_N - 1 : mc % MC_N);
    flash(2, O_TYPE);
}

static void next_preset(void)                   /* T2: the next machine (0.1: the next saved preset) */
{
    type_set(mc_now() + 1);
}

static void fx_tkey(int t, int ev)
{
    if (t == 0) {                               /* HOLD: the shell's held[0] is the momentary; a short press (under
                                                   0.4 s, whether or not the core saw a hold) latches */
        if (ev == TK_PRESS)
            t1_at = ST.blocks;
        else if (ev == TK_TAP && ST.blocks - t1_at < 600)
            ST.sw[SW_HOLD] = !ST.sw[SW_HOLD];
    } else if (t == 1) {
        if (ev == TK_TAP)
            next_preset();
        else if (ev == TK_HOLD)
            ST.sw[SW_SWELL] = 1;
        else if (ev == TK_UP)
            ST.sw[SW_SWELL] = 0;
    } else if (ev == TK_PRESS) {
        type_set(mc_now() + (t == 2 ? -1 : 1));
    }
}

static void fx_trig(int n)
{
    if (n < MC_N) {
        type_set(n);
    } else if (n == 12) {
        ST.sw[SW_HOLD] = !ST.sw[SW_HOLD];
    } else {
        set_ctl(2, O_HOLD, !OPT(O_HOLD));
        flash(2, O_HOLD);
    }
}

static int fx_cc(int cc, int v)
{
    static const s8 knob_cc[20] = { -1, -1, -1, K_TONE, -1, -1, -1, -1, -1, K_P1, -1, -1, -1, -1, K_MOD, K_MIX,
                                    K_P2, K_DECAY, K_PRE, -1 };     /* CC 0 .. 19 */
    unsigned k;
    if (cc < 20 && knob_cc[cc] >= 0) {
        set_ctl(0, knob_cc[cc], v);
    } else if (cc == 19) {
        set_ctl(2, O_TYPE, v > MC_N - 1 ? MC_N - 1 : v);
    } else if (cc == 23) {                      /* BOOST 0-60: -3 .. +3 dB */
        set_ctl(1, S_BOOST, 59 + (v > 60 ? 60 : v) * 21 / 60);
    } else if (cc == 22) {
        set_ctl(2, O_PERSIST, v ? 1 : 0);
    } else if (cc == 70) {
        set_ctl(2, O_HOLD, v ? HM_INFINITE : HM_FREEZE);
    } else if (cc == 71) {
        set_ctl(2, O_SYNC, v ? 1 : 0);
    } else if (cc == 97) {
        ST.sw[SW_HOLD] = (u8)(v >= 64);
    } else if (cc == 93) {
        tap();
    } else if (cc == 102) {
        bypass_set(v < 64);
    } else if (cc == 80 || cc == 82 || cc == 81) {    /* A, B, C: 0.1's presets; taken, and they
                                                       do nothing else */
        ;
    } else {
        for (k = 0; k < sizeof p_cc / sizeof p_cc[0]; k++)
            if (p_cc[k][0] == cc) {
                int mc = p_cc[k][1], s = p_cc[k][2], n = s == 5 ? 21 : sky_pn[mc][s];
                if (mc == mc_now() && n)
                    slot_set(s, sky_knob(v >= n ? n - 1 : v, n));
                return 1;                       /* another machine's: taken, and ignored */
            }
        return 0;
    }
    return 1;
}

/* a slot's value: its name or its step */
static int slot_text(void *bmp, const void *font, int x, int y, int mc, int s, int knob)
{
    int n = sky_pn[mc][s], v = sky_step(knob, n);
    const char *t = 0;
    if (!n)
        return 0;
    if (s == P_P1) {
        if (mc <= MC_PLATE) t = v_size[mc][v];
        else if (mc == MC_SPRING) t = v_dwell[v];
        else if (mc == MC_VOWEL) t = v_vowel[v];
        else if (mc == MC_SHIMMER) t = v_shift[v];
        else if (mc == MC_TAPE) t = v_heads[v];
        else if (mc == MC_NONLIN) t = v_shape[v];
        else if (mc == MC_SWELL) {
            u32 ms = (u32)sky_rise[v] / 48;
            TEXTF(bmp, font, x, y, -1, "%d.%02dS", (int)(ms / 1000), (int)(ms / 10 % 100));
            return 1;
        }
    } else if (s == P_P2) {
        if (mc == MC_SPRING) {
            TEXTF(bmp, font, x, y, -1, "%d", v + 1);
            return 1;
        }
        if (mc == MC_SWELL) t = v_swmode[v];
        else if (mc == MC_VOWEL) t = v_reso[v];
        else if (mc == MC_SHIMMER) t = v ? v_shift[v - 1] : "OFF";
        else if (mc == MC_TAPE) t = v_spacing[v];
    } else if (s == P_X2 && mc == MC_SHIMMER) {
        t = v_shmode[v];
    } else if (s == P_X1 && mc == MC_REFL) {
        t = v_room[v];
    }
    if (mc == MC_REFL && s <= P_P2) {           /* LOCATION: L3 .. C .. R3, F3 .. C .. B3 */
        if (v == 3)
            TEXTF(bmp, font, x, y, -1, "C");
        else
            TEXTF(bmp, font, x, y, -1, "%c%d", v < 3 ? (s == P_P1 ? 'L' : 'F') : (s == P_P1 ? 'R' : 'B'),
                  v < 3 ? 3 - v : v - 3);
        return 1;
    }
    if (t)
        TEXTF(bmp, font, x, y, -1, "%s", t);
    else
        TEXTF(bmp, font, x, y, -1, "%d", v);
    return 1;
}

static int fx_value(void *bmp, const void *font, int x, int y, int pg, int i)
{
    int mc = mc_now(), v;
    if (pg == 0) {
        v = ctl(0, i);
        if (i == K_DECAY) {
            u32 ms = ST.val[V_RT];
            if (ms < 1000)
                TEXTF(bmp, font, x, y, -1, "%dmS", (int)ms);
            else
                TEXTF(bmp, font, x, y, -1, "%d.%02dS", (int)(ms / 1000), (int)(ms / 10 % 100));
        } else if (i == K_PRE) {
            if (mc == MC_TAPE || mc == MC_NONLIN)
                TEXTF(bmp, font, x, y, -1, "%d%%", v * 100 / 127);
            else if (ST.opt[O_SYNC] && dhalo_tempo())
                TEXTF(bmp, font, x, y, -1, "%s", pre_names[v * 9 / 128]);
            else
                TEXTF(bmp, font, x, y, -1, "%dmS", (int)(ST.val[V_PRE] / 48));
        } else if (i == K_P1 || i == K_P2) {
            return slot_text(bmp, font, x, y, mc, i == K_P1 ? P_P1 : P_P2, v);
        } else if (i == K_LOW) {
            int s = sky_step(v, 21) - 10;
            TEXTF(bmp, font, x, y, -1, "%c%d", s < 0 ? '-' : '+', s < 0 ? -s : s);
        } else {
            return 0;
        }
        return 1;
    }
    if (pg == 1) {
        v = ctl(1, i);
        if (i <= S_X3)
            return slot_text(bmp, font, x, y, mc, P_X1 + i, v);
        if (i == S_BOOST) {                     /* the shell's output level: 64 is 0 dB */
            int t = v >= 64 ? (v - 64) * 120 / 63 : (v - 64) * 360 / 63, a = t < 0 ? -t : t;
            TEXTF(bmp, font, x, y, -1, "%c%d.%ddB", t < 0 ? '-' : '+', a / 10, a % 10);
            return 1;
        }
    }
    return 0;
}

static void fx_left(void *bmp)                  /* the machine; F or I while HOLD is on; S while swelling */
{
    int h = ST.flag[F_HOLD];
    small_text(bmp, 7, 44, mc_abbr[ST.flag[F_MC] % MC_N]);
    if (h)
        small_text(bmp, 4, 37, h == 1 ? "F" : "I");
    if (ST.flag[F_SWELL])
        small_text(bmp, 10, 37, "S");
}

static const char *fx_status(void)
{
    int h = ST.flag[F_HOLD];
    if (h)
        return h == 1 ? "Freeze" : "Infinite";
    if (ST.sw[SW_SWELL])
        return "Swell: wet out";
    return 0;
}

static int fx_off(int pg, int i)
{
    int mc = mc_now();
    if (pg == 0 && (i == K_P1 || i == K_P2))
        return !sky_pn[mc][i == K_P1 ? P_P1 : P_P2];
    if (pg == 1 && i <= S_X3)
        return !sky_pn[mc][P_X1 + i];
    if (pg == 2 && i == O_SYNC)
        return !dhalo_tempo();
    return 0;
}
