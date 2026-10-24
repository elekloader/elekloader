/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * core-ot 0.6: the project's lifecycle, the engine task, and the mods' data on the card and in CS1
 * (docs/ADAPTING.md, "Project data on the Octatrack"; the declarations are in
 * elekloader/sdk/include/octatrack/core.h, the sites in store.s).
 *
 * - ev_project(how, dropped), as core-ar2's and core-a4's: after a job that loads a whole project
 *   (LOAD PROJECT, the power-up's load, RELOAD) or makes a new one.
 * - ot_engine_post: a mod's function run by the engine task, where the firmware does its own card
 *   I/O; the file calls are for there.
 * - core_projdata / core_carddata: the Digitone's descriptor ({tag, size, data, loaded}); core keeps
 *   each block in a file pair the stock OS ignores, <project>/elekloader.work and .strd, and
 *   /ELEKLOADER/card.work and .strd: .work rewritten about 2 s after a mod marks its block changed,
 *   .strd copied where stock copies its own (SAVE PROJECT) and back (RELOAD). A file that does not
 *   read back is never written over: core asks once, at the next encoder turn, whether to keep it
 *   (mods run without it) or start fresh (it is copied to .bak first).
 * - ot_sram: claims in the battery-backed CS1 range stock never touches (0x100f859c..0x100fff00).
 *
 * The pair rules and the job queue follow octabam's KITS and BRAIN (MIT, docs/proposals/BRAIN.md
 * sections 6.2-6.3: KITS measured them on a unit); the container is our own. With no block and
 * no claim in the build, core reads and writes nothing.
 */
#include <stdint.h>
#include "octatrack/core.h"

#define U8(a)           (*(volatile uint8_t *)(uintptr_t)(a))
#define U32(a)          (*(volatile uint32_t *)(uintptr_t)(a))

#define F_COPY          ((int (*)(const char *, const char *, int))0x40016388)     /* (dst, src, 0) -> <0 */
#define F_OPEN          ((int (*)(void *, const char *, const char *, void *, uint32_t))0x40016864)
#define F_READ          ((int (*)(void *, void *, uint32_t))0x40016564)           /* -> 1: all n read */
#define F_WRITE         ((int (*)(void *, const void *, uint32_t))0x400166b8)     /* -> 1 */
#define F_CLOSE         ((int (*)(void *))0x4001677c)
/* The firmware returns pointers in d0; GCC's m68k ABI reads a returned pointer from a0. A stock
 * routine that returns one is declared returning an integer, and cast. */
#define PROJDIR         ((uint32_t (*)(int, int))0x40025230)                      /* the project's folder */
#define MKDIR           (*(int (**)(const char *))0x46c8240a)                     /* the FS vtable's mkdir */
#define Q_SEND          ((int (*)(void *, void *))0x40000c3c)                     /* (queue, message) */
#define ENGINE_Q        ((void *)0x460d17ce)
#define DIALOG          ((void (*)(const char *, uint32_t, const char *const *, uint32_t, void (*)(int)))0x4006d57c)
#define LIST_OPEN       U32(0x460e5e30)     /* a list window is open */
#define TEXT_OPEN       U32(0x460e7612)     /* a text editor is open */
#define CONFIRM_OPEN    U32(0x460e5cd0)     /* a stock confirmation dialog is open */

#define OTS_JOB         0x42                /* store.s */
#define BUF_LEN         65536u              /* a file, whole */
#define KEPT_LEN        16384u              /* records of mods not in this build, written back */
#define HDR             32u
#define REC             12u

typedef void (*ev2)(unsigned, unsigned);
extern const ev2 ev_banks_load[], ev_banks_write[], ev_project_store[], ev_project_reload[],
    ev_cs1_bank[], ev_part_saved[], ev_part_cleared[], ev_part_saving[], ev_part_reload[],
    ev_part_init[], ev_project_init[];
