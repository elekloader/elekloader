/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * A probe of core-ot 0.9's ev_mute: it counts each change per kind and logs the last live word and the
 * Part's copy before the change into `mup` (read back by a test harness from the emulator's memory).
 * With `mup[KEEP]` poked nonzero it returns the Part's old copy, so the Part keeps it.
 */
#include <stdint.h>
#include "octatrack/core.h"

enum { MAGIC, AUDIO_N, MIDI_N, LAST_KIND, LAST_WORD, LAST_PART, KEEP, KEPT, WORDS };
volatile uint32_t mup[WORDS] = { 0x4d555031 };  /* 'MUP1' */

unsigned mu_mute(unsigned kind, unsigned word)
{
    unsigned part = kind == OT_MUTE_MIDI ? OT_PART_MUTES_MIDI : OT_PART_MUTES_AUDIO;
    ++mup[kind == OT_MUTE_MIDI ? MIDI_N : AUDIO_N];
    mup[LAST_KIND] = kind;
    mup[LAST_WORD] = word;
    mup[LAST_PART] = part;
    if (mup[KEEP]) {
        ++mup[KEPT];
        return part;
    }
    return word;
}
