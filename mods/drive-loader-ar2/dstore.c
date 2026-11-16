/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Drive Loader: +Drive storage, the boot allocator and /mods/.store (dstore.h). Each function
 * names the one of elekloader/drive_store.py it follows; the lists keep that file's orders, so the
 * same inputs give the same bytes. */
#include "dstore.h"

const char *const ds_what[DS_W_N] = {
    "sound", "kit", "global", "track", "pattern", "project", "codes",
    "core_lock_codes", "core_field_inits", "core_handles",
};

#define HEAD 0x28
#define SCOPE_REC 8
#define KIT 2                            /* the kit scope's number */
static const u8 MAGIC[4] = {'D', 'L', 'S', 'T'};
static const u8 ID_HASH[8] = {'E', 'L', 'K', 'D', 'R', 'I', 'V', 'E'};

/* the device's CC pool in the order fields get them, and those a kit field gets none of
 * (store.DEVICES['analog-rytm-mk2']) */
static const u8 CC_POOL[] = {8, 9, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63,
                             65, 66, 67, 68, 69, 84, 85, 110, 111, 112, 113, 114, 115, 116, 119};
#define CC_POOL_N (sizeof CC_POOL)
static int fx_taken(u32 cc) { return cc == 84 || cc == 85; }

/* ---- names ----------------------------------------------------------------------------- */
static int keycmp(const char *am, const char *as, const char *bm, const char *bs)
{
    int c = dl_strcmp(am, bm);
    return c ? c : dl_strcmp(as, bs);
}

static int keyeq(const char *am, const char *as, const char *bm, const char *bs)
{
    return !dl_strcmp(am, bm) && !dl_strcmp(as, bs);
}

/* Python's strict UTF-8 */
static int utf8_ok(const u8 *p, u32 n)
{
    u32 i = 0;
    while (i < n) {
        u8 c = p[i];
        u32 k, need;
        u8 lo = 0x80, hi = 0xBF;
        if (c < 0x80) {
            if (!c)
                return 0;                /* a NUL: the loader's names end there */
            i++;
            continue;
        }
        if (c >= 0xC2 && c <= 0xDF)
            need = 1;
        else if (c >= 0xE0 && c <= 0xEF) {
            need = 2;
            if (c == 0xE0)
                lo = 0xA0;
            if (c == 0xED)
                hi = 0x9F;
        } else if (c >= 0xF0 && c <= 0xF4) {
            need = 3;
            if (c == 0xF0)
                lo = 0x90;
            if (c == 0xF4)
                hi = 0x8F;
        } else {
            return 0;
        }
        if (i + need >= n)
            return 0;
        for (k = 1; k <= need; k++) {
            u8 d = p[i + k];
            if (k == 1 ? (d < lo || d > hi) : (d < 0x80 || d > 0xBF))
                return 0;
        }
        i += need + 1;
    }
    return 1;
}

static u32 pad4(u32 n) { return (n + 3) & ~3u; }

/* ---- the core's LFO destination fields (drive_store.Base.lfo_dst, dst_fields) ------------ */
static const char *const DST_SYM[2] = {"core_lfo_dst", "core_fx_lfo_dst"};

/* Does the flashed build place the core's LFO destination field for this scope (1 sound, 2 kit)? It
 * does when one of its own fields of the scope is an LFO destination. */
static int lfo_dst(const struct ds_base *b, u32 scope)
{
    u32 i;
    for (i = 0; i < b->ncodes; i++)
        if ((dl_rd32(b->codes + 8 * i + 4) & DS_LFO_FLAG) && dl_rd32(b->codes + 8 * i) >> 28 == scope)
            return 1;
    return 0;
}

/* The core's LFO destination fields the +Drive places for w: for each scope with an "lfo" field, where
 * the core keeps the field live and the flashed build does not place it. -> how many, into x[0..1]. */
static u32 dst_fields(const struct ds_base *b, const struct ds_want *w, u32 n, struct ds_want *x)
{
    u32 s, i, k = 0;
    if (!b->live_dst || !b->core)
        return 0;
    for (s = 1; s <= 2; s++) {
        if (lfo_dst(b, s))
            continue;
        for (i = 0; i < n && !(w[i].scope == s && (w[i].flags & DS_LFO)); i++)
            ;
        if (i == n)
            continue;
        dl_memset(&x[k], 0, sizeof x[k]);
        x[k].mod = b->core;
        x[k].sym = DST_SYM[s - 1];
        x[k].scope = (u8)s;
        x[k].bits = DS_LFO_DST_BITS;
        k++;
    }
    return k;
}

/* ---- fit (drive_store.fit, _needs) ------------------------------------------------------ */
static int uses(const struct ds_want *e, u32 what)
{
    if (what < DS_SCOPES)
        return e->scope == what + 1;
    if (what == DS_W_CODES || what == DS_W_CODES_TABLE)
        return (e->flags & DS_LOCK) != 0;
    if (what == DS_W_INITS_TABLE)
        return e->init != 0;
    return (e->flags & DS_HANDLE) != 0;
}

