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
    unsigned char code;         /* 42..127, DEAD once gone */
    unsigned char pad;
    unsigned short value;       /* 0..16383 */
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

static struct ledger *volatile led;
static volatile unsigned gen;   /* every change, to the array or in place */

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

static int any_in(const struct ledger *l, unsigned a, unsigned b)
{
    int i;
    for (i = lower(l, a); i < l->n && l->e[i].addr < b; i++)
        if (l->e[i].code != DEAD)
            return 1;
    return 0;
}

/* the locks in [a, b) go, in place (no memory taken: also from operator new
 * and from interrupts) */
static void drop(unsigned a, unsigned b)
{
    struct ledger *l = led;
    int i;
    if (!l)
        return;
    for (i = lower(l, a); i < l->n && l->e[i].addr < b; i++)
        if (l->e[i].code != DEAD) {
            l->e[i].code = DEAD;
            gen++;
        }
}

/* the ledger with [a, b) holding only add[0..k) (sorted, each in [a, b));
 * 0 when it could not be made */
static int replace(unsigned a, unsigned b, const struct ent *add, int k)
{
    struct ledger *l, *n;
    unsigned g;
    unsigned short sr;
    int i, j, live;
    for (;;) {
        l = led;
        g = gen;
        live = k;
        for (i = 0; l && i < l->n; i++)
            if (l->e[i].code != DEAD && (l->e[i].addr < a || l->e[i].addr >= b))
                live++;
        if (live > MOST)
            return 0;
        n = live ? (struct ledger *)NEW(4 + sizeof(struct ent) * live) : 0;
        if (live && !n)
            return 0;
        j = 0;
        for (i = 0; l && i < l->n && l->e[i].addr < a; i++)
            if (l->e[i].code != DEAD)
                n->e[j++] = l->e[i];
        copy_bytes(&n->e[j], add, sizeof(struct ent) * k);
        j += k;
        for (; l && i < l->n; i++)
            if (l->e[i].code != DEAD && l->e[i].addr >= b)
                n->e[j++] = l->e[i];
        if (n)
            n->n = j;
        sr = irq_mask();
        if (led == l && gen == g) {
            led = n;
            gen++;
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

/* a step record's mod locks, into out[] by code; their count */
static int step_locks(unsigned addr, struct ent *out)
{
    struct ledger *l = led;
    int i, k = 0;
    for (i = l ? lower(l, addr) : 0; l && i < l->n && l->e[i].addr == addr && k < CODES; i++)
        if (l->e[i].code != DEAD)
            out[k++] = l->e[i];
    return k;
}

/* ---- live patterns and their stored rows ---- */

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
    struct ledger *l = led;
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
        if (l->e[i].code != DEAD && (o = l->e[i].addr - store) % TRACK_LOCKS < STEPS_LOCKS)
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
        if (l->e[i].code != DEAD && (o = l->e[i].addr - store) % TRACK_LOCKS < STEPS_LOCKS) {
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

/* memcpy (dst, src, n), after the copy (rows.s): n >= 88 */
void core_rows_memcpy(unsigned dst, unsigned src, unsigned n)
{
    struct ledger *l = led;
    struct ent *add;
    int i, k, first;
    if (!l || !l->n || dst == src || (!any_in(l, dst, dst + n) && !any_in(l, src, src + n)))
        return;
    first = lower(l, src);
    for (k = 0, i = first; i < l->n && l->e[i].addr < src + n; i++)
        if (l->e[i].code != DEAD && l->e[i].addr + STEP_LOCKS <= src + n)
            k++;
    if (!k) {
        drop(dst, dst + n);
    } else if ((add = (struct ent *)NEW(sizeof(struct ent) * k)) != 0) {
        for (k = 0, i = first; i < l->n && l->e[i].addr < src + n; i++)
            if (l->e[i].code != DEAD && l->e[i].addr + STEP_LOCKS <= src + n) {
                add[k] = l->e[i];
                add[k++].addr += dst - src;
            }
        if (!replace(dst, dst + n, add, k))
            drop(dst, dst + n);
        DELETE(add);
    } else {
        drop(dst, dst + n);
    }
    changed(dst, dst + n);
}

/* memset (dst, c, n) and operator new's block (rows.s), track clear
 * (data, locks: rows.s): the locks there go */
void core_rows_drop(unsigned a, unsigned n)
{
    struct ledger *l = led;
    if (!l || !l->n || !any_in(l, a, a + n))
        return;
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

/* 0x400c4d62, 0x400c3eb0 (rows.s): the step's mod locks join its list */
void core_rows_list(unsigned rec, int step, void *list, struct listed *e)
{
    struct ent at[CODES];
    unsigned a = step_addr(rec, step);
    int i, k = a ? step_locks(a, at) : 0;
    for (i = 0; i < k; i++) {
        e->value = at[i].value << 8;
        e->code = at[i].code;
        e->fine = 0;
        LIST_PUSH(list, e);
    }
}

static int put(unsigned addr, int code, int v);

/* 0x400c058c (rows.s): (track record, step, code, value 8.8, fine) for a
 * code above 41; 1 when it was set */
static int clamp(const struct core_lock_param *p, int v);

int core_rows_paste(unsigned rec, int step, int code, int value)
{
    const struct core_lock_param *p;
    unsigned a;
    if (code < FIRST_CODE || code > LAST_CODE || TRACK_LENGTH(rec) < step || !(a = step_addr(rec, step)))
        return 0;
    value >>= 8;
    p = core_lock_by_code(code);
    return put(a, code, p ? clamp(p, value) : value < 0 ? 0 : value > 127 ? 127 : value);
}

/* ---- live recording ---- */

/* the value (0..16383) a mod parameter's knob or MIDI gave the track's
 * sound: while live recording runs, it is locked on the step the track
 * plays, found as stock's live record finds it (0x400af762), which adds a
 * lock trig to a step without a trig */
void core_lock_record(int track, int code, int value)
{
    unsigned app, rec;
    int step;
    if ((unsigned)track >= 13 || !LIVE_RECORDING(REC_STATE()))
        return;
    step = PLAY_STEP(SEQUENCER(), track);
    app = APP();
    rec = TRACK_REC(CURRENT_PATTERN(app), track);
    if (SCALED(rec))
        step = SCALED_STEP(rec, step);
    if (QUANTIZED(rec) == 1) {
        if ((step = QUANTIZED_STEP(rec, step)) < 0)
            return;
    } else if (!HAS_TRIG(rec, step) && !HAS_LOCK_TRIG(rec, step))
        ADD_LOCK_TRIG(rec, step, 1);
    core_rows_paste(rec, step, code, value << 8);
}

/* ---- playback: the trig's locks (0x4009a954: track, out, the step's
 * locks, ...) ---- */
void core_rows_trig(int track, unsigned step)
{
    struct ledger *l = led;
    int i, c, n;
    if ((unsigned)track >= 13)
        return;
    for (i = 0; i < now_n[track]; i++)
        core_lock_now[track][now_set[track][i]] = 0;
    n = 0;
    for (i = l ? lower(l, step) : 0; l && i < l->n && l->e[i].addr == step; i++)
        if (l->e[i].code != DEAD && n < CODES) {
            c = l->e[i].code - FIRST_CODE;
            core_lock_now[track][c] = l->e[i].value + 1;
            now_set[track][n++] = c;
        }
    now_n[track] = n;
}

int core_lock_value(int track, unsigned field)
{
    int code = core_lock_code(field), v;
    if ((unsigned)track >= 13 || code < FIRST_CODE)
        return -1;
    v = core_lock_now[track][code - FIRST_CODE];
    return v - 1;
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
    struct ledger *l = led;
    unsigned track;
    int k = live_of(addr), i;
    if (!l || k < 0)
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
    struct ent at[CODES + 1];
    int k = step_locks(addr, at), i, j, live = live_of(addr);
    for (i = j = 0; i < k; i++)
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

static int clamp(const struct core_lock_param *p, int v)
{
    int lo = core_lock_min(p), hi = core_lock_max(p);
    return v < lo ? lo : v > hi ? hi : v;
}

/* sound set vtable +60: (set, id, step) -> the lock (8.8) or -1; the FX
 * set's the same (0.9), with stock's FX call for a knob not core's */
static int lock_get(void *set, int id, int step, int (*stock)(void *, int, int))
{
    unsigned code, a;
    unsigned char *s;
    if (!ours(set, id, &code, &s))
        return stock(set, id, step);
    if (!(a = where(step)))
        return -1;
    step = get(a, code);
    return step < 0 ? -1 : step << 8;
}

int core_lock_get_vt(void *set, int id, int step) { return lock_get(set, id, step, STOCK_LOCK_GET); }
int core_fx_lock_get_vt(void *set, int id, int step) { return lock_get(set, id, step, FX_LOCK_GET); }

/* +64: (set, id, value 8.8, step) */
static int lock_set(void *set, int id, int value, int step, int (*stock)(void *, int, int, int))
{
    const struct core_lock_param *p;
    unsigned code, a;
    unsigned char *s;
    if (!(p = ours(set, id, &code, &s)))
        return stock(set, id, value, step);
    if (!(a = where(step)))
        return 0;
    return put(a, code, clamp(p, value >> 8));
}

int core_lock_set_vt(void *set, int id, int value, int step) { return lock_set(set, id, value, step, STOCK_LOCK_SET); }
int core_fx_lock_set_vt(void *set, int id, int value, int step) { return lock_set(set, id, value, step, FX_LOCK_SET); }

/* +68: (set, id, step) */
static int lock_clear(void *set, int id, int step, int (*stock)(void *, int, int))
{
    unsigned code, a;
    unsigned char *s;
    if (!ours(set, id, &code, &s))
        return stock(set, id, step);
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
    unsigned code, a;
    unsigned char *s;
    int v;
    if (!(p = ours(set, id, &code, &s)))
        return STOCK_LOCK_TURN(set, id, delta, step);
    if (!(a = where(step)))
        return -1;
    if ((v = get(a, code)) < 0)
        v = core_store_get(core_lock_field(p), s, 0);
    v = clamp(p, v + (delta > 0 ? (delta + 255) >> 8 : delta < 0 ? -((255 - delta) >> 8) : 0));
    put(a, code, v);
    return v << 8;
}
