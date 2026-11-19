/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-ar2 0.17: std1's voice events (README, "Standard events (0.17)").
 * Both run in the audio interrupt (AUDIO, observers): keep a handler short
 * and call no firmware. core-a4mk2's voice.c is the model.
 *
 *   ev_voice_start  std_voice_start: f(voice, track), at the interrupt's
 *                   trig intake (0x401210ae, stdvoice.s) when a track's
 *                   trig (state 1) takes its voice: the owner table
 *                   0x40291138[voice] gets the track
 *   ev_voice_end    std_voice_end: f(voice), when the intake releases a
 *                   started voice (a track's state 2: amp envelope to
 *                   release, 0x4012110e), or a start on a voice still
 *                   started ends it first: a retrig, or a steal by the
 *                   track that shares the voice
 *
 * The intake (0x4011fb66's loop at 0x40121090) walks tracks 11..0; the
 * static table 0x4025a3a0 gives each its voice 0..7 (some tracks share
 * one), and the first track of a block to act on a voice is the only one
 * that block. Stock frees no voice anywhere else: the owner table has no
 * other writer, and a release comes only from the intake. core keeps a bit
 * per started voice, so each start gets one end and an end comes only
 * after a start. The release does not check the owner (stock releases the
 * voice for either track that shares it), and neither does the end.
 */

#include <std1.h>

#define VOICES 8
#define TRACKS 12                               /* the FX track has no voice */

typedef void (*fn_start)(unsigned voice, unsigned track);
typedef void (*fn_end)(unsigned voice);

extern const fn_start std_voice_start[];
extern const fn_end std_voice_end[];

static unsigned started;                        /* a bit per voice */

/* stdvoice.s's release site, and the start below */
void core_std_voice_end(unsigned v)
{
    const fn_end *f;
    if (v >= VOICES || !(started & (1u << v)))
        return;
    started &= ~(1u << v);
    for (f = std_voice_end; *f; f++)
        (*f)(v);
}

/* stdvoice.s's trig site: voice v now belongs to track */
void core_std_voice_start(unsigned v, unsigned track)
{
    const fn_start *f;
    if (v >= VOICES || track >= TRACKS)
        return;
    core_std_voice_end(v);                      /* a retrig, or a steal */
    started |= 1u << v;
    for (f = std_voice_start; *f; f++)
        (*f)(v, track);
}
