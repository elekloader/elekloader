/* SPDX-License-Identifier: GPL-2.0-or-later */
/* DigiTriad's UI, on the pedal shell (pedal_ui.inc: the pages, SETUP, presets, TAP, BYPASS, the owner):
 *
 *   1 TRIAD     PITCH 1 PITCH 2 PITCH 3 MIX / TIME FEEDBACK KEY MICROTUNE
 *   2 ALT       TIME DIVISION 1-3 GLIDE / the voices' LEVELs, OUT
 *   3 SETTINGS  MODE (PITCH FEEDBACK, SERIES, DUAL + CROSS, DUAL) SCALE RHYTHM CORRECTION /
 *               ALGORITHM (CLASSIC, SMOOTH) HALF SPEED, PITCH SMOOTHING, SPREAD
 *   T1 PITCH: tap to turn the voices' intervals off (they repeat at unison) or on; hold for a moment's change
 *   T2 SWELL: tap to latch the swell (each note fades in), hold for a moment's swell
 *   T3 HALF SPEED: tap to turn it on or off
 *   T4 FREEZE: tap to latch, hold for a moment: the delay goes round as it is
 *   trig 1-12  KEY C .. B;  trig 13  KEY CHROMATIC;  trig 14  the next SCALE
 *   MIDI CCs: HALF SPEED 9, the pedal on 14 (below 64 bypassed), TIME 15, KEY 16, MICROTUNE 17,
 *   MIX 18, PITCH 1-3 19-21, SCALE 22, CORRECTION + GLIDE 23, FEEDBACK 24, TIME DIVISION 1-3 25-27, TAP 28 (127),
 *   MODE 29, PITCH SMOOTHING 30, SWELL 31 */
#pragma GCC optimize ("no-tree-loop-distribute-patterns")
#include "dtriad.h"
#include "tables.h"

#define PD_TAG      0x44484544u             /* "DHED" */
#define PD_NAME     "DIGITRIAD"
#define PD_SAVE_VER 1
#define PD_KBIP     ((1 << K_P1) | (1 << K_P2) | (1 << K_P3))
#define PD_SBIP     0
#define PD_NCFG     0

/* The Mod Menu's DIGITRIAD: a pyramid, its three faces */
static const u16 pd_icon[16] = {
    0x0000, 0x0100, 0x0380, 0x0540, 0x0540, 0x0920, 0x0920, 0x1110,
    0x1110, 0x2108, 0x2108, 0x4104, 0x4104, 0xfffe, 0x0000, 0x0000
};
static const char *const pd_title[3] = { "Triad", "Alt", "Settings" };
static const char *const pd_klbl[8] = { "PIT1", "PIT2", "PIT3", "MIX", "TIME", "FDBK", "KEY", "MTUN" };
static const u8 pd_kdef[8] = PD_KDEF;
static const char *const pd_slbl[8] = { "TDV1", "TDV2", "TDV3", "GLID", "LEV1", "LEV2", "LEV3", "OUT" };
static const u8 pd_sdef[8] = PD_SDEF;
static const char *const pd_olbl[8] = { "MODE", "SCAL", "RHYT", "CORR", "ALGO", "HALF", "SMTH", "SPRD" };
static const u8 pd_onum[8] = { 4, 7, 8, 3, 2, 2, 2, 3 };
static const u8 pd_odef[8] = PD_ODEF;
static const char *const o_mode[4] = { "PFB", "SER", "DX", "DUAL" };
static const char *const o_scale[7] = { "MAJ", "MIN", "MEL", "HARM", "DBL", "LYD5", "MIN5" };
static const char *const o_rhythm[8] = { "MAN", "NONE", "STR8", "TRIP", "DOTD", "SWNG", "GALP", "CASC" };
static const char *const o_corr[3] = { "NONE", "LOOS", "STRC" };
static const char *const o_algo[2] = { "CLAS", "SMTH" };
static const char *const o_onoff[2] = { "OFF", "ON" };
static const char *const o_spread[3] = { "WIDE", "NARR", "MONO" };
static const char *const *const pd_oval[8] = { o_mode, o_scale, o_rhythm, o_corr, o_algo, o_onoff, o_onoff, o_spread };
static const char *const pd_cfg_lbl[1] = { "" };
static const u8 pd_cfg_num[1] = { 1 }, pd_cfg_def[1] = { 0 };
static const char *const pd_cfg_none[1] = { "" };
static const char *const *const pd_cfg_val[1] = { pd_cfg_none };

#include "pedal_ui.inc"

