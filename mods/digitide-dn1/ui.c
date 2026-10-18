/* SPDX-License-Identifier: GPL-2.0-or-later */
/* DigiTide's UI, on the pedal shell (pedal_ui.inc: the pages, fxkit's MIXER, LFOs and LOCK mode, SETUP, TAP, BYPASS):
 *
 *   1 TIDE      TIME MIX LENGTH BALANCE / MODIFY (wet) CLOCK MODIFY (looper) TONE   (the wet side left, the
 *               shared knobs in the middle, the looper's right)
 *   2 EXTRAS    WIDTH FADE DIRECT OUT / WOBL DAMP XFAD RAMP
 *   3 TOGGLES   WET (REVERB, DELAY, GRAB) LOOP (ENV, TAPE, STRETCH) ROUTING (IN, ML+IN, ML) SYNC (OFF, LOOP to
 *               the wet, WET to the loop) / SPREAD (OFF, BOTH, WET, LOOP) LATCH DRY KILL NO DUB
 *   T1 the wet channel: tap on/off, hold FREEZE      T2 the looper: tap on/off, hold OVERDUB
 *   T3 tap: the next wet mode, hold: the next ROUTING T4 tap: the next loop mode, hold: the next SYNC
 *   trig 1-6   TIME's six divisions;  trig 7-12  LENGTH's six;  trig 13, 14  CLOCK a step down, up
 *   MIDI: TIME 14, MIX 15, LENGTH 16, MODIFY 17, CLOCK 18, MODIFY 19,
 *   RAMP 20, WET 21, ROUTING 22, LOOP 23, WIDTH 24, FADE 26, TONE 27, BALANCE 28, DIRECT 29, SYNC 31, SPREAD's
 *   channels 32, LOOP BUFFER 33, MONO 72, SPREAD 73, DRY KILL 74, TRAILS 75, LATCH 76, NO DUB 77, SMOOTH 78, the looper on
 *   102, the wet on 103, the extras page 104, FREEZE 105, OVERDUB 106, TAP 107 and 93 */
#pragma GCC optimize ("no-tree-loop-distribute-patterns")
#include "dtide.h"
#include "tables.h"

#define PD_TAG      0x444D4F44u             /* "DMOD" */
#define PD_NAME     "DIGITIDE"
#define PD_SAVE_VER 1
#define PD_KBIP     (1 << K_BAL)
#define PD_SBIP     0
#define PD_NCFG     2

/* The Mod Menu's DIGITIDE: a crescent moon */
static const u16 pd_icon[16] = {
    0x0000, 0x0780, 0x1f00, 0x3e00, 0x3c00, 0x7c00, 0x7c00, 0x7c00,
    0x7c00, 0x7e00, 0x7f00, 0x3f80, 0x3ffc, 0x1ff8, 0x07e0, 0x0000
};
static const char *const pd_title[3] = { "Tide", "Extras", "Toggles" };
static const char *const pd_klbl[8] = { "TIME", "MIX", "LNGT", "BAL", "WMOD", "CLK", "LMOD", "TONE" };
static const u8 pd_kdef[8] = PD_KDEF;
static const char *const pd_slbl[8] = { "WDTH", "FADE", "DRCT", "OUT", "WOBL", "DAMP", "XFAD", "RAMP" };
static const u8 pd_sdef[8] = PD_SDEF;
static const char *const pd_olbl[8] = { "WET", "LOOP", "ROUT", "SYNC", "SPRD", "LTCH", "DRYK", "NDUB" };
static const u8 pd_onum[8] = { 3, 3, 3, 3, 4, 2, 2, 2 };
static const u8 pd_odef[8] = PD_ODEF;
static const char *const o_wet[3] = { "REV", "DLY", "GRAB" };
static const char *const o_loop[3] = { "ENV", "TAPE", "STR" };
static const char *const o_rout[3] = { "IN", "ML+I", "ML" };
static const char *const o_sync[3] = { "OFF", "LOOP", "WET" };
static const char *const o_sprd[4] = { "OFF", "BOTH", "WET", "LOOP" };
static const char *const o_off[2] = { "OFF", "ON" };
static const char *const *const pd_oval[8] = { o_wet, o_loop, o_rout, o_sync, o_sprd, o_off, o_off, o_off };
static const char *const pd_cfg_lbl[2] = { "SMOOTH CLK", "LOOP BUFFER" };
static const u8 pd_cfg_num[2] = { 2, 2 }, pd_cfg_def[2] = { 0, 0 };
static const char *const c_buf[2] = { "FULL", "HALF" };
static const char *const *const pd_cfg_val[2] = { o_off, c_buf };

#include "pedal_ui.inc"