extern void (*const ev_project[])(int32_t, uint32_t);
extern const struct core_projdata *const core_projdata[], *const core_carddata[];
extern const struct ot_sram *const ot_sram[];

static inline uint32_t irq_off(void)
{
    uint32_t sr;
    __asm__ volatile("move.w %%sr,%0\n\tmove.w #0x2700,%%sr" : "=d"(sr) : : "memory");
    return sr;
}

static inline void irq_on(uint32_t sr)
{
    __asm__ volatile("move.w %0,%%sr" : : "d"(sr) : "memory");
}

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static void put32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static uint32_t align4(uint32_t n) { return (n + 3) & ~3u; }

static void zero(volatile uint8_t *p, uint32_t n)
{
    while (n--)
        *p++ = 0;
}

static void copy(volatile uint8_t *d, const volatile uint8_t *s, uint32_t n)
{
    while (n--)
        *d++ = *s++;
}

static uint32_t crc32(const uint8_t *p, uint32_t n)
{
    uint32_t c = 0xffffffffu;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xedb88320u & -(c & 1));
    }
    return ~c;
}

static unsigned slen(const char *s)
{
    unsigned n = 0;
    while (s[n])
        ++n;
    return n;
}

static int scmp(const char *a, const char *b)
{
    while (*a && *a == *b)
        ++a, ++b;
    return (uint8_t)*a - (uint8_t)*b;
}

/* d = a then b, at most n - 1 bytes; 0 when it did not fit. */
static int cat2(char *d, unsigned n, const char *a, const char *b)
{
    unsigned i = 0;
    for (; *a; ++a)
        if (i + 1 < n) d[i++] = *a; else return 0;
    for (; *b; ++b)
        if (i + 1 < n) d[i++] = *b; else return 0;
    d[i] = 0;
    return 1;
}

/* ==== the engine task ========================================================================== */

#define NJOB 8
static struct { void (*fn)(uint32_t); uint32_t arg; } jobs[NJOB];
static volatile uint32_t jhead, jtail, posted;
static uint8_t job_msg[16] __attribute__((aligned(4))) = { OTS_JOB };
volatile int32_t ots_last_job = -1;         /* store.s: the stock job running, -1 none */
uint32_t ots_counts[16];                    /* see core.h, OTS_C_* */
char ots_last_path[64];                     /* the last file core could not open for writing */

static void repost(void)
{
    uint32_t sr = irq_off();
    int need = !posted && jhead != jtail;
    if (need)
        posted = 1;
    irq_on(sr);
    if (need && Q_SEND(ENGINE_Q, job_msg) < 0)
        posted = 0;                         /* the queue was full: ots_tick tries again */
}

int ot_engine_post(void (*fn)(uint32_t), uint32_t arg)
{
    uint32_t sr = irq_off();
    uint32_t t = (jtail + 1) % NJOB;
    if (t == jhead) {
        irq_on(sr);
        ++ots_counts[OTS_C_JOBS_FULL];
        return 0;
    }
    jobs[jtail].fn = fn;
    jobs[jtail].arg = arg;
    jtail = t;
    irq_on(sr);
    repost();
    return 1;
}

/* store.s, the engine task, at a message of core's type. */
void ots_job_run(void)
{
    posted = 0;                             /* a post from here on sends another message */
    while (jhead != jtail) {
        void (*fn)(uint32_t) = jobs[jhead].fn;
        uint32_t arg = jobs[jhead].arg;
        jhead = (jhead + 1) % NJOB;
        fn(arg);
    }
}

/* ==== the file calls (the engine task) ========================================================== */

int ot_file_open(struct ot_file *f, const char *path, const char *mode)
{
    return F_OPEN(f->obj, path, mode, f->iob, sizeof f->iob);
}

