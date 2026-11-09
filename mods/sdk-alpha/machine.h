/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The SDK alpha's machines (elekloader/sdk_alpha_machine.h), the part every companion shares
 * (machine.c). Each companion builds machine.c with its own device layer (dev.h): machine-pages on
 * the Digitakt mk1, machines-dn1 on the Digitone mk1, machines-dn2 on the Digitone II. */
#ifndef SDK_ALPHA_SHARED_H
#define SDK_ALPHA_SHARED_H

#include "elekloader/sdk_alpha_machine.h"

#define SDK_ALPHA_KNOBS ELEK_UI_KNOB_MAX
#define SDK_ALPHA_RAW_MAX (127 << 8)    /* a knob's native 8.8 range, 0..32512, whatever its bounds */

/* A machine, indexed once: its knobs' ids by page and encoder. */
struct sdk_alpha_entry {
    const struct elek_machine *machine;
    uint32_t count;                     /* knobs 0..count-1 */
    uint32_t pages;                     /* pages 0..pages-1: one blank page when it declares none */
    const char *title[ELEK_UI_PAGE_MAX]; /* each page's: its label, or the blank page's machine name */
    int8_t encoder[ELEK_UI_PAGE_MAX][8]; /* the knob's id, or -1 */
};
extern struct sdk_alpha_entry sdk_alpha_entries[];
extern int32_t sdk_alpha_count;         /* -1 until sdk_alpha_init has run */

/* Index the device's machines (dev.h's dev_candidate), once; safe to reenter. */
void sdk_alpha_init(void);
/* The machine with native id `id`, or 0. */
const struct sdk_alpha_entry *sdk_alpha_find(int32_t id);

/* A native 8.8 value (clamped to 0..SDK_ALPHA_RAW_MAX) as the knob's mapped value. */
int32_t sdk_alpha_map(const struct elek_knob *knob, int32_t raw);
/* The first native step whose mapped value reaches the knob's default. */
int32_t sdk_alpha_default(const struct elek_knob *knob);
/* A turn of a knob with clicks: the stock's 8.8 delta (its acceleration included) for the knob at raw, as
 * the delta that moves it that many clicks over its values x clicks points, stopping at the ends; delta
 * unchanged for a knob without clicks. */
int32_t sdk_alpha_step(const struct elek_knob *knob, int32_t raw, int32_t delta);
/* A knob's value text: the decimal number (12 bytes at most); unless wide, "#" when it needs more than
 * SDK_ALPHA_VALUE_MAX characters, which is what fits under a knob. */
#define SDK_ALPHA_VALUE_MAX 5
void sdk_alpha_number(int32_t value, char *buf, int wide);
/* Knob k's value text for native value raw (ELEK_UI_LABEL_MAX characters, or a wide number): the view's
 * value from .present with knob k at raw and the others, track and page as it last saw them for e (else
 * the defaults, track 0, ELEK_UI_NO_PAGE), or else sdk_alpha_number of the mapped value. */
void sdk_alpha_value(const struct sdk_alpha_entry *e, uint32_t k, int32_t raw, char *buf, int wide);
/* On a device whose value formatter f(raw, buf) is given no knob (dev.h defines dev_active): knob k's
 * sdk_alpha_value, for the active track's machine; the native number for another machine. */
extern void (*const sdk_alpha_value_texts[SDK_ALPHA_KNOBS])(int32_t raw, char *buf);

/* What .present last left, and for which machine, track and page. */
extern char sdk_alpha_labels[SDK_ALPHA_KNOBS][ELEK_UI_LABEL_MAX + 1];
extern uint8_t sdk_alpha_visibility[SDK_ALPHA_KNOBS];
extern const struct elek_glyph *sdk_alpha_glyphs[SDK_ALPHA_KNOBS];
extern const struct sdk_alpha_entry *sdk_alpha_seen;   /* set 0 to present again */
extern int32_t sdk_alpha_seen_page;

/* Calls .present with the knobs' native values raw[0..count-1] when the machine, track, page or a
 * value changed since the last call (or force); a page the machine lacks is ELEK_UI_NO_PAGE.
 * -> nonzero when a label, visibility or glyph changed. */
int32_t sdk_alpha_present(const struct sdk_alpha_entry *e, uint32_t track, int32_t page,
                          const int32_t *raw, int force);
/* Knob k as .present last left it for e, else as declared; 0 past the machine's knobs. */
const char *sdk_alpha_label(const struct sdk_alpha_entry *e, uint32_t k);
const struct elek_glyph *sdk_alpha_glyph(const struct sdk_alpha_entry *e, uint32_t k);
int32_t sdk_alpha_visible(const struct sdk_alpha_entry *e, uint32_t k);

/* Clears (x, y)-(x + 16, y + 16) with dev.h's dev_fillrect, then lights the glyph's pixels, its top
 * row at y + 16. */
void sdk_alpha_blit(void *bmp, int32_t x, int32_t y, const struct elek_glyph *glyph);

/* The voices, on a device that renders (dev.h's SDK_ALPHA_VOICES). */
/* The machine id voice `slot` plays: another id zeroes its state and drops a pending start. */
void sdk_alpha_select(uint32_t slot, int32_t id);
/* One block of voice `slot`: start latches v->start until a block is rendered; fills v's knob, start,
 * state, idle (the companion's count of frames not rendered since the last render; 0 in the state's
 * first render) and cc, the CCs on v->track since the slot's last render, mapping raw[0..count-1] as
 * sdk_alpha_present does. */
void sdk_alpha_voice(const struct sdk_alpha_entry *e, uint32_t slot, struct elek_voice *v,
                     const int32_t *raw, int start, uint32_t idle, elek_sample *out);
/* Swaps two slots' machine and state: a mono track's sound moving to another voice. */
void sdk_alpha_swap(uint32_t a, uint32_t b);

/* Mono machines (.play), on a device whose core has ev_live_note (dev.h's SDK_ALPHA_TRACKS). The handler:
 * every live key on a track whose machine's .play is not ELEK_POLY is taken, and the stock plays only the
 * note .prio chooses, through dev_note_on and dev_note_off, so it holds one key at a time. */
int32_t sdk_alpha_live_note(int32_t track, int32_t note, int32_t vel, int32_t on, int32_t source);
/* The pitch the track's voice renders for machine id, from the stock's: legato moves it. */
int32_t sdk_alpha_pitch(uint32_t track, int32_t id, int32_t pitch);
/* ev_midi_cc's handler (dev.h's SDK_ALPHA_TRACKS: the tracks with voices, from 0): records the CC for
 * the voices of the track to read in v->cc. -> 0: the stock applies it as before. */
int32_t sdk_alpha_midi_cc(int32_t track, int32_t cc, int32_t value, int32_t flags);
#endif