int ds_fit_one(const struct ds_base *b, const struct ds_want *w, u32 n, const u8 *off, struct ds_short *s)
{
    u32 what, i, nx = 0;
    struct ds_want x[2];
    if (b->live_dst) {                   /* the core's LFO fields, for the living set's "lfo" fields */
        struct ds_want *a = dl_alloc((n + 1) * sizeof *a);
        u32 m = 0;
        if (a) {
            for (i = 0; i < n; i++)
                if (!(off && off[i]))
                    a[m++] = w[i];
            nx = dst_fields(b, a, m, x);
            dl_free(a);
        }
    }
    for (what = 0; what < DS_W_N; what++) {
        s32 need = 0, have, sp;
        int victim = -1;
        for (i = 0; i < n; i++) {
            if ((off && off[i]) || !uses(&w[i], what))
                continue;
            need += what < DS_SCOPES ? w[i].bits : 1;
            victim = (int)i;
        }
        for (i = 0; i < nx; i++)
            if (uses(&x[i], what))
                need += x[i].bits;
        if (what < DS_SCOPES) {
            have = (s32)b->room[what] - (s32)b->used[what];
        } else if (what == DS_W_CODES) {
            have = DS_LAST_CODE - DS_FIRST_CODE + 1 - (s32)b->ncodes;
        } else {
            sp = b->spare[what == DS_W_CODES_TABLE ? DS_T_CODES : what == DS_W_INITS_TABLE ? DS_T_INITS
                                                                                            : DS_T_HANDLES];
            have = sp < 0 ? 0 : sp;
            if (what == DS_W_INITS_TABLE && sp >= 0 && DS_MAX_INITS - (s32)b->inits < have)
                have = DS_MAX_INITS - (s32)b->inits;
        }
        if (need > have && victim >= 0) {
            s->victim = (u32)victim;
            s->what = what;
            s->need = need;
            s->have = have;
            return 1;
        }
    }
    return 0;
}

/* ---- the state: /mods/.store (drive_store.fresh, encode, decode, forget) ---------------- */
static struct ds_state *fresh(const struct ds_base *b)
{
    struct ds_state *st = dl_alloc(sizeof *st);
    u32 s;
    if (!st)
        return 0;
    st->flashed_id = st->id = b->flashed_id;
    for (s = 0; s < DS_SCOPES; s++) {
        st->fgen[s] = st->gen[s] = (u8)b->gen[s];
        st->low[s] = b->room[s];
    }
    st->next_code = DS_LAST_CODE;
    return st;
}

void ds_state_free(struct ds_state *st)
{
    if (st)
        dl_free(st->names);
    dl_free(st);
}

static u32 crc_of(const u8 *p, u32 n)
{
    static const u8 z[4];
    u32 c = 0xFFFFFFFF;
    c = dl_crc32_add(c, p, 0x0c);
    c = dl_crc32_add(c, z, 4);
    c = dl_crc32_add(c, p + 0x10, n - 0x10);
    return ~c;
}

u8 *ds_encode(const struct ds_state *st, u32 *n)
{
    const struct ds_tag *tl[4] = {st->cd, st->fcd, st->cc, st->fcc};
    u32 tn[4] = {st->ncd, st->nfcd, st->ncc, st->nfcc};
    u32 size = HEAD + SCOPE_REC * DS_SCOPES + 4 * st->nids, i, k, at;
    u8 *p;
    for (k = 0; k < 2; k++)
        for (i = 0; i < (k ? st->npk : st->nf); i++) {
            const struct ds_field *f = k ? &st->pk[i] : &st->f[i];
            u32 lm = dl_strlen(f->mod), ls = dl_strlen(f->sym);
            if (!lm || !ls || lm > DS_NAME_MAX || ls > DS_NAME_MAX)
                return 0;
            size += 12 + pad4(lm + ls);
        }
    for (k = 0; k < 4; k++)
        for (i = 0; i < tn[k]; i++) {
            u32 lm = dl_strlen(tl[k][i].mod), ls = dl_strlen(tl[k][i].sym);
            if (!lm || !ls || lm > DS_NAME_MAX || ls > DS_NAME_MAX)
                return 0;
            size += 4 + pad4(lm + ls);
        }
    p = dl_alloc(size);
    if (!p)
        return 0;
    dl_memcpy(p, MAGIC, 4);
    dl_wr16(p + 4, 1);
    dl_wr32(p + 8, size);
    dl_wr32(p + 0x10, st->flashed_id);
    dl_wr32(p + 0x14, st->id);
    p[0x18] = st->next_code;
    p[0x19] = (u8)st->nids;
    dl_wr16(p + 0x1a, st->nf);
    dl_wr16(p + 0x1c, st->npk);
    dl_wr16(p + 0x1e, st->ncd);
    dl_wr16(p + 0x20, st->nfcd);
    dl_wr16(p + 0x22, st->ncc);
    dl_wr16(p + 0x24, st->nfcc);
    at = HEAD;
    for (i = 0; i < DS_SCOPES; i++, at += SCOPE_REC) {
        p[at] = st->fgen[i];
        p[at + 1] = st->gen[i];
        dl_wr32(p + at + 4, st->low[i]);
    }
    for (i = 0; i < st->nids; i++, at += 4)
        dl_wr32(p + at, st->ids[i]);
    for (k = 0; k < 2; k++)
        for (i = 0; i < (k ? st->npk : st->nf); i++) {
            const struct ds_field *f = k ? &st->pk[i] : &st->f[i];
            u32 lm = dl_strlen(f->mod), ls = dl_strlen(f->sym);
            p[at] = f->scope;
            p[at + 1] = f->bits;
            dl_wr16(p + at + 2, f->off);
            dl_wr32(p + at + 4, f->init);
            p[at + 8] = (u8)lm;
            p[at + 9] = (u8)ls;
            dl_memcpy(p + at + 12, f->mod, lm);
            dl_memcpy(p + at + 12 + lm, f->sym, ls);
            at += 12 + pad4(lm + ls);
        }
    for (k = 0; k < 4; k++)
        for (i = 0; i < tn[k]; i++) {
            u32 lm = dl_strlen(tl[k][i].mod), ls = dl_strlen(tl[k][i].sym);
            p[at] = tl[k][i].v;
            p[at + 1] = (u8)lm;
            p[at + 2] = (u8)ls;
            dl_memcpy(p + at + 4, tl[k][i].mod, lm);
            dl_memcpy(p + at + 4 + lm, tl[k][i].sym, ls);
            at += 4 + pad4(lm + ls);
        }
    dl_wr32(p + 0x0c, crc_of(p, size));
    *n = size;
    return p;
}

