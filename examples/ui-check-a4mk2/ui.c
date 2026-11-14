/* SPDX-License-Identifier: GPL-2.0-or-later */
/* ui-check: core-a4mk2 0.7's UI and system events (std1), and core's own
 * ev_knob_turn, one mod built with "events": "std1". It changes nothing the
 * unit does, except while armed (UI TEST selected in SETTINGS, again to
 * disarm): then ev_knob_turn takes every turn of encoder B and of the LEVEL
 * encoder, ev_mute takes every mute, ev_pattern takes every queued pattern
 * change (pre 1, PATTERN_QUEUE), and ev_key_leds lights LED 0x20.
 *
 * Everything it sees is in ui_check (an exported symbol), for an emulator's
 * or a unit's memory read:
 *   magic        0x55494331 ("UIC1") once ev_boot_done ran
 *   boot_done ... knob   each event's calls (idle counts its calls / 256)
 *   usb_configured, usb_mode      the last ev_usb's argument, core_usb_mode()
 *   mode_*        the last ev_mode's (mode, old, new)
 *   set_*         the last ev_param_set's (track, id, value)
 *   clip_*        the last ev_clip's (pre, kind, op, track); clip_pre and
 *                 clip_post count the two calls
 *   pattern_*     the last ev_pattern's (pre, pattern, how); pattern_how_n[7]
 *                 the calls of each how (0 the change, 1.. the requests),
 *                 pattern_taken the requests taken
 *   view, view_*  ev_view's calls; view_push, view_pop each op's; the last
 *                 one's op and core_view_name (16 chars)
 *   frame         ev_analog_frame's calls / 256; frame_w0 its first word
 *   mute_*        the last ev_mute's (track, muted, scope); mute_taken
 *   knob_*        the last ev_knob_turn's (id, delta, knob, how); knob_taken
 *                 and knob_how_n[3], the turns of each how
 *   leds[24]      the frame the panel shows (0x4180eb4c) at the last tick
 *   led20_*       LED 0x20 read back at the last tick: its slot (-1 none),
 *                 the 2-bit palette index in the shown frame and in the
 *                 steady layer (0x4180eb64), the colour that index holds
 *                 (palette 0x448b5b19 + id * 4), and the LedManager's
 *                 requested mode and colour (mgr+0x2c + id*4, mgr+0x7d4 + id)
 *                 at the last ev_key_leds
 *   mode_n[4]     ev_mode's calls for each mode (MODE_*), mode_last_new[4]
 *                 the new value each last reported
 *   crash_head, crash_pc   the exception frame's first long (format, vector,
 *                 SR) and PC at the last ev_crash; crash_magic 0x43525348
 *                 ("CRSH") once a handler ran
 *   mute_n, set_n each call of ev_mute and ev_param_set, as mute and
 *                 param_set (kept apart so a plan can read them after the
 *                 older fields)
 *   faults        UI CRASH selects that raised the fault
 *   clip_slot     the last ev_clip's slot
 *   clip_seen[3]  per op (copy, paste, clear), a bit 1 << kind for each kind
 *                 whose post call came
 *
 * UI CRASH (a second SETTINGS row) raises an illegal instruction in the UI
 * task, but only while armed, so ev_crash runs on a real fault. From 0.4
 * ev_crash also draws its fields on screen rows 0-3 and holds them about
 * 10 s (see "the crash screen" below). The unit then stops in stock's crash
 * screen and needs a power cycle.
 */
#include <stdint.h>
#include <std1.h>
#include "analog-four-mk2/keys.h"
#include "analog-four-mk2/leds.h"
#include "analog-four-mk2/ui.h"

extern void core_additem(void *menu, const void *row);
extern void uc_label(void);
extern void uc_crash_label(void);

