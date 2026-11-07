/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-ar2 0.12: knob looks (README, "Knob looks (0.12)"). A mod's own knob
 * id (555 up, core_params / core_param_locks) has no stock records: stock
 * reads record 0 for it, so the knob draws as record 0's dial with record
 * 0's range, whatever it shows. An entry in core_knob_looks gives the ids
 * the look of a stock knob, or a UI record of the mod's own:
 *
 *     {first, last, look, ui, min, max}
 *
 * Stock finds a knob's look in two places, and core answers both for the
 * entry's ids:
 *
 *   - its UI record (84 bytes an id, built in RAM at boot from 0x419a4888),
 *     through 0x400ff5c2, uirec(id), the only reader of that table (12
 *     callers): the flags at +0 (which parts the knob draws), the turn's
 *     steps at +4..+0x10, and four delegates from +0x14 (the value text, the
 *     graphic: dial, switch, select box, bipolar dial and the others, the
 *     pop-up, the value pop-up's bar). The entry's ui(id), when it answers,
 *     else look's stock record;
 *   - its range (min, max and the third long of the parameter record's +8,
 *     12 bytes), through 0x40006ff0, range(id) (32 callers: the pop-ups and
 *     the header): look's, with min and max in its place when min < max.
 *
 * The label and the name stay the mod's (core_param_locks' label and name),
 * the value and its text the mod's (core_params), and what stock does with
 * a parameter (its sound slot, its lists, MIDI, the +0x24 flags) is not
 * taken from look: only how the knob is drawn. An id without an entry is
 * unchanged (record 0). */
#include <stdint.h>

#define UIRECS ((uint8_t *)0x419a4888)  /* stock's UI records, 84 bytes an id */
#define UIREC_SIZE 84
#define PARAMS ((const uint8_t *)0x401bfbc8)  /* the parameter records, 52 bytes an id */
#define PARAM_SIZE 52
#define STOCK_IDS 469                   /* stock's records: 0..468 */
#define OWN_FIRST 555                   /* pages.h CORE_PARAM_LOCK_FIRST */

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

/* looks.s, from 0x400ff5c2: uirec(id), into %d0 */
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

/* looks.s, from 0x40006ff0: range(id) into out (12 bytes: min, max and the
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
