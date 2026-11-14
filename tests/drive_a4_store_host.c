/* SPDX-License-Identifier: GPL-2.0-or-later */
/* drive-loader-a4mk2's store (mods/drive-loader-a4mk2/store.c) on the computer, over a card in RAM
 * that counts the sector writes and can fail one on demand (tests/test_drive_a4_store.py runs it).
 * Prints "ok" lines and exits 0, or a FAIL line and exits 1. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fs.h"

#define SECTORS 0x80000u
static unsigned char *card;
static long writes;
static long fail_at = -1;                 /* the write (counted from 0) that fails, -1 none */
static unsigned max_written;              /* the highest sector any write reached, +1 */
static unsigned min_written = 0xFFFFFFFFu;

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
    if (writes++ == fail_at)
        return -2;
    memset(card + (size_t)sector * 512, 0xA5, (size_t)n * 512);   /* stock's DMA buffer's leftovers */
    memcpy(card + (size_t)sector * 512, src, bytes);
    if (sector < min_written)
        min_written = sector;
    if (sector + n > max_written)
        max_written = sector + n;
    return 0;
}

unsigned ds_host_sectors(void)
{
    return SECTORS;
}

static int fails;
#define CHECK(c, what) do { if (!(c)) { printf("FAIL %s (line %d)\n", what, __LINE__); fails++; } else printf("ok %s\n", what); } while (0)

static void remount(void)
{
    ds_ready = 0;
    ds_mount();
}

static unsigned char pat(unsigned i, unsigned seed)
{
    return (unsigned char)(i * 31 + seed * 7 + (i >> 9));
}

static int put(const char *path, unsigned size, unsigned seed, unsigned chunk)
{
    dl_file f;
    unsigned char *b = malloc(size + 1);
    unsigned i, at = 0;
    int r;
    for (i = 0; i < size; i++)
        b[i] = pat(i, seed);
    r = dl_fs_open(&f, path, 1);
    while (!r && at < size) {
        unsigned k = size - at < chunk ? size - at : chunk;
        r = dl_fs_write(&f, b + at, k);
        at += k;
    }
    dl_fs_close(&f);
    free(b);
    return r ? r : dl_fs_file_size(path) == (int)size ? 0 : -9;
}

static int same_as(const char *path, unsigned size, unsigned seed, unsigned chunk)
{
    dl_file f;
    unsigned char *b = malloc(size + 1);
    unsigned i, at = 0;
    int ok = dl_fs_open(&f, path, 0) == 0 && dl_fs_size(&f) == (int)size;
    while (ok && at < size) {
        unsigned k = size - at < chunk ? size - at : chunk;
        ok = dl_fs_read(&f, b + at, k) == (int)k;
        at += k;
    }
    ok = ok && dl_fs_read(&f, b, 1) == 0;  /* at the end */
    dl_fs_close(&f);
    for (i = 0; ok && i < size; i++)
        ok = b[i] == pat(i, seed);
    free(b);
    return ok;
}

static int listed(const char *rel, const char *name, int dir)
{
    struct dl_ent e[64];
    int n = dl_list_dir(rel, e, 64), i;
    for (i = 0; i < n; i++)
        if (!strcmp(e[i].name, name) && e[i].dir == dir)
            return 1;
    return 0;
}

