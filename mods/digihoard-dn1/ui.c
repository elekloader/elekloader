/* SPDX-License-Identifier: GPL-2.0-or-later */
/* DigiHoard's UI, on the pedal shell (pedal_ui.inc: the pages, SETUP, presets, TAP, BYPASS, the owner):
 *
 *   1 HOARD     VOLUME REPEATS SIZE MOD / SPREAD SCAN RAMP MIX
 *   2 SECONDARY WIDTH KEEP (COLLECT's) M-DRY (the memory dry's level) OUT / TONE GLIDE STAY (AUTO's) INPUT
 *   3 SWITCHES  BANK (A, OFF, B) NUMBER (1-3) MODE (IN, OUT, FEED) SCAN (AUTO, MANUAL) /
 *               MEMORY (NORM, COLLECT, ALWAYS, both) LOOP (MOMENTARY, LATCH) RAMP (the knob it moves) SHAPE
 *   SETUP adds MEM DRY (SCANNING, OFF, ON) and WIPE (OFF, ON)
 *   T1 the left footswitch: tap the tempo; hold for SCAN at its most (momentary)
 *   T2 the right footswitch: tap for bypass; hold to LOOP (momentary, or latched: a hold toggles it)
 *   T3 both footswitches: CLEAR the memory
 *   T4 tap: the modifiers off or on; hold: WIPE (the memory alone) while held
 *   trig 1-3  A1-A3, trig 4-6 B1-B3, trig 7 the modifiers off; trig 8-10 IN, OUT, FEED; trig 11 SCAN AUTO or
 *   MANUAL; trig 12 COLLECT; trig 13 ALWAYS; trig 14 LATCH
 *   MIDI: its chart: VOLUME 14, REPEATS 15, SIZE 16, MOD 17, SPREAD 18, SCAN 19, RAMP 20, MOD NUMBER 21,
 *   MOD BANK 22, MODE 23 (1, 2, 3), LOOP 24, SCAN (left hold) 25, CLEAR 26, MIDI CLOCK IGNORE 51, TAP 93,
 *   EXPRESSION 100, BYPASS 102; and MIX 27, SCAN's mode 28, COLLECT 29 */
#pragma GCC optimize ("no-tree-loop-distribute-patterns")
#include "dhoard.h"
#include "tables.h"

#define PD_TAG      0x44484142u             /* "DHAB" */
#define PD_NAME     "DIGIHOARD"
#define PD_SAVE_VER 1
#define PD_KBIP     (1 << K_MOD)
#define PD_SBIP     0
#define PD_NCFG     2

/* The Mod Menu's DIGIHOARD: a reel of tape and the echoes trailing from it */
static const u16 pd_icon[16] = {
    0x0000, 0x0f00, 0x30c0, 0x4020, 0x4920, 0x8010, 0x9690, 0x8010,
    0x4920, 0x4020, 0x30c0, 0x0f00, 0x0000, 0xfdb5, 0x0000, 0x0000
};
static const char *const pd_title[3] = { "Hoard", "Secondary", "Switches" };
static const char *const pd_klbl[8] = { "VOL", "RPTS", "SIZE", "MOD", "SPRD", "SCAN", "RAMP", "MIX" };
static const u8 pd_kdef[8] = PD_KDEF;
static const char *const pd_slbl[8] = { "WIDE", "KEEP", "MDRY", "OUT", "TONE", "GLID", "STAY", "INPT" };
static const u8 pd_sdef[8] = PD_SDEF;
static const char *const pd_olbl[8] = { "BANK", "NUM", "MODE", "SCAN", "MEM", "LOOP", "RAMP", "SHAP" };
static const u8 pd_onum[8] = { 3, 3, 3, 2, 4, 2, 6, 6 };
static const u8 pd_odef[8] = PD_ODEF;
static const char *const o_bank[3] = { "A", "OFF", "B" };
static const char *const o_num[3] = { "1", "2", "3" };
static const char *const o_mode[3] = { "IN", "OUT", "FEED" };
static const char *const o_scan[2] = { "AUTO", "MAN" };
static const char *const o_mem[4] = { "NORM", "CLCT", "ALWS", "C+A" };
static const char *const o_loop[2] = { "MOM", "LTCH" };
static const char *const o_ramp[6] = { "OFF", "RPTS", "SIZE", "MOD", "SPRD", "SCAN" };
static const char *const o_shape[6] = { "UP", "DOWN", "BNC+", "BNC-", "RAND", "SQR" };
static const char *const *const pd_oval[8] = { o_bank, o_num, o_mode, o_scan, o_mem, o_loop, o_ramp, o_shape };
static const char *const pd_cfg_lbl[2] = { "MEM DRY", "WIPE" };
static const u8 pd_cfg_num[2] = { 3, 2 }, pd_cfg_def[2] = { MD_SCAN, 0 };
static const char *const c_mdry[3] = { "SCANNING", "OFF", "ON" };
static const char *const c_wipe[2] = { "OFF", "ON" };
static const char *const *const pd_cfg_val[2] = { c_mdry, c_wipe };

