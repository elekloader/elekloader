/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-ar2 0.8: lockable mod parameters, the first part of the lock service
 * (README, "Lockable parameters"). Every address is OS 1.74's.
 *
 * The linker gives core the table core_lock_codes, {field descriptor, code}
 * for each field declared "lock": true, from the lockfile, with LFO (0x100)
 * set in the code of a field also declared "lfo": true, and mods add
 * {field, id, min, max, short name, name, category} to core_lock_params. A parameter
 * with a code and an entry is lockable; one with LFO set also gets:
 *
 *   - a pseudo id, PSEUDO + code - 42, above stock's 469 records: the LFO
 *     destination list (0x400aa280) holds parameter ids, and its users turn
 *     them back into a sound slot (0x40006ea8) and names (the record table);
 *     the code is the slot, so the LFO's DST byte holds it;
 *   - in each sound's destination list, after stock's (lock.s); the menu's
 *     label and the DST value show its names; the step, the validity check,
 *     page turns and RANDOMIZE on DST follow from the list with no hooks;
 *   - the LFO's offset: where the audio interrupt's LFO block skips a slot
 *     above 41 (0x4009eac8), core keeps the track's code and the offset
 *     stock would have added (lock.s), and core_lfo_offset hands it on;
 *
 * and any of them, with a page id, gets RANDOMIZE PAGE and RELOAD PAGE on
 * its knob (lock.s), and its short name as the knob's label.
 *
 * core-ar2 0.9: one with ROUTE (0x400, "mod": true) set gets the same pseudo
 * id in each sound's VELOCITY MOD and AFTERTOUCH lists, which stock builds
 * with the same routine (kind 0x200); a route keeps the code in its own
 * byte, and routes.c applies it.
 */

#include <stdint.h>

#define PSEUDO 469
#define FIRST_CODE 42
#define LAST_CODE 127
#define LFO 0x100                             /* in core_lock_codes: an LFO destination */
#define ROUTE 0x400                           /* in core_lock_codes: a velocity/aftertouch destination */
#define SOUND_SLOT_ID ((int (*)(void *, int))0x400a86dc)   /* sound vtable +76, stock's */
#define LIST_APPEND ((void (*)(void *, int *))0x40197a94)   /* (vector, &id) */
#define RANDOM ((unsigned (*)(void))0x4002a6e0)   /* as stock's randomize uses */

struct core_lock_param {
    unsigned field;
    int32_t id;
    int16_t min, max;
    const char *short_name;
    const char *name;
    const char *category;
};

extern const unsigned core_lock_codes[];                       /* {field, code}..., {0, 0} */
extern const struct core_lock_param *const core_lock_params[]; /* zero-terminated */

unsigned core_store_get(unsigned field, void *item, int index);
void core_store_set(unsigned field, void *item, int index, unsigned value);
void core_project_edited(void);

/* the audio interrupt's view, per track: the LFO's mod code (0: a stock
 * destination) and the offset (lock.s writes both) */
volatile unsigned char core_lfo_code[13];
volatile int16_t core_lfo_value[13];

/* each code's parameter, made once, on the first ask */
static const struct core_lock_param *by_code[LAST_CODE + 1];
static unsigned char lfo_ok[LAST_CODE + 1];
static unsigned char route_ok[LAST_CODE + 1];
static volatile int made;

/* and its strings, cut to what stock shows: stock draws a short name with a
 * plain %s into a 128-byte stack buffer (0x4007154c, by 0x40097dcc), so no
 * string of a mod's reaches stock uncut */
#define SHORT_MAX 4
#define NAME_MAX 32
#define CATEGORY_MAX 4
static char shorts[LAST_CODE + 1 - FIRST_CODE][SHORT_MAX + 1];
static char names[LAST_CODE + 1 - FIRST_CODE][NAME_MAX + 1];
static char categories[LAST_CODE + 1 - FIRST_CODE][CATEGORY_MAX + 1];

static void cut(char *to, const char *from, int max)
{
    int i = 0;
    if (from)
        for (; i < max && from[i]; i++)
            to[i] = from[i];
    to[i] = 0;
}

