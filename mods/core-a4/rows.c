/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-a4 0.5: mod p-locks as lock rows (README, "Mod p-locks"), core-ar2
 * 0.8's rows.c with the Analog Four MKII's pattern, and the playing locks per
 * voice with Param Slide's deltas. Every address is OS 1.56's.
 *
 * A pattern keeps its p-locks in 128 rows of 66 bytes, [column][track][64
 * step bytes] (a step byte with bit 7 set has no lock; a fine column's second
 * row has the header {0x80, 0x80}); stock's columns are 0..0x6f, a mod
 * parameter's is its lock code (0x70..0x7f, of its track's kind, lock.c).
 * The rows are only the stored form: stock edits and plays the live lock
 * store, a 0xe4-byte step record (0x70 lock words and a count) at pattern
 * +0x1626 + track x 0x3970 + step x 0xe4 in each of the 129 unpacked
 * patterns (the working copy, then slots 0..127, 0x16fda apart), and packs
 * the rows from it, which drops every row it does not know. A step record
 * has no room for more, so core keeps the mod locks itself, in a ledger:
 * {the step record's address, code, value}, sorted by address. The ledger
 * follows stock's moves of step records by address:
 *
 *   - memcpy (0x40077fa0, and its thunk 0x4009099e) of 0xe4 bytes or more:
 *     the locks of the step records copied go with them, and those the copy
 *     overwrote go: whole patterns, tracks, pages, the clipboards and undo;
 *   - memset (0x40077fe8), bzero (0x400778e0), operator new (0x400653b8,
 *     0x400653be), the lock store's init (0x40015d10), a track's
 *     (0x40015c2e) and a step's clear (0x400a35ac): the locks there go;
 *   - the row unpack (0x40009dbc): the rows with codes 0x70..0x7f (stock
 *     skips them) become the locks of the lock store unpacked;
 *   - the full packer (0x4000a02c): after stock's rows, the store's mod locks
 *     as rows, in code order then track order, each where one still fits
 *     under stock's 127 (row 127 stays free, as stock keeps it), so stock's
 *     locks always win; the per-column update (0x4000a1e6), which appends at
 *     the count of used rows, runs with the mod rows taken out and gets them
 *     back after, so a mod row never makes stock refuse a lock;
 *   - trig copy (0x400b8e66) lists a step's columns 0..0x6f as {value,
 *     column, fine} for a later paste: core adds the step's mod locks to the
 *     list, and the paste's lock set (0x400b51f6), given a column of 0x70 or
 *     more, sets a mod lock;
 *   - the three parameter sets' lock calls (vtable +0x48 get, +0x4c set,
 *     +0x50 clear, +0x54 turn) take a mod parameter's knob (its page id,
 *     lock.c) with a trig held, on the current track; the page's value
 *     (page_get_value with a step, pages.s) shows the step's lock;
 *   - live recording: a mod parameter's knob turned (pages.s) locks the
 *     value on the step the track plays, as stock's live record
 *     (0x400a690c) locks its own.
 *
 * After a change to a live pattern's locks, core writes its mod rows into
 * the stored pattern, as stock's row job would.
 *
 * A parameter above 127 (lock.c, core_lock_shift) is locked whole: its lock
 * takes two rows per track, the value's bits 13..7 in the first and its bits
 * 6..0 in the row straight after, whose header is {0x80, 0x81}. Never stock's
 * {0x80, 0x80}: stock's unpack ORs such a row into the last stock fine
 * column before it, across any rows it skips.
 *
 * Codes are a build's (its lockfile's), so mod rows leave the unit in a
 * pattern's SysEx dump only with a marker row, {0x7f, 0xfe} then the sender's
 * layout ID: core adds it to the dump's buffer (0x4006b492, after the pack of
 * the working pattern or the copy of a slot), or, with no free row for it,
 * leaves the mod rows out. A received pattern (0x40011d44, which unpacks it
 * into its slot at once) keeps its mod rows only with a marker holding this
 * build's own layout ID; the marker never stays. A project of a layout this
 * build does not read (st_proj_load) loses its mod rows.
 *
 * Playing locks. Stock's trig fire (0x4007a1d2) builds a lock list per track
 * and event (the immediate one, three scheduled; 0x40079fbe), and the audio
 * interrupt copies an event's list to the voice it gives the trig (event
 * accept, 0x40101d22). Core lists the step's mod locks at the same call, in
 * core's own event slots, and copies them at the same accept, into
 * core_lock_now[voice][code] and core_slide_delta[voice][code]. A voice is
 * 0..5: the four synth voices, the FX track's, the CV track's. The note
 * path's list (0x40066f2a, accepted at 0x40101fb2) is a fifth slot. Stopped,
 * the sequencer fills its first step's lists at STOP and again on each of
 * stock's own lock edits; a mod lock's edit does not, so an accept whose
 * slot was filled before the ledger last changed fills it again first.
 */

#include <stdint.h>

