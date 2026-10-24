/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-ar2 0.5: machines mods add (docs/ADAPTING.md, "Machines (core-ar2 0.5,
 * Analog Rytm MKII)"). The Rytm has 53 machine slots; 34..52 are hidden
 * factory machines (mask 0: no voice may pick them). A mod takes one of
 * slots 34..43 through the table core_machines, contributed in mod.json:
 *
 *     "contribute": [
 *       {"to": "core_machines", "order": 50, "data": "00000000",
 *        "relocs": [[0, "abs32", "sym:my_machine", 0]]}],
 *     "resources": {"core": "0.5"}
 *
 * elekloader names the entry's slot as the resource "machine:<slot>" (the
 * profile's slot_claims), so two mods that take the same slot are refused.
 * At boot, before the OS reads them, core writes each entry's engine, home
 * track, voice mask and names into the machine table, and its knobs' labels,
 * maximum and default into the slot's parameter records. A record keeps its
 * id, its slot in the sound (indices 9..16, LEV first) and its MIDI CC and
 * NRPN, so a sound stores the knobs as it stores any machine's. A knob
 * whose name is 0 keeps the stock record as it is. Slots 35..42 (the hidden
 * "2" machines) have all eight records; 34 (BT IMPROVED) and 43 (FUNC. GEN.)
 * have five (LEV and four more), and a knob with no record is ignored. The
 * TEST machines' slots 44..52 are not offered: mods already use their records
 * as spare parameters (fx-sends, voice-dac). The value's text and the
 * knob's turn can be changed through core_params (pages.h).
 *
 * A kit or sound that names the machine loads on a build without the mod (or
 * on stock) with that sound's machine set to DISABLE: stock's kit load does
 * that for any machine its voice may not run.
 *
 * The engine is called once per 32-frame audio block for each physical voice
 * running the machine, as stock's are (the audio interrupt):
 *
 *     void engine(int32_t voice, void *frame, uint16_t *blk, int32_t pitch)
 *
 * frame is the analog control frame (0x800063c0), blk the track's parameter
 * block (word k is sound index 9 + k, so word 0 is LEV), pitch the note. An
 * engine may run a stock engine (CORE_ENGINE_*) with a block of its own.
 */
#ifndef ELEKLOADER_AR2_MACHINES_H
#define ELEKLOADER_AR2_MACHINES_H

#include <stdint.h>

typedef void (*core_engine)(int32_t voice, void *frame, uint16_t *blk, int32_t pitch);

struct core_machine_knob {
    const char *name;       /* long name ("Decay Time"); 0: the record as stock has it */
    const char *label;      /* the knob's three letters ("DEC") */
    uint8_t min, max;       /* the range, in the record's units (0..127 for most) */
    uint8_t def;            /* the default */
    uint8_t centred;        /* 1: shown around the middle of the range (stock's Tune) */
};

struct core_machine {
    int32_t slot;           /* 34..43 */
    core_engine engine;
    int8_t home;            /* the track the machine list files it under: 0 BD,
                               1 SD, 4 BT, -1 none (as XT CLASSIC) */
    uint8_t voices;         /* bit v: physical voice v may run it (0 BD, 1 BT,
                               2 SD, 3 LT, 4 CH/OH, 5 MT/HT, 6 CY/CB, 7 RS/CP) */
    uint16_t pad;
    const char *name;       /* "BD TX-9" */
    const char *label;      /* four letters: "BDT9" */
    struct core_machine_knob knobs[8];  /* sound indices 9..16: LEV, then A..H's order */
};

#define CORE_VOICE_BD 0x01
#define CORE_VOICE_BT 0x02
#define CORE_VOICE_SD 0x04
#define CORE_VOICE_LT 0x08
#define CORE_VOICE_HH 0x10
#define CORE_VOICE_MT 0x20
#define CORE_VOICE_CY 0x40
#define CORE_VOICE_RS 0x80

/* stock engines (OS 1.74) */
#define CORE_ENGINE_BD_CLASSIC ((core_engine)0x401105c8)
#define CORE_ENGINE_SD_CLASSIC ((core_engine)0x401179bc)
#define CORE_ENGINE_BT_CLASSIC ((core_engine)0x4011b5a0)
#define CORE_ENGINE_XT_CLASSIC ((core_engine)0x4011b850)
#define CORE_ENGINE_DISABLE ((core_engine)0x40113f68)

#endif
