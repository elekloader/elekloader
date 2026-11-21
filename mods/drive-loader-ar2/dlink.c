/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Drive Loader: the boot linker (design steps 4 to 8). It links the chosen .elemod files
 * into the build descriptor's +Drive area and merges their table entries into the flashed
 * tables' spare entries, exactly as `elekloader link --device-layout` does on the computer.
 *
 *   pass 1   each file once, in /mods/active order: what it is, what it needs and gives
 *   check    duplicates, core version, requires, conflicts, exports, claims, imports and
 *            table room, in id order, again and again until nothing more is refused
 *   layout   every .run from the area's start in id order, then every .bss
 *   pass 2   each file again: .run into place, .bss zeroed, relocations, table entries
 *   bind     each table with new entries rewritten, sorted by (order, mod id, index)
 *   sites    each loaded mod's sites written into the running main OS (dl_apply_site)
 *
 * A refusal in the layout or in pass 2 (the area is full, a pc16 out of range) drops that
 * mod and starts again from the check, so a mod that needs it goes too. Memory goes only
 * through DL_MEM and files only through dl_read_file (dl.h), so the same code runs on the
 * computer in tests. */
#include "json.h"
#include "dstore.h"

/* ---- the link state ------------------------------------------------------------------------
 * The header lives in .bss; the mods and the pools come from the heap for each dl_link and
 * dl_check, sized to the set (the mods) and to the +Drive area (the pools, dl.h DL_POOL_*),
 * and go back when it returns. The merge's room is the descriptor's biggest table. */

enum { SEC_ABS, SEC_RUN, SEC_BSS, SEC_TABLE, SEC_COUNT, SEC_STORE, SEC_DTABLE, SEC_DCOUNT };
enum { R_ABS32, R_PC32, R_PC16, R_ABS16 };   /* abs16: two big-endian bytes (link.py put_reloc) */
#define FLASHED 0xFFFF
/* A relocation kind's size in bytes, or 0 for one this loader doesn't take */
#define rsize(s, n) (js_eq(s, n, "abs32") || js_eq(s, n, "pc32") ? 4 \
                     : js_eq(s, n, "pc16") || js_eq(s, n, "abs16") ? 2 : 0)
#define NONE 0xFFFF

struct sym {
    u32 hash;
    u16 owner;                           /* FLASHED, or a mod's index */
    u8 sec;                              /* SEC_* */
    u8 pad;
    u16 name;                            /* a +Drive export's name in the arena */
    u16 pad2;
    u32 val;                             /* address, offset in its section, table index */
};

struct local {
    u32 hash;
    u32 val;
    u32 sec;
};

struct list { u16 first, n; };

#ifdef DL_REGIONS
#define DL_REGIONS_MAX 64                    /* regions of all the mods in one link */
#endif

struct dmod {
    u16 choice;                          /* its line in /mods/active (its status record) */
    u8 alive, done;
    u16 id, version, core;               /* arena offsets (NONE: not given) */
    u32 run_len, run_align, bss_len, bss_align;
    u32 run_base, bss_base;
    u32 file_len;                        /* its file's length, from pass 1 */
    struct list imports, weak, requires, conflicts, names;
    u16 exp_first, exp_n;                /* its exports in syms */
    u16 ctr_first, ctr_n;                /* its contributions */
    u8 init_sec;                         /* drive_init, if it has one */
    u32 init_off;
    u16 site_first, site_n;              /* its sites and copied blocks */
    u16 claim_n;                         /* its contributions' claims, in L.site after them */
    u32 sdata_first;                     /* where its sites' bytes start in sdata */
    u16 fld_first, fld_n;                /* its storage fields */
#ifdef DL_COLLECTIONS
    u16 coll_first, coll_n;              /* the tables it declares, in L.coll */
#endif
#ifdef DL_SITES
    u16 tsite_first, tsite_n;            /* its sites in L.sites */
#endif
#ifdef DL_REGIONS
    u16 reg_first, reg_n;                /* its regions in L.regs */
#endif
#ifdef DL_KNOBS
    u16 knobs;                           /* its "knobs": how many ids it asks for */
    u16 kfix_first, kfix_n;              /* the ids its entries name, in L.kfix */
    u32 knob_base;                       /* its run's first id (knob_runs) */
#endif
};

#ifdef DL_KNOBS
/* Knob ids an entry names (link.Mod2.id_ranges), a +Drive mod's or (claims of kind 4) a flashed
 * module's: [a, b], signed as link.py reads them. */
struct kfix {
    s32 a, b;
    u16 who;                             /* the mod's index, or the flashed module's */
    u8 own_only, pad;                    /* from a table whose ids are the mods' own (dl.h DL_KNOB_TABLES) */
};
#endif

#ifdef DL_SITES
/* A site a mod writes in the running OS (the Octatrack's TELOS, dl.h "sites"): its range, and
 * from pass 2 its bytes (relocations applied) at tsdata. Written only by dl_link's commit. */
struct tsite {
    u32 addr;
    u16 len, mod;
    u32 data;                            /* offset in L.tsdata */
};
#endif

struct fld {                             /* a +Drive storage field a mod declares */
    u16 mod;
    u16 sym;                             /* its name in the arena */
    u8 scope, bits, flags, pad;
    u32 init;
};

struct dsite {                           /* a site, or a copied block or a claim (data NONE32) */
    u32 lo, hi;
    u32 data;                            /* a site's bytes in sdata */
};
#define NONE32 0xFFFFFFFFu

struct contrib {
    s32 order;
    u16 table;                           /* the descriptor's table index; with DL_COLLECTIONS, DTAB | a
                                          * +Drive table's index once a check round found it, NONE before */
    u16 nent;                            /* entries it adds */
    u16 index;                           /* its place in the mod's contribute list */
#ifdef DL_COLLECTIONS
    u16 tname;                           /* a +Drive table's name in the arena; NONE: a flashed table */
    u16 dlen;                            /* its data's bytes */
#endif
};

#ifdef DL_COLLECTIONS
/* A table a +Drive mod declares (docs/DRIVE.md, "+Drive collections"): placed by the layout after
 * every .bss, its living entries and a zero entry; NAME and NAME_n are its mod's global names. */
#define DTAB 0x8000u
struct coll {
    u16 mod;
    u16 name;                            /* in the arena */
    u16 entry;
    u16 count;                           /* its living entries, from the layout */
    u32 addr;                            /* from the layout */
};
#endif

struct entry {                           /* pass 2: one contribution's bytes */
    u16 mod, ctr;
    u16 data;                            /* offset in edata */
    u16 rel_first, rel_n;
};

struct erel {
    u16 off;
    u8 type, pad;
    u32 value;                           /* S + A */
};

struct item {                            /* one entry of a table being merged */
    s32 order;
    const char *id;
    const u8 *data;
    u16 seq;
    u16 entry;                           /* NONE for a flashed entry, else its entry */
    u16 sub;                             /* which entry of that contribution */
};

static struct {
    u8 *buf;
    u32 max;
    const u8 *d;                         /* the descriptor */
    u32 d_addr;
    u32 pool;                            /* its string pool's offset */
    u32 area_lo, area_hi;
    int nmods;
    struct dmod *mods;                   /* n of them */
    u16 *order;                          /* mods by id */
    u16 *place;                          /* the living mods in placement order (layout) */
    int nplace;
    u32 runs_end;                        /* the layout's .run end, aligned 4: .bss starts here */
    u32 buf_at;                          /* the buffer's address when it is in the area, else 0 */
    u32 read_hi;                         /* the highest end a read into the area reached */
    u32 arena_n, lists_n, nsyms, nlocals, nctr, nent, edata_n, nerel, nsite, sdata_n;
    u32 flashed_syms;
    /* the pools' sizes (dl.h, DL_POOL_*; the merge's from the descriptor) */
    u32 ARENA, LISTS, SYMS, LOCALS, CONTRIBS, EDATA, ERELOCS, MERGE, MDATA, SITES, SDATA;
    u32 main_hi;                         /* the main OS a site may change ends here; 0: no site map */
    u32 scan_arg;                        /* pass 1's refusal argument (DL_R_CLASH) */
    struct dsite *site;
    u8 *sdata;
    char *arena;
    u16 *lists;
    struct sym *syms;
    u16 *sym_next;                       /* the symbols' buckets: chains in insertion order */
    u16 *sym_head;
    u32 sym_mask;
    struct local *locals;
    struct contrib *ctr;
    struct entry *ent;
    u8 *edata;
    struct erel *erel;
    struct item *items;
    u8 *mdata;
    /* +Drive storage (dstore.h): the flashed build's side, the fields, their descriptors */
    int store;                           /* the build keeps +Drive storage */
    struct ds_base sb;
    u32 sb_anc[DS_MAX_IDS + 1];
    int t_ids, t_codes, t_ccs, t_inits, t_handles;   /* their tables' indexes */
    u32 gens_at;                         /* core_store_gens */
    u32 dsts_at;                         /* core_lfo_dsts (core-ar2 0.14), 0 without */
    struct fld *fld;
    u32 nfld;
    u32 *fdesc;                          /* each field's descriptor, once placed */
#ifdef DL_COLLECTIONS
    struct coll *coll;                   /* the +Drive tables of every file pass 1 read */
    u32 ncoll, COLLS;
#endif
    struct ds_want *want;                /* the living mods' fields, for dstore */
    u16 *want_fld;
    struct dl_status *st;                /* where the records go: dl_status, or a check's */
    char (*det)[32];
#ifdef DL_SITES
    struct tsite *sites;
    u8 *tsdata;
    u32 nsites, tsdata_n;
#endif
#ifdef DL_REGIONS
    struct { u32 lo, hi; } regs[DL_REGIONS_MAX];   /* the mods' regions, [lo, hi) */
    u32 nregs;
#endif
#ifdef DL_KNOBS
    int knobs;                           /* the build gives +Drive mods knob ids (F_KNOBS) */
    struct kfix *kfix, *kflash;          /* the +Drive mods' fixed ids; the flashed modules' */
    u32 nkfix, nkflash, KFIX;
    struct ds_state *kprev;              /* /mods/.store's runs (ds_decode), 0 for none */
    struct ds_knob *krun, *kfree;        /* knob_runs' runs and freed runs */
    u32 nkrun, nkfree;
    struct ds_knobset kset;              /* what ds_allocate writes (ds_knobs_in) */
#endif
    u32 crc_table[256];
} L;

#ifdef DL_SITES
volatile u32 dl_trace;                   /* where a link got to, for a debugger: 1 begin, 2 pass 1
                                         * done, 3 checks, 4 layout, 0x100|i placing mod i,
                                         * 0x200|i placed, 5 bind */
#define TRACE(v) (dl_trace = (v))
#else
#define TRACE(v) ((void)0)
#endif
struct dl_status *dl_status;
char (*dl_detail)[32];
u32 dl_status_n;
u8 dl_skip_reason;
u32 *dl_inits;
u32 dl_inits_n;
const u8 *dl_store_prev;
u32 dl_store_prev_n;
struct ds_out dl_store_out;
u32 dl_used_lo, dl_used_hi;
u8 dl_stage_top;
struct dl_site *dl_sites;
u8 *dl_site_orig;
u32 dl_applied_n;

/* ---- small helpers ---------------------------------------------------------------------- */
#ifdef DL_SITES
/* Out of line: inlined into a loop, GCC can make `move.b (a0)+,(0,a0,a1)` of it, the same
 * register post-incremented as the source and indexing the destination, which octabam's
 * ColdFire emulator reads with the incremented a0 (every byte one address late). */
__attribute__((noinline))
#endif
void dl_memcpy(void *d, const void *s, u32 n)
{
    u8 *a = d;
    const u8 *b = s;
    while (n--)
        *a++ = *b++;
}

void dl_memset(void *d, int c, u32 n)
{
    u8 *a = d;
    while (n--)
        *a++ = (u8)c;
}

int dl_strcmp(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return (u8)*a - (u8)*b;
}

u32 dl_strlen(const char *s)
{
    u32 n = 0;
    while (s[n])
        n++;
    return n;
}

u32 dl_hash(const char *s, u32 n)
{
    u32 h = 0x811c9dc5;
    while (n--)
        h = (h ^ (u8)*s++) * 0x01000193;
    return h;
}

/* The CRC-32 register c carried over n more bytes (no inversions, no breath): a file's CRC
 * taken a stretch at a time as it is read (the MODS page's reads, one stretch a tick). */
u32 dl_crc32_add(u32 c, const u8 *p, u32 n)
{
    const u8 *e = p + n;
    if (!L.crc_table[1]) {
        u32 i, k, v;
        for (i = 0; i < 256; i++) {
            for (v = i, k = 0; k < 8; k++)
                v = v & 1 ? 0xEDB88320 ^ (v >> 1) : v >> 1;
            L.crc_table[i] = v;
        }
    }
    for (; p + 4 <= e; p += 4) {         /* four bytes a turn */
        c = L.crc_table[(c ^ p[0]) & 0xFF] ^ (c >> 8);
        c = L.crc_table[(c ^ p[1]) & 0xFF] ^ (c >> 8);
        c = L.crc_table[(c ^ p[2]) & 0xFF] ^ (c >> 8);
        c = L.crc_table[(c ^ p[3]) & 0xFF] ^ (c >> 8);
    }
    while (p < e)
        c = L.crc_table[(c ^ *p++) & 0xFF] ^ (c >> 8);
    return c;
}

u32 dl_crc32(const u8 *p, u32 n)
{
    u32 c = 0xFFFFFFFF;
    while (n) {                          /* 32 KB stretches, a breath between */
        u32 k = n > DL_BREATHE ? DL_BREATHE : n;
        c = dl_crc32_add(c, p, k);
        p += k;
        n -= k;
        if (n)
            dl_breathe();
    }
    return ~c;
}

static const char *pool(u32 off) { return (const char *)L.d + L.pool + off; }
static const char *astr(u16 off) { return off == NONE ? "" : L.arena + off; }

/* A raw JSON string -> the arena (decoded). -> its offset, or NONE when full or bad. */
static u16 keep(const char *s, u32 n)
{
    s32 k;
    if (L.arena_n >= L.ARENA)
        return NONE;
    k = js_unescape(s, n, L.arena + L.arena_n, L.ARENA - L.arena_n);
    if (k < 0)
        return NONE;
    L.arena_n += k + 1;
    return (u16)(L.arena_n - k - 1);
}

static void set_status(struct dmod *m, int state, int code, u32 arg, const char *detail)
{
    struct dl_status *s = &L.st[m->choice];
    u32 i;
    s->state = (u8)state;
    s->code = (u8)code;
    s->arg = (u16)(arg > 0xFFFF ? 0xFFFF : arg);
    if (detail) {
        for (i = 0; i < 31 && detail[i]; i++)
            L.det[m->choice][i] = detail[i];
        L.det[m->choice][i] = 0;
    }
}

static void refuse(struct dmod *m, int code, u32 arg, const char *detail)
{
    m->alive = 0;
    set_status(m, DL_ST_REFUSED, code, arg, detail);
}

/* ---- the descriptor -------------------------------------------------------------------- */
#define D16(o) dl_rd16(L.d + (o))
#define D32(o) dl_rd32(L.d + (o))
static u32 n_modules(void) { return D16(0x20); }
static u32 n_exports(void) { return D16(0x22); }
static u32 n_tables(void) { return D16(0x24); }
static u32 n_claims(void) { return D16(0x26); }
static u32 o_module(u32 i) { return D32(0x28) + 16 * i; }
static u32 o_export(u32 i) { return D32(0x2c) + 8 * i; }
static u32 o_table(u32 i) { return D32(0x30) + 24 * i; }
static u32 o_claim(u32 i) { return D32(0x34) + 8 * i; }
static const char *module_name(u32 i) { return pool(D32(o_module(i))); }
static const char *table_name(u32 i) { return pool(D32(o_table(i))); }
static u32 table_addr(u32 i) { return D32(o_table(i) + 4); }
static u32 table_entry(u32 i) { return D16(o_table(i) + 8); }
static u32 table_count(u32 i) { return D16(o_table(i) + 10); }
static u32 table_cap(u32 i) { return D16(o_table(i) + 12); }
static u32 table_flags(u32 i) { return D16(o_table(i) + 14); }
static u32 table_key(u32 i, u32 k) { return D32(D32(o_table(i) + 16) + 4 * k); }
/* ABI 1.2: the site map (12 a range: lo, hi, kind, module) and the pre-link ranges */
static u32 n_map(void) { return D16(4 + 2) >= 2 ? D16(0x50) : 0; }
static u32 o_map(u32 i) { return D32(0x4c) + 12 * i; }
static u32 n_pre(void) { return D16(4 + 2) >= 2 ? D16(0x52) : 0; }
static u32 o_pre(u32 i) { return D32(0x54) + 12 * i; }
/* ABI 1.3: the flags; bit 5, F_CLAIMS: contributions' claims are taken, and the site map has the
 * flashed ones (kind 7) */
static u32 d_flags(void) { return D16(4 + 2) >= 3 ? D32(0x60) : 0; }
#define F_CLAIMS 32
#define F_KNOBS 128                      /* bit 7: +Drive mods' knob ids (knob_start) */
#define MAP_CLAIM 7
/* An entry's order: ABI 1.1's orders array, else (1.0) the key's, 0xFFFF the linker's own */
static s32 table_order(u32 i, u32 k)
{
    u32 o = D32(o_table(i) + 20), key;
    if (o)
        return (s32)D32(o + 4 * k);
    key = table_key(i, k) >> 16;
    return key == 0xFFFF ? 0x7FFFFFFF : (s32)key;
}
#define F_MORE 1
#define F_DSP 4

static int find_table(const char *s, u32 n)
{
    u32 i;
    for (i = 0; i < n_tables(); i++)
        if (js_eq(s, n, table_name(i)))
            return (int)i;
    return -1;
}

/* The table the core's std map sends standard event s (n bytes) to: the descriptor's claims of
 * kind 3, "<event> <table>" (desc.py CLAIM_STD) -> the table's name, or 0 where it maps none */
static const char *std_table(const char *s, u32 n)
{
    u32 i, q;
    for (i = 0; i < n_claims(); i++) {
        const char *p = pool(D32(o_claim(i) + 4));
        if (D16(o_claim(i)) != 3)
            continue;
        for (q = 0; q < n && p[q] && p[q] == s[q]; q++)
            ;
        if (q == n && p[q] == ' ')
            return p + q + 1;
    }
    return 0;
}

/* A contribution's entry size: its table's. */
static u32 ctr_entry(const struct contrib *c)
{
#ifdef DL_COLLECTIONS
    if (c->table == NONE)
        return 0;
    if (c->table & DTAB)
        return L.coll[c->table & ~DTAB].entry;
#endif
    return table_entry(c->table);
}

static int flashed_module(const char *name)
{
    u32 i;
    for (i = 0; i < n_modules(); i++)
        if (!dl_strcmp(module_name(i), name))
            return (int)i;
    return -1;
}

/* ---- symbols ---------------------------------------------------------------------------- */
static int add_sym(u32 hash, u16 owner, u8 sec, u32 val)
{
    if (L.nsyms >= L.SYMS)
        return 0;
    L.syms[L.nsyms].name = NONE;
    L.syms[L.nsyms].hash = hash;
    L.syms[L.nsyms].owner = owner;
    L.syms[L.nsyms].sec = sec;
    L.syms[L.nsyms].val = val;
    L.nsyms++;
    return 1;
}

/* The address a global symbol has now (a +Drive mod's after the layout). */
static u32 sym_value(const struct sym *s)
{
    const struct dmod *m;
    switch (s->sec) {
    case SEC_ABS:
        return s->val;
    case SEC_TABLE:
        return table_addr(s->val);
    case SEC_STORE:                      /* a storage field: its descriptor */
        return L.fdesc ? L.fdesc[s->val] : 0;
#ifdef DL_COLLECTIONS
    case SEC_DTABLE:                     /* a +Drive table: its address, from the layout */
        return L.coll[s->val].addr;
    case SEC_DCOUNT:                     /* its NAME_n: its living entries */
        return L.coll[s->val].count;
#endif
    case SEC_COUNT: {                    /* NAME_n: the count after the merge */
        u32 n = table_count(s->val), i, k;
        for (i = 0; i < (u32)L.nmods; i++)
            if (L.mods[i].alive)
                for (k = 0; k < L.mods[i].ctr_n; k++)
                    if (L.ctr[L.mods[i].ctr_first + k].table == s->val)
                        n += L.ctr[L.mods[i].ctr_first + k].nent;
        return n;
    }
    }
    m = &L.mods[s->owner];
    return (s->sec == SEC_RUN ? m->run_base : m->bss_base) + s->val;
}

/* The first symbol in a hash's bucket (NONE: none); each chain is in insertion order. */
static u32 sym_first(u32 hash) { return L.sym_head[hash & L.sym_mask]; }

/* After pass 1, the only time symbols are added or given back: each symbol into its
 * bucket, insertion order kept in each chain, so a lookup sees what a scan in order would. */
static void hash_syms(void)
{
    u32 i;
    for (i = 0; i <= L.sym_mask; i++)
        L.sym_head[i] = NONE;
    for (i = L.nsyms; i-- > 0;) {
        u32 b = L.syms[i].hash & L.sym_mask;
        L.sym_next[i] = L.sym_head[b];
        L.sym_head[b] = (u16)i;
    }
}

