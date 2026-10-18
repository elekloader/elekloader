/* SPDX-License-Identifier: GPL-2.0-or-later */
/* fxkit: what an effect mod for the Digitone mk1 shares (docs/ADAPTING.md, "fxkit"): one effect at a time with
 * the audio (exclusive: core_audio with the voices muted), a mixer and two LFOs around it, and p-locks that follow
 * the pattern. An effect is a whole mod with its own Mod Menu entry and page; it describes its knobs to fxkit and
 * lets fxkit play them:
 *
 *     #include "digitone-mk1/fxkit.h"
 *
 * with "requires": ["core", "fxkit"] and "resources": {"core": "4.1"}, and a contribution to fx_effects:
 *
 *     "contribute": [{"to": "fx_effects", "order": 50, "data": "00000000",
 *                     "relocs": [[0, "abs32", "sym:my_effect", 0]]}]
 *
 * Each knob has a number, its "slot" (the sound slot of a track that the stock knobs of its page would edit): the
 * effect's own knobs 17-24 (page 1), 27-34 (page 2), 50-53, 35-37, 25 and 38-40 (settings), 54-64; the mixer's
 * 66-76; the LFOs' 1-16. fxkit keeps each effect's value of each (8.8) and, while the effect is active, gives its
 * values() this block's: the base, the p-locks of the step that plays, the LFOs on them.
 *
 * P-locks are kept in the pattern, in the lock table of one synth track (the lock track, T4 unless SETUP says
 * otherwise): a step with an effect's locks is a lock trig of that track with those slots locked. So the stock
 * saves, loads, copies and pastes them with the pattern, and the sequencer fires them: core-dn1 4.1's
 * ev_note_queue gives fxkit each, with the time it plays (the first step's, as PLAY starts, fxkit reads from the
 * lock table). The lock track should play nothing of its own in a pattern where an effect has locks: its notes
 * would take them too, and an arpeggiator on it turns its trigs into the arpeggiator's events. */
#ifndef ELEKLOADER_DIGITONE_MK1_FXKIT_H
#define ELEKLOADER_DIGITONE_MK1_FXKIT_H

#include <stdint.h>

#define FX_ABI      2

/* A knob, by its slot. Its value is 8.8: FX_KNOB and FX_BIP 0.0-127.0 (FX_BIP shows -64..+63, 64.0 its centre),
 * FX_OPT 0 .. n - 1. */
enum { FX_KNOB, FX_BIP, FX_OPT };
struct fx_param {
    uint8_t slot;                   /* its slot; 0 ends a list */
    uint8_t kind;                   /* FX_KNOB, FX_BIP or FX_OPT */
    uint8_t n;                      /* FX_OPT: its options, 2-24 */
    uint8_t def;                    /* its default: 0-127, or the option */
    const char *name;               /* its label, 4 characters (the LFO's DEST shows it) */
    const char *lname;              /* its long name, 12 characters, or 0 */
    const char *const *opts;        /* FX_OPT: each option's text, 5 characters */
};

#define FX_S_P1     17              /* page 1, A-H: 17-24 */
#define FX_S_P2     27              /* page 2, A-H: 27-34 */
#define FX_S_SET(i) ((i) < 4 ? 50 + (i) : (i) < 7 ? 35 + (i) - 4 : (i) == 7 ? 25 : 38 + (i) - 8)  /* settings 0-10 */

/* The mixer's, around the effect */
#define FX_M_INL    66              /* IN L: the left input's level, as the stock mixer's input level: 16 (v / 127)^2,
                                       32 unity, 127 +24 dB */
#define FX_M_INR    67              /* IN R */
#define FX_M_PANL   68              /* L PAN: where the left input sits (64: the centre) */
#define FX_M_PANR   69              /* R PAN */
#define FX_M_MIX    70              /* MIX: the dry and the effect (127: the effect only), for an effect without
                                       FX_OWN_MIX */
