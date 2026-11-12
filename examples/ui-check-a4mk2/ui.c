/* SPDX-License-Identifier: GPL-2.0-or-later */
/* ui-check: core-a4mk2 0.7's UI and system events (std1), and core's own
 * ev_knob_turn, one mod built with "events": "std1". It changes nothing the
 * unit does, except while armed (UI TEST selected in SETTINGS, again to
 * disarm): then ev_knob_turn takes every turn of encoder B, ev_mute takes
 * every mute, and ev_key_leds lights LED 0x20.
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
 *   pattern_*     the last ev_pattern's (pre, pattern, how)
 *   mute_*        the last ev_mute's (track, muted, scope); mute_taken
 *   knob_*        the last ev_knob_turn's (id, delta, knob, how); knob_taken
 *                 and knob_how_n[3], the turns of each how
 *   leds[24]      the LED frame (0x4180e9b4) at the last tick
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
    return 0;
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
    if (ui_check.armed && knob == ENC_A + 1) {
        ui_check.knob_taken++;
        return 1;
    }
    return 0;
}

void uc_tick(void)
{
    const volatile uint8_t *frame = (const volatile uint8_t *)0x4180e9b4u;
    int i;
    ui_check.ticks++;
    for (i = 0; i < 24; i++)
        ui_check.leds[i] = frame[i];
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