/* names at p (lm and ls bytes) -> the arena, NUL-terminated. -> 0 when bad. */
static int names(const u8 *raw, u32 n, u32 at, u32 lm, u32 ls, char **arena, const char **m, const char **s)
{
    if (!lm || !ls || at + lm + ls > n || !utf8_ok(raw + at, lm) || !utf8_ok(raw + at + lm, ls))
        return 0;
    dl_memcpy(*arena, raw + at, lm);
    (*arena)[lm] = 0;
    *m = *arena;
    *arena += lm + 1;
    dl_memcpy(*arena, raw + at + lm, ls);
    (*arena)[ls] = 0;
    *s = *arena;
    *arena += ls + 1;
    return 1;
}

struct ds_state *ds_decode(const u8 *p, u32 n)
{
    struct ds_state *st;
    struct ds_tag *tl[4];
    u32 tn[4], i, k, at, size;
    char *arena;
    if (!p || n < HEAD + SCOPE_REC * DS_SCOPES || n > DS_MAX_FILE)
        return 0;
    size = dl_rd32(p + 8);
    if (p[0] != 'D' || p[1] != 'L' || p[2] != 'S' || p[3] != 'T' || dl_rd16(p + 4) != 1 || size != n)
        return 0;
    if (crc_of(p, n) != dl_rd32(p + 0x0c))
        return 0;
    st = dl_alloc(sizeof *st);
    if (!st)
        return 0;
    st->names = dl_alloc(2 * n);         /* every name with its NUL: fewer bytes than the file */
    if (!st->names)
        goto bad;
    arena = st->names;
    st->flashed_id = dl_rd32(p + 0x10);
    st->id = dl_rd32(p + 0x14);
    st->next_code = p[0x18];
    st->nids = p[0x19];
    st->nf = dl_rd16(p + 0x1a);
    st->npk = dl_rd16(p + 0x1c);
    st->ncd = dl_rd16(p + 0x1e);
    st->nfcd = dl_rd16(p + 0x20);
    st->ncc = dl_rd16(p + 0x22);
    st->nfcc = dl_rd16(p + 0x24);
    if (st->nf > DS_MAX_FIELDS || st->npk > DS_MAX_FIELDS || st->ncd > DS_MAX_TAGS || st->nfcd > DS_MAX_TAGS
        || st->ncc > DS_MAX_TAGS || st->nfcc > DS_MAX_TAGS)
        goto bad;
    at = HEAD;
    for (i = 0; i < DS_SCOPES; i++, at += SCOPE_REC) {
        st->fgen[i] = p[at];
        st->gen[i] = p[at + 1];
        st->low[i] = dl_rd32(p + at + 4);
    }
    if (at + 4 * st->nids > n)
        goto bad;
    for (i = 0; i < st->nids; i++, at += 4)
        st->ids[i] = dl_rd32(p + at);
    for (k = 0; k < 2; k++)
        for (i = 0; i < (k ? st->npk : st->nf); i++) {
            struct ds_field *f = k ? &st->pk[i] : &st->f[i];
            u32 lm, ls;
            if (at + 12 > n)
                goto bad;
            f->scope = p[at];
            f->bits = p[at + 1];
            f->off = dl_rd16(p + at + 2);
            f->init = dl_rd32(p + at + 4);
            lm = p[at + 8];
            ls = p[at + 9];
            if (!names(p, n, at + 12, lm, ls, &arena, &f->mod, &f->sym))
                goto bad;
            if (f->scope < 1 || f->scope > DS_SCOPES || f->bits < 1 || f->bits > DS_MAX_BITS
                || (f->bits < 32 && f->init >> f->bits))
                goto bad;
            f->placed = 1;
            at += 12 + pad4(lm + ls);
        }
    tl[0] = st->cd;
    tl[1] = st->fcd;
    tl[2] = st->cc;
    tl[3] = st->fcc;
    tn[0] = st->ncd;
    tn[1] = st->nfcd;
    tn[2] = st->ncc;
    tn[3] = st->nfcc;
    for (k = 0; k < 4; k++)
        for (i = 0; i < tn[k]; i++) {
            u32 lm, ls;
            if (at + 4 > n)
                goto bad;
            tl[k][i].v = p[at];
            lm = p[at + 1];
            ls = p[at + 2];
            if (!names(p, n, at + 4, lm, ls, &arena, &tl[k][i].mod, &tl[k][i].sym))
                goto bad;
            at += 4 + pad4(lm + ls);
        }
    if (at != size)
        goto bad;
    return st;
bad:
    ds_state_free(st);
    return 0;
}

static u32 drop_fields(struct ds_field *a, u32 n, const char *mod)
{
    u32 i, k = 0;
    for (i = 0; i < n; i++)
        if (dl_strcmp(a[i].mod, mod))
            a[k++] = a[i];
    return k;
}

static u32 drop_tags(struct ds_tag *a, u32 n, const char *mod)
{
    u32 i, k = 0;
    for (i = 0; i < n; i++)
        if (dl_strcmp(a[i].mod, mod))
            a[k++] = a[i];
    return k;
}

int ds_forget(struct ds_state *st, const char *mod)
{
    u32 a = st->npk, b = st->nfcd, c = st->nfcc;
    st->npk = drop_fields(st->pk, st->npk, mod);
    st->nfcd = drop_tags(st->fcd, st->nfcd, mod);
    st->nfcc = drop_tags(st->fcc, st->nfcc, mod);
    return a != st->npk || b != st->nfcd || c != st->nfcc;
}

/* ---- the layout ID (drive_store.layout_id) ---------------------------------------------- */
struct item { u32 scope, off; const void *p; };

