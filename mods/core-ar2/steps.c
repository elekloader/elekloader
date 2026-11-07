/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-ar2 0.11: step tags and ev_steps (README, "Step tags (0.11)"). Every
 * address is OS 1.74's.
 *
 * A step tag is a mod's 16-bit handle on a step: for the Sidecar mods, the
 * Pi's record of that step. It goes wherever stock moves the step's record
 * (an 88-byte step lock record, rows.c), as core's mod locks do, and core
 * tells the mods the moves they act on (ev_steps).
 *
 * A mod that keeps tags adds a record to core_step_owners; its owner number
 * is the record's place there (core_step_owner), 0..7. Tags live in RAM only:
 * they are never written as lock rows or into a SysEx dump, so they last
 * until a power-off, and the mod restores them from its own store at
 * ev_project and on RELOAD.
 *
 *   - The 129 live pattern objects (rows.c) get a dense table for each
 *     owner: 2 bytes a step, 13 x 64 a pattern (1,664 bytes), 214,656 bytes
 *     an owner, in the free RAM below core's area (TAG_AREA), zeroed at the
 *     first ev_tick. A tag is found from the step record's address, with no
 *     search and no cap. Not on stock's heap: stock's buddy heap comes up
 *     from boot with a bad entry on its 512 KB free list (README), which a
 *     256 KB block taken there leaves the next large request to split.
 *   - A copy outside the live patterns (the clipboards, the undo buffers, a
 *     trig shift's temporaries) keeps its tags as sparse ledger entries with
 *     the code TAG_CODE + owner (rows.c), which rebuild never writes as rows.
 *   - memcpy, memset, operator new, the track and step clears and pattern
 *     init (rows.c's sites) copy or clear the matching slice of the tables;
 *     row load clears its store's (RELOAD); trig copy and paste carry a tag
 *     through the lock list with its code (0x400c4d62, 0x400c058c).
 *
 * Live object 0 is stock's copy of the pattern that plays as it was when it
 * started (a pattern switch copies it there, RELOAD PATTERN copies it back):
 * its tags follow too, but it is not a pattern a mod addresses, so no event
 * names it and core_step_tags reports its tags as copies (pattern -1).
 * Object k + 1 is pattern k (A01 = 0 .. H16 = 127).
 */

#include <stdint.h>

#define TAG_AREA 0x47938000u                    /* the start of the bigger area (README) */

#define LIVE_PATTERNS 0x407c89e3u
#define PATTERN_SIZE 85760u
#define LIVE_LOCKS 0x2e91
#define TRACK_LOCKS 5674
#define STEP_LOCKS 88
#define STEPS_LOCKS (64 * STEP_LOCKS)
#define STORE_LOCKS (13 * TRACK_LOCKS)
#define OBJECTS 129
#define STEPS (13 * 64)                         /* a pattern's steps */
#define TABLE (OBJECTS * STEPS)
#define TAG_CODE 128
#define OWNERS 8

#define TAGS_MOVED 1                            /* steps.h CORE_STEPS_MOVED ... */
#define TAGS_DROPPED 2
#define RELOAD 3
#define LENGTH 4
#define RESYNC 5

struct ent {                                    /* rows.c's */
    unsigned addr;
    unsigned char code, pad;
    unsigned short value;
};

struct core_step_owner;
extern const struct core_step_owner *const core_step_owners[];   /* zero-terminated */
typedef void (*ev_steps_t)(int32_t kind, int32_t pattern, int32_t track, int32_t step, int32_t n,
                           int32_t owner);
extern const ev_steps_t ev_steps[];

int core_rows_tag_at(unsigned addr, int owner);
void core_rows_tags_walk(int owner, void (*fn)(int, int, int, int));
int core_rows_current(void);

/* each owner's table: the handle + 1 at (object * 13 + track) * 64 + step,
 * 0 for none; and its tags in each object, so a copy that carries none
 * costs one look */
/* the tables end at 0x47b00000, below the mods' area (0x47b10000) */
typedef char tables_fit[TAG_AREA + OWNERS * TABLE * 2 <= 0x47b00000u ? 1 : -1];
static unsigned short *table[OWNERS];
static unsigned short tagged[OWNERS][OBJECTS];
static int owners = -1;

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

static int count_owners(void)
{
    int n = 0;
    if (owners < 0) {
        while (n < OWNERS && core_step_owners[n])
            n++;
        owners = n;
    }
    return owners;
}

/* ---- the event queue: 64, delivered at ev_tick ---- */

struct qev {
    short kind, owner;
    int a, b, c, d;                             /* a range: a the first step (object * 832 + ...), b its count */
};

#define QN 64
static struct qev q[QN];
static volatile unsigned qh, qt;                /* read and write counts */
static volatile int over;

/* an event joins a queued one of its kind and owner whose range it overlaps
 * or adjoins, when no event queued after that one touches its steps (a
 * trig shift's run of moves is one or two events); else it is queued */
static void push(int kind, int owner, int a, int b, int c, int d)
{
    unsigned short sr;
    struct qev *l;
    unsigned i;
    if (!ev_steps[0])
        return;
    sr = irq_mask();
    for (i = qt; kind != LENGTH && i != qh && qt - i < 8; i--) {
        l = &q[(i - 1) % QN];
        if (l->kind == kind && l->owner == owner && a <= l->a + l->b && a + b >= l->a) {
            b = (a + b > l->a + l->b ? a + b : l->a + l->b);
            if (a > l->a)
                a = l->a;
            l->a = a;
            l->b = b - a;
            irq_restore(sr);
            return;
        }
        if (l->kind == LENGTH || l->kind == RELOAD || (a < l->a + l->b && a + b > l->a))
            break;                              /* it touches these steps: keep the order */
    }
    if (qt - qh >= QN) {
        over = 1;
    } else {
        l = &q[qt % QN];
        l->kind = kind;
        l->owner = owner;
        l->a = a;
        l->b = b;
        l->c = c;
        l->d = d;
        qt++;
    }
    irq_restore(sr);
}

/* a range of steps (object * 832 + track * 64 + step), past object 0 */
static void push_range(int kind, int owner, int g, int n)
{
    if (g < STEPS) {
        n -= STEPS - g;
        g = STEPS;
    }
    if (n > 0)
        push(kind, owner, g, n, 0, 0);
}

static void deliver(const struct qev *e)
{
    const ev_steps_t *f;
    int pattern = -1, track = -1, step = -1, n = 0;
    if (e->kind == LENGTH) {
        pattern = e->a;
        track = e->b;
        step = e->c;
        n = e->d;
    } else if (e->kind != RESYNC) {
        pattern = e->a / STEPS - 1;
        track = e->a % STEPS / 64;
        step = e->a % 64;
        n = e->kind == RELOAD ? e->b / STEPS : e->b;   /* RELOAD: patterns */
    }
    for (f = ev_steps; *f; f++)
        (*f)(e->kind, pattern, track, step, n, e->owner);
}

/* ev_tick (the UI task): the tables, once, then the queue */
void core_steps_tick(void *ctrl)
{
    struct qev e;
    unsigned short sr;
    int o, i;
    (void)ctrl;
    if (count_owners() && !table[owners - 1])
        for (o = 0; o < owners; o++) {
            unsigned short *t = (unsigned short *)(TAG_AREA + o * TABLE * 2);
            for (i = 0; i < TABLE; i++)
                t[i] = 0;
            table[o] = t;
        }
    for (;;) {
        sr = irq_mask();
        if (over) {
            over = 0;
            qh = qt;
            irq_restore(sr);
            e.kind = RESYNC;
            e.owner = -1;
            deliver(&e);
            continue;
        }
        if (qh == qt) {
            irq_restore(sr);
            break;
        }
        e = q[qh % QN];
        qh++;
        irq_restore(sr);
        deliver(&e);
    }
}

/* ---- where a step record is ---- */

/* a live step record's place (object * 832 + track * 64 + step), or -1 */
int core_steps_live(unsigned addr)
{
    unsigned o = addr - LIVE_PATTERNS - LIVE_LOCKS, r;
    if (addr < LIVE_PATTERNS + LIVE_LOCKS || o / PATTERN_SIZE >= OBJECTS)
        return -1;
    r = o % PATTERN_SIZE;
    if (r >= STORE_LOCKS || r % TRACK_LOCKS >= STEPS_LOCKS || r % TRACK_LOCKS % STEP_LOCKS)
        return -1;
    return o / PATTERN_SIZE * STEPS + r / TRACK_LOCKS * 64 + r % TRACK_LOCKS / STEP_LOCKS;
}

static unsigned addr_of(int g)
{
    return LIVE_PATTERNS + PATTERN_SIZE * (g / STEPS) + LIVE_LOCKS + TRACK_LOCKS * (g % STEPS / 64) +
           STEP_LOCKS * (g % 64);
}

/* the live steps whose records start in [a, b): [*g0, *g1), 0 for none */
static int live_steps(unsigned a, unsigned b, int *g0, int *g1)
{
    unsigned base = LIVE_PATTERNS + LIVE_LOCKS, end = base + PATTERN_SIZE * (OBJECTS - 1) + STORE_LOCKS;
    unsigned o, r, k;
    if (b <= base || a >= end || b <= a)
        return 0;
    if (a < base)
        a = base;
    if (b > end)
        b = end;
    /* the first step at or after a */
    o = a - base;
    k = o / PATTERN_SIZE;
    r = o % PATTERN_SIZE;
    if (r >= STORE_LOCKS)
        *g0 = (k + 1) * STEPS;
    else if (r % TRACK_LOCKS >= STEPS_LOCKS)
        *g0 = k * STEPS + (r / TRACK_LOCKS + 1) * 64;
    else
        *g0 = k * STEPS + r / TRACK_LOCKS * 64 + (r % TRACK_LOCKS + STEP_LOCKS - 1) / STEP_LOCKS;
    /* the first step at or after b */
    o = b - base;
    k = o / PATTERN_SIZE;
    r = o % PATTERN_SIZE;
    if (r >= STORE_LOCKS)
        *g1 = (k + 1) * STEPS;
    else if (r % TRACK_LOCKS >= STEPS_LOCKS)
        *g1 = k * STEPS + (r / TRACK_LOCKS + 1) * 64;
    else
        *g1 = k * STEPS + r / TRACK_LOCKS * 64 + (r % TRACK_LOCKS + STEP_LOCKS - 1) / STEP_LOCKS;
    if (*g1 > TABLE)
        *g1 = TABLE;
    return *g0 < *g1;
}

static void set_at(int o, int g, unsigned v)
{
    unsigned short old = table[o][g];
    if (!old == !v) {
        table[o][g] = v;
        return;
    }
    tagged[o][g / STEPS] += v ? 1 : -1;
    table[o][g] = v;
}

/* does any owner have a tag in a live step of [a, b)? (rows.c's look) */
int core_steps_any(unsigned a, unsigned b)
{
    int g0, g1, o, k;
    if (owners <= 0 || !live_steps(a, b, &g0, &g1))
        return 0;
    for (o = 0; o < owners; o++)
        if (table[o])
            for (k = g0 / STEPS; k <= (g1 - 1) / STEPS; k++)
                if (tagged[o][k])
                    return 1;
    return 0;
}

/* the tags of the live steps of [a, b) go; an event unless quiet */
void core_steps_clear(unsigned a, unsigned b, int quiet)
{
    int g0, g1, o, g, first, last;
    if (owners <= 0 || !live_steps(a, b, &g0, &g1))
        return;
    for (o = 0; o < owners; o++) {
        if (!table[o])
            continue;
        first = last = -1;
        for (g = g0; g < g1; g++) {
            if (!tagged[o][g / STEPS]) {
                g = (g / STEPS + 1) * STEPS - 1;
                continue;
            }
            if (table[o][g]) {
                set_at(o, g, 0);
                if (first < 0)
                    first = g;
                last = g;
            }
        }
        if (first >= 0 && !quiet)
            push_range(TAGS_DROPPED, o, first, last - first + 1);
    }
}

/* row load into a live store (rows.c, before its locks): the store's tags
 * go and the mods hear RELOAD for its pattern */
void core_steps_reload(unsigned store)
{
    int g = core_steps_live(store);
    if (g < 0 || g % STEPS || count_owners() <= 0)
        return;
    core_steps_clear(store, store + STORE_LOCKS, 1);
    push_range(RELOAD, -1, g, STEPS);
}

static int object_of(unsigned p)
{
    unsigned o = p - LIVE_PATTERNS;
    return p >= LIVE_PATTERNS && o / PATTERN_SIZE < OBJECTS ? (int)(o / PATTERN_SIZE) : -1;
}

/* memcpy (rows.c, after the copy): each live step of [dst, dst + n) takes
 * the tag of the record copied onto it (dense from a live step, sparse from
 * elsewhere), or none when no whole record was. In the copy's direction, so
 * an overlapping copy reads each tag before it is overwritten. A copy by
 * whole pattern objects (a pattern switch, RELOAD PATTERN, a pattern's copy
 * and undo) moves slices, skipping objects with no tags at either end. */
void core_steps_copy(unsigned dst, unsigned src, unsigned n)
{
    int g0, g1, w0, w1, o, g, sg, step, v, k, end, moved0, moved1, drop0, drop1;
    int delta = (int)(dst - src), same, dk = 0, c;
    unsigned short *t, *f;
    unsigned at, from;
    if (owners <= 0 || !live_steps(dst, dst + n, &g0, &g1))
        return;
    same = delta % (int)PATTERN_SIZE == 0;
    if (same) {
        dk = delta / (int)PATTERN_SIZE;
        if (g0 / STEPS - dk < 0 || (g1 - 1) / STEPS - dk >= OBJECTS)
            same = 0;
        else if (n < STEP_LOCKS || !live_steps(dst, dst + n - STEP_LOCKS + 1, &w0, &w1))
            w1 = g0;                            /* no whole record */
    }
    for (o = 0; o < owners; o++) {
        if (!table[o])
            continue;
        moved0 = drop0 = -1;
        moved1 = drop1 = -1;
        if (same) {                             /* object by object: other objects, no overlap */
            for (g = g0; g < g1; g = end) {
                k = g / STEPS;
                end = (k + 1) * STEPS < g1 ? (k + 1) * STEPS : g1;
                if (!tagged[o][k] && !tagged[o][k - dk])
                    continue;
                t = table[o];
                f = t - dk * STEPS;
                c = 0;
                for (sg = g; sg < end; sg++) {
                    v = sg < w1 ? f[sg] : 0;
                    if (t[sg] == v)
                        continue;
                    if (t[sg]) {
                        c--;
                        if (drop0 < 0)
                            drop0 = sg;
                        drop1 = sg;
                    }
                    if (v) {
                        c++;
                        if (moved0 < 0)
                            moved0 = sg;
                        moved1 = sg;
                    }
                    t[sg] = v;
                }
                tagged[o][k] += c;
            }
        } else {
            step = delta > 0 ? -1 : 1;
            g = step > 0 ? g0 : g1 - 1;
            for (; step > 0 ? g < g1 : g >= g0; g += step) {
                at = addr_of(g);
                from = at - delta;
                v = 0;
                if (from >= src && from + STEP_LOCKS <= src + n) {
                    if ((sg = core_steps_live(from)) >= 0)
                        v = table[o][sg];
                    else if ((v = core_rows_tag_at(from, o)) >= 0)
                        v++;
                    else
                        v = 0;
                }
                if (table[o][g] == v)
                    continue;
                if (table[o][g]) {
                    if (drop0 < 0 || g < drop0)
                        drop0 = g;
                    if (g > drop1)
                        drop1 = g;
                }
                if (v) {
                    if (moved0 < 0 || g < moved0)
                        moved0 = g;
                    if (g > moved1)
                        moved1 = g;
                }
                set_at(o, g, v);
            }
        }
        if (drop0 >= 0)
            push_range(TAGS_DROPPED, o, drop0, drop1 - drop0 + 1);
        if (moved0 >= 0)
            push_range(TAGS_MOVED, o, moved0, moved1 - moved0 + 1);
    }
}

/* memcpy (rows.c): the dense tags of [src, src + n)'s live steps copied
 * whole to a place that is no live step, as sparse entries (sorted by
 * address) into out when it is given; their count */
int core_steps_src(unsigned dst, unsigned src, unsigned n, struct ent *out)
{
    int g0, g1, o, g, k = 0;
    unsigned at, to;
    if (owners <= 0 || !live_steps(src, src + n, &g0, &g1))
        return 0;
    if ((dst - src) % PATTERN_SIZE == 0 && object_of(dst) >= 0 && object_of(dst + n - 1) >= 0)
        return 0;                               /* live to live, the same layout: all dense */
    for (g = g0; g < g1; g++) {
        if (g % STEPS == 0) {
            for (o = 0; o < owners && !(table[o] && tagged[o][g / STEPS]); o++)
                ;
            if (o == owners) {
                g += STEPS - 1;
                continue;
            }
        }
        at = addr_of(g);
        if (at + STEP_LOCKS > src + n)
            break;
        to = at + (dst - src);
        if (core_steps_live(to) >= 0)
            continue;
        for (o = 0; o < owners; o++)
            if (table[o] && table[o][g]) {
                if (out) {
                    out[k].addr = to;
                    out[k].code = TAG_CODE + o;
                    out[k].pad = 0;
                    out[k].value = table[o][g] - 1;
                }
                k++;
            }
    }
    return k;
}

/* trig copy (rows.c): a live step's tags into the lock list, push(entry,
 * code, handle) each */
int core_steps_list(unsigned addr, int (*fn)(void *, int, int), void *entry)
{
    int g = core_steps_live(addr), o;
    if (g < 0 || owners <= 0)
        return 0;
    for (o = 0; o < owners; o++)
        if (table[o] && table[o][g])
            fn(entry, TAG_CODE + o, table[o][g] - 1);
    return 1;
}

/* trig paste (rows.c, 0x400c058c with a tag's code): the tag on a live step */
int core_steps_paste(unsigned addr, int owner, int handle)
{
    int g = core_steps_live(addr);
    unsigned v = (unsigned)handle + 1;
    if (g < 0 || owner < 0 || owner >= count_owners() || !table[owner] || handle < 0 || handle > 0xfffe)
        return 0;
    if (table[owner][g] == v)
        return 1;
    if (table[owner][g])
        push_range(TAGS_DROPPED, owner, g, 1);
    set_at(owner, g, v);
    push_range(TAGS_MOVED, owner, g, 1);
    return 1;
}

/* ---- LENGTH: the length and scale setters (steps.s), after stock ---- */

/* a setter's object's data (its method at +0x28, as the setters ask it) */
static unsigned data_of(unsigned obj)
{
    unsigned (*get)(unsigned) = *(unsigned (**)(unsigned))(*(unsigned *)obj + 0x28);
    return get(obj);
}

/* which: CORE_STEPS_LEN_* (steps.h); obj: the setter's object, whose data is
 * in a live pattern, a track's (+64 its track) for the track's two;
 * value: what the setter was given */
void core_steps_length(int which, unsigned obj, int value)
{
    int k = object_of(data_of(obj)), track = -1;
    if (which <= 1) {                           /* a track's length, speed */
        track = *(int *)(obj + 64);
        if ((unsigned)track > 12)
            track = -1;
    }
    if (k > 0)
        push(LENGTH, -1, k - 1, track, value, which);
}

/* ---- the API (steps.h) ---- */

int core_step_owner(const struct core_step_owner *rec)
{
    int o;
    for (o = 0; o < count_owners(); o++)
        if (core_step_owners[o] == rec)
            return o;
    return -1;
}

static int place(int pattern, int track, int step, int owner)
{
    if ((unsigned)pattern >= OBJECTS - 1 || (unsigned)track >= 13 || (unsigned)step >= 64 ||
        owner < 0 || owner >= count_owners() || !table[owner])
        return -1;
    return (pattern + 1) * STEPS + track * 64 + step;
}

int core_step_tag(int32_t pattern, int32_t track, int32_t step, int32_t owner)
{
    int g = place(pattern, track, step, owner);
    return g < 0 ? -1 : (int)table[owner][g] - 1;
}

int core_step_tag_set(int32_t pattern, int32_t track, int32_t step, int32_t owner, int32_t handle)
{
    int g = place(pattern, track, step, owner);
    if (g < 0 || handle < -1 || handle > 0xfffe)
        return 0;
    set_at(owner, g, (unsigned)(handle + 1));
    return 1;
}

void core_step_tags(int32_t owner, void (*fn)(int32_t pattern, int32_t track, int32_t step, int32_t handle))
{
    int g;
    if (owner < 0 || owner >= count_owners() || !table[owner] || !fn)
        return;
    for (g = 0; g < TABLE; g++) {
        if (!tagged[owner][g / STEPS]) {
            g += STEPS - 1;
            continue;
        }
        if (table[owner][g])
            fn(g < STEPS ? -1 : g / STEPS - 1, g % STEPS / 64, g % 64, table[owner][g] - 1);
    }
    core_rows_tags_walk(owner, (void (*)(int, int, int, int))fn);
}

int core_step_pattern_now(void)
{
    int k = core_rows_current();
    return k > 0 ? k - 1 : -1;
}

/* whether the tables were made (1) or are still to be (0) */
int core_step_tables(void)
{
    return count_owners() && table[owners - 1] ? 1 : 0;
}
