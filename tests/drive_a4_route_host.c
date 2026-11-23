/* SPDX-License-Identifier: GPL-2.0-or-later */
/* drive-loader-a4mk2's /mods route (mods/drive-loader-a4mk2/route.c) over its store (store.c) on
 * the computer, with a card in RAM, driven in stock's order for an upload (README, "The /mods
 * route"): the entry resolved (dl_entry), copied as the router copies it (each std::function slot's
 * closure cloned by its manager, so every slot holds a closure of its own), then +0x1c, +0x3c, the
 * writer streaming the body to the bytes +0x0c points at, +0x6c and +0x4c. Also a read's +0x1c
 * followed by a Clear resolved on its own (no reservation), and a replacing upload. From 0.2 the
 * files in use (skip.h dl_payload_held, here a list the test sets): no Clear, no .mv, an upload
 * kept as <name>.new, a name with no room for ".new" refused, and .status's payload lines.
 * tests/test_drive_a4_route.py builds it with -no-pie, so the heap below (a static arena) and the
 * functions sit below 4 GB and route.c's 32-bit slots hold them, and runs it. Prints "ok" lines
 * and PASSED, or a FAIL line and exits 1. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static unsigned char heap[1u << 22] __attribute__((aligned(16)));
static unsigned heap_at;
static void *host_new(unsigned n)
{
    void *p = heap + heap_at;
    heap_at += (n + 15) & ~15u;
    if (heap_at > sizeof heap) {
        printf("FAIL heap\n");
        exit(1);
    }
    memset(p, 0, n);
    return p;
}
static void host_delete(void *p)
{
    (void)p;                              /* a freed closure keeps its bytes: route.c must not need them */
}
static const char *host_str;              /* the last std::string the route made */
static void host_str_new(void *slot, const char *s, void *a)
{
    (void)slot, (void)a;
    host_str = s;
}
#define NEW host_new
#define DELETE host_delete
#define STR_NEW host_str_new
#define STR_DEL(p) ((void)(p))
#include "route.c"

/* the Drive payload table's answers (payload.c): the paths in use, and its .status lines */
static const char *held_paths[4];
int dl_payload_held(const char *path, int dir)
{
    unsigned i, k = (unsigned)strlen(path);
    for (i = 0; i < 4 && held_paths[i]; i++)
        if (dir ? !strncmp(held_paths[i], path, k) && held_paths[i][k] == '/' : !strcmp(held_paths[i], path))
            return 1;
    return 0;
}
unsigned dl_payload_status_size(void) { return 32; }
char *dl_payload_status(char *at, char *end) { return cat(at, end, "payloads: 1 kept, 0 lost\n"); }

const unsigned char core_desc[16];
int dl_list_inv(void) { return 0; }
int dl_entry_inv(void) { return 0; }
int dl_pre_inv(void) { return 0; }
int dl_hdr_inv(void) { return 0; }

/* ---- the card ---- */
#define SECTORS 0x80000u
static unsigned char *card;
int ds_host_read(unsigned sector, unsigned bytes, void *dest)
{
    if (sector + (bytes + 511) / 512 > SECTORS)
        return -1;
    memcpy(dest, card + (size_t)sector * 512, bytes);
    return 0;
}
int ds_host_write(unsigned sector, unsigned bytes, const void *src)
{
    unsigned n = (bytes + 511) / 512;
    if (sector + n > SECTORS)
        return -10;
    memset(card + (size_t)sector * 512, 0xA5, (size_t)n * 512);
    memcpy(card + (size_t)sector * 512, src, bytes);
    return 0;
}
unsigned ds_host_sectors(void) { return SECTORS; }

static int fails;
#define CHECK(c, what) do { if (!(c)) { printf("FAIL %s (line %d)\n", what, __LINE__); fails++; } else printf("ok %s\n", what); } while (0)

static unsigned get32(const unsigned char *p) { return *(const unsigned *)p; }

/* the router's copy of an entry: the bytes, then each std::function slot's closure cloned */
static const int slots[] = {E_PRE, E_FILL, E_RESERVE, E_CHANGE, E_HEADER};
static unsigned char *copy_entry(const unsigned char *e)
{
    unsigned char *c = host_new(ENTRY_SIZE);
    unsigned i;
    memcpy(c, e, ENTRY_SIZE);
    for (i = 0; i < sizeof slots / sizeof slots[0]; i++) {
        unsigned char *s = c + slots[i];
        void *src = (void *)(uintptr_t)get32(s), *dst = 0;
        if (!get32(s + 8))
            continue;
        ((int (*)(void **, void *const *, int))(uintptr_t)get32(s + 8))(&dst, &src, 2);
        put32(s, (unsigned)(uintptr_t)dst);
        put32(s + 4, 0);
    }
    return c;
}
/* the slot as the invoker's functor argument (its closure pointer, the next word 0) */
static void **fn(unsigned char *e, int slot)
{
    return (void **)(e + slot);
}

