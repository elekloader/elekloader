/* SPDX-License-Identifier: GPL-2.0-or-later */
/* drive-loader-a4mk2 0.2's Drive payload table (payload.c) and the boot's update swap (boot.c
 * dl_apply_updates, store.c ds_swap), built for the computer over the loader's own store
 * (store.c, -DDL_HOST) on a card in RAM: tests/test_drive_a4_payload.py builds it with the host's
 * cc and drives it. The requests are tests/drive_payload_host.c's, over the store:
 *   file NAME TEXT       DL_DIR "/" NAME holds TEXT ("|" a newline), written through the store
 *   pad K                the next hexfiles (and their badhex, note) put K spaces before HEAD, so
 *                        the hex starts at another place in a sector (HEAD is 64 bytes)
 *   hexfile NAME N       DL_DIR "/" NAME holds HEAD, then N payload bytes as hex (byte i is
 *                        (i * 13 + 1) & 0xff), then TAIL; the payload's hex starts at HEADLEN
 *   badhex NAME K        byte K of NAME's hex becomes 'g', on the card (the file stays where it is)
 *   note NAME LEN        the boot staged NAME and skipped LEN bytes of payload at HEADLEN (pass 1
 *                        or 2: dl_payload_note)
 *   keep NAME=ID=S ...   the boot's set, in order, S "L" loaded or "R" refused (dl_payload_keep)
 *   reset                a new boot (dl_payload_reset)
 *   count                -> "COUNT n LOST m"
 *   at I                 -> "AT id len path" or "AT none" (drive_payload_at, drive_payload_info)
 *   find ID              -> "FIND path" or "FIND none" (drive_payload_find, the FNV-1a of ID)
 *   held REL DIR         -> "HELD 0|1" (dl_payload_held, REL below DL_DIR)
 *   lostof NAME          -> "LOSTOF 0|1"
 *   status               -> "STATUS line~line~..." (dl_payload_status)
 *   read I AT N [BUF]    dl_payload_read of entry I into a buffer of BUF bytes (N if absent) with
 *                        guard bytes around it -> "READ r ok|bad GUARD ok|bad READS k": ok when the
 *                        bytes match the payload's, k the card reads it made
 *   touch NAME           NAME written again one byte longer (a new run and size)
 *   updates NAME[=CRC] ...  dl_apply_updates on that set (CRC hex, else 0) -> "UPDATES applied
 *                        failed" and the set's CRCs after it, "CRCS name=crc ..."
 *   failswap PATH        the store's swap of PATH (below DL_DIR) fails, as a card write would
 *   cut K                a power cut: the card takes K more sector writes and then no more
 *   remount              the store read again from the card (a restart after the cut)
 *   tree                 -> "TREE name=text ; ..." (the files below DL_DIR, sorted; a newline as "|")
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../mods/drive-loader-a4mk2/dl.h"
#include "../mods/drive-loader-a4mk2/fs.h"
#include "../mods/drive-loader-a4mk2/skip.h"

#define HEAD "{\"sections\":{\".payload\":{\"len\":0,\"sha256\":\"00\",\"parts\":[[\"hex\",\""
#define TAIL "\"]]}}}"
static unsigned headpad;                 /* spaces before HEAD (pad) */
#define HEADLEN (headpad + (unsigned)strlen(HEAD))

void dl_memcpy(void *d, const void *s, u32 n) { memmove(d, s, n); }
void dl_memset(void *d, int c, u32 n) { memset(d, c, n); }
u32 dl_strlen(const char *s) { return (u32)strlen(s); }
int dl_strcmp(const char *a, const char *b) { return strcmp(a, b); }
void *dl_alloc(u32 n) { return calloc(1, n); }
void dl_free(void *p) { free(p); }
u32 dl_crc32_add(u32 c, const u8 *p, u32 n)
{
    u32 k;
    while (n--)
        for (c ^= *p++, k = 0; k < 8; k++)
            c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    return c;
}

/* ---- the card ---- */
#define SECTORS DS_END
static unsigned char *card;
static int reads;
static long cut_left = -1;               /* sector writes the card still takes, -1 no cut */

int ds_host_read(unsigned sector, unsigned bytes, void *dest)
{
    if (sector + (bytes + 511) / 512 > SECTORS)
        return -1;
    reads++;
    memcpy(dest, card + (size_t)sector * 512, bytes);
    return 0;
}

int ds_host_write(unsigned sector, unsigned bytes, const void *src)
{
    unsigned n = (bytes + 511) / 512, i;
    if (sector + n > SECTORS)
        return -10;
    for (i = 0; i < n; i++, cut_left -= cut_left > 0) {   /* a sector at a time, as power goes */
        unsigned k = bytes - i * 512 < 512 ? bytes - i * 512 : 512;
        if (!cut_left)
            return 0;                    /* the card is off: the loader never hears of it */
        memset(card + (size_t)(sector + i) * 512, 0xA5, 512);
        memcpy(card + (size_t)(sector + i) * 512, (const char *)src + i * 512, k);
    }
    return 0;
}

unsigned ds_host_sectors(void) { return SECTORS; }

