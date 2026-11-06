/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-ar2 0.8: mod p-locks as lock rows (README, "Mod p-locks"), the second
 * part of the lock service. Every address is OS 1.74's.
 *
 * A pattern keeps its p-locks in a pool of 72 rows of 66 bytes, [code]
 * [track][64 step bytes] (a step byte with bit 7 set has no lock); stock's
 * codes are 0..41, a mod parameter's is its lock code (42..127, lock.c). The
 * rows are only the stored form: stock edits and plays the live lock store,
 * an 88-byte record per step (42 lock words and a count), and rebuilds the
 * rows from it (0x40009216), which drops every row it does not know. A step
 * record has no room for more, so core keeps the mod locks itself, in a
 * ledger: {the step record's address, code, value}, sorted by address. Stock
 * moves step records with memcpy and clears them with a few routines, so
 * the ledger follows them by address, wherever they go:
 *
 *   - memcpy (0x40098bc0) of 88 bytes or more: the locks of the step records
 *     copied go with them, and those the copy overwrote go; whole patterns,
 *     tracks, pages and trigs, to and from the clipboards, the undo buffers
 *     and a trig shift's swap alike;
 *   - memset (0x40098c08), operator new (0x40082a40, 0x40082a46: memory
 *     given out afresh), a track's clear (0x4001197c), a step's (0x400ac86e)
 *     and pattern init (st_pat_init): the locks there go;
 *   - row load (0x40008f98): the rows with codes 42..127 (stock skips them)
 *     become the locks of the live lock store loaded;
 *   - rebuild (0x40009216): after stock's rows, the lock store's mod locks as
 *     rows, in code order then track order, each where one still fits under
 *     stock's 71 (stock skips a pair that does not fit and carries on, and so
 *     does core), so stock's locks always win;
 *   - trig copy and Euclid's convert (0x400c4d62, 0x400c3eb0) list a step's
 *     locks as {value, code, fine} for a later paste: core adds the step's
 *     mod locks to the list, and the paste's lock set (0x400c058c), given a
 *     code above 41, sets a mod lock;
 *   - the sound parameter set's lock calls (vtable +60 get, +64 set, +68
 *     clear, +72 turn) take a mod parameter's knob (its page id, lock.c)
 *     with a trig held; from 0.9 the FX set's too (vtable 0x401dcea0), for a
 *     kit parameter's knob on the FX track (track 12): stock's FX calls find
 *     the step as the sound's do, on the current track;
 *   - live recording: a mod parameter's knob turned and its NRPN or CC
 *     received (pages.s, midi.c) lock the value on the step the track
 *     plays, as stock's live record (0x400af762) locks its own;
 *   - playback (0x4009a954, a trig's locks): core notes the trig's mod
 *     locks per track, which mods read with core_lock_value.
 *
 * After a change to a live pattern's locks, core writes its mod rows into
 * the stored pattern, as stock's rebuild would (stock does not rebuild for a
 * change it does not see).
 *
 * Codes are a build's (its lockfile's), so mod rows leave the unit in a
 * pattern's SysEx dump only with a marker row, code 127 and track 0xfe, then
 * the sender's layout ID: core adds it to the dump's buffer after stock's
 * pattern save (0x4000eff2), or, with no free row for it, leaves the mod
 * rows out. A received pattern (0x4000d79c, once stock has checked it)
 * keeps its mod rows only with a marker holding this build's own layout ID;
 * the marker never stays. A project of a layout this build does not read
 * (st_proj_load) loses its mod rows.
 *
 * A parameter above 127 (lock.c, core_lock_shift) is locked whole: its lock
 * takes two rows per track, the value's bits 13..7 in the first and its bits
 * 6..0 in a second, with the code and the track + 0x40 (LOW), as stock keeps
 * a fine lock in two rows; core places both or neither.
 */

#include <stdint.h>

#define NEW ((unsigned (*)(unsigned))0x400827c4)         /* operator new's body; d0 */
#define DELETE ((void (*)(void *))0x40082a4c)
#define APP ((unsigned (*)(void))0x4017fa0a)
#define APP_TRACKS ((unsigned (*)(unsigned))0x400ae492)
#define CURRENT_TRACK ((int (*)(unsigned))0x400b693e)
#define CURRENT_PATTERN ((unsigned (*)(unsigned))0x400af0ac)
#define TRACK_REC ((unsigned (*)(unsigned, int))0x400ab29e)
#define TRACK_LENGTH ((int (*)(unsigned))0x400c0524)
#define LIST_PUSH ((void (*)(void *, void *))0x401a034a)   /* (vector, &{value, code, fine}) */
#define STOCK_LOCK_GET ((int (*)(void *, int, int))0x400a98b4)
#define STOCK_LOCK_SET ((int (*)(void *, int, int, int))0x400a9926)
#define STOCK_LOCK_CLEAR ((int (*)(void *, int, int))0x400a99c2)
#define STOCK_LOCK_TURN ((int (*)(void *, int, int, int))0x400a8560)   /* the FX set's too */
#define FX_LOCK_GET ((int (*)(void *, int, int))0x400a9a26)           /* the FX set's (0.9) */
#define FX_LOCK_SET ((int (*)(void *, int, int, int))0x400a9a98)
#define FX_LOCK_CLEAR ((int (*)(void *, int, int))0x400a9b0e)
#define REC_STATE ((unsigned (*)(void))0x401816f4)
#define LIVE_RECORDING ((unsigned char (*)(unsigned))0x40036370)
#define SEQUENCER ((unsigned (*)(void))0x40181790)
#define PLAY_STEP ((int (*)(unsigned, int))0x4003532c)          /* (sequencer, track) */
#define SCALED ((unsigned char (*)(unsigned))0x400c29f8)        /* the track's own scale */
#define SCALED_STEP ((int (*)(unsigned, int))0x400c2a2c)
#define QUANTIZED ((signed char (*)(unsigned))0x400c0e6e)
#define QUANTIZED_STEP ((signed char (*)(unsigned, int))0x400c12ae)
#define HAS_TRIG ((unsigned char (*)(unsigned, int))0x400bff7e)
#define HAS_LOCK_TRIG ((unsigned char (*)(unsigned, int))0x400c000e)
#define ADD_LOCK_TRIG ((void (*)(unsigned, int, int))0x400c2698)