static void make(void)
{
    const unsigned *c;
    const struct core_lock_param *const *p;
    unsigned code;
    for (c = core_lock_codes; c[0]; c += 2)
        if ((code = c[1] & 0xff) >= FIRST_CODE && code <= LAST_CODE)
            for (p = core_lock_params; *p; p++)
                if ((*p)->field == c[0]) {
                    by_code[code] = *p;
                    lfo_ok[code] = !!(c[1] & LFO);
                    route_ok[code] = !!(c[1] & ROUTE);
                    cut(shorts[code - FIRST_CODE], (*p)->short_name, SHORT_MAX);
                    cut(names[code - FIRST_CODE], (*p)->name, NAME_MAX);
                    cut(categories[code - FIRST_CODE], (*p)->category, CATEGORY_MAX);
                    break;
                }
    made = 1;
}

static const struct core_lock_param *param(unsigned code)
{
    if (!made)
        make();
    return code >= FIRST_CODE && code <= LAST_CODE ? by_code[code] : 0;
}

/* an LFO destination's parameter, or 0 */
static const struct core_lock_param *lfo_param(unsigned code)
{
    return param(code) && lfo_ok[code] ? by_code[code] : 0;
}

/* a velocity and aftertouch destination's parameter, or 0 */
static const struct core_lock_param *route_param(unsigned code)
{
    return param(code) && route_ok[code] ? by_code[code] : 0;
}

/* a parameter that has a pseudo id: in some destination list */
static const struct core_lock_param *listed_param(unsigned code)
{
    return lfo_param(code) ? by_code[code] : route_param(code);
}

int core_lock_code(unsigned field)
{
    const unsigned *c;
    for (c = core_lock_codes; c[0]; c += 2)
        if (c[0] == field)
            return c[1] & 0xff;
    return 0;
}

int core_lfo_offset(int track, unsigned field)
{
    unsigned code;
    if ((unsigned)track >= 13 || !(code = core_lfo_code[track]) || code != (unsigned)core_lock_code(field) ||
        !lfo_param(code))
        return 0;
    return core_lfo_value[track];
}

/* a pseudo id's code, or 0 */
static unsigned pseudo_code(int id)
{
    unsigned code = id - PSEUDO + FIRST_CODE;
    return id >= PSEUDO && listed_param(code) ? code : 0;
}

/* sound vtable +76 (ptr site): a slot that is a mod parameter's code gives
 * its pseudo id, any other slot stock's id */
int core_dst_slot_id(void *set, int slot)
{
    if (slot >= FIRST_CODE && slot <= LAST_CODE && listed_param(slot))
        return PSEUDO + slot - FIRST_CODE;
    return SOUND_SLOT_ID(set, slot);
}

/* 0x40006ea8 (lock.s): a pseudo id's slot, its code; -1 (record 0's) for one
 * no parameter has */
int core_pseudo_slot(int id)
{
    unsigned code = pseudo_code(id);
    return code ? (int)code : -1;
}

/* the destination list's end (lock.s), for a sound's parameter set: the mod
 * parameters, by code; the LFO's list (kind 0x600, 0x400 set) those with
 * LFO, the velocity and aftertouch lists (kind 0x200) those with ROUTE */
void core_dst_append(void **list, unsigned kind)
{
    int code, id;
    for (code = FIRST_CODE; code <= LAST_CODE; code++)
        if (kind & 0x400 ? lfo_param(code) != 0 : route_param(code) != 0) {
            id = PSEUDO + code - FIRST_CODE;
            LIST_APPEND(*list, &id);
        }
}

/* the menu label's and the DST value's names (lock.s), or 0 */
const char *core_pseudo_name(int id)
{
    unsigned code = pseudo_code(id);
    return code && param(code)->name ? names[code - FIRST_CODE] : 0;
}

const char *core_pseudo_short(int id)
{
    unsigned code = pseudo_code(id);
    return code && param(code)->short_name ? shorts[code - FIRST_CODE] : 0;
}

/* the menu label's category (lock.s): the parameter's, at most 4
 * characters, or MOD when it has none */
const char *core_pseudo_category(int id)
{
    unsigned code = pseudo_code(id);
    return code && categories[code - FIRST_CODE][0] ? categories[code - FIRST_CODE] : "MOD";
}

/* ---- RANDOMIZE PAGE and RELOAD PAGE on a mod parameter's knob ---- */

