/* SPDX-License-Identifier: GPL-2.0-or-later */
/* std-check: core-a4mk2 0.7's standard events (std1), one mod built with
 * "events": "std1" against elekloader/sdk/include/std1.h and the A4's
 * keys.h. It changes nothing the unit does, except: while armed (STD TEST
 * selected in SETTINGS, again to disarm), ev_param_turn takes every turn of
 * encoder A, so stock's value under it stays put.
 *
 * Everything it sees is in std_check (an exported symbol), for an
 * emulator's or a unit's memory read:
 *   magic      0x53544431 ("STD1") once ev_boot ran
 *   boot ... param_turn   each event's calls
 *   key, flags            the last ev_key's arguments
 *   turn_id, turn_delta, turn_knob   the last ev_param_turn's
 *   project_how, project_dropped     the last ev_project's
 *   posted, timed         core_post's calls run (one posted per 1024 audio
 *                         blocks from ev_render_in) and core_timer's (mask
 *                         8: every 8th tick)
 *   post_fail             core_post refusals
 * and on screen, 2-pixel bars from the left whose length (mod 128) is a
 * count: y 60 keys, y 56 param turns, y 52 STD TEST selects, y 48 posted
 * calls, y 44 timer calls; a 3x3 square top right blinks with the tick and
 * is drawn because core_redraw asks for a frame every 8 ticks.
 */
#include <stdint.h>
#include <std1.h>
#include "analog-four-mk2/keys.h"

typedef void (*fillrect_t)(void *bmp, int x0, int y0, int x1, int y1, int colour);
#ifndef FILLRECT_AT
#error "FILLRECT_AT: fill_rect's address (mod.json cflags)"
#endif
#define FILLRECT ((fillrect_t)FILLRECT_AT)

extern void core_additem(void *menu, const void *row);  /* core_setting_add is not on the A4 yet */
extern void sc_label(void);                              /* label.s: the row's name */

struct std_check {
    uint32_t magic;
    uint32_t boot, tick, draw, key, settings, project, render_in, param_turn;
    uint32_t last_key, last_flags;
    int32_t turn_id, turn_delta, turn_knob;
    int32_t project_how;
    uint32_t project_dropped;
    uint32_t posted, timed, post_fail;
    uint32_t armed, selects;
};
volatile struct std_check std_check;

void sc_boot(void)
{
    std_check.magic = 0x53544431u;
    std_check.boot++;
}

static void sc_timed(void)
{
    std_check.timed++;
}

static void sc_posted(void *arg)
{
    std_check.posted += (uint32_t)(uintptr_t)arg;
}

void sc_tick(void)
{
    if (std_check.tick++ == 0)
        core_timer(sc_timed, 8);
    if ((std_check.tick & 7) == 0)
        core_redraw();
}

int sc_key(unsigned key, unsigned flags)
{
    std_check.key++;
    std_check.last_key = key;
    std_check.last_flags = flags;
    return 0;
}

int sc_param_turn(void *view, int id, int delta, int knob)
{
    (void)view;
    std_check.param_turn++;
    std_check.turn_id = id;
    std_check.turn_delta = delta;
    std_check.turn_knob = knob;
    return std_check.armed && knob == ENC_A;
}

void sc_project(int how, unsigned dropped)
{
    std_check.project++;
    std_check.project_how = how;
    std_check.project_dropped = dropped;
}

void sc_render_in(void)
{
    if ((++std_check.render_in & 1023) == 0 && core_post(sc_posted, (void *)1))
        std_check.post_fail++;
}

static void sc_select(void)
{
    std_check.selects++;
    std_check.armed ^= 1;
}

static int sc_nothing(void)
{
    return 0;
}

static const void *const sc_row[4] = {
    (const void *)sc_label, (const void *)sc_select,
    (const void *)sc_nothing, (const void *)sc_nothing,
};

void sc_settings(void *menu)
{
    std_check.settings++;
    core_additem(menu, sc_row);
}

static void sc_bar(void *bmp, int y, unsigned n)
{
    int len = n & 127;
    FILLRECT(bmp, 0, y, 127, y + 1, 0);
    if (len)
        FILLRECT(bmp, 0, y, len - 1, y + 1, 1);
}

void sc_draw(void *fb)
{
    std_check.draw++;
    sc_bar(fb, 60, std_check.key);
    sc_bar(fb, 56, std_check.param_turn);
    sc_bar(fb, 52, std_check.selects);
    sc_bar(fb, 48, std_check.posted);
    sc_bar(fb, 44, std_check.timed);
    FILLRECT(fb, 125, 61, 127, 63, (std_check.tick >> 4) & 1);
}