int main(void)
{
    static const unsigned sizes[] = {0, 1, 511, 512, 513, 4096, 100000, 1500001};
    unsigned i, sec, size, tok, largest, total;
    char p[DL_PATH_MAX + 80];
    card = calloc((size_t)SECTORS, 512);
    if (!card)
        return 2;
    memset(card, 0x5A, (size_t)DS_END * 512); /* a used card: not zeros where the store goes */

    CHECK(ds_mount() == 0 && dl_list_dir("", 0, 0) == 0, "a card with no store reads as an empty one");
    CHECK(writes == 0, "mounting writes nothing");
    CHECK(dl_fs_file_size(DL_DIR "/active") < 0, "no active");
    CHECK(dl_fs_mkdir(DL_DIR) == 0, "DL_DIR is always there");

    for (i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        snprintf(p, sizeof p, DL_DIR "/f%u.elemod", i);
        CHECK(put(p, sizes[i], i, 1000 + i * 77) == 0, "write a file");
        CHECK(same_as(p, sizes[i], i, 333 + i), "read it back in odd chunks");
        CHECK(same_as(p, sizes[i], i, 0x8000), "and in 32 KB stretches");
    }
    remount();
    for (i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        snprintf(p, sizeof p, DL_DIR "/f%u.elemod", i);
        CHECK(same_as(p, sizes[i], i, 4096), "every file after a remount");
    }
    CHECK(min_written >= DS_FIRST && max_written <= DS_END, "every write inside 0x3000..0x7a000");

    /* a write that fails leaves the old file */
    fail_at = writes + 1;
    CHECK(put(DL_DIR "/f6.elemod", 70000, 99, 0x8000) != 0, "a failed write is refused");
    fail_at = -1;
    CHECK(same_as(DL_DIR "/f6.elemod", 100000, 6, 4096), "and the old file stays");
    CHECK(put(DL_DIR "/f6.elemod", 70000, 99, 0x8000) == 0 && same_as(DL_DIR "/f6.elemod", 70000, 99, 4096),
          "a replaced file reads as the new one");

    /* a cut while the directory is written: the older copy stands */
    {
        unsigned g = ds_gen;
        fail_at = writes + 1;             /* the entries go, the header write fails */
        CHECK(dl_fs_delete(DL_DIR "/f1.elemod") != 0, "a delete whose header write fails is refused");
        fail_at = -1;
        remount();
        CHECK(ds_gen == g && dl_fs_file_size(DL_DIR "/f1.elemod") == 1, "the store is as before it");
        CHECK(dl_fs_delete(DL_DIR "/f1.elemod") == 0 && dl_fs_file_size(DL_DIR "/f1.elemod") < 0, "delete");
        remount();
        CHECK(dl_fs_file_size(DL_DIR "/f1.elemod") < 0 && ds_gen == g + 1, "and it stays deleted");
        /* the newer copy's entries torn (its header whole): the older copy is read */
        card[(size_t)(ds_gen & 1 ? DS_COPY_A : DS_COPY_B) * 512 + 512 + 3] ^= 0xFF;
        remount();
        CHECK(ds_gen == g && dl_fs_file_size(DL_DIR "/f1.elemod") == 1, "a torn newer copy falls back to the older");
        CHECK(dl_fs_delete(DL_DIR "/f1.elemod") == 0, "and the next change writes over the torn one");
        remount();
        CHECK(dl_fs_file_size(DL_DIR "/f1.elemod") < 0, "which then stands");
    }

    /* folders */
    CHECK(dl_fs_mkdir(DL_DIR "/a/b") != 0, "mkdir needs its parent");
    CHECK(dl_fs_mkdir(DL_DIR "/a") == 0 && dl_fs_mkdir(DL_DIR "/a/b") == 0 && dl_fs_mkdir(DL_DIR "/a/b/c") == 0,
          "mkdir, three deep");
    CHECK(dl_fs_mkdir(DL_DIR "/a") != 0, "mkdir of a folder there already");
    CHECK(put(DL_DIR "/a/b/c/x.elemod", 9000, 5, 512) == 0, "a file in it");
    CHECK(put(DL_DIR "/nope/x.elemod", 10, 5, 512) != 0, "no file without its folder");
    CHECK(listed("", "a", 1) && listed("a", "b", 1) && listed("a/b/c", "x.elemod", 0) && !listed("", "b", 1),
          "listings show each level's own entries");
    CHECK(dl_fs_rmdir(DL_DIR "/a/b") != 0, "rmdir of a folder that isn't empty");
    CHECK(dl_fs_rename(DL_DIR "/a/b", DL_DIR "/a/b/c/d") != 0, "a folder can't move into itself");
    CHECK(dl_fs_rename(DL_DIR "/a/b", DL_DIR "/moved") == 0, "a folder moves to another parent");
    CHECK(same_as(DL_DIR "/moved/c/x.elemod", 9000, 5, 700) && dl_fs_file_size(DL_DIR "/a/b/c/x.elemod") < 0,
          "with everything below it");
    CHECK(dl_fs_rename(DL_DIR "/f2.elemod", DL_DIR "/moved/f2.elemod") == 0
          && same_as(DL_DIR "/moved/f2.elemod", 511, 2, 100), "a file moves");
    CHECK(dl_fs_rename(DL_DIR "/f3.elemod", DL_DIR "/moved/f2.elemod") != 0, "not over a file there");
    CHECK(dl_fs_delete(DL_DIR "/moved/c/x.elemod") == 0 && dl_fs_rmdir(DL_DIR "/moved/c") == 0, "an empty folder goes");
    CHECK(dl_fs_delete(DL_DIR "/moved") != 0, "delete is for files");
    CHECK(dl_fs_open(&(dl_file){0}, "/projects/x", 1) != 0 && dl_fs_mkdir("/other") != 0,
          "nothing outside DL_DIR");

    /* the route's reservations */
    ds_room(&largest, &total);
    CHECK(largest > 200u << 20 && total >= largest, "about 238 MiB free");
    sec = ds_reserve(300000, &tok);
    CHECK(sec >= DS_DATA && tok, "a reservation");
    {
        unsigned tok2, sec2 = ds_reserve(1000, &tok2);
        CHECK(sec2 && tok2 != tok, "a newer reservation");
        CHECK(ds_commit(DL_DIR "/up.elemod", tok, 300000) != 0, "drops the older one");
        memset(card + (size_t)sec2 * 512, 0x42, 1000);
        CHECK(ds_commit(DL_DIR "/up.elemod", tok2, 1025) != 0, "a commit larger than its run is refused");
        CHECK(ds_commit(DL_DIR "/up.elemod", tok2, 1000) == 0 && ds_file(DL_DIR "/up.elemod", &sec, &size) == 0
              && sec == sec2 && size == 1000, "a commit makes the run the file");
        CHECK(ds_commit(DL_DIR "/up.elemod", tok2, 1000) != 0, "once");
    }
    CHECK(ds_reserve(DS_END * 512u, &tok) == 0 && !tok, "a reservation over the room is refused");

    /* no two files share a sector, after churn */
    for (i = 0; i < 40; i++) {
        snprintf(p, sizeof p, DL_DIR "/c%u", i % 13);
        if (i % 5 == 4)
            dl_fs_delete(p);
        else
            put(p, 3000 + i * 4111, i, 4096);
    }
    {
        struct dl_ent e[64];
        int n = dl_list_dir("", e, 64), j, k, ok = 1;
        unsigned s[64], z[64], m = 0;
        for (j = 0; j < n; j++) {
            snprintf(p, sizeof p, DL_DIR "/%s", e[j].name);
            if (!e[j].dir && ds_file(p, &s[m], &z[m]) == 0 && z[m])
                m++;
        }
        for (j = 0; j < (int)m; j++) {
            ok = ok && s[j] >= DS_DATA && s[j] + (z[j] + 511) / 512 <= DS_END && s[j] % 8 == 0;
            for (k = 0; k < j; k++)
                ok = ok && (s[j] + (z[j] + 511) / 512 <= s[k] || s[k] + (z[k] + 511) / 512 <= s[j]);
        }
        CHECK(ok && m > 10, "files' runs are apart, aligned and inside the data range");
    }

    /* FORMAT +DRIVE */
    ds_format();
    CHECK(dl_list_dir("", 0, 0) == 0 && dl_fs_file_size(DL_DIR "/f0.elemod") < 0, "format empties the store");
    remount();
    CHECK(dl_list_dir("", 0, 0) == 0, "for good: both copies");
    CHECK(put(DL_DIR "/active", 20, 1, 20) == 0 && same_as(DL_DIR "/active", 20, 1, 20), "and it works after");
    CHECK(min_written >= DS_FIRST && max_written <= DS_END, "still nothing written outside the range");

    printf("%s\n", fails ? "FAILED" : "PASSED");
    return fails ? 1 : 0;
}
