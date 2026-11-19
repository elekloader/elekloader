/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-a4mk2 0.6: knob looks (README, "Knob looks (0.6)"), core-ar2 0.13's
 * with the A4's addresses (OS 1.56). A mod's own knob id (above 0x126,
 * core_params, lock.h) has no stock records: stock reads record 0 for it,
 * so the knob draws as record 0's dial with record 0's range, whatever it
 * shows. An entry in core_knob_looks gives the ids
 * the look of a stock knob, or a UI record of the mod's own:
 *
 *     {first, last, look, ui, min, max}
 *
 * Stock finds a knob's look in two places, and core answers both for the
 * entry's ids:
 *
 *   - its UI record (84 bytes an id, built in RAM at boot from 0x41856c44),
 *     through 0x400f6232, uirec(id), the only reader of that table (18
 *     callers): the flags at +0 (which parts the knob draws), the turn's
 *     steps at +4..+0x10, and four delegates from +0x14 (the value text, the
 *     graphic: dial, switch, select box, bipolar dial and the others, the
 *     pop-up, the value pop-up's bar). The entry's ui(id), when it answers,
 *     else look's stock record;
 *   - its range (min, max and the third long of the parameter record's +8,
 *     12 bytes; the A4's records are 56 bytes), through 0x40007798,
 *     range(id) (81 callers: the pop-ups and the header): look's, with
 *     min and max in its place when min < max.
 *
 * The label and the name stay the mod's, the value and its text the mod's
 * (core_params), and what stock does with a parameter (its sound slot, its
 * lists, MIDI, its flags) is not taken from look: only how the knob is
 * drawn. An id without an entry is
 * unchanged (record 0). */
#include <stdint.h>

#define UIRECS ((uint8_t *)0x41856c44)  /* stock's UI records, 84 bytes an id */
#define UIREC_SIZE 84
#define PARAMS ((const uint8_t *)0x40186af4)  /* the parameter records, 56 bytes an id */
#define PARAM_SIZE 56
#define STOCK_IDS 0x127                 /* stock's records: 0..0x126 */
#define OWN_FIRST STOCK_IDS             /* a mod's own ids: above stock's (lock.h) */

struct core_knob_look {                 /* pages.h */
    int32_t first, last;
    int32_t look;
    const void *(*ui)(int32_t id);
    int32_t min, max;
};

extern const struct core_knob_look *const core_knob_looks[];   /* zero-terminated */

static const struct core_knob_look *look_of(int32_t id)
{
    const struct core_knob_look *const *e;
    if (id < OWN_FIRST)
        return 0;
    for (e = core_knob_looks; *e; e++)
        if (id >= (*e)->first && id <= (*e)->last)
            return *e;
    return 0;
}

/* a stock id's own UI record, record 0 for any other (stock's lookup) */
const void *core_knob_stock_ui(int32_t id)
{
    return UIRECS + UIREC_SIZE * ((uint32_t)id < STOCK_IDS ? id : 0);
}

/* looks.s, from 0x400f6232: uirec(id), into %d0 */
uint32_t core_uirec(int32_t id)
{
    const struct core_knob_look *l;
    const void *p;
    if ((uint32_t)id < STOCK_IDS || !(l = look_of(id)))
        return (uint32_t)core_knob_stock_ui(id);
    if (l->ui && (p = l->ui(id)) != 0)
        return (uint32_t)p;
    return (uint32_t)core_knob_stock_ui(l->look);
}

/* looks.s, from 0x40007798: range(id) into out (12 bytes: min, max and the
 * record's third long, 8.8) */
void core_param_range(int32_t *out, int32_t id)
{
    const struct core_knob_look *l = (uint32_t)id < STOCK_IDS ? 0 : look_of(id);
    int32_t from = l ? l->look : id;
    const int32_t *r = (const int32_t *)(PARAMS + 8 + PARAM_SIZE * ((uint32_t)from < STOCK_IDS ? from : 0));
    out[0] = r[0];
    out[1] = r[1];
    out[2] = r[2];
    if (l && l->min < l->max) {
        out[0] = l->min;
        out[1] = l->max;
    }
}

/* looks.s, from 0x4009eaaa, a knob's graphic (set, id, value, ...; the
 * parameter sets' vtable +0x84): the UI record's delegate draws value on the scale of the parameter
 * it was made for (a dial: 0..127). For an entry with min < max, value
 * moves from min..max onto look's range (0..127 where look's is 0..0), so
 * the graphic spans the knob's own range: min at its start, max at its end. */
int32_t core_knob_dial(int32_t id, int32_t value)
{
    const struct core_knob_look *l = (uint32_t)id < STOCK_IDS ? 0 : look_of(id);
    const int32_t *r;
    int32_t lo, hi;
    uint32_t span, off, out;
    int sh;
    if (!l || l->min >= l->max)
        return value;
    r = (const int32_t *)(PARAMS + 8 + PARAM_SIZE * ((uint32_t)l->look < STOCK_IDS ? l->look : 0));
    lo = r[0];
    hi = r[1];
    if (lo >= hi) {
        lo = 0;
        hi = 127 << 8;
    }
    if (value <= l->min)
        return lo;
    if (value >= l->max)
        return hi;
    off = (uint32_t)(value - l->min);           /* 0 < off < span */
    span = (uint32_t)(l->max - l->min);
    out = (uint32_t)(hi - lo);
    sh = 0;
    while (out > 0xffff) {                      /* off * out in 32 bits */
        out >>= 1;
        sh++;
    }
    while (off > 0xffff) {
        off >>= 1;
        span >>= 1;
    }
    return lo + (int32_t)((off * out / (span ? span : 1)) << sh);
}