#define LIVE_PATTERNS 0x407c89e3u               /* the live project +24: 129 objects */
#define PATTERN_SIZE 85760u
#define LIVE_LOCKS 0x2e91                       /* the live lock store in an object */
#define TRACK_LOCKS 5674
#define STEP_LOCKS 88
#define STEPS_LOCKS (64 * STEP_LOCKS)           /* a track's step records */
#define STORE_LOCKS (13 * TRACK_LOCKS)
#define STORED_PATTERNS (0x405c8f20u + 0x18)     /* the stored project's 129 slots */
#define STORED_PATTERN 13101
#define STORED_ROWS 0x2091
#define ROWS 72
#define ROW 66
#define MAX_USED 71
#define FIRST_CODE 42
#define LAST_CODE 127
#define CODES (LAST_CODE - FIRST_CODE + 1)
#define DEAD 0xff
#define LOW 0x40                                /* a low row's track: + the track */
#define NONE 0xffff
#define MOST 16384                              /* the ledger's entries at most */

struct ent {
    unsigned addr;              /* the step record */
    unsigned char code;         /* 42..127 a mod lock, 128.. a step tag (0.11), DEAD once gone */
    unsigned char pad;
    unsigned short value;       /* 0..16383; a tag's handle */
};

struct ledger {
    int n;
    struct ent e[];
};

struct core_lock_param;
const struct core_lock_param *core_lock_by_id(int id, unsigned *code);
const struct core_lock_param *core_lock_by_code(unsigned code);
const struct core_lock_param *core_lock_set_param(void *set, int id, unsigned *code, unsigned char **item);
int core_lock_code(unsigned field);
unsigned core_lock_field(const struct core_lock_param *p);
int core_lock_min(const struct core_lock_param *p);
int core_lock_max(const struct core_lock_param *p);
int core_lock_shift(const struct core_lock_param *p);
unsigned core_store_get(unsigned field, void *item, int index);
extern const unsigned core_store_ids[];         /* this build's layout ID first */

/* steps.c (0.11): the live patterns' dense tag tables */
#define TAG_CODE 128                            /* a step tag's code: TAG_CODE + its owner */
int core_steps_any(unsigned a, unsigned b);
void core_steps_copy(unsigned dst, unsigned src, unsigned n);
int core_steps_src(unsigned dst, unsigned src, unsigned n, struct ent *out);
void core_steps_clear(unsigned a, unsigned b, int quiet);
void core_steps_reload(unsigned store);
int core_steps_list(unsigned addr, int (*push)(void *, int, int), void *list);
int core_steps_paste(unsigned addr, int owner, int handle);
int core_steps_live(unsigned addr);

/* (0.11) the ledger, split per region: one array for each of the 129 live
 * pattern objects' lock stores and one for everything else (the heap: the
 * clipboards, the undo buffers, a trig shift's temporaries), each sorted by
 * address. A change rebuilds only its region's array, so its cost follows
 * that region's entries, not the whole ledger's (0.8 had one array). */
#define REGIONS 130
#define HEAP_REGION 129

static struct ledger *volatile led[REGIONS];
static volatile unsigned gen[REGIONS];          /* every change, to the array or in place */
static volatile unsigned short dead[REGIONS];   /* entries marked DEAD since the last rebuild */

/* the playing trig's mod locks, per track, each value + 1 (0: none, so a
 * track no trig has played yet has none); now_set: the codes the track's
 * last trig set, which the next one clears, so a trig costs its own rows,
 * not one store a code */
volatile unsigned short core_lock_now[13][CODES];
static unsigned char now_set[13][CODES], now_n[13];

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

static void copy_bytes(void *d, const void *s, unsigned n)
{
    unsigned char *a = d;
    const unsigned char *b = s;
    while (n--)
        *a++ = *b++;
}

/* the live pattern whose lock store holds addr, or -1 */
static int live_of(unsigned addr)
{
    unsigned o = addr - LIVE_PATTERNS - LIVE_LOCKS;
    if (addr < LIVE_PATTERNS + LIVE_LOCKS || o / PATTERN_SIZE >= 129 ||
        o % PATTERN_SIZE >= STORE_LOCKS)
        return -1;
    return o / PATTERN_SIZE;
}

static unsigned store_of(int k)
{
    return LIVE_PATTERNS + PATTERN_SIZE * k + LIVE_LOCKS;
}

static int region_of(unsigned addr)
{
    int k = live_of(addr);
    return k < 0 ? HEAP_REGION : k;
}

/* the live regions [*lo, *hi] whose lock stores [a, b) overlaps; 0 for none */
static int live_span(unsigned a, unsigned b, int *lo, int *hi)
{
    unsigned base = LIVE_PATTERNS + LIVE_LOCKS;
    if (b <= base || a >= store_of(128) + STORE_LOCKS || b <= a)
        return 0;
    *lo = a <= base ? 0 : (a - base) / PATTERN_SIZE;
    if (store_of(*lo) + STORE_LOCKS <= a)
        ++*lo;
    *hi = (b - 1 - base) / PATTERN_SIZE;
    if (*hi > 128)
        *hi = 128;
    return *lo <= *hi;
}

/* the first entry at or above addr */
static int lower(const struct ledger *l, unsigned addr)
{
    int lo = 0, hi = l->n, m;
    while (lo < hi) {
        m = (lo + hi) / 2;
        if (l->e[m].addr < addr)
            lo = m + 1;
        else
            hi = m;
    }
    return lo;
}

static int any_in_r(const struct ledger *l, unsigned a, unsigned b)
{
    int i;
    for (i = l ? lower(l, a) : 0; l && i < l->n && l->e[i].addr < b; i++)
        if (l->e[i].code != DEAD)
            return 1;
    return 0;
}

