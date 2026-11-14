/* SPDX-License-Identifier: GPL-2.0-or-later
 * TELOS DSP tone: examples/dsp-tone-ot's tone (input A becomes a quiet sawtooth) as a mod TELOS
 * loads from the card: its DSP code goes to the DSP at run time through dspload-ot. */
#include <stdint.h>
#include "octatrack/dspload.h"
#include "tone_dsp.h"

static const struct ot_dsp_blob tone = { tone_words, TONE_N, TONE_ENTRY, tone_rel, TONE_NREL };
volatile int32_t tone_slot = -2;

void drive_init(void)                    /* TELOS calls it after the link */
{
    tone_slot = ot_dsp_load(&tone);
}
