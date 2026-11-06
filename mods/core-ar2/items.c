/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-ar2 0.11: remote fields and item events (README, "Remote fields
 * (0.11)"). Every address is OS 1.74's.
 *
 * A storage field declared "where": "sidecar" (sound, kit or pattern scope)
 * is kept in no stock object: core keeps a RAM mirror of it for every place
 * that holds such an item, read and written with core_store_get and
 * core_store_set like any field, and the mod keeps its data elsewhere (for
 * the Sidecar mods, on the Pi). A stock field rides inside the item's bytes
 * wherever stock copies them; a sidecar field's mirror has to be moved by
 * core, so core follows sound, kit and pattern records by address, as the
 * ledger follows step records (rows.c), and tells the mods each move
 * (ev_items), delivered in the UI task.
 *
 * The places with a mirror ("fixed" places):
 *
 *   pattern  the 129 live pattern objects (object 0 the playing copy, object
 *            k + 1 pattern k) and the stored project's 129 pattern records
 *   kit      the 128 live kits as edited, the 128 as last saved (what RELOAD
 *            reads), the stored project's 128 kits and its working kit
 *   sound    the 12 sounds of each of those kits, the 128 live pool sounds
 *            and the stored project's 128
 *
 * and any other memory a whole record is copied to from a place ("other":
 * the clipboards, the undo buffers, Transfer's and a swap's temporaries),
 * kept by address until overwritten, cleared or given out again (32 at
 * most, oldest first out). A kit carries its 12 sounds: a kit's move moves
 * their mirrors too, under one KIT event.
 *
 * What moves an item:
 *   - memcpy (0x40098bc0, rows.s) of 88 bytes or more: each whole place in
 *     the destination takes the mirror of what sat at the same offset in the
 *     source (a place, or other memory core follows), else zeros; a whole
 *     record copied from a place to other memory starts following it there;
 *   - memset of 88 or more and operator new (rows.s): other memory there is
 *     no longer followed; a place wholly cleared gets zeros;
 *   - the serializers core already hooks (store.c: sound, kit and pattern
 *     save and load): to or from the stored project, the place there; to or
 *     from any other buffer (SysEx in and out, Transfer, the +Drive's sound
 *     library), an export cache of 32 entries keyed by a hash of the stored
 *     record, so a record that comes back unchanged brings its mirror back;
 *   - the inits (pattern, sound, and the kit's 0x400117cc: a kit browser
 *     clear, a new kit): zeros, from NONE;
 *   - a whole project load or save, init (CREATE NEW) and file copy: the
 *     records move one by one as above, with one event per kind at the end
 *     (from ..ALL to ..ALL) instead of one per record.
 *
 * Cost: the memcpy test is a few compares when neither end is near a place
 * or followed memory; a move is a short copy with interrupts masked. All
 * events are queued (64, coalescing runs) and delivered at ev_tick.
 */

#include <stdint.h>

#define MIRROR_AREA 0x47b00000u                 /* after 8 owners' tag tables (steps.c), below the mods' area */

#define LIVE_PROJECT 0x407c89cbu
#define LIVE_PATTERNS (LIVE_PROJECT + 24)       /* object 0, then the 128 */
#define PATTERN_SIZE 85760u
#define OBJECTS 129
#define LIVE_KITS (LIVE_PROJECT + 0xa8cf18)     /* 128 as edited, then 128 as saved */
#define KIT_SIZE 2531u
#define KIT_SOUNDS 42u
#define SOUND_SIZE 152u
#define LIVE_POOL (LIVE_PROJECT + 0xb4b27c)
#define S 0x405c8f20u                           /* the stored project */
#define IMAGE_SIZE 0x1f9f32u
#define S_PATTERNS (S + 0x18)
#define S_PATTERN 13101u
#define S_KITS (S + 0x19d3fb)
#define S_WORKING_KIT (S + 0x19c9c5)
#define S_KIT 2610u
#define S_KIT_SOUNDS 46u
#define S_SOUND 162u
#define S_POOL (S + 0x1f4397)

#define K_SOUND 1                               /* the kinds: store scopes */
#define K_KIT 2
#define K_PATTERN 5

/* refs: where << 24 | index (items.h) */
#define W_NONE 0
#define W_LIVE 1
#define W_SAVED 2
#define W_POOL 3
#define W_IMAGE 4
#define W_IMAGE_POOL 5
#define W_OTHER 6
#define W_OUT 7
#define W_EXT 8
#define REF(w, i) ((unsigned)(w) << 24 | ((unsigned)(i) & 0xffffff))
#define ALL 0xffffff
#define WORKING 0xffff                          /* object 0, the stored project's working kit */
#define RESYNC 0

/* fixed places: patterns 0..128 live objects, 129..257 the stored project's;
 * kits 0..255 live (edited, saved), 256..383 stored, 384 the stored working
 * kit; sounds kit place * 12 + track, then 4620.. the live pool, 4748.. the
 * stored pool. Other memory: OTHER + entry (* 16 + track for a kit's sound,
 * + 12 for a sound entry's own). */
#define PAT_PLACES 258
#define KIT_PLACES 385
#define POOL_LIVE (KIT_PLACES * 12)
#define POOL_IMAGE (POOL_LIVE + 128)
#define SOUND_PLACES (POOL_IMAGE + 128)
#define OTHER 0x10000
#define OTHERS 32
#define OUTS 32

/* mirror strides: the most a build can declare (store.py SIDECAR_ROOM) */
#define MS 8                                    /* a sound's bytes */
#define MK 8                                    /* a kit's */
#define MP 32                                   /* a pattern's */
#define MREC (MK + 12 * MS)                     /* the most one record carries: a kit and its sounds */

struct other {
    unsigned addr;              /* 0: free */
    unsigned char kind, stored; /* stored: the record is in its stored form */
    unsigned short serial;
    unsigned char m[MREC];
};

struct out {
    unsigned hash;
    unsigned char kind, used;
    unsigned short serial;
    unsigned char m[MREC];
};

#define M_PAT ((unsigned char *)MIRROR_AREA)
#define M_KIT (M_PAT + PAT_PLACES * MP)
#define M_SOUND (M_KIT + KIT_PLACES * MK)
#define others ((struct other *)(M_SOUND + SOUND_PLACES * MS))
#define outs ((struct out *)(others + OTHERS))
#define M_END ((unsigned char *)(outs + OUTS))
/* the mirrors end below the mods' area of 0.11 (0x47b10000, the profile's core_ddr) */
typedef char mirrors_fit[MIRROR_AREA + PAT_PLACES * MP + KIT_PLACES * MK + SOUND_PLACES * MS
                         + OTHERS * sizeof(struct other) + OUTS * sizeof(struct out) <= 0x47b10000u ? 1 : -1];
static unsigned short serials, out_serials;
static unsigned others_at, outs_at;
static volatile int ready;                      /* the area zeroed (the first ev_tick) */
static int bulk;                                /* a whole project's load, save or init: one event per kind */
static unsigned bulk_from, bulk_to;
static int nested;                              /* inside a kit's serializer or init: its sounds go with it */

typedef void (*ev_items_t)(int32_t kind, uint32_t from, uint32_t to, int32_t n);
extern const ev_items_t ev_items[];
extern char __sidecar_bytes[];                  /* linker: sound << 16 | kit << 8 | pattern */

#ifdef CORE_ITEMS_MEASURE
/* a measuring build only (-DCORE_ITEMS_MEASURE): DMA timer 0 ticks with
 * interrupts masked here (the longest, and the sum), and the longest
 * memcpy hook call (masked or not) */
#define DTCN0 (*(volatile unsigned *)0xfc07000c)
struct { char magic[4]; uint32_t n, sum, max, calls, call_max; } core_items_cost = {{'I', 'T', 'C', 'T'}};
static unsigned masked_at;
#endif

static unsigned short irq_mask(void)
{
    unsigned short sr;
    __asm__ volatile("move.w %%sr,%0\n\tmove.w #0x2700,%%sr" : "=d"(sr) : : "memory");
#ifdef CORE_ITEMS_MEASURE
    if ((sr & 0x0700) != 0x0700)                /* the outermost only */
        masked_at = DTCN0;
#endif
    return sr;
}

static void irq_restore(unsigned short sr)
{
#ifdef CORE_ITEMS_MEASURE
    unsigned d = DTCN0 - masked_at;
    if ((sr & 0x0700) == 0x0700)
        goto nested;
    core_items_cost.n++;
    core_items_cost.sum += d;
    if (d > core_items_cost.max)
        core_items_cost.max = d;
nested:
#endif
    __asm__ volatile("move.w %0,%%sr" : : "d"(sr) : "memory");
}

static int sidecar(int kind)
{
    unsigned b = (unsigned)__sidecar_bytes;
    return kind == K_SOUND ? b >> 16 & 255 : kind == K_KIT ? b >> 8 & 255 : b & 255;
}

/* __sidecar_bytes is a value in a symbol's address. The compiler takes an
 * array's address as never null and folds a plain `!= 0` to true, which
 * left every remote-field hook live on builds without sidecar fields; the
 * empty asm hides the value from it. */
static int any_sidecar(void)
{
    unsigned b = (unsigned)__sidecar_bytes;
    __asm__("" : "+d"(b));
    return b != 0;
}

/* ---- the event queue ---- */

struct qev {
    int kind;
    unsigned from, to;
    int n;
};

#define QN 64
static struct qev q[QN];
static volatile unsigned qh, qt;
static volatile int over;

static int same_run(unsigned a, unsigned b, int n)
{
    return a >> 24 == b >> 24 && (a & 0xffffff) != ALL && (a & 0xffffff) + n == (b & 0xffffff);
}

static void push(int kind, unsigned from, unsigned to)
{
    unsigned short sr;
    struct qev *l;
    if (!ev_items[0] || !any_sidecar())
        return;
    if (bulk)
        return;
    sr = irq_mask();
    if (qt != qh) {                             /* a run: the next of each, as RELOAD of all kits */
        l = &q[(qt - 1) % QN];
        if (l->kind == kind && l->n == 1 && l->from == from && l->to == to) {
            irq_restore(sr);                    /* the same move again (a save's repeats) */
            return;
        }
        if (l->kind == kind && (from >> 24 == W_NONE ? l->from == from : same_run(l->from, from, l->n)) &&
            same_run(l->to, to, l->n)) {
            l->n++;
            irq_restore(sr);
            return;
        }
    }
    if (qt - qh >= QN) {
        over = 1;
    } else {
        l = &q[qt % QN];
        l->kind = kind;
        l->from = from;
        l->to = to;
        l->n = 1;
        qt++;
    }
    irq_restore(sr);
}

static void push_all(int kind, unsigned from, unsigned to)
{
    unsigned short sr;
    struct qev *l;
    if (!ev_items[0] || !any_sidecar())
        return;
    sr = irq_mask();
    if (qt - qh >= QN) {
        over = 1;
    } else {
        l = &q[qt % QN];
        l->kind = kind;
        l->from = from;
        l->to = to;
        l->n = 0;
        qt++;
    }
    irq_restore(sr);
}

/* ---- places ---- */

static int in(unsigned a, unsigned base, unsigned size, unsigned count, unsigned *k)
{
    unsigned o = a - base;
    if (a < base || o % size || o / size >= count)
        return 0;
    *k = o / size;
    return 1;
}

static int other_at(unsigned a, int kind, int *t);

/* the place of a record at a, of this kind, or -1 */
static int place_of(int kind, unsigned a)
{
    unsigned k, r;
    int t = 12;
    if (kind == K_PATTERN) {
        if (in(a, LIVE_PATTERNS, PATTERN_SIZE, OBJECTS, &k))
            return k;
        if (in(a, S_PATTERNS, S_PATTERN, OBJECTS, &k))
            return OBJECTS + k;
    } else if (kind == K_KIT) {
        if (in(a, LIVE_KITS, KIT_SIZE, 256, &k))
            return k;
        if (in(a, S_KITS, S_KIT, 128, &k))
            return 256 + k;
        if (a == S_WORKING_KIT)
            return 384;
    } else {
        if (a >= LIVE_KITS + KIT_SOUNDS && a < LIVE_KITS + 256 * KIT_SIZE) {
            k = (a - LIVE_KITS) / KIT_SIZE;
            r = (a - LIVE_KITS) % KIT_SIZE;
            if (r >= KIT_SOUNDS && (r - KIT_SOUNDS) % SOUND_SIZE == 0 && (r - KIT_SOUNDS) / SOUND_SIZE < 12)
                return k * 12 + (r - KIT_SOUNDS) / SOUND_SIZE;
            return -1;
        }
        if (a >= S_KITS + S_KIT_SOUNDS && a < S_KITS + 128 * S_KIT) {
            k = (a - S_KITS) / S_KIT;
            r = (a - S_KITS) % S_KIT;
            if (r >= S_KIT_SOUNDS && (r - S_KIT_SOUNDS) % S_SOUND == 0 && (r - S_KIT_SOUNDS) / S_SOUND < 12)
                return (256 + k) * 12 + (r - S_KIT_SOUNDS) / S_SOUND;
            return -1;
        }
        if (in(a, S_WORKING_KIT + S_KIT_SOUNDS, S_SOUND, 12, &k))
            return 384 * 12 + k;
        if (in(a, LIVE_POOL, SOUND_SIZE, 128, &k))
            return POOL_LIVE + k;
        if (in(a, S_POOL, S_SOUND, 128, &k))
            return POOL_IMAGE + k;
    }
    if ((k = other_at(a, kind, &t)) != (unsigned)-1)
        return OTHER + (kind == K_SOUND ? k * 16 + t : k);
    return -1;
}

static int stored_place(int kind, int p)
{
    if (p >= OTHER)
        return others[kind == K_SOUND ? (p - OTHER) >> 4 : p - OTHER].stored;
    if (kind == K_PATTERN)
        return p >= OBJECTS;
    if (kind == K_KIT)
        return p >= 256;
    return p >= POOL_IMAGE || (p < POOL_LIVE && p / 12 >= 256);
}

static unsigned char *mirror(int kind, int p)
{
    struct other *o;
    if (p < 0)
        return 0;
    if (p >= OTHER) {
        if (kind == K_SOUND) {
            o = &others[(p - OTHER) >> 4];
            return o->kind == K_KIT ? o->m + MK + MS * ((p - OTHER) & 15) : o->m;
        }
        return others[p - OTHER].m;
    }
    return kind == K_PATTERN ? M_PAT + MP * p : kind == K_KIT ? M_KIT + MK * p : M_SOUND + MS * p;
}

static int stride(int kind)
{
    return kind == K_PATTERN ? MP : kind == K_KIT ? MK : MS;
}

/* a place's ref, as the mods know it */
static unsigned ref_of(int kind, int p)
{
    struct other *o;
    if (p < 0)
        return REF(W_EXT, 0);
    if (p >= OTHER) {
        if (kind == K_SOUND) {
            o = &others[(p - OTHER) >> 4];
            return o->kind == K_KIT ? REF(W_OTHER, o->serial * 16 + ((p - OTHER) & 15)) : REF(W_OTHER, o->serial);
        }
        return REF(W_OTHER, others[p - OTHER].serial);
    }
    if (kind == K_PATTERN)
        return p < OBJECTS ? REF(W_LIVE, p ? p - 1 : WORKING) : REF(W_IMAGE, p - OBJECTS ? p - OBJECTS - 1 : WORKING);
    if (kind == K_KIT)
        return p < 128 ? REF(W_LIVE, p) : p < 256 ? REF(W_SAVED, p - 128) : p < 384 ? REF(W_IMAGE, p - 256)
                                                                               : REF(W_IMAGE, WORKING);
    if (p >= POOL_IMAGE)
        return REF(W_IMAGE_POOL, p - POOL_IMAGE);
    if (p >= POOL_LIVE)
        return REF(W_POOL, p - POOL_LIVE);
    {
        unsigned k = p / 12, t = p % 12;
        return k < 128 ? REF(W_LIVE, k * 16 + t) : k < 256 ? REF(W_SAVED, (k - 128) * 16 + t)
               : k < 384 ? REF(W_IMAGE, (k - 256) * 16 + t) : REF(W_IMAGE, WORKING * 16 + t);
    }
}

/* ---- other memory ---- */

static unsigned rec_size(int kind, int stored)
{
    if (kind == K_PATTERN)
        return stored ? S_PATTERN : PATTERN_SIZE;
    if (kind == K_KIT)
        return stored ? S_KIT : KIT_SIZE;
    return stored ? S_SOUND : SOUND_SIZE;
}

/* the entry whose record of this kind starts at a (a sound: also a kit
 * entry's sound, *t its track; 12 for a sound entry), or -1 */
static int other_at(unsigned a, int kind, int *t)
{
    int i;
    unsigned o;
    for (i = 0; i < OTHERS; i++) {
        struct other *e = &others[i];
        if (!e->addr)
            continue;
        if (e->kind == kind && e->addr == a) {
            *t = 12;
            return i;
        }
        if (kind == K_SOUND && e->kind == K_KIT) {
            unsigned first = e->stored ? S_KIT_SOUNDS : KIT_SOUNDS, sz = e->stored ? S_SOUND : SOUND_SIZE;
            o = a - e->addr;
            if (a >= e->addr + first && (o - first) % sz == 0 && (o - first) / sz < 12) {
                *t = (o - first) / sz;
                return i;
            }
        }
    }
    return -1;
}

/* no longer follow anything that overlaps [a, b) */
static void others_drop(unsigned a, unsigned b)
{
    int i;
    for (i = 0; i < OTHERS; i++)
        if (others[i].addr && others[i].addr < b &&
            others[i].addr + rec_size(others[i].kind, others[i].stored) > a)
            others[i].addr = 0;
}

static int others_any(unsigned a, unsigned b)
{
    int i;
    for (i = 0; i < OTHERS; i++)
        if (others[i].addr && others[i].addr < b &&
            others[i].addr + rec_size(others[i].kind, others[i].stored) > a)
            return 1;
    return 0;
}

/* a kit place's sound j's mirror */
static unsigned char *kit_sound(int p, int j)
{
    return p >= OTHER ? others[p - OTHER].m + MK + MS * j : mirror(K_SOUND, p * 12 + j);
}

static void bytes(unsigned char *to, const unsigned char *from, int n)
{
    int i;
    for (i = 0; i < n; i++)
        to[i] = from ? from[i] : 0;
}

/* a record's mirror (a kit's with its sounds) from place p (-1: zeros) to
 * place q; no buffer: memcpy runs on small task stacks too */
static void copy_mirror(int kind, int p, int q)
{
    unsigned char *to = mirror(kind, q);
    int j;
    if (!to || p == q)
        return;
    bytes(to, p >= 0 ? mirror(kind, p) : 0, stride(kind));
    if (kind == K_KIT)
        for (j = 0; j < 12; j++)
            bytes(kit_sound(q, j), p >= 0 ? kit_sound(p, j) : 0, MS);
}

static int other_new(unsigned a, int kind, int stored)
{
    int i = others_at++ % OTHERS, k;
    for (k = 0; k < OTHERS; k++)                /* a free one first */
        if (!others[k].addr) {
            i = k;
            break;
        }
    others[i].addr = a;
    others[i].kind = kind;
    others[i].stored = stored;
    others[i].serial = ++serials;
    return i;
}

/* ---- a record moved: its mirror from p to q ---- */

static void move(int kind, int from, int to, int quiet)
{
    if (to < 0)
        return;
    copy_mirror(kind, from, to);
    if (!quiet)
        push(kind, ref_of(kind, from), ref_of(kind, to));
}

static void reset(int kind, int p, int quiet)
{
    if (p < 0)
        return;
    copy_mirror(kind, -1, p);
    if (!quiet)
        push(kind, REF(W_NONE, 0), ref_of(kind, p));
}

/* the fixed places of a kind, by runs of equal records */
struct span {
    unsigned base, size, count;
    int first;
};

static int spans(int kind, struct span *s)
{
    if (kind == K_PATTERN) {
        s[0] = (struct span){LIVE_PATTERNS, PATTERN_SIZE, OBJECTS, 0};
        s[1] = (struct span){S_PATTERNS, S_PATTERN, OBJECTS, OBJECTS};
        return 2;
    }
    if (kind == K_KIT) {
        s[0] = (struct span){LIVE_KITS, KIT_SIZE, 256, 0};
        s[1] = (struct span){S_KITS, S_KIT, 128, 256};
        s[2] = (struct span){S_WORKING_KIT, S_KIT, 1, 384};
        return 3;
    }
    s[0] = (struct span){LIVE_POOL, SOUND_SIZE, 128, POOL_LIVE};
    s[1] = (struct span){S_POOL, S_SOUND, 128, POOL_IMAGE};
    return 2;
}

/* calls f(kind, place, address) for each fixed place of the kind wholly
 * inside [a, b), the kits' sounds included for K_SOUND */
static int each_place(int kind, unsigned a, unsigned b, void (*f)(int, int, unsigned, unsigned), unsigned arg)
{
    struct span sp[3];
    int i, ns = spans(kind, sp), hits = 0;
    unsigned j, lo, hi, t;
    for (i = 0; i < ns; i++) {
        lo = a > sp[i].base ? a : sp[i].base;
        hi = sp[i].base + sp[i].size * sp[i].count;
        if (b < hi)
            hi = b;
        if (lo >= hi)
            continue;
        for (j = (lo - sp[i].base + sp[i].size - 1) / sp[i].size;
             j < sp[i].count && sp[i].base + sp[i].size * (j + 1) <= hi; j++) {
            f(kind, sp[i].first + j, sp[i].base + sp[i].size * j, arg);
            hits++;
        }
    }
    if (kind == K_SOUND) {
        spans(K_KIT, sp);
        for (i = 0; i < 3; i++) {
            unsigned first = i ? S_KIT_SOUNDS : KIT_SOUNDS, sz = i ? S_SOUND : SOUND_SIZE, k0, k1;
            lo = a > sp[i].base ? a : sp[i].base;
            hi = sp[i].base + sp[i].size * sp[i].count;
            if (b < hi)
                hi = b;
            if (lo >= hi)
                continue;
            k0 = (lo - sp[i].base) / sp[i].size;
            k1 = (hi - 1 - sp[i].base) / sp[i].size;
            for (j = k0; j <= k1 && j < sp[i].count; j++)
                for (t = 0; t < 12; t++) {
                    unsigned at = sp[i].base + sp[i].size * j + first + sz * t;
                    if (at >= a && at + sz <= b) {
                        f(kind, (sp[i].first + j) * 12 + t, at, arg);
                        hits++;
                    }
                }
        }
    }
    return hits;
}

static void copy_one(int kind, int p, unsigned at, unsigned delta)
{
    unsigned short sr = irq_mask();
    move(kind, place_of(kind, at + delta), p, 0);
    irq_restore(sr);
}

static void clear_one(int kind, int p, unsigned at, unsigned arg)
{
    unsigned short sr = irq_mask();
    (void)at;
    (void)arg;
    reset(kind, p, 0);
    irq_restore(sr);
}

static int near(unsigned a, unsigned b)
{
    /* the live project's patterns, kits and pool, or the stored project */
    return (b > LIVE_PATTERNS && a < LIVE_POOL + 128 * SOUND_SIZE) || (b > S && a < S + IMAGE_SIZE);
}

static int carries(int kind)
{
    return sidecar(kind) || (kind == K_KIT && sidecar(K_SOUND));
}

/* memcpy (dst, src, n), n >= 88, after the copy (rows.c). Not masked as a
 * whole: each record's move is. */
static void items_memcpy(unsigned dst, unsigned src, unsigned n);

void core_items_memcpy(unsigned dst, unsigned src, unsigned n)
{
#ifdef CORE_ITEMS_MEASURE
    unsigned t0 = DTCN0, d;
    items_memcpy(dst, src, n);
    d = DTCN0 - t0;
    core_items_cost.calls++;
    if (d > core_items_cost.call_max)
        core_items_cost.call_max = d;
#else
    items_memcpy(dst, src, n);
#endif
}

static void items_memcpy(unsigned dst, unsigned src, unsigned n)
{
    static const int kinds[3] = {K_PATTERN, K_KIT, K_SOUND};
    unsigned short sr;
    int ki, any = 0, kits = 0, i, p, kind, stored;
    if (!any_sidecar() || !ready || dst == src)
        return;
    if (!near(dst, dst + n) && !near(src, src + n) && !others_any(dst, dst + n) && !others_any(src, src + n))
        return;
    for (ki = 0; ki < 3; ki++) {
        kind = kinds[ki];
        if (!carries(kind) || (kind == K_SOUND && kits))
            continue;                           /* a kit's sounds went with it */
        i = each_place(kind, dst, dst + n, copy_one, src - dst);
        any += i;
        if (kind == K_KIT)
            kits = i;
    }
    if (any)
        return;
    /* not a place: other memory. A whole record from a place, or from other
     * memory core follows, is followed there; anything else there is not */
    sr = irq_mask();
    p = -1;
    kind = 0;
    stored = 0;
    for (ki = 0; ki < 3 && p < 0; ki++) {
        kind = kinds[ki];
        for (stored = 0; stored < 2; stored++)
            if (n == rec_size(kind, stored) && (p = place_of(kind, src)) >= 0 && stored_place(kind, p) == stored)
                break;
            else
                p = -1;
    }
    if (p >= 0 && carries(kind) && !(p >= OTHER && others_any(dst, dst + n) &&
                                      src < dst + n && src + n > dst)) {
        int e, q;
        others_drop(dst, dst + n);              /* src does not overlap dst: p stays */
        e = other_new(dst, kind, stored);
        q = OTHER + (kind == K_SOUND ? e * 16 + 12 : e);
        copy_mirror(kind, p, q);
        push(kind, ref_of(kind, p), ref_of(kind, q));
    } else {
        others_drop(dst, dst + n);
    }
    irq_restore(sr);
}

/* memset (n >= 88), operator new: [a, a + n) */
void core_items_drop(unsigned a, unsigned n)
{
    static const int kinds[3] = {K_PATTERN, K_KIT, K_SOUND};
    unsigned short sr;
    int ki;
    if (!any_sidecar() || !ready)
        return;
    if (!near(a, a + n) && !others_any(a, a + n))
        return;
    sr = irq_mask();
    others_drop(a, a + n);
    irq_restore(sr);
    for (ki = 0; ki < 3; ki++)
        if (sidecar(kinds[ki]))
            each_place(kinds[ki], a, a + n, clear_one, 0);
}

/* ---- the serializers (store.c): a live record and its stored form ---- */

static unsigned hash(const unsigned char *p, unsigned n)
{
    unsigned h = 2166136261u;
    while (n--)
        h = (h ^ *p++) * 16777619u;
    return h | 1;
}

static int image_place(int kind, unsigned stored)
{
    int p;
    if (stored < S || stored >= S + IMAGE_SIZE)
        return -1;
    p = place_of(kind, stored);
    return p >= OTHER ? -1 : p;
}

/* save: live -> stored. The hash runs unmasked (13,101 bytes for a
 * pattern): only the cache entry's update is masked. */
void core_items_save(int kind, unsigned stored, unsigned live)
{
    unsigned short sr;
    int from, to, i;
    unsigned h;
    if (!any_sidecar() || !ready || !stored || !live || (nested && kind == K_SOUND) || !carries(kind))
        return;
    if ((to = image_place(kind, stored)) < 0 && bulk)
        return;
    h = to < 0 ? hash((const unsigned char *)stored, rec_size(kind, 1)) : 0;
    sr = irq_mask();
    from = place_of(kind, live);
    if (to >= 0) {
        move(kind, from, to, 0);
    } else {
        struct out *o = &outs[outs_at++ % OUTS];
        o->hash = h;
        o->kind = kind;
        o->used = 1;
        o->serial = ++out_serials;
        bytes(o->m, from >= 0 ? mirror(kind, from) : 0, stride(kind));
        if (kind == K_KIT)
            for (i = 0; i < 12; i++)
                bytes(o->m + MK + MS * i, from >= 0 ? kit_sound(from, i) : 0, MS);
        push(kind, ref_of(kind, from), REF(W_OUT, o->serial));
    }
    irq_restore(sr);
}

/* load: stored -> live */
void core_items_load(int kind, unsigned live, unsigned stored)
{
    unsigned short sr;
    int from, to, i;
    unsigned h;
    if (!any_sidecar() || !ready || !stored || !live || (nested && kind == K_SOUND) || !carries(kind))
        return;
    from = image_place(kind, stored);
    h = from < 0 && !bulk ? hash((const unsigned char *)stored, rec_size(kind, 1)) : 0;
    sr = irq_mask();
    to = place_of(kind, live);
    if (from >= 0) {
        move(kind, from, to, 0);
    } else if (to >= 0) {
        struct out *o = 0;
        for (i = 0; h && i < OUTS; i++) {
            struct out *c = &outs[(outs_at - 1 - i) % OUTS];
            if (c->used && c->kind == kind && c->hash == h) {
                o = c;
                break;
            }
        }
        bytes(mirror(kind, to), o ? o->m : 0, stride(kind));
        if (kind == K_KIT)
            for (i = 0; i < 12; i++)
                bytes(kit_sound(to, i), o ? o->m + MK + MS * i : 0, MS);
        push(kind, o ? REF(W_OUT, o->serial) : REF(W_EXT, 0), ref_of(kind, to));
    }
    irq_restore(sr);
}

/* the inits: a fresh record */
void core_items_init(int kind, unsigned live)
{
    unsigned short sr;
    if (!any_sidecar() || !ready || !live || (nested && kind == K_SOUND))
        return;
    sr = irq_mask();
    reset(kind, place_of(kind, live), 0);
    irq_restore(sr);
}

/* a kit's serializer or init runs its sounds' own: they go with the kit */
void core_items_nest(int d)
{
    nested += d;
}

/* a whole project's load (from: the image it reads), save (to) or init:
 * the records move quietly, then one event per kind */
void core_items_bulk(int on, unsigned from, unsigned to)
{
    int k;
    static const int kinds[3] = {K_SOUND, K_KIT, K_PATTERN};
    if (on) {
        if (!bulk++) {
            bulk_from = from;
            bulk_to = to;
        }
        return;
    }
    if (--bulk)
        return;
    for (k = 0; k < 3; k++)
        if (sidecar(kinds[k]))
            push_all(kinds[k], bulk_from, bulk_to);
}

/* ---- the mods' calls ---- */

/* a sidecar field (store.h STORE_SIDECAR): its bits in item's mirror */
static unsigned char *field_mirror(unsigned field, void *item, int *w, int *o)
{
    int kind = field >> 28 & 7;
    *w = field >> 22 & 63;
    *o = field & 0xffff;
    if (!ready)
        return 0;
    return mirror(kind, place_of(kind, (unsigned)item));
}

unsigned core_items_get(unsigned field, void *item)
{
    unsigned v = 0;
    int w, o, i;
    unsigned short sr = irq_mask();
    unsigned char *m = field_mirror(field, item, &w, &o);
    if (m)
        for (i = 0; i < w; i++, o++)
            v = v << 1 | (m[o >> 3] >> (7 - (o & 7)) & 1);
    irq_restore(sr);
    return v;
}

void core_items_set(unsigned field, void *item, unsigned value)
{
    int w, o, i;
    unsigned short sr = irq_mask();
    unsigned char *m = field_mirror(field, item, &w, &o);
    if (m)
        for (i = 0; i < w; i++, o++) {
            unsigned bit = 0x80 >> (o & 7);
            if (value >> (w - 1 - i) & 1)
                m[o >> 3] |= bit;
            else
                m[o >> 3] &= ~bit;
        }
    irq_restore(sr);
}

/* a fixed sound place's address */
static unsigned sound_addr(int p)
{
    unsigned k = p / 12, t = p % 12;
    if (p >= POOL_IMAGE)
        return S_POOL + S_SOUND * (p - POOL_IMAGE);
    if (p >= POOL_LIVE)
        return LIVE_POOL + SOUND_SIZE * (p - POOL_LIVE);
    if (k < 256)
        return LIVE_KITS + KIT_SIZE * k + KIT_SOUNDS + SOUND_SIZE * t;
    if (k < 384)
        return S_KITS + S_KIT * (k - 256) + S_KIT_SOUNDS + S_SOUND * t;
    return S_WORKING_KIT + S_KIT_SOUNDS + S_SOUND * t;
}

/* the address of the item a ref names (0: none, or not in memory) */
void *core_item_addr(int kind, unsigned ref)
{
    unsigned w = ref >> 24, i = ref & 0xffffff, k, t;
    int e;
    if (kind == K_PATTERN) {
        if (w == W_LIVE)
            return i == WORKING ? (void *)LIVE_PATTERNS : i < 128 ? (void *)(LIVE_PATTERNS + PATTERN_SIZE * (i + 1)) : 0;
        if (w == W_IMAGE)
            return i == WORKING ? (void *)S_PATTERNS : i < 128 ? (void *)(S_PATTERNS + S_PATTERN * (i + 1)) : 0;
    } else if (kind == K_KIT) {
        if (w == W_LIVE && i < 128)
            return (void *)(LIVE_KITS + KIT_SIZE * i);
        if (w == W_SAVED && i < 128)
            return (void *)(LIVE_KITS + KIT_SIZE * (128 + i));
        if (w == W_IMAGE)
            return i == WORKING ? (void *)S_WORKING_KIT : i < 128 ? (void *)(S_KITS + S_KIT * i) : 0;
    } else if (kind == K_SOUND) {
        k = i >> 4;
        t = i & 15;
        if (w == W_POOL)
            return i < 128 ? (void *)(LIVE_POOL + SOUND_SIZE * i) : 0;
        if (w == W_IMAGE_POOL)
            return i < 128 ? (void *)(S_POOL + S_SOUND * i) : 0;
        if (t < 12 && w == W_LIVE && k < 128)
            return (void *)sound_addr(k * 12 + t);
        if (t < 12 && w == W_SAVED && k < 128)
            return (void *)sound_addr((128 + k) * 12 + t);
        if (t < 12 && w == W_IMAGE)
            return k == WORKING ? (void *)sound_addr(384 * 12 + t) : k < 128 ? (void *)sound_addr((256 + k) * 12 + t) : 0;
    }
    if (w == W_OTHER && ready)
        for (e = 0; e < OTHERS; e++) {
            struct other *o = &others[e];
            if (!o->addr)
                continue;
            if (o->kind == kind && o->serial == i)
                return (void *)o->addr;
            if (kind == K_SOUND && o->kind == K_KIT && o->serial == i >> 4 && (i & 15) < 12)
                return (void *)(o->addr + (o->stored ? S_KIT_SOUNDS + S_SOUND * (i & 15)
                                                       : KIT_SOUNDS + SOUND_SIZE * (i & 15)));
        }
    return 0;
}

/* the ref of the item at addr (0 when none follows it) */
unsigned core_item_ref(int kind, void *addr)
{
    int p = ready ? place_of(kind, (unsigned)addr) : -1;
    return p < 0 ? 0 : ref_of(kind, p);
}

/* ---- ev_tick (order 3): the area once, then the queue ---- */

static void deliver(const struct qev *e)
{
    const ev_items_t *f;
    for (f = ev_items; *f; f++)
        (*f)(e->kind, e->from, e->to, e->n);
}

void core_items_tick(void *ctrl)
{
    struct qev e;
    unsigned short sr;
    unsigned *p;
    (void)ctrl;
    if (!any_sidecar())
        return;
    if (!ready) {
        for (p = (unsigned *)M_PAT; p < (unsigned *)M_END; p++)
            *p = 0;
        ready = 1;
    }
    for (;;) {
        sr = irq_mask();
        if (over) {
            over = 0;
            qh = qt;
            irq_restore(sr);
            e.kind = RESYNC;
            e.from = e.to = 0;
            e.n = 0;
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