#include "pedal_ui.inc"

static const char *const divs[13] = { "1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4T", "1/8D", "1/4", "1/2T",
                                      "1/4D", "1/2", "1/2D", "1 BAR" };
static const s16 a1_x10[10] = { -40, -20, -10, -5, 5, 10, 15, 20, 30, 40 };     /* STEPPED SPEED, x10 */
static u8 last_bank = BANK_A;

static void fx_defaults(void) {}

static void fx_opened(void)
{
    send_cmd(CMD_RAMP);                     /* opening steps it on: a ramp starts */
}

static void fx_tick(void)
{
    if (ST.opt[O_BANK] != BANK_OFF)
        last_bank = ST.opt[O_BANK];
}

static u32 fx_sig(void)
{
    return (u32)(ST.flag[F_STATE] | ST.flag[F_MOD] << 5) + (ST.val[V_FILL] >> 16) * 977 + (ST.val[V_DP] >> 12) * 31
           + (ST.val[V_DS] >> 12) * 7 + ST.val[V_SIZE];
}

static void loop_set(int on)
{
    ST.sw[SW_LOOP] = (u8)on;
}

static void fx_tkey(int t, int ev)
{
    if (t == 0) {                           /* the left footswitch: TAP; held, SCAN at its most */
        if (ev == TK_TAP)
            tap();
        else if (ev == TK_HOLD)
            ST.sw[SW_SCAN] = 1;
        else if (ev == TK_UP)
            ST.sw[SW_SCAN] = 0;
    } else if (t == 1) {                    /* the right footswitch: BYPASS; held, LOOP */
        if (ev == TK_TAP)
            bypass_set(!ST.bypass);
        else if (ev == TK_HOLD)
            loop_set(ST.opt[O_LOOP] == LOOP_LTCH ? !ST.sw[SW_LOOP] : 1);
        else if (ev == TK_UP && ST.opt[O_LOOP] == LOOP_MOM)
            loop_set(0);
    } else if (t == 2) {                    /* both: CLEAR */
        if (ev == TK_PRESS) {
            ST.sw[SW_CLEAR] = 1;
            send_cmd(CMD_CLEAR);
        } else if (ev == TK_TAP || ev == TK_UP) {
            ST.sw[SW_CLEAR] = 0;
        }
    } else {                                /* the modifiers off and on; held, WIPE */
        if (ev == TK_TAP) {
            ST.opt[O_BANK] = ST.opt[O_BANK] == BANK_OFF ? last_bank : BANK_OFF;
            flash(2, O_BANK);
        } else if (ev == TK_HOLD) {
            ST.sw[SW_WIPE] = 1;
        } else if (ev == TK_UP) {
            ST.sw[SW_WIPE] = 0;
        }
    }
}