static u32 layout_id(const struct ds_state *st, const struct ds_base *b, int *err)
{
    u32 size = 8 + 4 + DS_SCOPES, i, k, at, nf = st->nf + st->npk, nc = st->ncd + st->nfcd, id;
    const struct ds_field **fs;
    const struct ds_tag **cs;
    u8 *h, dg[32];
    int same = 1;
    *err = 0;
    for (i = 0; i < DS_SCOPES; i++)
        if (st->gen[i] != b->gen[i])
            same = 0;
    if (!nf && !nc && same)
        return b->flashed_id;
    fs = dl_alloc((nf + 1) * sizeof *fs);
    cs = dl_alloc((nc + 1) * sizeof *cs);
    for (i = 0; i < nf; i++) {
        const struct ds_field *f = i < st->nf ? &st->f[i] : &st->pk[i - st->nf];
        size += 8 + 2 + dl_strlen(f->mod) + dl_strlen(f->sym);
    }
    for (i = 0; i < nc; i++) {
        const struct ds_tag *c = i < st->ncd ? &st->cd[i] : &st->fcd[i - st->ncd];
        size += 3 + dl_strlen(c->mod) + dl_strlen(c->sym);
    }
    h = dl_alloc(size);
    if (!fs || !cs || !h) {
        dl_free(fs);
        dl_free(cs);
        dl_free(h);
        *err = 1;
        return 0;
    }
    /* fields then parked fields, a stable sort by (scope, offset); codes then freed codes by code */
    for (i = 0; i < nf; i++) {
        const struct ds_field *x = i < st->nf ? &st->f[i] : &st->pk[i - st->nf];
        k = i;
        while (k > 0 && (fs[k - 1]->scope > x->scope || (fs[k - 1]->scope == x->scope && fs[k - 1]->off > x->off))) {
            fs[k] = fs[k - 1];
            k--;
        }
        fs[k] = x;
    }
    for (i = 0; i < nc; i++) {
        const struct ds_tag *x = i < st->ncd ? &st->cd[i] : &st->fcd[i - st->ncd];
        k = i;
        while (k > 0 && cs[k - 1]->v > x->v) {
            cs[k] = cs[k - 1];
            k--;
        }
        cs[k] = x;
    }
    dl_memcpy(h, ID_HASH, 8);
    dl_wr32(h + 8, b->flashed_id);
    at = 12;
    for (i = 0; i < DS_SCOPES; i++)
        h[at++] = st->gen[i];
    for (i = 0; i < nf; i++) {
        u32 lm = dl_strlen(fs[i]->mod), ls = dl_strlen(fs[i]->sym);
        h[at] = fs[i]->scope;
        h[at + 1] = fs[i]->bits;
        dl_wr16(h + at + 2, fs[i]->off);
        dl_wr32(h + at + 4, fs[i]->init);
        at += 8;
        h[at++] = (u8)lm;
        dl_memcpy(h + at, fs[i]->mod, lm);
        at += lm;
        h[at++] = (u8)ls;
        dl_memcpy(h + at, fs[i]->sym, ls);
        at += ls;
    }
    for (i = 0; i < nc; i++) {
        u32 lm = dl_strlen(cs[i]->mod), ls = dl_strlen(cs[i]->sym);
        h[at++] = cs[i]->v;
        h[at++] = (u8)lm;
        dl_memcpy(h + at, cs[i]->mod, lm);
        at += lm;
        h[at++] = (u8)ls;
        dl_memcpy(h + at, cs[i]->sym, ls);
        at += ls;
    }
    dl_sha256(h, at, dg);
    dl_free(fs);
    dl_free(cs);
    dl_free(h);
    id = dl_rd32(dg);
    return id ? id : 1;
}

/* ---- the allocator (drive_store.allocate) ----------------------------------------------- */
static u8 bump(u32 g) { return g >= 255 ? 1 : (u8)(g + 1); }

static const struct ds_want *want_of(const struct ds_want *w, u32 n, const char *mod, const char *sym)
{
    u32 i;
    for (i = 0; i < n; i++)
        if (keyeq(w[i].mod, w[i].sym, mod, sym))
            return &w[i];
    return 0;
}

static int field_eq(const struct ds_field *a, const struct ds_field *b)
{
    return keyeq(a->mod, a->sym, b->mod, b->sym) && a->scope == b->scope && a->off == b->off
           && a->bits == b->bits && a->init == b->init;
}

static void remove_at(void *a, u32 size, u32 *n, u32 i)
{
    u8 *p = a;
    dl_memcpy(p + i * size, p + (i + 1) * size, (*n - i - 1) * size);   /* forward copy: safe */
    (*n)--;
}

/* the allocator's working lists */
struct work {
    struct ds_field fl[DS_MAX_FIELDS];   /* kept, then added (fields) */
    struct ds_field gone[DS_MAX_FIELDS];
    u8 back[DS_MAX_FIELDS];              /* fl[i] came back to its parked place */
    u8 kept[DS_MAX_FIELDS];
    struct ds_tag t[DS_MAX_TAGS], fr[2 * DS_MAX_TAGS];
    u32 a2[2 * DS_MAX_FIELDS], e2[2 * DS_MAX_FIELDS];
    u32 idx[DS_MAX_FIELDS], dsc[DS_MAX_FIELDS], ids[256];
};