/* /mods/<name> resolved: the entry (the resolver's own, which the router copies), or 0 */
static unsigned char *resolve(const char *name)
{
    static unsigned char res[8 + ENTRY_SIZE];
    static char parts[DL_NAME_MAX + 1];
    const char *v[8];
    void *caps[2];
    int n = 0;
    char *p = parts;
    strcpy(parts, name);                  /* one capture a path part, as the router's patterns give them */
    v[n++] = p;
    for (; *p; p++)
        if (*p == '/') {
            *p = 0;
            v[n++] = p + 1;
        }
    caps[0] = (void *)v;
    caps[1] = (void *)(v + n);
    dl_entry(res, 0, caps);
    if (!res[0]) {
        printf("  (refused: %s)\n", host_str);
        return 0;
    }
    return copy_entry(res + 8);
}

static void header(unsigned char *h, unsigned size)
{
    memset(h, 0, 31);
    h[H_TYPE] = 'E', h[H_TYPE + 1] = 'M', h[H_TYPE + 2] = 'O', h[H_TYPE + 3] = 'D';
    h[H_PAYLOAD] = size >> 24, h[H_PAYLOAD + 1] = size >> 16, h[H_PAYLOAD + 2] = size >> 8;
    h[H_PAYLOAD + 3] = size;
}

static unsigned char pat(unsigned i, unsigned seed) { return (unsigned char)(i * 13 + seed * 7 + (i >> 9)); }

/* one upload in stock's order: -> 0 when every step passed and the file reads back as `as` */
static int upload_as(const char *name, const char *as, unsigned size, unsigned seed, const char **why)
{
    unsigned char r[16], h[31], *e, *copy;
    unsigned at, n, i;
    char path[DL_PATH_MAX];
    unsigned char *body = malloc(size + 1), *back = malloc(size + 1);
    int ok = -1;
    dl_file f;
    *why = "";
    for (i = 0; i < size; i++)
        body[i] = pat(i, seed);
    e = resolve(name);
    if (!e) {
        *why = "resolve";
        goto out;
    }
    copy = copy_entry(e);                 /* the upload's copy, its size the header's */
    put32(copy + E_SIZE, size);
    dl_pre(r, fn(copy, E_PRE), copy);
    if (!r[0]) {
        *why = host_str;
        goto out;
    }
    dl_reserve(fn(copy, E_RESERVE));
    at = get32(copy + E_ADDR);
    n = get32(copy + E_SIZE);
    if (at == SCRATCH_AT || n != size) {
        *why = "no run reserved";
        goto out;
    }
    ds_host_write(at / 512, size, body);  /* the +Drive writer */
    header(h, size);
    dl_hdr(r, fn(copy, E_HEADER), h);
    if (!r[0]) {
        *why = host_str;
        goto out;
    }
    dl_change(fn(copy, E_CHANGE), h);
    path_of(path, as, "");
    if (dl_fs_file_size(path) != (int)size) {
        *why = dl_note;
        goto out;
    }
    if (dl_fs_open(&f, path, 0) == 0 && dl_fs_read(&f, back, size) == (int)size && !memcmp(back, body, size))
        ok = 0;
    else
        *why = "read back differs";
    dl_fs_close(&f);
out:
    free(body);
    free(back);
    return ok;
}

static int upload(const char *name, unsigned size, unsigned seed, const char **why)
{
    return upload_as(name, name, size, seed, why);
}

/* /mods/name's entry's permissions, or -1 refused */
static int perm_of(const char *name)
{
    unsigned char *e = resolve(name);
    return e ? *(unsigned short *)(e + E_PERM) : -1;
}