static void fx_trig(int n)
{
    if (n < 6) {                            /* A1-A3, B1-B3 */
        ST.opt[O_BANK] = n < 3 ? BANK_A : BANK_B;
        ST.opt[O_NUM] = (u8)(n % 3);
        flash(2, O_BANK);
    } else if (n == 6) {
        ST.opt[O_BANK] = BANK_OFF;
        flash(2, O_BANK);
    } else if (n < 10) {
        ST.opt[O_MODE] = (u8)(n - 7);
        flash(2, O_MODE);
    } else if (n == 10) {
        ST.opt[O_SCAN] = !ST.opt[O_SCAN];
        flash(2, O_SCAN);
    } else if (n == 11) {
        ST.opt[O_MEM] ^= 1;                 /* COLLECT */
        flash(2, O_MEM);
    } else if (n == 12) {
        ST.opt[O_MEM] ^= 2;                 /* ALWAYS */
        flash(2, O_MEM);
    } else {
        ST.opt[O_LOOP] = !ST.opt[O_LOOP];
        flash(2, O_LOOP);
    }
}

static int three(int v)                     /* the positions 1, 2, 3 (0 as 1); a sweep in thirds */
{
    if (v <= 3)
        return v <= 1 ? 0 : v - 1;
    return v * 3 / 128;
}

static int fx_cc(int cc, int v)
{
    if (cc >= 14 && cc <= 20) {             /* VOLUME REPEATS SIZE MOD SPREAD SCAN RAMP */
        set_ctl(0, cc - 14, v);
    } else if (cc == 21) {
        set_ctl(2, O_NUM, three(v));
    } else if (cc == 22) {
        set_ctl(2, O_BANK, three(v));
    } else if (cc == 23) {
        set_ctl(2, O_MODE, three(v));
    } else if (cc == 24) {
        loop_set(v != 0);
    } else if (cc == 25) {
        ST.sw[SW_SCAN] = (u8)(v != 0);
    } else if (cc == 26) {
        send_cmd(CMD_CLEAR);
    } else if (cc == 51) {
        ST.clock = v ? CL_TEMPO : CL_FREE;
    } else if (cc == 93) {
        tap();
    } else if (cc == 100) {
        send_cmd(CMD_EXP + v);
    } else if (cc == 102) {
        bypass_set(v == 0);
    } else if (cc == 27) {
        set_ctl(0, K_MIX, v);
    } else if (cc == 28) {
        set_ctl(2, O_SCAN, v >= 64);
    } else if (cc == 29) {
        set_ctl(2, O_MEM, (ST.opt[O_MEM] & 2) | (v >= 64));
    } else {
        return 0;
    }
    return 1;
}

static void secs(void *bmp, const void *font, int x, int y, u32 fr)    /* frames as seconds */
{
    u32 t = fr / 4800;                      /* tenths */
    if (t >= 600)
        TEXTF(bmp, font, x, y, -1, "%d:%d%d", t / 600, t / 100 % 6, t / 10 % 10);
    else
        TEXTF(bmp, font, x, y, -1, "%d.%dS", t / 10, t % 10);
}

