/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-ar2 0.18: knob flags (README, "Knob flags (0.18)"). A mod's own knob
 * id (555 up) has no stock parameter record, so stock reads record 0's flags
 * (+0x24, 0) for it: no own knob opens a destination list when it is turned
 * or pushed, as LFO DEST does. An entry in core_knob_flags gives the ids
 * flags to add to what stock reads:
 *
 *     {first, last, flags}
 *
 * Only CORE_KNOB_LIST (0x10000: a turn or a push opens the knob's list) is
 * honoured; the other bits are masked off until each one is traced. A stock
 * id (below 555) is never changed, nor an id without an entry. flags.s
 * takes stock's five readers of the bit (OS 1.74): the flags getter
 * 0x40007234 (the destination knobs' value checks, 0x400aa4ac and
 * 0x400aa648), and the four inline reads in the knob set's turn
 * (0x40039fbe, 0x4003a224), its push (0x4003a890) and the list's creation
 * (0x40039d94). */
#include <stdint.h>

#define PARAMS ((const uint8_t *)0x401bfbc8)  /* the parameter records, 52 bytes an id */
#define PARAM_SIZE 52
#define STOCK_IDS 469                   /* stock's records: 0..468 */
#define OWN_FIRST 555                   /* pages.h CORE_PARAM_LOCK_FIRST */
#define KNOWN 0x10000u                  /* pages.h CORE_KNOB_LIST */

struct core_knob_flag {                 /* pages.h */
    int32_t first, last;
    uint32_t flags;
};

extern const struct core_knob_flag *const core_knob_flags[];   /* zero-terminated */

/* the flags core adds for id: its entries' flags, the known bits only */
uint32_t core_knob_flags_of(int32_t id)
{
    const struct core_knob_flag *const *e;
    uint32_t f = 0;
    if (id < OWN_FIRST)
        return 0;
    for (e = core_knob_flags; *e; e++)
        if (id >= (*e)->first && id <= (*e)->last)
            f |= (*e)->flags;
    return f & KNOWN;
}

/* flags.s, from 0x40007234: stock's flags getter, record +0x24 of id
 * (record 0 above 468), with the id's entry flags */
uint32_t core_param_flags(int32_t id)
{
    const uint8_t *r = PARAMS + PARAM_SIZE * ((uint32_t)id < STOCK_IDS ? id : 0);
    return *(const uint32_t *)(r + 0x24) | core_knob_flags_of(id);
}