int ot_file_read(struct ot_file *f, void *dst, uint32_t n) { return F_READ(f->obj, dst, n); }
int ot_file_write(struct ot_file *f, const void *src, uint32_t n) { return F_WRITE(f->obj, src, n); }
void ot_file_close(struct ot_file *f) { F_CLOSE(f->obj); }
int ot_file_copy(const char *dst, const char *src) { return F_COPY(dst, src, 0); }
int ot_mkdir(const char *path) { return MKDIR(path); }

const char *ot_project_dir(void)
{
    const char *d = (const char *)(uintptr_t)PROJDIR(0, 0);
    return d ? d : "";
}

/* ==== the files: one container per file ============================================================
 *   0  "ELKD"   4  u16 header size (32), u8 version (1), u8 0 for a project's file, 1 for the card's
 *   8  u32 total bytes   12 u32 records   16 u32 CRC-32 of bytes 20..total-1   20..31 zero
 *   records: u32 tag, u32 size, u32 CRC-32 of the bytes, then the bytes, padded to 4 with zeros. */

static struct ot_file fo;
static uint8_t buf[BUF_LEN] __attribute__((aligned(4)));

enum { PAIR_PROJECT, PAIR_CARD };
static const struct core_projdata *const *const pair_table[2] = { core_projdata, core_carddata };
static const char *const pair_name[2] = { "/elekloader", "/ELEKLOADER/card" };   /* after the folder */
static struct pair {                    /* in .bss: no initializer */
    char work[256], strd[256];
    uint8_t kept[KEPT_LEN] __attribute__((aligned(4)));
    uint32_t nkept, kept_len;
    uint8_t loaded;                     /* the blocks came from (or were set for) this folder */
    uint8_t refused;                    /* the pair did not read back: never write it */
    uint8_t asked;                      /* the prompt for it is waiting */
    uint8_t exists;                     /* a .work of ours is there (read, or written) */
    volatile uint8_t dirty;
    uint32_t dirty_at;
} pairs[2];
#define TABLE(p)        (pair_table[(p) - pairs])

static char loaded_dir[256];

static int have_blocks(const struct pair *p)
{
    return TABLE(p)[0] != 0 || p->nkept;
}

static int set_paths(struct pair *p)
{
    const char *dir = p == &pairs[PAIR_PROJECT] ? ot_project_dir() : "";
    char base[256];
    if (p == &pairs[PAIR_PROJECT] && !*dir)
        return 0;
    if (!cat2(base, sizeof base, dir, pair_name[p - pairs]))
        return 0;
    return cat2(p->work, sizeof p->work, base, ".work") && cat2(p->strd, sizeof p->strd, base, ".strd");
}

/* The file at path into buf: its length when it holds a valid container, 0 when there is no file,
 * 1 when there is one that does not read back. */
static uint32_t slurp(const char *path, unsigned kind)
{
    uint32_t n = 1;
    if (ot_file_open(&fo, path, "r") < 0)
        return 0;
    if (ot_file_read(&fo, buf, HDR) == 1 && be32(buf) == 0x454c4b44u && buf[4] == 0 && buf[5] == HDR
        && buf[6] == 1 && buf[7] == kind) {
        uint32_t total = be32(buf + 8);
        if (total >= HDR && total <= BUF_LEN
            && (total == HDR || ot_file_read(&fo, buf + HDR, total - HDR) == 1)
            && crc32(buf + 20, total - 20) == be32(buf + 16)) {
            uint32_t at = HDR, count = be32(buf + 12), r;
            for (r = 0; r < count; ++r) {
                if (at + REC > total)
                    break;
                uint32_t size = be32(buf + at + 4);
                if (size > total - at - REC || crc32(buf + at + REC, size) != be32(buf + at + 8))
                    break;
                at += REC + align4(size);
            }
            if (r == count && at == total)
                n = total;
        }
    }
    ot_file_close(&fo);
    return n;
}

static const struct core_projdata *find(const struct pair *p, uint32_t tag)
{
    for (const struct core_projdata *const *d = TABLE(p); *d; ++d)
        if ((*d)->tag == tag)
            return *d;
    return 0;
}