static const char *const keys[13] = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B", "CHR" };
static const char *const tdivs[11] = { "1/6", "1/4", "1/3", "3/8", "1/2", "5/8", "2/3", "3/4", "5/6", "7/8", "1" };
static const char *const times[7] = { "1/16", "1/8T", "1/8", "1/8D", "1/4", "1/4D", "1/2" };
static const char *const degrees[8] = { "UNI", "2ND", "3RD", "4TH", "5TH", "6TH", "7TH", "OCT" };
static const u8 key_at[12] = { 1, 7, 18, 31, 42, 53, 62, 72, 82, 92, 103, 114 };   /* KEY's bands' middles */

static void fx_defaults(void) {}
static void fx_opened(void) {}
static void fx_tick(void) {}

static u32 fx_sig(void)
{
    return (u32)(ST.flag[F_NOTE] | ST.flag[F_VOICED] << 4 | ST.flag[F_FREEZE] << 5 | ST.flag[F_SWELL] << 6
                 | ST.flag[F_HARM] << 7);
}

static u32 t_at[4];                             /* when each track key went down (blocks) */

static void fx_tkey(int t, int ev)              /* T1, T2, T4: a tap latches (a hold is the render's: held) */
{
    if (ev == TK_PRESS)
        t_at[t] = ST.blocks;
    if (ev != TK_TAP || ST.blocks - t_at[t] > 450)  /* down 0.3 s or more: a hold, even without ev_hold */
        return;
    if (t == SW_HALF)
        ST.opt[O_HALF] = (u8)!ST.opt[O_HALF];
    else
        ST.sw[t] = (u8)!ST.sw[t];
}

static void fx_trig(int n)
{
    if (n < 12) {
        set_ctl(0, K_KEY, key_at[n]);
        flash(0, K_KEY);
    } else if (n == 12) {
        set_ctl(0, K_KEY, 127);
        flash(0, K_KEY);
    } else {
        ST.opt[O_SCALE] = (u8)((ST.opt[O_SCALE] + 1) % 7);
        flash(2, O_SCALE);
    }
}

static int fx_cc(int cc, int v)
{
    static const s8 knob_cc[9] = { K_KEY, K_MTUN, K_MIX, K_P1, K_P2, K_P3, -1, -1, K_FDBK };     /* 16 .. 24 */
    int on = v >= 64;
    if (cc >= 16 && cc <= 24 && knob_cc[cc - 16] >= 0) {
        set_ctl(0, knob_cc[cc - 16], v);
    } else if (cc == 15) {
        set_ctl(0, K_TIME, v);
    } else if (cc == 22) {                      /* SCALE: in bands */
        static const u8 from[6] = { 12, 38, 59, 79, 98, 120 };
        int s = 0;
        while (s < 6 && v >= from[s])
            s++;
        set_ctl(2, O_SCALE, s);
    } else if (cc == 23) {                      /* the lower half CORRECTION, the upper GLIDE */
        if (v < 64) {
            set_ctl(2, O_CORR, v * 3 / 64);
            set_ctl(1, S_GLIDE, 0);
        } else {
            set_ctl(1, S_GLIDE, (v - 64) * 2);
        }
    } else if (cc >= 25 && cc <= 27) {
        set_ctl(1, S_TD1 + cc - 25, v);
    } else if (cc == 9) {
        ST.opt[O_HALF] = (u8)on;
    } else if (cc == 14) {
        bypass_set(!on);
    } else if (cc == 28) {
        if (v == 127)
            tap();
    } else if (cc == 29) {
        cc_opt(O_MODE, v);
    } else if (cc == 30) {
        ST.opt[O_SMOOTH] = (u8)on;
    } else if (cc == 31) {
        ST.sw[SW_SWELL] = (u8)on;
    } else {
        return 0;
    }
    return 1;
}

static void interval_text(void *bmp, const void *font, int x, int y, int v)
{
    int key = dh_key[ST.knob[K_KEY]], s, a;
    if (key >= 12) {                            /* chromatic: semitones */
        s = dh_chrom[v];
        a = s < 0 ? -s : s;
        if (s == -128)
            TEXTF(bmp, font, x, y, -1, "OFF");
        else if (!s)
            TEXTF(bmp, font, x, y, -1, "UNI");
        else if (a == 12 || a == 24)
            TEXTF(bmp, font, x, y, -1, "%c%sOCT", s < 0 ? '-' : '+', a == 24 ? "2" : "");
        else
            TEXTF(bmp, font, x, y, -1, "%c%dST", s < 0 ? '-' : '+', a);
        return;
    }
    if (ST.opt[O_SCALE] < SC_LYDP) {            /* seven notes: the degree */
        s = dh_dia[v];
        a = s < 0 ? -s : s;
        if (s == -128)
            TEXTF(bmp, font, x, y, -1, "OFF");
        else if (!s)
            TEXTF(bmp, font, x, y, -1, "UNI");
        else
            TEXTF(bmp, font, x, y, -1, "%c%s%s", s < 0 ? '-' : '+', a == 14 ? "2" : "", degrees[a == 14 ? 7 : a]);
    } else {                                    /* pentatonic: the scale's notes */
        s = dh_pent[v];
        a = s < 0 ? -s : s;
        if (s == -128)
            TEXTF(bmp, font, x, y, -1, "OFF");
        else if (!s)
            TEXTF(bmp, font, x, y, -1, "UNI");
        else if (a >= 5)
            TEXTF(bmp, font, x, y, -1, "%c%sOCT", s < 0 ? '-' : '+', a == 10 ? "2" : "");
        else
            TEXTF(bmp, font, x, y, -1, "%c%d", s < 0 ? '-' : '+', a + 1);
    }
}

