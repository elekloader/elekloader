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
 */
#include <stdint.h>
#include <std1.h>
#include "analog-four-mk2/keys.h"
#include "analog-four-mk2/leds.h"
#include "analog-four-mk2/ui.h"

extern void core_additem(void *menu, const void *row);
extern void uc_label(void);

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

void uc_crash(const void *frame)
{
    (void)frame;
    ui_check.crash++;
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
}

void uc_param_set(unsigned track, unsigned id, int32_t value)
{
    ui_check.param_set++;
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

static int uc_nothing(void)
{
    return 0;
}

static const void *const uc_row[4] = {
    (const void *)uc_label, (const void *)uc_select,
    (const void *)uc_nothing, (const void *)uc_nothing,
};

void uc_settings(void *menu)
{
    core_additem(menu, uc_row);
}
