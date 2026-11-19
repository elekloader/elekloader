/* SPDX-License-Identifier: GPL-2.0-or-later */
/* frame-check 0.3: does a value a mod chooses, written in core-a4mk2's
 * ev_analog_frame, do on the analog board what it should? It changes
 * nothing the unit does until armed (FRAME TEST selected in SETTINGS, again
 * to disarm). Armed, every block it raises voice 1's two oscillators one
 * octave: for each it takes stock's pitch state u for this block, doubles
 * it (clamped to stock's own top, 0x3fffff), and writes the frame word stock
 * would have built for that u (fc_pitch_word, pitch.s: stock's formula with
 * stock's calibration pair for that oscillator). Track 1 should sound the
 * same, one octave up.
 *
 * Every block, before any write, it also rebuilds voice 1's two oscillator
 * words from stock's own u and compares them with the words stock built
 * (fml_ok / fml_bad). A block whose rebuild does not match is left as stock
 * built it (fml_skip), so only values from a formula proven on this unit
 * reach the board.
 *
 * As 0.2, each call reads back the analog frame stock packed the block
 * before (the DMA buffer 0x80002800 + i * 0x1f0, i the buffer index at
 * 0x417357f4; frame word w in the low half of the long at +0x10 + w * 4,
 * the high half 0x9001, with bit 26 also set on word 0's) and compares voice
 * 1's 13 words with what this mod left in the frame then.
 *
 * Everything it sees is in frame_check (exported), for a memory read
 * (byte offsets):
 *   +0   magic       0x46524333 ("FRC3") once the first call ran
 *   +4   calls       ev_analog_frame's calls / 256
 *   +8   armed, +12 selects  the SETTINGS row's state and its selects
 *   +16  writes      the blocks it wrote the octave words / 256
 *   +20  pack_ok, +24 pack_bad  blocks whose packed voice 1 matched / didn't
 *                    match what the mod left (never counts the first call)
 *   +28  pack_tag_bad  packed longs whose high half was not 0x9001
 *   +32  bad_w, +36 bad_got, +40 bad_want  the first pack mismatch
 *   +44  v1_11_min .. +72 v2_12_max  voice 1's and 2's words 11 and 12 as
 *                    stock built them: min and max since the last select
 *   +76  last_v1[13], +102 last_v2[13]  voice 1's and 2's words at the last
 *                    call, before any write (u16)
 *   +128 fml_ok, +132 fml_bad  blocks whose rebuilt w0 and w1 matched /
 *                    didn't match stock's
 *   +136 fml_bad_o, +140 fml_bad_got, +144 fml_bad_want  the first mismatch:
 *                    oscillator, rebuilt, stock's
 *   +148 fml_skip    armed blocks left unwritten because the rebuild failed
 *   +152 clamps      octave values clamped to 0x3fffff
 *   +156 last_u[2]   voice 1's pitch state per oscillator, last call
 *   +164 last_up[2]  the doubled (clamped) state written, last armed call
 *   +172 last_w[2]   stock's w0 and w1, last call (s16)
 *   +176 last_wr[2]  the words written, last armed call (s16)
 *   +180 w0_min, +184 w0_max  stock's w0 since the last select (int32)
 *   size 188
 */
#include <stdint.h>
#include <std1.h>
#include "analog-four-mk2/ui.h"

extern void core_additem(void *menu, const void *row);
extern void fc_label(void);
extern int32_t fc_pitch_word(uint32_t u, int32_t c0, int32_t c1);

#define VOICE_WORDS 13
#define OSCS 2
#define PACK_INDEX (*(volatile uint32_t *)0x417357f4u)
#define PACK_BUF(i) ((volatile uint32_t *)(0x80002800u + (i) * 0x1f0u + 0x10u))
/* stock's pitch state per voice and oscillator, stored by the frame builder
 * after it built the word (bit 31 a flag), and its calibration pairs */
#define PITCH_U(v, o) (*(volatile uint32_t *)(0x80006a2cu + (v) * 8u + (o) * 4u))
#define PITCH_C0(v, o) (*(volatile int32_t *)(0x40218f68u + (v) * 16u + (o) * 8u))
#define PITCH_C1(v, o) (*(volatile int32_t *)(0x40218f6cu + (v) * 16u + (o) * 8u))
#define U_MAX 0x3fffffu

