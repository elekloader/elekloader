/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The Drive Loader's +Drive storage allocator (mods/drive-loader-ar2/dstore.c), run on the
 * computer: tests/test_drive_store_host.py builds it and replays js/test/drivestore-vectors.json
 * through it, one step a run. The step comes on stdin as words:
 *
 *   ROOM USED GEN x6  FLASHED_ID  NANC ANC...  NCODES (DESC CODE)...  NCCS (DESC CC)...
 *   INITS  SPARE_CODES SPARE_CCS SPARE_INITS SPARE_HANDLES  N (MODHEX SYMHEX SCOPE BITS INIT FLAGS)...
 *   PREV_HEX|-  place|fit|forget [MODHEX]
 *
 * and the result goes to stdout as JSON, in drive_store.Placed.summary()'s terms. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../mods/drive-loader-ar2/dstore.h"

void *dl_alloc(u32 n) { return calloc(n ? n : 1, 1); }
void dl_free(void *p) { free(p); }
void dl_breathe(void) {}
void dl_apply_site(u32 addr, const u8 *src, u32 len) { (void)addr; (void)src; (void)len; exit(5); }
/* dlink.c's helpers come with it; it reads no memory or file here */
u8 *dl_host_mem(u32 a) { (void)a; exit(5); }
s32 dl_read_file(const char *name, u8 *buf, u32 max) { (void)name; (void)buf; (void)max; return -1; }

static u32 num(void)
{
    char w[64];
    if (scanf("%63s", w) != 1)
        exit(2);
    return (u32)strtoll(w, 0, 0);
}

static char *hexstr(void)
{
    static char w[0x40000];
    char *out;
    size_t i, n;
    if (scanf("%262143s", w) != 1)
        exit(2);
    if (!strcmp(w, "-"))
        return 0;
    n = strlen(w) / 2;
    out = calloc(n + 1, 1);
    for (i = 0; i < n; i++) {
        unsigned v;
        sscanf(w + 2 * i, "%2x", &v);
        out[i] = (char)v;
    }
    out[n] = 0;
    return out;
}

static u32 hexlen;
static u8 *hexbytes(void)
{
    static char w[0x40000];
    u8 *out;
    size_t i, n;
    if (scanf("%262143s", w) != 1)
        exit(2);
    if (!strcmp(w, "-"))
        return 0;
    n = strlen(w) / 2;
    out = calloc(n + 1, 1);
    for (i = 0; i < n; i++) {
        unsigned v;
        sscanf(w + 2 * i, "%2x", &v);
        out[i] = (u8)v;
    }
    hexlen = (u32)n;
    return out;
}

static void jstr(const char *s)
{
    putchar('"');
    for (; *s; s++) {
        if (*s == '"' || *s == '\\')
            printf("\\%c", *s);
        else
            putchar(*s);
    }
    putchar('"');
}

static void hexout(const u8 *p, u32 n)
{
    u32 i;
    putchar('"');
    for (i = 0; i < n; i++)
        printf("%02x", p[i]);
    putchar('"');
}

