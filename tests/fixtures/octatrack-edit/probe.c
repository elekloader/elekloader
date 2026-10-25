/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * A probe of core-ot 0.8's events: it counts each one, logs its arguments into `edp` (read back by
 * a test harness from the emulator's memory), refuses or changes what poked controls say, and runs
 * a poked program of stock calls (or byte stores into the bank, fn 2) from the sys task's tick (the
 * task the panel's keys run in).
 */
#include <stdint.h>
#include "octatrack/core.h"

enum {
    MAGIC, BOOT_EARLY, EARLY_MARK, BOOT_DONE, MARK_AT_DONE,
    CLIP_N, CLIP_REFUSE,                        /* poked: bit OT_CLIP_PASTE / OT_CLIP_UNDO: refuse it */
    SAVING_N, SAVING_PART, RELOAD_N, INIT_PRE, INIT_POST, INIT_ODD, INIT_LAST,
    PROJINIT_N, PROJINIT_JOB, PSEL_N, VRESET_N, VRESET_KEEP, VRESET_KEPT, VKILL_MASK,
    ICON_N, ICON_FORCE, DRAW_N, DRAW_LAST, DIAL_AMP, DIAL_FX1, DIAL_FX2, DIAL_LAST, DIAL_VALUE,
    DIAL_FORCE, TICKS,
    CLIP_LOG = 40,                              /* 16: pre<<31 | what<<28 | how<<24 | bank<<16 | pattern<<8 | track */
    CLIP_OPS = 56,                              /* 16: op | buf is the clipboard <<8 | the undo buffer <<9 */
    RELOAD_LOG = 72,                            /* 8: how<<8 | part */
    PSEL_LOG = 80,                              /* 8: phase<<24 | from<<16 | to<<8 | machines given | OT_PART<<4 */
    STEPS = 88,                                 /* 10 x {tick, fn, a, b, c, ret, done, 0} */
    WORDS = 168,
};
volatile uint32_t edp[WORDS] = { 0x45445031 };  /* 'EDP1' */
static uint32_t marker;

void ed_boot_early(void)
{
    ++edp[BOOT_EARLY];
    marker = 0x5a5aa5a5;
    edp[EARLY_MARK] = OT_B(0x80000000) | 0x100;  /* the SRAM has its stock values (track 0) */
}

void ed_boot_done(void)
{
    ++edp[BOOT_DONE];
    edp[MARK_AT_DONE] = marker;
}

int ed_clip(unsigned pre, const struct ot_clip *c)
{
    uint32_t n = edp[CLIP_N]++;
    if (n < 16) {
        edp[CLIP_LOG + n] = (uint32_t)pre << 31 | (uint32_t)c->what << 28 | (uint32_t)c->how << 24
                            | (uint32_t)c->bank << 16 | (uint32_t)c->pattern << 8 | c->track;
        edp[CLIP_OPS + n] = c->op | (c->buf == OT_CLIPBOARD) << 8 | (c->buf == OT_UNDO_BUFFER) << 9;
    }
    return pre && (edp[CLIP_REFUSE] >> c->how & 1);
}

void ed_part_saving(unsigned part, unsigned zero)
{
    ++edp[SAVING_N];
    edp[SAVING_PART] = part | zero << 8;
}

void ed_part_reload(unsigned how, unsigned part)
{
    uint32_t n = edp[RELOAD_N]++;
    if (n < 8)
        edp[RELOAD_LOG + n] = how << 8 | part;
}

void ed_part_init(unsigned pre, unsigned part)
{
    if (pre) {
        ++edp[INIT_PRE];
        edp[INIT_LAST] = part;
    } else {
        ++edp[INIT_POST];
        if (part != edp[INIT_LAST])
            ++edp[INIT_ODD];
    }
}

void ed_project_init(unsigned job, unsigned zero)
{
    ++edp[PROJINIT_N];
    edp[PROJINIT_JOB] = job | zero << 8;
}

void ed_part_select(unsigned phase, const struct ot_part_select *s)
{
    uint32_t n = edp[PSEL_N]++;
    if (n < 8)
        edp[PSEL_LOG + n] = phase << 24 | (uint32_t)s->from << 16 | (uint32_t)s->to << 8
                            | (uint32_t)OT_PART << 4 | (s->machines != 0);
}

int ed_voice_reset(unsigned trk)
{
    ++edp[VRESET_N];
    if (edp[VRESET_KEEP] >> trk & 1) {
        ++edp[VRESET_KEPT];
        return 1;
    }
    return 0;
}

void ed_voice_kill(unsigned trk)
{
    edp[VKILL_MASK] |= 1u << trk;
}

unsigned ed_track_icon(unsigned trk, unsigned state)
{
    ++edp[ICON_N];
    return edp[ICON_FORCE] == trk + 1 ? 8 : state;
}

/* ". . []" (octabam's REC TRIG MUTE glyph: 7 columns, a long each, the top pixel at bit 31) */
static const uint32_t dots_bits[7] = { 0x20000000, 0, 0x20000000, 0, 0x70000000, 0x70000000, 0x70000000 };
static const uint32_t dots_mask[7] = { 0xf8000000, 0xf8000000, 0xf8000000, 0xf8000000, 0xf8000000,
                                       0xf8000000, 0xf8000000 };
static const struct ot_bitmap dots = { 7, 5, 1, dots_bits, dots_mask };

int ed_track_icon_draw(unsigned trk, unsigned state, int x, int y)
{
    ++edp[DRAW_N];
    edp[DRAW_LAST] = trk << 24 | state << 16 | (uint32_t)(x & 0xff) << 8 | (uint32_t)(y & 0xff);
    if (state != 8)
        return 0;
    ot_blit(&dots, OT_SCREEN_PLANE, x, y);
    return 1;
}

void ed_setup_dial(struct ot_dial *d)
{
    if (d->page >= OT_P2_AMP && d->page <= OT_P2_FX2)
        ++edp[DIAL_AMP + d->page - OT_P2_AMP];
    edp[DIAL_LAST] = (uint32_t)d->page << 24 | (uint32_t)d->slot << 16 | (uint32_t)d->track << 8 | d->part;
    if (d->slot == 0) {
        edp[DIAL_VALUE] = (uint32_t)d->value;
        if (edp[DIAL_FORCE])
            d->value = (int32_t)(edp[DIAL_FORCE] & 0x7f);
    }
}

void ed_tick(void)
{
    uint32_t t = ++edp[TICKS];
    for (unsigned k = 0; k < 10; ++k) {
        volatile uint32_t *s = &edp[STEPS + 8 * k];
        if (s[1] && !s[6] && t >= s[0]) {
            s[6] = 1;
            if (s[1] == 2)                      /* a byte into the current bank: (offset, value) */
                OT_B(OT_BANK_RAM + s[2]) = (uint8_t)s[3];
            else
                s[5] = ((uint32_t (*)(uint32_t, uint32_t, uint32_t))s[1])(s[2], s[3], s[4]);
            return;                             /* one a tick */
        }
    }
}