const struct core_lock_param *core_lock_by_id(int id, unsigned *code);

static const struct core_lock_param *by_id(int id)
{
    unsigned code;
    return core_lock_by_id(id, &code);
}

/* for rows.c: a page id's parameter and its code */
const struct core_lock_param *core_lock_by_id(int id, unsigned *code)
{
    if (!id)
        return 0;
    for (*code = FIRST_CODE; *code <= LAST_CODE; ++*code)
        if (param(*code) && param(*code)->id == id)
            return param(*code);
    return 0;
}

/* for midi.c: a code's parameter, or 0 */
const struct core_lock_param *core_lock_by_code(unsigned code)
{
    return param(code);
}

/* for routes.c and store.c: a velocity and aftertouch destination's
 * parameter, or 0 */
const struct core_lock_param *core_lock_route(unsigned code)
{
    return route_param(code);
}

/* for store.c: an LFO destination's parameter, or 0 */
const struct core_lock_param *core_lock_lfo(unsigned code)
{
    return lfo_param(code);
}

unsigned core_lock_field(const struct core_lock_param *p) { return p->field; }
int core_lock_min(const struct core_lock_param *p) { return p->min; }

/* the most a lock holds: max, at most 16383 (14 bits, an NRPN's value) */
#define TOP 16383
int core_lock_max(const struct core_lock_param *p) { return p->max > TOP ? TOP : p->max; }

/* a parameter above 127's coarse step, as a shift: scene locks, performance
 * depths and CCs hold value >> shift, 0..127, as stock's fine parameters
 * keep only their whole steps there; p-locks and NRPNs the whole value */
int core_lock_shift(const struct core_lock_param *p)
{
    int s = 0;
    while (core_lock_max(p) >> s > 127)
        s++;
    return s;
}

#define LIVE_KITS 0x412558e3                  /* the live project +0xa8cf18: the kits as edited */
#define LIVE_KITS_SAVED 0x4f180                 /* then the kits as saved */
#define LIVE_KIT_SIZE 2531
#define WORKING_KIT_INDEX ((const volatile unsigned char *)0x405c8f20 + 0x19d3f7)

/* a live sound of an edited kit, or 0 */
static unsigned char *edited_sound(void *p)
{
    unsigned o = (unsigned char *)p - (unsigned char *)LIVE_KITS - 42;
    if ((unsigned)p < LIVE_KITS + 42 || o / LIVE_KIT_SIZE >= 128 || o % LIVE_KIT_SIZE % 152 ||
        o % LIVE_KIT_SIZE / 152 >= 12)
        return 0;
    return p;
}

/* a parameter set's sound: its object (+24) answers it (vtable +40), as the
 * sound setter (0x400a9dea) asks it; only a sound of an edited kit counts.
 * Stock returns it in %d0, so the call is typed as returning a number (gcc
 * reads a returned pointer from %a0). */
unsigned char *core_lock_sound(void *set)
{
    void *obj = set ? *(void **)((char *)set + 24) : 0;
    if (!obj)
        return 0;
    return edited_sound((void *)(*(unsigned (**)(void *))(*(char **)obj + 40))(obj));
}

/* would the sound setter (0x400a9dea) take this id for this sound? Its
 * slot is a stock one (0x40006ea8, which core answers for pseudo ids), and
 * the parameter is common to all machines (0x40006f18) or the sound's
 * machine's (0x4000714a against the sound's +104). A mod page's knob borrows
 * an id stock rejects here (an Error record, another machine's parameter);
 * where the id is the sound's own stock parameter, the knob is stock's. */
#define ID_SLOT ((int (*)(int))0x40006ea8)
#define ID_COMMON ((int (*)(int))0x40006f18)
#define ID_MACHINE ((int (*)(int))0x4000714a)

int core_lock_stock_takes(int id, const unsigned char *s)
{
    int slot = ID_SLOT(id);
    if (slot < 0 || slot > 41)
        return 0;
    return ID_COMMON(id) || ID_MACHINE(id) == (signed char)s[104];
}

/* 0x400aa32a (lock.s): a mod parameter's knob gets a random value in its
 * range in the set's sound; 1 when it was one */