static int any_in(unsigned a, unsigned b)
{
    int k, lo, hi;
    if (any_in_r(led[HEAP_REGION], a, b))
        return 1;
    if (live_span(a, b, &lo, &hi))
        for (k = lo; k <= hi; k++)
            if (any_in_r(led[k], a, b))
                return 1;
    return 0;
}

/* the locks in [a, b) go, in place (no memory taken: also from operator new
 * and from interrupts) */
static void drop_r(int r, unsigned a, unsigned b)
{
    struct ledger *l = led[r];
    int i;
    for (i = l ? lower(l, a) : 0; l && i < l->n && l->e[i].addr < b; i++)
        if (l->e[i].code != DEAD) {
            l->e[i].code = DEAD;
            dead[r]++;
            gen[r]++;
        }
}

static void drop(unsigned a, unsigned b)
{
    int k, lo, hi;
    drop_r(HEAP_REGION, a, b);
    if (live_span(a, b, &lo, &hi))
        for (k = lo; k <= hi; k++)
            drop_r(k, a, b);
}

/* the ledger's live entries, all regions (the cap, MOST) */
static int live_total(void)
{
    int r, t = 0;
    for (r = 0; r < REGIONS; r++)
        if (led[r])
            t += led[r]->n - dead[r];
    return t;
}

/* region r's array with [a, b) holding only those of add[0..k) in region r
 * (add sorted by address, each in [a, b)); 0 when it could not be made */
static int replace_r(int r, unsigned a, unsigned b, const struct ent *add, int k)
{
    struct ledger *l, *n;
    unsigned g;
    unsigned short sr;
    int i, j, live, mine, others;
    for (mine = i = 0; i < k; i++)
        mine += region_of(add[i].addr) == r;
    for (;;) {
        l = led[r];
        g = gen[r];
        live = mine;
        for (i = 0; l && i < l->n; i++)
            if (l->e[i].code != DEAD && (l->e[i].addr < a || l->e[i].addr >= b))
                live++;
        others = live_total() - (l ? l->n - dead[r] : 0);
        if (live + others > MOST)
            return 0;
        if (!live && !l)
            return 1;
        n = live ? (struct ledger *)NEW(4 + sizeof(struct ent) * live) : 0;
        if (live && !n)
            return 0;
        j = 0;
        for (i = 0; l && i < l->n && l->e[i].addr < a; i++)
            if (l->e[i].code != DEAD)
                n->e[j++] = l->e[i];
        for (live = 0; live < k; live++)
            if (region_of(add[live].addr) == r)
                n->e[j++] = add[live];
        for (; l && i < l->n; i++)
            if (l->e[i].code != DEAD && l->e[i].addr >= b)
                n->e[j++] = l->e[i];
        if (n)
            n->n = j;
        sr = irq_mask();
        if (led[r] == l && gen[r] == g) {
            led[r] = n;
            dead[r] = 0;
            gen[r]++;
            irq_restore(sr);
            if (l)
                DELETE(l);
            return 1;
        }
        irq_restore(sr);
        if (n)
            DELETE(n);
    }
}

/* the ledger with [a, b) holding only add[0..k) (sorted, each in [a, b)),
 * region by region; 0 when a region's could not be made */
static int replace(unsigned a, unsigned b, const struct ent *add, int k)
{
    int r, lo, hi, ok = 1;
    if (live_span(a, b, &lo, &hi))
        for (r = lo; r <= hi; r++)
            ok &= replace_r(r, a, b, add, k);
    return replace_r(HEAP_REGION, a, b, add, k) & ok;
}

/* a step record's entries, into out[] by code (max of them): its mod locks,
 * and with tags its step tags too; their count */
static int step_ents(unsigned addr, struct ent *out, int max, int tags)
{
    struct ledger *l = led[region_of(addr)];
    int i, k = 0;
    for (i = l ? lower(l, addr) : 0; l && i < l->n && l->e[i].addr == addr && k < max; i++)
        if (l->e[i].code != DEAD && (tags || l->e[i].code <= LAST_CODE))
            out[k++] = l->e[i];
    return k;
}

static int step_locks(unsigned addr, struct ent *out)
{
    return step_ents(addr, out, CODES, 0);
}

/* (0.11) steps.c: a step record's sparse tag of owner (outside the live
 * patterns), or -1 */
int core_rows_tag_at(unsigned addr, int owner)
{
    struct ledger *l = led[region_of(addr)];
    int i;
    for (i = l ? lower(l, addr) : 0; l && i < l->n && l->e[i].addr == addr; i++)
        if (l->e[i].code == TAG_CODE + owner)
            return l->e[i].value;
    return -1;
}

/* (0.11) steps.c: every sparse tag of owner, fn(handle) */
void core_rows_tags_walk(int owner, void (*fn)(int, int, int, int))
{
    struct ledger *l;
    int r, i;
    for (r = 0; r < REGIONS; r++)
        for (l = led[r], i = 0; l && i < l->n; i++)
            if (l->e[i].code == TAG_CODE + owner)
                fn(-1, -1, -1, l->e[i].value);
}

/* sort by address (a merge sort; tmp holds n) */
static void sort_ents(struct ent *e, struct ent *tmp, int n)
{
    int w, i, a, b, ea, eb, j;
    for (w = 1; w < n; w *= 2) {
        for (i = 0; i < n; i += 2 * w) {
            a = i;
            ea = i + w < n ? i + w : n;
            b = ea;
            eb = i + 2 * w < n ? i + 2 * w : n;
            for (j = i; a < ea || b < eb; j++)
                tmp[j] = b >= eb || (a < ea && e[a].addr <= e[b].addr) ? e[a++] : e[b++];
        }
        copy_bytes(e, tmp, sizeof(struct ent) * n);
    }
}

/* ---- live patterns and their stored rows ---- */

static unsigned char *stored_rows(int k)
{
    return (unsigned char *)STORED_PATTERNS + STORED_PATTERN * k + STORED_ROWS;
}