/* Do a scope's held fields (live and parked) lie in the pool, at or above the low mark, apart? */
static int held_ok(const struct ds_state *st, struct work *W, u32 s, u32 low, u32 room, u32 used)
{
    u32 i, k, n = 0, *a = W->a2, *e = W->e2;
    for (i = 0; i < st->nf + st->npk; i++) {
        const struct ds_field *f = i < st->nf ? &st->f[i] : &st->pk[i - st->nf];
        u32 x, y;
        if (f->scope != s + 1)
            continue;
        x = f->off;
        y = f->off + f->bits;
        for (k = n; k > 0 && (a[k - 1] > x || (a[k - 1] == x && e[k - 1] > y)); k--) {
            a[k] = a[k - 1];
            e[k] = e[k - 1];
        }
        a[k] = x;
        e[k] = y;
        n++;
    }
    for (i = 0; i < n; i++) {
        if (!(used <= a[i] && e[i] <= room && low <= a[i]))
            return 0;
        if (i && a[i] < e[i - 1])
            return 0;
    }
    return 1;
}


static int sorted_by_key(const struct ds_field *a, const struct ds_field *b)
{
    return keycmp(a->mod, a->sym, b->mod, b->sym) < 0;
}

static void repack(struct ds_state *st, const struct ds_base *b, struct work *W, u32 nf, u32 s, u8 gen, u8 *repacked)
{
    u32 i, k, at = b->room[s], n = 0, *idx = W->idx;
    st->gen[s] = gen;
    for (i = 0, k = 0; i < st->npk; i++)
        if (st->pk[i].scope != s + 1)
            st->pk[k++] = st->pk[i];
    st->npk = k;
    for (i = 0; i < nf; i++) {           /* this scope's fields by (mod, sym) */
        if (W->fl[i].scope != s + 1)
            continue;
        for (k = n; k > 0 && sorted_by_key(&W->fl[i], &W->fl[idx[k - 1]]); k--)
            idx[k] = idx[k - 1];
        idx[k] = i;
        n++;
    }
    for (i = 0; i < n; i++) {
        at -= W->fl[idx[i]].bits;
        W->fl[idx[i]].off = (u16)at;
        W->fl[idx[i]].placed = 1;
    }
    st->low[s] = at;
    repacked[s] = 1;
}

static int in_kept(const struct work *W, u32 nkept, const char *m, const char *s)
{
    u32 i;
    for (i = 0; i < nkept; i++)
        if (keyeq(W->fl[i].mod, W->fl[i].sym, m, s))
            return 1;
    return 0;
}

/* _codes: each lockable field its code, top down, past the build's codes */
static int codes(struct ds_state *st, const struct ds_base *b, struct work *W, const struct ds_want *w, u32 n,
                 u32 nkept, u32 nf)
{
    u8 flashed[256];
    s32 top = DS_FIRST_CODE - 1, nxt;
    u32 i, k, nt = 0, nfr = 0;
    dl_memset(flashed, 0, sizeof flashed);
    for (i = 0; i < b->ncodes; i++) {
        u32 c = dl_rd32(b->codes + 8 * i + 4) & 0xFF;
        flashed[c] = 1;
        if ((s32)c > top)
            top = (s32)c;
    }
    for (i = 0; i < st->nfcd; i++)
        if (!flashed[st->fcd[i].v])
            W->fr[nfr++] = st->fcd[i];
    for (i = 0; i < st->ncd; i++) {
        const struct ds_tag *c = &st->cd[i];
        const struct ds_want *x = want_of(w, n, c->mod, c->sym);
        if (flashed[c->v])
            continue;
        if (x && (x->flags & DS_LOCK) && in_kept(W, nkept, c->mod, c->sym))
            W->t[nt++] = *c;
        else
            W->fr[nfr++] = *c;
    }
    nxt = st->next_code;
    for (;;) {                           /* the wanted keys not coded yet, in key order */
        const struct ds_want *x = 0;
        struct ds_tag c;
        int j = -1, back = 0;
        for (i = 0; i < n; i++) {
            if (!(w[i].flags & DS_LOCK))
                continue;
            for (k = 0; k < nt && !keyeq(W->t[k].mod, W->t[k].sym, w[i].mod, w[i].sym); k++)
                ;
            if (k < nt)
                continue;
            if (!x || keycmp(w[i].mod, w[i].sym, x->mod, x->sym) < 0)
                x = &w[i];
        }
        if (!x)
            break;
        for (i = 0; i < nf; i++)
            if (W->back[i] && keyeq(W->fl[i].mod, W->fl[i].sym, x->mod, x->sym))
                back = 1;
        for (i = 0; i < nfr; i++)
            if (keyeq(W->fr[i].mod, W->fr[i].sym, x->mod, x->sym))
                j = (int)i;
        c.mod = x->mod;
        c.sym = x->sym;
        if (j >= 0 && back) {
            u32 q;
            c = W->fr[j];
            for (q = 0; !(keyeq(W->fr[q].mod, W->fr[q].sym, c.mod, c.sym) && W->fr[q].v == c.v); q++)
                ;
            remove_at(W->fr, sizeof W->fr[0], &nfr, q);
        } else if (nxt > top) {
            c.v = (u8)nxt--;
        } else if (nfr) {
            c.v = W->fr[0].v;
            remove_at(W->fr, sizeof W->fr[0], &nfr, 0);
        } else {
            s32 v;
            for (v = DS_LAST_CODE; v >= DS_FIRST_CODE; v--) {
                if (flashed[v])
                    continue;
                for (k = 0; k < nt && W->t[k].v != v; k++)
                    ;
                if (k == nt)
                    break;
            }
            if (v < DS_FIRST_CODE)
                return 0;
            c.v = (u8)v;
        }
        for (i = 0, k = 0; i < nfr; i++)
            if (!keyeq(W->fr[i].mod, W->fr[i].sym, x->mod, x->sym))
                W->fr[k++] = W->fr[i];
        nfr = k;
        if (nt >= DS_MAX_TAGS)
            return 0;
        W->t[nt++] = c;
    }
    for (i = 1; i < nt; i++) {           /* by code, stable */
        struct ds_tag c = W->t[i];
        for (k = i; k > 0 && W->t[k - 1].v > c.v; k--)
            W->t[k] = W->t[k - 1];
        W->t[k] = c;
    }
    dl_memcpy(st->cd, W->t, nt * sizeof W->t[0]);
    st->ncd = nt;
    if (nfr > DS_MAX_TAGS)
        return 0;
    dl_memcpy(st->fcd, W->fr, nfr * sizeof W->fr[0]);
    st->nfcd = nfr;
    st->next_code = (u8)(nxt > DS_FIRST_CODE - 1 ? nxt : DS_FIRST_CODE - 1);
    return 1;
}