static const char *const tdivs[6] = { "1/32", "1/16", "1/8T", "1/8", "1/8D", "1/4" };
static const char *const ldivs[6] = { "X/32", "X/16", "X/8", "X/4", "X/2", "X1" };
static const u8 band_mid[6] = { 6, 25, 50, 74, 98, 119 };          /* a value inside each of CC 14's six bands */
static const u8 clk_mid[10] = { 2, 10, 22, 35, 47, 62, 77, 90, 103, 118 };   /* and CC 18's ten (64K is 48K) */
static const u8 clk_khz[10] = { 2, 3, 4, 6, 8, 12, 16, 24, 32, 48 };

static int ui_band(int v)                   /* CC 14's and CC 16's bands */
{
    return v < 13 ? 0 : v < 37 ? 1 : v < 62 ? 2 : v < 85 ? 3 : v < 111 ? 4 : 5;
}

static int clk_of(int v)                    /* CC 18's bands: the CLOCK step */
{
    static const u8 lo[9] = { 5, 16, 29, 41, 54, 70, 84, 96, 110 };
    int i = 0;
    while (i < 9 && v >= lo[i])
        i++;
    return i;
}

static void fx_defaults(void)               /* a new project: the wet channel on, the looper listening */
{
    ST.sw[SW_WET] = 1;
}
static void fx_opened(void) {}
static void fx_tick(void) {}

static u32 fx_sig(void)
{
    return (u32)(ST.flag[F_WET] | ST.flag[F_LOOP] << 2 | ST.flag[F_ENV] << 4 | ST.flag[F_CLK] << 5);
}

static void wet_on(int on)
{
    ST.sw[SW_WET] = (u8)on;
    if (!on)
        ST.sw[SW_FRZ] = 0;
}

static void loop_on(int on)
{
    ST.sw[SW_LOOP] = (u8)on;
    if (!on)
        ST.sw[SW_DUB] = 0;
}

static void fx_tkey(int t, int ev)
{
    int latch = ST.opt[O_LATCH];
    if (t == 0) {                               /* the wet channel: tap on/off, hold FREEZE */
        if (ev == TK_TAP) {
            wet_on(!ST.sw[SW_WET]);
        } else if (ev == TK_HOLD) {
            ST.sw[SW_WET] = 1;
            ST.sw[SW_FRZ] = latch ? !ST.sw[SW_FRZ] : 1;
        } else if (ev == TK_UP && !latch) {
            ST.sw[SW_FRZ] = 0;
        }
    } else if (t == 1) {                        /* the looper: tap on (the last LENGTH) or off, hold OVERDUB */
        if (ev == TK_TAP) {
            loop_on(!ST.sw[SW_LOOP]);
        } else if (ev == TK_HOLD) {
            ST.sw[SW_LOOP] = 1;
            ST.sw[SW_DUB] = latch ? !ST.sw[SW_DUB] : 1;
        } else if (ev == TK_UP && !latch) {
            ST.sw[SW_DUB] = 0;
        }
    } else if (t == 2) {                        /* the next wet mode; held, the next ROUTING */
        if (ev == TK_TAP) {
            set_opt(O_WET, ((OPT(O_WET) + 1) % 3));
            flash(2, O_WET);
        } else if (ev == TK_HOLD) {
            set_opt(O_ROUT, ((OPT(O_ROUT) + 1) % 3));
            flash(2, O_ROUT);
        }
    } else {                                    /* the next loop mode; held, the next SYNC */
        if (ev == TK_TAP) {
            set_opt(O_LOOP, ((OPT(O_LOOP) + 1) % 3));
            flash(2, O_LOOP);
        } else if (ev == TK_HOLD) {
            set_opt(O_SYNC, ((OPT(O_SYNC) + 1) % 3));
            flash(2, O_SYNC);
        }
    }
}

static void fx_trig(int n)
{
    if (n < 6) {                                /* TIME's divisions */
        set_ctl(0, K_TIME, band_mid[n]);
        flash(0, K_TIME);
    } else if (n < 12) {                        /* LENGTH's */
        set_ctl(0, K_LEN, band_mid[n - 6]);
        flash(0, K_LEN);
    } else {                                    /* CLOCK a step down or up */
        int c = clk_of(ST.knob[K_CLK]) + (n == 12 ? -1 : 1);
        set_ctl(0, K_CLK, clk_mid[c < 0 ? 0 : c > 9 ? 9 : c]);
        flash(0, K_CLK);
    }
}

