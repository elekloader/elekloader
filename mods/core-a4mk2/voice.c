/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-a4mk2 0.7: std1's voice events (README, "Standard events (0.7)").
 * Both run in the audio interrupt (AUDIO, observers): keep a handler short
 * and call no firmware.
 *
 *   ev_voice_start  std_voice_start, from rows.s's core_voice_apply
 *                   (0x401025ac): f(voice, track), once for each voice a new
 *                   note gets (on a poly track the voice the allocator
 *                   picked, in UNISON each of its voices)
 *
 * A voice is 0..3 the synth voices, 4 the FX track, 5 the CV track; a track
 * is 0..3 synth, 4 FX, 5 CV (analog-four-mk2/params.h).
 */

#include <std1.h>

#define VOICES 6
#define TRACKS 6

typedef void (*fn_start)(unsigned voice, unsigned track);

extern const fn_start std_voice_start[];

void core_std_voice_start(unsigned v, unsigned track)
{
    const fn_start *f;
    if (v >= VOICES || track >= TRACKS)
        return;
    for (f = std_voice_start; *f; f++)
        (*f)(v, track);
}