static int fx_value(void *bmp, const void *font, int x, int y, int pg, int i)
{
    int v;
    if (pg != 0)
        return 0;
    v = ST.knob[i];
    if (i == K_SIZE) {
        if (dhoard_tempo()) {
            int d = v * 13 / 128;
            TEXTF(bmp, font, x, y, -1, "%s", divs[d > 12 ? 12 : d]);
        } else {
            TEXTF(bmp, font, x, y, -1, "%dMS", ST.val[V_SIZE] / 48);    /* the render's SIZE now */
        }
    } else if (i == K_MOD) {
        int id = ST.opt[O_BANK] == BANK_OFF ? 0 : 1 + (ST.opt[O_BANK] == BANK_B ? 3 : 0) + ST.opt[O_NUM] % 3;
        if (!id) {
            TEXTF(bmp, font, x, y, -1, "OFF");
        } else if (id == M_SSPD) {
            int s = a1_x10[v * 10 / 128], a = s < 0 ? -s : s;
            TEXTF(bmp, font, x, y, -1, "%s%d.%dX", s < 0 ? "-" : "", a / 10, a % 10);
        } else {
            static const char *const nm[7] = { "", "", "STAB", "TRIM", "SPD", "FLT", "DROP" };
            TEXTF(bmp, font, x, y, -1, "%s %c%d", nm[id], v >= 64 ? '+' : '-', v >= 64 ? v - 64 : 64 - v);
        }
    } else if (i == K_SPRD) {
        if (!v || !ST.val[V_DS])
            TEXTF(bmp, font, x, y, -1, "OFF");
        else                                /* where the secondary head is: behind the primary */
            secs(bmp, font, x, y, ST.val[V_DS] - ST.val[V_DP]);
    } else if (i == K_SCAN) {
        if (ST.opt[O_SCAN] == SCAN_MAN)
            secs(bmp, font, x, y, ST.val[V_DP]);    /* where the head is */
        else if (!v)
            TEXTF(bmp, font, x, y, -1, "OFF");
        else
            TEXTF(bmp, font, x, y, -1, "AUTO %d", v);
    } else if (i == K_RAMP && ST.opt[O_RAMP] == RT_OFF) {
        TEXTF(bmp, font, x, y, -1, "OFF");
    } else {
        return 0;
    }
    return 1;
}

/* The left column: the memory's fill (0-3 minutes) as a bar, the heads' places in it (primary, secondary), the
 * modifier's bank and number */
static void fx_left(void *bmp)
{
    u32 f = ST.val[V_FILL], dp = ST.val[V_DP], ds = ST.val[V_DS];
    int h = (int)(f * 20u / HB_MFR), y;
    FRAMERECT(bmp, 1, 28, 4, 49, 1);
    if (h > 0)
        FILLRECT(bmp, 2, 29, 3, 28 + h, 1);
    y = 29 + (int)(dp * 20u / HB_MFR);
    FILLRECT(bmp, 5, y, 8, y, 1);           /* where the primary head plays: from the bottom, now */
    if (ds) {
        y = 29 + (int)(ds * 20u / HB_MFR);
        FILLRECT(bmp, 6, y, 8, y, 1);
        px(bmp, 5, y, 0);
    }
    if (ST.flag[F_MOD]) {
        int id = ST.flag[F_MOD];
        small_text(bmp, 11, 46, id > 3 ? "B" : "A");
        small_text(bmp, 11, 39, o_num[(id - 1) % 3]);
    }
}

static const char *fx_status(void)
{
    int f = ST.flag[F_STATE];
    if (f & FS_CLEARED)
        return "Memory cleared";
    if (ST.sw[SW_SCAN])
        return "Scan: all the way";
    if (f & FS_LOOP)
        return ST.opt[O_LOOP] == LOOP_LTCH ? "Loop (latched)" : "Loop";
    if (f & FS_WIPE)
        return "Wipe: the memory";
    if (ST.bypass && ST.opt[O_MEM] >= MEM_ALWS)
        return "Bypassed: recording";
    if (ST.bypass && ST.opt[O_MEM] == MEM_CLCT)
        return "Bypassed: kept";
    return 0;
}

static int fx_off(int pg, int i)
{
    if (pg == 0 && i == K_MOD)
        return ST.opt[O_BANK] == BANK_OFF;
    if (pg == 0 && i == K_RAMP)
        return ST.opt[O_RAMP] == RT_OFF;
    if (pg == 1 && i == S_KEEP)
        return !(ST.opt[O_MEM] & 1);
    if (pg == 1 && i == S_MDRY)
        return ST.cfg[C_MDRY] == MD_OFF && !ST.cfg[C_WIPE];
    if (pg == 1 && i == S_STAY)
        return ST.opt[O_SCAN] == SCAN_MAN;
    if (pg == 2 && i == O_NUM)
        return ST.opt[O_BANK] == BANK_OFF;
    if (pg == 2 && i == O_SHAPE)
        return ST.opt[O_RAMP] == RT_OFF;
    return 0;
}
