/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * machines-ot (the Octatrack's machines companion, mods/machines-ot): a machine
 * mod adds a source machine to the track's machine chooser by contributing a
 * pointer to its `struct ot_machine` to the `ot_machines` table:
 *
 *     "contribute": [{"to": "ot_machines", "order": 50, "data": "00000000",
 *                     "relocs": [[0, "abs32", "sym:my_machine", 0]]}],
 *     "requires": ["core", "machines"]
 *
 * The record has the layout and the meaning of version 1 of the record of the
 * same name on Modwerk's Octatrack base (repeat98/modwerk), so a machine
 * written for one describes itself to the other the same way. This header is
 * elekloader's own.
 *
 * A machine's track stays a FLEX track (its machine byte is 1). What makes it
 * the machine is a signature in the Part: three bytes at Part + 60 + 30 * track
 * (NEIGHBOR's first three PLAYBACK bytes), two letters and a version, the form
 * octabam's machines write: ANALOG BD "AB" 1, OCTA303 "TB" 1, CXOSC "CX" 1.
 * Its parameters are FLEX's 12 bytes of the track: PLAYBACK at
 * Part + 0x2a + 30 * track + 6 .. +11, SETUP at Part + 0x1da + 30 * track + 6 .. +11.
 * The machine chooser lists it as row 5 + k, k its place in the table; rows
 * are never stored, so a project keeps its tracks when machines come or go.
 */
#ifndef ELEKLOADER_OCTATRACK_MACHINES_H
#define ELEKLOADER_OCTATRACK_MACHINES_H

#include <stdint.h>

#define OT_MACHINE_ABI          1u

#define OT_MACHINE_PROTECT      1u  /* stock's Part validator never sees its tracks' 12 FLEX bytes */
#define OT_MACHINE_POOL         2u  /* its row has a pool or engine list (not served by machines-ot 0.1) */
#define OT_MACHINE_RENDER_FLEX  4u  /* its render is also offered the FLEX tracks no machine owns */
#define OT_MACHINE_BACKING      8u  /* its page is STATIC's on a STATIC backing pool (not served by 0.1) */

struct ot_machine {
    uint32_t abi;                       /*  0 OT_MACHINE_ABI */
    uint8_t sig[2], sig_lo, sig_hi;     /*  4 the signature's two letters, and the versions it takes */
    const char *label;                  /*  8 its name in the chooser and SRC SETUP */
    uint32_t flags;                     /* 12 OT_MACHINE_* */
    uint32_t page;                      /* 16 its PLAYBACK page (a FLEX descriptor's form); 0: FLEX's */
    const uint8_t *defaults;            /* 20 the 12 FLEX bytes a track takes when it becomes this machine */
    /* 24 signs (on) and unsigns (off) its own tracks, in place of the companion; -> the machine byte */
    unsigned (*assign)(volatile uint8_t *part, unsigned track, unsigned on);
    unsigned (*admit_track)(const volatile uint8_t *part, unsigned track);   /* 28 0 refuses the track */
    unsigned (*chooser_type)(unsigned type, const volatile uint8_t *slot);   /* 32 not 5: the row is not shown as the track's */
    uint32_t (*track_page)(const volatile uint8_t *slot);                    /* 36 the track's page, in place of `page` */
    void (*ui_tick)(void);                                                   /* 40 60 times a second, in the UI task */
    void (*leave)(unsigned track);                                           /* 44 the track becomes another machine */
    void (*pool_open)(void);                                                 /* 48 not served by 0.1 */
    void (*pool_left)(unsigned key, unsigned edge);                          /* 52 not served by 0.1 */
    void (*choice_left)(unsigned key, unsigned edge);                        /* 56 not served by 0.1 */
    const void *list_draw;                                                   /* 60 not served by 0.1 */
    /* 64 in place of the FLEX renderer for its tracks: call ot_stock_render with the same
     * arguments, hand back its result, and change the samples it shipped (ot_render_header) */
    int (*render)(unsigned track, unsigned ping, unsigned start, unsigned end);
    int (*validate)(uint8_t *part, int (*next)(uint8_t *part));              /* 68 wraps the Part validator: call next */
    uint16_t source_voice, source_engine;                                    /* 72 a DSP voice's price: 0.1 skips a machine */
    uint8_t source_kind, reserved[3];                                        /* 76 that has one (no DSP source stage yet) */
};

/* What machines-ot exports to machine mods. */
int ot_stock_render(unsigned track, unsigned ping, unsigned start, unsigned end);  /* the FLEX renderer */
volatile uint8_t *ot_part(void);                    /* the current Part (Part'), 0 before a project */
const struct ot_machine *ot_track_machine(unsigned track);   /* the current Part's track's machine, or 0 */
unsigned ot_machine_row(const struct ot_machine *m); /* its chooser row (5 on), 0 when it is not listed */

/* The header a render call writes (*0x80001c80): byte 3 = the samples it shipped, then from
 * +16 each sample as two longs, L and R, the 16-bit sample in the high half. */
static inline volatile uint8_t *ot_render_header(void)
{
    return *(volatile uint8_t *volatile *)0x80001c80u;
}

/* A track's FLEX bytes in a Part: PLAYBACK k = 0..5, SETUP k = 6..11. */
static inline volatile uint8_t *ot_flex_byte(volatile uint8_t *part, unsigned track, unsigned k)
{
    return part + (k < 6 ? 0x2au : 0x1dau) + 30u * track + 6u + k % 6u;
}

#endif