/* the store's ds_swap (built as store_ds_swap, -Dds_swap=store_ds_swap on store.c alone) behind
 * one that can fail a path */
int store_ds_swap(const char *path, const char *newpath);
static char failing[DL_PATH_MAX + 8];
int ds_swap(const char *path, const char *newpath)
{
    if (!strcmp(path, failing))
        return -1;
    return store_ds_swap(path, newpath);
}

static u8 pay_byte(u32 i) { return (u8)((i * 13 + 1) & 0xff); }

static u32 fnv(const char *s)
{
    u32 h = 0x811C9DC5u;
    while (*s)
        h = (h ^ (u8)*s++) * 0x01000193u;
    return h;
}

static void full(char *out, const char *name) { snprintf(out, DL_PATH_MAX + 8, "%s/%s", DL_DIR, name); }

/* NAME's folders made, then NAME written whole through the store */
static void put(const char *name, const char *data, unsigned n)
{
    char p[DL_PATH_MAX + 8];
    dl_file f;
    const char *s;
    for (s = strchr(name, '/'); s; s = strchr(s + 1, '/')) {
        char d[DL_PATH_MAX + 8];
        snprintf(d, sizeof d, "%s/%.*s", DL_DIR, (int)(s - name), name);
        dl_fs_mkdir(d);
    }
    full(p, name);
    if (dl_fs_open(&f, p, 1) || dl_fs_write(&f, data, n))
        printf("PUT FAILED %s\n", name);
    dl_fs_close(&f);
}

static char *get(const char *path, unsigned *n)
{
    dl_file f;
    int size = dl_fs_file_size(path);
    char *b = calloc(1, (size_t)(size < 0 ? 0 : size) + 1);
    *n = 0;
    memset(&f, 0, sizeof f);
    if (size > 0 && dl_fs_open(&f, path, 0) == 0 && dl_fs_read(&f, b, (unsigned)size) == size)
        *n = (unsigned)size;
    dl_fs_close(&f);
    return b;
}

static char *names[4096];
static int nnames;

static void walk(const char *rel)
{
    struct dl_ent e[256];
    int n = dl_list_dir(rel, e, 256), i;
    for (i = 0; i < n; i++) {
        char p[DL_NAME_MAX + 2];
        snprintf(p, sizeof p, "%s%s%s", rel, rel[0] ? "/" : "", e[i].name);
        if (e[i].dir)
            walk(p);
        else
            names[nnames++] = strdup(p);
    }
}

