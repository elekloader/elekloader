/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-a4mk2 0.7: std1's voice events (README, "Standard events (0.7)").
 * Both run in the audio interrupt (AUDIO, observers): keep a handler short
 * and call no firmware.
 *
 *   ev_voice_start  std_voice_start: f(voice, track), from rows.s's
 *                   core_voice_apply (0x401025ac) on a note on, once for
 *                   each voice the note gets (each note of a chord, in
 *                   UNISON each voice)
 *   ev_voice_end    std_voice_end: f(voice), when the interrupt frees a
 *                   started voice: its note length ran out (also STOP and
 *                   all notes off), a new trig cut its track's held live
 *                   note (voice.s), a live or MIDI note off (the apply's
 *                   release path), or a steal: a start on a voice still
 *                   started ends it first
 *
 * Every free on 1.56 runs in the interrupt, the allocator's steals too.
 * core keeps a bit per started voice, so each start gets one end and an
 * end comes only after a start. A voice is 0..3 the synth voices, 4 the FX
 * track, 5 the CV track; a track is 0..3 synth, 4 FX, 5 CV.
 */

#include <std1.h>

#define VOICES 6
#define TRACKS 6
#define NOTE_ON 1                              /* the request's +0xc, as stock tests it */

typedef void (*fn_start)(unsigned voice, unsigned track);
typedef void (*fn_end)(unsigned voice);

extern const fn_start std_voice_start[];
extern const fn_end std_voice_end[];

static unsigned started;                       /* a bit per voice */

/* voice.s's two free sites, and the apply's release path below */
void core_voice_end(unsigned v)
{
    const fn_end *f;
    if (v >= VOICES || !(started & (1u << v)))
        return;
    started &= ~(1u << v);
    for (f = std_voice_end; *f; f++)
        (*f)(v);
}

/* rows.s's core_voice_apply: voice v gets channel (track) ch's request */
void core_voice_note(unsigned v, unsigned track, unsigned type)
{
    const fn_start *f;
    if (v >= VOICES)
        return;
    core_voice_end(v);                         /* a note off, or a steal */
    if (type != NOTE_ON || track >= TRACKS)
        return;
    started |= 1u << v;
    for (f = std_voice_start; *f; f++)
        (*f)(v, track);
}