/* the open folder rel's listing: name's item's permissions, or -1 not listed */
static int listed_perm(const char *rel, const char *name)
{
    struct dl_ent ents[64];
    unsigned char items[64 * 0x14];
    int n = dl_list_dir(rel, ents, 64), k, i, j = 0;
    k = walk(items, rel, rel[0] != 0, ents, n);
    for (i = 0; i < n; i++) {             /* walk's items follow the entries it keeps */
        if (ents[i].name[0] == '.')
            continue;
        if (!strcmp(ents[i].name, name))
            return j < k ? *(unsigned short *)(items + 0x14 * j + 0xc) : -1;
        j++;
    }
    return -1;
}

/* a .mv of FROM to TO through the route's move: -> 0 or its refusal */
static const char *mv(const char *from, const char *to)
{
    unsigned n = (unsigned)sprintf((char *)drive_staging, "%s\n%s", from, to);
    return move(n);
}

int main(void)
{
    const char *why;
    unsigned char r[16], *e, *copy;
    unsigned largest, total, largest2, total2, sector = 0, bytes = 0;
    char path[DL_PATH_MAX];
    card = calloc(SECTORS, 512);
    CHECK(ds_mount() == 0, "an empty card mounts");

    CHECK(upload("hello.elemod", 1749, 1, &why) == 0, "an upload in stock's order, each slot its own closure, is saved");
    if (*why)
        printf("  (%s)\n", why);
    CHECK(upload("hello.elemod", 9000, 2, &why) == 0, "a second upload of the name replaces it");
    if (*why)
        printf("  (%s)\n", why);
    CHECK(upload("pay.elemod", 70000, 3, &why) == 0, "a larger one beside it");
    if (*why)
        printf("  (%s)\n", why);

    /* a read's +0x1c, then a Clear resolved on its own: the Clear's +0x3c reserves nothing */
    path_of(path, "hello.elemod", "");
    ds_file(path, &sector, &bytes);
    e = resolve("hello.elemod");
    copy = copy_entry(e);
    dl_pre(r, fn(copy, E_PRE), copy);
    CHECK(r[0], "a read's +0x1c passes");
    ds_room(&largest, &total);
    e = resolve("hello.elemod");
    copy = copy_entry(e);
    dl_reserve(fn(copy, E_RESERVE));
    ds_room(&largest2, &total2);
    CHECK(get32(copy + E_ADDR) == sector * 512u && get32(copy + E_SIZE) == bytes,
          "a Clear's +0x3c after a read leaves the entry on the file's own run");
    CHECK(largest == largest2 && total == total2, "... and reserves no room");
    dl_change(fn(copy, E_CHANGE), 0);
    CHECK(dl_fs_file_size(path) < 0, "the Clear deletes the file");

    /* +0x3c on a copy whose +0x1c never ran reserves nothing, though another resolution's did */
    e = resolve("other.elemod");
    copy = copy_entry(e);
    put32(copy + E_SIZE, 500);
    dl_pre(r, fn(copy, E_PRE), copy);
    e = resolve("other.elemod");
    copy = copy_entry(e);
    dl_reserve(fn(copy, E_RESERVE));
    CHECK(get32(copy + E_ADDR) == SCRATCH_AT, "+0x3c of a later resolution doesn't take an earlier one's +0x1c");

    CHECK(upload("pay.elemod", 3000, 4, &why) == 0, "an upload after all that still saves");
    if (*why)
        printf("  (%s)\n", why);

    /* an upload into folders not there yet makes them, as the Rytm's file system does */
    CHECK(upload("fx/delays/tape.elemod", 2500, 5, &why) == 0, "an upload into missing folders makes them and saves");
    if (*why)
        printf("  (%s)\n", why);
    path_of(path, "fx", "");
    CHECK(dl_fs_file_size(path) < 0 && upload("fx/plate.elemod", 600, 6, &why) == 0,
          "... fx is a folder, and takes another file");
    if (*why)
        printf("  (%s)\n", why);

    /* a file where the folder would go: nothing is saved, and .status says why */
    {
        unsigned token, at2;
        path_of(path, "blk", "");
        at2 = ds_reserve(512, &token);
        CHECK(at2 && ds_commit(path, token, 512) == 0, "a file named blk");
    }
    CHECK(upload("blk/a.elemod", 700, 7, &why) != 0 && strstr(why, "no folder") != 0,
          "an upload into a folder a file's name blocks is not saved, noted as no folder");
    if (*why)
        printf("  (%s)\n", why);
    path_of(path, "blk/a.elemod", "");
    CHECK(dl_fs_file_size(path) < 0, "... and no file is there");
    CHECK(upload("pay.elemod", 3000, 8, &why) == 0, "the next upload still saves");

    /* files in use (0.2): a loaded mod's file whose payload the table keeps */
    {
        static char held1[DL_PATH_MAX], longname[DL_NAME_MAX + 1], held2[DL_PATH_MAX];
        char nw[DL_PATH_MAX];
        unsigned s0, b0, s1, b1;
        int k;
        CHECK(upload("use/p.elemod", 4000, 9, &why) == 0, "a payload mod's file");
        path_of(held1, "use/p.elemod", "");
        held_paths[0] = held1;
        CHECK(!(perm_of("use/p.elemod") & P_CLEAR) && (perm_of("use/p.elemod") & P_WRITE),
              "a file in use: its entry has no Clear, and takes an upload");
        CHECK(listed_perm("use", "p.elemod") >= 0 && !(listed_perm("use", "p.elemod") & P_CLEAR),
              "... nor its listing's item");
        CHECK((perm_of("pay.elemod") & P_CLEAR) && (listed_perm("", "pay.elemod") & P_CLEAR),
              "a file not in use keeps its Clear");
        e = resolve("use/p.elemod");
        copy = copy_entry(e);
        dl_change(fn(copy, E_CHANGE), 0);
        CHECK(dl_fs_file_size(held1) == 4000 && strstr(dl_note, "In use: restart first:") != 0,
              "a Clear that gets through anyway deletes nothing, noted as in use");
        ds_file(held1, &s0, &b0);
        path_of(nw, "use/p.elemod", ".new");
        CHECK(upload_as("use/p.elemod", "use/p.elemod.new", 5000, 10, &why) == 0
              && !strcmp(dl_note, "pending restart: use/p.elemod"),
              "an upload over it is kept as <name>.new, noted pending restart");
        if (*why)
            printf("  (%s)\n", why);
        ds_file(held1, &s1, &b1);
        CHECK(s1 == s0 && b1 == b0 && b1 == 4000, "... and the file in use is where it was");
        CHECK(upload_as("use/p.elemod", "use/p.elemod.new", 6000, 11, &why) == 0 && dl_fs_file_size(nw) == 6000,
              "a second upload replaces the waiting .new");
        CHECK(mv("use/p.elemod", "use/q.elemod") && !strcmp(mv("use/p.elemod", "use/q.elemod"), IN_USE),
              "a .mv of it is refused, in use");
        CHECK(mv("use", "used") && !strcmp(mv("use", "used"), IN_USE), "... and of a folder holding it");
        CHECK(dl_fs_file_size(held1) == 4000, "... and it is still there");
        status_text();
        CHECK(strstr(dl_status_text, "payloads: 1 kept, 0 lost\n") != 0, ".status carries the table's lines");
        held_paths[0] = 0;                /* the next boot swapped it in: an upload saves over it */
        CHECK(upload("use/p.elemod", 7000, 12, &why) == 0 && dl_fs_file_size(nw) < 0,
              "not in use, an upload saves over the file and drops a stale .new");

        /* a name with no room for ".new" in the store's paths: two folders of 50 and a file */
        k = DS_PATH - 1 - (int)(sizeof DL_DIR - 1);   /* the longest name the store takes */
        memset(longname, 'n', sizeof longname);
        longname[50] = longname[101] = '/';
        memcpy(longname + k - 7, ".elemod", 8);
        CHECK(upload(longname, 300, 13, &why) == 0, "a file with the longest name the store takes");
        if (*why)
            printf("  (%s)\n", why);
        path_of(held2, longname, "");
        held_paths[0] = held2;
        e = resolve(longname);
        copy = e ? copy_entry(e) : 0;
        if (copy) {
            put32(copy + E_SIZE, 300);
            dl_pre(r, fn(copy, E_PRE), copy);
        }
        CHECK(copy && !r[0] && !strcmp(host_str, IN_USE), "in use, an upload over it is refused at +0x1c: no room for .new");
        memcpy(longname + k - 4 - 7, ".elemod", 8);   /* 4 shorter */
        CHECK(upload(longname, 300, 14, &why) == 0, "a name 4 shorter");
        path_of(held2, longname, "");
        CHECK(upload_as(longname, strcat(strcpy(nw, longname), ".new"), 300, 15, &why) == 0,
              "... in use, takes its .new");
        if (*why)
            printf("  (%s)\n", why);
        held_paths[0] = 0;
    }

    if (fails) {
        printf("FAILED %d\n", fails);
        return 1;
    }
    printf("PASSED\n");
    return 0;
}