int core_lock_rand(void *set, int id)
{
    const struct core_lock_param *p = by_id(id);
    unsigned char *s;
    unsigned span;
    if (!p || !(s = core_lock_sound(set)) || core_lock_stock_takes(id, s))
        return 0;
    span = p->max - p->min + 1;
    core_store_set(p->field, s, 0, p->min + (RANDOM() & 0x7fff) % span);
    core_project_edited();
    return 1;
}

/* 0x400af32c: RELOAD PAGE. After stock's copy, each mod parameter's knob
 * among the ids takes its value back from the track's sound in the saved
 * kit (the current kit's saved copy) */
extern int core_reload_body(void *view, int track, int **ids, int d);

int core_reload(void *view, int track, int **ids, int d)
{
    int r = core_reload_body(view, track, ids, d), *id;
    unsigned k = (unsigned)WORKING_KIT_INDEX[0] << 24 | (unsigned)WORKING_KIT_INDEX[1] << 16 |
                 (unsigned)WORKING_KIT_INDEX[2] << 8 | WORKING_KIT_INDEX[3];
    unsigned char *s;
    const struct core_lock_param *p;
    if (!ids || k >= 128 || (unsigned)track >= 12)
        return r;
    s = (unsigned char *)LIVE_KITS + LIVE_KIT_SIZE * k + 42 + 152 * track;
    for (id = ids[0]; id && id < ids[1]; id++)
        if ((p = by_id(*id)) != 0 && !core_lock_stock_takes(*id, s)) {
            core_store_set(p->field, s, 0, core_store_get(p->field, s + LIVE_KITS_SAVED, 0));
            core_project_edited();
        }
    return r;
}

/* 0x400a9000, a knob's label (set, id): the record's +0x30, called by the
 * page's two knob draws (0x40037c1a, 0x40037d42) and the knob's pop-up
 * (0x400d37fa). A mod parameter's knob shows its short name; where stock
 * takes the id for the set's sound, the knob is stock's and so is its label.
 * The callers read the string from %d0, so it is typed as a number. */
#define KNOB_LABEL ((unsigned (*)(void *, int))0x400a9000)

unsigned core_knob_label(void *set, int id)
{
    unsigned code;
    const struct core_lock_param *p = core_lock_by_id(id, &code);
    unsigned char *s;
    if (p && p->short_name && !((s = core_lock_sound(set)) && core_lock_stock_takes(id, s)))
        return (unsigned)shorts[code - FIRST_CODE];
    return KNOB_LABEL(set, id);
}

/* param_value_text's last word (pages.s, after the mods' text entries
 * passed): a mod parameter's value, with a trig held its lock's, as a
 * number. Stock would check the id against the set (vtable +36) and, for the
 * id a mod page borrows (an Error record, another machine's parameter),
 * write ERR. Where stock takes the id for the set's sound, it is stock's. */
int core_lock_text(void *set, int id, int value, char *buf)
{
    const struct core_lock_param *p = by_id(id);
    unsigned char *s;
    unsigned v, d;
    int n = 0, digits = 0;
    if (!p || ((s = core_lock_sound(set)) && core_lock_stock_takes(id, s)))
        return 0;
    if (value < 0) {
        buf[n++] = '-';
        value = -value;
    }
    v = (unsigned)value >> 8;
    for (d = 10000; d > 1; d /= 10)
        if (v >= d || digits) {
            buf[n++] = '0' + v / d % 10;
            digits = 1;
        }
    buf[n++] = '0' + v % 10;
    buf[n] = 0;
    return 1;
}

/* 0x400a9022, a knob's name (set, id): the record's +0x28, called by the
 * header's pop-up as a knob turns (0x4003a1c6, 0x4003a8d6), which formats
 * it. A mod parameter's knob shows its name, as core_knob_label its short
 * name. */
#define KNOB_NAME ((unsigned (*)(void *, int))0x400a9022)

unsigned core_knob_name(void *set, int id)
{
    unsigned code;
    const struct core_lock_param *p = core_lock_by_id(id, &code);
    unsigned char *s;
    if (p && p->name && !((s = core_lock_sound(set)) && core_lock_stock_takes(id, s)))
        return (unsigned)names[code - FIRST_CODE];
    return KNOB_NAME(set, id);
}