static int fx_cc(int cc, int v)
{
    static const s8 knob_cc[7] = { K_TIME, K_MIX, K_LEN, K_WMOD, K_CLK, K_LMOD, -1 };     /* 14 .. 20 */
    static const s8 sec_cc[6] = { S_WIDTH, -1, S_FADE, -1, -1, S_DIRECT };                /* 24 .. 29 */
    int on = v > 0, three = v < 2 ? 0 : v == 2 ? 1 : 2;     /* the toggles' 0-1, 2, 3-127 */
    if (cc >= 14 && cc <= 19) {
        set_ctl(0, knob_cc[cc - 14], v);
    } else if (cc == 20) {
        set_ctl(1, S_RAMP, v);
    } else if (cc == 21) {
        set_ctl(2, O_WET, three);
    } else if (cc == 22) {
        set_ctl(2, O_ROUT, three);
    } else if (cc == 23) {
        set_ctl(2, O_LOOP, three);
    } else if (cc >= 24 && cc <= 29 && cc != 25 && sec_cc[cc - 24] >= 0) {
        set_ctl(1, sec_cc[cc - 24], v);
    } else if (cc == 27) {
        set_ctl(0, K_TONE, v);
    } else if (cc == 28) {
        set_ctl(0, K_BAL, v);
    } else if (cc == 31) {                      /* SYNC: left (the looper to the wet), none, right */
        set_ctl(2, O_SYNC, v < 2 ? SY_LOOP : v == 2 ? SY_OFF : SY_WET);
    } else if (cc == 32) {                      /* SPREAD's channels: the wet only, both, the looper only */
        set_ctl(2, O_SPRD, v < 2 ? SP_WET : v == 2 ? SP_BOTH : SP_LOOP);
    } else if (cc == 33) {
        ST.cfg[C_HALF] = (u8)(v < 2);
    } else if (cc == 72) {
        ST.mono = (u8)on;
    } else if (cc == 73) {
        set_ctl(2, O_SPRD, on ? (OPT(O_SPRD) ? OPT(O_SPRD) : SP_BOTH) : SP_OFF);
    } else if (cc == 74) {
        set_ctl(2, O_DRYK, on);
    } else if (cc == 75) {
        ST.trails = (u8)on;
    } else if (cc == 76) {
        set_ctl(2, O_LATCH, on);
    } else if (cc == 77) {
        set_ctl(2, O_NDUB, on);
    } else if (cc == 78) {
        ST.cfg[C_SMOOTH] = (u8)on;
    } else if (cc == 102) {
        loop_on(on);
    } else if (cc == 103) {
        wet_on(on);
    } else if (cc == 104) {
        ui_pg = (u8)(on ? 1 : 0);
    } else if (cc == 105) {
        if (on)
            ST.sw[SW_WET] = 1;
        ST.sw[SW_FRZ] = (u8)on;
    } else if (cc == 106) {
        if (on)
            ST.sw[SW_LOOP] = 1;
        ST.sw[SW_DUB] = (u8)on;
    } else if (cc == 93 || cc == 107) {
        if (v)
            tap();
    } else {
        return 0;
    }
    return 1;
}

