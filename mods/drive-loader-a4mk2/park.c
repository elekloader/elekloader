/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Parked +Drive storage in /mods/.store for the MODS page (park.h). */

#include "park.h"

const char *const dl_store_scopes[DL_STORE_SCOPES] = {"sound", "kit", "global", "track", "pattern", "project"};

#define HEAD 0x28
#define SCOPE_REC 8
#define FORMAT 1
#define MAX_BITS 32
#define MAX_RECS (DL_MAX_MODS * 64)      /* no store holds more records: a bound for a bad count */

/* the counts at +0x1a: fields, parked, codes, freed codes, CCs, freed CCs */
enum { C_FIELDS, C_PARKED, C_CODES, C_FREED_CODES, C_CCS, C_FREED_CCS, NCOUNTS };

struct rec {                             /* one record, as the walk finds it */
    u32 at, size;                        /* its place and length, padding included */
    u8 kind;                             /* its list: C_* */
    u8 scope, bits;                      /* a field's */
    const u8 *mod;                       /* its mod id's bytes */
    u8 nmod;
};

/* The file's records one at a time: walk(raw, len, i, &r) for i = 0, 1, ... -> 1 a record,
 * 0 past the last, -1 a bad file. check() runs it once over the whole file first. */
struct walk {
    const u8 *raw;
    u32 len, at;
    u16 count[NCOUNTS];
    int list, left;
};

static int walk_start(struct walk *w, const u8 *raw, u32 len)
{
    u32 crc, i;
    if (!raw || len < HEAD + SCOPE_REC * DL_STORE_SCOPES)
        return -1;
    if (raw[0] != 'D' || raw[1] != 'L' || raw[2] != 'S' || raw[3] != 'T' || dl_rd16(raw + 4) != FORMAT ||
        dl_rd32(raw + 8) != len)
        return -1;
    crc = dl_crc32_add(0xFFFFFFFF, raw, 0x0c);
    crc = dl_crc32_add(crc, (const u8 *)"\0\0\0\0", 4);
    crc = ~dl_crc32_add(crc, raw + 0x10, len - 0x10);
    if (crc != dl_rd32(raw + 0x0c))
        return -1;
    w->raw = raw;
    w->len = len;
    for (i = 0; i < NCOUNTS; i++)
        w->count[i] = dl_rd16(raw + 0x1a + 2 * i);
    w->at = HEAD + SCOPE_REC * DL_STORE_SCOPES + 4 * (u32)raw[0x19];   /* past the scopes and the IDs */
    w->list = 0;
    w->left = w->count[0];
    return w->at > len ? -1 : 0;
}

static int walk_next(struct walk *w, struct rec *r)
{
    const u8 *p;
    u32 lm, ls, head;
    while (!w->left) {
        if (++w->list >= NCOUNTS)
            return w->at == w->len ? 0 : -1;
        w->left = w->count[w->list];
    }
    w->left--;
    head = w->list <= C_PARKED ? 12 : 4;
    if (w->at + head > w->len)
        return -1;
    p = w->raw + w->at;
    r->at = w->at;
    r->kind = (u8)w->list;
    if (head == 12) {                    /* u8 scope, u8 bits, u16 offset, u32 init, u8 lm, u8 ls, u16 0 */
        r->scope = p[0];
        r->bits = p[1];
        lm = p[8];
        ls = p[9];
        if (r->scope < 1 || r->scope > DL_STORE_SCOPES || r->bits < 1 || r->bits > MAX_BITS ||
            (r->bits < 32 && dl_rd32(p + 4) >> r->bits))
            return -1;
    } else {                             /* u8 code or CC, u8 lm, u8 ls, u8 0 */
        r->scope = r->bits = 0;
        lm = p[1];
        ls = p[2];
    }
    if (!lm || !ls)
        return -1;
    r->mod = p + head;
    r->nmod = (u8)lm;
    r->size = head + ((lm + ls + 3) & ~3u);
    if (w->at + head + lm + ls > w->len || w->at + r->size > w->len)
        return -1;
    w->at += r->size;
    return 1;
}

