/* SPDX-License-Identifier: GPL-2.0-or-later */
/* machine-ar2: the smallest machine mod for the Analog Rytm MKII (core-ar2
 * 0.5). BD COPY sits in hidden slot 36 on the BD track and plays exactly as
 * BD CLASSIC: its engine hands the block to BD CLASSIC's, and its knobs are
 * BD CLASSIC's. A template to start a machine from (docs/ADAPTING.md,
 * "Machines (core-ar2 0.5, Analog Rytm MKII)").
 */
#include "analog-rytm-mk2/machines.h"

static void copy_engine(int32_t voice, void *frame, uint16_t *blk, int32_t pitch)
{
    CORE_ENGINE_BD_CLASSIC(voice, frame, blk, pitch);
}

const struct core_machine machine_bd_copy = {
    36, copy_engine, 0, CORE_VOICE_BD, 0, "BD COPY", "BDCP",
    {
        {"Level", "LEV", 0, 127, 100, 0},
        {"Tune", "TUN", 32, 96, 58, 1},
        {"Decay Time", "DEC", 0, 127, 32, 0},
        {"Hold Time", "HLD", 0, 127, 32, 0},
        {"Sweep Time", "SWT", 0, 127, 64, 0},
        {"Sweep Depth", "SWD", 0, 127, 42, 0},
        {"Waveform", "WAV", 0, 2, 1, 0},
        {"Transient", "TRA", 0, 55, 17, 0},
    },
};