struct ui_check {
    uint32_t magic;
    uint32_t boot_done, idle, crash, usb, key_leds, mode, param_set, clip, pattern, mute, knob;
    uint32_t usb_configured, usb_mode;
    int32_t mode_mode, mode_old, mode_new;
    uint32_t set_track, set_id;
    int32_t set_value;
    uint32_t clip_pre, clip_post, clip_kind, clip_op, clip_track;
    uint32_t pattern_pre, pattern_now, pattern_how;
    uint32_t mute_track, mute_muted, mute_scope, mute_taken;
    int32_t knob_id, knob_delta, knob_knob, knob_how;
    uint32_t knob_taken, knob_how_n[3];
    uint32_t armed, selects, ticks;
    uint8_t leds[24];
    uint32_t pattern_how_n[7], pattern_taken;
    uint32_t view, view_push, view_pop, view_op;
    char view_name[16];
    uint32_t frame, frame_w0;
    int32_t led20_slot;
    uint32_t led20_shown, led20_steady, led20_colour, led20_req_mode, led20_req_colour;
    uint32_t mode_n[4];
    int32_t mode_last_new[4];
    uint32_t crash_magic, crash_head, crash_pc;
    uint32_t mute_n, set_n, faults;
    uint32_t clip_slot, clip_seen[3];
};
volatile struct ui_check ui_check;
static unsigned idle_sub;

void uc_boot_done(void)
{
    ui_check.magic = 0x55494331u;
    ui_check.boot_done++;
}

void uc_idle(void)
{
    if (++idle_sub == 256) {
        idle_sub = 0;
        ui_check.idle++;
    }
}

/* ---- the crash screen ------------------------------------------------------
 * After the fault the OS runs with interrupts masked, so nothing answers USB and
 * the power cycle loses RAM. So uc_crash also shows its fields on the screen,
 * the way stock's crash printer (0x4006380e) does: polled bytes to the front
 * panel's UART (0x4006377c: wait for bit 3 of 0xec070004, write 0xec07000c),
 * 0x51 + (col << 4 | row) the cursor (the column in the high nibble: unit T298;
 * stock only ever sends 0,0), 0x52 + n the attribute, 0x50 + ch one
 * character, 16 characters a row. Rows 0-3:
 *   UC CRASH Nnn Fff   crash and faults
 *   Mmmmmmmmm          crash_magic
 *   Hhhhhhhhh          crash_head (format, vector, SR)
 *   Ppppppppp          crash_pc
 * in upper-case hex, as stock's own screen prints. It then holds about 10 s,
 * redrawing each second, interrupts still masked, and returns, so core calls
 * stock's printer, which writes over rows 0-7 from 0,0 and halts.
 *
 * The panel link may be inside a message when the fault lands (one longest
 * message is 10 bytes: 0x1x, a column of pixels), and the panel would take
 * the cursor command as its data; so each draw starts with nine 0xb8 bytes
 * first: a one-byte message stock itself sends after each frame (about one
 * header in five of its panel traffic), so the panel takes each as a whole
 * message once the cut one is filled. No calls into stock. */
#define PANEL_USR (*(const volatile uint8_t *)0xec070004u)
#define PANEL_UTB (*(volatile uint8_t *)0xec07000cu)
#define PANEL_TXRDY 0x08u
#define PANEL_SPINS 200000u      /* most polls of TXRDY for one byte */
#define PANEL_SYNC 0xb8u         /* a one-byte message (see above) */

/* DTIM0's count runs free from the bus clock, also with interrupts masked:
 * stock's 64-bit clock (0x40105f84) adds its 32-bit unsigned differences, and
 * the tap tempo (0x40054220) scales it as 132 MHz. */
#define DTCN0 (*(const volatile uint32_t *)0xfc07000cu)
#define BUS_HZ 132000000u
#define HOLD_TICKS (10u * BUS_HZ)  /* 1.32e9 < 2^32 */
/* A cap, should DTCN0 not move: 2^29 passes. One pass is one I/O read and
 * about seven instructions, at least 8 cycles at 250 MHz, so 10 s take at most
 * 312M passes and the cap never ends a running hold early; with a stopped
 * counter it ends the hold after about 17-54 s (8-25 cycles a pass). */
#define HOLD_SPINS 0x20000000u

static void uc_tx(unsigned b)
{
    unsigned n;
    for (n = 0; n < PANEL_SPINS && !(PANEL_USR & PANEL_TXRDY); n++)
        ;
    PANEL_UTB = (uint8_t)b;
}

static unsigned uc_put(unsigned col, unsigned ch)
{
    if (col < 16) {
        uc_tx(0x50);
        uc_tx(ch);
    }
    return col + 1;
}

static unsigned uc_puts(unsigned col, const char *s)
{
    while (*s)
        col = uc_put(col, (unsigned char)*s++);
    return col;
}

