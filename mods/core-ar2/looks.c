/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-ar2 0.13: knob looks (README, "Knob looks (0.13)"). A mod's own knob
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
 * unchanged (record 0).
 *
 * (0.14) A stock id whose record a core_machines slot fills (machines.c)
 * takes the look its knob entry names (machines.h, CORE_MACHINE_LOOKS):
 * uirec answers look's UI record (or the entry's ui), the range stays the
 * record's own (core wrote the knob's min..max into it), and the graphic
 * spans that range on look's scale, as an entry with min < max does. */
#include <stdint.h>
#include "analog-rytm-mk2/machines.h"

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

/* sidecar-smpl-machines: a stock knob's face, per call (pages.h). A stock id
 * (1..468) a core_knob_faces entry answers for is drawn, labelled, named and
 * ranged as the face its function returns for the sound: a framework mod
 * whose own machine takes over stock records (smpl-machines' SMPL machines on
 * records 39..46) shows that machine's knobs on stock's page, while the
 * records, so the sound's slots, its p-locks, slides, LFO, MIDI and
 * storage, stay stock's. 0 from the function (or no entry): stock's. */
struct core_knob_face {
    const char *label, *name;
    int32_t look;
    const void *ui;
    int32_t min, max, def;
};

struct core_knob_faces {
    int32_t first, last;
    const struct core_knob_face *(*face)(int32_t id, void *sound);
};

extern const struct core_knob_faces *const core_knob_faces[];   /* zero-terminated */

/* the face of id for sound (a live sound item; 0: the current track's), or
 * 0 for stock's. lock.c asks it for the label and the name. (0.14) An own
 * id (555 up) takes a face too: a framework mod's knob whose value it keeps
 * itself (smpl-machines' knob C) shows the machine's label, name, look and
 * range, as a stock id under a face does. */
const struct core_knob_face *core_knob_face(int32_t id, void *sound)
{
    const struct core_knob_faces *const *e;
    const struct core_knob_face *f;
    if (id <= 0 || (id >= STOCK_IDS && id < OWN_FIRST))
        return 0;
    for (e = core_knob_faces; *e; e++)
        if (id >= (*e)->first && id <= (*e)->last && (*e)->face && (f = (*e)->face(id, sound)) != 0)
            return f;
    return 0;
}

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

const struct core_machine_look *core_machine_look_of(int32_t id);   /* machines.c */

/* a stock id's own UI record, record 0 for any other (stock's lookup) */
const void *core_knob_stock_ui(int32_t id)
{
    return UIRECS + UIREC_SIZE * ((uint32_t)id < STOCK_IDS ? id : 0);
}

/* looks.s, from 0x400ff5c2: uirec(id), into %d0 */
uint32_t core_uirec(int32_t id)
{
    const struct core_knob_look *l;
    const struct core_knob_face *f;
    const void *p;
    const struct core_machine_look *ml;
    if ((f = core_knob_face(id, 0)) != 0 && ((uint32_t)id < STOCK_IDS || f->ui || f->look)) {
        if (f->ui)
            return (uint32_t)f->ui;
        return (uint32_t)core_knob_stock_ui(f->look ? f->look : id);
    }
    if ((uint32_t)id < STOCK_IDS) {     /* (0.14) a machine knob's look, under a face */
        if (!(ml = core_machine_look_of(id)))
            return (uint32_t)core_knob_stock_ui(id);
        if (ml->ui && (p = ml->ui(id)) != 0)
            return (uint32_t)p;
        return (uint32_t)core_knob_stock_ui(ml->look ? ml->look : id);
    }
    if (!(l = look_of(id)))
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
    const struct core_knob_face *f = core_knob_face(id, 0);
    int32_t from = f && f->look && (uint32_t)id >= STOCK_IDS ? f->look : l ? l->look : id;
    const int32_t *r = (const int32_t *)(PARAMS + 8 + PARAM_SIZE * ((uint32_t)from < STOCK_IDS ? from : 0));
    out[0] = r[0];
    out[1] = r[1];
    out[2] = r[2];
    if (l && l->min < l->max) {
        out[0] = l->min;
        out[1] = l->max;
    }
    if (f && f->min < f->max) {         /* a face's range and default, the id's own record otherwise */
        out[0] = f->min;
        out[1] = f->max;
        out[2] = f->def;
    }
}

/* looks.s, from 0x400a85e8, a knob's graphic (set, id, value, ...): the
 * UI record's delegate at +0x24 draws value on the scale of the parameter
 * it was made for (a dial: 0..127). For an entry with min < max, value
 * moves from min..max onto look's range (0..127 where look's is 0..0), so
 * the graphic spans the knob's own range: min at its start, max at its end. */
int32_t core_knob_dial(int32_t id, int32_t value)
{
    const struct core_knob_look *l = (uint32_t)id < STOCK_IDS ? 0 : look_of(id);
    const struct core_knob_face *f = core_knob_face(id, 0);
    const struct core_machine_look *ml = (uint32_t)id < STOCK_IDS && !f ? core_machine_look_of(id) : 0;
    const int32_t *r;
    int32_t lo, hi, min, max, look;
    uint32_t span, off, out;
    int sh;
    if (l && l->min < l->max) {
        min = l->min;
        max = l->max;
        look = l->look;
    } else if (f && f->min < f->max) {  /* a face: onto its look's range, or the id's own */
        min = f->min;
        max = f->max;
        look = f->look ? f->look : id;
    } else if (ml && ml->look && ml->look != id) {  /* (0.14) a machine knob's look: its own range onto look's */
        r = (const int32_t *)(PARAMS + 8 + PARAM_SIZE * id);
        min = r[0];
        max = r[1];
        look = ml->look;
        if (min >= max)
            return value;
    } else
        return value;
    r = (const int32_t *)(PARAMS + 8 + PARAM_SIZE * ((uint32_t)look < STOCK_IDS ? look : 0));
    lo = r[0];
    hi = r[1];
    if (lo >= hi) {
        lo = 0;
        hi = 127 << 8;
    }
    if (lo == min && hi == max)
        return value;
    if (value <= min)
        return lo;
    if (value >= max)
        return hi;
    off = (uint32_t)(value - min);              /* 0 < off < span */
    span = (uint32_t)(max - min);
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