static int mod_code(signed char c)
{
    return c >= FIRST_CODE && c <= LAST_CODE;
}

static int empty(const unsigned char *r)
{
    return r[0] == 0xff && r[1] == 0xff;
}

/* a mod row, a first one or a low one */
static int mod_row(const unsigned char *r)
{
    return mod_code(r[0]) && (r[1] < 13 || (r[1] >= LOW && r[1] < LOW + 13));
}

/* does a code's lock take two rows? */
static int wide(int code)
{
    const struct core_lock_param *p = core_lock_by_code(code);
    return p && core_lock_shift(p);
}

static int used(const unsigned char *rows)
{
    int i, u = 0;
    for (i = 0; i < ROWS; i++)
        if (!empty(rows + ROW * i))
            u++;
    return u;
}

/* a lock store's mod locks as rows after the ones in rows, in code order
 * then track order, where they fit under stock's 71 (a wide code's two rows
 * or neither) */
static void append(unsigned char *rows, unsigned store)
{
    struct ledger *l = led[region_of(store)];
    unsigned char at[CODES][13], lo[CODES][13], *r;
    unsigned o;
    int u, code, t, s, i, n, w, k = 0, first, last;
    if (!l)
        return;
    first = lower(l, store);
    last = lower(l, store + STORE_LOCKS);
    for (code = 0; code < CODES; code++)
        for (t = 0; t < 13; t++)
            at[code][t] = lo[code][t] = 0xff;
    for (i = first; i < last; i++)
        if (l->e[i].code <= LAST_CODE && (o = l->e[i].addr - store) % TRACK_LOCKS < STEPS_LOCKS)
            at[l->e[i].code - FIRST_CODE][o / TRACK_LOCKS] = 0xfe;
    u = used(rows);
    for (code = 0; code < CODES; code++) {
        w = wide(FIRST_CODE + code);
        for (t = 0; t < 13; t++) {
            if (at[code][t] == 0xff)
                continue;
            at[code][t] = 0xff;
            if (u + 1 + w > MAX_USED)
                continue;
            for (n = 0; n <= w; n++) {
                while (k < ROWS && !empty(rows + ROW * k))
                    k++;
                if (k == ROWS)
                    goto fill;
                r = rows + ROW * k;
                r[0] = FIRST_CODE + code;
                r[1] = n ? LOW + t : t;
                for (s = 0; s < 64; s++)
                    r[2 + s] = 0xff;
                if (n)
                    lo[code][t] = k;
                else
                    at[code][t] = k;
            }
            u += 1 + w;
        }
    }
fill:
    for (i = first; i < last; i++)
        if (l->e[i].code <= LAST_CODE && (o = l->e[i].addr - store) % TRACK_LOCKS < STEPS_LOCKS) {
            code = l->e[i].code - FIRST_CODE;
            t = o / TRACK_LOCKS;
            s = o % TRACK_LOCKS / STEP_LOCKS;
            n = lo[code][t];
            if ((k = at[code][t]) >= ROWS || (wide(FIRST_CODE + code) && n >= ROWS))
                continue;
            if (n < ROWS) {
                rows[ROW * k + 2 + s] = l->e[i].value >> 7 & 127;
                rows[ROW * n + 2 + s] = l->e[i].value & 127;
            } else {
                rows[ROW * k + 2 + s] = l->e[i].value > 127 ? 127 : l->e[i].value;
            }
        }
}

/* the mod rows go */
static void unappend(unsigned char *rows)
{
    int i, j;
    for (i = 0; i < ROWS; i++)
        if (mod_row(rows + ROW * i))
            for (j = 0; j < ROW; j++)
                rows[ROW * i + j] = j < 2 ? 0xff : 0;
}

/* the stored pattern of live pattern k takes its mod rows afresh */
static void sync(int k)
{
    unsigned char *rows = stored_rows(k);
    unappend(rows);
    append(rows, store_of(k));
}

/* the live patterns whose lock stores [a, b) touches take theirs */
static void changed(unsigned a, unsigned b)
{
    int k;
    for (k = 0; k < 129; k++)
        if (store_of(k) < b && store_of(k) + STORE_LOCKS > a)
            sync(k);
}

/* ---- the sites ---- */

/* the entries of [src, src + n) whose step record was copied whole, moved
 * to dst, into out (sorted by address) when out is given; their count. From
 * 0.11 a step tag that lands in a live pattern's step goes into its dense
 * table instead (core_steps_copy), and a dense tag copied out of one comes
 * in as a sparse entry (core_steps_src). */
static int moved(unsigned dst, unsigned src, unsigned n, struct ent *out)
{
    struct ledger *l;
    int r, lo, hi, i, k = 0, runs = 0, any;
    unsigned to;
    for (r = live_span(src, src + n, &lo, &hi) ? lo : HEAP_REGION; r <= HEAP_REGION;
         r = r == hi ? HEAP_REGION : r + 1) {
        l = led[r];
        any = 0;
        for (i = l ? lower(l, src) : 0; l && i < l->n && l->e[i].addr < src + n; i++)
            if (l->e[i].code != DEAD && l->e[i].addr + STEP_LOCKS <= src + n) {
                to = l->e[i].addr + (dst - src);
                if (l->e[i].code >= TAG_CODE && core_steps_live(to) >= 0)
                    continue;
                if (out) {
                    out[k] = l->e[i];
                    out[k].addr = to;
                }
                k++;
                any = 1;
            }
        runs += any;
        if (r == HEAP_REGION)
            break;
    }
    return k | (runs > 1 ? 0x40000000 : 0);
}

