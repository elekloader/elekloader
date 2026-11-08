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
 *
 * A refusal in the layout or in pass 2 (the area is full, a pc16 out of range) drops that
 * mod and starts again from the check, so a mod that needs it goes too. Memory goes only
 * through DL_MEM and files only through dl_read_file (dl.h), so the same code runs on the
 * computer in tests. */
#include "json.h"

/* ---- the link state ------------------------------------------------------------------------
 * The header lives in .bss; the mods and the pools come from the heap for each dl_link and
 * dl_check, sized to the set (the mods) and to the +Drive area (the pools, dl.h DL_POOL_*),
 * and go back when it returns. The merge's room is the descriptor's biggest table. */

enum { SEC_ABS, SEC_RUN, SEC_BSS, SEC_TABLE, SEC_COUNT };
enum { R_ABS32, R_PC32, R_PC16 };
#define FLASHED 0xFFFF
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

struct dmod {
    u16 choice;                          /* its line in /mods/active (its status record) */
    u8 alive, done;
    u16 id, version, core;               /* arena offsets (NONE: not given) */
    u32 run_len, run_align, bss_len, bss_align;
    u32 run_base, bss_base;
    struct list imports, weak, requires, conflicts, names;
    u16 exp_first, exp_n;                /* its exports in syms */
    u16 ctr_first, ctr_n;                /* its contributions */
    u8 init_sec;                         /* drive_init, if it has one */
    u32 init_off;
};

struct contrib {
    u16 table;                           /* the descriptor's table index */
    u16 order;
    u16 nent;                            /* entries it adds */
    u16 index;                           /* its place in the mod's contribute list */
};

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
    u16 order;
    u16 seq;
    const char *id;
    const u8 *data;
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
    u32 arena_n, lists_n, nsyms, nlocals, nctr, nent, edata_n, nerel;
    u32 flashed_syms;
    /* the pools' sizes (dl.h, DL_POOL_*; the merge's from the descriptor) */
    u32 ARENA, LISTS, SYMS, LOCALS, CONTRIBS, EDATA, ERELOCS, MERGE, MDATA;
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
    struct dl_status *st;                /* where the records go: dl_status, or a check's */
    char (*det)[32];
    u32 crc_table[256];
} L;

struct dl_status *dl_status;
char (*dl_detail)[32];
u32 dl_status_n;
u8 dl_skip_reason;
u32 *dl_inits;
u32 dl_inits_n;
u32 dl_used_lo, dl_used_hi;

/* ---- small helpers ---------------------------------------------------------------------- */
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
       K_RESOURCES, K_REQUIRES, K_CONFLICTS, K_N };