/* A global name, among the flashed build's and the living +Drive mods' (flashed first). */
static const struct sym *global(u32 hash)
{
    u32 i;
    for (i = sym_first(hash); i != NONE; i = L.sym_next[i])
        if (L.syms[i].hash == hash && (L.syms[i].owner == FLASHED || L.mods[L.syms[i].owner].alive))
            return &L.syms[i];
    return 0;
}

static int in_list(const struct list *l, const char *s)
{
    u32 i;
    for (i = 0; i < l->n; i++)
        if (!dl_strcmp(L.arena + L.lists[l->first + i], s))
            return 1;
    return 0;
}

/* ---- versions, as elemod.version_key compares them ------------------------------------- */
static int isdig(int c) { return c >= '0' && c <= '9'; }

static int version_cmp(const char *a, const char *b)
{
    while (*a && *b) {
        int da = isdig(*a), db = isdig(*b);
        if (da != db)
            return da ? -1 : 1;          /* digits sort before text */
        if (da) {
            const char *ea = a, *eb = b;
            u32 la, lb;
            while (*a == '0' && isdig(a[1]))
                a++;
            while (*b == '0' && isdig(b[1]))
                b++;
            for (ea = a; isdig(*ea); ea++)
                ;
            for (eb = b; isdig(*eb); eb++)
                ;
            la = ea - a;
            lb = eb - b;
            if (la != lb)
                return la < lb ? -1 : 1;
            for (; a < ea; a++, b++)
                if (*a != *b)
                    return *a < *b ? -1 : 1;
        } else {
            const char *ea = a, *eb = b;
            while (*ea && !isdig(*ea))
                ea++;
            while (*eb && !isdig(*eb))
                eb++;
            while (a < ea && b < eb && *a == *b)
                a++, b++;
            if (a < ea || b < eb) {
                if (a == ea)
                    return -1;
                if (b == eb)
                    return 1;
                return (u8)*a < (u8)*b ? -1 : 1;
            }
        }
    }
    return *a ? 1 : *b ? -1 : 0;
}

/* ---- pass 1 ----------------------------------------------------------------------------- */
/* The top-level values pass 1 and pass 2 read, found once per file. */
enum { K_ELEMOD, K_ID, K_VERSION, K_TARGET, K_SECTIONS, K_SYMBOLS, K_EXPORTS, K_IMPORTS, K_WEAK,
       K_RELOCS, K_SITES, K_COLLECTIONS, K_CONTRIBUTE, K_COPIED, K_STORAGE, K_SHARC,
       K_RESOURCES, K_REQUIRES, K_CONFLICTS, K_EVENTS, K_KNOBS, K_N };
static const char *const KEYS[K_N] = {
    "elemod", "id", "version", "target", "sections", "symbols", "exports", "imports", "weak",
    "relocs", "sites", "collections", "contribute", "copied", "storage", "sharc",
    "resources", "requires", "conflicts", "events", "knobs",
};

static int top(const u8 *buf, u32 len, const char **at)
{
    js_t j;
    const char *s;
    u32 n, k;
    int first = 1;
    for (k = 0; k < K_N; k++)
        at[k] = 0;
    js_init(&j, (const char *)buf, len);
    if (!js_open(&j, '{'))
        return 0;
    while (js_next(&j, '}', &first)) {
        if (!js_key(&j, &s, &n))
            return 0;
        for (k = 0; k < K_N; k++)
            if (js_eq(s, n, KEYS[k]))
                break;
        js_peek(&j);
        if (k < K_N)
            at[k] = j.p;
        if (!js_skip(&j))
            return 0;
    }
    if (j.err)
        return 0;
    while (j.p < j.end && (*j.p == ' ' || *j.p == '\n' || *j.p == '\r' || *j.p == '\t'))
        j.p++;
    return j.p == j.end;
}

static void at(js_t *j, const char *p, const u8 *buf, u32 len)
{
    js_init(j, p, (const char *)buf + len - p);
}

/* Is a value empty: absent, null, [], {}, "" or false? (Python's falsiness for a field.) */
static int empty(const char *p, const u8 *buf, u32 len)
{
    js_t j;
    int first = 1;
    if (!p)
        return 1;
    at(&j, p, buf, len);
    switch (js_peek(&j)) {
    case 'n':
        return 1;
    case 'f':
        return 1;
    case '[':
        js_open(&j, '[');
        return !js_next(&j, ']', &first) && !j.err;
    case '{':
        js_open(&j, '{');
        return !js_next(&j, '}', &first) && !j.err;
    case '"':
        return p[1] == '"';
    case '0': {
        s32 v;
        return js_int(&j, &v) && v == 0;
    }
    }
    return 0;
}

/* A list of strings -> the arena. */
static int read_list(const char *p, const u8 *buf, u32 len, struct list *l)
{
    js_t j;
    const char *s;
    u32 n;
    u16 o;
    int first = 1;
    l->first = (u16)L.lists_n;
    l->n = 0;
    if (!p)
        return 1;
    at(&j, p, buf, len);
    if (!js_open(&j, '['))
        return 0;
    while (js_next(&j, ']', &first)) {
        if (!js_str(&j, &s, &n) || (o = keep(s, n)) == NONE || L.lists_n >= L.LISTS)
            return 0;
        L.lists[L.lists_n++] = o;
        l->n++;
    }
    return !j.err;
}

#ifdef DL_SITES
static void stock_copy(u8 *dst, u32 a, u32 n);
#endif
static void shadow_over(u8 *dst, u32 a, u32 n);

/* A section's parts -> their length (and, with dst, their bytes). */
static int parts(js_t *j, u8 *dst, u32 max, u32 *len)
{
    int first = 1, f2;
    const char *s;
    u32 n, total = 0;
    if (!js_open(j, '['))
        return 0;
    while (js_next(j, ']', &first)) {
        if (!js_open(j, '[') || !js_str(j, &s, &n))
            return 0;
        f2 = 0;
        if (js_eq(s, n, "hex")) {
            if (!js_next(j, ']', &f2) || !js_hex(j, dst ? dst + total : 0, max - total, &n))
                return 0;
            total += n;
        } else if (js_eq(s, n, "stock")) {
            s32 a, k;
            if (!js_next(j, ']', &f2) || !js_int(j, &a) || !js_next(j, ']', &f2) || !js_int(j, &k)
                || k <= 0 || (dst && (u32)k > max - total))
                return 0;
            if (dst)
#ifdef DL_SITES
                stock_copy(dst + total, (u32)a, (u32)k);
#else
            {
                dl_memcpy(dst + total, DL_MEM((u32)a), (u32)k);
                shadow_over(dst + total, (u32)a, (u32)k);
            }
#endif
            total += (u32)k;
        } else {
            return 0;
        }
        if (js_next(j, ']', &f2))
            return 0;
        if (j->err)
            return 0;
    }
    *len = total;
    return !j->err;
}

/* A section's description: {"align", "len", "parts"} or {"align", "size"}. */
static int section(js_t *j, int bss, u32 *size, u32 *align)
{
    const char *s;
    u32 n;
    s32 v, given = -1;
    int first = 1;
    *align = 4;
    *size = 0;
    if (!js_open(j, '{'))
        return 0;
    while (js_next(j, '}', &first)) {
        if (!js_key(j, &s, &n))
            return 0;
        if (js_eq(s, n, "align")) {
            if (!js_int(j, &v) || v < 1 || v > 4096 || (v & (v - 1)))
                return 0;
            *align = (u32)v;
        } else if (bss && js_eq(s, n, "size")) {
            if (!js_int(j, &v) || v < 0)
                return 0;
            *size = (u32)v;
        } else if (!bss && js_eq(s, n, "len")) {
            if (!js_int(j, &given) || given < 0)
                return 0;
        } else if (!bss && js_eq(s, n, "parts")) {
            if (!parts(j, 0, 0, size))
                return 0;
        } else if (!js_skip(j)) {
            return 0;
        }
    }
    if (!bss && given >= 0 && (u32)given != *size)
        return 0;
    return !j->err;
}

#ifdef DL_PAYLOAD_SKIP
/* A .payload the stage skipped (skip.h; docs/DRIVE.md, "Payloads on the +Drive"): {"align",
 * "len", "sha256", "parts"} with "len" and a 64-digit "sha256" before "parts", and "parts" one
 * ["hex", ""] part, the hex the stage left out. Its bytes stay on the +Drive: nothing is placed. */
static int skipped_payload(js_t *j)
{
    const char *s;
    u32 n;
    s32 v;
    int first = 1, f2 = 1, f3 = 1, have_len = 0, have_sha = 0, have_parts = 0;
    if (!js_open(j, '{'))
        return 0;
    while (js_next(j, '}', &first)) {
        if (!js_key(j, &s, &n))
            return 0;
        if (js_eq(s, n, "align")) {
            if (!js_int(j, &v) || v < 1 || v > 4096 || (v & (v - 1)))
                return 0;
        } else if (js_eq(s, n, "len")) {
            if (!js_int(j, &v) || v < 0)
                return 0;
            have_len = 1;
        } else if (js_eq(s, n, "sha256")) {
            if (!js_str(j, &s, &n) || n != 64)
                return 0;
            have_sha = 1;
        } else if (js_eq(s, n, "parts")) {
            if (!have_len || !have_sha || have_parts || !js_open(j, '[') || !js_next(j, ']', &f2)
                || !js_open(j, '[') || !js_next(j, ']', &f3) || !js_str(j, &s, &n) || !js_eq(s, n, "hex")
                || !js_next(j, ']', &f3) || !js_str(j, &s, &n) || n || js_next(j, ']', &f3)
                || js_next(j, ']', &f2))
                return 0;
            have_parts = 1;
        } else if (!js_skip(j)) {
            return 0;
        }
    }
    return have_parts && !j->err;
}

/* Relocations ([[..., "sec:.payload", ...], ...]) that point into a skipped .payload: -> 1 */
static int payload_relocs(const char *p, const u8 *buf, u32 len)
{
    js_t j;
    const char *s;
    u32 n;
    int first = 1, f2;
    if (!p)
        return 0;
    at(&j, p, buf, len);
    if (!js_open(&j, '['))
        return 0;
    while (js_next(&j, ']', &first)) {
        f2 = 1;
        if (!js_open(&j, '['))
            return 0;
        while (js_next(&j, ']', &f2)) {
            if (js_peek(&j) == '"') {
                if (!js_str(&j, &s, &n))
                    return 0;
                if (js_eq(s, n, "sec:.payload"))
                    return 1;
            } else if (!js_skip(&j)) {
                return 0;
            }
        }
    }
    return 0;
}
#endif

static int same(const char *a, const char *b, u32 n)
{
    while (n--)
        if (*a++ != *b++)
            return 0;
    return 1;
}

/* The symbol `name` in a mod's "symbols": -> its section and offset. */
static int find_symbol(const char *p, const u8 *buf, u32 len, const char *name, u32 nlen,
                       u8 *sec, u32 *off)
{
    js_t j;
    const char *s;
    u32 n;
    s32 v;
    int first = 1, f2;
    if (!p)
        return 0;
    at(&j, p, buf, len);
    if (!js_open(&j, '{'))
        return 0;
    while (js_next(&j, '}', &first)) {
        if (!js_key(&j, &s, &n))
            return 0;
        if (n != nlen || !same(s, name, n)) {
            if (!js_skip(&j))
                return 0;
            continue;
        }
        f2 = 1;
        if (!js_open(&j, '[') || !js_next(&j, ']', &f2) || !js_str(&j, &s, &n))
            return 0;
        *sec = js_eq(s, n, ".run") ? SEC_RUN : js_eq(s, n, ".bss") ? SEC_BSS
             : js_eq(s, n, "abs") ? SEC_ABS : 0xFF;
        if (*sec == 0xFF || !js_next(&j, ']', &f2) || !js_int(&j, &v) || js_next(&j, ']', &f2))
            return 0;
        *off = (u32)v;
        return 1;
    }
    return 0;
}

static int target_ok(const char *p, const u8 *buf, u32 len)
{
    js_t j;
    const char *s;
    u32 n;
    int first = 1, dev = 0, os = 0;
    if (!p)
        return 0;
    at(&j, p, buf, len);
    if (!js_open(&j, '{'))
        return 0;
    while (js_next(&j, '}', &first)) {
        if (!js_key(&j, &s, &n))
            return 0;
        if (js_eq(s, n, "device")) {
            if (!js_str(&j, &s, &n))
                return 0;
            dev = js_eq(s, n, pool(D32(0x44)));
        } else if (js_eq(s, n, "os")) {
            if (!js_str(&j, &s, &n))
                return 0;
            os = js_eq(s, n, pool(D32(0x48)));
        } else if (!js_skip(&j)) {
            return 0;
        }
    }
    return dev && os && !j.err;
}

/* ---- +Drive storage fields (store.parse_decls, as the device takes them) ----------------- */
static const char *const SCOPE_NAMES[DS_SCOPES] = {"sound", "kit", "global", "track", "pattern", "project"};