#define FX_M_BAL    71              /* BAL: the output's balance */
#define FX_M_VOL    72              /* VOL: the output's level (100: unity) */
#define FX_M_WIDTH  75              /* WDTH: the output's width (64: as it is, 0 mono) */
#define FX_M_BYP    76              /* BYP: bypassed */
/* The LFOs', as the stock LFO page's (8.8 as the stock's): LFO 1 from 1, LFO 2 from 9: SPD (64.0 the centre, in
 * the stock's fine steps up to 0x7ffe), MULT (0-11 the tempo's x1 .. x2048, 12-23 free), FADE (64.0 none), DEST
 * (a slot, 0 none), WAVE (TRI SIN SQR SAW EXP RMP RND), SPH (0-127), MODE (FREE TRIG HOLD ONE HALF), DEP (64.0
 * none, fine steps as SPD) */
#define FX_L1       1
#define FX_L2       9

#define FX_OWN_MIX    1u            /* flags: the effect mixes the dry itself: the mixer's MIX is blank */
#define FX_OWN_BYPASS 2u            /* the effect bypasses itself (its trails): values() gets FX_V_BYPASS */
#define FX_V_BYPASS   1u

struct fx_effect {
    uint32_t abi;                   /* FX_ABI */
    const char *name;               /* its name, the pages' titles */
    const struct fx_param *params;  /* its knobs, ending in slot 0 (the mixer's and the LFOs' are fxkit's) */
    uint32_t flags;                 /* FX_OWN_MIX, FX_OWN_BYPASS */
    void (*init)(void);             /* the UI task, once, before fxkit reads params, or 0 */
    /* interrupt level, each block (the render's entry) while it is active: this block's values, val[slot] 8.8 */
    void (*values)(const int16_t *val, uint32_t flags);
    /* interrupt level: 32 frames L,R Q1.31, the inputs after the mixer's input stage to out (all 64 words) */
    void (*render)(int32_t *out, const int32_t *in);
} __attribute__((aligned(4)));

/* ---- the UI task ------------------------------------------------------------------------------------- */
/* The active effect: it has the audio (exclusive, the synths muted) and its values are played. Activating one
 * puts the one before it off. */
extern void fx_activate(const struct fx_effect *e);
extern void fx_deactivate(const struct fx_effect *e);
extern const struct fx_effect *fx_active(void);
/* e plays in fxkit's audio (1, as it starts) or as an insert of its own (0: fxkit's owner stays off for it; its
 * values() still comes) */
extern void fx_chain(const struct fx_effect *e, int32_t in_chain);

/* A knob's value, 8.8: e's own (whether e is active or not). From the MIDI task, fx_set_later. */
extern int32_t fx_get(const struct fx_effect *e, int32_t slot);
extern void fx_set(const struct fx_effect *e, int32_t slot, int32_t value);
extern void fx_set_later(const struct fx_effect *e, int32_t slot, int32_t value);
/* What a knob's control shows: the held step's lock in LOCK mode, else value (the effect's own, what plays) */
extern int32_t fx_view(int32_t slot, int32_t value);
/* A knob turned on e's page to value: in LOCK mode with a trig held, that step's lock; else e's value */
extern void fx_turned(const struct fx_effect *e, int32_t slot, int32_t value);

/* LOCK mode (RECORD on an effect's page): the trig keys are the steps of the current 16-step page */
extern int32_t fx_lock_mode(void);
extern void fx_lock_mode_set(int32_t on);
extern int32_t fx_lock_page(void);                  /* 0-3 */
extern int32_t fx_lock_pages(void);                 /* the pattern's 16-step pages, 1-4 */
extern int32_t fx_held_step(void);                  /* the step whose trig is held, -1 */
/* A key while LOCK mode is on: trig keys hold a step (FUNC + trig clears its locks of e), PAGE the next page.
 * key and flags as ev_key's; func: FUNC is held. -> 1 taken */