static unsigned uc_puthex(unsigned col, uint32_t v, int digits)
{
    while (digits-- > 0) {
        unsigned d = (v >> (digits * 4)) & 15u;
        col = uc_put(col, d < 10 ? '0' + d : 'A' + d - 10);
    }
    return col;
}

static void uc_row_start(unsigned row, unsigned attr)
{
    uc_tx(0x51);
    uc_tx(row);                  /* (col << 4) | row, col 0 */
    uc_tx(0x52);
    uc_tx(attr);
}

static void uc_row_end(unsigned col)
{
    while (col < 16)
        col = uc_put(col, ' ');
}

static void uc_crash_draw(void)
{
    unsigned col, i;
    for (i = 0; i < 9; i++)
        uc_tx(PANEL_SYNC);
    uc_row_start(0, 1);
    col = uc_puts(0, "UC CRASH N");
    col = uc_puthex(col, ui_check.crash, 2);
    col = uc_puts(col, " F");
    col = uc_puthex(col, ui_check.faults, 2);
    uc_row_end(col);
    uc_row_start(1, 0);
    uc_row_end(uc_puthex(uc_put(0, 'M'), ui_check.crash_magic, 8));
    uc_row_start(2, 0);
    uc_row_end(uc_puthex(uc_put(0, 'H'), ui_check.crash_head, 8));
    uc_row_start(3, 0);
    uc_row_end(uc_puthex(uc_put(0, 'P'), ui_check.crash_pc, 8));
}

static void uc_crash_hold(void)
{
    uint32_t prev = DTCN0, now, d, held = 0, since = 0, n;
    for (n = 0; n < HOLD_SPINS && held < HOLD_TICKS; n++) {
        now = DTCN0;
        d = now - prev;
        prev = now;
        held += d;
        since += d;
        if (since >= BUS_HZ) {
            since = 0;
            uc_crash_draw();
        }
    }
}

void uc_crash(const void *frame)
{
    const volatile uint32_t *f = (const volatile uint32_t *)frame;
    ui_check.crash++;
    if (f) {
        ui_check.crash_head = f[0];
        ui_check.crash_pc = f[1];
    }
    ui_check.crash_magic = 0x43525348u;
    uc_crash_draw();
    uc_crash_hold();
}

void uc_usb(unsigned configured)
{
    ui_check.usb++;
    ui_check.usb_configured = configured;
    ui_check.usb_mode = core_usb_mode();
}

void uc_key_leds(void *mgr)
{
    ui_check.key_leds++;
    if (ui_check.armed)
        core_led_set(mgr, 0x20, LED_ON);
    ui_check.led20_req_mode = *(const volatile uint32_t *)((const uint8_t *)mgr + 0x2c + 0x20 * 4);
    ui_check.led20_req_colour = *((const volatile uint8_t *)mgr + 0x7d4 + 0x20);
}

void uc_mode(int mode, int old, int new_)
{
    ui_check.mode++;
    ui_check.mode_mode = mode;
    ui_check.mode_old = old;
    ui_check.mode_new = new_;
    if (mode >= 0 && mode < 4) {
        ui_check.mode_n[mode]++;
        ui_check.mode_last_new[mode] = new_;
    }
}

void uc_param_set(unsigned track, unsigned id, int32_t value)
{
    ui_check.param_set++;
    ui_check.set_n++;
    ui_check.set_track = track;
    ui_check.set_id = id;
    ui_check.set_value = value;
}

int uc_clip(unsigned pre, const struct core_clip *c)
{
    ui_check.clip++;
    if (pre)
        ui_check.clip_pre++;
    else
        ui_check.clip_post++;
    ui_check.clip_kind = c->kind;
    ui_check.clip_op = c->op;
    ui_check.clip_track = c->track;
    ui_check.clip_slot = c->slot;
    if (!pre && c->op < 3)
        ui_check.clip_seen[c->op] |= 1u << c->kind;
    return 0;
}

int uc_pattern(unsigned pre, unsigned pattern, unsigned how)
{
    ui_check.pattern++;
    ui_check.pattern_pre = pre;
    ui_check.pattern_now = pattern;
    ui_check.pattern_how = how;
    if (how < 7)
        ui_check.pattern_how_n[how]++;
    if (ui_check.armed && pre && how == PATTERN_QUEUE) {
        ui_check.pattern_taken++;
        return 1;
    }
    return 0;
}