static int fx_value(void *bmp, const void *font, int x, int y, int pg, int i)
{
    int v;
    if (pg == 0) {
        v = ST.knob[i];
        if (i <= K_P3) {
            interval_text(bmp, font, x, y, v);
        } else if (i == K_MIX || i == K_FDBK) {
            TEXTF(bmp, font, x, y, -1, "%d%%", v * 100 / 127);
        } else if (i == K_TIME) {
            if (dtriad_tempo()) {
                int d = v * 7 / 128;
                TEXTF(bmp, font, x, y, -1, "%s %dMS", times[d > 6 ? 6 : d], (int)(ST.val[V_TIME] / 48));
            } else {
                TEXTF(bmp, font, x, y, -1, "%dMS", (int)(ST.val[V_TIME] / 48));
            }
        } else if (i == K_KEY) {
            TEXTF(bmp, font, x, y, -1, "%s", keys[dh_key[v]]);
        } else {
            TEXTF(bmp, font, x, y, -1, "%dC", v * 30 / 127);
        }
        return 1;
    }
    if (pg == 1 && i <= S_TD3) {
        int d = dh_tdiv[ST.sec[i]];
        if (ST.opt[O_RHYTHM])
            TEXTF(bmp, font, x, y, -1, "RHYTHM");
        else if (!d)
            TEXTF(bmp, font, x, y, -1, "OFF");
        else if (d < 0)
            TEXTF(bmp, font, x, y, -1, "%dMS", -d * 3);
        else
            TEXTF(bmp, font, x, y, -1, "%s", tdivs[d == 4 ? 0 : d == 6 ? 1 : d == 8 ? 2 : d == 9 ? 3 : d == 12 ? 4 :
                                                d == 15 ? 5 : d == 16 ? 6 : d == 18 ? 7 : d == 20 ? 8 : d == 21 ? 9 : 10]);
        return 1;
    }
    return 0;
}

static void fx_left(void *bmp)                  /* the note heard, the voices on, the latches */
{
    int k, n = ST.flag[F_NOTE];
    if (n)
        small_text(bmp, 7, 45, keys[n - 1]);
    if (ST.flag[F_VOICED])                      /* a pitch heard now: a bar under the note */
        FILLRECT(bmp, 3, 41, 11, 41, 1);
    for (k = 0; k < 3; k++)                     /* the voices: a square each, hollow while OFF */
        if (ST.knob[K_P1 + k]) {
            FILLRECT(bmp, 2 + 4 * k, 36, 4 + 4 * k, 38, 1);
            if (!ST.flag[F_HARM])
                px(bmp, 3 + 4 * k, 37, 0);
        } else {
            px(bmp, 3 + 4 * k, 37, 1);
        }
    if (ST.flag[F_FREEZE])
        small_text(bmp, 4, 31, "F");
    else if (ST.flag[F_SWELL] || ST.sw[SW_SWELL])
        small_text(bmp, 4, 31, "S");
    else if (ST.opt[O_HALF])
        small_text(bmp, 4, 31, "H");
}

static const char *fx_status(void)
{
    if (ST.flag[F_FREEZE])
        return "Freeze";
    if (!ST.flag[F_HARM])
        return "Pitch off";
    if (ST.sw[SW_SWELL])
        return "Swell";
    return 0;
}

static int fx_off(int pg, int i)
{
    int chrom = dh_key[ST.knob[K_KEY]] >= 12;
    if (pg == 1 && i <= S_TD3)
        return ST.opt[O_RHYTHM] != 0;           /* a RHYTHM pattern sets the divisions */
    if (pg == 1 && i >= S_LEV1 && i <= S_LEV3)
        return ST.knob[K_P1 + i - S_LEV1] == 0;
    if (pg == 2 && (i == O_SCALE || i == O_CORR))
        return chrom;
    if (pg == 0 && i == K_MTUN)
        return !ST.flag[F_HARM];
    return 0;
}
