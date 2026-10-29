/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Machines for the Digitone mk1, in C: what a machine mod gives the machines companion
 * (mods/machines-dn1) and what the companion gives it (docs/ADAPTING.md, "Machines on the Digitone").
 * The SDK puts this folder on the include path:
 *
 *     #include "digitone-mk1/machines.h"
 *
 * A machine replaces the FM voice of every note its sounds play. Each audio block the companion calls its
 * render for each voice playing one of its sounds; what it writes is that voice's sound, which the
 * Digitone's multimode filter, mixer and effects then take as they take FM. The companion applies the
 * AMP page's envelope and the note's velocity after it. A machine's parameters are the SYN1 and SYN2
 * knobs of its sounds, relabelled: their values keep their sound slots and their stock ranges, so p-locks,
 * MIDI CC, the LFOs and sound presets work on them as on FM, and FM stays valid if the sound goes back.
 * A machine with DNM_PAGES2 takes the two pages' second pages too (machines 0.2): 27 knobs in place of 16.
 * The machine a sound plays is in its sound slot 0 (the id << 8); 0 is FM. */
#ifndef ELEKLOADER_DIGITONE_MK1_MACHINES_H
#define ELEKLOADER_DIGITONE_MK1_MACHINES_H

#include "digitone-mk1/core3.h"

#define DNM_KNOBS 16                /* SYN1 A-H, then SYN2 A-H */
#define DNM_KNOBS_ALL 32            /* with DNM_PAGES2: then SYN1's second page A-H, then SYN2's */

/* One knob. Its value is the stock parameter's: an 8.8 number within that parameter's range
 * (dnm_knob_range): SYN1 A ALGO 0-7, B-D the ratios (indices), E HARM -26..26, F DTUN 0-127, G FDBK 0-127,
 * H MIX -63..63; SYN2 A-H 0-127. With DNM_PAGES2, knobs 16-31 are the second pages, which FM alone uses:
 * 16-19 SYN1 2/2 A-D (the ratio offsets, 0-127.99 in 1/256 steps, FM's default 64), 20-23 none;
 * 24 ADEL and 28 BDEL 0-127, 25 ATRG, 26 ARST, 29 BTRG and 30 BRST off and on (0-1), 27 PHRT 0-4,
 * 31 none (SYN2 2/2 A-H). A knob with no parameter behind it stays empty whatever the machine says. */
struct dnm_knob {
    const char *name;               /* +0  its label (5 characters); 0: unused (blank) on this machine */
    void (*fmt)(int32_t value, char *buf);  /* +4 8.8 value text; terminate within 16 bytes; 0: number */
    int32_t look;                   /* +8  the stock id whose knob graphic it borrows; 0 a plain knob (PTIM) */
};

struct dnm_voice {                  /* the companion's account of a voice, for the machine's render */
    int32_t pitch;                  /* +0 OS 1.44/machines 0.3: processed pitch including portamento;
                                          earlier companions and OS 1.43: raw note word */
    int32_t velocity;               /* +4  its note's velocity, 1-127 */
    int32_t fresh;                  /* +8  1 on the first block of a note: start its phases */
    int32_t track;                  /* +12 0-3 */
    int32_t gate;                   /* +16 1 until the note is released */
};

#define DNM_OWN_ENV 1               /* flags: the machine applies its own envelope (no AMP envelope) */
#define DNM_PAGES2  2               /* flags: knobs and defaults are DNM_KNOBS_ALL long, the second pages
                                       the machine's too (machines 0.2; an older companion reads 16) */

/* How loud to write: a voice that peaks about here is as loud as an FM voice at the stock defaults (a sine
 * peaking at DNM_PEAK is a Q15 sine << 12). The track's level, the AMP page and the mixer follow as for FM. */
#define DNM_PEAK    0x08000000

