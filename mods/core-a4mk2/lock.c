/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-a4mk2 0.5: lockable mod parameters, the first part of the lock service
 * (README, "Lockable parameters"): core-ar2's lock.c (0.8 to 0.10) with the
 * Analog Four MKII's track kinds. Every address is OS 1.56's.
 *
 * The linker gives core the table core_lock_codes, {field descriptor, code}
 * for each field declared "lock": true, from the lockfile, and mods add
 * {field, id, min, max, short name, name, category} to core_lock_params. A
 * parameter with a code and an entry is lockable.
 *
 * Codes are numbered per track kind, 0x70..0x7f each: the synth tracks'
 * (1..4, sound fields), the FX track's (5, kit fields) and the CV track's
 * (6, kit fields declared "track": "cv", CV_FLAG in the table). Stock keeps
 * a parameter table per kind, and a code is a column of a step record on a
 * track of its kind, so the same code on two kinds is two parameters. Stock's
 * columns stop at 0x6f.
 *
 * A parameter's page knob (its id) is one stock does not have: stock's
 * records end at 0x126, and every accessor maps a higher id to record 0. Its
 * pseudo id, the id core gives it in stock's lists (destinations, from part
 * 2), is 0x127 + 16 x kind + code - 0x70.
 */

#include <stdint.h>

#define FIRST_CODE 0x70
#define LAST_CODE 0x7f
#define CODES 16
#define KINDS 3                               /* synth, FX, CV */
#define PSEUDO 0x127
#define CV_FLAG 0x800                         /* in core_lock_codes: a CV track's kit field */
#define SC_SOUND 1                            /* a field descriptor's scope (bits 28..31) */
#define SC_KIT 2
#define FX_TRACK 4
#define CV_TRACK 5

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

/* each code's parameter and flags per kind, made once, on the first ask */
static const struct core_lock_param *by_code[KINDS][CODES];
static unsigned flags_of[KINDS][CODES];
static volatile int made;

static int kind_of_entry(unsigned field, unsigned code)
{
    unsigned scope = field >> 28;
    return scope == SC_SOUND ? 0 : scope == SC_KIT ? (code & CV_FLAG ? 2 : 1) : -1;
}

static void make(void)
{
    const unsigned *c;
    const struct core_lock_param *const *p;
    unsigned code;
    int k;
    for (c = core_lock_codes; c[0]; c += 2)
        if ((code = c[1] & 0xff) >= FIRST_CODE && code <= LAST_CODE && (k = kind_of_entry(c[0], c[1])) >= 0)
            for (p = core_lock_params; *p; p++)
                if ((*p)->field == c[0]) {
                    by_code[k][code - FIRST_CODE] = *p;
                    flags_of[k][code - FIRST_CODE] = c[1] & ~0xffu;
                    break;
                }
    made = 1;
}

/* a kind's code's parameter, or 0 */
const struct core_lock_param *core_lock_by_code(int kind, unsigned code)
{
    if (!made)
        make();
    return (unsigned)kind < KINDS && code >= FIRST_CODE && code <= LAST_CODE ? by_code[kind][code - FIRST_CODE] : 0;
}

/* its flags in core_lock_codes (LFO, SMOOTH_OFF, ROUTE, CV_FLAG), or 0 */
unsigned core_lock_flags(int kind, unsigned code)
{
    return core_lock_by_code(kind, code) ? flags_of[kind][code - FIRST_CODE] : 0;
}

/* a track's kind: 0 the synth tracks (0..3), 1 FX (4), 2 CV (5), else -1 */
int core_lock_kind_of_track(int track)
{
    return (unsigned)track < 4 ? 0 : track == FX_TRACK ? 1 : track == CV_TRACK ? 2 : -1;
}

/* does the parameter belong on the track? */
int core_lock_on_track(const struct core_lock_param *p, int track)
{
    unsigned code;
    int kind = core_lock_kind_of_track(track);
    if (!p || kind < 0)
        return 0;
    for (code = FIRST_CODE; code <= LAST_CODE; code++)
        if (core_lock_by_code(kind, code) == p)
            return 1;
    return 0;
}

