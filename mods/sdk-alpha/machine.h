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
    uint32_t pages;                     /* pages 0..pages-1 */
    int8_t encoder[2][8];               /* the knob's id, or -1 */
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
/* One block of voice `slot`: start latches v->start until a block is rendered; fills v's knob, start
 * and state, mapping raw[0..count-1] as sdk_alpha_present does. */
void sdk_alpha_voice(const struct sdk_alpha_entry *e, uint32_t slot, struct elek_voice *v,
                     const int32_t *raw, int start, elek_sample *out);
#endif
