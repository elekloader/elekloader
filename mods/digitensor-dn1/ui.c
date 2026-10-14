/* SPDX-License-Identifier: GPL-2.0-or-later */
/* DigiTensor's UI, on the pedal shell (pedal_ui.inc: the pages, SETUP, presets, TAP, BYPASS, the owner):
 *
 *   1 TENSOR    SPEED PITCH TIME BLEND / RANDOM GLIDE TONE SLICE
 *   2 SECONDARY GRAIN XFADE R-SPD R-PCH / R-REV R-STU WIDTH OUT  (the random events' weights)
 *   3 MODES     MODE (TAPE, STRETCH) DIR (FWD, REV, ALT) HOLD (MOMENTARY, LATCH) CAPTURE (LAST, NEXT) /
 *               QUANT (HOLD on the beat) PATTERN (SLICE's) RANDOM (each WINDOW or each BEAT)
 *   T1 HOLD: loop the window (held, or latched with HOLD: LTCH)
 *   T2 REVERSE: tap to flip the direction; hold for a momentary flip
 *   T3 TAPE STOP: slows the tape to a stop while held (in STRETCH, a freeze)
 *   T4 RANDOM: tap to turn the random events on or off; hold for all of them, every point
 *   trig 1-10  TIME's divisions, 1/32 .. 2 bars;  trig 11-14  SPEED -1x, 0.5x, 1x, 2x
 *   MIDI: SPEED 20, TIME 21, PITCH 22, BLEND 23, RANDOM 24 (as the Tensor's), TONE 25, SLICE 26, MODE 27, CAPTURE
 *   28 (0-42 NEXT, as the Tensor's hold mode), DIR 29, PATTERN 30, GLIDE 31, HOLD 85, REVERSE 86, TAPE STOP 87,
 *   RANDOM on 88, TAP 93, the pedal on 102 (below 64 bypassed) */
#pragma GCC optimize ("no-tree-loop-distribute-patterns")
#include "dtensor.h"
#include "tables.h"

#define PD_TAG      0x4454454Eu             /* "DTEN" */
#define PD_NAME     "DIGITENSOR"
#define PD_SAVE_VER 1
#define PD_KBIP     ((1 << K_SPEED) | (1 << K_PITCH) | (1 << K_TONE))
#define PD_SBIP     0
#define PD_NCFG     0

/* The Mod Menu's DIGITENSOR: a tape reel and its arrow back */
static const u16 pd_icon[16] = {
    0x0000, 0x0ff0, 0x1818, 0x33cc, 0x6666, 0x6db6, 0x6db6, 0x6666,
    0x33cc, 0x1818, 0x0ff0, 0x0000, 0x2000, 0x7ffe, 0x2000, 0x0000
};
static const char *const pd_title[3] = { "Tensor", "Secondary", "Modes" };
static const char *const pd_klbl[8] = { "SPED", "PTCH", "TIME", "BLND", "RAND", "GLID", "TONE", "SLIC" };
static const u8 pd_kdef[8] = PD_KDEF;
static const char *const pd_slbl[8] = { "GRAN", "XFAD", "RSPD", "RPCH", "RREV", "RSTU", "WIDE", "OUT" };
static const u8 pd_sdef[8] = PD_SDEF;
static const char *const pd_olbl[8] = { "MODE", "DIR", "HOLD", "CAPT", "QNT", "PATN", "RAND", "" };
static const u8 pd_onum[8] = { 2, 3, 2, 2, 2, 8, 2, 0 };
static const u8 pd_odef[8] = PD_ODEF;
static const char *const o_mode[2] = { "TAPE", "STRT" };
static const char *const o_dir[3] = { "FWD", "REV", "ALT" };
static const char *const o_hold[2] = { "MOM", "LTCH" };
static const char *const o_capt[2] = { "LAST", "NEXT" };
static const char *const o_qnt[2] = { "OFF", "BEAT" };
static const char *const o_patn[8] = { "P1", "P2", "P3", "P4", "P5", "P6", "P7", "P8" };
static const char *const o_rand[2] = { "WIN", "BEAT" };
static const char *const *const pd_oval[8] = { o_mode, o_dir, o_hold, o_capt, o_qnt, o_patn, o_rand, 0 };
static const char *const pd_cfg_lbl[1] = { "" };
static const u8 pd_cfg_num[1] = { 1 }, pd_cfg_def[1] = { 0 };
static const char *const pd_cfg_none[1] = { "" };
static const char *const *const pd_cfg_val[1] = { pd_cfg_none };