struct dnm_machine {                /* what a mod contributes to the table dn_machines points to */
    int32_t id;                     /* +0  1-127, claimed as the resource dnmachine:<id> */
    const char *name;               /* +4  its name in the MACHINES menu (14 characters fit) */
    const uint16_t *icon;           /* +8  16 rows of 16 pixels for its tile (bit 15 the left), or 0 */
    const struct dnm_knob *knobs;   /* +12 DNM_KNOBS of them, or DNM_KNOBS_ALL with DNM_PAGES2 */
    /* +16 32 samples, Q1.31, for one block of a voice playing one of its sounds; block holds the FM voice's
     * on entry. Interrupt level, every block: keep it short (the main CPU also runs the filters and
     * effects). */
    void (*render)(int32_t voice, int32_t *block, const struct dnm_voice *v);
    uint32_t flags;                 /* +20 DNM_OWN_ENV, DNM_PAGES2 */
    /* +24 what its knobs are set to when a track switches to it (8.8, within each knob's stock range; -1
     * leaves a knob as it is), as many as its knobs, or 0 for none. A switch back to FM sets the SYN knobs
     * to FM's defaults, and the second pages too when the machine had them; so does a switch to a machine
     * without them. */
    const int16_t *defaults;
} __attribute__((aligned(4)));

/* From the companion. */
extern int32_t dnm_knob(int32_t voice, int32_t knob);           /* the voice's knob value, 8.8 (0-31) */
extern void dnm_knob_range(int32_t knob, int32_t *min, int32_t *max);   /* its stock range, 8.8 */
extern uint32_t dnm_phase_inc(int32_t pitch);                   /* a phase step a sample at 48 kHz */
extern int32_t dnm_sin(uint32_t phase);                         /* sin(2 pi phase / 2^32), Q15 */

/* Optional native SYN UI, machines 0.3 + core4.2 on OS 1.44. Referencing this marker
 * refuses an older companion or an OS whose native UI has not been ported.
 * Existing dnm_machine/knob/voice layouts are unchanged. One descriptor per
 * machine id, static lifetime; use designated initializers. UI task only. */
extern const uint32_t dnm_ui_v1;
struct dnm_page {
    const uint32_t *abi;            /* &dnm_ui_v1 */
    int32_t id;                    /* a registered machine */
    int32_t pages[2];              /* SYN1/2: 0 native count, 1 or 2 available pages */
    /* Return nonzero after drawing; 0 retains the native renderer.
     * syn is 1 or 2, page is zero-based. Notifications draw afterwards. */
    int32_t (*draw)(void *view, void *bitmap, int32_t syn, int32_t page);
};
/* Contribute a pointer to dnm_page to dn_machine_pages. Without a descriptor,
 * native drawing/navigation stay available. UI task only, except machine_id. */
extern int32_t dnm_machine_id(void);             /* registered active machine, 0 for FM */
extern void *dnm_syn_view(int32_t syn);          /* last native SYN view, or 0 */
extern int32_t dnm_syn_page(void *brain);        /* visible SYN1/2, or 0 below menus */
/* Active track's knob (0-31, same physical mapping as dnm_knob), with the
 * native parameter-view GRID/STEP value. Until its SYN view is drawn, kit
 * base value/no lock. Five padding positions return 0. A null locked is OK.
 * *locked is 1 for an explicit lock, otherwise 0; invalid knob/track/kit: 0. */
extern int32_t dnm_knob_value(int32_t knob, int32_t *locked);
/* A persistent UI edit: kit/live update and native SoundRef change event.
 * Track 0-3, slot 0-78. Do not use for temporary playback modulation. */
extern void dnm_sound_edit(int32_t track, int32_t slot, int32_t value);
/* On both SYN subpages the companion consumes unused knobs and handles
 * FUNC+turn using defaults, including all selected GRID/STEP steps 1-64. */

/* Optional long labels, machines 0.3 on OS 1.44; existing descriptor ABI stays unchanged. */
#ifndef DNM_NAMES_V1_API
#define DNM_NAMES_V1_API
extern const uint32_t dnm_names_v1;
struct dnm_names {
    const uint32_t *abi;            /* &dnm_names_v1 */
    int32_t id;
    int32_t count;                 /* DNM_KNOBS or DNM_KNOBS_ALL; bounds long_names reads */
    const char *const *long_names;  /* count entries; null/empty falls back to the short label */
};
#endif
/* Contribute a pointer to dn_machine_names. One descriptor per machine id,
 * static lifetime; use designated initializers. Invalid count or missing/empty
 * long_names entry falls back to the short name. Long labels are used in
 * notifications and the LFO list; compact controls retain dnm_knob.name.
 * Unused SYN controls are omitted from LFO choices, keeping native target IDs. */
#endif