/* memcpy (dst, src, n), after the copy (rows.s): n >= 88 */
void core_rows_memcpy(unsigned dst, unsigned src, unsigned n)
{
    struct ent *add;
    int k, d, runs;
    int dense = core_steps_any(dst, dst + n) || core_steps_any(src, src + n);
    int from = any_in(src, src + n), to = any_in(dst, dst + n);
    if (dst == src || (!dense && !from && !to))
        return;
    if (dense || from)                  /* (0.11) a sparse tag may land in a live step */
        core_steps_copy(dst, src, n);
    d = dense ? core_steps_src(dst, src, n, 0) : 0;
    if (!from && !to && !d)
        return;                         /* (0.11) only dense tags, moved: the ledger keeps */
    k = moved(dst, src, n, 0);
    runs = k >> 30;
    k &= 0x3fffffff;
    if (!k && !d) {
        drop(dst, dst + n);
    } else if ((add = (struct ent *)NEW(sizeof(struct ent) * (k + d) * 2)) != 0) {
        moved(dst, src, n, add);
        if (d)
            core_steps_src(dst, src, n, add + k);
        if (runs || (d && k))
            sort_ents(add, add + k + d, k + d);
        if (!replace(dst, dst + n, add, k + d))
            drop(dst, dst + n);
        DELETE(add);
    } else {
        drop(dst, dst + n);
    }
    changed(dst, dst + n);
}

/* memset (dst, c, n) and operator new's block (rows.s), track clear
 * (data, locks: rows.s): the locks there go; (0.11) and the step tags */
void core_rows_drop(unsigned a, unsigned n)
{
    int dense = core_steps_any(a, a + n);
    if (!dense && !any_in(a, a + n))
        return;
    if (dense)
        core_steps_clear(a, a + n, 0);
    drop(a, a + n);
    changed(a, a + n);
}

/* pattern init (st_pat_init) */
void core_rows_clear(unsigned obj)
{
    core_rows_drop(obj + LIVE_LOCKS, STORE_LOCKS);
}

/* 0x400ac86e, a step's clear (pattern, track, step), after stock (rows.s) */
void core_rows_step_clear(unsigned pat, int track, int step)
{
    unsigned locks;
    if (!pat || (unsigned)track > 12 || (unsigned)step > 63)
        return;
    locks = (*(unsigned (**)(unsigned))(*(unsigned *)pat + 40))(pat);
    if (locks)
        core_rows_drop(locks + TRACK_LOCKS * track + STEP_LOCKS * step, STEP_LOCKS);
}

/* row load (0x40008f98: live lock store, rows), after stock */
void core_rows_load(unsigned store, const unsigned char *rows)
{
    unsigned char ord[ROWS];
    const unsigned char *r;
    struct ent *add;
    int i, j, k, t, s, x;
    if (!store || !rows)
        return;
    core_steps_reload(store);           /* (0.11) its step tags go: RELOAD */
    for (i = k = 0; i < ROWS; i++)
        if (mod_code(rows[ROW * i]) && rows[ROW * i + 1] < 13)
            ord[k++] = i;
    for (i = 1; i < k; i++)             /* by track, then code */
        for (j = i; j > 0; j--) {
            const unsigned char *a = rows + ROW * ord[j - 1], *b = rows + ROW * ord[j];
            if (a[1] < b[1] || (a[1] == b[1] && a[0] <= b[0]))
                break;
            x = ord[j];
            ord[j] = ord[j - 1];
            ord[j - 1] = x;
        }
    for (i = x = 0; i < k; i++)
        for (s = 0; s < 64; s++)
            x += !(rows[ROW * ord[i] + 2 + s] & 0x80);
    if (!x) {
        drop(store, store + STORE_LOCKS);
        return;
    }
    if (!(add = (struct ent *)NEW(sizeof(struct ent) * x)))
        return;
    for (t = x = 0; t < 13; t++)
        for (s = 0; s < 64; s++)
            for (i = 0; i < k; i++) {
                r = rows + ROW * ord[i];
                if (r[1] != t || (r[2 + s] & 0x80))
                    continue;
                add[x].addr = store + TRACK_LOCKS * t + STEP_LOCKS * s;
                add[x].code = r[0];
                add[x].value = r[2 + s];
                for (j = 0; j < ROWS; j++)              /* its low row */
                    if (rows[ROW * j] == r[0] && rows[ROW * j + 1] == LOW + t) {
                        add[x].value = r[2 + s] << 7 | (rows[ROW * j + 2 + s] & 127);
                        break;
                    }
                add[x++].pad = 0;
            }
    if (!replace(store, store + STORE_LOCKS, add, x))
        drop(store, store + STORE_LOCKS);
    DELETE(add);
}

/* rebuild (0x40009216: rows, live lock store), after stock */
void core_rows_save(unsigned char *rows, unsigned store)
{
    if (rows && store) {
        unappend(rows);
        append(rows, store);
    }
}

/* ---- SysEx pattern dumps and foreign projects ---- */

#define PATTERN_SAVE ((int (*)(unsigned char *, void *, int))0x4000937c)
#define MARK_CODE 127
#define MARK_TRACK 0xfe

static int marker(const unsigned char *r)
{
    return r[0] == MARK_CODE && r[1] == MARK_TRACK;
}

/* 0x4000eff2 (rows.s): stock's pattern save into a dump's buffer (13,101
 * bytes, a stored pattern's form), then the marker for its mod rows */
int core_dump_out(unsigned char *buf, void *live, int a)
{
    int r = PATTERN_SAVE(buf, live, a), i, any = 0, free = -1;
    unsigned char *rows = buf + STORED_ROWS, *m;
    for (i = 0; i < ROWS; i++) {
        any |= mod_code(rows[ROW * i]) && rows[ROW * i + 1] < 13;
        if (free < 0 && empty(rows + ROW * i))
            free = i;
    }
    if (!any)
        return r;
    if (free < 0) {
        unappend(rows);
        return r;
    }
    m = rows + ROW * free;
    for (i = 0; i < ROW; i++)
        m[i] = 0xff;
    m[0] = MARK_CODE;
    m[1] = MARK_TRACK;
    m[2] = core_store_ids[0] >> 24;
    m[3] = core_store_ids[0] >> 16;
    m[4] = core_store_ids[0] >> 8;
    m[5] = core_store_ids[0];
    return r;
}

/* 0x4000d79c (rows.s): a received pattern's buffer, checked, before stock
 * loads it */