#include "pedal_ui.inc"

static const char *const divs[10] = { "1/32", "1/16T", "1/16", "1/8T", "1/8", "1/8D", "1/4", "1/2", "1 BAR", "2 BAR" };

static void fx_defaults(void) {}
static void fx_opened(void) {}
static void fx_tick(void) {}

static u32 fx_sig(void)
{
    return (u32)(ST.flag[F_HOLD] | ST.flag[F_STOP] << 3 | ST.flag[F_EVENT] << 4 | ST.flag[F_REV] << 7);
}

static void hold_key(int on)
{
    ST.sw[SW_HOLD] = (u8)on;
    send_cmd(on ? CMD_HOLD_ON : CMD_HOLD_OFF);
}

static void fx_tkey(int t, int ev)
{
    if (t == SW_HOLD) {
        if (ST.opt[O_HOLD]) {                   /* LATCH */
            if (ev == TK_PRESS)
                hold_key(!ST.sw[SW_HOLD]);
        } else if (ev == TK_PRESS) {
            hold_key(1);
        } else if (ev == TK_TAP || ev == TK_UP) {
            hold_key(0);
        }
    } else if (t == SW_REV) {
        if (ev == TK_TAP || ev == TK_HOLD || ev == TK_UP)
            ST.sw[SW_REV] = !ST.sw[SW_REV];
    } else if (t == SW_STOP) {
        if (ev == TK_PRESS) {
            ST.sw[SW_STOP] = 1;
            send_cmd(CMD_STOP_ON);
        } else if (ev == TK_TAP || ev == TK_UP) {
            ST.sw[SW_STOP] = 0;
            send_cmd(CMD_STOP_OFF);
        }
    } else if (ev == TK_TAP) {                  /* RANDOM */
        ST.sw[SW_RAND] = !ST.sw[SW_RAND];
    }
}

static void fx_trig(int n)
{
    static const u8 speeds[4] = { 32, 80, 96, 127 };     /* -1x 0.5x 1x 2x */
    if (n < 10) {
        set_ctl(0, K_TIME, (n * 128 + 64) / 10);
        flash(0, K_TIME);
    } else {
        set_ctl(0, K_SPEED, speeds[n - 10]);
        flash(0, K_SPEED);
    }
}

static int fx_cc(int cc, int v)
{
    static const s8 knob_cc[12] = { K_SPEED, K_TIME, K_PITCH, K_BLEND, K_RANDOM, K_TONE, K_SLICE, -1, -1, -1, -1,
                                    K_GLIDE };          /* 20 .. 31 */
    int on = v >= 64;
    if (cc >= 20 && cc <= 31 && knob_cc[cc - 20] >= 0) {
        set_ctl(0, knob_cc[cc - 20], v);
    } else if (cc == 27) {
        cc_opt(O_MODE, v);
    } else if (cc == 28) {
        set_ctl(2, O_CAPT, v < 43 ? CAPT_NEXT : CAPT_LAST);
    } else if (cc == 29) {
        cc_opt(O_DIR, v);
    } else if (cc == 30) {
        cc_opt(O_PATN, v);
    } else if (cc == 85) {
        if (on != ST.sw[SW_HOLD])
            hold_key(on);
    } else if (cc == 86) {
        ST.sw[SW_REV] = (u8)on;
    } else if (cc == 87) {
        ST.sw[SW_STOP] = (u8)on;
        send_cmd(on ? CMD_STOP_ON : CMD_STOP_OFF);
    } else if (cc == 88) {
        ST.sw[SW_RAND] = (u8)on;
    } else if (cc == 93) {
        tap();
    } else if (cc == 102) {
        bypass_set(!on);
    } else {
        return 0;
    }
    return 1;
}