struct frame_check {
    uint32_t magic, calls, armed, selects, writes;
    uint32_t pack_ok, pack_bad, pack_tag_bad;
    uint32_t bad_w, bad_got, bad_want;
    int32_t v1_11_min, v1_11_max, v1_12_min, v1_12_max;
    int32_t v2_11_min, v2_11_max, v2_12_min, v2_12_max;
    uint16_t last_v1[VOICE_WORDS], last_v2[VOICE_WORDS];
    uint32_t fml_ok, fml_bad, fml_bad_o, fml_bad_got, fml_bad_want;
    uint32_t fml_skip, clamps;
    uint32_t last_u[OSCS], last_up[OSCS];
    int16_t last_w[OSCS], last_wr[OSCS];
    int32_t w0_min, w0_max;
};
volatile struct frame_check frame_check;

static unsigned call_sub, write_sub;
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
    uint32_t u[OSCS];
    int w, o, fml = 1;

    if (!frame_check.magic)
        frame_check.magic = 0x46524333u;
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

    /* stock's words, before any write */
    if (reset_stats) {
        reset_stats = 0;
        frame_check.v1_11_min = frame_check.v1_11_max = (int16_t)words[11];
        frame_check.v1_12_min = frame_check.v1_12_max = (int16_t)words[12];
        frame_check.v2_11_min = frame_check.v2_11_max = (int16_t)words[VOICE_WORDS + 11];
        frame_check.v2_12_min = frame_check.v2_12_max = (int16_t)words[VOICE_WORDS + 12];
        frame_check.w0_min = frame_check.w0_max = (int16_t)words[0];
    }
    range(&frame_check.v1_11_min, &frame_check.v1_11_max, (int16_t)words[11]);
    range(&frame_check.v1_12_min, &frame_check.v1_12_max, (int16_t)words[12]);
    range(&frame_check.v2_11_min, &frame_check.v2_11_max, (int16_t)words[VOICE_WORDS + 11]);
    range(&frame_check.v2_12_min, &frame_check.v2_12_max, (int16_t)words[VOICE_WORDS + 12]);
    range(&frame_check.w0_min, &frame_check.w0_max, (int16_t)words[0]);
    for (w = 0; w < VOICE_WORDS; w++) {
        frame_check.last_v1[w] = words[w];
        frame_check.last_v2[w] = words[VOICE_WORDS + w];
    }

    /* rebuild voice 1's oscillator words from stock's state */
    for (o = 0; o < OSCS; o++) {
        int16_t got;

        u[o] = PITCH_U(0, o) & 0x7fffffffu;
        got = (int16_t)fc_pitch_word(u[o], PITCH_C0(0, o), PITCH_C1(0, o));
        frame_check.last_u[o] = u[o];
        frame_check.last_w[o] = (int16_t)words[o];
        if (got != (int16_t)words[o] && fml) {
            fml = 0;
            if (!frame_check.fml_bad) {
                frame_check.fml_bad_o = (uint32_t)o;
                frame_check.fml_bad_got = (uint32_t)(int32_t)got;
                frame_check.fml_bad_want = (uint32_t)(int32_t)(int16_t)words[o];
            }
        }
    }
    if (fml)
        frame_check.fml_ok++;
    else
        frame_check.fml_bad++;

    if (frame_check.armed) {
        if (!fml) {
            frame_check.fml_skip++;
        } else {
            for (o = 0; o < OSCS; o++) {
                uint32_t up = u[o] << 1;

                if (up > U_MAX) {
                    up = U_MAX;
                    frame_check.clamps++;
                }
                words[o] = (uint16_t)fc_pitch_word(up, PITCH_C0(0, o), PITCH_C1(0, o));
                frame_check.last_up[o] = up;
                frame_check.last_wr[o] = (int16_t)words[o];
            }
            if (++write_sub == 256) {
                write_sub = 0;
                frame_check.writes++;
            }
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