void core_dump_in(unsigned char *buf)
{
    unsigned char *rows = buf + STORED_ROWS, *r;
    unsigned id;
    int i, j, ok = 0;
    for (i = 0; i < ROWS; i++)
        if (marker(r = rows + ROW * i)) {
            id = (unsigned)r[2] << 24 | (unsigned)r[3] << 16 | (unsigned)r[4] << 8 | r[5];
            ok = id && id == core_store_ids[0];
            for (j = 0; j < ROW; j++)
                r[j] = j < 2 ? 0xff : 0;
        }
    if (!ok)
        unappend(rows);
}

/* st_proj_load: a project of a layout this build does not read keeps no mod
 * locks */
void core_rows_forget(void)
{
    int k;
    for (k = 0; k < 129; k++) {
        unappend(stored_rows(k));
        drop(store_of(k), store_of(k) + STORE_LOCKS);
        core_steps_clear(store_of(k), store_of(k) + STORE_LOCKS, 1);
    }
}

/* ---- trig copy and paste ---- */

struct listed {
    int value;                  /* 8.8, as stock's lock words */
    int code;
    int fine;
};

static unsigned step_addr(unsigned rec, int step)
{
    unsigned pat = *(unsigned *)(rec + 44), locks;
    int track = *(int *)(rec + 64);
    if (!pat || (unsigned)track > 12 || (unsigned)step > 63)
        return 0;
    locks = (*(unsigned (**)(unsigned))(*(unsigned *)pat + 40))(pat);
    return locks ? locks + TRACK_LOCKS * track + STEP_LOCKS * step : 0;
}

static void *tag_list;

/* core_steps_list's push: e is the caller's entry, the list the one set below */
static int list_tag(void *entry, int code, int handle)
{
    struct listed *e = entry;
    e->value = handle << 8;
    e->code = code;
    e->fine = 0;
    LIST_PUSH(tag_list, e);
    return 0;
}

/* 0x400c4d62, 0x400c3eb0 (rows.s): the step's mod locks join its list */
void core_rows_list(unsigned rec, int step, void *list, struct listed *e)
{
    struct ent at[CODES];
    unsigned a = step_addr(rec, step);
    int i, k = a ? step_locks(a, at) : 0;
    tag_list = list;
    for (i = 0; i < k; i++) {
        e->value = at[i].value << 8;
        e->code = at[i].code;
        e->fine = 0;
        LIST_PUSH(list, e);
    }
    if (a)                              /* (0.11) and its step tags, code TAG_CODE + owner */
        core_steps_list(a, list_tag, e);
}

static int put(unsigned addr, int code, int v);

/* 0x400c058c (rows.s): (track record, step, code, value 8.8, fine) for a
 * code above 41; 1 when it was set */
static int clamp(const struct core_lock_param *p, int v);

int core_rows_paste(unsigned rec, int step, int code, int value)
{
    const struct core_lock_param *p;
    unsigned a;
    if (code < FIRST_CODE || code >= DEAD || TRACK_LENGTH(rec) < step || !(a = step_addr(rec, step)))
        return 0;
    value >>= 8;
    if (code >= TAG_CODE)               /* (0.11) a listed step tag */
        return core_steps_paste(a, code - TAG_CODE, value);
    p = core_lock_by_code(code);
    return put(a, code, p ? clamp(p, value) : value < 0 ? 0 : value > 127 ? 127 : value);
}

/* ---- live recording ---- */

/* the step live recording locks on the track (its record in *rec), found
 * as stock's live record finds it (0x400af762), which adds a lock trig to a
 * step without a trig; -1 for none (not recording, or a quantized step
 * that is none) */
static int record_step(int track, unsigned *rec)
{
    int step;
    if ((unsigned)track >= 13 || !LIVE_RECORDING(REC_STATE()))
        return -1;
    step = PLAY_STEP(SEQUENCER(), track);
    *rec = TRACK_REC(CURRENT_PATTERN(APP()), track);
    if (SCALED(*rec))
        step = SCALED_STEP(*rec, step);
    if (QUANTIZED(*rec) == 1)
        return QUANTIZED_STEP(*rec, step);
    if (!HAS_TRIG(*rec, step) && !HAS_LOCK_TRIG(*rec, step))
        ADD_LOCK_TRIG(*rec, step, 1);
    return step;
}

/* the value (0..16383) a mod parameter's knob or MIDI gave the track's
 * sound: while live recording runs, it is locked on the step the track
 * plays */
void core_lock_record(int track, int code, int value)
{
    unsigned rec;
    int step = record_step(track, &rec);
    if (step >= 0)
        core_rows_paste(rec, step, code, value << 8);
}

/* ---- playback: the trig's locks (0x4009a954: track, out, the step's
 * locks, ...) ---- */
void core_seq_edge(void);

void core_rows_trig(int track, unsigned step)
{
    struct ledger *l = led[region_of(step)];
    int i, c, n;
    if ((unsigned)track >= 13)
        return;
    core_seq_edge();                    /* (0.11) the first step after PLAY first (seq.c) */
    for (i = 0; i < now_n[track]; i++)
        core_lock_now[track][now_set[track][i]] = 0;
    n = 0;
    for (i = l ? lower(l, step) : 0; l && i < l->n && l->e[i].addr == step; i++)
        if (l->e[i].code <= LAST_CODE && n < CODES) {
            c = l->e[i].code - FIRST_CODE;
            core_lock_now[track][c] = l->e[i].value + 1;
            now_set[track][n++] = c;
        }
    now_n[track] = n;
}

/* a new or loaded project (st_proj_init, st_proj_load): no trig has played
 * in it yet, so no track has a playing trig's locks; stock drops its own
 * held locks at CREATE NEW too (it keeps them through STOP and a pattern or
 * kit change, until the track's next trig, and so does core) */
void core_rows_now_reset(void)
{
    unsigned short sr = irq_mask();
    int t, i;
    for (t = 0; t < 13; t++) {
        for (i = 0; i < CODES; i++)
            core_lock_now[t][i] = 0;
        now_n[t] = 0;
    }
    irq_restore(sr);
}