/* Zeros in every block, then each one's record from buf[0..n) when n holds a file. Records of tags
 * this build lacks are kept, to be written back. -> 1 when a file was used. */
static int apply(struct pair *p, uint32_t n)
{
    for (const struct core_projdata *const *d = TABLE(p); *d; ++d)
        zero((volatile uint8_t *)(*d)->data, (*d)->size);
    p->nkept = p->kept_len = 0;
    if (n < HDR) {
        for (const struct core_projdata *const *d = TABLE(p); *d; ++d)
            if ((*d)->loaded)
                (*d)->loaded(0);
        return 0;
    }
    uint32_t at = HDR, count = be32(buf + 12);
    uint32_t found = 0;                     /* bit k: table entry k came from the file */
    for (uint32_t r = 0; r < count; ++r) {
        uint32_t tag = be32(buf + at), size = be32(buf + at + 4), len = REC + align4(size);
        const struct core_projdata *d = find(p, tag);
        if (d && d->size == size) {
            copy((volatile uint8_t *)d->data, buf + at + REC, size);
            for (unsigned k = 0; TABLE(p)[k]; ++k)
                if (TABLE(p)[k] == d && k < 32)
                    found |= 1u << k;
        } else if (!d) {
            if (p->kept_len + len <= KEPT_LEN) {
                copy(p->kept + p->kept_len, buf + at, len);
                p->kept_len += len;
                ++p->nkept;
            } else {
                p->refused = 1;             /* it would be lost by the next write: keep the file */
                ++ots_counts[OTS_C_KEPT_FULL];
            }
        }                                   /* a known tag of another size: the mod's new layout */
        at += len;
    }
    for (unsigned k = 0; TABLE(p)[k]; ++k)
        if (TABLE(p)[k]->loaded)
            TABLE(p)[k]->loaded(k < 32 && (found >> k & 1));
    return 1;
}

/* The blocks and the kept records, as a container in buf: its length, 0 when it does not fit. */
static uint32_t compose(const struct pair *p, unsigned kind)
{
    uint32_t at = HDR, count = 0;
    for (const struct core_projdata *const *d = TABLE(p); *d; ++d) {
        uint32_t size = (*d)->size, len = REC + align4(size);
        if (at + len + p->kept_len > BUF_LEN)
            return 0;
        put32(buf + at, (*d)->tag);
        put32(buf + at + 4, size);
        copy(buf + at + REC, (const volatile uint8_t *)(*d)->data, size);
        zero(buf + at + REC + size, align4(size) - size);
        put32(buf + at + 8, crc32(buf + at + REC, size));
        at += len;
        ++count;
    }
    copy(buf + at, p->kept, p->kept_len);
    at += p->kept_len;
    count += p->nkept;
    zero(buf, HDR);
    put32(buf, 0x454c4b44u);                /* "ELKD" */
    buf[5] = HDR;
    buf[6] = 1;
    buf[7] = kind;
    put32(buf + 8, at);
    put32(buf + 12, count);
    put32(buf + 16, crc32(buf + 20, at - 20));
    return at;
}

static int write_work(struct pair *p)
{
    unsigned kind = p == &pairs[PAIR_CARD];
    uint32_t n;
    if (p->refused || !have_blocks(p) || !set_paths(p) || !(n = compose(p, kind))) {
        ++ots_counts[OTS_C_WRITE_ERR];
        return -1;
    }
    int rc = ot_file_open(&fo, p->work, "w");
    if (rc < 0 && kind) {
        ot_mkdir("/ELEKLOADER");            /* the first write on this card */
        rc = ot_file_open(&fo, p->work, "w");
    }
    if (rc < 0) {
        ++ots_counts[OTS_C_WRITE_ERR];
        ots_counts[OTS_C_LAST_ERR] = (uint32_t)rc;
        cat2(ots_last_path, sizeof ots_last_path, p->work, "");
        return -1;
    }
    int ok = ot_file_write(&fo, buf, n) == 1;
    ot_file_close(&fo);
    if (!ok) {
        ++ots_counts[OTS_C_WRITE_ERR];
        return -1;
    }
    p->exists = 1;
    ++ots_counts[OTS_C_WRITES];
    return 0;
}