/* the whole file walks to its end -> 0, or -1 */
static int check(const u8 *raw, u32 len)
{
    struct walk w;
    struct rec r;
    int k, n = 0;
    if (walk_start(&w, raw, len))
        return -1;
    while ((k = walk_next(&w, &r)) > 0)
        if (++n > MAX_RECS)
            return -1;
    return k;
}

static int eq(const u8 *a, const u8 *b, u32 n)
{
    while (n--)
        if (*a++ != *b++)
            return 0;
    return 1;
}

static int same(const u8 *a, u32 n, const char *s)
{
    u32 i;
    for (i = 0; i < n; i++)
        if (!s[i] || (u8)s[i] != a[i])
            return 0;
    return !s[n];
}

int dl_park_list(const u8 *raw, u32 len, struct dl_park *out, int max)
{
    struct walk w;
    struct rec r;
    int n = 0, i;
    if (check(raw, len))
        return -1;
    walk_start(&w, raw, len);
    while (walk_next(&w, &r) > 0) {
        struct dl_park *p = 0;
        if (r.kind != C_PARKED && r.kind != C_FREED_CODES && r.kind != C_FREED_CCS)
            continue;
        if (out) {
            for (i = 0; i < n && i < max; i++)
                if (same(r.mod, r.nmod, out[i].id)) {
                    p = &out[i];
                    break;
                }
        } else {                         /* counting: is this mod's id one met before? */
            struct walk v;
            struct rec q;
            int seen = 0;
            walk_start(&v, raw, len);
            while (!seen && walk_next(&v, &q) > 0 && q.at < r.at)
                seen = (q.kind == C_PARKED || q.kind == C_FREED_CODES || q.kind == C_FREED_CCS) &&
                       q.nmod == r.nmod && eq(q.mod, r.mod, r.nmod);
            if (!seen)
                n++;
            continue;
        }
        if (!p) {
            if (n >= max)
                continue;
            p = &out[n++];
            dl_memset(p, 0, sizeof *p);
            dl_memcpy(p->id, r.mod, r.nmod);
        }
        if (r.kind == C_PARKED) {
            p->bits[r.scope - 1] += r.bits;
            p->fields++;
        } else if (r.kind == C_FREED_CODES)
            p->codes++;
        else
            p->ccs++;
    }
    return n;
}

s32 dl_park_forget(const u8 *raw, u32 len, const char *const *ids, int n, u8 *out)
{
    struct walk w;
    struct rec r;
    u32 at, crc;
    u16 count[NCOUNTS];
    int i, gone;
    if (check(raw, len))
        return -1;
    walk_start(&w, raw, len);
    for (i = 0; i < NCOUNTS; i++)
        count[i] = w.count[i];
    at = w.at;
    dl_memcpy(out, raw, at);             /* the head, the scopes and the IDs as they are */
    while (walk_next(&w, &r) > 0) {
        gone = 0;
        if (r.kind == C_PARKED || r.kind == C_FREED_CODES || r.kind == C_FREED_CCS)
            for (i = 0; i < n && !gone; i++)
                gone = same(r.mod, r.nmod, ids[i]);
        if (gone) {
            count[r.kind]--;
            continue;
        }
        dl_memcpy(out + at, raw + r.at, r.size);
        at += r.size;
    }
    for (i = 0; i < NCOUNTS; i++)
        dl_wr16(out + 0x1a + 2 * i, count[i]);
    dl_wr32(out + 8, at);
    crc = dl_crc32_add(0xFFFFFFFF, out, 0x0c);
    crc = dl_crc32_add(crc, (const u8 *)"\0\0\0\0", 4);
    crc = ~dl_crc32_add(crc, out + 0x10, at - 0x10);
    dl_wr32(out + 0x0c, crc);
    return (s32)at;
}
