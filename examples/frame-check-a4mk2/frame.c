/* SPDX-License-Identifier: GPL-2.0-or-later */
/* frame-check: does a write in core-a4mk2's ev_analog_frame reach the
 * analog board? It changes nothing the unit does until armed (FRAME TEST
 * selected in SETTINGS, again to disarm). Armed, every block it copies
 * voice 2's 13 frame words over voice 1's (words[13 .. 25] to
 * words[0 .. 12]). With trigs on track 1 only, voice 2 idles, so its words
 * are stock's own values for a silent voice, and track 1 should go silent.
 *
 * Each call it also reads back the analog frame stock packed the block
 * before (the DMA buffer 0x80002800 + i * 0x1f0, i the buffer index at
 * 0x417357f4; frame word w in the low half of the long at +0x10 + w * 4,
 * the high half 0x9001, with bit 26 also set on word 0's) and compares voice 1's 13 words with what this mod
 * left in the frame then: so a match proves a handler's write is what the
 * pack sends.
 *
 * Everything it sees is in frame_check (exported), for a memory read:
 *   magic         0x46524331 ("FRC1") once the first call ran
 *   calls         ev_analog_frame's calls / 256
 *   armed, selects  the SETTINGS row's state and its selects
 *   copies        the blocks it copied voice 2 over voice 1 / 256
 *   pack_ok, pack_bad  blocks whose packed voice 1 matched / didn't match
 *                 what the mod left (pack_bad: never counts the first call)
 *   pack_tag_bad  packed longs whose high half was not 0x9001
 *   bad_w, bad_got, bad_want  the first mismatch: word, packed, left
 *   v1_*, v2_*    voice 1's and 2's words 11 and 12 as stock built them
 *                 (before any copy): min and max since the last select
 *   last_v1[13], last_v2[13]  voice 1's and 2's words at the last call,
 *                 before any copy
 */
#include <stdint.h>
#include <std1.h>
#include "analog-four-mk2/ui.h"

extern void core_additem(void *menu, const void *row);
extern void fc_label(void);

#define VOICE_WORDS 13
#define PACK_INDEX (*(volatile uint32_t *)0x417357f4u)
#define PACK_BUF(i) ((volatile uint32_t *)(0x80002800u + (i) * 0x1f0u + 0x10u))

struct frame_check {
    uint32_t magic, calls, armed, selects, copies;
    uint32_t pack_ok, pack_bad, pack_tag_bad;
    uint32_t bad_w, bad_got, bad_want;
    int32_t v1_11_min, v1_11_max, v1_12_min, v1_12_max;
    int32_t v2_11_min, v2_11_max, v2_12_min, v2_12_max;
    uint16_t last_v1[VOICE_WORDS], last_v2[VOICE_WORDS];
};
volatile struct frame_check frame_check;

static unsigned call_sub, copy_sub;
static uint16_t left[VOICE_WORDS];
static int have_left, reset_stats = 1;

static void range(volatile int32_t *min, volatile int32_t *max, int16_t v)
{
    if (v < *min)
        *min = v;
    if (v > *max)
        *max = v;
}

void fc_frame(uint16_t *words)
{
    volatile uint32_t *packed;
    int w;

    if (!frame_check.magic)
        frame_check.magic = 0x46524331u;
    if (++call_sub == 256) {
        call_sub = 0;
        frame_check.calls++;
    }

    /* the block before: what the pack sent against what this mod left */
    if (have_left) {
        int bad = 0;
        packed = PACK_BUF(PACK_INDEX & 1u);
        for (w = 0; w < VOICE_WORDS; w++) {
            uint32_t l = packed[w];
            /* the pack sets bit 26 of word 0's long after filling it */
            if (((l & ~0x04000000u) >> 16) != 0x9001u)
                frame_check.pack_tag_bad++;
            if ((uint16_t)l != left[w] && !bad) {
                bad = 1;
                if (!frame_check.pack_bad) {
                    frame_check.bad_w = (uint32_t)w;
                    frame_check.bad_got = l;
                    frame_check.bad_want = left[w];
                }
            }
        }
        if (bad)
            frame_check.pack_bad++;
        else
            frame_check.pack_ok++;
    }

    /* stock's words, before any copy */
    if (reset_stats) {
        reset_stats = 0;
        frame_check.v1_11_min = frame_check.v1_11_max = (int16_t)words[11];
        frame_check.v1_12_min = frame_check.v1_12_max = (int16_t)words[12];
        frame_check.v2_11_min = frame_check.v2_11_max = (int16_t)words[VOICE_WORDS + 11];
        frame_check.v2_12_min = frame_check.v2_12_max = (int16_t)words[VOICE_WORDS + 12];
    }
    range(&frame_check.v1_11_min, &frame_check.v1_11_max, (int16_t)words[11]);
    range(&frame_check.v1_12_min, &frame_check.v1_12_max, (int16_t)words[12]);
    range(&frame_check.v2_11_min, &frame_check.v2_11_max, (int16_t)words[VOICE_WORDS + 11]);
    range(&frame_check.v2_12_min, &frame_check.v2_12_max, (int16_t)words[VOICE_WORDS + 12]);
    for (w = 0; w < VOICE_WORDS; w++) {
        frame_check.last_v1[w] = words[w];
        frame_check.last_v2[w] = words[VOICE_WORDS + w];
    }

    if (frame_check.armed) {
        for (w = 0; w < VOICE_WORDS; w++)
            words[w] = words[VOICE_WORDS + w];
        if (++copy_sub == 256) {
            copy_sub = 0;
            frame_check.copies++;
        }
    }
    for (w = 0; w < VOICE_WORDS; w++)
        left[w] = words[w];
    have_left = 1;
}

static void fc_select(void)
{
    frame_check.selects++;
    frame_check.armed ^= 1;
    reset_stats = 1;
}

static int fc_nothing(void)
{
    return 0;
}

static const void *const fc_row[4] = {
    (const void *)fc_label, (const void *)fc_select,
    (const void *)fc_nothing, (const void *)fc_nothing,
};

void fc_settings(void *menu)
{
    core_additem(menu, fc_row);
}