/* The pair's blocks from the card (the rules of BRAIN.md 6.2): a valid .work, else a valid .strd;
 * neither file: fresh; a file that does not read back and no valid copy: refused. -> 1 when the
 * blocks came from a file. */
static int load_pair(struct pair *p)
{
    unsigned kind = p == &pairs[PAIR_CARD];
    uint32_t n = 0;
    p->refused = p->asked = p->exists = 0;
    p->dirty = 0;
    if (!TABLE(p)[0] || !set_paths(p)) {    /* no block of ours: the file is not read */
        apply(p, 0);
        p->loaded = TABLE(p)[0] != 0;
        return 0;
    }
    uint32_t w = slurp(p->work, kind);
    if (w > 1) {
        n = w;
        p->exists = 1;
    } else {
        uint32_t s = slurp(p->strd, kind);
        if (s > 1) {
            n = s;                          /* .work damaged or gone: the saved copy */
            ++ots_counts[OTS_C_RECOVERED];
        } else if (w || s) {
            p->refused = p->asked = 1;      /* there is a file, and neither copy reads back */
            ++ots_counts[OTS_C_REFUSED];
        }
    }
    int used = apply(p, n);
    p->loaded = 1;
    return used;
}

static uint32_t ticks;
static volatile uint32_t write_queued;

static void write_job(uint32_t arg)
{
    (void)arg;
    write_queued = 0;
    for (unsigned k = 0; k < 2; ++k) {
        struct pair *p = &pairs[k];
        if (p->dirty && p->loaded && !p->refused) {
            p->dirty = 0;
            write_work(p);
        }
    }
}

void core_projdata_dirty(uint32_t tag)
{
    (void)tag;
    pairs[PAIR_PROJECT].dirty = 1;
    pairs[PAIR_PROJECT].dirty_at = ticks;
}

void core_carddata_dirty(uint32_t tag)
{
    (void)tag;
    pairs[PAIR_CARD].dirty = 1;
    pairs[PAIR_CARD].dirty_at = ticks;
}

/* ==== the lifecycle ============================================================================== */

/* store.s, the engine task, back at its receive: the job noted has ended. */
void ots_job_end(void)
{
    int32_t job = ots_last_job;
    ots_last_job = -1;
    int32_t how;
    switch (job) {
    case 3: case 4: case 5: case 19:        /* every bank loaded, LOAD PROJECT, the power-up, RELOAD */
        how = CORE_PROJECT_LOADED;
        break;
    case 9: case 10: case 13:               /* CREATE EMPTY PROJECT and the two other new-project jobs */
        how = CORE_PROJECT_NEW;
        break;
    default:
        return;
    }
    uint32_t dropped = 0;
    struct pair *c = &pairs[PAIR_CARD], *p = &pairs[PAIR_PROJECT];
    if (!c->loaded && TABLE(c)[0] && !load_pair(c))
        dropped |= OT_DROPPED_CARD;         /* the card's blocks: once, the first time */
    if (how == CORE_PROJECT_LOADED) {
        if (!load_pair(p) && TABLE(p)[0])
            dropped |= OT_DROPPED_PROJECT;
    } else {
        p->refused = p->asked = p->exists = 0;
        p->dirty = 0;
        apply(p, 0);                        /* a new project: zeros, loaded(0) */
        p->loaded = 1;
        if (TABLE(p)[0])
            dropped |= OT_DROPPED_PROJECT;
    }
    const char *d = ot_project_dir();
    if (!cat2(loaded_dir, sizeof loaded_dir, d, ""))
        loaded_dir[0] = 0;
    ++ots_counts[how == CORE_PROJECT_NEW ? OTS_C_NEW : OTS_C_LOADED];
    for (void (*const *f)(int32_t, uint32_t) = ev_project; *f; ++f)
        (*f)(how, dropped);
}