static int cmp(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

int main(void)
{
    static char line[1 << 21];
    static struct dl_status st[256];
    static struct dl_choice ch[256];
    char path[DL_PATH_MAX + 8];
    card = calloc(SECTORS, 512);
    if (ds_mount())
        printf("MOUNT FAILED\n");
    while (fgets(line, sizeof line, stdin)) {
        char *a = line, *c;
        line[strcspn(line, "\r\n")] = 0;
        if (!strncmp(line, "file ", 5)) {
            char *name = line + 5, *text = strchr(name, ' ');
            *text++ = 0;
            for (c = text; *c; c++)
                if (*c == '|')
                    *c = '\n';
            put(name, text, (unsigned)strlen(text));
        } else if (!strncmp(line, "pad ", 4)) {
            headpad = (unsigned)strtoul(line + 4, 0, 0);
        } else if (!strncmp(line, "hexfile ", 8)) {
            char *name = line + 8, *num = strchr(name, ' '), *buf;
            u32 n, i, k;
            *num++ = 0;
            n = (u32)strtoul(num, 0, 0);
            buf = malloc(headpad + sizeof HEAD + 2 * n + sizeof TAIL);
            memset(buf, ' ', headpad);
            memcpy(buf + headpad, HEAD, strlen(HEAD));
            k = HEADLEN;
            for (i = 0; i < n; i++)
                k += (u32)sprintf(buf + k, i & 1 ? "%02X" : "%02x", pay_byte(i));   /* both cases */
            memcpy(buf + k, TAIL, strlen(TAIL));
            put(name, buf, k + (u32)strlen(TAIL));
            free(buf);
        } else if (!strncmp(line, "badhex ", 7)) {
            char *name = line + 7, *num = strchr(name, ' ');
            unsigned sector, size;
            *num++ = 0;
            full(path, name);
            ds_file(path, &sector, &size);
            card[(size_t)sector * 512 + HEADLEN + strtoul(num, 0, 0)] = 'g';
        } else if (!strncmp(line, "touch ", 6)) {
            unsigned n;
            char *b;
            full(path, line + 6);
            b = get(path, &n);
            b = realloc(b, n + 2);
            b[n++] = ' ';
            put(line + 6, b, n);
            free(b);
        } else if (!strncmp(line, "note ", 5)) {
            char *name = line + 5, *num = strchr(name, ' ');
            struct dl_pay pay;
            u32 i;
            *num++ = 0;
            memset(&pay, 0, sizeof pay);
            pay.off = HEADLEN;
            pay.len = (u32)strtoul(num, 0, 0);
            for (i = 0; i < 32; i++)
                pay.sha256[i] = (u8)i;
            full(path, name);
            dl_payload_note(path, &pay);
        } else if (!strncmp(line, "keep", 4)) {
            int n = 0;
            for (a = strtok(line + 4, " "); a && n < 256; a = strtok(0, " "), n++) {
                char *id = strchr(a, '='), *s = strrchr(a, '=');
                *id++ = 0;
                *s++ = 0;
                memset(&st[n], 0, sizeof st[n]);
                strncpy(ch[n].name, a, sizeof ch[n].name - 1);
                strncpy(st[n].id, id, DL_ID_MAX - 1);
                st[n].state = *s == 'L' ? DL_ST_LOADED : DL_ST_REFUSED;
            }
            dl_payload_keep(st, ch, n);
        } else if (!strcmp(line, "reset")) {
            dl_payload_reset();
        } else if (!strcmp(line, "count")) {
            printf("COUNT %u LOST %u\n", drive_payload_count(), drive_payloads_lost);
        } else if (!strncmp(line, "at ", 3)) {
            const struct drive_payload *h = drive_payload_at((u32)strtoul(line + 3, 0, 0));
            u32 id, len;
            u8 sha[32];
            if (drive_payload_info(h, &id, &len, sha))
                printf("AT none\n");
            else
                printf("AT %08x %u %s sha%s\n", id, len, h->path, sha[31] == 31 ? "ok" : "bad");
        } else if (!strncmp(line, "find ", 5)) {
            const struct drive_payload *h = drive_payload_find(fnv(line + 5));
            printf("FIND %s\n", h ? h->path : "none");
        } else if (!strncmp(line, "held ", 5)) {
            char *rel = line + 5, *d = strrchr(rel, ' ');
            *d++ = 0;
            full(path, rel);
            printf("HELD %d\n", dl_payload_held(path, atoi(d)));
        } else if (!strncmp(line, "lostof ", 7)) {
            full(path, line + 7);
            printf("LOSTOF %d\n", dl_payload_lost_of(path));
        } else if (!strcmp(line, "status")) {
            u32 cap = dl_payload_status_size();
            char *t = malloc(cap + 16), *end;
            memset(t, 0x5a, cap + 16);
            end = dl_payload_status(t, t + cap);
            printf("STATUS ");
            for (c = t; c < end; c++)
                putchar(*c == '\n' ? '~' : *c);
            printf("%s\n", (u32)(end - t) < cap && (u8)t[cap] == 0x5a ? "" : " OVER");
            free(t);
        } else if (!strncmp(line, "read ", 5)) {
            u32 i = (u32)strtoul(line + 5, &a, 0), at = (u32)strtoul(a, &a, 0), n = (u32)strtoul(a, &a, 0);
            u32 buf = *a ? (u32)strtoul(a, 0, 0) : n, k;
            u8 *mem = malloc(buf + 64);
            s32 r;
            int ok = 1, guard = 1;
            memset(mem, 0xa5, buf + 64);
            reads = 0;
            r = dl_payload_read(drive_payload_at(i), at, mem + 32, n);
            for (k = 0; r > 0 && k < (u32)r; k++)
                ok &= mem[32 + k] == pay_byte(at + k);
            for (k = 0; k < 32; k++)
                guard &= mem[k] == 0xa5 && mem[32 + buf + k] == 0xa5;
            printf("READ %d %s GUARD %s READS %d\n", r, ok ? "ok" : "bad", guard ? "ok" : "bad", reads);
            free(mem);
        } else if (!strncmp(line, "failswap ", 9)) {
            full(failing, line + 9);
        } else if (!strncmp(line, "cut ", 4)) {
            cut_left = strtol(line + 4, 0, 0);
        } else if (!strcmp(line, "remount")) {
            cut_left = -1;
            ds_ready = 0;
            ds_mount();
        } else if (!strncmp(line, "updates", 7)) {
            int n = 0, k;
            memset(ch, 0, sizeof ch);
            for (a = strtok(line + 7, " "); a && n < 256; a = strtok(0, " "), n++) {
                char *eq = strchr(a, '=');
                if (eq) {
                    *eq = 0;
                    ch[n].crc = (u32)strtoul(eq + 1, 0, 16);
                }
                strncpy(ch[n].name, a, sizeof ch[n].name - 1);
            }
            dl_apply_updates(ch, n);
            printf("UPDATES %u %u\nCRCS", dl_updates[0], dl_updates[1]);
            for (k = 0; k < n; k++)
                printf(" %s=%08x", ch[k].name, ch[k].crc);
            printf("\n");
        } else if (!strcmp(line, "tree")) {
            int i;
            nnames = 0;
            walk("");
            qsort(names, (size_t)nnames, sizeof *names, cmp);
            printf("TREE");
            for (i = 0; i < nnames; i++) {
                unsigned q, n;
                char *b;
                full(path, names[i]);
                b = get(path, &n);
                printf("%s%s=", i ? " ; " : " ", names[i]);
                for (q = 0; q < n; q++)          /* a newline as "|", as `file` takes it */
                    putchar(b[q] == '\n' ? '|' : b[q]);
                free(b);
                free(names[i]);
            }
            printf("\n");
        }
    }
    return 0;
}
