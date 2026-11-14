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
 *   delta, delta_id       a native core_params entry over every stock id
 *                         (0..0x126) that counts param_apply_delta's calls and
 *                         passes them on, beside ev_param_turn
 *   drawn, draw_selects   frames drawn with DRAW TEST's pattern, and its
 *                         row's selects
 *   w_small, w_medium, w_large, w_centre   core_text's widths in that pattern
 * and on screen, drawn with core_fill, 2-pixel bars from the left whose
 * length (mod 128) is a count, at glass rows (from the top) 2 keys, 6 param
 * turns, 10 STD TEST selects, 14 posted calls, 18 timer calls; a 3x3 square
 * top right blinks with the tick and is drawn because core_redraw asks for
 * a frame every 8 ticks. Both rows are core_setting_add's; DRAW TEST
 * (selected again to stop) draws the helpers' pattern in the right half,
 * glass rows 24..63 (README).
 */
#include <stdint.h>
#include <std1.h>
#include "analog-four-mk2/keys.h"
#include "analog-four-mk2/pages.h"

struct std_check {
    uint32_t magic;
    uint32_t boot, tick, draw, key, settings, project, render_in, param_turn;
    uint32_t last_key, last_flags;
    int32_t turn_id, turn_delta, turn_knob;
    int32_t project_how;
    uint32_t project_dropped;
    uint32_t posted, timed, post_fail;
    uint32_t armed, selects;
    uint32_t delta;
    int32_t delta_id;
    uint32_t drawn, draw_selects;        /* DRAW TEST: frames with the pattern, its row's selects */
    int32_t w_small, w_medium, w_large;  /* core_text's widths (README, "DRAW TEST") */
    int32_t w_centre;
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

/* core_params (native): every stock id, counted and passed on */
static int sc_delta(void *view, int32_t id, int32_t delta, int32_t func)
{
    (void)view; (void)delta; (void)func;
    std_check.delta++;
    std_check.delta_id = id;
    return 0;
}

const struct core_param sc_params = {0, 0x126, 0, sc_delta, 0};

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

static void sc_select(const struct core_setting *s)
{
    (void)s;
    std_check.selects++;
    std_check.armed ^= 1;
}

static uint32_t draw_armed;

static void sc_draw_select(const struct core_setting *s)
{
    (void)s;
    std_check.draw_selects++;
    draw_armed ^= 1;
}

static const struct core_setting sc_row = {"STD TEST", sc_select};
static const struct core_setting sc_draw_row = {"DRAW TEST", sc_draw_select};

void sc_settings(void *menu)
{
    std_check.settings++;
    core_setting_add(menu, &sc_row);
    core_setting_add(menu, &sc_draw_row);
}

/* a bar 2 rows high from the left at glass row y (from the top) */
static void sc_bar(void *fb, int y, unsigned n)
{
    int len = n & 127;
    core_fill(fb, 0, y, 128, 2, 0);
    core_fill(fb, 0, y, len, 2, 1);
}

/* DRAW TEST's 8 x 8 image: a frame with a diagonal, rows top first */
static const uint8_t sc_image[8] = {0xff, 0xc1, 0xa1, 0x91, 0x89, 0x85, 0x83, 0xff};

/* DRAW TEST: the right half, glass rows 24..63 (README, "DRAW TEST") */
static void sc_pattern(void *fb)
{
    std_check.drawn++;
    core_fill(fb, 64, 24, 64, 40, 0);
    core_fill(fb, 66, 26, 10, 4, 1);
    core_blit(fb, 80, 26, 8, 8, sc_image);
    std_check.w_small = core_text(fb, 66, 36, TEXT_SMALL | TEXT_LEFT, "STD1");
    std_check.w_medium = core_text(fb, 126, 36, TEXT_MEDIUM | TEXT_RIGHT | TEXT_INVERT, "STD1");
    std_check.w_large = core_text(fb, 66, 46, TEXT_LARGE | TEXT_LEFT, "A4");
    std_check.w_centre = core_text(fb, 104, 56, TEXT_SMALL | TEXT_CENTRE, "STD1");
    core_px(fb, 64, 24, 1);
    core_px(fb, 127, 24, 1);
    core_px(fb, 64, 63, 1);
    core_px(fb, 127, 63, 1);
    core_px(fb, 128, 30, 1);                  /* off the frame: clipped */
    core_fill(fb, 124, 60, 8, 8, 1);          /* clipped to columns 124..127, rows 60..63 */
}

void sc_draw(void *fb)
{
    std_check.draw++;
    sc_bar(fb, 2, std_check.key);
    sc_bar(fb, 6, std_check.param_turn);
    sc_bar(fb, 10, std_check.selects);
    sc_bar(fb, 14, std_check.posted);
    sc_bar(fb, 18, std_check.timed);
    core_fill(fb, 125, 0, 3, 3, (std_check.tick >> 4) & 1);
    if (draw_armed)
        sc_pattern(fb);
}