int uc_view(int op, void *ctrl, void *view, void *b)
{
    const char *name = core_view_name(view);
    int i;
    (void)ctrl;
    (void)b;
    ui_check.view++;
    if (op == VIEW_PUSH)
        ui_check.view_push++;
    else if (op == VIEW_POP)
        ui_check.view_pop++;
    ui_check.view_op = (uint32_t)op;
    for (i = 0; i < 15 && name && name[i]; i++)
        ui_check.view_name[i] = name[i];
    for (; i < 16; i++)
        ui_check.view_name[i] = 0;
    return 0;
}

static unsigned frame_sub;

void uc_analog_frame(uint16_t *words)
{
    if (++frame_sub == 256) {
        frame_sub = 0;
        ui_check.frame++;
    }
    ui_check.frame_w0 = words[0];
}

int uc_mute(unsigned track, unsigned muted, unsigned scope)
{
    ui_check.mute++;
    ui_check.mute_n++;
    ui_check.mute_track = track;
    ui_check.mute_muted = muted;
    ui_check.mute_scope = scope;
    if (ui_check.armed) {
        ui_check.mute_taken++;
        return 1;
    }
    return 0;
}

int uc_knob_turn(void *view, int id, int delta, int knob, int how)
{
    (void)view;
    ui_check.knob++;
    ui_check.knob_id = id;
    ui_check.knob_delta = delta;
    ui_check.knob_knob = knob;
    ui_check.knob_how = how;
    if (how >= 0 && how < 3)
        ui_check.knob_how_n[how]++;
    if (ui_check.armed && (knob == ENC_A + 1 || knob == ENC_LEVEL)) {
        ui_check.knob_taken++;
        return 1;
    }
    return 0;
}

/* The LED layers (24 bytes, 4 LEDs a byte, 2 bits each): the steady layer
 * 0x4180eb64, the timed-flash layer 0x4180e9b4, and 0x4180eb4c, what the
 * panel was last sent (flash ^ steady). An LED's slot in them is
 * *(0x4180eb88 + id * 4) (-1: no LED), and its 2 bits are an index into the
 * LED's own 4 colours at 0x448b5b19 + id * 4, so a lit LED can read 0. */
#define LED_SHOWN ((const volatile uint8_t *)0x4180eb4cu)
#define LED_STEADY ((const volatile uint8_t *)0x4180eb64u)
#define LED_SLOT(id) (*(const volatile int32_t *)(0x4180eb88u + (id) * 4u))
#define LED_PALETTE(id, k) (*(const volatile uint8_t *)(0x448b5b19u + (id) * 4u + (k)))

void uc_tick(void)
{
    int32_t slot = LED_SLOT(0x20);
    int i;
    ui_check.ticks++;
    for (i = 0; i < 24; i++)
        ui_check.leds[i] = LED_SHOWN[i];
    ui_check.led20_slot = slot;
    if (slot >= 0 && slot < 96) {
        unsigned sh = ((unsigned)slot & 3u) * 2u;
        ui_check.led20_shown = (LED_SHOWN[slot >> 2] >> sh) & 3u;
        ui_check.led20_steady = (LED_STEADY[slot >> 2] >> sh) & 3u;
        ui_check.led20_colour = LED_PALETTE(0x20, ui_check.led20_shown);
    }
}

static void uc_select(void)
{
    ui_check.selects++;
    ui_check.armed ^= 1;
}

/* only while armed: an illegal instruction (0x4afc) in the UI task */
static void uc_crash_select(void)
{
    if (!ui_check.armed)
        return;
    ui_check.faults++;
    __asm__ volatile(".short 0x4afc" : : : "memory");
}

static int uc_nothing(void)
{
    return 0;
}

static const void *const uc_row[4] = {
    (const void *)uc_label, (const void *)uc_select,
    (const void *)uc_nothing, (const void *)uc_nothing,
};

static const void *const uc_crash_row[4] = {
    (const void *)uc_crash_label, (const void *)uc_crash_select,
    (const void *)uc_nothing, (const void *)uc_nothing,
};

void uc_settings(void *menu)
{
    core_additem(menu, uc_row);
    core_additem(menu, uc_crash_row);
}