static int is_kit(const struct ds_state *st, const char *m, const char *s)
{
    u32 i;
    for (i = 0; i < st->nf; i++)
        if (st->f[i].scope == KIT && keyeq(st->f[i].mod, st->f[i].sym, m, s))
            return 1;
    return 0;
}

static int cc_flashed(const struct ds_base *b, u32 cc)
{
    u32 i;
    for (i = 0; i < b->nccs; i++)
        if (dl_rd32(b->ccs + 8 * i + 4) == cc)
            return 1;
    return 0;
}

/* _ccs: each field with "cc" its CC from the pool, from its end, past the build's own */
static int ccs(struct ds_state *st, const struct ds_base *b, struct work *W, const struct ds_want *w, u32 n)
{
    u8 unused[CC_POOL_N];
    u32 nun = 0, i, k, nt = 0, nfr = 0, room = b->spare[DS_T_CCS] > 0 ? (u32)b->spare[DS_T_CCS] : 0;
    for (i = 0; i < st->nfcc; i++)
        if (!cc_flashed(b, st->fcc[i].v))
            W->fr[nfr++] = st->fcc[i];
    for (i = 0; i < st->ncc; i++) {
        const struct ds_tag *c = &st->cc[i];
        const struct ds_want *x = want_of(w, n, c->mod, c->sym);
        if (cc_flashed(b, c->v))
            continue;
        if (x && (x->flags & DS_CC) && !(is_kit(st, c->mod, c->sym) && fx_taken(c->v)))
            W->t[nt++] = *c;
        else
            W->fr[nfr++] = *c;
    }
    for (i = CC_POOL_N; i-- > 0;) {      /* the pool from its end, less the build's and the held */
        u8 cc = CC_POOL[i];
        if (cc_flashed(b, cc))
            continue;
        for (k = 0; k < nt && W->t[k].v != cc; k++)
            ;
        if (k < nt)
            continue;
        for (k = 0; k < nfr && W->fr[k].v != cc; k++)
            ;
        if (k < nfr)
            continue;
        unused[nun++] = cc;
    }
    {
        /* the wanted keys without a CC, in key order: each once */
        u32 *order = W->idx, no = 0;
        for (i = 0; i < n; i++) {
            if (!(w[i].flags & DS_CC))
                continue;
            for (k = 0; k < nt && !keyeq(W->t[k].mod, W->t[k].sym, w[i].mod, w[i].sym); k++)
                ;
            if (k < nt)
                continue;
            for (k = no; k > 0 && keycmp(w[i].mod, w[i].sym, w[order[k - 1]].mod, w[order[k - 1]].sym) < 0; k--)
                order[k] = order[k - 1];
            order[k] = i;
            no++;
        }
        for (i = 0; i < no; i++) {
            const struct ds_want *x = &w[order[i]];
            int kit = is_kit(st, x->mod, x->sym), j = -1, o = -1, nw = -1;
            struct ds_tag c;
            u32 q;
            for (q = 0; q < nfr; q++) {
                int ok = !kit || !fx_taken(W->fr[q].v);
                if (ok && keyeq(W->fr[q].mod, W->fr[q].sym, x->mod, x->sym))
                    j = (int)q;
                if (ok && o < 0)
                    o = (int)q;
            }
            for (q = 0; q < nun && nw < 0; q++)
                if (!kit || !fx_taken(unused[q]))
                    nw = (int)q;
            if (nt >= room)
                continue;
            c.mod = x->mod;
            c.sym = x->sym;
            if (j >= 0) {
                c = W->fr[j];
                for (q = 0; !(keyeq(W->fr[q].mod, W->fr[q].sym, c.mod, c.sym) && W->fr[q].v == c.v); q++)
                    ;
                remove_at(W->fr, sizeof W->fr[0], &nfr, q);
            } else if (nw >= 0) {
                c.v = unused[nw];
                for (q = (u32)nw; q + 1 < nun; q++)
                    unused[q] = unused[q + 1];
                nun--;
            } else if (o >= 0) {
                c.v = W->fr[o].v;
                remove_at(W->fr, sizeof W->fr[0], &nfr, (u32)o);
            } else {
                continue;
            }
            W->t[nt++] = c;
        }
    }
    for (i = 1; i < nt; i++) {           /* by CC, stable */
        struct ds_tag c = W->t[i];
        for (k = i; k > 0 && W->t[k - 1].v > c.v; k--)
            W->t[k] = W->t[k - 1];
        W->t[k] = c;
    }
    dl_memcpy(st->cc, W->t, nt * sizeof W->t[0]);
    st->ncc = nt;
    if (nfr > DS_MAX_TAGS)
        return 0;
    dl_memcpy(st->fcc, W->fr, nfr * sizeof W->fr[0]);
    st->nfcc = nfr;
    return 1;
}

/* Is the core's LFO destination field of this scope (1 sound, 2 kit) among the placed fields? */
static int placed_dst(const struct ds_state *st, const struct ds_base *b, u32 scope)
{
    u32 i;
    if (!b->core || scope < 1 || scope > 2)
        return 0;
    for (i = 0; i < st->nf; i++)
        if (keyeq(st->f[i].mod, st->f[i].sym, b->core, DST_SYM[scope - 1]))
            return 1;
    return 0;
}