static int fx_value(void *bmp, const void *font, int x, int y, int pg, int i)
{
    int v;
    if (pg != 0)
        return 0;
    v = ST.knob[i];
    if (i == K_SPEED) {
        int c = (v - 64) * 100 / 32, a = c < 0 ? -c : c;
        TEXTF(bmp, font, x, y, -1, "%s%d.%d%dX", c < 0 ? "-" : "", a / 100, a / 10 % 10, a % 10);
    } else if (i == K_PITCH) {
        int st = ((v - 64) * 24 + (v >= 64 ? 32 : -32)) / 64;
        TEXTF(bmp, font, x, y, -1, "%c%dST", st >= 0 ? '+' : '-', st >= 0 ? st : -st);
    } else if (i == K_TIME) {
        if (dtensor_tempo()) {
            int d = v * 10 / 128;
            TEXTF(bmp, font, x, y, -1, "%s", divs[d > 9 ? 9 : d]);
        } else {
            TEXTF(bmp, font, x, y, -1, "%dMS", dt_time[v] / 48);
        }
    } else if (i == K_BLEND) {
        TEXTF(bmp, font, x, y, -1, "%d%%", v * 100 / 127);
    } else {
        return 0;
    }
    return 1;
}

static void fx_left(void *bmp)                  /* HOLD's state over the direction; a random event's letter */
{
    int h = ST.flag[F_HOLD], k;
    if (h == H_LOOP) {                          /* a loop: a play arrow */
        for (k = -3; k <= 3; k++)
            FILLRECT(bmp, 5, 44 + k, 5 + 3 - (k < 0 ? -k : k), 44 + k, 1);
    } else if (h == H_CAPTURE || h == H_ARMED) {    /* taking it in: a dot (hollow while it waits) */
        FILLRECT(bmp, 4, 42, 8, 46, 1);
        if (h == H_ARMED)
            FILLRECT(bmp, 5, 43, 7, 45, 0);
    } else if (ST.flag[F_STOP]) {               /* the tape stopped: a square */
        FILLRECT(bmp, 4, 42, 8, 46, 1);
        FILLRECT(bmp, 5, 43, 7, 45, 1);
    } else {                                    /* the direction */
        int rv = ST.flag[F_REV];
        for (k = -3; k <= 3; k++) {
            int a = 3 - (k < 0 ? -k : k);
            if (rv)
                FILLRECT(bmp, 9 - a, 44 + k, 9, 44 + k, 1);
            else
                FILLRECT(bmp, 5, 44 + k, 5 + a, 44 + k, 1);
        }
    }
    if (ST.sw[SW_RAND] || ST.held[SW_RAND]) {
        static const char *const evl[5] = { "R", "S", "P", "V", "U" };
        small_text(bmp, 4, 36, evl[ST.flag[F_EVENT] % 5]);
    }
}

static const char *fx_status(void)
{
    int h = ST.flag[F_HOLD];
    if (h == H_ARMED)
        return "Hold: on the beat";
    if (h == H_CAPTURE)
        return "Hold: taking it in";
    if (h == H_LOOP && ST.sw[SW_HOLD])
        return "Hold";
    if (ST.sw[SW_STOP])
        return ST.opt[O_MODE] == MODE_STRETCH ? "Freeze" : "Tape stop";
    return 0;
}

static int fx_off(int pg, int i)
{
    if (pg == 0 && i == K_PITCH)
        return ST.opt[O_MODE] == MODE_TAPE;     /* TAPE: the pitch is the speed's */
    if (pg == 0 && i == K_RANDOM)
        return !ST.sw[SW_RAND];
    if (pg == 1 && i == S_GRAIN)
        return ST.opt[O_MODE] == MODE_TAPE;
    return 0;
}