/* a field's code (and kind) in one probe or a few: an open hash of the fields
 * in core_lock_codes (at most 48), made once, on the first ask, with
 * interrupts masked, by whichever context asks first (core-ar2 0.10's) */
#define HASH 128
static unsigned hash_field[HASH];
static unsigned char hash_code[HASH], hash_kind[HASH];
static volatile int hashed;

static unsigned hash_of(unsigned field)
{
    return (field ^ field >> 8 ^ field >> 16 ^ field >> 24) & (HASH - 1);
}

static void make_hash(void)
{
    const unsigned *c;
    unsigned h;
    unsigned short sr;
    int k;
    __asm__ volatile("move.w %%sr,%0\n\tmove.w #0x2700,%%sr" : "=d"(sr) : : "memory");
    if (!hashed) {
        for (c = core_lock_codes; c[0]; c += 2) {
            if ((k = kind_of_entry(c[0], c[1])) < 0)
                continue;
            for (h = hash_of(c[0]); hash_field[h] && hash_field[h] != c[0]; h = (h + 1) & (HASH - 1))
                ;
            hash_code[h] = c[1] & 0xff;
            hash_kind[h] = k;
            hash_field[h] = c[0];
        }
        hashed = 1;
    }
    __asm__ volatile("move.w %0,%%sr" : : "d"(sr) : "memory");
}

static int slot_of(unsigned field)
{
    unsigned h;
    if (!hashed)
        make_hash();
    if (!field)
        return -1;
    for (h = hash_of(field); hash_field[h]; h = (h + 1) & (HASH - 1))
        if (hash_field[h] == field)
            return h;
    return -1;
}

/* the field's code (0x70..0x7f), or 0 */
int core_lock_code(unsigned field)
{
    int h = slot_of(field);
    return h < 0 ? 0 : hash_code[h];
}

/* the field's kind (0 synth, 1 FX, 2 CV), or -1 */
int core_lock_kind(unsigned field)
{
    int h = slot_of(field);
    return h < 0 ? -1 : hash_kind[h];
}

/* a kind's code's pseudo id, and back (0 when it is none) */
int core_lock_pseudo(int kind, unsigned code)
{
    return core_lock_by_code(kind, code) ? PSEUDO + CODES * kind + (int)code - FIRST_CODE : 0;
}

unsigned core_pseudo_code(int id, int *kind)
{
    int n = id - PSEUDO;
    if (n < 0 || n >= KINDS * CODES || !core_lock_by_code(n / CODES, FIRST_CODE + n % CODES))
        return 0;
    *kind = n / CODES;
    return FIRST_CODE + n % CODES;
}

/* a page knob's parameter among a kind's, and its code; 0 for an id stock
 * has (0..0x126) or no parameter's */
const struct core_lock_param *core_lock_by_id_in(int id, unsigned *code, int kind)
{
    const struct core_lock_param *p;
    if (id <= 0x126)
        return 0;
    for (*code = FIRST_CODE; *code <= LAST_CODE; ++*code)
        if ((p = core_lock_by_code(kind, *code)) != 0 && p->id == id)
            return p;
    return 0;
}

unsigned core_lock_field(const struct core_lock_param *p) { return p->field; }
int core_lock_min(const struct core_lock_param *p) { return p->min; }

/* the most a lock holds: max, at most 16383 (14 bits, two rows) */
#define TOP 16383
int core_lock_max(const struct core_lock_param *p) { return p->max > TOP ? TOP : p->max; }

/* a parameter above 127's coarse step, as a shift (0 for one up to 127) */
int core_lock_shift(const struct core_lock_param *p)
{
    int s = 0;
    while (core_lock_max(p) >> s > 127)
        s++;
    return s;
}

int core_lock_clamp(const struct core_lock_param *p, int v)
{
    int lo = core_lock_min(p), hi = core_lock_max(p);
    return v < lo ? lo : v > hi ? hi : v;
}