void ds_out_free(struct ds_out *o)
{
    dl_free(o->desc);
    dl_free(o->codes);
    dl_free(o->ccs);
    dl_free(o->inits);
    dl_free(o->handles);
    dl_free(o->bytes);
    dl_memset(o, 0, sizeof *o);
}

static void add_id(u32 *list, u32 *n, u32 max, u32 id, u32 self)
{
    u32 i;
    if (!id || id == self || *n >= max)
        return;
    for (i = 0; i < *n; i++)
        if (list[i] == id)
            return;
    list[(*n)++] = id;
}

static int allocate(const struct ds_base *b, const u8 *prev, u32 nprev, const struct ds_want *w, u32 n,
                    u32 nw, struct ds_out *out);

/* The chosen set and the core's LFO destination fields it needs, placed together (drive_store.allocate's
 * want_f); out->desc covers the set's n entries, out->lfo_dst the core's. */
int ds_allocate(const struct ds_base *b, const u8 *prev, u32 nprev, const struct ds_want *w, u32 n,
                struct ds_out *out)
{
    struct ds_want *a;
    u32 nx, i;
    int rc;
    dl_memset(out, 0, sizeof *out);
    if (n > DS_MAX_FIELDS)
        return -2;
    a = dl_alloc((n + 2) * sizeof *a);
    if (!a)
        return -1;
    for (i = 0; i < n; i++)
        a[i] = w[i];
    nx = dst_fields(b, w, n, a + n);
    rc = allocate(b, prev, nprev, a, n + nx, n, out);
    if (!rc)
        for (i = 0; i < nx; i++)
            out->lfo_dst[a[n + i].scope - 1] = out->desc[n + i];
    dl_free(a);
    return rc;
}

