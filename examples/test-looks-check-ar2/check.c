/* SPDX-License-Identifier: GPL-2.0-or-later */
/* test-looks-check-ar2: the knob looks and knob order of a TEST slot
 * (core-ar2 0.15; machines.h, CORE_MACHINE_LOOKS). Two silent machines with
 * the knob tables of two Sidecar voices:
 *
 *   TL PLAITS, slot 44 (BD TEST's records): A ENG, B HRM, C TIM, D MOR,
 *     E DEC, F LPG, G TUN, H LEV. LPG and TUN, a switch and a select on
 *     BD TEST's own records, draw as dials (looks 103 and 100).
 *   TL SAMPLE, slot 45 (SD TEST's, no records for 13 and 14): A SMP, B TUN,
 *     C STA, D and E empty, F END, G LOP, H LEV. END draws as a 0..127 dial
 *     (look 103), LOP as a switch (look 457, voice-dac POL's).
 *
 * The knobs without a look keep the test machine's own. */
#include "analog-rytm-mk2/machines.h"

#define LOOK_DIAL 103       /* BD HARD DEC: a 0..127 dial, plain text */
#define LOOK_TUNE 100       /* BD HARD's TUN: a centred dial */
#define LOOK_SWITCH 457     /* an off/on switch */

static void silent(int32_t voice, void *frame, uint16_t *blk, int32_t pitch)
{
    CORE_ENGINE_DISABLE(voice, frame, blk, pitch);
}

static const struct core_machine_look plaits_looks[8] = {
    {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
    {LOOK_DIAL, 0},         /* LPG */
    {LOOK_TUNE, 0},         /* TUN */
};

const struct core_machine tl_plaits = {
    44, silent, -1, 0xff, CORE_MACHINE_LOOKS, "TL PLAITS", "TLPL",
    {
        {"Level", "LEV", 0, 127, 100, 0},
        {"Engine", "ENG", 0, 127, 0, 0},
        {"Harmonics", "HRM", 0, 127, 64, 0},
        {"Timbre", "TIM", 0, 127, 64, 0},
        {"Morph", "MOR", 0, 127, 64, 0},
        {"Decay", "DEC", 0, 127, 64, 0},
        {"LPG Colour", "LPG", 0, 127, 64, 0},
        {"Tune", "TUN", 32, 96, 64, 1},
    },
    plaits_looks,
};

static const struct core_machine_look sample_looks[8] = {
    {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
    {LOOK_DIAL, 0},         /* END */
    {LOOK_SWITCH, 0},       /* LOP */
};

const struct core_machine tl_sample = {
    45, silent, -1, 0xff, CORE_MACHINE_LOOKS, "TL SAMPLE", "TLSM",
    {
        {"Level", "LEV", 0, 127, 100, 0},
        {"Sample", "SMP", 0, 127, 0, 0},
        {"Tune", "TUN", 40, 88, 64, 1},
        {"Start", "STA", 0, 127, 0, 0},
        {0, 0, 0, 0, 0, 0},
        {0, 0, 0, 0, 0, 0},
        {"End", "END", 0, 127, 127, 0},
        {"Loop", "LOP", 0, 1, 0, 0},
    },
    sample_looks,
};