static int c_name(const char *s)
{
    if (!((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z') || *s == '_'))
        return 0;
    for (s++; *s; s++)
        if (!((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9') || *s == '_'))
            return 0;
    return 1;
}

/* a JSON whole number 0..0xFFFFFFFF (not a string, not a fraction) */
static int js_u32(js_t *j, u32 *v)
{
    const char *p;
    u32 x = 0, n = 0;
    if (js_peek(j) != '0' || *j->p == '-')
        return 0;
    for (p = j->p; p < j->end && *p >= '0' && *p <= '9'; p++, n++) {
        if (x > 429496729u || (x == 429496729u && *p > '5'))
            return 0;
        x = x * 10 + (u32)(*p - '0');
    }
    if (!n || (n > 1 && *j->p == '0') || (p < j->end && (*p == '.' || *p == 'e' || *p == 'E')))
        return 0;
    j->p = p;
    *v = x;
    return 1;
}

/* A mod's "storage" -> its fields, each a global name of kind SEC_STORE. -> 0, or DL_R_FORMAT
 * (a field the core can't keep), DL_R_LIMIT (past the loader's room). */
static int storage(struct dmod *m, const char *p, const u8 *buf, u32 len)
{
    js_t j;
    const char *s;
    u32 n, q;
    int first = 1;
    m->fld_first = (u16)L.nfld;
    m->fld_n = 0;
    if (empty(p, buf, len))
        return 0;
    at(&j, p, buf, len);
    if (!js_open(&j, '['))
        return DL_R_FORMAT;
    while (js_next(&j, ']', &first)) {
        int f2 = 1, v, has_smooth = 0, has_init = 0, scope = -1;
        s32 bits = -1;
        u32 init = 0;
        u16 sym = NONE;
        u8 flags = 0, lock = 0, cc = 0, lfo = 0, route = 0, handle = 0;
        struct fld *f;
        if (!js_open(&j, '{'))
            return DL_R_FORMAT;
        while (js_next(&j, '}', &f2)) {
            if (!js_key(&j, &s, &n))
                return DL_R_FORMAT;
            if (js_eq(s, n, "sym")) {
                if (!js_str(&j, &s, &n))
                    return DL_R_FORMAT;
                if ((sym = keep(s, n)) == NONE)
                    return DL_R_LIMIT;
            } else if (js_eq(s, n, "scope")) {
                if (!js_str(&j, &s, &n))
                    return DL_R_FORMAT;
                for (scope = DS_SCOPES - 1; scope >= 0 && !js_eq(s, n, SCOPE_NAMES[scope]); scope--)
                    ;
                if (scope < 0)
                    return DL_R_FORMAT;
            } else if (js_eq(s, n, "bits")) {
                if (!js_int32(&j, &bits))
                    return DL_R_FORMAT;
            } else if (js_eq(s, n, "init")) {
                if (!js_u32(&j, &init))
                    return DL_R_FORMAT;
                has_init = 1;
            } else if (js_eq(s, n, "lock") || js_eq(s, n, "cc") || js_eq(s, n, "lfo") || js_eq(s, n, "smooth")
                       || js_eq(s, n, "mod") || js_eq(s, n, "handle")) {
                char c0 = s[0], c1 = s[1];
                if (!js_bool(&j, &v))
                    return DL_R_FORMAT;
                if (c0 == 'l' && c1 == 'o')
                    lock = (u8)v;
                else if (c0 == 'c')
                    cc = (u8)v;
                else if (c0 == 'l')
                    lfo = (u8)v;
                else if (c0 == 's') {
                    has_smooth = 1;
                    if (!v)
                        flags |= DS_ROUGH;
                } else if (c0 == 'm')
                    route = (u8)v;
                else
                    handle = (u8)v;
            } else {
                return DL_R_FORMAT;      /* "track" too: the Rytm has no CV track */
            }
        }
        if (j.err || sym == NONE || scope < 0 || bits < 1 || bits > DS_MAX_BITS || !c_name(L.arena + sym)
            || dl_strlen(L.arena + sym) > DS_NAME_MAX)
            return DL_R_FORMAT;
        if (((cc || lfo || route || has_smooth) && !lock) || (handle && (lock || init))
            || (lock && scope > 1) || (route && scope == 1) || (has_init && bits < 32 && init >> bits))
            return DL_R_FORMAT;
        for (q = 0; q < m->fld_n; q++)
            if (!dl_strcmp(L.arena + L.fld[m->fld_first + q].sym, L.arena + sym))
                return DL_R_FORMAT;      /* declared twice */
        if (L.nfld >= DS_MAX_FIELDS || L.nsyms >= L.SYMS)
            return DL_R_LIMIT;
        f = &L.fld[L.nfld];
        f->mod = (u16)(m - L.mods);
        f->sym = sym;
        f->scope = (u8)(scope + 1);
        f->bits = (u8)bits;
        f->init = init;
        f->flags = flags | (lock ? DS_LOCK : 0) | (cc ? DS_CC : 0) | (lfo ? DS_LFO : 0) | (route ? DS_ROUTE : 0)
                   | (handle ? DS_HANDLE : 0);
        add_sym(dl_hash(L.arena + sym, dl_strlen(L.arena + sym)), (u16)(m - L.mods), SEC_STORE, L.nfld);
        L.syms[L.nsyms - 1].name = sym;
        L.nfld++;
        m->fld_n++;
        m->exp_n++;                      /* a global name, as an export is */
    }
    return j.err ? DL_R_FORMAT : 0;
}

/* ---- sites ------------------------------------------------------------------------------ */
static int hexval(int c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10
         : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

static int overlap(u32 a, u32 b, u32 lo, u32 hi) { return a < hi && lo < b; }

/* A site's bytes as the running main OS has them before any +Drive site: the memory, with the
 * stock bytes the boot kept for each site it applied (a check after the boot reads through
 * them). */
static void stock_bytes(u8 *dst, u32 addr, u32 len)
{
    u32 i;
    dl_memcpy(dst, DL_MEM(addr), len);
    for (i = 0; i < dl_applied_n; i++) {
        const struct dl_site *a = &dl_sites[i];
        u32 lo = a->addr > addr ? a->addr : addr, hi = a->addr + a->len < addr + len ? a->addr + a->len : addr + len;
        if (lo < hi)
            dl_memcpy(dst + (lo - addr), dl_site_orig + a->orig + (lo - a->addr), hi - lo);
    }
}

/* A refused site's detail: its address as 8 hex digits, and the flashed owner when there is one. */
static const char *site_detail(u32 addr, const char *owner)
{
    static char d[32];
    static const char hx[] = "0123456789abcdef";
    u32 i;
    for (i = 0; i < 8; i++)
        d[i] = hx[addr >> (28 - 4 * i) & 15];
    d[8] = 0;
    if (owner && owner[0]) {
        d[8] = ' ';
        for (i = 0; i < 22 && owner[i]; i++)
            d[9 + i] = owner[i];
        d[9 + i] = 0;
    }
    return d;
}

/* Is the r-th site's stock_sha256 (in "sites" at p) the hash h? */
static int sha_is(const char *p, const u8 *buf, u32 len, u32 r, const u8 *h)
{
    js_t j;
    const char *s;
    u32 n, i, k_ = 0;
    int first = 1;
    at(&j, p, buf, len);
    js_open(&j, '[');
    while (js_next(&j, ']', &first)) {
        int f2 = 1;
        if (k_++ != r) {
            js_skip(&j);
            continue;
        }
        js_open(&j, '{');
        while (js_next(&j, '}', &f2)) {
            js_key(&j, &s, &n);
            if (!js_eq(s, n, "stock_sha256")) {
                js_skip(&j);
                continue;
            }
            js_str(&j, &s, &n);
            for (i = 0; i < 32; i++)
                if ((hexval(s[2 * i]) << 4 | hexval(s[2 * i + 1])) != h[i])
                    return 0;
            return 1;
        }
        return 0;
    }
    return 0;
}

/* Pass 1's part for "sites" and "copied", after the rest of the file: each site's form (addr,
 * len, new, kind, stock_sha256, relocations of the four kinds inside it) and each copied
 * block's (lo < hi, to), inside the main OS; then each site against the flashed build's site
 * map, the pre-link ranges and its stock bytes, and each copied block against the flashed
 * sites. -> 0, or a refusal code (DL_R_CLASH's argument in L.scan_arg). */
static int scan_sites(struct dmod *m, const char **k, const u8 *buf, u32 len, const char **what)
{
    js_t j;
    const char *s;
    u32 n, i, r;
    int first, pass;

    m->site_first = (u16)L.nsite;
    m->site_n = 0;
    m->sdata_first = L.sdata_n;
    if (!L.main_hi)
        return 0;
    for (pass = 0; pass < 2; pass++) {
        const char *p = k[pass ? K_COPIED : K_SITES];
        if (!p || empty(p, buf, len))
            continue;
        at(&j, p, buf, len);
        first = 1;
        if (!js_open(&j, '['))
            return DL_R_FORMAT;
        while (js_next(&j, ']', &first)) {
            int f2 = 1, kind = 0, have_sha = 0;
            s32 addr = 0, n_ = 0, lo = 0, hi = 0, to = 0;
            u32 got = 0, nlen = 0;
            const char *newp = 0, *relp = 0;
            struct dsite *d;
            if (!js_open(&j, '{'))
                return DL_R_FORMAT;
            while (js_next(&j, '}', &f2)) {
                if (!js_key(&j, &s, &n))
                    return DL_R_FORMAT;
                if (!pass && js_eq(s, n, "addr")) {
                    if (!js_int(&j, &addr))
                        return DL_R_FORMAT;
                    got |= 1;
                } else if (!pass && js_eq(s, n, "len")) {
                    if (!js_int(&j, &n_))
                        return DL_R_FORMAT;
                    got |= 2;
                } else if (!pass && js_eq(s, n, "new")) {
                    newp = j.p;
                    if (!js_hex(&j, 0, 0, &nlen))
                        return DL_R_FORMAT;
                } else if (!pass && js_eq(s, n, "kind")) {
                    if (!js_str(&j, &s, &n))
                        return DL_R_FORMAT;
                    kind = js_eq(s, n, "code") || js_eq(s, n, "data");
                } else if (!pass && js_eq(s, n, "stock_sha256")) {
                    if (!js_str(&j, &s, &n) || n != 64)
                        return DL_R_FORMAT;
                    for (i = 0; i < 64; i++)
                        if (hexval(s[i]) < 0)
                            return DL_R_FORMAT;
                    have_sha = 1;
                } else if (!pass && js_eq(s, n, "relocs")) {
                    relp = j.p;
                    if (!js_skip(&j))
                        return DL_R_FORMAT;
                } else if (pass && js_eq(s, n, "lo")) {
                    if (!js_int(&j, &lo))
                        return DL_R_FORMAT;
                    got |= 1;
                } else if (pass && js_eq(s, n, "hi")) {
                    if (!js_int(&j, &hi))
                        return DL_R_FORMAT;
                    got |= 2;
                } else if (pass && js_eq(s, n, "to")) {
                    if (!js_int(&j, &to))
                        return DL_R_FORMAT;
                    got |= 4;
                } else if (!js_skip(&j)) {
                    return DL_R_FORMAT;
                }
            }
            if (j.err)
                return DL_R_FORMAT;
            if (!pass) {                 /* a site */
                if (got != 3 || !newp || !kind || !have_sha || n_ <= 0 || nlen != (u32)n_
                    || (u32)addr < DL_MAIN_LO || (u32)addr >= L.main_hi || (u32)n_ > L.main_hi - (u32)addr)
                    return DL_R_FORMAT;
                lo = addr;
                hi = addr + n_;
                if (relp) {              /* [off, abs32 | pc32 | pc16 | abs16, target, addend] */
                    js_t q;
                    int f3 = 1;
                    at(&q, relp, buf, len);
                    if (!js_open(&q, '['))
                        return DL_R_FORMAT;
                    while (js_next(&q, ']', &f3)) {
                        int f4 = 1, sz;
                        s32 off, add;
                        if (!js_open(&q, '[') || !js_next(&q, ']', &f4) || !js_int(&q, &off)
                            || !js_next(&q, ']', &f4) || !js_str(&q, &s, &n))
                            return DL_R_FORMAT;
                        sz = rsize(s, n);
                        if (!sz || off < 0 || off + sz > n_ || !js_next(&q, ']', &f4) || !js_str(&q, &s, &n)
                            || !js_next(&q, ']', &f4) || !js_int(&q, &add) || js_next(&q, ']', &f4))
                            return DL_R_FORMAT;
                    }
                    if (q.err)
                        return DL_R_FORMAT;
                }
            } else if (got != 7 || lo >= hi || (u32)lo < DL_MAIN_LO || (u32)hi > L.main_hi) {
                return DL_R_FORMAT;      /* a copied block */
            }
            if (L.nsite >= L.SITES || (!pass && L.sdata_n + (u32)n_ > L.SDATA))
                return DL_R_LIMIT;
            d = &L.site[L.nsite++];
            d->lo = (u32)lo;
            d->hi = (u32)hi;
            d->data = pass ? NONE32 : L.sdata_n;
            if (!pass)
                L.sdata_n += (u32)n_;
            m->site_n++;
            (void)to;
        }
        if (j.err)
            return DL_R_FORMAT;
    }

    /* against the flashed build: each site, then each copied block */
    for (r = 0; r < m->site_n; r++) {
        const struct dsite *d = &L.site[m->site_first + r];
        if (d->data == NONE32)
            continue;
        for (i = 0; i < n_map(); i++)
            if (overlap(d->lo, d->hi, D32(o_map(i)), D32(o_map(i) + 4))) {
                u32 kd = D16(o_map(i) + 8), mi = D16(o_map(i) + 10);
                *what = site_detail(d->lo, (kd == 1 || kd == 2 || kd == MAP_CLAIM) && mi < n_modules()
                                    ? module_name(mi) : 0);
                L.scan_arg = DL_SITE_CORE << 8;
                return DL_R_CLASH;
            }
        for (i = 0; i < n_pre(); i++)
            if (overlap(d->lo, d->hi, D32(o_pre(i)), D32(o_pre(i) + 4))) {
                *what = site_detail(d->lo, 0);
                L.scan_arg = DL_SITE_PRELINK << 8;
                return DL_R_CLASH;
            }
    }
    /* the stock bytes: the site's own slot of sdata holds them while the hash runs */
    at(&j, k[K_SITES], buf, len);
    for (r = 0; r < m->site_n; r++) {
        const struct dsite *d = &L.site[m->site_first + r];
        u8 h[32];
        if (d->data == NONE32)
            continue;
        stock_bytes(L.sdata + d->data, d->lo, d->hi - d->lo);
        dl_sha256(L.sdata + d->data, d->hi - d->lo, h);
        if (!sha_is(k[K_SITES], buf, len, r, h)) {
            *what = site_detail(d->lo, 0);
            L.scan_arg = DL_SITE_STOCK << 8;
            return DL_R_CLASH;
        }
    }
    for (r = 0; r < m->site_n; r++) {
        const struct dsite *d = &L.site[m->site_first + r];
        if (d->data != NONE32)
            continue;
        for (i = 0; i < n_map(); i++)
            if (D16(o_map(i) + 8) == 1 && overlap(d->lo, d->hi, D32(o_map(i)), D32(o_map(i) + 4))) {
                u32 mi = D16(o_map(i) + 10);
                *what = site_detail(d->lo, mi < n_modules() ? module_name(mi) : 0);
                L.scan_arg = DL_SITE_CORE << 8;
                return DL_R_CLASH;
            }
    }
    return 0;
}

/* Pass 1's part for the contributions' "claims" (F_CLAIMS), after the sites: each claim's form,
 * [lo, hi] with lo < hi inside the main OS (as a copied block's), kept in L.site after the mod's
 * sites and copied blocks (data NONE32, claim_n of them); then each against the flashed build's
 * site map (every kind, the flashed claims too) and the pre-link ranges. Nothing is written for a
 * claim: it keeps other mods' sites and claims off those bytes. -> 0, or a refusal code. */
static int scan_claims(struct dmod *m, const char *p, const u8 *buf, u32 len, const char **what)
{
    js_t j;
    const char *s;
    u32 n, i, r;
    int first = 1;

    m->claim_n = 0;
    if (!p || !L.main_hi || !(d_flags() & F_CLAIMS))
        return 0;
    at(&j, p, buf, len);
    if (!js_open(&j, '['))
        return DL_R_FORMAT;
    while (js_next(&j, ']', &first)) {
        int f2 = 1;
        if (!js_open(&j, '{'))
            return DL_R_FORMAT;
        while (js_next(&j, '}', &f2)) {
            int f3 = 1;
            if (!js_key(&j, &s, &n))
                return DL_R_FORMAT;
            if (!js_eq(s, n, "claims") || empty(j.p, buf, len)) {
                if (!js_skip(&j))
                    return DL_R_FORMAT;
                continue;
            }
            if (!js_open(&j, '['))
                return DL_R_FORMAT;
            while (js_next(&j, ']', &f3)) {
                int f4 = 1;
                s32 lo, hi;
                struct dsite *d;
                if (!js_open(&j, '[') || !js_next(&j, ']', &f4) || !js_int(&j, &lo)
                    || !js_next(&j, ']', &f4) || !js_int(&j, &hi) || js_next(&j, ']', &f4))
                    return DL_R_FORMAT;
                if ((u32)lo < DL_MAIN_LO || (u32)hi > L.main_hi || (u32)lo >= (u32)hi)
                    return DL_R_FORMAT;
                if (L.nsite >= L.SITES)
                    return DL_R_LIMIT;
                d = &L.site[L.nsite++];
                d->lo = (u32)lo;
                d->hi = (u32)hi;
                d->data = NONE32;
                m->claim_n++;
            }
        }
    }
    if (j.err)
        return DL_R_FORMAT;
    for (r = 0; r < m->claim_n; r++) {
        const struct dsite *d = &L.site[m->site_first + m->site_n + r];
        for (i = 0; i < n_map(); i++)
            if (overlap(d->lo, d->hi, D32(o_map(i)), D32(o_map(i) + 4))) {
                u32 kd = D16(o_map(i) + 8), mi = D16(o_map(i) + 10);
                *what = site_detail(d->lo, (kd == 1 || kd == 2 || kd == MAP_CLAIM) && mi < n_modules()
                                    ? module_name(mi) : 0);
                L.scan_arg = DL_SITE_CORE << 8;
                return DL_R_CLASH;
            }
        for (i = 0; i < n_pre(); i++)
            if (overlap(d->lo, d->hi, D32(o_pre(i)), D32(o_pre(i) + 4))) {
                *what = site_detail(d->lo, 0);
                L.scan_arg = DL_SITE_PRELINK << 8;
                return DL_R_CLASH;
            }
    }
    return 0;
}

/* A mod's range a (its sites, copied blocks, then claims): 1 a site, 0 a copied block, 2 a claim */
static int range_kind(const struct dmod *m, u32 a)
{
    return a >= m->site_n ? 2 : L.site[m->site_first + a].data != NONE32;
}

#if defined(DL_SITES) || defined(DL_REGIONS)
/* An address as a refusal's detail, "0x" and 8 digits. */
static char hexdetail[12];

static const char *hex_of(u32 v)
{
    static const char dg[] = "0123456789abcdef";
    int i;
    hexdetail[0] = '0';
    hexdetail[1] = 'x';
    for (i = 0; i < 8; i++)
        hexdetail[2 + i] = dg[(v >> (28 - 4 * i)) & 15];
    hexdetail[10] = 0;
    return hexdetail;
}

#endif

/* The descriptor's shadow (ABI 1.2: the stock bytes of every range the flashed build wrote)
 * laid over dst, which holds [a, a+n) as the running OS has it. A stock part of a +Drive mod
 * names stock's bytes; where the flashed build wrote over them (a core site), the running OS
 * holds the site's bytes instead, so the build carries the shadow (drive_stock_shadow,
 * route.s) and the copy takes stock's from it. A descriptor without one leaves dst as it is.
 * Without it a mod linked against stock (sidecar 0.7: six parts on core-ar2 0.14's sites)
 * runs the sites' jumps in place of stock's instructions. */
static void shadow_over(u8 *dst, u32 a, u32 n)
{
    u32 o, cnt, i;
    if (dl_rd16(L.d + 6) < 2 || !(o = D32(0x58)))
        return;
    cnt = D32(0x5c);
    for (i = 0; i < cnt; i++) {
        u32 sa = D32(o), sn = D32(o + 4), lo, hi;
        lo = sa > a ? sa : a;
        hi = sa + sn < a + n ? sa + sn : a + n;
        if (lo < hi)
            dl_memcpy(dst + (lo - a), L.d + o + 8 + (lo - sa), hi - lo);
        o += 8 + ((sn + 3) & ~3u);
    }
}

#ifdef DL_SITES
/* ---- sites (the Octatrack's TELOS) ----------------------------------------------------------
 * A mod's "sites": [{"addr", "len", "stock_sha256", "new", "kind", "relocs"}]. Pass 1 checks
 * each against the running OS: its bytes there must still be stock's (sha256), so a site over
 * anything the flashed build wrote is refused (DL_R_STOCK), and records its range for the
 * overlap check. Pass 2 builds its bytes; the commit writes them. */
/* Stock bytes for [a, a+n): the OS image's before the OS ran (dl_stock_read), with the descriptor's shadow (ABI 1.2: the stock
 * bytes of every range the flashed build wrote) laid over them. */
static void stock_copy(u8 *dst, u32 a, u32 n)
{
    dl_stock_read(dst, a, n);            /* the host's: the running OS, or its boot-time copy */
    shadow_over(dst, a, n);
}

static int scan_tsites(struct dmod *m, const char *p, const u8 *buf, u32 len, const char **what)
{
    js_t j;
    const char *s;
    u32 n;
    int first = 1;
    m->tsite_first = (u16)L.nsites;
    m->tsite_n = 0;
    if (!p || empty(p, buf, len))
        return 0;
    at(&j, p, buf, len);
    if (!js_open(&j, '['))
        return DL_R_FORMAT;
    while (js_next(&j, ']', &first)) {
        int f2 = 1, i;
        s32 addr = -1, slen = -1;
        u32 nlen = 0;
        const char *sha = 0;
        u8 want[32], got[32];
        if (!js_open(&j, '{'))
            return DL_R_FORMAT;
        while (js_next(&j, '}', &f2)) {
            if (!js_key(&j, &s, &n))
                return DL_R_FORMAT;
            if (js_eq(s, n, "addr")) {
                if (!js_int(&j, &addr))
                    return DL_R_FORMAT;
            } else if (js_eq(s, n, "len")) {
                if (!js_int(&j, &slen))
                    return DL_R_FORMAT;
            } else if (js_eq(s, n, "stock_sha256")) {
                if (!js_str(&j, &s, &n) || n != 64)
                    return DL_R_FORMAT;
                sha = s;
            } else if (js_eq(s, n, "new")) {
                if (!js_hex(&j, 0, 0, &nlen))
                    return DL_R_FORMAT;
            } else if (!js_skip(&j)) {
                return DL_R_FORMAT;
            }
        }
        if (j.err || addr <= 0 || slen <= 0 || slen > 256 || (u32)slen != nlen || !sha)
            return DL_R_FORMAT;
        if (!dl_site_ok((u32)addr, (u32)slen)) {     /* the host's own rule: the OS image only */
            *what = hex_of((u32)addr);
            return DL_R_STOCK;
        }
#ifdef DL_EARLY
        if (dl_site_early((u32)addr, (u32)slen)) {   /* too late from the card: flash it */
            *what = hex_of((u32)addr);
            return DL_R_EARLY;
        }
#endif
        for (i = 0; i < 32; i++) {
            int hi = hexval(sha[2 * i]), lo = hexval(sha[2 * i + 1]);
            if (hi < 0 || lo < 0)
                return DL_R_FORMAT;
            want[i] = (u8)(hi << 4 | lo);
        }
        {                                /* over the flashed build's own writes: never */
            u32 o = dl_rd16(L.d + 6) >= 2 ? D32(0x58) : 0, c = o ? D32(0x5c) : 0, q;
            for (q = 0; q < c; q++) {
                u32 sa = D32(o), sn = D32(o + 4);
                if ((u32)addr < sa + sn && sa < (u32)addr + (u32)slen) {
                    *what = hex_of((u32)addr);
                    return DL_R_FLASHED;
                }
                o += 8 + ((sn + 3) & ~3u);
            }
        }
        {                                /* stock's bytes, as the OS had them before any link */
            u8 tmp[256];
            stock_copy(tmp, (u32)addr, (u32)slen);
            dl_sha256(tmp, (u32)slen, got);
        }
        for (i = 0; i < 32 && got[i] == want[i]; i++)
            ;
        if (i < 32) {
            *what = hex_of((u32)addr);
            return DL_R_STOCK;
        }
        if (L.nsites >= DL_SITES_MAX)
            return DL_R_LIMIT;
        L.sites[L.nsites].addr = (u32)addr;
        L.sites[L.nsites].len = (u16)slen;
        L.sites[L.nsites].mod = (u16)(m - L.mods);
        L.sites[L.nsites].data = 0;
        L.nsites++;
        m->tsite_n++;
    }
    return j.err ? DL_R_FORMAT : 0;
}

static int resolve(struct dmod *m, const char *s, u32 n, u32 *v);
static int put(u8 *at_, int type, u32 value, u32 p);
static int rtype(const char *s, u32 n);

/* Pass 2: each site's bytes, its relocations applied at its own address. */
static int place_sites(struct dmod *m, const char *p, const u8 *buf, u32 len, const char **what)
{
    js_t j;
    const char *s;
    u32 n, q = 0;
    int first = 1;
    if (!m->tsite_n)
        return 0;
    at(&j, p, buf, len);
    js_open(&j, '[');
    while (js_next(&j, ']', &first)) {
        struct tsite *x = &L.sites[m->tsite_first + q++];
        int f2 = 1;
        u32 got;
        if (L.tsdata_n + x->len > DL_SDATA_MAX)
            return DL_R_LIMIT;
        x->data = L.tsdata_n;
        js_open(&j, '{');
        while (js_next(&j, '}', &f2)) {
            js_key(&j, &s, &n);
            if (js_eq(s, n, "new")) {
                if (!js_hex(&j, L.tsdata + x->data, x->len, &got) || got != x->len)
                    return DL_R_JSON;
            } else if (js_eq(s, n, "relocs")) {
                int f3 = 1;
                js_open(&j, '[');
                while (js_next(&j, ']', &f3)) {
                    int f4 = 1, type;
                    s32 off, add;
                    u32 sv;
                    js_open(&j, '[');
                    js_next(&j, ']', &f4);
                    js_int(&j, &off);
                    js_next(&j, ']', &f4);
                    js_str(&j, &s, &n);
                    if (!rsize(s, n))
                        return DL_R_FORMAT;
                    type = rtype(s, n);
                    js_next(&j, ']', &f4);
                    js_str(&j, &s, &n);
                    if (!resolve(m, s, n, &sv)) {
                        static char nm[32];
                        u32 c = n > 4 ? (n - 4 < 31 ? n - 4 : 31) : 0;
                        dl_memcpy(nm, s + 4, c);
                        nm[c] = 0;
                        *what = nm;
                        return DL_R_IMPORT;
                    }
                    js_next(&j, ']', &f4);
                    js_int(&j, &add);
                    js_next(&j, ']', &f4);
                    if (off < 0 || (u32)off + (type == R_PC16 || type == R_ABS16 ? 2 : 4) > x->len)
                        return DL_R_FORMAT;
                    if (!put(L.tsdata + x->data + off, type, sv + (u32)add, x->addr + (u32)off))
                        return DL_R_RANGE;
                }
            } else {
                js_skip(&j);
            }
        }
        L.tsdata_n += x->len;
    }
    return j.err ? DL_R_JSON : 0;
}

int dl_site_live(u32 i, u32 *addr, u32 *len)
{
    if (i >= L.nsites || !L.mods[L.sites[i].mod].alive)
        return 0;
    *addr = L.sites[i].addr;
    *len = L.sites[i].len;
    return 1;
}

u32 dl_sites_n(void) { return L.nsites; }
#endif

/* Pass 1: read one file into m. -> 0, or a refusal code (detail in *what). */
#ifdef DL_COLLECTIONS
/* A mod's "collections" (docs/DRIVE.md, "+Drive collections"): each table's name (1 to 59 bytes) and
 * entry size (a multiple of 4, at most 0xFFFF) into L.coll, and NAME and NAME_n as its global names,
 * after its exports and storage fields (so the check rounds treat them as exports). A DSP table
 * ("space") is a feature refusal; "spare", "max" and "render" are a flashed build's and ignored. */
static int collections(struct dmod *m, const char *p, const u8 *buf, u32 len, const char **what)
{
    js_t j;
    const char *s;
    u32 n;
    int first = 1;
    m->coll_first = (u16)L.ncoll;
    m->coll_n = 0;
    if (empty(p, buf, len))
        return 0;
    at(&j, p, buf, len);
    if (!js_open(&j, '{'))
        return DL_R_FORMAT;
    while (js_next(&j, '}', &first)) {
        char nm[64];
        s32 k, entry = -1, v;
        int f2 = 1;
        u16 at_;
        struct coll *c;
        if (!js_key(&j, &s, &n) || (k = js_unescape(s, n, nm, 60)) <= 0)
            return DL_R_FORMAT;
        if (!js_open(&j, '{'))
            return DL_R_FORMAT;
        while (js_next(&j, '}', &f2)) {
            const char *ks;
            u32 kn;
            if (!js_key(&j, &ks, &kn))
                return DL_R_FORMAT;
            if (js_eq(ks, kn, "entry")) {
                if (!js_int(&j, &v) || v <= 0 || v > 0xFFFF || (v & 3))
                    return DL_R_FORMAT;
                entry = v;
            } else if (js_eq(ks, kn, "space")) {   /* a DSP table: never on the +Drive */
                *what = "collections";
                return DL_R_FEATURE;
            } else if (!js_skip(&j)) {
                return DL_R_FORMAT;
            }
        }
        if (j.err || entry < 0)
            return DL_R_FORMAT;
        /* its name and NAME_n into the arena, as keep() would put them */
        if (L.ncoll >= L.COLLS || L.nsyms + 2 > L.SYMS || L.arena_n >= L.ARENA
            || L.arena_n + (u32)k + 1 > L.ARENA)
            return DL_R_LIMIT;
        at_ = (u16)L.arena_n;
        dl_memcpy(L.arena + at_, nm, (u32)k);
        L.arena[at_ + k] = 0;
        L.arena_n += (u32)k + 1;
        if (L.arena_n >= L.ARENA || L.arena_n + (u32)k + 3 > L.ARENA)
            return DL_R_LIMIT;
        dl_memcpy(L.arena + L.arena_n, nm, (u32)k);
        L.arena[L.arena_n + k] = '_';
        L.arena[L.arena_n + k + 1] = 'n';
        L.arena[L.arena_n + k + 2] = 0;
        c = &L.coll[L.ncoll];
        c->mod = (u16)(m - L.mods);
        c->name = at_;
        c->entry = (u16)entry;
        c->count = 0;
        c->addr = 0;
        add_sym(dl_hash(nm, (u32)k), c->mod, SEC_DTABLE, L.ncoll);
        L.syms[L.nsyms - 1].name = at_;
        add_sym(dl_hash(L.arena + L.arena_n, (u32)k + 2), c->mod, SEC_DCOUNT, L.ncoll);
        L.syms[L.nsyms - 1].name = (u16)L.arena_n;
        L.arena_n += (u32)k + 3;
        L.ncoll++;
        m->coll_n++;
        m->exp_n += 2;                   /* global names, as exports are */
    }
    return j.err ? DL_R_FORMAT : 0;
}

/* The living mod that declares the +Drive table name: the first in id order. -> its table's
 * index in L.coll, or -1. */
static int declarer(const char *name)
{
    int i;
    u32 q;
    for (i = 0; i < L.nmods; i++) {
        const struct dmod *o = &L.mods[L.order[i]];
        if (!o->alive)
            continue;
        for (q = o->coll_first; q < (u32)o->coll_first + o->coll_n; q++)
            if (!dl_strcmp(L.arena + L.coll[q].name, name))
                return (int)q;
    }
    return -1;
}
#endif

#ifdef DL_KNOBS
static int knob_scan(struct dmod *m, const char **k, const u8 *buf, u32 len);
#endif

static int scan(struct dmod *m, const u8 *buf, u32 len, const char **what)
{
    const char *k[K_N], *s;
    js_t j;
    u32 n, i;
    s32 v;
    int first;
    static const char *const no[] = { "sites", "collections", "copied", "storage", "sharc" };
    static const int nok[] = { K_SITES, K_COLLECTIONS, K_COPIED, K_STORAGE, K_SHARC };

    *what = 0;
    L.scan_arg = 0;
    if (!top(buf, len, k))
        return DL_R_JSON;
    at(&j, k[K_ELEMOD], buf, len);
    if (!k[K_ELEMOD] || !js_int(&j, &v) || v != 2)
        return DL_R_FORMAT;
    if (!k[K_ID] || (at(&j, k[K_ID], buf, len), !js_str(&j, &s, &n)) || (m->id = keep(s, n)) == NONE
        || !L.arena[m->id] || dl_strlen(L.arena + m->id) >= DL_ID_MAX)
        return DL_R_FORMAT;
    for (i = 0; i < DL_ID_MAX; i++)          /* the record names the mod from here on */
        L.st[m->choice].id[i] = i < dl_strlen(L.arena + m->id) ? L.arena[m->id + i] : 0;
    if (!k[K_VERSION] || (at(&j, k[K_VERSION], buf, len), !js_str(&j, &s, &n))
        || (m->version = keep(s, n)) == NONE)
        return DL_R_FORMAT;
    if (!target_ok(k[K_TARGET], buf, len))
        return DL_R_TARGET;
#ifdef DL_KNOBS
    {                                    /* "knobs": 0..DL_KNOBS_MAX; __knobs is the linker's */
        u8 sec;
        u32 off;
        m->knobs = 0;
        m->kfix_first = (u16)L.nkfix;
        m->kfix_n = 0;
        if (k[K_KNOBS]) {
            at(&j, k[K_KNOBS], buf, len);
            if (!js_int(&j, &v) || v < 0 || v > DL_KNOBS_MAX)
                return DL_R_FORMAT;
            m->knobs = (u16)v;
        }
        if (find_symbol(k[K_SYMBOLS], buf, len, "__knobs", 7, &sec, &off))
            return DL_R_FORMAT;
    }
#endif
#ifdef DL_SITES
    for (i = 1; i < sizeof(nok) / sizeof(nok[0]); i++)   /* sites are read below */
#else
    for (i = 0; i < sizeof(nok) / sizeof(nok[0]); i++)
#endif
        if (!(nok[i] == K_STORAGE && L.store) && (!L.main_hi || (nok[i] != K_SITES && nok[i] != K_COPIED))
#ifdef DL_COLLECTIONS
            && nok[i] != K_COLLECTIONS   /* read below (collections()) */
#endif
            && !empty(k[nok[i]], buf, len)) {
            *what = no[i];
            return DL_R_FEATURE;
        }

    /* sections: .run and .bss only */
    if (!k[K_SECTIONS])
        return DL_R_FORMAT;
    at(&j, k[K_SECTIONS], buf, len);
    first = 1;
    if (!js_open(&j, '{'))
        return DL_R_FORMAT;
    m->run_align = m->bss_align = 4;
    while (js_next(&j, '}', &first)) {
        if (!js_key(&j, &s, &n))
            return DL_R_FORMAT;
        if (js_eq(s, n, ".run")) {
            if (!section(&j, 0, &m->run_len, &m->run_align))
                return DL_R_FORMAT;
        } else if (js_eq(s, n, ".bss")) {
            if (!section(&j, 1, &m->bss_len, &m->bss_align))
                return DL_R_FORMAT;
#ifdef DL_PAYLOAD_SKIP
        } else if (js_eq(s, n, ".payload")) {
            if (!skipped_payload(&j)) {  /* bytes the stage didn't skip: a payload it can't keep */
                *what = ".payload";
                return DL_R_FEATURE;
            }
#endif
        } else {
            static char sec[24];
            u32 q = n < 23 ? n : 23;
            dl_memcpy(sec, s, q);
            sec[q] = 0;
            *what = sec;
            return DL_R_FEATURE;
        }
    }

    /* resources: regions refused, names kept as claims, core kept */
    m->core = NONE;
    m->names.first = (u16)L.lists_n;
    m->names.n = 0;
    if (k[K_RESOURCES]) {
        at(&j, k[K_RESOURCES], buf, len);
        first = 1;
        if (!js_open(&j, '{'))
            return DL_R_FORMAT;
        while (js_next(&j, '}', &first)) {
            if (!js_key(&j, &s, &n))
                return DL_R_FORMAT;
            if (js_eq(s, n, "regions")) {
#ifdef DL_REGIONS
                int rf = 1;
                m->reg_first = (u16)L.nregs;
                m->reg_n = 0;
                if (!js_open(&j, '['))
                    return DL_R_FORMAT;
                while (js_next(&j, ']', &rf)) {    /* each {"name", "lo", "hi"} */
                    int of = 1;
                    s32 lo = 0, hi = 0;
                    if (!js_open(&j, '{'))
                        return DL_R_FORMAT;
                    while (js_next(&j, '}', &of)) {
                        const char *ks;
                        u32 kn;
                        if (!js_key(&j, &ks, &kn))
                            return DL_R_FORMAT;
                        if (js_eq(ks, kn, "lo")) {
                            if (!js_int(&j, &lo))
                                return DL_R_FORMAT;
                        } else if (js_eq(ks, kn, "hi")) {
                            if (!js_int(&j, &hi))
                                return DL_R_FORMAT;
                        } else if (!js_skip(&j)) {
                            return DL_R_FORMAT;
                        }
                    }
                    if ((u32)hi <= (u32)lo)
                        return DL_R_FORMAT;
                    if (!dl_region_ok((u32)lo, (u32)hi)) {   /* the host's rule: a free area */
                        *what = hex_of((u32)lo);
                        return DL_R_OUTSIDE;
                    }
                    if (L.nregs >= DL_REGIONS_MAX)
                        return DL_R_LIMIT;
                    L.regs[L.nregs].lo = (u32)lo;
                    L.regs[L.nregs].hi = (u32)hi;
                    L.nregs++;
                    m->reg_n++;
                }
#else
                if (!empty(j.p, buf, len)) {
                    *what = "regions";
                    return DL_R_FEATURE;
                }
                js_skip(&j);
#endif
            } else if (js_eq(s, n, "names")) {
                if (!read_list(j.p, buf, len, &m->names))
                    return DL_R_LIMIT;
                js_skip(&j);
            } else if (js_eq(s, n, "core")) {
                if (js_peek(&j) == 'n') {
                    js_skip(&j);
                } else if (!js_str(&j, &s, &n) || (m->core = keep(s, n)) == NONE) {
                    return DL_R_FORMAT;
                }
            } else if (!js_skip(&j)) {
                return DL_R_FORMAT;
            }
        }
        if (j.err)
            return DL_R_FORMAT;
    }
    if (!read_list(k[K_IMPORTS], buf, len, &m->imports) || !read_list(k[K_WEAK], buf, len, &m->weak)
        || !read_list(k[K_REQUIRES], buf, len, &m->requires)
        || !read_list(k[K_CONFLICTS], buf, len, &m->conflicts))
        return DL_R_LIMIT;

#ifdef DL_PAYLOAD_SKIP
    if (payload_relocs(k[K_RELOCS], buf, len)) {   /* the payload is never in RAM */
        *what = "relocs into .payload";
        return DL_R_FEATURE;
    }
#endif
    /* relocations: in .run, of the four kinds, inside it */
    if (k[K_RELOCS]) {
        at(&j, k[K_RELOCS], buf, len);
        first = 1;
        if (!js_open(&j, '['))
            return DL_R_FORMAT;
        while (js_next(&j, ']', &first)) {
            int f2 = 1, sz;
            if (!js_open(&j, '[') || !js_next(&j, ']', &f2) || !js_str(&j, &s, &n))
                return DL_R_FORMAT;
            if (!js_eq(s, n, ".run")) {
                *what = "relocs";
                return DL_R_FORMAT;
            }
            if (!js_next(&j, ']', &f2) || !js_int(&j, &v) || !js_next(&j, ']', &f2) || !js_str(&j, &s, &n))
                return DL_R_FORMAT;
            sz = rsize(s, n);
            if (!sz || v < 0 || (u32)v + sz > m->run_len)
                return DL_R_FORMAT;
            if (!js_next(&j, ']', &f2) || !js_skip(&j) || !js_next(&j, ']', &f2) || !js_skip(&j)
                || js_next(&j, ']', &f2))
                return DL_R_FORMAT;
        }
        if (j.err)
            return DL_R_FORMAT;
    }

    /* "events": "std1" or none; a contribution with "std": true then names a standard event, whose
     * table is the core's std map's */
    if (k[K_EVENTS]) {
        at(&j, k[K_EVENTS], buf, len);
        if (js_peek(&j) != 'n' && (!js_str(&j, &s, &n) || !js_eq(s, n, "std1")))
            return DL_R_FORMAT;
    }

    /* contributions: to a table that takes more, whole entries, claims only with F_CLAIMS */
    m->ctr_first = (u16)L.nctr;
    m->ctr_n = 0;
    if (k[K_CONTRIBUTE]) {
        u16 index = 0;
        at(&j, k[K_CONTRIBUTE], buf, len);
        first = 1;
        if (!js_open(&j, '['))
            return DL_R_FORMAT;
        while (js_next(&j, ']', &first)) {
            int f2 = 1, t = -1, std = 0;
            s32 order = 50;
            u32 dlen = 0, had_data = 0, to_n = 0;
            const char *to = 0;
            struct contrib *c;
#ifdef DL_COLLECTIONS
            u16 tname = NONE;
#endif
            if (!js_open(&j, '{'))
                return DL_R_FORMAT;
            while (js_next(&j, '}', &f2)) {
                if (!js_key(&j, &s, &n))
                    return DL_R_FORMAT;
                if (js_eq(s, n, "to")) {
                    if (!js_str(&j, &to, &to_n))
                        return DL_R_FORMAT;
                } else if (js_eq(s, n, "std")) {    /* true, or null for none */
                    if (js_peek(&j) == 'n')
                        js_skip(&j);
                    else if (js_peek(&j) != 't' || !js_bool(&j, &std))
                        return DL_R_FORMAT;
                } else if (js_eq(s, n, "order")) {
                    if (!js_int32(&j, &order))
                        return DL_R_FORMAT;
                } else if (js_eq(s, n, "data")) {
                    if (!js_hex(&j, 0, 0, &dlen))
                        return DL_R_FORMAT;
                    had_data = 1;
                } else if (js_eq(s, n, "claims")) {   /* read after the sites (scan_claims) */
                    if (!empty(j.p, buf, len) && (!L.main_hi || !(d_flags() & F_CLAIMS))) {
                        *what = "claims";
                        return DL_R_FEATURE;
                    }
                    js_skip(&j);
#ifdef DL_PAYLOAD_SKIP
                } else if (js_eq(s, n, "relocs") && payload_relocs(j.p, buf, len)) {
                    *what = "relocs into .payload";
                    return DL_R_FEATURE;
#endif
                } else if (!js_skip(&j)) {
                    return DL_R_FORMAT;
                }
            }
            if (j.err)
                return DL_R_FORMAT;
            if (std && (!k[K_EVENTS] || empty(k[K_EVENTS], buf, len)))
                return DL_R_FORMAT;          /* a standard event's subscription needs "events" */
            if (std && to) {                 /* the core's std map (the descriptor's claims of kind 3) */
                static char ev[32];
                const char *tn = std_table(to, to_n);
                if (!tn) {
                    u32 q = to_n < 31 ? to_n : 31;
                    dl_memcpy(ev, to, q);
                    ev[q] = 0;
                    *what = ev;
                    return DL_R_STD;
                }
                to = tn;
                to_n = dl_strlen(tn);
            }
            if (to) {
                t = find_table(to, to_n);
#ifdef DL_COLLECTIONS
                if (t < 0) {             /* a +Drive table's: found in the check rounds */
                    if ((tname = keep(to, to_n)) == NONE)
                        return DL_R_LIMIT;
                    t = (int)NONE;
                } else
#endif
                if (t < 0 || !(table_flags(t) & F_MORE) || (table_flags(t) & F_DSP)) {
                    static char tn[32];
                    u32 q = to_n < 31 ? to_n : 31;
                    dl_memcpy(tn, to, q);
                    tn[q] = 0;
                    *what = tn;
                    return DL_R_TABLE;
                }
            }
            if (t < 0 || !had_data)
                return DL_R_FORMAT;
            if (order == 0x7FFFFFFF)     /* the linker's own entries' */
                return DL_R_FORMAT;
#ifdef DL_COLLECTIONS
            if (tname != NONE) {
                if (!dlen) {
                    *what = astr(tname);
                    return DL_R_TABLE;
                }
            } else
#endif
            if (!dlen || dlen % table_entry(t)) {
                *what = table_name(t);
                return DL_R_TABLE;
            }
            if (L.nctr >= L.CONTRIBS)
                return DL_R_LIMIT;
            c = &L.ctr[L.nctr++];
            c->table = (u16)t;
            c->order = order;
#ifdef DL_COLLECTIONS
            c->tname = tname;
            c->dlen = (u16)dlen;
            c->nent = tname != NONE ? 0 : (u16)(dlen / table_entry(t));
#else
            c->nent = (u16)(dlen / table_entry(t));
#endif
            c->index = index++;
            m->ctr_n++;
        }
        if (j.err)
            return DL_R_FORMAT;
    }

#ifdef DL_SITES
    {
        int code = scan_tsites(m, k[K_SITES], buf, len, what);
        if (code)
            return code;
    }
#endif
    /* exports: each a symbol it defines */
    m->exp_first = (u16)L.nsyms;
    m->exp_n = 0;
    if (k[K_EXPORTS]) {
        at(&j, k[K_EXPORTS], buf, len);
        first = 1;
        if (!js_open(&j, '['))
            return DL_R_FORMAT;
        while (js_next(&j, ']', &first)) {
            u8 sec;
            u32 off;
            if (!js_str(&j, &s, &n) || !find_symbol(k[K_SYMBOLS], buf, len, s, n, &sec, &off))
                return DL_R_FORMAT;
            if (js_eq(s, n, "drive_init"))  /* every mod's own: never a global name */
                continue;
            if (!add_sym(dl_hash(s, n), (u16)(m - L.mods), sec, off))
                return DL_R_LIMIT;
            if ((L.syms[L.nsyms - 1].name = keep(s, n)) == NONE)
                return DL_R_LIMIT;
            m->exp_n++;
        }
        if (j.err)
            return DL_R_FORMAT;
    }
    if (L.store && (i = (u32)storage(m, k[K_STORAGE], buf, len)) != 0)
        return (int)i;
#ifdef DL_COLLECTIONS
    if ((i = (u32)collections(m, k[K_COLLECTIONS], buf, len, what)) != 0)
        return (int)i;
#endif
    {
        int code = scan_sites(m, k, buf, len, what);
        if (!code)
            code = scan_claims(m, k[K_CONTRIBUTE], buf, len, what);
        if (code)
            return code;
    }
#ifdef DL_KNOBS
    if (L.knobs && (i = (u32)knob_scan(m, k, buf, len)) != 0)
        return (int)i;
#endif
    m->init_sec = 0xFF;
    {
        u8 sec;
        u32 off;
        if (find_symbol(k[K_SYMBOLS], buf, len, "drive_init", 10, &sec, &off) && sec != SEC_ABS) {
            m->init_sec = sec;
            m->init_off = off;
        }
    }
    return 0;
}

#ifdef DL_KNOBS
/* ---- knob ids (docs/DRIVE.md, "Knob ids on the +Drive") ----------------------------------------
 * A +Drive mod may declare "knobs": n and gets a run of n ids in DL_KNOB_LO..DL_KNOB_HI, kept from
 * boot to boot in /mods/.store (store.knobs, the lockfile's rules). Ids a mod's entries name in its
 * own bytes follow link.id_problems: none of the core's, an own-ids table's entry's (dl.h
 * DL_KNOB_TABLES) from DL_KNOB_OWN up, and from there up no id taken twice, by a flashed module (claims of kind 4) or a mod. */
#define KNOB_FREED_MAX DS_MAX_KNOBS      /* the freed runs a .store keeps: the last ones */
#define KNOB_RUNS_MAX (DS_MAX_KNOBS + DL_MAX_MODS)
static char *put_num(char *p, u32 v);

/* The knob state for a build with F_KNOBS (ABI 1.3): the flashed modules' ids and the .store's runs.
 * -> 0 when the heap has no room. */
static int knob_start(void)
{
    u32 i, nk = 0;
    ds_knobs_in = 0;
    if (!(d_flags() & F_KNOBS))
        return 1;
    L.knobs = 1;
    for (i = 0; i < n_claims(); i++)
        if (D16(o_claim(i)) == 4)
            nk++;
    L.KFIX = 4 * L.CONTRIBS;
    L.kfix = dl_alloc(L.KFIX * sizeof *L.kfix);
    L.kflash = dl_alloc((nk ? nk : 1) * sizeof *L.kflash);
    L.krun = dl_alloc(KNOB_RUNS_MAX * sizeof *L.krun);
    L.kfree = dl_alloc(2 * DS_MAX_KNOBS * sizeof *L.kfree);
    if (!L.kfix || !L.kflash || !L.krun || !L.kfree)
        return 0;
    for (i = 0; i < n_claims(); i++) {   /* "<first> <last>" in hex */
        const char *p = pool(D32(o_claim(i) + 4));
        struct kfix *f = &L.kflash[L.nkflash];
        u32 q = 0;
        if (D16(o_claim(i)) != 4)
            continue;
        while (p[q] && p[q] != ' ')
            q++;
        js_parse_int(p, q, &f->a);
        p += q + (p[q] == ' ');
        q = 0;
        while (p[q] && p[q] != ' ')
            q++;
        js_parse_int(p, q, &f->b);
        f->who = D16(o_claim(i) + 2);
        f->own_only = 0;
        L.nkflash++;
    }
    if (dl_store_prev)
        L.kprev = ds_decode(dl_store_prev, dl_store_prev_n);   /* 0: none the allocator takes */
    return 1;
}

/* Relocations at p ([[..., type, target, addend], ...]) to __knobs: each abs32 or abs16, its
 * addend below knobs (link.check). -> 0 when one is not. */
static int knob_refs_ok(const char *p, const u8 *buf, u32 len, u32 knobs)
{
    js_t j;
    int first = 1;
    if (!p)
        return 1;
    at(&j, p, buf, len);
    if (!js_open(&j, '['))
        return 1;                        /* its form is checked elsewhere */
    while (js_next(&j, ']', &first)) {
        const char *s, *typ = 0, *tgt = 0;
        u32 n, tn = 0, gn = 0;
        s32 add = 0, x;
        int f2 = 1;
        if (!js_open(&j, '['))
            return 1;
        while (js_next(&j, ']', &f2)) {
            if (js_peek(&j) == '"') {
                if (!js_str(&j, &s, &n))
                    return 1;
                typ = tgt;
                tn = gn;
                tgt = s;
                gn = n;
            } else if (js_int(&j, &x)) {
                add = x;
            } else if (!js_skip(&j)) {
                return 1;
            }
        }
        if (tgt && js_eq(tgt, gn, "sym:__knobs")
            && (!typ || !(js_eq(typ, tn, "abs32") || js_eq(typ, tn, "abs16")) || add < 0 || (u32)add >= knobs))
            return 0;
    }
    return 1;
}

/* The same for each object's "relocs" in a list of objects (sites, contributions). */
static int knob_list_ok(const char *p, const u8 *buf, u32 len, u32 knobs)
{
    js_t j;
    const char *s;
    u32 n;
    int first = 1;
    if (!p)
        return 1;
    at(&j, p, buf, len);
    if (!js_open(&j, '['))
        return 1;
    while (js_next(&j, ']', &first)) {
        int f2 = 1;
        if (!js_open(&j, '{'))
            return 1;
        while (js_next(&j, '}', &f2)) {
            if (!js_key(&j, &s, &n))
                return 1;
            if (js_eq(s, n, "relocs") && !knob_refs_ok(j.p, buf, len, knobs))
                return 0;
            if (!js_skip(&j))
                return 1;
        }
    }
    return 1;
}

/* The value at key `name` of the object at p, or 0. */
static const char *member(const char *p, const u8 *buf, u32 len, const char *name)
{
    js_t j;
    const char *s;
    u32 n;
    int first = 1;
    if (!p)
        return 0;
    at(&j, p, buf, len);
    if (!js_open(&j, '{'))
        return 0;
    while (js_next(&j, '}', &first)) {
        if (!js_key(&j, &s, &n))
            return 0;
        js_peek(&j);
        if (js_eq(s, n, name))
            return j.p;
        if (!js_skip(&j))
            return 0;
    }
    return 0;
}

/* The big-endian long at .run+off from its hex parts (link.Mod2._long without the stock image):
 * -> 1, or 0 past .run, under a relocation or over a stock part. */
static int run_long(const char **k, const char *parts_p, const u8 *buf, u32 len, u32 run_len, s32 off, u32 *v)
{
    js_t j;
    const char *s;
    u32 n, at_ = 0, got = 0;
    u8 b[4];
    int first = 1;
    if (off < 0 || (u32)off + 4 > run_len || !parts_p)
        return 0;
    if (k[K_RELOCS]) {                   /* [".run", off, type, target, addend] */
        at(&j, k[K_RELOCS], buf, len);
        js_open(&j, '[');
        while (js_next(&j, ']', &first)) {
            int f2 = 1;
            s32 o;
            js_open(&j, '[');
            js_next(&j, ']', &f2);
            js_skip(&j);
            js_next(&j, ']', &f2);
            js_int(&j, &o);
            js_next(&j, ']', &f2);
            js_str(&j, &s, &n);
            if (o < off + 4 && off < o + rsize(s, n))
                return 0;
            while (js_next(&j, ']', &f2))
                js_skip(&j);
        }
    }
    at(&j, parts_p, buf, len);
    first = 1;
    if (!js_open(&j, '['))
        return 0;
    while (js_next(&j, ']', &first)) {
        int f2 = 1;
        u32 cnt, lo, hi, q;
        if (!js_open(&j, '[') || !js_next(&j, ']', &f2) || !js_str(&j, &s, &n))
            return 0;
        if (js_eq(s, n, "hex")) {
            if (!js_next(&j, ']', &f2) || !js_str(&j, &s, &n))
                return 0;
            cnt = n / 2;
            lo = (u32)off > at_ ? (u32)off : at_;
            hi = (u32)off + 4 < at_ + cnt ? (u32)off + 4 : at_ + cnt;
            for (q = lo; q < hi; q++)
                b[got++] = (u8)(hexval(s[2 * (q - at_)]) << 4 | hexval(s[2 * (q - at_) + 1]));
        } else {                         /* a stock part: no bytes here */
            s32 a, c;
            if (!js_next(&j, ']', &f2) || !js_int(&j, &a) || !js_next(&j, ']', &f2) || !js_int(&j, &c))
                return 0;
            cnt = (u32)c;
            if ((u32)off < at_ + cnt && at_ < (u32)off + 4)
                return 0;
        }
        while (js_next(&j, ']', &f2))
            js_skip(&j);
        at_ += cnt;
        if (got == 4) {
            *v = dl_rd32(b);
            return 1;
        }
    }
    return 0;
}

/* Pass 1's part for knob ids, on a build with F_KNOBS: the references to __knobs, then the ids the
 * mod's entries name in its own bytes (link.Mod2.id_ranges: the DL_KNOB_TABLES entries point at a
 * pair of longs, the first and last id). -> 0, or a refusal code. */
static int knob_scan(struct dmod *m, const char **k, const u8 *buf, u32 len)
{
    static const struct { const char *name; u8 own_only; } tabs[] = DL_KNOB_TABLES;
    const char *parts_p = member(member(k[K_SECTIONS], buf, len, ".run"), buf, len, "parts");
    u32 t;
    if (!knob_refs_ok(k[K_RELOCS], buf, len, m->knobs) || !knob_list_ok(k[K_SITES], buf, len, m->knobs)
        || !knob_list_ok(k[K_CONTRIBUTE], buf, len, m->knobs))
        return DL_R_FORMAT;
    m->kfix_first = (u16)L.nkfix;
    m->kfix_n = 0;
    if (!k[K_CONTRIBUTE])
        return 0;
    for (t = 0; t < sizeof tabs / sizeof tabs[0]; t++) {
        js_t j;
        const char *s;
        u32 n;
        int first = 1;
        at(&j, k[K_CONTRIBUTE], buf, len);
        js_open(&j, '[');
        while (js_next(&j, ']', &first)) {
            const char *obj, *rel, *to;
            js_t q;
            int f3 = 1;
            js_peek(&j);
            obj = j.p;
            js_skip(&j);
            to = member(obj, buf, len, "to");
            if (!to)
                continue;
            at(&q, to, buf, len);
            if (!js_str(&q, &s, &n) || !js_eq(s, n, tabs[t].name) || !(rel = member(obj, buf, len, "relocs")))
                continue;
            at(&q, rel, buf, len);
            js_open(&q, '[');
            while (js_next(&q, ']', &f3)) {   /* [off, type, target, addend] */
                int f4 = 1;
                s32 off, add;
                const char *ty, *tg;
                u32 tyn, tgn, a, b;
                s32 at_;
                u8 sec;
                u32 so;
                js_open(&q, '[');
                js_next(&q, ']', &f4);
                js_int(&q, &off);
                js_next(&q, ']', &f4);
                js_str(&q, &ty, &tyn);
                js_next(&q, ']', &f4);
                js_str(&q, &tg, &tgn);
                js_next(&q, ']', &f4);
                tyn = js_int(&q, &add) ? tyn : 0;   /* an addend past s32: no entry of ours */
                while (js_next(&q, ']', &f4))
                    js_skip(&q);
                if (off % 4 || !js_eq(ty, tyn, "abs32"))
                    continue;
                if (tgn > 4 && tg[0] == 's' && tg[1] == 'y' && tg[2] == 'm' && tg[3] == ':') {
                    if (!find_symbol(k[K_SYMBOLS], buf, len, tg + 4, tgn - 4, &sec, &so) || sec != SEC_RUN)
                        continue;
                    at_ = (s32)so + add;
                } else if (js_eq(tg, tgn, "sec:.run")) {
                    at_ = add;
                } else {
                    continue;
                }
                if (!run_long(k, parts_p, buf, len, m->run_len, at_, &a)
                    || !run_long(k, parts_p, buf, len, m->run_len, at_ + 4, &b) || (s32)a > (s32)b)
                    continue;
                if (L.nkfix >= L.KFIX)
                    return DL_R_LIMIT;
                L.kfix[L.nkfix].a = (s32)a;
                L.kfix[L.nkfix].b = (s32)b;
                L.kfix[L.nkfix].who = (u16)(m - L.mods);
                L.kfix[L.nkfix].own_only = tabs[t].own_only;
                L.nkfix++;
                m->kfix_n++;
            }
        }
    }
    return 0;
}

/* Python's '%#x' of v */
static char *put_xhex(char *p, s32 v)
{
    static const char dg[] = "0123456789abcdef";
    u32 u = v < 0 ? 0u - (u32)v : (u32)v;
    int i = 28;
    if (v < 0)
        *p++ = '-';
    if (!u) {
        *p++ = '0';
        return p;
    }
    *p++ = '0';
    *p++ = 'x';
    while (!(u >> i))
        i -= 4;
    for (; i >= 0; i -= 4)
        *p++ = dg[u >> i & 15];
    return p;
}

static char knob_why[64];

/* "<a>-<b> are <tail>" in knob_why */
static const char *knob_detail(s32 a, s32 b, const char *tail, s32 own)
{
    char *p = put_xhex(knob_why, a);
    *p++ = '-';
    p = put_xhex(p, b);
    *p++ = ' ';
    *p++ = 'a';
    *p++ = 'r';
    *p++ = 'e';
    *p++ = ' ';
    if (own) {
        const char *w = "below ";
        while (*w)
            *p++ = *w++;
        p = put_xhex(p, own);
    } else {
        u32 q;
        for (q = 0; tail[q] && p < knob_why + 60; q++)
            *p++ = tail[q];
        *p++ = '\'';
        *p++ = 's';
    }
    *p = 0;
    return knob_why;
}

/* link.id_problems for the mod at id order i against the flashed ids and the earlier living mods'.
 * -> a refusal's detail, or 0. */
static const char *knob_problem(int i, const struct dmod *m)
{
    u32 r, x;
    int k;
    for (r = m->kfix_first; r < (u32)m->kfix_first + m->kfix_n; r++) {
        s32 a = L.kfix[r].a, b = L.kfix[r].b;
        if (a <= DL_KNOB_CORE_HI && DL_KNOB_CORE_LO <= b)
            return knob_detail(a, b, "core", 0);
        if (L.kfix[r].own_only && a < DL_KNOB_OWN)
            return knob_detail(a, b, 0, DL_KNOB_OWN);
        if (b < DL_KNOB_OWN)
            continue;
        if (a < DL_KNOB_OWN)
            a = DL_KNOB_OWN;
        for (x = 0; x < L.nkflash; x++) {
            const struct kfix *f = &L.kflash[x];
            if (f->a <= b && a <= f->b)
                return knob_detail(a > f->a ? a : f->a, b < f->b ? b : f->b, module_name(f->who), 0);
        }
        for (k = 0; k < i; k++) {
            const struct dmod *o = &L.mods[L.order[k]];
            if (!o->alive)
                continue;
            for (x = o->kfix_first; x < (u32)o->kfix_first + o->kfix_n; x++) {
                const struct kfix *f = &L.kfix[x];
                if (f->a <= b && a <= f->b)
                    return knob_detail(a > f->a ? a : f->a, b < f->b ? b : f->b, astr(o->id), 0);
            }
        }
    }
    return 0;
}

/* store.knobs' clash: the highest last id of the taken runs [at, at+n-1] meets (fixed: the flashed
 * ids and the living mods' from DL_KNOB_LO up; the runs; the freed runs not of mod), or -1. */
static s32 knob_clash(u32 at_, u32 n, int runs, const char *mod)
{
    s32 hit = -1, lo = (s32)at_, hi = (s32)(at_ + n - 1);
    u32 x;
    int k;
    for (x = 0; x < L.nkflash; x++)
        if (L.kflash[x].b >= DL_KNOB_LO && L.kflash[x].a <= hi && lo <= L.kflash[x].b && L.kflash[x].b > hit)
            hit = L.kflash[x].b;
    for (k = 0; k < L.nmods; k++) {
        const struct dmod *o = &L.mods[k];
        if (!o->alive)
            continue;
        for (x = o->kfix_first; x < (u32)o->kfix_first + o->kfix_n; x++) {
            const struct kfix *f = &L.kfix[x];
            if (f->b >= DL_KNOB_LO && f->a <= hi && lo <= f->b && f->b > hit)
                hit = f->b;
        }
    }
    if (!runs)
        return hit;
    for (x = 0; x < L.nkrun; x++) {
        const struct ds_knob *r = &L.krun[x];
        s32 y = (s32)(r->first + r->count - 1);
        if ((s32)r->first <= hi && lo <= y && y > hit)
            hit = y;
    }
    for (x = 0; x < L.nkfree; x++) {
        const struct ds_knob *r = &L.kfree[x];
        s32 y = (s32)(r->first + r->count - 1);
        if (dl_strcmp(r->mod, mod) && (s32)r->first <= hi && lo <= y && y > hit)
            hit = y;
    }
    return hit;
}

static struct dmod *knob_mod(const char *id)
{
    int k;
    for (k = 0; k < L.nmods; k++) {
        struct dmod *o = &L.mods[L.order[k]];
        if (o->alive && o->knobs && !dl_strcmp(astr(o->id), id))
            return o;
    }
    return 0;
}

/* Each living "knobs" mod's run (store.knobs from the .store's runs, past the flashed ids and the
 * living mods' own, in DL_KNOB_LO..DL_KNOB_HI): m->knob_base, and L.kset for the new .store. A mod
 * with no run left is refused. -> 1 when it refused one. */
static int knob_runs(void)
{
    const struct ds_state *p = L.kprev;
    u32 nxt = p && p->next_knob > DL_KNOB_LO ? p->next_knob : DL_KNOB_LO, x, y;
    int k, want = 0;
    if (!L.knobs)
        return 0;
    ds_knobs_in = 0;
    for (k = 0; k < L.nmods; k++)
        if (L.mods[k].alive && L.mods[k].knobs)
            want = 1;
    L.kset.n = L.kset.nf = 0;
    L.kset.next = p ? p->next_knob : 0;
    L.kset.kn = p ? p->kn : 0;
    L.kset.fkn = p ? p->fkn : 0;
    if (!want && (!p || (!p->nkn && !p->nfkn))) {   /* nothing given, nothing held: as it was */
        ds_knobs_in = &L.kset;
        return 0;
    }
    L.nkrun = L.nkfree = 0;
    if (p)
        for (x = 0; x < p->nfkn; x++)
            L.kfree[L.nkfree++] = p->fkn[x];
    for (x = 0; p && x < p->nkn; x++) {  /* a run stays while its mod asks for as many, clear of fixed */
        const struct ds_knob *r = &p->kn[x];
        const struct dmod *o = knob_mod(r->mod);
        if (o && o->knobs == r->count && knob_clash(r->first, r->count, 0, 0) < 0)
            L.krun[L.nkrun++] = *r;
        else
            L.kfree[L.nkfree++] = *r;
    }
    for (k = 0; k < L.nmods; k++) {      /* the others in id order */
        struct dmod *o = &L.mods[L.order[k]];
        const char *id = astr(o->id);
        u32 n = o->knobs, at_ = 0;
        int found = 0, again;
        if (!o->alive || !n)
            continue;
        for (x = 0; x < L.nkrun && dl_strcmp(L.krun[x].mod, id); x++)
            ;
        if (x < L.nkrun)
            continue;
        for (x = L.nkfree; x-- > 0;)     /* back where it was, the latest first */
            if (!dl_strcmp(L.kfree[x].mod, id) && L.kfree[x].first + n - 1 <= DL_KNOB_HI
                && knob_clash(L.kfree[x].first, n, 1, id) < 0) {
                at_ = L.kfree[x].first;
                found = 1;
                break;
            }
        for (x = y = 0; x < L.nkfree; x++)
            if (dl_strcmp(L.kfree[x].mod, id))
                L.kfree[y++] = L.kfree[x];
        L.nkfree = y;
        for (again = 0; !found && again < 2; again++) {
            s32 hit;
            if (again) {                 /* the never-used ids are gone: the freed runs go out again */
                if (!L.nkfree)
                    break;
                L.nkfree = 0;
            }
            at_ = again ? DL_KNOB_LO : nxt;
            while ((hit = knob_clash(at_, n, 1, id)) >= 0)
                at_ = (u32)hit + 1;
            if (at_ + n - 1 <= DL_KNOB_HI)
                found = 1;
        }
        if (!found) {
            char d[32], *q = d;
            const char *w = "run of ";
            while (*w)
                *q++ = *w++;
            q = put_num(q, n);
            w = ": none free";
            while (*w)
                *q++ = *w++;
            *q = 0;
            refuse(o, DL_R_IDS, 0, d);
            return 1;
        }
        L.krun[L.nkrun].mod = id;
        L.krun[L.nkrun].first = at_;
        L.krun[L.nkrun].count = n;
        L.nkrun++;
        if (at_ + n > nxt)
            nxt = at_ + n;
    }
    for (x = 1; x < L.nkrun; x++) {      /* by first id (an insertion sort) */
        struct ds_knob r = L.krun[x];
        for (y = x; y > 0 && L.krun[y - 1].first > r.first; y--)
            L.krun[y] = L.krun[y - 1];
        L.krun[y] = r;
    }
    for (x = 0; x < L.nkrun + L.nkfree; x++) {
        const struct ds_knob *r = x < L.nkrun ? &L.krun[x] : &L.kfree[x - L.nkrun];
        if (r->first + r->count > nxt)
            nxt = r->first + r->count;
        if (x < L.nkrun)
            knob_mod(r->mod)->knob_base = r->first;
    }
    L.kset.n = L.nkrun;
    L.kset.kn = L.krun;
    L.kset.nf = L.nkfree > KNOB_FREED_MAX ? KNOB_FREED_MAX : L.nkfree;
    L.kset.fkn = L.kfree + (L.nkfree - L.kset.nf);   /* the latest */
    L.kset.next = nxt;
    ds_knobs_in = &L.kset;
    return 0;
}
#endif

/* ---- check -------------------------------------------------------------------------------- */
static struct dmod *by_id(const char *id, struct dmod *self)
{
    int i;
    for (i = 0; i < L.nmods; i++) {
        struct dmod *o = &L.mods[L.order[i]];
        if (o != self && o->alive && !dl_strcmp(astr(o->id), id))
            return o;
    }
    return 0;
}

static int name_claimed(const char *name, const struct dmod *self)
{
    u32 i;
    int k;
    for (i = 0; i < n_claims(); i++)
        if (D16(o_claim(i)) == 1 && !dl_strcmp(pool(D32(o_claim(i) + 4)), name))
            return 1;
    for (k = 0; k < L.nmods; k++) {     /* an earlier living mod in id order */
        const struct dmod *o = &L.mods[L.order[k]];
        if (o == self)
            break;
        if (o->alive && in_list(&o->names, name))
            return 1;
    }
    return 0;
}

#ifdef DL_REGIONS
/* A region of m's that overlaps one the flashed build claims (the descriptor's claims of kind 2,
 * named "<lo> <hi> <name>" in hex) or one of an earlier living mod in id order. -> the other's
 * name (a static buffer), or 0. */
static const char *region_clash(const struct dmod *self)
{
    static char who[32];
    u32 r, i;
    int k;
    for (r = self->reg_first; r < (u32)self->reg_first + self->reg_n; r++) {
        u32 lo = L.regs[r].lo, hi = L.regs[r].hi;
        for (i = 0; i < n_claims(); i++) {
            const char *p = pool(D32(o_claim(i) + 4));
            s32 a = 0, b = 0;
            u32 q = 0;
            if (D16(o_claim(i)) != 2)
                continue;
            while (p[q] && p[q] != ' ')
                q++;
            js_parse_int(p, q, &a);
            p += q + (p[q] == ' ');
            q = 0;
            while (p[q] && p[q] != ' ')
                q++;
            js_parse_int(p, q, &b);
            if (lo < (u32)b && (u32)a < hi)
                return module_name(D16(o_claim(i) + 2));
        }
        for (k = 0; k < L.nmods; k++) {
            const struct dmod *o = &L.mods[L.order[k]];
            u32 x;
            if (o == self)
                break;
            if (!o->alive)
                continue;
            for (x = o->reg_first; x < (u32)o->reg_first + o->reg_n; x++)
                if (lo < L.regs[x].hi && L.regs[x].lo < hi) {
                    u32 n = dl_strlen(astr(o->id));
                    dl_memcpy(who, astr(o->id), n < 31 ? n : 31);
                    who[n < 31 ? n : 31] = 0;
                    return who;
                }
        }
    }
    return 0;
}
#endif

static int known_global(const char *name)
{
    return global(dl_hash(name, dl_strlen(name))) != 0;
}

/* One round over the living mods in id order. -> how many it refused. */
static int check_round(void)
{
    int i, k, refused = 0;
    u32 q, t;
    for (i = 0; i < L.nmods; i++) {
        struct dmod *m = &L.mods[L.order[i]];
        const char *id = astr(m->id);
        if (!m->alive)
            continue;
        if (flashed_module(id) >= 0) {
            refuse(m, DL_R_DUPLICATE, 0, id);
            refused++;
            continue;
        }
        for (k = 0; k < i; k++) {
            struct dmod *o = &L.mods[L.order[k]];
            if (o->alive && !dl_strcmp(astr(o->id), id))
                break;
        }
        if (k < i) {
            refuse(m, DL_R_DUPLICATE, 0, id);
            refused++;
            continue;
        }
        if (m->core != NONE && version_cmp(pool(D32(o_module(0) + 4)), astr(m->core)) < 0) {
            refuse(m, DL_R_CORE, 0, astr(m->core));
            refused++;
            continue;
        }
        for (q = 0; q < m->requires.n; q++) {
            const char *r = L.arena + L.lists[m->requires.first + q];
            if (flashed_module(r) < 0 && !by_id(r, m))
                break;
        }
        if (q < m->requires.n) {
            refuse(m, DL_R_REQUIRES, 0, L.arena + L.lists[m->requires.first + q]);
            refused++;
            continue;
        }
        {                                /* conflicts, both ways: the later of a pair goes */
            const char *with = 0;
            for (q = 0; q < m->conflicts.n && !with; q++) {
                const char *c = L.arena + L.lists[m->conflicts.first + q];
                if (flashed_module(c) >= 0)
                    with = c;
            }
            for (k = 0; k < i && !with; k++) {
                struct dmod *o = &L.mods[L.order[k]];
                if (o->alive && (in_list(&m->conflicts, astr(o->id)) || in_list(&o->conflicts, id)))
                    with = astr(o->id);
            }
            if (with) {
                refuse(m, DL_R_CONFLICTS, 0, with);
                refused++;
                continue;
            }
        }
#ifdef DL_SITES
        {                                /* sites: the later of two that overlap goes */
            const char *with = 0;
            u32 a, b;
            for (a = 0; a < m->tsite_n && !with; a++) {
                const struct tsite *x = &L.sites[m->tsite_first + a];
                for (k = 0; k < i && !with; k++) {
                    struct dmod *o = &L.mods[L.order[k]];
                    if (!o->alive)
                        continue;
                    for (b = 0; b < o->tsite_n; b++) {
                        const struct tsite *y = &L.sites[o->tsite_first + b];
                        if (x->addr < y->addr + y->len && y->addr < x->addr + x->len) {
                            with = astr(o->id);
                            break;
                        }
                    }
                }
            }
            if (with) {
                refuse(m, DL_R_SITE, 0, with);
                refused++;
                continue;
            }
        }
#endif
        {                                /* exports: names the build and earlier mods lack */
            u32 e, x;
            for (e = 0; e < m->exp_n; e++) {
                const struct sym *s = &L.syms[m->exp_first + e];
                for (x = sym_first(s->hash); x != NONE; x = L.sym_next[x]) {
                    const struct sym *g = &L.syms[x];
                    if (g->hash != s->hash || g == s)
                        continue;
                    if (g->owner == FLASHED)
                        break;
                    if (g->owner == s->owner || !L.mods[g->owner].alive)
                        continue;
                    for (k = 0; k < i && L.order[k] != g->owner; k++)
                        ;
                    if (k < i)
                        break;
                }
                if (x != NONE)
                    break;
            }
            if (e < m->exp_n) {
                refuse(m, DL_R_EXPORT, 0, astr(L.syms[m->exp_first + e].name));
                refused++;
                continue;
            }
        }
        for (q = 0; q < m->names.n; q++)
            if (name_claimed(L.arena + L.lists[m->names.first + q], m))
                break;
        if (q < m->names.n) {
            refuse(m, DL_R_CLAIM, 0, L.arena + L.lists[m->names.first + q]);
            refused++;
            continue;
        }
#ifdef DL_KNOBS
        if (L.knobs) {                   /* knob ids: link.id_problems' rules */
            const char *why = knob_problem(i, m);
            if (why) {
                refuse(m, DL_R_IDS, 0, why);
                refused++;
                continue;
            }
        }
#endif
#ifdef DL_REGIONS
        {
            const char *who = region_clash(m);
            if (who) {
                refuse(m, DL_R_REGION, 0, who);
                refused++;
                continue;
            }
        }
#endif
        for (q = 0; q < m->imports.n; q++) {
            const char *nm = L.arena + L.lists[m->imports.first + q];
            if (!known_global(nm) && !in_list(&m->weak, nm)
#ifdef DL_KNOBS
                && !(L.knobs && m->knobs && !dl_strcmp(nm, "__knobs"))   /* its run's first id */
#endif
                )
                break;
        }
        if (q < m->imports.n) {
            refuse(m, DL_R_IMPORT, 0, L.arena + L.lists[m->imports.first + q]);
            refused++;
            continue;
        }
#ifdef DL_COLLECTIONS
        for (q = 0; q < m->ctr_n; q++) {     /* +Drive tables: a living mod declares each, of that entry */
            struct contrib *c = &L.ctr[m->ctr_first + q];
            int d;
            if (c->tname == NONE)
                continue;
            d = declarer(L.arena + c->tname);
            if (d < 0 || c->dlen % L.coll[d].entry) {
                c->table = NONE;
                break;
            }
            c->table = (u16)(DTAB | (u32)d);
            c->nent = (u16)(c->dlen / L.coll[d].entry);
        }
        if (q < m->ctr_n) {
            refuse(m, DL_R_TABLE, 0, L.arena + L.ctr[m->ctr_first + q].tname);
            refused++;
            continue;
        }
#endif
        {                                /* sites and claims against earlier living mods' sites,
                                          * copied blocks and claims: a site clashes with any, a
                                          * claim with a claim (copied blocks share bytes with
                                          * copied blocks and claims) */
            u32 a, b;
            const struct dmod *with = 0;
            const struct dsite *x = 0;
            for (a = 0; a < (u32)m->site_n + m->claim_n && !with; a++) {
                int ka = range_kind(m, a);
                x = &L.site[m->site_first + a];
                for (k = 0; k < i && !with; k++) {
                    const struct dmod *o = &L.mods[L.order[k]];
                    if (!o->alive)
                        continue;
                    for (b = 0; b < (u32)o->site_n + o->claim_n; b++) {
                        const struct dsite *y = &L.site[o->site_first + b];
                        int kb = range_kind(o, b);
                        if ((ka == 1 || kb == 1 || (ka == 2 && kb == 2)) && overlap(x->lo, x->hi, y->lo, y->hi)) {
                            with = o;
                            break;
                        }
                    }
                }
            }
            if (with) {
                refuse(m, DL_R_CLASH, (u32)DL_SITE_MOD << 8 | with->choice, site_detail(x->lo, 0));
                refused++;
                continue;
            }
        }
    }
    /* table room: while a table is over, the last living mod (id order) adding to it goes */
    for (t = 0; t < n_tables(); t++) {
        for (;;) {
            u32 total = table_count(t);
            int last = -1;
            for (i = 0; i < L.nmods; i++) {
                struct dmod *m = &L.mods[L.order[i]];
                if (!m->alive)
                    continue;
                for (q = 0; q < m->ctr_n; q++)
                    if (L.ctr[m->ctr_first + q].table == t) {
                        total += L.ctr[m->ctr_first + q].nent;
                        last = i;
                    }
            }
            if (total <= table_cap(t))
                break;
            refuse(&L.mods[L.order[last]], DL_R_FULL, table_cap(t) - table_count(t), table_name(t));
            refused++;
        }
    }
    return refused;
}

/* ---- layout ------------------------------------------------------------------------------- */
static u32 align(u32 v, u32 a) { return (v + a - 1) & ~(a - 1); }

/* Where pass 2 reads a mod's file with the stage at the top: the highest word-aligned
 * address it fits below the area's end. */
static u32 stage_at(const struct dmod *m)
{
    u32 room = L.area_hi - L.area_lo;
    return m->file_len > room ? L.area_lo : (L.area_hi - m->file_len) & ~3u;
}

/* The living mods in placement order: id order, or with the stage at the top, the longest
 * file first, ties in id order (a stable insertion sort). Swapping two neighbours so that the
 * longer file goes first never raises the highest point a file reaches, so this order gives
 * the lowest peak. Exports, tables, sites and inits keep the id order. -> how many. */
static int placement(void)
{
    int i, k, n = 0;
    for (i = 0; i < L.nmods; i++) {
        u16 x = L.order[i];
        if (!L.mods[x].alive)
            continue;
        k = n++;
        while (dl_stage_top && k > 0 && L.mods[L.place[k - 1]].file_len < L.mods[x].file_len) {
            L.place[k] = L.place[k - 1];
            k--;
        }
        L.place[k] = x;
    }
    L.nplace = n;
    return n;
}

/* Every .run in placement order from the area's start, then every .bss. With the stage at the
 * top, each mod's file must also fit between the end of its own .run and the area's end, as
 * pass 2 reads it there after writing the code below. -> 1 when it fits; else the last living
 * mod in id order is refused, with the KB the worst of those is over, and 0. */
static int layout(void)
{
    u32 a = L.area_lo, over = 0;
    int i, last = -1, n = placement();
    for (i = 0; i < L.nmods; i++)
        if (L.mods[L.order[i]].alive)
            last = i;
    for (i = 0; i < n; i++) {
        struct dmod *m = &L.mods[L.place[i]];
        if (m->run_len) {
            a = align(a, m->run_align < 4 ? 4 : m->run_align);
            m->run_base = a;
            a += m->run_len;
        } else {
            m->run_base = 0;
        }
        if (dl_stage_top && a > stage_at(m) && a - stage_at(m) > over)
            over = a - stage_at(m);
    }
    a = align(a, 4);
    L.runs_end = a;
    for (i = 0; i < n; i++) {
        struct dmod *m = &L.mods[L.place[i]];
        if (m->bss_len) {
            a = align(a, m->bss_align < 4 ? 4 : m->bss_align);
            m->bss_base = a;
            a += m->bss_len;
        } else {
            m->bss_base = 0;
        }
    }
    a = align(a, 4);
#ifdef DL_COLLECTIONS
    for (i = 0; i < L.nmods; i++) {      /* the +Drive tables, in id order: living entries and a zero */
        struct dmod *m = &L.mods[L.order[i]];
        u32 q, x, k;
        if (!m->alive)
            continue;
        for (q = m->coll_first; q < (u32)m->coll_first + m->coll_n; q++) {
            struct coll *c = &L.coll[q];
            u32 n = 0;
            for (x = 0; x < (u32)L.nmods; x++)
                if (L.mods[x].alive)
                    for (k = 0; k < L.mods[x].ctr_n; k++)
                        if (L.ctr[L.mods[x].ctr_first + k].table == (DTAB | q))
                            n += L.ctr[L.mods[x].ctr_first + k].nent;
            c->count = (u16)n;
            c->addr = a;
            a += (n + 1) * c->entry;
        }
    }
#endif
    if (a > L.area_hi && a - L.area_hi > over)
        over = a - L.area_hi;
    if (over) {
        refuse(&L.mods[L.order[last]], DL_R_ROOM, (over + 1023) / 1024, 0);
        return 0;
    }
    dl_used_lo = L.area_lo;
    dl_used_hi = a;
    return 1;
}

/* ---- pass 2 ----------------------------------------------------------------------------- */
/* A relocation's target -> S. -> 1, or 0 when nothing defines it. */
static int resolve(struct dmod *m, const char *s, u32 n, u32 *v)
{
    if (js_eq(s, n, "abs")) {
        *v = 0;
        return 1;
    }
    if (n > 4 && s[0] == 's' && s[1] == 'e' && s[2] == 'c' && s[3] == ':') {
        if (js_eq(s + 4, n - 4, ".run") && m->run_len) {
            *v = m->run_base;
            return 1;
        }
        if (js_eq(s + 4, n - 4, ".bss") && m->bss_len) {
            *v = m->bss_base;
            return 1;
        }
        return 0;
    }
    if (n > 4 && s[0] == 's' && s[1] == 'y' && s[2] == 'm' && s[3] == ':') {
        u32 h = dl_hash(s + 4, n - 4), i;
        const struct sym *g;
#ifdef DL_KNOBS
        if (L.knobs && m->knobs && js_eq(s + 4, n - 4, "__knobs")) {   /* its run's first id */
            *v = m->knob_base;
            return 1;
        }
#endif
        for (i = 0; i < L.nlocals; i++)
            if (L.locals[i].hash == h) {
                u32 off = L.locals[i].val, sec = L.locals[i].sec;
                *v = sec == SEC_RUN ? m->run_base + off : sec == SEC_BSS ? m->bss_base + off : off;
                return 1;
            }
        g = global(h);
        if (g) {
            *v = sym_value(g);
            return 1;
        }
        for (i = 0; i < m->weak.n; i++)
            if (js_eq(s + 4, n - 4, L.arena + L.lists[m->weak.first + i])) {
                g = global(dl_hash("core_zero", 9));
                if (!g)
                    return 0;
                *v = sym_value(g);
                return 1;
            }
    }
    return 0;
}

static int put(u8 *at_, int type, u32 value, u32 p)
{
    if (type == R_ABS32) {
        dl_wr32(at_, value);
    } else if (type == R_PC32) {
        dl_wr32(at_, value - p);
    } else if (type == R_ABS16) {        /* link.put_reloc: two bytes, -0x8000 to 0xFFFF */
        if (value > 0xFFFF && value < 0xFFFF8000u)
            return 0;
        dl_wr16(at_, value);
    } else {
        s32 d = (s32)(value - p);
        if (d < -0x8000 || d >= 0x8000)
            return 0;
        dl_wr16(at_, (u32)d);
    }
    return 1;
}

static int rtype(const char *s, u32 n)
{
    return js_eq(s, n, "abs32") ? R_ABS32 : js_eq(s, n, "pc32") ? R_PC32 : js_eq(s, n, "abs16") ? R_ABS16 : R_PC16;
}

/* Pass 2 for one mod, its file in buf. -> 0, or a refusal code. */
static int place(struct dmod *m, const u8 *buf, u32 len, const char **what)
{
    const char *k[K_N], *s;
    js_t j;
    u32 n;
    s32 v;
    int first;
    u16 ci = 0;

    *what = 0;
    if (!top(buf, len, k))
        return DL_R_JSON;
    /* its own symbols */
    L.nlocals = 0;
    if (k[K_SYMBOLS]) {
        at(&j, k[K_SYMBOLS], buf, len);
        first = 1;
        js_open(&j, '{');
        while (js_next(&j, '}', &first)) {
            int f2 = 1;
            u32 h, sec;
            js_key(&j, &s, &n);
            h = dl_hash(s, n);
            js_open(&j, '[');
            js_next(&j, ']', &f2);
            js_str(&j, &s, &n);
            sec = js_eq(s, n, ".run") ? SEC_RUN : js_eq(s, n, ".bss") ? SEC_BSS
                : js_eq(s, n, "abs") ? SEC_ABS : 0xF;
            js_next(&j, ']', &f2);
            js_int(&j, &v);
            js_next(&j, ']', &f2);
            if (sec == 0xF)              /* a section it may not have: no reference can use it */
                continue;
            if (L.nlocals >= L.LOCALS)
                return DL_R_LIMIT;
            L.locals[L.nlocals].hash = h;
            L.locals[L.nlocals].val = (u32)v;
            L.locals[L.nlocals].sec = sec;
            L.nlocals++;
        }
        if (j.err)
            return DL_R_JSON;
    }
    /* .run into place, .bss zeroed */
    if (m->run_len) {
        at(&j, k[K_SECTIONS], buf, len);
        first = 1;
        js_open(&j, '{');
        while (js_next(&j, '}', &first)) {
            js_key(&j, &s, &n);
            if (js_eq(s, n, ".run")) {
                int f2 = 1;
                js_open(&j, '{');
                while (js_next(&j, '}', &f2)) {
                    js_key(&j, &s, &n);
                    if (js_eq(s, n, "parts")) {
                        u32 got;
                        if (!parts(&j, DL_MEM(m->run_base), m->run_len, &got) || got != m->run_len)
                            return DL_R_JSON;
                    } else {
                        js_skip(&j);
                    }
                }
            } else {
                js_skip(&j);
            }
        }
    }
    if (m->bss_len)                      /* with the stage in the area, a file read later may */
        dl_memset(DL_MEM(m->bss_base), 0, m->bss_len);   /* cover it: link_set zeroes it again */
    /* relocations */
    if (k[K_RELOCS]) {
        at(&j, k[K_RELOCS], buf, len);
        first = 1;
        js_open(&j, '[');
        while (js_next(&j, ']', &first)) {
            int f2 = 1, type;
            s32 off, add;
            u32 sv;
            js_open(&j, '[');
            js_next(&j, ']', &f2);
            js_str(&j, &s, &n);
            js_next(&j, ']', &f2);
            js_int(&j, &off);
            js_next(&j, ']', &f2);
            js_str(&j, &s, &n);
            type = rtype(s, n);
            js_next(&j, ']', &f2);
            js_str(&j, &s, &n);
            if (!resolve(m, s, n, &sv)) {
                static char nm[32];
                u32 q = n > 4 ? (n - 4 < 31 ? n - 4 : 31) : 0;
                dl_memcpy(nm, s + 4, q);
                nm[q] = 0;
                *what = nm;
                return DL_R_IMPORT;
            }
            js_next(&j, ']', &f2);
            js_int(&j, &add);
            js_next(&j, ']', &f2);
            if (!put(DL_MEM(m->run_base + off), type, sv + (u32)add, m->run_base + off))
                return DL_R_RANGE;

        }
        if (j.err)
            return DL_R_JSON;
    }
#ifdef DL_SITES
    {
        int code = place_sites(m, k[K_SITES], buf, len, what);
        if (code)
            return code;
    }
#endif
    /* table entries: their bytes and their relocations (placed at the merge) */
    if (k[K_CONTRIBUTE]) {
        at(&j, k[K_CONTRIBUTE], buf, len);
        first = 1;
        js_open(&j, '[');
        while (js_next(&j, ']', &first)) {
            int f2 = 1;
            struct entry *e;
            const struct contrib *c = &L.ctr[m->ctr_first + ci];
            if (L.nent >= L.CONTRIBS)
                return DL_R_LIMIT;
            e = &L.ent[L.nent];
            e->mod = (u16)(m - L.mods);
            e->ctr = (u16)(m->ctr_first + ci);
            e->data = (u16)L.edata_n;
            e->rel_first = (u16)L.nerel;
            e->rel_n = 0;
            js_open(&j, '{');
            while (js_next(&j, '}', &f2)) {
                js_key(&j, &s, &n);
                if (js_eq(s, n, "data")) {
                    u32 dn;
                    if (L.edata_n + (u32)c->nent * ctr_entry(c) > L.EDATA)
                        return DL_R_LIMIT;
                    if (!js_hex(&j, L.edata + L.edata_n, L.EDATA - L.edata_n, &dn))
                        return DL_R_JSON;
                    L.edata_n += dn;
                } else if (js_eq(s, n, "relocs")) {
                    int f3 = 1;
                    js_open(&j, '[');
                    while (js_next(&j, ']', &f3)) {
                        int f4 = 1;
                        s32 off, add;
                        u32 sv;
                        struct erel *r;
                        js_open(&j, '[');
                        js_next(&j, ']', &f4);
                        js_int(&j, &off);
                        js_next(&j, ']', &f4);
                        js_str(&j, &s, &n);
                        if (L.nerel >= L.ERELOCS)
                            return DL_R_LIMIT;
                        r = &L.erel[L.nerel];
                        r->type = (u8)rtype(s, n);
                        if (!rsize(s, n))
                            return DL_R_FORMAT;
                        js_next(&j, ']', &f4);
                        js_str(&j, &s, &n);
                        if (!resolve(m, s, n, &sv)) {
                            static char nm[32];
                            u32 q = n > 4 ? (n - 4 < 31 ? n - 4 : 31) : 0;
                            dl_memcpy(nm, s + 4, q);
                            nm[q] = 0;
                            *what = nm;
                            return DL_R_IMPORT;
                        }
                        js_next(&j, ']', &f4);
                        js_int(&j, &add);
                        js_next(&j, ']', &f4);
                        if (off < 0 || (u32)off + (r->type == R_PC16 || r->type == R_ABS16 ? 2 : 4)
                                           > (u32)c->nent * ctr_entry(c))
                            return DL_R_FORMAT;
                        r->off = (u16)off;
                        r->value = sv + (u32)add;
                        L.nerel++;
                        e->rel_n++;
                    }
                } else {
                    js_skip(&j);
                }
            }
            L.nent++;
            ci++;
        }
        if (j.err)
            return DL_R_JSON;
    }
    /* sites: their new bytes into sdata, relocated at their own address (written at the end) */
    if (m->site_n && k[K_SITES]) {
        const struct dsite *d = &L.site[m->site_first];
        at(&j, k[K_SITES], buf, len);
        first = 1;
        js_open(&j, '[');
        while (js_next(&j, ']', &first)) {
            int f2 = 1;
            u8 *dst = L.sdata + d->data;
            const char *relp = 0;
            js_open(&j, '{');
            while (js_next(&j, '}', &f2)) {
                js_key(&j, &s, &n);
                if (js_eq(s, n, "new")) {
                    u32 dn;
                    if (!js_hex(&j, dst, d->hi - d->lo, &dn))
                        return DL_R_JSON;
                } else if (js_eq(s, n, "relocs")) {
                    relp = j.p;
                    js_skip(&j);
                } else {
                    js_skip(&j);
                }
            }
            if (relp) {
                js_t q;
                int f3 = 1;
                at(&q, relp, buf, len);
                js_open(&q, '[');
                while (js_next(&q, ']', &f3)) {
                    int f4 = 1, type;
                    s32 off, add;
                    u32 sv;
                    js_open(&q, '[');
                    js_next(&q, ']', &f4);
                    js_int(&q, &off);
                    js_next(&q, ']', &f4);
                    js_str(&q, &s, &n);
                    type = rtype(s, n);
                    js_next(&q, ']', &f4);
                    js_str(&q, &s, &n);
                    if (!resolve(m, s, n, &sv)) {
                        static char nm[32];
                        u32 c = n > 4 ? (n - 4 < 31 ? n - 4 : 31) : 0;
                        dl_memcpy(nm, s + 4, c);
                        nm[c] = 0;
                        *what = nm;
                        return DL_R_IMPORT;
                    }
                    js_next(&q, ']', &f4);
                    js_int(&q, &add);
                    js_next(&q, ']', &f4);
                    if (!put(dst + off, type, sv + (u32)add, d->lo + (u32)off))
                        return DL_R_RANGE;
                }
            }
            d++;
            while (d < L.site + m->site_first + m->site_n && d->data == NONE32)
                d++;
        }
        if (j.err)
            return DL_R_JSON;
    }
    return 0;
}

/* ---- bind: merge each table that gets entries ------------------------------------------- */
static int item_before(const struct item *a, const struct item *b)
{
    int c;
    if (a->order != b->order)
        return a->order < b->order;
    c = dl_strcmp(a->id, b->id);
    if (c)
        return c < 0;
    return a->seq < b->seq;
}

static int bind(int write)
{
    u32 t, i, k, nt = n_tables();
#ifdef DL_COLLECTIONS
    nt += L.ncoll;                       /* then the +Drive tables: no flashed entries, in the area */
#endif
    for (t = 0; t < nt; t++) {
        u32 tix = t, esz, cnt, n = 0, addr, cap;
        u8 *tab;
        int any = 0;
#ifdef DL_COLLECTIONS
        if (t >= n_tables()) {
            const struct coll *c = &L.coll[t - n_tables()];
            if (!L.mods[c->mod].alive)
                continue;
            tix = DTAB | (t - n_tables());
            esz = c->entry;
            cnt = 0;
            addr = c->addr;
            cap = c->count;
        } else
#endif
        {
            esz = table_entry(t);
            cnt = table_count(t);
            addr = table_addr(t);
            cap = table_cap(t);
        }
        tab = DL_MEM(addr);
        for (i = 0; i < L.nent; i++)
            if (L.mods[L.ent[i].mod].alive && L.ctr[L.ent[i].ctr].table == tix)
                any = 1;
        if (!any)
            continue;
        if (cnt && (cnt + 1) * esz > L.MDATA)   /* a +Drive table has no flashed entries to keep */
            return 0;
        dl_memcpy(L.mdata, tab, cnt * esz);
        for (i = 0; i < cnt; i++) {
            struct item *it = &L.items[n++];
            it->order = table_order(t, i);
            it->id = module_name(table_key(t, i) & 0xFFFF);
            it->seq = (u16)i;
            it->data = L.mdata + i * esz;
            it->entry = NONE;
            it->sub = 0;
        }
        for (i = 0; i < L.nent; i++) {
            const struct entry *e = &L.ent[i];
            const struct contrib *c = &L.ctr[e->ctr];
            if (!L.mods[e->mod].alive || c->table != tix)
                continue;
            for (k = 0; k < c->nent; k++) {
                struct item *it;
                if (n >= L.MERGE)
                    return 0;
                it = &L.items[n++];
                it->order = c->order;
                it->id = astr(L.mods[e->mod].id);
                it->seq = (u16)(c->index << 8 | k);
                it->data = L.edata + e->data + k * esz;
                it->entry = (u16)i;
                it->sub = (u16)k;
            }
        }
        for (i = 1; i < n; i++) {        /* a stable insertion sort */
            struct item x = L.items[i];
            k = i;
            while (k > 0 && item_before(&x, &L.items[k - 1])) {
                L.items[k] = L.items[k - 1];
                k--;
            }
            L.items[k] = x;
        }
        /* items point into mdata and edata; mdata is a copy, so the table can be rewritten */
        for (i = 0; i < n && write; i++) {
            const struct item *it = &L.items[i];
            dl_memcpy(tab + i * esz, it->data, esz);
        }
        for (i = 0; i < n; i++) {
            const struct item *it = &L.items[i];
            const struct entry *e;
            if (it->entry == NONE)
                continue;
            e = &L.ent[it->entry];
            for (k = 0; k < e->rel_n; k++) {
                const struct erel *r = &L.erel[e->rel_first + k];
                u32 lo = it->sub * esz, at_ = addr + i * esz + (r->off - lo);
                if (r->off < lo || r->off >= lo + esz)
                    continue;
                if (write)
                    put(DL_MEM(at_), r->type, r->value, at_);
                else if (r->type == R_PC16 && ((s32)(r->value - at_) < -0x8000 || (s32)(r->value - at_) >= 0x8000))
                    return 0;
                else if (r->type == R_ABS16 && r->value > 0xFFFF && r->value < 0xFFFF8000u)
                    return 0;
            }
        }
        if (write)
            dl_memset(tab + n * esz, 0, (cap + 1 - n) * esz);
    }
    return 1;
}

/* ---- /mods/active ------------------------------------------------------------------------ */
int dl_parse_active(const char *text, u32 len, struct dl_choice *out)
{
    u32 i = 0, n = 0;
    while (i < len) {
        u32 a = i, e, sp, k;
        s32 crc;
        while (i < len && text[i] != '\n')
            i++;
        e = i++;
        if (e > a && text[e - 1] == '\r')
            e--;
        while (a < e && (text[a] == ' ' || text[a] == '\t'))
            a++;
        while (e > a && (text[e - 1] == ' ' || text[e - 1] == '\t'))
            e--;
        if (a == e || text[a] == '#')
            continue;
        for (sp = e; sp > a && text[sp - 1] != ' ' && text[sp - 1] != '\t'; sp--)
            ;
        if (sp == a)
            continue;                    /* no CRC: not a line the loader takes */
        k = sp;
        while (k > a && (text[k - 1] == ' ' || text[k - 1] == '\t'))
            k--;
        if (k - a >= DL_LINE_NAME_MAX)
            continue;
        {
            char hex[12];
            u32 h = e - sp, q;
            if (h > 10)
                continue;
            for (q = 0; q < h; q++)
                hex[q] = text[sp + q];
            if (!(h > 2 && hex[0] == '0' && (hex[1] | 0x20) == 'x')) {   /* bare hex digits */
                for (q = h; q > 0; q--)
                    hex[q + 1] = hex[q - 1];
                hex[0] = '0';
                hex[1] = 'x';
                h += 2;
            }
            if (!js_parse_int(hex, h, &crc))
                continue;
        }
        if (n >= DL_MAX_MODS)
            return -1;
        if (out) {
            dl_memcpy(out[n].name, text + a, k - a);
            out[n].name[k - a] = 0;
            out[n].crc = (u32)crc;
        }
        n++;
    }
    return (int)n;
}

/* ---- the link state's memory ------------------------------------------------------------- */
static void *take(u32 n) { return dl_alloc(n ? n : 4); }

/* Every block begin() took goes back. */
#ifdef DL_KNOBS
static int knob_start(void);
#endif

static void finish(void)
{
    static void **const p[] = {(void **)&L.mods, (void **)&L.order, (void **)&L.place, (void **)&L.arena, (void **)&L.lists,
                               (void **)&L.syms, (void **)&L.sym_next, (void **)&L.sym_head, (void **)&L.locals,
                               (void **)&L.ctr, (void **)&L.ent, (void **)&L.edata, (void **)&L.erel,
                               (void **)&L.items, (void **)&L.mdata, (void **)&L.fld, (void **)&L.fdesc,
                               (void **)&L.want, (void **)&L.want_fld,
#ifdef DL_COLLECTIONS
                               (void **)&L.coll,
#endif
#ifdef DL_SITES
                               (void **)&L.sites, (void **)&L.tsdata,
#endif
#ifdef DL_KNOBS
                               (void **)&L.kfix, (void **)&L.kflash, (void **)&L.krun, (void **)&L.kfree,
#endif
                               };
    u32 i;
    for (i = 0; i < sizeof p / sizeof p[0]; i++) {
        dl_free(*p[i]);
        *p[i] = 0;
    }
#ifdef DL_KNOBS
    ds_state_free(L.kprev);
    L.kprev = 0;
    ds_knobs_in = 0;
#endif
}

static u32 pool_n(u32 per, u32 mb, u32 base)
{
    u32 v = base + per * mb;
    return v > DL_POOL_CAP ? DL_POOL_CAP : v;
}

/* The pools' sizes (dl.h, DL_POOL_*): per started MB of the area, the symbols past the
 * flashed build's own names; the merge's room is the biggest table +Drive entries can go
 * to. -> 1 when every block was taken, else 0 (finish() gives back what was). */
static int pools(int n)
{
    u32 mb = L.area_hi > L.area_lo ? (L.area_hi - L.area_lo + 0xFFFFF) >> 20 : 0, t;
    u32 flashed = n_exports() + 1;
    if (!mb)
        mb = 1;
    for (t = 0; t < n_tables(); t++) {
        flashed += dl_strlen(table_name(t)) < 60 ? 2 : 1;
        if ((table_flags(t) & F_MORE) && !(table_flags(t) & F_DSP)) {
            if (table_cap(t) + 1 > L.MERGE)
                L.MERGE = table_cap(t) + 1;
            if ((table_cap(t) + 1) * table_entry(t) > L.MDATA)
                L.MDATA = (table_cap(t) + 1) * table_entry(t);
        }
    }
    L.ARENA = pool_n(DL_POOL_ARENA, mb, 0);
    L.LISTS = pool_n(DL_POOL_LISTS, mb, 0);
    L.SYMS = pool_n(DL_POOL_SYMS, mb, flashed);
    L.LOCALS = pool_n(DL_POOL_LOCALS, mb, 0);
    L.CONTRIBS = pool_n(DL_POOL_CONTRIBS, mb, 0);
    L.EDATA = pool_n(DL_POOL_EDATA, mb, 0);
    L.ERELOCS = pool_n(DL_POOL_ERELOCS, mb, 0);
    L.SITES = pool_n(DL_POOL_SITES, mb, 0);
    L.SDATA = pool_n(DL_POOL_SDATA, mb, 0);
#ifdef DL_COLLECTIONS
    L.COLLS = pool_n(DL_POOL_COLLS, mb, 0);
    if (L.EDATA / 4 + 1 > L.MERGE)       /* a +Drive table's entries all fit the entry bytes' pool */
        L.MERGE = L.EDATA / 4 + 1;
    L.coll = take(L.COLLS * sizeof *L.coll);
    if (!L.coll)
        return 0;
#endif
    L.mods = take((u32)n * sizeof *L.mods);
    L.order = take((u32)n * sizeof *L.order);
    L.place = take((u32)n * sizeof *L.place);
    L.arena = take(L.ARENA);
    L.lists = take(L.LISTS * sizeof *L.lists);
    L.syms = take(L.SYMS * sizeof *L.syms);
    L.sym_next = take(L.SYMS * sizeof *L.sym_next);
    L.locals = take(L.LOCALS * sizeof *L.locals);
    L.ctr = take(L.CONTRIBS * sizeof *L.ctr);
    L.ent = take(L.CONTRIBS * sizeof *L.ent);
    L.edata = take(L.EDATA);
    L.erel = take(L.ERELOCS * sizeof *L.erel);
    L.items = take(L.MERGE * sizeof *L.items);
    L.mdata = take(L.MDATA);
    if (L.store) {
        L.fld = take(DS_MAX_FIELDS * sizeof *L.fld);
        L.fdesc = take(DS_MAX_FIELDS * sizeof *L.fdesc);
        L.want = take(DS_MAX_FIELDS * sizeof *L.want);
        L.want_fld = take(DS_MAX_FIELDS * sizeof *L.want_fld);
        if (!L.fld || !L.fdesc || !L.want || !L.want_fld)
            return 0;
    }
#ifdef DL_SITES
    L.sites = take(DL_SITES_MAX * sizeof *L.sites);
    L.tsdata = take(DL_SDATA_MAX);
    if (!L.sites || !L.tsdata)
        return 0;
#endif
    L.site = take(L.SITES * sizeof *L.site);
    L.sdata = take(L.SDATA);
    return L.mods && L.order && L.place && L.arena && L.lists && L.syms && L.sym_next && L.locals && L.ctr
           && L.ent && L.edata && L.erel && L.items && L.mdata && L.site && L.sdata;
}

/* ---- the whole link ------------------------------------------------------------------------ */
static s32 load(struct dmod *m, const struct dl_choice *ch)
{
    s32 len = dl_read_file(ch->name, L.buf, L.max);
    if (L.buf_at && L.buf_at + (len > 0 ? (u32)len : L.max) > L.read_hi)
        L.read_hi = L.buf_at + (len > 0 ? (u32)len : L.max);   /* a failed read may have written */
    if (len == -1)
        return -DL_R_MISSING;
    if (len == -2)
        return -DL_R_TOO_BIG;
    if (len < 0)
        return -DL_R_MISSING;
    if (dl_crc32(L.buf, (u32)len) != ch->crc)
        return -DL_R_CHANGED;
    (void)m;
    return len;
}

/* ---- +Drive storage ------------------------------------------------------------------------ */
static int find_named(const char *name)
{
    u32 i;
    for (i = 0; i < n_tables(); i++)
        if (!dl_strcmp(table_name(i), name))
            return (int)i;
    return -1;
}

static u32 flashed_export(const char *name)
{
    u32 e;
    for (e = 0; e < n_exports(); e++)
        if (!dl_strcmp(pool(D32(o_export(e))), name))
            return D32(o_export(e) + 4);
    return 0;
}

/* Does the build keep +Drive storage, and what does it give it (drive_store.Base.from_build)?
 * It does with the six scopes in its storage record, the tables core_store_ids, core_lock_codes,
 * core_lock_ccs, core_field_inits and core_handles, and core 0.14's core_store_gens. Read before
 * the boot rewrites anything: the flashed entries are each table's first `count`. */
static void store_base(void)
{
    u32 o = D32(0x40), i;
    L.store = 0;
    L.t_ids = find_named("core_store_ids");
    L.t_codes = find_named("core_lock_codes");
    L.t_ccs = find_named("core_lock_ccs");
    L.t_inits = find_named("core_field_inits");
    L.t_handles = find_named("core_handles");
    L.gens_at = flashed_export("core_store_gens");
    if (L.t_ids < 0 || L.t_codes < 0 || L.t_ccs < 0 || L.t_inits < 0 || L.t_handles < 0 || !L.gens_at
        || D16(o + 8) != DS_SCOPES || table_count(L.t_ids) < 1)
        return;
    for (i = 0; i < DS_SCOPES; i++) {
        if (dl_strcmp(pool(D32(o + 12 + 16 * i)), SCOPE_NAMES[i]))
            return;
        L.sb.room[i] = D32(o + 12 + 16 * i + 4);
        L.sb.used[i] = D32(o + 12 + 16 * i + 8);
        L.sb.gen[i] = D32(o + 12 + 16 * i + 12);
    }
    L.sb.flashed_id = dl_rd32(DL_MEM(table_addr(L.t_ids)));
    L.sb.nanc = table_count(L.t_ids) - 1;
    if (L.sb.nanc > DS_MAX_IDS)
        L.sb.nanc = DS_MAX_IDS;
    for (i = 0; i < L.sb.nanc; i++)
        L.sb_anc[i] = dl_rd32(DL_MEM(table_addr(L.t_ids) + 4 * (i + 1)));
    L.sb.anc = L.sb_anc;
    L.sb.codes = DL_MEM(table_addr(L.t_codes));
    L.sb.ncodes = table_count(L.t_codes);
    L.sb.ccs = DL_MEM(table_addr(L.t_ccs));
    L.sb.nccs = table_count(L.t_ccs);
    L.sb.inits = table_count(L.t_inits);
    L.sb.spare[DS_T_CODES] = (s32)(table_cap(L.t_codes) - table_count(L.t_codes));
    L.sb.spare[DS_T_CCS] = (s32)(table_cap(L.t_ccs) - table_count(L.t_ccs));
    L.sb.spare[DS_T_INITS] = (s32)(table_cap(L.t_inits) - table_count(L.t_inits));
    L.sb.spare[DS_T_HANDLES] = (s32)(table_cap(L.t_handles) - table_count(L.t_handles));
    L.dsts_at = flashed_export("core_lfo_dsts");
    L.sb.live_dst = L.dsts_at != 0;
    L.sb.core = module_name(0);
    L.store = 1;
}

/* The living mods' fields in id order, each mod's in its order (dstore.h). -> how many. */
static u32 wanted(void)
{
    u32 n = 0, q;
    int i;
    for (i = 0; i < L.nmods; i++) {
        const struct dmod *m = &L.mods[L.order[i]];
        if (!m->alive)
            continue;
        for (q = 0; q < m->fld_n; q++) {
            const struct fld *f = &L.fld[m->fld_first + q];
            struct ds_want *w = &L.want[n];
            w->mod = astr(m->id);
            w->sym = L.arena + f->sym;
            w->scope = f->scope;
            w->bits = f->bits;
            w->flags = f->flags;
            w->init = f->init;
            L.want_fld[n++] = (u16)(m->fld_first + q);
        }
    }
    return n;
}

static char *put_num(char *p, u32 v)
{
    char t[10];
    int k = 0;
    do
        t[k++] = (char)('0' + v % 10);
    while (v /= 10);
    while (k)
        *p++ = t[--k];
    return p;
}

/* The room (drive_store.fit), one refusal: the last living mod in id order that needs what is
 * short. -> 1 when it refused one. */
static int fit_round(void)
{
    struct ds_short sh;
    char d[32], *p = d;
    const char *w;
    u32 n;
    if (!L.store || !(n = wanted()) || !ds_fit_one(&L.sb, L.want, n, 0, &sh))
        return 0;
    for (w = ds_what[sh.what]; *w;)
        *p++ = *w++;
    *p++ = ' ';
    p = put_num(p, (u32)sh.need);
    *p++ = ' ';
    p = put_num(p, sh.have < 0 ? 0 : (u32)sh.have);
    *p = 0;
    refuse(&L.mods[L.fld[L.want_fld[sh.victim]].mod], DL_R_STORE, 0, d);
    return 1;
}

/* Check rounds, the room and the knob runs, until none refuses a mod. */
static void settle(void)
{
    do
        while (check_round())
            ;
#ifdef DL_KNOBS
    while (fit_round() || knob_runs());
#else
    while (fit_round());
#endif
}

/* The living mods' fields placed from /mods/.store (drive_store.allocate): their descriptors for
 * pass 2, and dl_store_out for the boot. -> 1, 0 when the heap has no room. */
static int place_store(void)
{
    u32 n, i;
    if (!L.store)
        return 1;
    ds_out_free(&dl_store_out);
    n = wanted();
    if (ds_allocate(&L.sb, dl_store_prev, dl_store_prev_n, L.want, n, &dl_store_out))
        return 0;
    for (i = 0; i < n; i++)
        L.fdesc[L.want_fld[i]] = dl_store_out.desc[i];
    return 1;
}

/* One loader-only table: the flashed entries, then ours (bytes, or with words the u32s), then
 * zeros to its end. */
static void put_entries(int t, const u8 *data, const u32 *words, u32 n)
{
    u32 esz = table_entry(t), cnt = table_count(t), cap = table_cap(t), i;
    u8 *tab = DL_MEM(table_addr(t));
    if (cnt + n > cap)
        n = cap - cnt;
    for (i = 0; i < n; i++)
        if (words)
            dl_wr32(tab + (cnt + i) * esz, words[i]);
        else
            dl_memcpy(tab + (cnt + i) * esz, data + i * esz, esz);
    dl_memset(tab + (cnt + n) * esz, 0, (cap + 1 - cnt - n) * esz);
}

/* The placement into the core's tables: the live layout ID and its ancestors, each scope's
 * generation, the core's LFO destination fields the +Drive placed, and the +Drive fields' codes,
 * CCs, inits and handles. */
static void write_store(void)
{
    u8 *ids;
    u32 i, cap;
    const struct ds_out *o = &dl_store_out;
    if (!L.store || !o->bytes)
        return;
    ids = DL_MEM(table_addr(L.t_ids));   /* the ID, its ancestors, then zeros */
    cap = table_cap(L.t_ids);
    dl_memset(ids, 0, 4 * (cap + 1));
    dl_wr32(ids, o->id);
    for (i = 0; i < o->nanc && i + 1 < cap; i++)
        dl_wr32(ids + 4 * (i + 1), o->anc[i]);
    dl_memcpy(DL_MEM(L.gens_at), o->gen, DS_SCOPES);
    for (i = 0; i < 2; i++)              /* the core's LFO fields the +Drive placed; else the build's */
        if (L.dsts_at && o->lfo_dst[i])
            dl_wr32(DL_MEM(L.dsts_at + 4 * i), o->lfo_dst[i]);
    put_entries(L.t_codes, o->codes, 0, o->ncodes);
    put_entries(L.t_ccs, o->ccs, 0, o->nccs);
    put_entries(L.t_inits, 0, o->inits, o->ninits);
    put_entries(L.t_handles, 0, o->handles, o->nhandles);
}

/* The link state for n lines and the flashed build's names: the first part of a link and a
 * check. The records go to st/det (n of them, zeroed here). -> 1, 0 when the descriptor isn't
 * one the loader reads, -1 when the heap has no room for the link state. */
static int begin_start(u32 desc, int n, u8 *buf, u32 max, struct dl_status *st_out, char (*det)[32])
{
    u32 e;

    finish();
    dl_memset(&L, 0, (u32)((u8 *)&L.crc_table - (u8 *)&L));   /* the crc table stays */
    L.st = st_out;
    L.det = det;
    dl_memset(st_out, 0, (u32)n * sizeof *st_out);
    dl_memset(det, 0, (u32)n * sizeof *det);
    L.buf = buf;
    L.max = max;
    L.d_addr = desc;
    L.d = DL_MEM(desc);
    if (dl_rd32(L.d) != 0x454c4b44 || dl_rd16(L.d + 4) != 1)
        return 0;
    L.pool = D32(0x18);
    L.area_lo = D32(D32(0x3c) + 4);
    L.area_hi = D32(D32(0x3c) + 8);
    if (dl_stage_top && !buf) {          /* a link with the stage in the area: pass 1 from its start */
        L.buf_at = L.area_lo;
        L.buf = DL_MEM(L.area_lo);
        L.max = L.area_hi - L.area_lo;
    }
    store_base();
    for (e = 0; e < n_map(); e++)        /* the main OS ends where the boot copier starts */
        if (D16(o_map(e) + 8) == 4 && (!L.main_hi || D32(o_map(e)) < L.main_hi))
            L.main_hi = D32(o_map(e));
    if (L.main_hi <= DL_MAIN_LO)
        L.main_hi = 0;
#ifdef DL_SITES
    L.main_hi = 0;                       /* a mod's sites are TELOS's (scan_tsites), not the site map's */
#endif
    if (!pools(n))
        return -1;
#ifdef DL_KNOBS
    if (!knob_start())
        return -1;
#endif
    L.arena_n = 1;                       /* offset 0 holds "" */

    /* the flashed build's names: exports, tables and their counts, core_desc */
    for (e = 0; e < n_exports(); e++) {
        const char *nm = pool(D32(o_export(e)));
        add_sym(dl_hash(nm, dl_strlen(nm)), FLASHED, SEC_ABS, D32(o_export(e) + 4));
    }
    for (e = 0; e < n_tables(); e++) {
        const char *nm = table_name(e);
        u32 h = dl_hash(nm, dl_strlen(nm));
        char cn[64];
        u32 q = dl_strlen(nm);
        add_sym(h, FLASHED, SEC_TABLE, e);
        if (q < 60) {
            dl_memcpy(cn, nm, q);
            cn[q] = '_';
            cn[q + 1] = 'n';
            add_sym(dl_hash(cn, q + 2), FLASHED, SEC_COUNT, e);
        }
    }
    add_sym(dl_hash("core_desc", 9), FLASHED, SEC_ABS, desc);
    L.flashed_syms = L.nsyms;
    return 1;
}

/* Pass 1 for line i of the set (lines in order, each once): its file read into the buffer, or
 * (have >= 0) already there, have bytes of it, or (why) not readable for that reason. */
static void begin_line(const struct dl_choice *ch, int i, s32 have, int why)
{
    struct dmod *m = &L.mods[L.nmods];
    struct dl_status *st = &L.st[i];
    const char *what;
    s32 len;
    int code;
    u32 q;
    for (q = 0; q < DL_ID_MAX - 1 && ch->name[q]; q++)
        st->id[q] = ch->name[q];
    dl_memset(m, 0, sizeof *m);
    m->choice = (u16)i;
    m->id = m->version = m->core = NONE;
    len = why ? -why : have >= 0 ? have : load(m, ch);
    if (len < 0) {
        set_status(m, -len == DL_R_TOO_BIG ? DL_ST_REFUSED : DL_ST_DROPPED, -len, 0, 0);
        return;
    }
    m->file_len = (u32)len;
    code = scan(m, L.buf, (u32)len, &what);
    if (code) {
        /* a file pass 1 refuses leaves nothing behind: its exports, its entries */
        set_status(m, DL_ST_REFUSED, code, L.scan_arg, what);
        if (m->exp_n)
            L.nsyms = m->exp_first;
        if (m->fld_n)
            L.nfld = m->fld_first;
        if (m->ctr_n)
            L.nctr = m->ctr_first;
#ifdef DL_KNOBS
        if (m->kfix_n)
            L.nkfix = m->kfix_first;
        m->kfix_n = 0;
#endif
#ifdef DL_COLLECTIONS
        if (m->coll_n)
            L.ncoll = m->coll_first;
#endif
        if (m->site_n) {
            L.nsite = m->site_first;
            L.sdata_n = m->sdata_first;
        }
        m->site_n = 0;
#ifdef DL_SITES
        if (m->tsite_n)
            L.nsites = m->tsite_first;
#endif
        return;
    }
    m->alive = 1;
    L.nmods++;
}

/* After pass 1: the symbols' buckets and the id order. -> 1, -1 when the heap has no room. */
static int begin_end(void)
{
    int i, k;
    u32 nb;
    /* the symbols' buckets: a power of two, at least as many as the symbols */
    for (nb = 16; nb < L.nsyms; nb <<= 1)
        ;
    L.sym_head = take(nb * sizeof *L.sym_head);
    if (!L.sym_head)
        return -1;
    L.sym_mask = nb - 1;
    hash_syms();

    /* id order: an insertion sort, stable, so of two files with one id the first line wins */
    for (i = 0; i < L.nmods; i++)
        L.order[i] = (u16)i;
    for (i = 1; i < L.nmods; i++) {
        u16 x = L.order[i];
        k = i;
        while (k > 0 && dl_strcmp(astr(L.mods[x].id), astr(L.mods[L.order[k - 1]].id)) < 0) {
            L.order[k] = L.order[k - 1];
            k--;
        }
        L.order[k] = x;
    }
    return 1;
}

/* The flashed build's names, then pass 1 over every chosen file, the symbols' buckets and the
 * id order: what a link and a check share. -> as begin_start. Every return is followed by
 * finish(). */
static int begin(u32 desc, const struct dl_choice *choice, int n, u8 *buf, u32 max,
                 struct dl_status *st_out, char (*det)[32])
{
    int i, b = begin_start(desc, n, buf, max, st_out, det);
    if (b <= 0)
        return b;
    for (i = 0; i < n; i++)
        begin_line(&choice[i], i, -1, 0);
    return begin_end();
}

/* The check's last part, after pass 1: versions, check rounds, layout, verdicts. */
static int check_end(int n, char (*ver)[DL_VER_MAX])
{
    u32 lo = dl_used_lo, hi = dl_used_hi;
    int i, rounds, ok = 0;
    if (ver) {
        dl_memset(ver, 0, (u32)n * sizeof *ver);
        for (i = 0; i < L.nmods; i++) {
            const char *v = astr(L.mods[i].version);
            u32 q;
            for (q = 0; q < DL_VER_MAX - 1 && v[q]; q++)
                ver[L.mods[i].choice][q] = v[q];
        }
    }
    for (rounds = 0; rounds < 4 * DL_MAX_MODS; rounds++) {
        settle();
        if (layout())
            break;
    }
    for (i = 0; i < L.nmods; i++)
        if (L.mods[i].alive) {
            set_status(&L.mods[i], DL_ST_LOADED, 0, 0, 0);
            ok++;
        }
    dl_used_lo = lo;
    dl_used_hi = hi;
    finish();
    return ok;
}

/* The MODS page's check of a set: pass 1, the checks and the layout, as dl_link, without
 * pass 2 and without writing anything outside the link state (the area, the tables and
 * dl_status stay as the boot left them). A mod that would load gets DL_ST_LOADED. The
 * refusals only pass 2 finds (a pc16 out of range, a relocation to a name nothing
 * defines) are not seen. -> how many would load, or -1 when the descriptor isn't one the
 * loader reads, the set is over DL_MAX_MODS or the heap has no room for the check. */
int dl_check(u32 desc, const struct dl_choice *choice, int n, u8 *buf, u32 max,
             struct dl_status *st, char (*det)[32], char (*ver)[DL_VER_MAX])
{
    if (n < 0 || n > DL_MAX_MODS)
        return -1;
    if (begin(desc, choice, n, buf, max, st, det) <= 0) {
        finish();
        return -1;
    }
    return check_end(n, ver);
}

/* dl_check in steps (dl.h): the same check, one file's pass 1 a call. */
static struct {
    int n, next;
    char (*ver)[DL_VER_MAX];
} S;

int dl_check_start(u32 desc, int n, u8 *buf, u32 max, struct dl_status *st, char (*det)[32],
                   char (*ver)[DL_VER_MAX])
{
    S.n = S.next = 0;
    if (n < 0 || n > DL_MAX_MODS || begin_start(desc, n, buf, max, st, det) <= 0) {
        finish();
        return -1;
    }
    S.n = n;
    S.ver = ver;
    return 0;
}

int dl_check_step(const struct dl_choice *ch)
{
    if (S.next >= S.n)
        return 0;
    begin_line(ch, S.next++, -1, 0);
    return S.n - S.next;
}

int dl_check_step_buf(const struct dl_choice *ch, s32 len, u32 crc)
{
    if (S.next >= S.n)
        return 0;
    begin_line(ch, S.next++, len, len == -2 ? DL_R_TOO_BIG : len < 0 ? DL_R_MISSING
                                  : crc != ch->crc ? DL_R_CHANGED : 0);
    return S.n - S.next;
}

int dl_check_finish(void)
{
    int n = S.n;
    S.n = 0;
    if (S.next < n || begin_end() < 0) {
        finish();
        S.next = 0;
        return -1;
    }
    S.next = 0;
    return check_end(n, S.ver);
}

/* dl_check of one file already in buf (len bytes, read by the caller, its CRC the caller's):
 * the MODS page's check of a file it has just read, without reading it again. */
int dl_check_buf(u32 desc, const struct dl_choice *ch, s32 len, u8 *buf, u32 max,
                 struct dl_status *st, char (*det)[32], char (*ver)[DL_VER_MAX])
{
    if (begin_start(desc, 1, buf, max, st, det) <= 0) {
        finish();
        return -1;
    }
    begin_line(ch, 0, len, 0);
    if (begin_end() < 0) {
        finish();
        return -1;
    }
    return check_end(1, ver);
}

/* A check started with dl_check_start, given up: its memory back. */
void dl_check_cancel(void)
{
    finish();
    S.n = S.next = 0;
}

/* The boot's records, for n lines, in place of the last link's. -> 0 when the heap has none. */
static int records(int n)
{
    dl_free(dl_status);
    dl_free(dl_detail);
    dl_free(dl_inits);
    dl_free(dl_sites);
    dl_free(dl_site_orig);
    dl_sites = 0;
    dl_site_orig = 0;
    dl_applied_n = 0;
    dl_status_n = 0;
    dl_status = take((u32)n * sizeof *dl_status);
    dl_detail = take((u32)n * sizeof *dl_detail);
    dl_inits = take((u32)n * sizeof *dl_inits);
    if (dl_status && dl_detail && dl_inits) {
        dl_status_n = (u32)n;
        return 1;
    }
    dl_free(dl_status);
    dl_free(dl_detail);
    dl_free(dl_inits);
    dl_status = 0;
    dl_detail = 0;
    dl_inits = 0;
    return 0;
}

/* The heap's room for the loaded mods' sites and their stock bytes, kept from the boot on.
 * -> 0 when there is none. */
static int keep_sites(void)
{
    u32 n = 0, bytes = 0, r;
    int i;
    for (i = 0; i < L.nmods; i++) {
        const struct dmod *m = &L.mods[i];
        if (!m->alive)
            continue;
        for (r = 0; r < m->site_n; r++)
            if (L.site[m->site_first + r].data != NONE32) {
                n++;
                bytes += L.site[m->site_first + r].hi - L.site[m->site_first + r].lo;
            }
    }
    if (!n)
        return 1;
    dl_sites = dl_alloc(n * sizeof *dl_sites);
    dl_site_orig = dl_alloc(bytes);
    if (dl_sites && dl_site_orig)
        return 1;
    dl_free(dl_sites);
    dl_free(dl_site_orig);
    dl_sites = 0;
    dl_site_orig = 0;
    return 0;
}

/* The loaded mods' sites, in id order: each one's stock bytes kept, then its new bytes written
 * (dl_apply_site). The caller's dl_flush ends it. */
static void apply_sites(void)
{
    u32 r, orig = 0;
    int i;
    for (i = 0; i < L.nmods; i++) {
        const struct dmod *m = &L.mods[L.order[i]];
        if (!m->alive)
            continue;
        for (r = 0; r < m->site_n; r++) {
            const struct dsite *d = &L.site[m->site_first + r];
            struct dl_site *a;
            if (d->data == NONE32)
                continue;
            a = &dl_sites[dl_applied_n++];
            a->addr = d->lo;
            a->len = (u16)(d->hi - d->lo);
            a->line = m->choice;
            a->orig = orig;
            dl_memcpy(dl_site_orig + orig, DL_MEM(d->lo), a->len);
            orig += a->len;
            dl_apply_site(d->lo, L.sdata + d->data, a->len);
        }
    }
}

static int link_set(u32 desc, const struct dl_choice *choice, int n, u8 *buf, u32 max)
{
    int i, rounds, loaded = 0, b;
    u32 dirty_hi = 0;                    /* the highest end any pass 2 wrote to */
    const char *what;

    TRACE(1);
    b = begin(desc, choice, n, buf, max, dl_status, dl_detail);
    TRACE(2);
    if (b <= 0) {
        dl_skip_reason = b < 0 ? DL_SKIP_MEMORY : DL_SKIP_ABI;
        if (L.read_hi > L.area_lo)       /* pass 1 read into the area */
            dl_memset(DL_MEM(L.area_lo), 0, L.read_hi - L.area_lo);
        return 0;
    }
    dirty_hi = L.read_hi;                /* with the stage in the area, pass 1's reads */

    /* check, layout and pass 2, until a pass 2 refuses nothing */
    for (rounds = 0; rounds < 4 * DL_MAX_MODS; rounds++) {
        int restart = 0;
        settle();
        TRACE(3);
        if (!layout())
            continue;
        if (L.read_hi > dirty_hi)        /* an earlier round's reads at the top */
            dirty_hi = L.read_hi;
        if (!place_store()) {
            dl_skip_reason = DL_SKIP_MEMORY;
            for (i = 0; i < L.nmods; i++)
                if (L.mods[i].alive)
                    refuse(&L.mods[i], DL_R_LIMIT, 0, 0);
            if (dirty_hi > dl_used_lo)
                dl_memset(DL_MEM(dl_used_lo), 0, dirty_hi - dl_used_lo);
            dl_used_hi = dl_used_lo;
            return 0;
        }
        TRACE(4);
        L.nent = L.edata_n = L.nerel = 0;
        if (dirty_hi < dl_used_hi)       /* an earlier round's pass 2 may have written past */
            dirty_hi = dl_used_hi;       /* this layout's end: zero that too */
        if (dirty_hi > dl_used_lo)
            dl_memset(DL_MEM(dl_used_lo), 0, dirty_hi - dl_used_lo);
        for (i = 0; i < L.nplace && !restart; i++) {   /* in address order: a file at the top */
            struct dmod *m = &L.mods[L.place[i]];         /* never covers code already placed */
            s32 len;
            int code;
            if (!m->alive)
                continue;
            if (L.buf_at) {
                L.buf_at = stage_at(m);
                L.buf = DL_MEM(L.buf_at);
                L.max = L.area_hi - L.buf_at;
            }
            len = load(m, &choice[m->choice]);
            if (len < 0) {
                refuse(m, -len, 0, 0);
                restart = 1;
                break;
            }
            TRACE(0x100 | i);
            code = place(m, L.buf, (u32)len, &what);
            TRACE(0x200 | i);
            if (code) {
                refuse(m, code, 0, what);
                restart = 1;
            }
        }
        if (L.read_hi > dirty_hi)
            dirty_hi = L.read_hi;
        if (!restart)
            break;
    }
    if (L.buf_at && rounds < 4 * DL_MAX_MODS) {   /* every .bss, and the files' bytes above */
        u32 hi = dirty_hi > dl_used_hi ? dirty_hi : dl_used_hi;
        if (hi > L.runs_end)
            dl_memset(DL_MEM(L.runs_end), 0, hi - L.runs_end);
    }
    TRACE(5);
    b = rounds < 4 * DL_MAX_MODS && bind(0);
    if (!b || !keep_sites()) {           /* never settled, past the loader's room, or no heap
                                          * for the sites' stock bytes */
        dl_skip_reason = b ? DL_SKIP_MEMORY : DL_SKIP_UNSTABLE;
        for (i = 0; i < L.nmods; i++)
            if (L.mods[i].alive)
                refuse(&L.mods[i], DL_R_LIMIT, 0, 0);
        if (dirty_hi > dl_used_lo)
            dl_memset(DL_MEM(dl_used_lo), 0, dirty_hi - dl_used_lo);
        dl_used_hi = dl_used_lo;
#ifdef DL_KNOBS
        ds_knobs_in = 0;                 /* and the knob runs stay the old .store's */
#endif
        if (place_store())               /* nothing loads: every field parks */
            write_store();
        return 0;
    }
#ifdef DL_SITES
    if (!dl_commit_begin()) {            /* the host found no quiet moment: nothing is written */
        dl_skip_reason = DL_SKIP_BUSY;
        for (i = 0; i < L.nmods; i++)
            if (L.mods[i].alive)
                refuse(&L.mods[i], DL_R_BUSY, 0, 0);
        if (dirty_hi > dl_used_lo)
            dl_memset(DL_MEM(dl_used_lo), 0, dirty_hi - dl_used_lo);
        dl_used_hi = dl_used_lo;
        return 0;
    }
    bind(1);
    {
        u32 q;
        for (q = 0; q < L.nsites; q++)
            if (L.mods[L.sites[q].mod].alive) {
                dl_site_saving(L.sites[q].addr, L.sites[q].len);
                dl_memcpy(DL_MEM(L.sites[q].addr), L.tsdata + L.sites[q].data, L.sites[q].len);
            }
    }
    dl_commit_end();
#else
    bind(1);
#endif
    write_store();
    apply_sites();
    for (i = 0; i < L.nmods; i++) {
        struct dmod *m = &L.mods[L.order[i]];
        if (!m->alive)
            continue;
        set_status(m, DL_ST_LOADED, 0, 0, 0);
        loaded++;
        if (m->init_sec != 0xFF)
            dl_inits[dl_inits_n++] = (m->init_sec == SEC_RUN ? m->run_base : m->bss_base) + m->init_off;
    }
    if (!loaded) {                       /* a pass 2 that refused the last mod left its bytes */
        if (dirty_hi > dl_used_lo)
            dl_memset(DL_MEM(dl_used_lo), 0, dirty_hi - dl_used_lo);
        dl_used_hi = dl_used_lo;
    }
    return loaded;
}

/* The +Drive area [*lo, *hi) the descriptor's area record names. -> 0, or -1 when desc isn't
 * a descriptor the loader reads (*lo = *hi = 0). */
int dl_area(u32 desc, u32 *lo, u32 *hi)
{
    const u8 *d = DL_MEM(desc);
    *lo = *hi = 0;
    if (dl_rd32(d) != 0x454c4b44 || dl_rd16(d + 4) != 1)
        return -1;
    *lo = dl_rd32(d + dl_rd32(d + 0x3c) + 4);
    *hi = dl_rd32(d + dl_rd32(d + 0x3c) + 8);
    return 0;
}

int dl_link(u32 desc, const struct dl_choice *choice, int n, u8 *buf, u32 max)
{
    int loaded;
    dl_inits_n = 0;
    dl_used_lo = dl_used_hi = 0;
    ds_out_free(&dl_store_out);
    if (n < 0 || n > DL_MAX_MODS) {
        records(0);
        dl_skip_reason = DL_SKIP_COUNT;
        return 0;
    }
    if (!records(n)) {
        dl_skip_reason = DL_SKIP_MEMORY;
        return 0;
    }
    loaded = link_set(desc, choice, n, buf, max);
    finish();
    return loaded;
}