/* (0.10) by the code, for smooth.c */
int core_lock_value_code(int track, int code)
{
    if ((unsigned)track >= 13 || code < FIRST_CODE || code > LAST_CODE)
        return -1;
    return core_lock_now[track][code - FIRST_CODE] - 1;
}

int core_lock_value(int track, unsigned field)
{
    return core_lock_value_code(track, core_lock_code(field));
}

/* ---- editing: a mod parameter's knob with a trig held ---- */

/* the step record a lock call works on, found as stock's (0x400a9926 ->
 * 0x400c058c -> 0x400ac6dc): the current pattern's track record, the step
 * within the track's length */
static unsigned where(int step)
{
    unsigned app = APP(), rec;
    rec = TRACK_REC(CURRENT_PATTERN(app), CURRENT_TRACK(APP_TRACKS(app)));
    if ((unsigned)step >= 64 || TRACK_LENGTH(rec) < step)
        return 0;
    return step_addr(rec, step);
}

static int get(unsigned addr, int code)
{
    struct ent at[CODES];
    int i, k = step_locks(addr, at);
    for (i = 0; i < k; i++)
        if (at[i].code == code)
            return at[i].value;
    return -1;
}

/* does the lock store hold a lock of code on addr's track? */
static int has_row(unsigned addr, int code)
{
    struct ledger *l;
    unsigned track;
    int k = live_of(addr), i;
    if (k < 0 || !(l = led[k]))
        return 0;
    track = addr - (addr - store_of(k)) % TRACK_LOCKS;
    for (i = lower(l, track); i < l->n && l->e[i].addr < track + STEPS_LOCKS; i++)
        if (l->e[i].code == code)
            return 1;
    return 0;
}

/* v: 0..16383, or -1 to clear; 0 when there was no room */
static int put(unsigned addr, int code, int v)
{
    struct ent at[CODES + 1 + 8];
    int k = step_ents(addr, at, CODES + 8, 1), i, j, live = live_of(addr);
    for (i = j = 0; i < k; i++)         /* (0.11) a sparse tag there stays */
        if (at[i].code != code)
            at[j++] = at[i];
    if (v >= 0) {
        /* a new row in a live pattern: stock's 71 must leave room */
        if (live >= 0 && !has_row(addr, code) && used(stored_rows(live)) + 1 + wide(code) > MAX_USED)
            return 0;
        for (i = j; i > 0 && at[i - 1].code > code; i--)
            at[i] = at[i - 1];
        at[i].addr = addr;
        at[i].code = code;
        at[i].value = v;
        at[i].pad = 0;
        j++;
    }
    if (!replace(addr, addr + 1, at, j))
        return 0;
    changed(addr, addr + 1);
    return 1;
}

/* the knob's mod parameter, or 0 for stock's; *s its item (the sound, or
 * the FX set's kit) */
static const struct core_lock_param *ours(void *set, int id, unsigned *code, unsigned char **s)
{
    return core_lock_set_param(set, id, code, s);
}

int core_scale_show(const struct core_lock_param *p, int v);       /* scale.c (0.11) */
int core_scale_unshow(const struct core_lock_param *p, int dial);

static int clamp(const struct core_lock_param *p, int v)
{
    int lo = core_lock_min(p), hi = core_lock_max(p);
    return v < lo ? lo : v > hi ? hi : v;
}

/* 0.11: a mod's own locks (core_param_locks, pages.h). A knob whose id is
 * in an entry's range, from OWN_FIRST up (above the pseudo ids, lock.c): with a trig held, the
 * set's four lock calls below go to the entry's functions, with the track
 * (the current one, 12 on the FX track, where() finds the step there too)
 * and the step, and the mod keeps the lock where it likes (the Sidecar
 * mods: on the Pi). Only for an edited kit's set, as core's own locks; a
 * step past the track's length is no step (-1, 0). Stock holds the trig
 * and marks the hold used before it calls them, so the trig stays when it
 * is let go, as with a stock lock. */
struct core_param_lock {                /* pages.h */
    int32_t first, last;
    int (*get)(int32_t id, int32_t track, int32_t step);
    int (*turn)(int32_t id, int32_t track, int32_t step, int32_t delta);
    int (*set)(int32_t id, int32_t track, int32_t step, int32_t value);
    int (*clear)(int32_t id, int32_t track, int32_t step);
    const char *(*label)(int32_t id);
    const char *(*name)(int32_t id);
};
extern unsigned char *core_lock_item(void *set, int *fx);
extern void core_project_edited(void);
extern const struct core_param_lock *const core_param_locks[];   /* zero-terminated */
#define OWN_FIRST 555                   /* pages.h CORE_PARAM_LOCK_FIRST */
#define RECORDING 0x100                 /* pages.h CORE_LOCK_RECORDING */
#define DTCN0 (*(volatile unsigned *)0xfc07000c)
#define HELD_TICKS 528000               /* 4 ms of DMA timer 0 (88,010 a 1/1500 s block) */

/* the entry that answers for id, or 0 */
static const struct core_param_lock *entry_of(int id)
{
    const struct core_param_lock *const *e;
    if (id < OWN_FIRST)
        return 0;
    for (e = core_param_locks; *e; e++)
        if (id >= (*e)->first && id <= (*e)->last)
            return *e;
    return 0;
}

/* the held trig's step and track, from the last lock call: stock calls get
 * for each knob as it draws it with a trig held, so a value text drawn
 * just after it (pages.h core_held_step) finds the step there */
static int held_step = -1, held_track;
static unsigned held_at;

static void held(int track, int step)
{
    held_track = track;
    held_step = step;
    held_at = DTCN0;
}

int core_held_step(int32_t *track)
{
    if (held_step < 0 || DTCN0 - held_at > HELD_TICKS)
        return -1;
    if (track)
        *track = held_track;
    return held_step;
}