#define NEW ((unsigned (*)(unsigned))0x4006513c)          /* operator new's body; d0 */
#define DELETE ((void (*)(void *))0x400653c4)
#define APP ((unsigned (*)(void))0x401494de)
#define APP_TRACKS ((unsigned (*)(unsigned))0x400a51be)
#define CURRENT_TRACK ((int (*)(unsigned))0x400adbd2)
#define APP_PATTERN ((unsigned (*)(unsigned))0x400a5f4a)
#define TRACK_WRAPPER ((unsigned (*)(unsigned, int))0x400a1f3c)   /* (pattern, track) */
#define LIST_PUSH ((void (*)(void *, void *))0x4016f120)          /* (vector, &{value, column, fine}) */
#define STOCK_LOCK_SET ((int (*)(unsigned, int, int, int, int))0x400b51f6)
#define REC_OWNER ((unsigned (*)(void))0x4014b1f6)
#define RECORDING ((unsigned char (*)(unsigned))0x40021dc2)
#define SEQUENCER ((unsigned (*)(void))0x4014b292)
#define PLAY_STEP ((int (*)(unsigned, int))0x40020d18)            /* (sequencer, track) */
#define SCALED ((unsigned char (*)(unsigned))0x400b781a)          /* the track's own scale */
#define SCALED_STEP ((int (*)(unsigned, int))0x400b784e)
#define QUANTIZED ((int (*)(unsigned))0x400b5e76)
#define QUANTIZED_STEP ((int (*)(unsigned, int))0x400b6302)
#define HAS_TRIG ((unsigned char (*)(unsigned, int))0x400b43d0)
#define HAS_LOCK_TRIG ((unsigned char (*)(unsigned, int))0x400b43e6)
#define ADD_LOCK_TRIG ((void (*)(unsigned, int, int))0x400b74ee)

#define SOUND_VT 0x401a164cu                    /* the parameter sets: synth, FX, CV */
#define FX_VT 0x401a16ecu
#define CV_VT 0x401a178cu
#define STOCK_LOCK_GET ((int (*)(void *, int, int))0x400a0190)   /* the three are alike */
#define FX_LOCK_GET ((int (*)(void *, int, int))0x400a044a)
#define CV_LOCK_GET ((int (*)(void *, int, int))0x400a0704)
#define SOUND_LOCK_SET ((int (*)(void *, int, int, int))0x400a0218)
#define FX_LOCK_SET ((int (*)(void *, int, int, int))0x400a04d2)
#define CV_LOCK_SET ((int (*)(void *, int, int, int))0x400a078c)
#define SOUND_LOCK_CLEAR ((int (*)(void *, int, int))0x400a02a4)
#define FX_LOCK_CLEAR ((int (*)(void *, int, int))0x400a055e)
#define CV_LOCK_CLEAR ((int (*)(void *, int, int))0x400a0818)
#define LOCK_TURN ((int (*)(void *, int, int, int))0x4009e74c)   /* +0x54, all three */

#define LIVE_PATTERNS 0x40653c5cu               /* the working copy, then slots 0..127 */
#define PATTERN_SIZE 0x16fdau
#define LIVE_LOCKS 0x1626
#define TRACK_LOCKS 0x3970
#define STEP_LOCKS 0xe4
#define STEPS_LOCKS (64 * STEP_LOCKS)           /* a track's step records */
#define TRACKS 6
#define STORE_LOCKS (TRACKS * TRACK_LOCKS)
#define LIVE_TRACK 0x3b1                        /* a pattern's track records, from +0 */
#define STORED_PATTERNS 0x4045c98cu             /* the stored project's working pattern, then 128 */
#define STORED_PATTERN 0x32ae
#define STORED_ROWS 0x119e
#define ROWS 128
#define ROW 66
#define MAX_USED 127
#define FIRST_CODE 0x70
#define LAST_CODE 0x7f
#define CODES 16
#define DEAD 0xff
#define LOW0 0x80                               /* a low row's header */
#define LOW1 0x81
#define MOST 16384                              /* the ledger's entries at most */

#define S ((unsigned char *)0x4045c974)         /* the stored project */
#define WORKING_KIT_INDEX (S + 0x199330)
#define LIVE_KITS 0x411e9936u                   /* the kits as edited */
#define LIVE_KIT_SIZE 0x960
#define KIT_SOUNDS 0x1c
#define SOUND_SIZE 0x152
#define PLAY_KIT ((volatile int *)0x42878860)   /* a pattern with no kit of its own plays it */

struct ent {
    unsigned addr;              /* the step record */
    unsigned char code;         /* 0x70..0x7f, DEAD once gone */
    unsigned char pad;
    unsigned short value;       /* 0..16383 */
};

struct ledger {
    int n;
    struct ent e[];
};

struct core_lock_param;
const struct core_lock_param *core_lock_by_code(int kind, unsigned code);
const struct core_lock_param *core_lock_by_id_in(int id, unsigned *code, int kind);
int core_lock_kind_of_track(int track);
int core_lock_kind(unsigned field);
int core_lock_code(unsigned field);
unsigned core_lock_field(const struct core_lock_param *p);
int core_lock_shift(const struct core_lock_param *p);
int core_lock_clamp(const struct core_lock_param *p, int v);
unsigned core_store_get(unsigned field, void *item, int index);
extern const unsigned core_store_ids[];         /* this build's layout ID first */

static struct ledger *volatile led;
static volatile unsigned gen;   /* every change, to the array or in place */

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