static const ev2 *const tables[] = { ev_banks_load, ev_banks_write, ev_project_store,
                                     ev_project_reload, ev_cs1_bank, ev_part_saved, ev_part_cleared,
                                     ev_part_saving, ev_part_reload, ev_part_init, ev_project_init };
enum { EV_BANKS_LOAD, EV_BANKS_WRITE, EV_PROJECT_STORE, EV_PROJECT_RELOAD, EV_CS1_BANK,
       EV_PART_SAVED, EV_PART_CLEARED,
       EV_PART_SAVING, EV_PART_RELOAD, EV_PART_INIT, EV_PROJECT_INIT };     /* 0.8: edit.s */

/* store.s and edit.s: core's part of the event, then its handlers. */
void ots_event(unsigned ev, unsigned a, unsigned b)
{
    struct pair *p = &pairs[PAIR_PROJECT], *c = &pairs[PAIR_CARD];
    if (ev == EV_PROJECT_STORE) {
        if (a) {                            /* SAVE PROJECT: the .work files as they are now */
            for (unsigned k = 0; k < 2; ++k)
                if (pairs[k].dirty && pairs[k].loaded && !pairs[k].refused) {
                    pairs[k].dirty = 0;
                    write_work(&pairs[k]);
                }
        } else {                            /* stock has copied its .work files: ours too */
            for (unsigned k = 0; k < 2; ++k)
                if (pairs[k].exists && !pairs[k].refused && set_paths(&pairs[k])
                    && ot_file_copy(pairs[k].strd, pairs[k].work) >= 0)
                    ++ots_counts[OTS_C_STRD];
        }
    } else if (ev == EV_PROJECT_RELOAD && !a) {
        /* stock has copied its .strd files back: ours too (the job's end loads the blocks) */
        if (TABLE(p)[0] && !p->refused && set_paths(p))
            ot_file_copy(p->work, p->strd);
    } else if (ev == EV_BANKS_WRITE && !a && p->loaded && have_blocks(p) && !p->refused) {
        /* the bank writer saved the project into another folder (SAVE AS): ours goes there too */
        const char *d = ot_project_dir();
        if (*d && scmp(d, loaded_dir)) {
            if (write_work(p) == 0 && set_paths(p))
                ot_file_copy(p->strd, p->work);
            cat2(loaded_dir, sizeof loaded_dir, d, "");
        }
    }
    (void)c;
    for (const ev2 *f = tables[ev]; *f; ++f)
        (*f)(a, b);
}

/* ==== a file that does not read back ===================================================== */

static const char *const prompt_lines[2][3] = {
    { "PROJECT DATA UNREADABLE", "MODS RUN WITHOUT IT", "NO: KEEP  YES: START NEW" },
    { "CARD DATA UNREADABLE", "MODS RUN WITHOUT IT", "NO: KEEP  YES: START NEW" },
};
static uint8_t prompt_for;                  /* which pair the open prompt is about */

static void fresh_job(uint32_t k)
{
    struct pair *p = &pairs[k];
    char bak[256];
    if (!set_paths(p))
        return;
    if (cat2(bak, sizeof bak, p->work, ".bak") && ot_file_copy(bak, p->work) < 0)
        ;                                   /* no .work: perhaps only a damaged .strd */
    if (cat2(bak, sizeof bak, p->strd, ".bak"))
        ot_file_copy(bak, p->strd);
    p->refused = 0;
    p->nkept = p->kept_len = 0;
    if (write_work(p) == 0)
        ot_file_copy(p->strd, p->work);     /* both copies good again */
    ++ots_counts[OTS_C_FRESH];
}

static void prompt_answer(int choice)
{
    if (choice == 0)                        /* YES: start fresh, the old files kept as .bak */
        ot_engine_post(fresh_job, prompt_for);
}                                           /* NO: keep the file, untouched, until the next load */

