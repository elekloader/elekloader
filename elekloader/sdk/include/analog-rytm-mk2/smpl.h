/* SPDX-License-Identifier: GPL-2.0-or-later */
/* smpl-machines: SMPL machines on the Analog Rytm MKII (mods/smpl-machines,
 * docs/ADAPTING.md "SMPL machines (smpl-machines, Analog Rytm MKII)").
 *
 * The SMPL page's eight knobs are the sound's indices 17..24 (records 39..46:
 * TUN, FIN, SMP, BR, STA, END, LOP, LEV), stock's sample layer, shared by
 * every SRC machine. A SMPL machine takes the page over for the sounds that
 * run it: its own title, knob labels, names, looks, ranges and value texts,
 * on stock's records, so the values are still the sound's own words: they
 * store, p-lock, slide and take the LFO and MIDI as stock's do. While a
 * sound runs one, stock's sample layer stays silent on its voice, and the
 * machine's engine, if it has one, gets the words each audio block. Stock's
 * sample playback is machine 0, so a sound that names none is unchanged.
 *
 * A machine's page shows its knobs in record order, so knob C is SMP's word
 * (index 19, CC 27), where stock's page shows BR. It is the sound's sample
 * slot: a move never writes it, and what it holds is the sample stock plays.
 * A machine that needs knob C for something else sets SMPL_OWN_C (below),
 * which gives it a value of its own.
 *
 * A mod adds machines through the table smpl_machines, and asks the
 * framework for their ids by count; the build's lockfile keeps them, so a
 * saved sound finds its machine again. In mod.json:
 *
 *     "pool_ids": {"smpl_machines": 1},
 *     "contribute": [
 *       {"to": "smpl_machines", "order": 50, "data": "00000000",
 *        "relocs": [[0, "abs32", "sym:my_smpl", 0]]}],
 *     "requires": ["core", "smpl-machines"]
 *
 * and in C:
 *
 *     SMPL_MACHINES;
 *     const struct smpl_machine my_smpl = {
 *         SMPL_MACHINE(0), "PLAITS 2", "PLT2", CORE_VOICE_BD | CORE_VOICE_SD, SMPL_REQUIRED, 0,
 *         CORE_MACHINE(0), my_engine,
 *         {{"HRM", "Harmonics", 0, 0, 127 << 8, 64 << 8}, ...}};
 *
 * Which machine a sound runs: the one its SRC machine requires (bind
 * SMPL_REQUIRED and src the SRC machine's slot), else the sound's own choice
 * in the sound setup menu's SMPL MACHINE row (kept in a sound field of
 * smpl-machines'), whose AUTO is the one its SRC machine names as
 * SMPL_DEFAULT, or stock's. A machine whose voices leave out the track's
 * voice is not offered there, and a sound that names it plays stock's. A
 * SMPL_REQUIRED machine is not offered either: only its SRC machine runs it.
 * A change of SRC machine that changes the SMPL machine writes the new one's
 * defaults, as a choice in the row does.
 *
 * The engine runs in the audio interrupt, once a block for each voice whose
 * owner track plays a sound that runs the machine (a sound-locked step's
 * pool sound included), before stock's sample layer:
 *
 *     void engine(int32_t voice, int32_t track, const int16_t *words, int32_t trig)
 *
 * words[k] is the track's engine word for sound index 17 + k (knob k, after
 * p-locks, slides and the LFO), value << 8 with the fine part in the low
 * byte; trig is 1 when the voice's sample layer would have started this
 * block (a trig of the track). Keep it short; plain reads. */
#ifndef ELEKLOADER_AR2_SMPL_H
#define ELEKLOADER_AR2_SMPL_H

#include <stdint.h>

struct smpl_knob {
    const char *label;      /* up to 4 characters; 0: a blank knob (no label, no graphic) */
    const char *name;       /* the header's name as it turns; 0: the label */
    int32_t look;           /* a stock id (1..468) whose knob it looks like; 0: the record's own,
                             * or a plain dial (BR's, 42) when min < max differs
                             * from the record's range (not knob C's own value) */
    int32_t min, max, def;  /* min < max: the range and default (8.8); else the record's */
    /* the value's text into buf (up to 8 characters), value 8.8; 0: a number */
    void (*text)(char *buf, int32_t value);
};

typedef void (*smpl_engine)(int32_t voice, int32_t track, const int16_t *words, int32_t trig);

struct smpl_machine {
    int32_t id;             /* SMPL_MACHINE(i) */
    const char *name;       /* the SMPL page's title, up to 16 */
    const char *label;      /* four letters */
    uint8_t voices;         /* bit v: voice v may run it (CORE_VOICE_*, machines.h) */
    uint8_t bind;           /* SMPL_FREE, SMPL_DEFAULT or SMPL_REQUIRED, with src */
    uint16_t flags;         /* SMPL_OWN_C, SMPL_WAVEFORM; 0 */
    int32_t src;            /* the SRC machine (its slot: CORE_MACHINE(i), or a number) bind names */
    smpl_engine engine;     /* 0: none */
    struct smpl_knob knobs[8];  /* sound indices 17..24, records 39..46, knobs A..H */
};

/* flags: knob C keeps a value of its own, a lockable sound field, instead of
 * SMP's word (index 19, the sample stock plays). The bits are taken only
 * when the companion smpl-knob-c is in the build: a mod with such a machine
 * lists "smpl-knob-c" in its "requires". Without it knob C stays SMP's word. */
#define SMPL_OWN_C 0x0001

/* (smpl-machines 0.3, core-ar2 0.15) The SAMPLE view's second page, SMPL
 * WAVEFORM, draws stock's waveform of the sample SMP's word names. A machine
 * that plays that sample sets SMPL_WAVEFORM to keep the page (its knobs,
 * compact, over stock's waveform); without it the machine has one page:
 * the SMPL key stays on it, and a view on page 2 moves back to it. */
#define SMPL_WAVEFORM 0x0002

/* smpl-knob-c's record (smpl-machines imports it weakly: zeros without it) */
struct smpl_knob_c {
    int32_t id;             /* its knob id (CORE_KNOB(0)), 0 when absent */
    uint32_t field;         /* STORE() of its lockable sound field */
};

#define SMPL_FREE 0         /* chosen in the sound setup menu only */
#define SMPL_DEFAULT 1      /* a sound of SRC machine src runs it on AUTO */
#define SMPL_REQUIRED 2     /* a sound of SRC machine src always runs it */

/* the i-th SMPL machine id the framework gives the mod ("pool_ids") */
#define SMPL_MACHINES extern char __pool_smpl_machines[]
#define SMPL_MACHINE(i) ((int32_t)__pool_smpl_machines + (i))

/* exported by smpl-machines: the machine track t plays now, as the audio
 * side sees it (the sound last put on the track, a sound lock's pool sound
 * included); 0 for stock's sample layer. Any context. Like a label, a
 * pointer returned to asm is in %a0 and %d0. */
const struct smpl_machine *smpl_track_machine(int32_t track);

#endif
