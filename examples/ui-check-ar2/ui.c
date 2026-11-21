/* SPDX-License-Identifier: GPL-2.0-or-later */
/* ui-check: core-ar2 0.17's UI and system events (std1), one mod built with
 * "events": "std1", the Rytm's twin of examples/ui-check-a4mk2. It changes
 * nothing the unit does, except while armed (UI TEST selected in SETTINGS,
 * again to disarm): then ev_mute takes every mute, ev_clip takes every
 * pattern clear, and ev_key_leds sets LED UC_LED to colour UC_COLOUR.
 *
 * Everything it sees is in ui_check (an exported symbol), for an emulator's
 * or a unit's memory read:
 *   magic        0x55494332 ("UIC2") once ev_boot_done ran
 *   boot_done ... view   each event's calls (idle counts its calls / 256)
 *   usb_configured, usb_mode   the last ev_usb's argument, core_usb_mode()
 *   mode_*       the last ev_mode's (mode, old, new); mode_n[4] the calls for
 *                each mode, mode_last_new[4] the new value each last reported
 *   set_*        the last ev_param_set's (track, id, value)
 *   clip_*       the last ev_clip's (pre, kind, op, track, slot); clip_pre and
 *                clip_post count the two calls, clip_taken the takes;
 *                clip_seen[3] per op, bit 1 << kind for each post that came
 *   pattern_*    the last ev_pattern's (pre, pattern, how)
 *   view_push, view_pop, view_op, view_last   each op's calls, the last op
 *                and view
 *   mute_*       the last ev_mute's (track, muted, scope); mute_taken
 *   crash_magic  0x43525348 ("CRSH") once ev_crash ran; crash_head and
 *                crash_pc the frame's first long and PC
 *   faults       UI CRASH selects that raised the fault
 *
 * UI CRASH (a second row) raises an illegal instruction in the UI task, but
 * only while armed, so ev_crash runs on a real fault. Stock's crash screen
 * follows and the unit needs a power cycle. */
#include <stdint.h>
#include <std1.h>
#include "analog-rytm-mk2/leds.h"
#include "analog-rytm-mk2/ui.h"

#define UC_LED 0                        /* a logical LED; which one it is, the run says */
#define UC_COLOUR 0x7fu

struct ui_check {
    uint32_t magic;
    uint32_t boot_done, idle, crash, usb, key_leds, mode, param_set, clip, pattern, mute, view;
    uint32_t usb_configured, usb_mode;
    int32_t mode_mode, mode_old, mode_new;
    uint32_t mode_n[4];
    int32_t mode_last_new[4];
    uint32_t set_track, set_id;
    int32_t set_value;
    uint32_t clip_pre, clip_post, clip_taken, clip_kind, clip_op, clip_track, clip_slot;
    uint32_t clip_seen[3];
    uint32_t pattern_pre, pattern_now, pattern_how;
    uint32_t view_push, view_pop, view_op;
    void *view_last;
    uint32_t mute_track, mute_muted, mute_scope, mute_taken;
    uint32_t crash_magic, crash_head, crash_pc;
    uint32_t armed, selects, faults;
};
volatile struct ui_check ui_check;
static unsigned idle_sub;

void uc_boot_done(void)
{
    ui_check.magic = 0x55494332u;
    ui_check.boot_done++;
}

void uc_idle(void)
{
    if (++idle_sub == 256) {
        idle_sub = 0;
        ui_check.idle++;
    }
}

/* the exception frame: +0 format, vector and SR; +4 the PC */
void uc_crash(const void *frame)
{
    const uint32_t *f = (const uint32_t *)frame;
    ui_check.crash++;
    if (f) {
        ui_check.crash_head = f[0];
        ui_check.crash_pc = f[1];
    }
    ui_check.crash_magic = 0x43525348u;
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
        core_led_set(mgr, UC_LED, UC_COLOUR);
}

void uc_mode(int mode, int old, int new_)
{
    ui_check.mode++;
    ui_check.mode_mode = mode;
    ui_check.mode_old = old;
    ui_check.mode_new = new_;
    if ((unsigned)mode < MODE_COUNT) {
        ui_check.mode_n[mode]++;
        ui_check.mode_last_new[mode] = new_;
    }
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
    ui_check.clip_slot = c->slot;
    if (!pre && c->op < 3)
        ui_check.clip_seen[c->op] |= 1u << c->kind;
    if (pre && ui_check.armed && c->kind == CLIP_PATTERN && c->op == CLIP_CLEAR) {
        ui_check.clip_taken++;
        return 1;
    }
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

int uc_view(int op, void *mgr, void *view, void *b)
{
    (void)mgr;
    (void)b;
    ui_check.view++;
    if (op == VIEW_PUSH)
        ui_check.view_push++;
    else if (op == VIEW_POP)
        ui_check.view_pop++;
    ui_check.view_op = (uint32_t)op;
    ui_check.view_last = view;
    return 0;
}

/* ---- SETTINGS: UI TEST arms and disarms; UI CRASH faults while armed ---- */

static void uc_select(const struct core_setting *s)
{
    (void)s;
    ui_check.selects++;
    ui_check.armed = !ui_check.armed;
}

static void uc_crash_select(const struct core_setting *s)
{
    (void)s;
    if (!ui_check.armed)
        return;
    ui_check.faults++;
    __asm__ volatile("illegal");
}

static const struct core_setting uc_row = {"UI TEST", uc_select};
static const struct core_setting uc_crash_row = {"UI CRASH", uc_crash_select};

void uc_settings(void *menu)
{
    core_setting_add(menu, &uc_row);
    core_setting_add(menu, &uc_crash_row);
}