/* n entries of w, the first nw the chosen set's (ds_fit_one's input), the rest the core's */
static int allocate(const struct ds_base *b, const u8 *prev, u32 nprev, const struct ds_want *w, u32 n,
                    u32 nw, struct ds_out *out)
{
    struct ds_state *st = 0;
    struct work *W = 0;
    struct ds_short sh;
    u8 has_to[DS_SCOPES], gen_to[DS_SCOPES], repacked[DS_SCOPES];
    u32 i, k, s, nf = 0, nkept, ngone = 0, old_id, slot;
    int err, rc = -2;

    if (n > DS_MAX_FIELDS || ds_fit_one(b, w, nw, 0, &sh))
        return -2;
    for (i = 0; i < n; i++)              /* a symbol declared twice (the link refuses it first) */
        for (k = 0; k < i; k++)
            if (!dl_strcmp(w[i].sym, w[k].sym))
                return -2;
    st = prev ? ds_decode(prev, nprev) : 0;
    if (!st)
        st = fresh(b);
    W = dl_alloc(sizeof *W);
    if (!st || !W) {
        rc = -1;
        goto done;
    }
    old_id = st->id;

    /* what the flashed build changed under the +Drive */
    for (s = 0; s < DS_SCOPES; s++) {
        has_to[s] = repacked[s] = 0;
        if (st->fgen[s] != b->gen[s]) {
            has_to[s] = 1;
            gen_to[s] = st->gen[s] == st->fgen[s] ? (u8)b->gen[s] : bump(st->gen[s]);
        } else if (!held_ok(st, W, s, st->low[s], b->room[s], b->used[s])
                   || !(b->used[s] <= st->low[s] && st->low[s] <= b->room[s])) {
            has_to[s] = 1;
            gen_to[s] = bump(st->gen[s]);
        }
        st->fgen[s] = (u8)b->gen[s];
    }
    /* kept and gone */
    for (i = 0; i < st->nf; i++) {
        const struct ds_field *f = &st->f[i];
        const struct ds_want *x = want_of(w, n, f->mod, f->sym);
        if (x && x->scope == f->scope && x->bits == f->bits && x->init == f->init)
            W->fl[nf++] = *f;
        else
            W->gone[ngone++] = *f;
    }
    nkept = nf;
    for (i = 0; i < nf; i++)
        W->kept[i] = 1;
    /* added, in key order */
    for (i = 0; i < n; i++) {
        struct ds_field f;
        if (in_kept(W, nkept, w[i].mod, w[i].sym))
            continue;
        f.mod = w[i].mod;
        f.sym = w[i].sym;
        f.scope = w[i].scope;
        f.bits = w[i].bits;
        f.init = w[i].init;
        f.off = 0;
        f.placed = 0;
        for (k = nf; k > nkept && sorted_by_key(&f, &W->fl[k - 1]); k--)
            W->fl[k] = W->fl[k - 1];
        W->fl[k] = f;
        nf++;
    }
    for (i = nkept; i < nf; i++)
        W->back[i] = W->kept[i] = 0;
    /* a parked field back with the same scope, width and init takes its old place */
    for (i = nkept; i < nf; i++) {
        struct ds_field *f = &W->fl[i];
        int j = -1;
        for (k = 0; k < st->npk; k++) {
            const struct ds_field *g = &st->pk[k];
            if (keyeq(g->mod, g->sym, f->mod, f->sym) && g->scope == f->scope && g->bits == f->bits
                && g->init == f->init)
                j = (int)k;
        }
        if (j >= 0 && !has_to[f->scope - 1]) {
            struct ds_field g = st->pk[j];
            f->off = g.off;
            f->placed = 1;
            for (k = 0; !field_eq(&st->pk[k], &g); k++)
                ;
            remove_at(st->pk, sizeof st->pk[0], &st->npk, k);
            W->back[i] = 1;
        }
    }
    /* a field no chosen mod declares as it was is parked */
    for (i = 0; i < ngone; i++) {
        u32 q = 0;
        for (k = 0; k < st->npk; k++)
            if (!keyeq(st->pk[k].mod, st->pk[k].sym, W->gone[i].mod, W->gone[i].sym))
                st->pk[q++] = st->pk[k];
        st->npk = q;
        if (st->npk >= DS_MAX_FIELDS) {
            rc = -2;
            goto done;
        }
        st->pk[st->npk++] = W->gone[i];
    }
    for (s = 0; s < DS_SCOPES; s++) {
        u32 sum = 0, pend = 0;
        for (i = 0; i < nf; i++)
            if (W->fl[i].scope == s + 1)
                sum += W->fl[i].bits;
        if ((s32)sum > (s32)b->room[s] - (s32)b->used[s])
            goto done;
        if (has_to[s]) {
            repack(st, b, W, nf, s, gen_to[s], repacked);
            continue;
        }
        for (i = nkept; i < nf; i++)
            if (W->fl[i].scope == s + 1 && !W->fl[i].placed)
                pend += W->fl[i].bits;
        if (!pend)
            continue;
        if ((s32)st->low[s] - (s32)pend >= (s32)b->used[s]) {
            for (i = nkept; i < nf; i++)
                if (W->fl[i].scope == s + 1 && !W->fl[i].placed) {
                    st->low[s] -= W->fl[i].bits;
                    W->fl[i].off = (u16)st->low[s];
                    W->fl[i].placed = 1;
                }
        } else {
            repack(st, b, W, nf, s, bump(st->gen[s]), repacked);
        }
    }
    for (i = nkept; i < nf; i++)
        if (repacked[W->fl[i].scope - 1])
            W->back[i] = 0;
    /* the fields by (scope, offset), stable */
    for (i = 0; i < nf; i++) {
        struct ds_field f = W->fl[i];
        for (k = i; k > 0 && (st->f[k - 1].scope > f.scope || (st->f[k - 1].scope == f.scope && st->f[k - 1].off > f.off)); k--)
            st->f[k] = st->f[k - 1];
        st->f[k] = f;
    }
    st->nf = nf;
    if (!codes(st, b, W, w, n, nkept, nf))
        goto done;
    if (!ccs(st, b, W, w, n))
        goto done;
    st->id = layout_id(st, b, &err);
    if (err) {
        rc = -1;
        goto done;
    }
    if (st->id != old_id) {
        u32 *ids = W->ids, m = 0;
        add_id(ids, &m, 256, old_id, st->id);
        for (i = 0; i < st->nids; i++)
            add_id(ids, &m, 256, st->ids[i], st->id);
        st->nids = m < DS_MAX_IDS ? m : DS_MAX_IDS;
        dl_memcpy(st->ids, ids, st->nids * 4);
    }
    st->flashed_id = b->flashed_id;

    /* Placed */
    out->id = st->id;
    for (i = 0; i < st->nids; i++)
        add_id(out->anc, &out->nanc, DS_MAX_IDS, st->ids[i], st->id);
    add_id(out->anc, &out->nanc, DS_MAX_IDS, b->flashed_id, st->id);
    for (i = 0; i < b->nanc; i++)
        add_id(out->anc, &out->nanc, DS_MAX_IDS, b->anc[i], st->id);
    for (s = 0; s < DS_SCOPES; s++)
        out->gen[s] = st->gen[s];
    out->desc = dl_alloc((n + 1) * 4);
    out->codes = dl_alloc(st->ncd * 8 + 8);
    out->ccs = dl_alloc(st->ncc * 8 + 8);
    out->inits = dl_alloc(st->nf * 4 + 4);
    out->handles = dl_alloc(st->nf * 4 + 4);
    if (!out->desc || !out->codes || !out->ccs || !out->inits || !out->handles) {
        rc = -1;
        goto done;
    }
    {
        u32 *dsc = W->dsc;
        slot = 0;
        for (i = 0; i < st->nf; i++) {
            const struct ds_field *f = &st->f[i];
            const struct ds_want *x = want_of(w, n, f->mod, f->sym);
            u32 sl = 0;
            if (f->init) {
                sl = b->inits + ++slot;
                out->inits[out->ninits++] = f->init;
            }
            dsc[i] = (u32)f->scope << 28 | (u32)f->bits << 22 | sl << 16 | f->off;
            if (x)
                out->desc[x - w] = dsc[i];
            if (x && (x->flags & DS_HANDLE))
                out->handles[out->nhandles++] = dsc[i];
        }
        for (i = 0; i < st->ncd; i++) {
            const struct ds_tag *c = &st->cd[i];
            const struct ds_want *x = want_of(w, n, c->mod, c->sym);
            u32 flags = 0, d = x ? out->desc[x - w] : 0;
            if (x && (x->flags & DS_LFO) && (lfo_dst(b, x->scope) || placed_dst(st, b, x->scope)))
                flags |= DS_LFO_FLAG;
            if (x && (x->flags & DS_ROUGH))
                flags |= DS_SMOOTH_OFF;
            if (x && (x->flags & DS_ROUTE))
                flags |= DS_ROUTE_FLAG;
            dl_wr32(out->codes + 8 * i, d);
            dl_wr32(out->codes + 8 * i + 4, c->v | flags);
        }
        out->ncodes = st->ncd;
        for (i = 0; i < st->ncc; i++) {
            const struct ds_tag *c = &st->cc[i];
            const struct ds_want *x = want_of(w, n, c->mod, c->sym);
            dl_wr32(out->ccs + 8 * i, x ? out->desc[x - w] : 0);
            dl_wr32(out->ccs + 8 * i + 4, c->v);
        }
        out->nccs = st->ncc;
    }
    out->nfields = st->nf;
    out->nparked = st->npk;
    out->bytes = ds_encode(st, &out->nbytes);
    rc = out->bytes ? 0 : -1;
done:
    if (rc)
        ds_out_free(out);
    ds_state_free(st);
    dl_free(W);
    return rc;
}