static unsigned rd32(const volatile unsigned char *p)
{
    return (unsigned)p[0] << 24 | (unsigned)p[1] << 16 | (unsigned)p[2] << 8 | p[3];
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

/* the locks in [a, b) go, in place (no memory taken: also from operator new) */
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

/* a step record's lock of code, or -1 */
static int get(unsigned addr, int code)
{
    struct ent at[CODES];
    int i, k = step_locks(addr, at);
    for (i = 0; i < k; i++)
        if (at[i].code == code)
            return at[i].value;
    return -1;
}

/* ---- live patterns and their stored rows ---- */

/* the live pattern whose lock store holds addr, or -1 */
static int live_of(unsigned addr)
{
    unsigned o = addr - LIVE_PATTERNS - LIVE_LOCKS;
    if (addr < LIVE_PATTERNS + LIVE_LOCKS || o / PATTERN_SIZE >= 129 || o % PATTERN_SIZE >= STORE_LOCKS)
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

static int mod_code(unsigned char c)
{
    return c >= FIRST_CODE && c <= LAST_CODE;
}

/* stock counts a row as used when neither header byte is 0xff */
static int empty(const unsigned char *r)
{
    return r[0] == 0xff || r[1] == 0xff;
}

static int mod_row(const unsigned char *r)
{
    return mod_code(r[0]) && r[1] < TRACKS;
}

static int low_row(const unsigned char *r)
{
    return r[0] == LOW0 && r[1] == LOW1;
}

/* does a code's lock on a track of the kind take two rows? */
static int wide(int kind, int code)
{
    const struct core_lock_param *p = core_lock_by_code(kind, code);
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
 * then track order, where they fit under stock's 127 (a wide lock's two
 * rows, together, or neither). Codes no parameter of the track's kind has
 * keep one row (their value's low 7 bits are not known to be needed). */
static void append(unsigned char *rows, unsigned store)
{
    struct ledger *l = led;
    unsigned char at[CODES][TRACKS], *r;
    unsigned o;
    int u, code, t, s, i, w, k, first, last;
    if (!l)
        return;
    first = lower(l, store);
    last = lower(l, store + STORE_LOCKS);
    for (code = 0; code < CODES; code++)
        for (t = 0; t < TRACKS; t++)
            at[code][t] = 0xff;
    for (i = first; i < last; i++)
        if (l->e[i].code != DEAD && (o = l->e[i].addr - store) % TRACK_LOCKS < STEPS_LOCKS &&
            (o % TRACK_LOCKS) % STEP_LOCKS == 0)
            at[l->e[i].code - FIRST_CODE][o / TRACK_LOCKS] = 0xfe;
    u = used(rows);
    for (k = ROWS; k > 0 && empty(rows + ROW * (k - 1)); k--)
        ;                                       /* after the last used row */
    for (code = 0; code < CODES; code++)
        for (t = 0; t < TRACKS; t++) {
            if (at[code][t] == 0xff)
                continue;
            at[code][t] = 0xff;
            w = wide(core_lock_kind_of_track(t), FIRST_CODE + code);
            if (u + 1 + w > MAX_USED || k + w >= MAX_USED)
                continue;
            r = rows + ROW * k;
            r[0] = FIRST_CODE + code;
            r[1] = t;
            for (s = 0; s < 64; s++)
                r[2 + s] = 0xff;
            if (w) {
                r[ROW] = LOW0;
                r[ROW + 1] = LOW1;
                for (s = 0; s < 64; s++)
                    r[ROW + 2 + s] = 0xff;
            }
            at[code][t] = k;
            k += 1 + w;
            u += 1 + w;
        }
    for (i = first; i < last; i++)
        if (l->e[i].code != DEAD && (o = l->e[i].addr - store) % TRACK_LOCKS < STEPS_LOCKS &&
            (o % TRACK_LOCKS) % STEP_LOCKS == 0) {
            code = l->e[i].code - FIRST_CODE;
            t = o / TRACK_LOCKS;
            s = o % TRACK_LOCKS / STEP_LOCKS;
            if ((k = at[code][t]) >= ROWS)
                continue;
            if (wide(core_lock_kind_of_track(t), FIRST_CODE + code)) {
                rows[ROW * k + 2 + s] = l->e[i].value >> 7 & 127;
                rows[ROW * (k + 1) + 2 + s] = l->e[i].value & 127;
            } else {
                rows[ROW * k + 2 + s] = l->e[i].value > 127 ? 127 : l->e[i].value;
            }
        }
}

/* the mod rows (and their low rows) go */
static void unappend(unsigned char *rows)
{
    int i, j;
    for (i = 0; i < ROWS; i++)
        if (mod_row(rows + ROW * i) || low_row(rows + ROW * i))
            for (j = 0; j < ROW; j++)
                rows[ROW * i + j] = 0xff;
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
    if (b <= store_of(0) || a >= store_of(128) + STORE_LOCKS)
        return;
    for (k = 0; k < 129; k++)
        if (store_of(k) < b && store_of(k) + STORE_LOCKS > a)
            sync(k);
}

/* ---- the follow sites ---- */

/* memcpy (dst, src, n), after the copy (rows.s): n >= 0xe4 */
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

/* memset, bzero, operator new's block (rows.s), the lock store's init and a
 * track's: the locks there go */
void core_rows_drop(unsigned a, unsigned n)
{
    struct ledger *l = led;
    if (!l || !l->n || !any_in(l, a, a + n))
        return;
    drop(a, a + n);
    changed(a, a + n);
}

/* a pattern model's step records (its virtual +0x28) */
static unsigned model_steps(unsigned model)
{
    return model ? (*(unsigned (**)(unsigned))(*(unsigned *)model + 0x28))(model) : 0;
}

/* 0x400a35ac, a step's locks' clear (model, track, step), after stock */
void core_rows_step_clear(unsigned model, int track, int step)
{
    unsigned steps;
    if ((unsigned)track >= TRACKS || (unsigned)step > 63 || !(steps = model_steps(model)))
        return;
    core_rows_drop(steps + TRACK_LOCKS * track + STEP_LOCKS * step, STEP_LOCKS);
}

/* the row unpack (0x40009dbc: lock store, rows), after stock */
void core_rows_load(unsigned store, const unsigned char *rows)
{
    const unsigned char *r;
    struct ent *add;
    int i, j, k, t, s, x, c;
    if (!store || !rows)
        return;
    for (i = x = 0; i < ROWS; i++)
        if (mod_row(r = rows + ROW * i))
            for (s = 0; s < 64; s++)
                x += !(r[2 + s] & 0x80);
    if (!x) {
        drop(store, store + STORE_LOCKS);
        return;
    }
    if (!(add = (struct ent *)NEW(sizeof(struct ent) * x)))
        return;
    /* by address (track, step), then code */
    for (t = x = 0; t < TRACKS; t++)
        for (s = 0; s < 64; s++)
            for (c = FIRST_CODE; c <= LAST_CODE; c++)
                for (i = 0; i < ROWS; i++) {
                    r = rows + ROW * i;
                    if (r[0] != c || r[1] != t || (r[2 + s] & 0x80))
                        continue;
                    add[x].addr = store + TRACK_LOCKS * t + STEP_LOCKS * s;
                    add[x].code = c;
                    add[x].value = r[2 + s];
                    j = i + 1;
                    if (j < ROWS && low_row(rows + ROW * j) && wide(core_lock_kind_of_track(t), c))
                        add[x].value = r[2 + s] << 7 | (rows[ROW * j + 2 + s] & 127);
                    add[x++].pad = 0;
                    break;                      /* one lock a code, track and step */
                }
    k = x;
    if (!replace(store, store + STORE_LOCKS, add, k))
        drop(store, store + STORE_LOCKS);
    DELETE(add);
}

/* the full packer (0x4000a02c: rows, lock store), after stock */
void core_rows_save(unsigned char *rows, unsigned store)
{
    if (rows && store) {
        unappend(rows);
        append(rows, store);
    }
}

/* the per-column update (0x4000a1e6: rows, lock store, column, track), before
 * stock: the mod rows go, so stock counts and appends only its own */
void core_rows_save_before(unsigned char *rows)
{
    if (rows)
        unappend(rows);
}

/* ---- SysEx pattern dumps and foreign projects ---- */

#define MARK_CODE 0x7f
#define MARK_TRACK 0xfe

static int marker(const unsigned char *r)
{
    return r[0] == MARK_CODE && r[1] == MARK_TRACK;
}

/* 0x4006b492 (rows.s): a dump's buffer (a stored pattern's form) after
 * stock's pack or copy: the marker for its mod rows */
void core_dump_out(unsigned char *buf)
{
    unsigned char *rows, *m;
    int i, any = 0, k;
    if (!buf)
        return;
    rows = buf + STORED_ROWS;
    for (i = 0; i < ROWS; i++)
        any |= mod_row(rows + ROW * i);
    if (!any)
        return;
    for (k = ROWS; k > 0 && empty(rows + ROW * (k - 1)); k--)
        ;
    if (k >= MAX_USED || used(rows) + 1 > MAX_USED) {
        unappend(rows);
        return;
    }
    m = rows + ROW * k;
    for (i = 0; i < ROW; i++)
        m[i] = 0xff;
    m[0] = MARK_CODE;
    m[1] = MARK_TRACK;
    m[2] = core_store_ids[0] >> 24;
    m[3] = core_store_ids[0] >> 16;
    m[4] = core_store_ids[0] >> 8;
    m[5] = core_store_ids[0];
}

/* 0x40011d44 (rows.s): a received pattern's buffer, before stock unpacks it */
void core_dump_in(unsigned char *buf)
{
    unsigned char *rows, *r;
    unsigned id;
    int i, j, ok = 0;
    if (!buf)
        return;
    rows = buf + STORED_ROWS;
    for (i = 0; i < ROWS; i++)
        if (marker(r = rows + ROW * i)) {
            id = (unsigned)r[2] << 24 | (unsigned)r[3] << 16 | (unsigned)r[4] << 8 | r[5];
            ok = id && id == core_store_ids[0];
            for (j = 0; j < ROW; j++)
                r[j] = 0xff;
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

/* ---- editing ---- */

/* a pattern-track wrapper's step record: its model (+0x2c) and track (+0x48) */
static unsigned wrapper_step(unsigned w, int step)
{
    unsigned steps;
    int track;
    if (!w || (unsigned)step > 63)
        return 0;
    track = *(int *)(w + 0x48);
    if ((unsigned)track >= TRACKS || !(steps = model_steps(*(unsigned *)(w + 0x2c))))
        return 0;
    return steps + TRACK_LOCKS * track + STEP_LOCKS * step;
}

static int wrapper_track(unsigned w)
{
    return w ? *(int *)(w + 0x48) : -1;
}

/* the current track, and its wrapper in the current pattern, as stock's lock
 * calls find them (0x400a0190) */
static int current(unsigned *w)
{
    unsigned app = APP();
    int track = CURRENT_TRACK(APP_TRACKS(app));
    *w = (unsigned)track < TRACKS ? TRACK_WRAPPER(APP_PATTERN(app), track) : 0;
    return *w ? track : -1;
}

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

static int track_of_addr(unsigned addr)
{
    int k = live_of(addr);
    return k < 0 ? -1 : (int)((addr - store_of(k)) / TRACK_LOCKS);
}

/* v: 0..16383, or -1 to clear; 0 when there was no room */
static int put(unsigned addr, int code, int v)
{
    struct ent at[CODES + 1];
    int k = step_locks(addr, at), i, j, live = live_of(addr), t = track_of_addr(addr);
    for (i = j = 0; i < k; i++)
        if (at[i].code != code)
            at[j++] = at[i];
    if (v >= 0) {
        /* a new row in a live pattern: stock's 127 must leave room */
        if (live >= 0 && !has_row(addr, code) &&
            used(stored_rows(live)) + 1 + wide(core_lock_kind_of_track(t), code) > MAX_USED)
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

/* trig copy (0x400b905a, rows.s), after its loop over a step's 0x70
 * columns: the step's mod locks join its list, {value 8.8, column, fine} */
struct listed {
    int value;
    int column;
    int fine;
};

void core_rows_list(unsigned w, int step, void *list, struct listed *e)
{
    struct ent at[CODES];
    unsigned a = wrapper_step(w, step);
    int i, k = a ? step_locks(a, at) : 0;
    for (i = 0; i < k; i++) {
        e->value = at[i].value << 8;
        e->column = at[i].code;
        e->fine = 0;
        LIST_PUSH(list, e);
    }
}

/* 0x400b51f6 (rows.s), the paste's lock set (wrapper, step, column, value
 * 8.8, fine) for a column of 0x70 or more; 1 when it was set */
int core_rows_paste(unsigned w, int step, int code, int value)
{
    const struct core_lock_param *p;
    unsigned a = wrapper_step(w, step);
    int kind = core_lock_kind_of_track(wrapper_track(w));
    if (!a || code < FIRST_CODE || code > LAST_CODE || kind < 0)
        return 0;
    value >>= 8;
    p = core_lock_by_code(kind, code);
    return put(a, code, p ? core_lock_clamp(p, value) : value < 0 ? 0 : value > 16383 ? 16383 : value);
}

/* the value (0..16383) a mod parameter's knob gave the track's sound or kit:
 * while live recording runs, it is locked on the step the track plays, found
 * as stock's live record finds it (0x400a690c), which adds a lock trig to a
 * step with neither a trig nor a lock trig */
void core_lock_record(int track, int code, int value)
{
    unsigned w;
    int step;
    if ((unsigned)track >= TRACKS || !RECORDING(REC_OWNER()))
        return;
    step = PLAY_STEP(SEQUENCER(), track);
    if (!(w = TRACK_WRAPPER(APP_PATTERN(APP()), track)))
        return;
    if (SCALED(w))
        step = SCALED_STEP(w, step);
    if (QUANTIZED(w) == 1) {
        if ((step = QUANTIZED_STEP(w, step)) < 0)
            return;
    } else if (!HAS_TRIG(w, step) && !HAS_LOCK_TRIG(w, step))
        ADD_LOCK_TRIG(w, step, 1);
    core_rows_paste(w, step, code, value << 8);
}

/* the item a knob of the current track edits: the current kit's sound on a
 * synth track, the kit on FX and CV */
static unsigned char *edited_item(int track)
{
    unsigned k = rd32(WORKING_KIT_INDEX);
    if (k >= 128 || (unsigned)track >= TRACKS)
        return 0;
    return (unsigned char *)LIVE_KITS + LIVE_KIT_SIZE * k + (track < 4 ? KIT_SOUNDS + SOUND_SIZE * track : 0);
}

/* the set's kind: synth 0, FX 1, CV 2, else -1 */
static int set_kind(void *set)
{
    unsigned vt = set ? *(unsigned *)set : 0;
    return vt == SOUND_VT ? 0 : vt == FX_VT ? 1 : vt == CV_VT ? 2 : -1;
}

/* the knob's mod parameter for the set on the current track, its code and
 * the step record, else 0 (stock's knob) */
static const struct core_lock_param *ours(void *set, int id, unsigned *code, int *track)
{
    unsigned w;
    int kind = set_kind(set);
    const struct core_lock_param *p;
    if (kind < 0 || !(p = core_lock_by_id_in(id, code, kind)))
        return 0;
    if ((*track = current(&w)) < 0 || core_lock_kind_of_track(*track) != kind)
        return 0;
    return p;
}

static unsigned where(int step)
{
    unsigned w;
    current(&w);
    return wrapper_step(w, step);
}

/* +0x48: (set, id, step) -> the lock (8.8) or -1 */
static int lock_get(void *set, int id, int step, int (*stock)(void *, int, int))
{
    unsigned code, a;
    int track;
    if (!ours(set, id, &code, &track))
        return stock(set, id, step);
    if (!(a = where(step)))
        return -1;
    step = get(a, code);
    return step < 0 ? -1 : step << 8;
}

int core_lock_get_vt(void *set, int id, int step) { return lock_get(set, id, step, STOCK_LOCK_GET); }
int core_fx_lock_get_vt(void *set, int id, int step) { return lock_get(set, id, step, FX_LOCK_GET); }
int core_cv_lock_get_vt(void *set, int id, int step) { return lock_get(set, id, step, CV_LOCK_GET); }

/* +0x4c: (set, id, value 8.8, step) */
static int lock_set(void *set, int id, int value, int step, int (*stock)(void *, int, int, int))
{
    const struct core_lock_param *p;
    unsigned code, a;
    int track;
    if (!(p = ours(set, id, &code, &track)))
        return stock(set, id, value, step);
    if (!(a = where(step)))
        return 0;
    return put(a, code, core_lock_clamp(p, value >> 8));
}

int core_lock_set_vt(void *set, int id, int value, int step) { return lock_set(set, id, value, step, SOUND_LOCK_SET); }
int core_fx_lock_set_vt(void *set, int id, int value, int step) { return lock_set(set, id, value, step, FX_LOCK_SET); }
int core_cv_lock_set_vt(void *set, int id, int value, int step) { return lock_set(set, id, value, step, CV_LOCK_SET); }

/* +0x50: (set, id, step) */
static int lock_clear(void *set, int id, int step, int (*stock)(void *, int, int))
{
    unsigned code, a;
    int track;
    if (!ours(set, id, &code, &track))
        return stock(set, id, step);
    if (!(a = where(step)))
        return 0;
    return put(a, code, -1);
}

int core_lock_clear_vt(void *set, int id, int step) { return lock_clear(set, id, step, SOUND_LOCK_CLEAR); }
int core_fx_lock_clear_vt(void *set, int id, int step) { return lock_clear(set, id, step, FX_LOCK_CLEAR); }
int core_cv_lock_clear_vt(void *set, int id, int step) { return lock_clear(set, id, step, CV_LOCK_CLEAR); }

/* +0x54: (set, id, delta 8.8, step) -> the new lock (8.8): a whole step at
 * least, from the lock or, with none yet, the item's value. The three sets
 * share stock's routine. */
int core_lock_turn_vt(void *set, int id, int delta, int step)
{
    const struct core_lock_param *p;
    unsigned code, a;
    unsigned char *s;
    int v, track;
    if (!(p = ours(set, id, &code, &track)))
        return LOCK_TURN(set, id, delta, step);
    if (!(a = where(step)))
        return -1;
    if ((v = get(a, code)) < 0)
        v = (s = edited_item(track)) ? (int)core_store_get(core_lock_field(p), s, 0) : 0;
    v = core_lock_clamp(p, v + (delta > 0 ? (delta + 255) >> 8 : delta < 0 ? -((255 - delta) >> 8) : 0));
    put(a, code, v);
    return v << 8;
}

/* page_get_value with a step (pages.s): a mod knob of the current track
 * shows the step's lock; 1 and *value (8.8) when it has one */
int core_lock_page_get(int id, int step, int *value)
{
    unsigned code, w, a;
    int track, v;
    if (step < 0 || id <= 0x126 || (track = current(&w)) < 0 ||
        !core_lock_by_id_in(id, &code, core_lock_kind_of_track(track)) || !(a = wrapper_step(w, step)) ||
        (v = get(a, code)) < 0)
        return 0;
    *value = v << 8;
    return 1;
}

/* 0x4002327c (rows.s): is the id a mod parameter's knob on the current
 * track? Then a trig held locks it. */
int core_lock_knob(int id)
{
    unsigned code, w;
    int track = current(&w);
    return track >= 0 && core_lock_by_id_in(id, &code, core_lock_kind_of_track(track)) != 0;
}

/* param_apply_delta, a mod's knob turn taken (pages.s): a mod parameter's
 * new value is recorded while live recording runs */
void core_lock_turned(int id)
{
    const struct core_lock_param *p;
    unsigned code, w;
    unsigned char *s;
    int track;
    if (id <= 0x126 || (track = current(&w)) < 0 ||
        !(p = core_lock_by_id_in(id, &code, core_lock_kind_of_track(track))) || !(s = edited_item(track)))
        return;
    core_lock_record(track, code, core_store_get(core_lock_field(p), s, 0));
}

/* ---- playing locks ---- */

#define VOICES 6
#define SLOTS 5                                 /* immediate, 3 scheduled, the note path's */
#define LIST_NOW 0x42872b60u                    /* + track x 0x390 */
#define LIST_LATER 0x428740c0u                  /* + track x 0xab0 + slot x 0x390 */
#define LIST_NOTE 0x4287a820u                   /* + track x 0x390 */
#define LIST_SIZE 0x390
#define VOICE_LISTS 0x8000e670u                 /* + voice x 0x390 */

struct slot {
    unsigned short value[CODES];                /* the lock + 1, or 0 */
    short delta[CODES];                         /* Param Slide's: next - this, in the field's steps */
    unsigned rec;                               /* the step record, 0 for none */
    unsigned gen;                               /* the ledger's gen when filled */
    int deltas;                                 /* the fire computed slides */
};

static struct slot slots[TRACKS][SLOTS];

/* per voice: the playing trig's mod locks, each value + 1 (0: none), and
 * their slide deltas; the voice's track and a count of its accepts */
volatile unsigned short core_lock_now[VOICES][CODES];
volatile short core_slide_delta[VOICES][CODES];
volatile signed char core_lock_voice_track[VOICES] = {-1, -1, -1, -1, -1, -1};
volatile unsigned core_lock_accepts[VOICES];     /* the accept's number, of all voices' */
static unsigned accepts;

/* a lock list's track and slot, or -1 */
static int list_slot(unsigned list, int *track)
{
    unsigned o;
    if (list >= LIST_NOW && (o = list - LIST_NOW) < TRACKS * LIST_SIZE && o % LIST_SIZE == 0) {
        *track = o / LIST_SIZE;
        return 0;
    }
    if (list >= LIST_LATER && (o = list - LIST_LATER) < TRACKS * 0xab0 && o % 0xab0 % LIST_SIZE == 0 &&
        o % 0xab0 / LIST_SIZE < 3) {
        *track = o / 0xab0;
        return 1 + o % 0xab0 / LIST_SIZE;
    }
    if (list >= LIST_NOTE && (o = list - LIST_NOTE) < TRACKS * LIST_SIZE && o % LIST_SIZE == 0) {
        *track = o / LIST_SIZE;
        return 4;
    }
    return -1;
}

static int16_t rd16(const volatile unsigned char *p)
{
    return (int16_t)((unsigned)p[0] << 8 | p[1]);
}

/* the playing kit's item for the track: its sound on a synth track, the kit
 * on FX and CV (as the trig fire's slide reads the sound's own value) */
static unsigned char *played_item(unsigned pat, int track)
{
    int k = (signed char)*(volatile unsigned char *)(pat + 0x16eca);
    if (k < 0)
        k = *PLAY_KIT;
    if ((unsigned)k >= 128)
        return 0;
    return (unsigned char *)LIVE_KITS + LIVE_KIT_SIZE * k + (track < 4 ? KIT_SOUNDS + SOUND_SIZE * track : 0);
}

/* the slide's next step for the step record, as stock's trig fire walks it
 * (0x4007a558..): forward, wrapping at the track's length, to the next step
 * with a trig or a lock trig; -1 for none (or a track in the mode whose
 * steps the trig fire remaps, +0x32a, which core does not follow) */
static int next_step(unsigned pat, int track, int step, unsigned *this_flags, unsigned *next_flags)
{
    unsigned rec = pat + LIVE_TRACK * track;
    int len, n, i;
    if (*(volatile unsigned char *)(rec + 0x32a) == 1)
        return -1;
    len = *(volatile unsigned char *)(pat + 0x16ecc) ? rd16((unsigned char *)rec + 0x1c5)
                                                      : rd16((unsigned char *)pat + 0x16ec6);
    if (len < 1 || len > 64)
        return -1;
    *this_flags = (unsigned short)rd16((unsigned char *)rec + 2 * step);
    if (!(*this_flags & 0x20))
        return -1;
    for (n = step, i = 0; i < 64; i++) {
        n = n + 1 == len ? 0 : n + 1;
        *next_flags = (unsigned short)rd16((unsigned char *)rec + 2 * n);
        if (*next_flags & 3)
            return n;
    }
    return -1;
}

/* a slot: the step's mod locks, with Param Slide's deltas when the trig fire
 * computed stock's (deltas set and the step's flag 0x20), by stock's four
 * rules */
static void fill(struct slot *sl, int track, int kind, unsigned rec, unsigned deltas)
{
    struct ent at[CODES], nx[CODES];
    unsigned pat, nrec, tf = 0, nf = 0, o;
    unsigned char *item = 0;
    int k, nk = 0, i, j, n, step, live;
    int a, b, snd;
    const struct core_lock_param *p;
    for (i = 0; i < CODES; i++) {
        sl->value[i] = 0;
        sl->delta[i] = 0;
    }
    sl->rec = rec;
    sl->deltas = deltas != 0;
    sl->gen = gen;
    k = rec ? step_locks(rec, at) : 0;
    for (i = 0; i < k; i++)
        if (core_lock_by_code(kind, at[i].code))
            sl->value[at[i].code - FIRST_CODE] = at[i].value + 1;
    if (!deltas || (live = live_of(rec)) < 0)
        return;
    pat = LIVE_PATTERNS + PATTERN_SIZE * live;
    o = rec - store_of(live);
    if ((int)(o / TRACK_LOCKS) != track || o % TRACK_LOCKS % STEP_LOCKS)
        return;
    step = o % TRACK_LOCKS / STEP_LOCKS;
    if ((n = next_step(pat, track, step, &tf, &nf)) < 0 || (nf & 4))
        return;
    nrec = store_of(live) + TRACK_LOCKS * track + STEP_LOCKS * n;
    nk = step_locks(nrec, nx);
    for (j = 0; j < CODES; j++) {
        if (!(p = core_lock_by_code(kind, FIRST_CODE + j)))
            continue;
        a = sl->value[j] ? sl->value[j] - 1 : -1;
        b = -1;
        for (i = 0; i < nk; i++)
            if (nx[i].code == FIRST_CODE + j)
                b = nx[i].value;
        if (a < 0 && b < 0)
            continue;
        if ((a < 0 && (tf & 2)) || (b < 0 && (nf & 2)))
            continue;                           /* a lock trig's side: no slide */
        if (a < 0 || b < 0) {
            if (!item && !(item = played_item(pat, track)))
                return;
            snd = core_store_get(core_lock_field(p), item, 0);
            if (a < 0)
                a = snd;
            else
                b = snd;
        }
        sl->delta[j] = b - a;
    }
}

/* 0x40079fbe, a trig's lock list (track, list, the step record, deltas),
 * after stock (rows.s): core's slot for the list */
void core_rows_fire(int track, unsigned list, unsigned rec, unsigned deltas)
{
    int t, slot = list_slot(list, &t), kind = core_lock_kind_of_track(track);
    if (slot < 0 || t != track || kind < 0)
        return;
    fill(&slots[track][slot], track, kind, rec, deltas);
}

/* the event accept in the audio interrupt (rows.s): the voice's list (dst)
 * from the event's list; the slot's locks become the voice's */
static void accept(int voice, int track, int slot)
{
    int i, kind;
    struct slot *sl;
    if ((unsigned)voice >= VOICES || (unsigned)track >= TRACKS || (unsigned)slot >= SLOTS)
        return;
    sl = &slots[track][slot];
    if (sl->rec && sl->gen != gen && (kind = core_lock_kind_of_track(track)) >= 0)
        /* edited since the fire: stock fills a stopped sequencer's first
         * step at STOP and again on each of its own lock edits, which a mod
         * lock's edit does not do, so the step's locks are read again here */
        fill(sl, track, kind, sl->rec, sl->deltas);
    for (i = 0; i < CODES; i++) {
        core_lock_now[voice][i] = sl->value[i];
        core_slide_delta[voice][i] = sl->delta[i];
    }
    core_lock_voice_track[voice] = track;
    core_lock_accepts[voice] = ++accepts;
}

void core_rows_accept(unsigned dst, unsigned list)
{
    int track, slot = list_slot(list, &track);
    if (dst < VOICE_LISTS || slot < 0)
        return;
    accept((dst - VOICE_LISTS) / LIST_SIZE, track, slot);
}

/* the note path's accept (0x40101fb2): index n's list (0x4287a820 + n x
 * 0x390, which the note path fills for track n) to voice n */
void core_rows_accept_note(int n)
{
    accept(n, n, 4);
}

/* a field set (core_store_set): a voice playing the item's track no longer
 * slides that parameter, as stock's knob push (0x401005b8) clears a stock
 * one's delta */
void core_lock_cancel(unsigned field, void *item)
{
    unsigned o = (unsigned)item - LIVE_KITS, code;
    int kind, track, v;
    if ((unsigned)item < LIVE_KITS || o / LIVE_KIT_SIZE >= 128 || !(code = core_lock_code(field)) ||
        (kind = core_lock_kind(field)) < 0)
        return;
    o %= LIVE_KIT_SIZE;
    if (kind == 0) {
        if (o < KIT_SOUNDS || (o - KIT_SOUNDS) % SOUND_SIZE || (o - KIT_SOUNDS) / SOUND_SIZE >= 4)
            return;
        track = (o - KIT_SOUNDS) / SOUND_SIZE;
    } else {
        if (o)
            return;
        track = kind == 1 ? 4 : 5;
    }
    for (v = 0; v < VOICES; v++)
        if (core_lock_voice_track[v] == track)
            core_slide_delta[v][code - FIRST_CODE] = 0;
}

/* a new or loaded project and a kit change: no trig has played in it yet */
void core_rows_now_reset(void)
{
    unsigned short sr = irq_mask();
    int t, i, s;
    for (t = 0; t < VOICES; t++) {
        for (i = 0; i < CODES; i++) {
            core_lock_now[t][i] = 0;
            core_slide_delta[t][i] = 0;
        }
        core_lock_voice_track[t] = -1;
    }
    for (t = 0; t < TRACKS; t++)
        for (s = 0; s < SLOTS; s++)
            for (i = 0; i < CODES; i++) {
                slots[t][s].value[i] = 0;
                slots[t][s].delta[i] = 0;
            }
    irq_restore(sr);
}

/* the voice's playing p-lock of code, or -1 */
int core_lock_value_voice(int voice, int code)
{
    if ((unsigned)voice >= VOICES || code < FIRST_CODE || code > LAST_CODE)
        return -1;
    return core_lock_now[voice][code - FIRST_CODE] - 1;
}

/* the track's: the voice its last trig went to (core's own record of the
 * accepts) */
int core_lock_value_code(int track, int code)
{
    int v, best = -1;
    unsigned n = 0;
    for (v = 0; v < VOICES; v++)
        if (core_lock_voice_track[v] == track && (best < 0 || (int)(core_lock_accepts[v] - n) > 0)) {
            best = v;
            n = core_lock_accepts[v];
        }
    return best < 0 ? -1 : core_lock_value_voice(best, code);
}

int core_lock_value(int track, unsigned field)
{
    int kind = core_lock_kind(field);
    if (kind < 0 || kind != core_lock_kind_of_track(track))
        return -1;
    return core_lock_value_code(track, core_lock_code(field));
}

int core_slide_delta_voice(int voice, int code)
{
    if ((unsigned)voice >= VOICES || code < FIRST_CODE || code > LAST_CODE)
        return 0;
    return core_slide_delta[voice][code - FIRST_CODE];
}