static int fx_value(void *bmp, const void *font, int x, int y, int pg, int i)
{
    int v = pg == 0 ? ctl(0, i) : ctl(1, i), synced = dtide_tempo() != 0;
    if (pg == 0 && i == K_TIME) {
        if (ST.opt[O_WET] == WET_REV)
            TEXTF(bmp, font, x, y, -1, "SIZE %d%%", v * 100 / 127);
        else if (ST.opt[O_SYNC] == SY_WET)
            TEXTF(bmp, font, x, y, -1, "LOOP/%d", "\x10\x08\x06\x04\x03\x02"[ui_band(v)]);
        else if (synced)
            TEXTF(bmp, font, x, y, -1, "%s", tdivs[ui_band(v)]);
        else
            TEXTF(bmp, font, x, y, -1, "%dMS", ST.val[V_TIME] / 48);
    } else if (pg == 0 && i == K_LEN) {
        if (ST.opt[O_LOOP] != LOOP_TAPE && ST.sw[SW_LOOP])
            TEXTF(bmp, font, x, y, -1, "SLICE %dMS", ST.val[V_GRAIN] / 48);
        else if (synced && ST.opt[O_SYNC] != SY_LOOP)
            TEXTF(bmp, font, x, y, -1, "%s", ldivs[ui_band(v)]);
        else
            TEXTF(bmp, font, x, y, -1, "%d.%dS", ST.val[V_LEN] / 48000, ST.val[V_LEN] / 4800 % 10);
    } else if (pg == 0 && (i == K_MIX || i == K_TONE)) {
        if (i == K_TONE && v >= 125)
            TEXTF(bmp, font, x, y, -1, "OPEN");
        else
            TEXTF(bmp, font, x, y, -1, "%d%%", v * 100 / 127);
    } else if (pg == 0 && i == K_BAL) {
        if (v == 64)
            TEXTF(bmp, font, x, y, -1, "EVEN");
        else
            TEXTF(bmp, font, x, y, -1, "%s %d", v < 64 ? "WET" : "LOOP", v < 64 ? 64 - v : v - 64);
    } else if (pg == 0 && i == K_WMOD) {
        if (ST.opt[O_WET] == WET_REV) {
            TEXTF(bmp, font, x, y, -1, "SMEAR %d%%", v * 100 / 127);
        } else if (ST.opt[O_WET] == WET_DLY) {
            TEXTF(bmp, font, x, y, -1, "FB %d%%", v * 100 / 127);
        } else {
            int st = v >= 64 ? (v - 64) * 12 / 63 : (63 - v) * 12 / 63;
            TEXTF(bmp, font, x, y, -1, "%s+%dST", v >= 64 ? "FWD " : "REV ", st);
        }
    } else if (pg == 0 && i == K_CLK) {
        if (ST.cfg[C_SMOOTH])
            TEXTF(bmp, font, x, y, -1, "%dHZ", ST.val[V_RATE]);
        else
            TEXTF(bmp, font, x, y, -1, "%dK", clk_khz[ST.flag[F_CLK] % 10]);
    } else if (pg == 0 && i == K_LMOD) {
        int lm = ST.opt[O_LOOP];
        if (lm == LOOP_ENV) {
            TEXTF(bmp, font, x, y, -1, v ? "SENS %d%%" : "SENS OFF", v * 100 / 127);
        } else if (lm == LOOP_TAPE) {
            static const char *const sp[8] = { "-4X", "-2X", "-1X", "-0.5X", "0.5X", "1X", "2X", "4X" };
            int b = v < 5 ? 0 : v < 24 ? 1 : v < 43 ? 2 : v < 62 ? 3 : v < 85 ? 4 : v < 98 ? 5 : v < 120 ? 6 : 7;
            TEXTF(bmp, font, x, y, -1, "%s", sp[b]);
        } else {
            static const char *const sp[9] = { "REV", "REV 1.5X", "REV 2X", "REV 4X", "STALLED", "4X", "2X", "1.5X",
                                               "NONE" };
            int b = v < 16 ? 0 : v < 31 ? 1 : v < 47 ? 2 : v < 61 ? 3 : v < 81 ? 4 : v < 97 ? 5 : v < 112 ? 6 : v < 127 ? 7 : 8;
            TEXTF(bmp, font, x, y, -1, "%s", sp[b]);
        }
    } else if (pg == 1 && i == S_FADE) {
        if (v == 127)
            TEXTF(bmp, font, x, y, -1, "NONE");
        else
            TEXTF(bmp, font, x, y, -1, "%d%%", v * 100 / 127);
    } else if (pg == 1 && i == S_RAMP && v == 0) {
        TEXTF(bmp, font, x, y, -1, "OFF");
    } else if (pg == 1 && i == S_WIDTH) {
        TEXTF(bmp, font, x, y, -1, "%d%%", v * 200 / 128);
    } else {
        return 0;
    }
    return 1;
}

static void fx_left(void *bmp)                  /* the channels: wet (W, F frozen) and looper (L, O overdub, . listening)
                                                   over their modes' letters; * while ENV holds a slice */
{
    static const char *const wl[3] = { "R", "D", "S" }, *const ll[3] = { "E", "T", "S" };
    int f = ST.flag[F_WET], l = ST.flag[F_LOOP];
    small_text(bmp, 4, 45, f == 2 ? "F" : f ? "W" : "-");
    small_text(bmp, 10, 45, l == 2 ? "O" : l ? "L" : ".");
    small_text(bmp, 4, 38, wl[ST.opt[O_WET] % 3]);
    small_text(bmp, 10, 38, ll[ST.opt[O_LOOP] % 3]);
    if (ST.flag[F_ENV])
        small_text(bmp, 4, 31, "*");
}

static const char *fx_status(void)
{
    if (ST.flag[F_WET] == 2)
        return ST.flag[F_LOOP] == 2 ? "Freeze, overdub" : "Freeze";
    if (ST.flag[F_LOOP] == 2)
        return ST.opt[O_NDUB] ? "Overdub (no dub)" : "Overdub";
    return 0;
}

static int fx_off(int pg, int i)
{
    if (pg == 0 && (i == K_TIME || i == K_WMOD))
        return !ST.sw[SW_WET];
    if (pg == 0 && i == K_LMOD)
        return !ST.sw[SW_LOOP];
    if (pg == 1 && i == S_DIRECT)
        return ST.opt[O_ROUT] == R_IN;
    if (pg == 1 && i == S_FADE)
        return ST.opt[O_NDUB];
    if (pg == 1 && i == S_WIDTH)
        return ST.opt[O_SPRD] != SP_BOTH && ST.opt[O_SPRD] != SP_WET;
    if (pg == 2 && i == O_ROUT)
        return !(ST.sw[SW_WET] && ST.sw[SW_LOOP]);
    return 0;
}
