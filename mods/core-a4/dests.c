/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-a4 0.5: mod parameters as destinations, the lock service's second part
 * (README, "Destinations"). Every address is OS 1.56's.
 *
 * A lockable field declared "mod": true (ROUTE in core_lock_codes) is a
 * destination of every modulation source: the five routes (velocity, pitch
 * bend, mod wheel, breath, aftertouch), the four Dest knobs (EnvF, Env2,
 * LFO1, LFO2, A and B), the ten performance macros and CONTROL IN 1 and 2,
 * on its own track kind (synth sound fields, FX kit fields, CV kit fields).
 *
 * Lists. Stock builds every destination list in one routine (0x400a1108(set,
 * mask)): the route screens, the Dest popups, the macro and CONTROL IN
 * setup. At its end (dests.s) core appends the pseudo id (lock.c) of each
 * "mod" field of the set's kind, whatever the mask.
 *
 * Index both ways. The lists hold ids, the stored bytes an engine index: a
 * route's and a macro target's last byte (signed, -1 none), a Dest word's
 * high byte. 0x40007632 (id to index) gives a pseudo id its code;
 * 0x400077d2 (kind, index to id), which stock answers 0 above 0x6f, gives a
 * code its pseudo id. So the code goes raw in the bytes, and the knob step,
 * RANDOMIZE and the list's place follow from the list.
 *
 * Names. The parameter sets' short and long name calls (vtable +0x68, +0x6c,
 * which read the record table, record 0 "ERR" / "Invalid parameter" for any
 * id above 0x126) answer a pseudo id and a mod parameter's page knob with
 * the parameter's names.
 *
 * Legality. A Dest knob's value check (0x4009f650, vtable +0x28) refuses an
 * id above 0x126; a pseudo id of the set's kind passes (dests.s).
 *
 * Pushes. Stock resolves destinations when it pushes a sound, not in the
 * interrupt: the synth sound push (0x40100a04(sound, voice, track)) and the
 * FX and CV pushes (0x40100d84, 0x40100fb8) write each Dest selector's
 * target slot, the route push (0x40100baa(sound, voice)) each route's, the
 * performance push (0x401011e0(entry, n)) each macro target's, and a Dest
 * knob's turn goes through the knob push (0x401005b8). A code goes to
 * stock's sink slot (0x24b). At the end of each, core notes the entries
 * whose index is a "mod" code of the voice's kind, for the value read
 * (part 3): per voice, its Dest selectors' codes and its routes {source,
 * code, depth}; per macro, its targets {track, code, depth}; and the item
 * each voice was pushed with. A voice is 0..5: the four synth voices, the FX
 * track, the CV track (the map rows 0x42878da0 + 0x200 x voice).
 *
 * A Dest stored outside its list. The lists offer a Dest only the depths of
 * stages the interrupt applies after it (LFO2, Env2, LFO1, EnvF), but a Dest
 * byte set by NRPN, a kit load or SysEx can hold any index. Stock then adds
 * the term to a depth its stage has already read, which nothing hears (the
 * smoother rewrites it the next block). Core sends such a Dest to the sink at
 * the push, so a depth core reads at the end of the block (part 3) never
 * holds a term stock never used. The rule is stock's own list rule: a Dest
 * whose target is a depth (a selector's index + 4) the selector's mask
 * would not list.
 *
 * Every write is made with interrupts masked: the pushes run from the UI and
 * from the audio interrupt, and the read (part 3) runs in the interrupt.
 */

#include <stdint.h>

#define FIRST_CODE 0x70
#define LAST_CODE 0x7f
#define ROUTE 0x400                           /* in core_lock_codes: "mod": true */
#define PSEUDO 0x127
#define VOICES 6
#define SINK 0x24b

#define RECORDS ((const volatile unsigned char *)0x40186af4)   /* 0x127 x 0x38 */
#define REC_SIZE 0x38
#define REC_FLAGS 0x24
#define REC_LONG 0x28
#define REC_SHORT 0x30
#define INDEX_IDS ((const volatile int16_t *)0x4134086c)      /* [kind x 0x70 + index] */
#define MAP ((volatile int16_t *)0x42878da0)                  /* [voice x 0x100 + index] */
#define BASE16 ((volatile uint16_t *)0x80003e38)
#define BASE32 ((volatile uint32_t *)0x80003e38)
#define VOICE_TRACK ((const volatile int32_t *)0x80005ff0)     /* a synth voice's track */
#define DEST_LIST(r) ((const volatile int32_t *)(0x4045c7a8 + 0x20 * (r)))  /* r 0..3; FX 4, CV 5 */
#define LIST_APPEND ((void (*)(void *, int *))0x40168822)      /* (vector, &id) */
#define STOCK_SHORT ((const char *(*)(void *, int))0x4009e79e)
#define STOCK_LONG ((const char *(*)(void *, int))0x4009e7c2)

#define SRC_VEL 0                             /* routes: velocity, bend, wheel, breath, aftertouch */
#define SRC_DEST 5                            /* + the selector's place in the Dest list (0..7) */
#define ROUTE_AT 0xec                         /* a synth sound's routes, 4 bytes each */
#define ROUTES 25
#define DESTS 8
#define MACROS 12
#define TARGETS 5

struct core_lock_param {
    unsigned field;
    int32_t id;
    int16_t min, max;
    const char *short_name;
    const char *name;
    const char *category;
};

const struct core_lock_param *core_lock_by_code(int kind, unsigned code);
const struct core_lock_param *core_lock_by_id_in(int id, unsigned *code, int kind);
unsigned core_lock_flags(int kind, unsigned code);
int core_lock_pseudo(int kind, unsigned code);
unsigned core_pseudo_code(int id, int *kind);

/* one modulation of a mod parameter: a route {SRC_VEL + source, its place,
 * code, depth}; a Dest {SRC_DEST + place, selector index, code, 0}; a macro
 * target {macro, track, code, depth} */
struct core_dst {
    uint8_t src, n, code, pad;
    int16_t depth;
};

struct core_voice_dsts {
    int16_t routes, dests;                    /* entries of each */
    struct core_dst e[ROUTES + DESTS];        /* routes first, then Dests */
};

volatile struct core_voice_dsts core_voice_dsts[VOICES];
volatile struct core_dst core_macro_dsts[MACROS * TARGETS];
volatile int16_t core_macro_count;
void *volatile core_voice_item[VOICES];       /* the sound (or FX, CV block) pushed */
static volatile uint8_t dest_code[VOICES][DESTS];   /* each Dest's code, 0 none */
static volatile uint8_t macro_code[MACROS][TARGETS];
static volatile int8_t macro_track[MACROS][TARGETS];
static volatile int16_t macro_depth[MACROS][TARGETS];
volatile unsigned core_dst_pushes;            /* every push core noted (checks) */

static unsigned short irq_mask(void)
{
    unsigned short sr;
    __asm__ volatile("move.w %%sr,%0\n\tmove.w #0x2700,%%sr" : "=d"(sr) : : "memory");
    return sr;
}

static void irq_restore(unsigned short sr)
{
    __asm__ volatile("move.w %0,%%sr" : : "d"(sr) : "memory");
}

/* a record kind (set +4: synth 1, FX 7, CV 8) as a lock kind, else -1 */
static int kind_of_rec(unsigned rk)
{
    return rk == 1 ? 0 : rk == 7 ? 1 : rk == 8 ? 2 : -1;
}

static int rec_kind(int kind)
{
    return kind == 0 ? 1 : kind == 1 ? 7 : 8;
}

static int voice_kind(int v)
{
    return v < 4 ? 0 : v == 4 ? 1 : 2;
}

/* is the code a "mod" parameter of the kind? */
static int mod_code(int kind, int code)
{
    return code >= FIRST_CODE && code <= LAST_CODE && (core_lock_flags(kind, code) & ROUTE);
}

/* ---- lists, index, names, legality ---- */

/* the builder's end (dests.s): the kind's "mod" parameters after stock's */
void core_dst_append(void *list, void *set, unsigned mask)
{
    int kind = kind_of_rec(((const unsigned *)set)[1]), code, id;
    (void)mask;                               /* every source is a destination */
    if (kind < 0)
        return;
    for (code = FIRST_CODE; code <= LAST_CODE; code++)
        if (mod_code(kind, code)) {
            id = core_lock_pseudo(kind, code);
            LIST_APPEND(list, &id);
        }
}

/* id to index (dests.s): a "mod" pseudo id's code, else -1 */
int core_dst_code(int id)
{
    int kind;
    unsigned code = core_pseudo_code(id, &kind);
    return code && mod_code(kind, code) ? (int)code : -1;
}

/* (record kind, index) to id (dests.s) for an index above 0x6f: a "mod"
 * code's pseudo id, else 0 (None), as stock */
int core_dst_id(unsigned rk, unsigned index)
{
    int kind = kind_of_rec(rk);
    return kind >= 0 && mod_code(kind, index) ? core_lock_pseudo(kind, index) : 0;
}

/* the Dest value check (dests.s): a "mod" pseudo id of the set's kind */
int core_dst_legal(void *set, int id)
{
    int kind, k = kind_of_rec(((const unsigned *)set)[1]);
    unsigned code = core_pseudo_code(id, &kind);
    return code && kind == k && mod_code(kind, code);
}

/* the set's id's parameter: a pseudo id or a page knob of the set's kind */
static const struct core_lock_param *named(void *set, int id)
{
    int kind, k = kind_of_rec(((const unsigned *)set)[1]);
    unsigned code;
    if (id <= 0x126 || k < 0)
        return 0;
    if ((code = core_pseudo_code(id, &kind)) != 0)
        return kind == k ? core_lock_by_code(kind, code) : 0;
    return core_lock_by_id_in(id, &code, k);
}

/* vtable +0x68: the short name (a knob's label, a Dest's value) */
const char *core_set_short(void *set, int id)
{
    const struct core_lock_param *p = named(set, id);
    return p && p->short_name ? p->short_name : STOCK_SHORT(set, id);
}

/* vtable +0x6c: the long name (the title with a trig held, a list's item) */
const char *core_set_long(void *set, int id)
{
    const struct core_lock_param *p = named(set, id);
    return p && p->name ? p->name : STOCK_LONG(set, id);
}

/* a "mod" pseudo id's parameter, whatever the set */
static const struct core_lock_param *pseudo_param(int id)
{
    int kind;
    unsigned code = core_pseudo_code(id, &kind);
    return code && mod_code(kind, code) ? core_lock_by_code(kind, code) : 0;
}

static const char *category_of(const struct core_lock_param *p)
{
    return p->category && p->category[0] ? p->category : "MOD";
}

/* the Dest knob's value text (dests.s): the short name, else 0 */
const char *core_pseudo_short(int id)
{
    const struct core_lock_param *p = pseudo_param(id);
    return p ? (p->short_name ? p->short_name : p->name ? p->name : "MOD") : 0;
}

/* a list's item (dests.s): its name and category; 0 when the id is not one */
int core_pseudo_names(int id, const char **name, const char **category)
{
    const struct core_lock_param *p = pseudo_param(id);
    if (!p)
        return 0;
    *name = p->name ? p->name : core_pseudo_short(id);
    *category = category_of(p);
    return 1;
}

/* a setup screen's target (dests.s): its short name, its category in
 * core_dst_category; 0 when the id is not one */
const char *volatile core_dst_category;

const char *core_pseudo_setup(int id)
{
    const struct core_lock_param *p = pseudo_param(id);
    if (!p)
        return 0;
    core_dst_category = category_of(p);
    return core_pseudo_short(id);
}

/* ---- pushes ---- */

static unsigned rec_flags(int id)
{
    const volatile unsigned char *r = RECORDS + (unsigned)id * REC_SIZE + REC_FLAGS;
    return (unsigned)r[0] << 24 | (unsigned)r[1] << 16 | (unsigned)r[2] << 8 | r[3];
}

/* a Dest selector's list mask, from its record's flags, as 0x4009f650 */
static unsigned dest_mask(unsigned f)
{
    return f & 0x4000 ? 0x3e00 : f & 0x10000 ? 0x1e00 : f & 0x8000 ? 0x0e00 : f & 0x20000 ? 0x0600 : 0;
}

static int index_id(int kind, int index)
{
    return index >= 0 && index < FIRST_CODE ? INDEX_IDS[rec_kind(kind) * 0x70 + index] : 0;
}

/* does the selector (sel) target (index) a depth its list does not offer? */
static int stale_depth(int kind, const volatile int32_t *list, int sel, int index)
{
    int i, s, id, t;
    for (i = 0; i < DESTS && (s = list[i]) != -1; i++)
        if (index == s + 4)
            break;
    if (i == DESTS || s == -1 || (id = index_id(kind, sel)) <= 0 || (t = index_id(kind, index)) <= 0)
        return 0;
    return (dest_mask(rec_flags(id)) & ~rec_flags(t)) != 0;
}

/* send a selector's target to the sink, as the push writes an unknown index */
static void to_sink(int row, int sel)
{
    int slot = MAP[row * 0x100 + sel];
    BASE16[slot] = SINK;
    BASE16[slot + 0xe5c] = SINK;
    BASE32[slot + 0x4e1] = SINK << 8;
}

/* the voice's Dests from dest_code into its list (masked) */
static void dests_out(int v)
{
    volatile struct core_voice_dsts *d = &core_voice_dsts[v];
    int i, n = 0;
    for (i = 0; i < DESTS; i++)
        if (dest_code[v][i]) {
            volatile struct core_dst *e = &d->e[d->routes + n++];
            e->src = SRC_DEST + i;
            e->n = DEST_LIST(v)[i];
            e->code = dest_code[v][i];
            e->depth = 0;
        }
    d->dests = n;
}

/* one selector's Dest byte: note a code, send a stale depth to the sink */
static void dest_one(int v, int i, int sel, int index)
{
    int kind = voice_kind(v);
    dest_code[v][i] = mod_code(kind, index) ? index : 0;
    if (stale_depth(kind, DEST_LIST(v), sel, index))
        to_sink(v, sel);
}

/* a push's Dests: item is the sound (synth, its words at +0x14), the FX or
 * CV block (at +4); the Dest byte is a word's high byte */
static void dests_push(int v, void *item, int words)
{
    const volatile int32_t *list = DEST_LIST(v);
    const unsigned char *w = (const unsigned char *)item + words;
    unsigned short sr = irq_mask();
    int i, s;
    for (i = 0; i < DESTS; i++)
        dest_code[v][i] = 0;
    for (i = 0; i < DESTS && (s = list[i]) != -1; i++)
        dest_one(v, i, s, (signed char)w[2 * s]);
    core_voice_item[v] = item;
    dests_out(v);
    core_dst_pushes++;
    irq_restore(sr);
}

void core_dst_push_synth(void *sound, int voice)
{
    if ((unsigned)voice < 4)
        dests_push(voice, sound, 0x14);
}

void core_dst_push_fx(void *fx) { dests_push(4, fx, 4); }
void core_dst_push_cv(void *cv) { dests_push(5, cv, 4); }

/* the route push (sound, voice): 5 sources x 5 routes {u16 depth, u8, s8 index} */
void core_dst_push_routes(void *sound, int voice)
{
    const unsigned char *r = (const unsigned char *)sound + ROUTE_AT;
    volatile struct core_voice_dsts *d;
    struct core_dst e[ROUTES];
    unsigned short sr;
    int i, n = 0, src, at;
    if ((unsigned)voice >= 4)
        return;
    for (i = 0; i < ROUTES; i++) {
        /* velocity +0xec, bend +0x102, wheel +0x116, breath +0x12a, AT +0x13e */
        src = i / 5;
        at = (src == 0 ? 0 : 0x16 + (src - 1) * 0x14) + 4 * (i % 5);
        if (mod_code(0, (signed char)r[at + 3])) {
            e[n].src = SRC_VEL + src;
            e[n].n = i % 5;
            e[n].code = r[at + 3];
            e[n].pad = 0;
            e[n].depth = (int16_t)(r[at] << 8 | r[at + 1]);
            n++;
        }
    }
    sr = irq_mask();
    d = &core_voice_dsts[voice];
    if (d->routes != n)                       /* the Dests follow the routes */
        for (i = 0; i < d->dests; i++)
            d->e[n + i] = d->e[d->routes + i];
    for (i = 0; i < n; i++)
        d->e[i] = e[i];
    d->routes = n;
    core_voice_item[voice] = sound;
    core_dst_pushes++;
    irq_restore(sr);
}

/* the performance push (entry, n): 5 targets {s16 depth, s8 track, s8 index} at +0x10 */
void core_dst_push_macro(void *entry, int n)
{
    const unsigned char *t = (const unsigned char *)entry + 0x10;
    unsigned short sr;
    int i, j, k = 0, track, kind;
    if ((unsigned)n >= MACROS)
        return;
    sr = irq_mask();
    for (i = 0; i < TARGETS; i++, t += 4) {
        track = (signed char)t[2];
        kind = (unsigned)track < 4 ? 0 : track == 4 ? 1 : track == 5 ? 2 : -1;
        macro_code[n][i] = kind >= 0 && mod_code(kind, (signed char)t[3]) ? t[3] : 0;
        macro_track[n][i] = track;
        macro_depth[n][i] = (int16_t)(t[0] << 8 | t[1]);
    }
    for (i = 0; i < MACROS; i++)
        for (j = 0; j < TARGETS; j++)
            if (macro_code[i][j]) {
                volatile struct core_dst *e = &core_macro_dsts[k++];
                e->src = i;
                e->n = macro_track[i][j];
                e->code = macro_code[i][j];
                e->depth = macro_depth[i][j];
            }
    core_macro_count = k;
    core_dst_pushes++;
    irq_restore(sr);
}

/* the knob push (value, track, index, Dest): a Dest knob's new byte on each
 * voice of the track, as stock pushes it */
void core_dst_knob(int value, int track, int index, int is_dest)
{
    unsigned short sr;
    int v, i;
    if (!is_dest)
        return;
    sr = irq_mask();
    for (v = 0; v < VOICES; v++) {
        if (v < 4 ? (unsigned)track >= 4 || VOICE_TRACK[v] != track : track != v)
            continue;
        for (i = 0; i < DESTS && DEST_LIST(v)[i] != -1; i++)
            if (DEST_LIST(v)[i] == index) {
                dest_one(v, i, index, (signed char)((int16_t)value >> 8));
                dests_out(v);
            }
    }
    core_dst_pushes++;
    irq_restore(sr);
}

/* ---- for the value read (part 3) ---- */

/* the voice's (0..5) modulations of its mod parameters: routes, then Dests */
const struct core_dst *core_dst_voice(int voice, int *routes, int *dests)
{
    if ((unsigned)voice >= VOICES) {
        *routes = *dests = 0;
        return 0;
    }
    *routes = core_voice_dsts[voice].routes;
    *dests = core_voice_dsts[voice].dests;
    return (const struct core_dst *)core_voice_dsts[voice].e;
}

/* the macro targets on mod parameters, every macro's */
const struct core_dst *core_dst_macros(int *n)
{
    *n = core_macro_count;
    return (const struct core_dst *)core_macro_dsts;
}

/* does anything modulate one of the voice's mod parameters? */
int core_dst_any(int voice)
{
    int i;
    if ((unsigned)voice >= VOICES)
        return 0;
    if (core_voice_dsts[voice].routes || core_voice_dsts[voice].dests)
        return 1;
    for (i = 0; i < core_macro_count; i++) {
        int t = core_macro_dsts[i].n;
        if (voice < 4 ? (unsigned)t < 4 && VOICE_TRACK[voice] == t : t == voice)
            return 1;
    }
    return 0;
}

/* ---- loads: a code this build has no "mod" parameter for becomes None ---- */

static int clean(int kind, int c, int foreign)
{
    return c >= FIRST_CODE && (foreign || kind < 0 || !mod_code(kind, c));
}

static void clean_dests(unsigned char *item, int words, const volatile int32_t *list, int kind, int foreign)
{
    int i, s;
    for (i = 0; i < DESTS && (s = list[i]) != -1; i++)
        if (clean(kind, (signed char)item[words + 2 * s], foreign))
            item[words + 2 * s] = item[words + 2 * s + 1] = 0;
}

/* a live sound after its load (store.c): its routes and Dest words; foreign:
 * of a layout this build does not read, so every code goes */
void core_dst_clean_sound(unsigned char *sound, int foreign)
{
    int i, at;
    for (i = 0; i < ROUTES; i++) {
        at = ROUTE_AT + (i < 5 ? 0 : 0x16 + (i / 5 - 1) * 0x14) + 4 * (i % 5);
        if (clean(0, (signed char)sound[at + 3], foreign))
            sound[at + 3] = 0xff;
    }
    clean_dests(sound, 0x14, DEST_LIST(0), 0, foreign);
}

/* a live kit after its load: the FX (+0x564) and CV (+0x668) blocks' Dest
 * words and the macro targets (+0x764, 12 x 0x26, 5 targets at +0x10) */
void core_dst_clean_kit(unsigned char *kit, int foreign)
{
    int i, j, track;
    clean_dests(kit + 0x564, 4, DEST_LIST(4), 1, foreign);
    clean_dests(kit + 0x668, 4, DEST_LIST(5), 2, foreign);
    for (i = 0; i < MACROS; i++)
        for (j = 0; j < TARGETS; j++) {
            unsigned char *t = kit + 0x764 + 0x26 * i + 0x10 + 4 * j;
            track = (signed char)t[2];
            if (clean((unsigned)track < 4 ? 0 : track == 4 ? 1 : track == 5 ? 2 : -1, (signed char)t[3], foreign))
                t[3] = 0xff;
        }
}