/* bus.s's core_enc, the sys task: an encoder turn. -> 1 when it opened the prompt (the turn is spent). */
int ots_ui_enc(void)
{
    for (unsigned k = 0; k < 2; ++k) {
        struct pair *p = &pairs[k];
        if (!p->asked)
            continue;
        if (LIST_OPEN || TEXT_OPEN || CONFIRM_OPEN)
            return 0;
        p->asked = 0;
        prompt_for = k;
        DIALOG("ELEKLOADER", 3, prompt_lines[k], 3, prompt_answer);
        return 1;
    }
    return 0;
}

/* bus.s's core_tick, the sys task, 60 times a second: a changed block goes out 2 s after the last
 * change, and a post the queue refused is tried again. */
void ots_tick(void)
{
    ++ticks;
    if (!write_queued)
        for (unsigned k = 0; k < 2; ++k)
            if (pairs[k].dirty && ticks - pairs[k].dirty_at >= 120 && pairs[k].loaded) {
                write_queued = 1;
                if (!ot_engine_post(write_job, 0))
                    write_queued = 0;
                break;
            }
    if (!posted && jhead != jtail)
        repost();
}

/* ==== CS1 ========================================================================================
 * At 0x100f859c: "ELKS", the claims (n), a sum of the directory, then n x {tag, size, offset}; the
 * claims' bytes after it. A claim keeps its bytes when the last boot had the same tag, size and
 * offset; any other is zeroed. */
#define CS1_LO          0x100f859cu
#define CS1_HI          0x100fff00u
#define CS1_MAX         64u

/* bus.s's core_main_end, before ev_boot_done: every claim gets its address. */
void ots_boot(void)
{
    unsigned n = 0;
    while (ot_sram[n] && n < CS1_MAX)
        ++n;
    if (!n)
        return;                             /* no claim: CS1 is not touched */
    volatile uint32_t *dir = (volatile uint32_t *)CS1_LO;
    unsigned old_n = 0;
    if (dir[0] == 0x454c4b53u && dir[1] <= CS1_MAX) {
        uint32_t sum = 0;
        for (unsigned k = 0; k < 3 * dir[1]; ++k)
            sum += dir[3 + k];
        if (sum == dir[2])
            old_n = dir[1];
    }
    uint32_t at = align4(12 + 12 * n), off[CS1_MAX];
    for (unsigned k = 0; k < n; ++k) {
        const struct ot_sram *c = ot_sram[k];
        uint32_t size = align4(c->size);
        if (CS1_LO + at + size > CS1_HI) {
            off[k] = 0;                     /* no room: the claim gets 0 */
            ++ots_counts[OTS_C_CS1_FULL];
            continue;
        }
        off[k] = at;
        int keep = 0;
        for (unsigned j = 0; j < old_n; ++j)
            if (dir[3 + 3 * j] == c->tag && dir[4 + 3 * j] == c->size && dir[5 + 3 * j] == at)
                keep = 1;
        if (!keep)
            zero((volatile uint8_t *)(CS1_LO + at), size);
        at += size;
    }
    dir[0] = 0;                             /* invalid while the directory changes */
    uint32_t sum = 0;
    for (unsigned k = 0; k < n; ++k) {
        dir[3 + 3 * k] = ot_sram[k]->tag;
        dir[4 + 3 * k] = ot_sram[k]->size;
        dir[5 + 3 * k] = off[k];
        sum += ot_sram[k]->tag + ot_sram[k]->size + off[k];
    }
    dir[1] = n;
    dir[2] = sum;
    dir[0] = 0x454c4b53u;                   /* "ELKS" */
    for (unsigned k = 0; k < n; ++k)
        if (ot_sram[k]->where)
            *ot_sram[k]->where = off[k] ? (void *)(CS1_LO + off[k]) : 0;
}