extern int32_t fx_lock_key(const struct fx_effect *e, uint32_t key, uint32_t flags, int32_t func);
/* bit s: step 16 page + s has a lock of e's */
extern uint32_t fx_lock_mask(const struct fx_effect *e, int32_t page);
extern int32_t fx_lock_track(void);                 /* 0-3, SETUP's LOCK TRACK */
extern void fx_lock_track_set(int32_t track);
/* The LOCK mode strip for an effect's title bar: the page's 16 steps, 3 pixels square 4 apart, from x, y (its
 * bottom row; 64 pixels wide), the held step's underlined */
extern void fx_lock_strip(const struct fx_effect *e, void *bmp, int32_t x, int32_t y);

/* Controls as the stock FLTR, AMP and LFO pages': eight (A-H, slots[i] 0 an empty one) in two rows of four over
 * their labels (x 17-127, y 0-53), what fx_view shows; the effect draws its title bar and its left column. A knob
 * turned (detents) or pushed (its default, or the next option): fx_turned, so in LOCK mode a held step's lock. */
extern void fx_draw_controls(const struct fx_effect *e, void *bmp, const uint8_t *slots);
extern void fx_knob_turn(const struct fx_effect *e, int32_t slot, int32_t detents);
extern void fx_knob_push(const struct fx_effect *e, int32_t slot);
/* The MIXER and LFO pages (0 MIXER, 1 LFO 1, 2 LFO 2) the same way, knob 0-7. The LFO pages are the stock LFO
 * page's: its knob graphics (the stock's own, from its UI records), WAVE and SPH as one wave, DEST's box and list
 * (a turn opens it, YES picks, NO closes), a turned knob's value under it for a while, in the stock's text. Their
 * titles carry their own count, as the stock's ("LFO (1/2)"; "Confirmed!" a moment after a DEST pick): an effect
 * shows them as they are, and the MIXER's with its own count. */
#define FX_PAGES    3
extern const char *fx_page_title(int32_t page);
extern void fx_page_draw(const struct fx_effect *e, void *bmp, int32_t page);
extern void fx_page_turn(const struct fx_effect *e, int32_t page, int32_t knob, int32_t detents);
extern void fx_page_push(const struct fx_effect *e, int32_t page, int32_t knob);
/* A key while page is on screen, before the effect's own handling: DEST's list takes YES, NO, UP and DOWN while it
 * is open, and any other key closes it (and is the effect's). key and flags as ev_key's; -> 1 taken */
extern int32_t fx_page_key(const struct fx_effect *e, int32_t page, uint32_t key, uint32_t flags);
/* What the pages show besides the values (a value under its knob, DEST's list, the title), folded into a word: an
 * effect draws its page again when it moves */
extern uint32_t fx_page_state(void);
/* A knob of a page: its label and its slot (0: none) */
extern const char *fx_page_label(const struct fx_effect *e, int32_t page, int32_t knob);
extern int32_t fx_page_slot(int32_t page, int32_t knob);
/* A value's text, as its control shows it (a number, +-n, an option, a DEST's name) */
extern void fx_text(const struct fx_effect *e, int32_t slot, int32_t value, char *buf);

/* The mixer and the LFOs of e, packed for its project data (FX_PACK bytes: 149 bits, a format, SPD's and DEP's
 * fine steps); fx_unpack(e, 0): their defaults. fx_serial: a count of the changes to e's own values (fx_set,
 * fx_unpack): an effect packs its block again when it moves, not every tick. Keep the bytes last in a block: a
 * block saved with fewer loads with the rest zero, which fx_unpack reads as the format before */
#define FX_PACK     23
extern void fx_pack(const struct fx_effect *e, uint8_t *out);
extern void fx_unpack(const struct fx_effect *e, const uint8_t *in);
extern uint32_t fx_serial(const struct fx_effect *e);

/* The render's report: the whole render's share of a block, per mille (DMA timer 0), and 1 once the CPU guard
 * has bypassed the active effect (cleared by fx_activate, or by BYP) */
extern volatile uint32_t fx_load;
extern int32_t fx_overload(void);

#endif