/* lock.c: whether an entry answers for id; a knob's label and name from
 * it, 0 for stock's */
int core_param_lock_owns(int id)
{
    return entry_of(id) != 0;
}

unsigned core_param_lock_label(int id)
{
    const struct core_param_lock *m = entry_of(id);
    return m && m->label ? (unsigned)m->label(id) : 0;
}

unsigned core_param_lock_name(int id)
{
    const struct core_param_lock *m = entry_of(id);
    return m && m->name ? (unsigned)m->name(id) : 0;
}

/* pages.s, after a core_params entry took a turn (id, delta 8.8) with no
 * trig held: while live recording runs, the entry's turn gets the step the
 * current track plays, with RECORDING set in the step argument, as core's
 * own locks are recorded (core_lock_record) */
void core_lock_record_own(int id, int delta)
{
    const struct core_param_lock *m = entry_of(id);
    unsigned rec;
    int track, step;
    if (!m || !m->turn)
        return;
    track = CURRENT_TRACK(APP_TRACKS(APP()));
    if ((step = record_step(track, &rec)) < 0 || step >= 64 || TRACK_LENGTH(rec) < step)
        return;
    m->turn(id, track, step | RECORDING, delta);
}

static const struct core_param_lock *theirs(void *set, int id, int step, int *track)
{
    const struct core_param_lock *m = entry_of(id);
    unsigned app, rec;
    int fx;
    if (!m || !core_lock_item(set, &fx))
        return 0;
    app = APP();
    *track = CURRENT_TRACK(APP_TRACKS(app));
    rec = TRACK_REC(CURRENT_PATTERN(app), *track);
    if ((unsigned)step >= 64 || TRACK_LENGTH(rec) < step)
        *track = -1;
    else
        held(*track, step);
    return m;
}

/* sound set vtable +60: (set, id, step) -> the lock (8.8) or -1; the FX
 * set's the same (0.9), with stock's FX call for a knob not core's */
static int lock_get(void *set, int id, int step, int (*stock)(void *, int, int))
{
    unsigned code, a;
    unsigned char *s;
    const struct core_param_lock *m;
    const struct core_lock_param *p;
    int t;
    if (!(p = ours(set, id, &code, &s))) {
        if (!(m = theirs(set, id, step, &t)))
            return stock(set, id, step);
        return t < 0 || !m->get ? -1 : m->get(id, t, step);
    }
    if (!(a = where(step)))
        return -1;
    step = get(a, code);
    return step < 0 ? -1 : core_scale_show(p, step);   /* (0.11) on the dial's scale */
}

int core_lock_get_vt(void *set, int id, int step) { return lock_get(set, id, step, STOCK_LOCK_GET); }
int core_fx_lock_get_vt(void *set, int id, int step) { return lock_get(set, id, step, FX_LOCK_GET); }

/* +64: (set, id, value 8.8, step) */
static int lock_set(void *set, int id, int value, int step, int (*stock)(void *, int, int, int))
{
    const struct core_lock_param *p;
    const struct core_param_lock *m;
    unsigned code, a;
    unsigned char *s;
    int t;
    if (!(p = ours(set, id, &code, &s))) {
        if (!(m = theirs(set, id, step, &t)))
            return stock(set, id, value, step);
        return t < 0 || !m->set ? 0 : m->set(id, t, step, value);
    }
    if (!(a = where(step)))
        return 0;
    return put(a, code, clamp(p, core_scale_unshow(p, value)));   /* (0.11) from the dial's scale */
}

int core_lock_set_vt(void *set, int id, int value, int step) { return lock_set(set, id, value, step, STOCK_LOCK_SET); }
int core_fx_lock_set_vt(void *set, int id, int value, int step) { return lock_set(set, id, value, step, FX_LOCK_SET); }

/* +68: (set, id, step) */
static int lock_clear(void *set, int id, int step, int (*stock)(void *, int, int))
{
    unsigned code, a;
    unsigned char *s;
    const struct core_param_lock *m;
    int t;
    if (!ours(set, id, &code, &s)) {
        if (!(m = theirs(set, id, step, &t)))
            return stock(set, id, step);
        return t < 0 || !m->clear ? 0 : m->clear(id, t, step);
    }
    if (!(a = where(step)))
        return 0;
    return put(a, code, -1);
}

int core_lock_clear_vt(void *set, int id, int step) { return lock_clear(set, id, step, STOCK_LOCK_CLEAR); }
int core_fx_lock_clear_vt(void *set, int id, int step) { return lock_clear(set, id, step, FX_LOCK_CLEAR); }

/* +72: (set, id, delta 8.8, step) -> the new lock (8.8): a whole step at
 * least, from the lock or, on the first turn, the item's value. Stock's
 * sound and FX sets share the routine, so one site each takes this. */
int core_lock_turn_vt(void *set, int id, int delta, int step)
{
    const struct core_lock_param *p;
    const struct core_param_lock *m;
    unsigned code, a;
    unsigned char *s;
    int v, t;
    if (!(p = ours(set, id, &code, &s))) {
        if (!(m = theirs(set, id, step, &t)))
            return STOCK_LOCK_TURN(set, id, delta, step);
        if (t < 0 || !m->turn)
            return -1;
        if ((v = m->turn(id, t, step, delta)) >= 0)
            core_project_edited();
        return v;
    }
    if (!(a = where(step)))
        return -1;
    if ((v = get(a, code)) < 0)
        v = core_store_get(core_lock_field(p), s, 0);
    v = clamp(p, v + (delta > 0 ? (delta + 255) >> 8 : delta < 0 ? -((255 - delta) >> 8) : 0));
    put(a, code, v);
    return core_scale_show(p, v);
}

/* (0.11) steps.c: the live pattern object the UI edits (its track 1's step
 * 1 found as a lock call finds it), or -1 */
int core_rows_current(void)
{
    unsigned rec = TRACK_REC(CURRENT_PATTERN(APP()), 0), a = rec ? step_addr(rec, 0) : 0;
    return a ? live_of(a) : -1;
}