int main(void)
{
    static struct ds_base b;
    static struct ds_want w[DS_MAX_FIELDS];
    static u32 anc[64];
    static u8 codes[8 * 256], ccs[8 * 256];
    char op[16];
    u32 i, n, s;
    u8 *prev;
    u32 nprev;
    for (s = 0; s < DS_SCOPES; s++) {
        b.room[s] = num();
        b.used[s] = num();
        b.gen[s] = num();
    }
    b.flashed_id = num();
    b.nanc = num();
    for (i = 0; i < b.nanc; i++)
        anc[i] = num();
    b.anc = anc;
    b.ncodes = num();
    for (i = 0; i < b.ncodes; i++) {
        dl_wr32(codes + 8 * i, num());
        dl_wr32(codes + 8 * i + 4, num());
    }
    b.codes = codes;
    b.nccs = num();
    for (i = 0; i < b.nccs; i++) {
        dl_wr32(ccs + 8 * i, num());
        dl_wr32(ccs + 8 * i + 4, num());
    }
    b.ccs = ccs;
    b.inits = num();
    for (i = 0; i < 4; i++)
        b.spare[i] = (s32)num();
    b.live_dst = num();
    b.core = "core";
    n = num();
    for (i = 0; i < n; i++) {
        w[i].mod = hexstr();
        w[i].sym = hexstr();
        w[i].scope = (u8)num();
        w[i].bits = (u8)num();
        w[i].init = num();
        w[i].flags = (u8)num();
    }
    prev = hexbytes();
    nprev = prev ? hexlen : 0;
    if (scanf("%15s", op) != 1)
        return 2;
    if (!strcmp(op, "fit")) {
        static u8 off[DS_MAX_FIELDS];
        struct ds_short sh;
        int first = 1;
        printf("{\"fit\": [");
        while (ds_fit_one(&b, w, n, off, &sh)) {
            const char *m = w[sh.victim].mod;
            printf("%s[", first ? "" : ", ");
            jstr(m);
            printf(", \"%s\", %d]", ds_what[sh.what], sh.need - sh.have);
            for (i = 0; i < n; i++)
                if (!strcmp(w[i].mod, m))
                    off[i] = 1;
            first = 0;
        }
        printf("]}\n");
        return 0;
    }
    if (!strcmp(op, "forget")) {
        char *m = hexstr();
        struct ds_state *st = ds_decode(prev, nprev);
        int ch;
        u8 *out;
        u32 no;
        if (!st)
            return 3;
        ch = ds_forget(st, m);
        out = ds_encode(st, &no);
        printf("{\"changed\": %s, \"store\": ", ch ? "true" : "false");
        hexout(out, no);
        printf("}\n");
        return 0;
    }
    {
        struct ds_out o;
        struct ds_state *st;
        int rc = ds_allocate(&b, prev, nprev, w, n, &o);
        if (rc) {
            printf("{\"error\": %d}\n", rc);
            return 0;
        }
        st = ds_decode(o.bytes, o.nbytes);
        if (!st)
            return 4;
        printf("{\"id\": \"0x%08x\", \"ancestors\": [", o.id);
        for (i = 0; i < o.nanc; i++)
            printf("%s\"0x%08x\"", i ? ", " : "", o.anc[i]);
        printf("], \"gens\": [");
        for (s = 0; s < DS_SCOPES; s++)
            printf("%s%d", s ? ", " : "", o.gen[s]);
        printf("], \"fields\": {");
        for (i = 0; i < n; i++) {
            printf("%s\"%s/%s\": \"0x%08x\"", i ? ", " : "", w[i].mod, w[i].sym, o.desc[i]);
        }
        printf("}, \"parked\": [");
        for (i = 0; i < st->npk; i++) {
            const struct ds_field *f = &st->pk[i];
            printf("%s\"%s/%s 0x%08x\"", i ? ", " : "", f->mod, f->sym,
                   (u32)f->scope << 28 | (u32)f->bits << 22 | f->off);
        }
        printf("], \"codes\": [");
        for (i = 0; i < o.ncodes; i++) {
            printf("%s", i ? ", " : "");
            hexout(o.codes + 8 * i, 8);
        }
        printf("], \"ccs\": [");
        for (i = 0; i < o.nccs; i++) {
            printf("%s", i ? ", " : "");
            hexout(o.ccs + 8 * i, 8);
        }
        printf("], \"inits\": [");
        for (i = 0; i < o.ninits; i++)
            printf("%s\"%08x\"", i ? ", " : "", o.inits[i]);
        printf("], \"handles\": [");
        for (i = 0; i < o.nhandles; i++)
            printf("%s\"%08x\"", i ? ", " : "", o.handles[i]);
        printf("], \"lfo_dsts\": {");
        if (o.lfo_dst[1])
            printf("\"kit\": \"0x%08x\"", o.lfo_dst[1]);
        if (o.lfo_dst[0])
            printf("%s\"sound\": \"0x%08x\"", o.lfo_dst[1] ? ", " : "", o.lfo_dst[0]);
        printf("}, \"nparked\": %u, \"store\": ", o.nparked);
        hexout(o.bytes, o.nbytes);
        printf("}\n");
        ds_out_free(&o);
        ds_state_free(st);
    }
    return 0;
}