static const char *const KEYS[K_N] = {
    "elemod", "id", "version", "target", "sections", "symbols", "exports", "imports", "weak",
    "relocs", "sites", "collections", "contribute", "copied", "storage", "sharc",
    "resources", "requires", "conflicts",
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
                dl_memcpy(dst + total, DL_MEM((u32)a), (u32)k);
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

/* Pass 1: read one file into m. -> 0, or a refusal code (detail in *what). */
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
    for (i = 0; i < sizeof(nok) / sizeof(nok[0]); i++)
        if (!empty(k[nok[i]], buf, len)) {
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
                if (!empty(j.p, buf, len)) {
                    *what = "regions";
                    return DL_R_FEATURE;
                }
                js_skip(&j);
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

    /* relocations: in .run, of the three kinds, inside it */
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
            sz = js_eq(s, n, "abs32") || js_eq(s, n, "pc32") ? 4 : js_eq(s, n, "pc16") ? 2 : 0;
            if (!sz || v < 0 || (u32)v + sz > m->run_len)
                return DL_R_FORMAT;
            if (!js_next(&j, ']', &f2) || !js_skip(&j) || !js_next(&j, ']', &f2) || !js_skip(&j)
                || js_next(&j, ']', &f2))
                return DL_R_FORMAT;
        }
        if (j.err)
            return DL_R_FORMAT;
    }

    /* contributions: to a table that takes more, whole entries, no claims */
    m->ctr_first = (u16)L.nctr;
    m->ctr_n = 0;
    if (k[K_CONTRIBUTE]) {
        u16 index = 0;
        at(&j, k[K_CONTRIBUTE], buf, len);
        first = 1;
        if (!js_open(&j, '['))
            return DL_R_FORMAT;
        while (js_next(&j, ']', &first)) {
            int f2 = 1, t = -1;
            s32 order = 50;
            u32 dlen = 0, had_data = 0;
            struct contrib *c;
            if (!js_open(&j, '{'))
                return DL_R_FORMAT;
            while (js_next(&j, '}', &f2)) {
                if (!js_key(&j, &s, &n))
                    return DL_R_FORMAT;
                if (js_eq(s, n, "to")) {
                    if (!js_str(&j, &s, &n))
                        return DL_R_FORMAT;
                    t = find_table(s, n);
                    if (t < 0 || !(table_flags(t) & F_MORE) || (table_flags(t) & F_DSP)) {
                        static char tn[32];
                        u32 q = n < 31 ? n : 31;
                        dl_memcpy(tn, s, q);
                        tn[q] = 0;
                        *what = tn;
                        return DL_R_TABLE;
                    }
                } else if (js_eq(s, n, "order")) {
                    if (!js_int(&j, &order))
                        return DL_R_FORMAT;
                } else if (js_eq(s, n, "data")) {
                    if (!js_hex(&j, 0, 0, &dlen))
                        return DL_R_FORMAT;
                    had_data = 1;
                } else if (js_eq(s, n, "claims")) {
                    if (!empty(j.p, buf, len)) {
                        *what = "claims";
                        return DL_R_FEATURE;
                    }
                    js_skip(&j);
                } else if (!js_skip(&j)) {
                    return DL_R_FORMAT;
                }
            }
            if (j.err || t < 0 || !had_data)
                return DL_R_FORMAT;
            if (order < 0 || order > 0xFFFE)
                return DL_R_FORMAT;
            if (!dlen || dlen % table_entry(t)) {
                *what = table_name(t);
                return DL_R_TABLE;
            }
            if (L.nctr >= L.CONTRIBS)
                return DL_R_LIMIT;
            c = &L.ctr[L.nctr++];
            c->table = (u16)t;
            c->order = (u16)order;
            c->nent = (u16)(dlen / table_entry(t));
            c->index = index++;
            m->ctr_n++;
        }
        if (j.err)
            return DL_R_FORMAT;
    }

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
        for (q = 0; q < m->imports.n; q++) {
            const char *nm = L.arena + L.lists[m->imports.first + q];
            if (!known_global(nm) && !in_list(&m->weak, nm))
                break;
        }
        if (q < m->imports.n) {
            refuse(m, DL_R_IMPORT, 0, L.arena + L.lists[m->imports.first + q]);
            refused++;
            continue;
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

/* -> 1 when it fits; else the last living mod is refused and 0. */
static int layout(void)
{
    u32 a = L.area_lo;
    int i, last = -1;
    for (i = 0; i < L.nmods; i++) {
        struct dmod *m = &L.mods[L.order[i]];
        if (!m->alive)
            continue;
        last = i;
        if (m->run_len) {
            a = align(a, m->run_align < 4 ? 4 : m->run_align);
            m->run_base = a;
            a += m->run_len;
        } else {
            m->run_base = 0;
        }
    }
    a = align(a, 4);
    for (i = 0; i < L.nmods; i++) {
        struct dmod *m = &L.mods[L.order[i]];
        if (!m->alive)
            continue;
        if (m->bss_len) {
            a = align(a, m->bss_align < 4 ? 4 : m->bss_align);
            m->bss_base = a;
            a += m->bss_len;
        } else {
            m->bss_base = 0;
        }
    }
    a = align(a, 4);
    if (a > L.area_hi) {
        u32 over = a - L.area_hi;
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
    return js_eq(s, n, "abs32") ? R_ABS32 : js_eq(s, n, "pc32") ? R_PC32 : R_PC16;
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
    if (m->bss_len)
        dl_memset(DL_MEM(m->bss_base), 0, m->bss_len);
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
                    if (L.edata_n + (u32)c->nent * table_entry(c->table) > L.EDATA)
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
                        if (!js_eq(s, n, "abs32") && !js_eq(s, n, "pc32") && !js_eq(s, n, "pc16"))
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
                        if (off < 0 || (u32)off + (r->type == R_PC16 ? 2 : 4)
                                           > (u32)c->nent * table_entry(c->table))
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
    u32 t, i, k;
    for (t = 0; t < n_tables(); t++) {
        u32 esz = table_entry(t), cnt = table_count(t), n = 0, addr = table_addr(t);
        u8 *tab = DL_MEM(addr);
        int any = 0;
        for (i = 0; i < L.nent; i++)
            if (L.mods[L.ent[i].mod].alive && L.ctr[L.ent[i].ctr].table == t)
                any = 1;
        if (!any)
            continue;
        if ((cnt + 1) * esz > L.MDATA)
            return 0;
        dl_memcpy(L.mdata, tab, cnt * esz);
        for (i = 0; i < cnt; i++) {
            u32 key = table_key(t, i);
            struct item *it = &L.items[n++];
            it->order = (u16)(key >> 16);
            it->id = module_name(key & 0xFFFF);
            it->seq = (u16)i;
            it->data = L.mdata + i * esz;
            it->entry = NONE;
            it->sub = 0;
        }
        for (i = 0; i < L.nent; i++) {
            const struct entry *e = &L.ent[i];
            const struct contrib *c = &L.ctr[e->ctr];
            if (!L.mods[e->mod].alive || c->table != t)
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
            }
        }
        if (write)
            dl_memset(tab + n * esz, 0, (table_cap(t) + 1 - n) * esz);
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
static void finish(void)
{
    void **p[] = {(void **)&L.mods, (void **)&L.order, (void **)&L.arena, (void **)&L.lists,
                  (void **)&L.syms, (void **)&L.sym_next, (void **)&L.sym_head, (void **)&L.locals,
                  (void **)&L.ctr, (void **)&L.ent, (void **)&L.edata, (void **)&L.erel,
                  (void **)&L.items, (void **)&L.mdata};
    u32 i;
    for (i = 0; i < sizeof p / sizeof p[0]; i++) {
        dl_free(*p[i]);
        *p[i] = 0;
    }
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
    L.mods = take((u32)n * sizeof *L.mods);
    L.order = take((u32)n * sizeof *L.order);
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
    return L.mods && L.order && L.arena && L.lists && L.syms && L.sym_next && L.locals && L.ctr
           && L.ent && L.edata && L.erel && L.items && L.mdata;
}

/* ---- the whole link ------------------------------------------------------------------------ */
static s32 load(struct dmod *m, const struct dl_choice *ch)
{
    s32 len = dl_read_file(ch->name, L.buf, L.max);
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
    if (!pools(n))
        return -1;
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
    code = scan(m, L.buf, (u32)len, &what);
    if (code) {
        /* a file pass 1 refuses leaves nothing behind: its exports, its entries */
        set_status(m, DL_ST_REFUSED, code, 0, what);
        if (m->exp_n)
            L.nsyms = m->exp_first;
        if (m->ctr_n)
            L.nctr = m->ctr_first;
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
        while (check_round())
            ;
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

static int link_set(u32 desc, const struct dl_choice *choice, int n, u8 *buf, u32 max)
{
    int i, rounds, loaded = 0, b;
    u32 dirty_hi = 0;                    /* the highest end any pass 2 wrote to */
    const char *what;

    b = begin(desc, choice, n, buf, max, dl_status, dl_detail);
    if (b <= 0) {
        dl_skip_reason = b < 0 ? DL_SKIP_MEMORY : DL_SKIP_ABI;
        return 0;
    }

    /* check, layout and pass 2, until a pass 2 refuses nothing */
    for (rounds = 0; rounds < 4 * DL_MAX_MODS; rounds++) {
        int restart = 0;
        while (check_round())
            ;
        if (!layout())
            continue;
        L.nent = L.edata_n = L.nerel = 0;
        if (dirty_hi < dl_used_hi)       /* an earlier round's pass 2 may have written past */
            dirty_hi = dl_used_hi;       /* this layout's end: zero that too */
        if (dirty_hi > dl_used_lo)
            dl_memset(DL_MEM(dl_used_lo), 0, dirty_hi - dl_used_lo);
        for (i = 0; i < L.nmods && !restart; i++) {
            struct dmod *m = &L.mods[L.order[i]];
            s32 len;
            int code;
            if (!m->alive)
                continue;
            len = load(m, &choice[m->choice]);
            if (len < 0) {
                refuse(m, -len, 0, 0);
                restart = 1;
                break;
            }
            code = place(m, buf, (u32)len, &what);
            if (code) {
                refuse(m, code, 0, what);
                restart = 1;
            }
        }
        if (!restart)
            break;
    }
    if (rounds >= 4 * DL_MAX_MODS || !bind(0)) {
        dl_skip_reason = DL_SKIP_UNSTABLE;
        for (i = 0; i < L.nmods; i++)
            if (L.mods[i].alive)
                refuse(&L.mods[i], DL_R_LIMIT, 0, 0);
        if (dirty_hi > dl_used_lo)
            dl_memset(DL_MEM(dl_used_lo), 0, dirty_hi - dl_used_lo);
        dl_used_hi = dl_used_lo;
        return 0;
    }
    bind(1);
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

int dl_link(u32 desc, const struct dl_choice *choice, int n, u8 *buf, u32 max)
{
    int loaded;
    dl_inits_n = 0;
    dl_used_lo = dl_used_hi = 0;
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
