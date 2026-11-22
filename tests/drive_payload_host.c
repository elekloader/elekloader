/* SPDX-License-Identifier: GPL-2.0-or-later */
/* drive-loader-ar2 0.12's Drive payload table (payload.c) and the boot's update swap (boot.c
 * dl_apply_updates), built for the computer over a made-up ekFS (absolute paths, files only):
 * tests/test_drive_payload.py builds it with the host's cc and drives it.
 * stdin, one request a line:
 *   file NAME TEXT       DL_DIR "/" NAME holds TEXT ("|" a newline)
 *   hexfile NAME N       DL_DIR "/" NAME holds HEAD, then N payload bytes as hex (byte i is
 *                        (i * 13 + 1) & 0xff), then TAIL; the payload's hex starts at HEADLEN
 *   badhex NAME K        byte K of NAME's hex becomes 'g'
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
 *                        bytes match the payload's, k the ekFS reads it made
 *   touch NAME           NAME grows by one byte (the size the boot noted no longer holds)
 *   updates NAME[=CRC] ...  dl_apply_updates on that set (CRC hex, else 0) -> "UPDATES applied
 *                        failed" and the set's CRCs after it, "CRCS name=crc ..."
 *   failrename PATH      the made-up rename of PATH (below DL_DIR) to anything fails
 *   tree                 -> "TREE name=text ; ..." (below DL_DIR, sorted; a newline as "|")
 *   cut N                a power cut after the next N ekFS changes (an open to write, a write, a
 *                        delete, a rename): later ones change nothing and fail; -1 none
 *   changes              -> "CHANGES n", the ekFS changes made since the last cut line
 *   boot                 the next boot's swap: the cut ends, dl_read_active, its "name crc" lines
 *                        to dl_apply_updates -> as updates
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../mods/drive-loader-ar2/dl.h"
#include "../mods/drive-loader-ar2/fs.h"
#include "../mods/drive-loader-ar2/skip.h"

#define HEAD "{\"sections\":{\".payload\":{\"len\":0,\"sha256\":\"00\",\"parts\":[[\"hex\",\""
#define TAIL "\"]]}}}"

void dl_memcpy(void *d, const void *s, u32 n) { memmove(d, s, n); }
void dl_memset(void *d, int c, u32 n) { memset(d, c, n); }
u32 dl_strlen(const char *s) { return (u32)strlen(s); }
int dl_strcmp(const char *a, const char *b) { return strcmp(a, b); }
static int locked;
void dl_lock(void) { locked++; }
void dl_unlock(void) { locked--; }

#define MAXF 256
static struct { char path[DL_PATH_MAX + 8]; char *data; unsigned n; } fsn[MAXF];
static int nfs, reads;
static char failing[DL_PATH_MAX + 8];
static int cut_left = -1, changes;       /* cut: the changes the power holds for, -1 all */

/* an ekFS change: 0 to make it, -1 when the power was cut before it */
static int change(void)
{
    if (!cut_left)
        return -1;
    if (cut_left > 0)
        cut_left--;
    changes++;
    return 0;
}

static int find(const char *p)
{
    int i;
    for (i = 0; i < nfs; i++)
        if (fsn[i].path[0] && !strcmp(fsn[i].path, p))
            return i;
    return -1;
}

static void full(char *out, const char *name) { snprintf(out, DL_PATH_MAX + 8, "%s/%s", DL_DIR, name); }

static int put(const char *name, const char *data, unsigned n)
{
    char p[DL_PATH_MAX + 8];
    int i;
    full(p, name);
    i = find(p);
    if (i < 0) {
        i = nfs++;
        strcpy(fsn[i].path, p);
    }
    free(fsn[i].data);
    fsn[i].data = malloc(n + 1);
    memcpy(fsn[i].data, data, n);
    fsn[i].n = n;
    return i;
}

int dl_fs_file_size(const char *p) { int i = find(p); return i < 0 ? -1 : (int)fsn[i].n; }
int dl_fs_delete(const char *p) { int i = find(p); if (i < 0 || change()) return -1; fsn[i].path[0] = 0; return 0; }
int dl_fs_rename(const char *a, const char *b)
{
    int i = find(a);
    if (i < 0 || find(b) >= 0 || !strcmp(a, failing) || change())
        return -1;
    strcpy(fsn[i].path, b);
    return 0;
}
int dl_fs_open(dl_file *f, const char *p, int write)
{
    int *s = (int *)f->raw, i = find(p);
    if (write && change())
        i = -1;
    else if (write) {                    /* made, or emptied */
        if (i < 0) {
            i = nfs++;
            strcpy(fsn[i].path, p);
        }
        free(fsn[i].data);
        fsn[i].data = 0;
        fsn[i].n = 0;
    }
    s[0] = i;
    s[1] = 0;
    return i < 0 ? -1 : 0;
}
void dl_fs_seek(dl_file *f, unsigned at) { ((int *)f->raw)[1] = (int)at; }
int dl_fs_read(dl_file *f, void *buf, unsigned n)
{
    int *s = (int *)f->raw;
    unsigned left;
    if (s[0] < 0)
        return -1;
    reads++;
    left = (unsigned)s[1] < fsn[s[0]].n ? fsn[s[0]].n - (unsigned)s[1] : 0;
    if (n > left)
        n = left;
    memcpy(buf, fsn[s[0]].data + s[1], n);
    s[1] += (int)n;
    return (int)n;
}
int dl_fs_write(dl_file *f, const void *buf, unsigned n)
{
    int i = ((int *)f->raw)[0];
    if (i < 0 || change())
        return -1;
    fsn[i].data = realloc(fsn[i].data, fsn[i].n + n + 1);
    memcpy(fsn[i].data + fsn[i].n, buf, n);
    fsn[i].n += n;
    return 0;
}
void dl_fs_close(dl_file *f) { (void)f; }
int dl_fs_size(dl_file *f) { int i = ((int *)f->raw)[0]; return i < 0 ? -1 : (int)fsn[i].n; }
void *dl_alloc(u32 n) { return calloc(1, n); }
void dl_free(void *p) { free(p); }
static unsigned char stage[64];          /* small: a file staged past it still gives its CRC */
unsigned char *volatile dl_stage_at = stage;
volatile unsigned dl_stage_len = sizeof stage;
u32 dl_crc32_add(u32 c, const u8 *p, u32 n)
{
    u32 k;
    while (n--)
        for (c ^= *p++, k = 0; k < 8; k++)
            c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    return c;
}

static u8 pay_byte(u32 i) { return (u8)((i * 13 + 1) & 0xff); }

static u32 fnv(const char *s)
{
    u32 h = 0x811C9DC5u;
    while (*s)
        h = (h ^ (u8)*s++) * 0x01000193u;
    return h;
}

static int cmp(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

int main(void)
{
    static char line[8192];
    static struct dl_status st[256];
    static struct dl_choice ch[256];
    char path[DL_PATH_MAX + 8];
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
        } else if (!strncmp(line, "hexfile ", 8)) {
            char *name = line + 8, *num = strchr(name, ' '), *buf;
            u32 n, i, k;
            *num++ = 0;
            n = (u32)strtoul(num, 0, 0);
            buf = malloc(sizeof HEAD + 2 * n + sizeof TAIL);
            k = (u32)strlen(HEAD);
            memcpy(buf, HEAD, k);
            for (i = 0; i < n; i++)
                k += (u32)sprintf(buf + k, i & 1 ? "%02X" : "%02x", pay_byte(i));   /* both cases */
            memcpy(buf + k, TAIL, strlen(TAIL));
            put(name, buf, k + (u32)strlen(TAIL));
            free(buf);
        } else if (!strncmp(line, "badhex ", 7)) {
            char *name = line + 7, *num = strchr(name, ' ');
            *num++ = 0;
            full(path, name);
            fsn[find(path)].data[strlen(HEAD) + strtoul(num, 0, 0)] = 'g';
        } else if (!strncmp(line, "touch ", 6)) {
            int i;
            full(path, line + 6);
            i = find(path);
            fsn[i].data = realloc(fsn[i].data, fsn[i].n + 2);
            fsn[i].data[fsn[i].n++] = ' ';
        } else if (!strncmp(line, "note ", 5)) {
            char *name = line + 5, *num = strchr(name, ' ');
            struct dl_pay pay;
            u32 i;
            *num++ = 0;
            memset(&pay, 0, sizeof pay);
            pay.off = (u32)strlen(HEAD);
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
        } else if (!strncmp(line, "failrename ", 11)) {
            full(failing, line + 11);
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
        } else if (!strncmp(line, "cut ", 4)) {
            cut_left = atoi(line + 4);
            changes = 0;
        } else if (!strcmp(line, "changes")) {
            printf("CHANGES %d\n", changes);
        } else if (!strcmp(line, "boot")) {
            char *text, *e;
            s32 len;
            int n = 0, k;
            cut_left = -1;
            memset(ch, 0, sizeof ch);
            len = dl_read_active(&text);
            for (a = text; len > 0 && a < text + len && n < 256; a = e + 1) {
                char name[DL_PATH_MAX];
                unsigned crc;
                e = memchr(a, '\n', (size_t)(text + len - a));
                if (!e)
                    e = text + len;
                if (*a != '#' && sscanf(a, "%63s %x", name, &crc) == 2) {
                    strncpy(ch[n].name, name, sizeof ch[n].name - 1);
                    ch[n++].crc = crc;
                }
            }
            dl_free(text);
            dl_apply_updates(ch, n);
            printf("UPDATES %u %u\nCRCS", dl_updates[0], dl_updates[1]);
            for (k = 0; k < n; k++)
                printf(" %s=%08x", ch[k].name, ch[k].crc);
            printf("\n");
        } else if (!strcmp(line, "tree")) {
            char *v[MAXF];
            int i, n = 0;
            size_t k = strlen(DL_DIR "/");
            for (i = 0; i < nfs; i++)
                if (fsn[i].path[0])
                    v[n++] = fsn[i].path;
            qsort(v, (size_t)n, sizeof *v, cmp);
            printf("TREE");
            for (i = 0; i < n; i++)
            {
                int j = find(v[i]);
                unsigned q;
                printf("%s%s=", i ? " ; " : " ", v[i] + k);
                for (q = 0; q < fsn[j].n; q++)    /* a newline as "|", as `file` takes it */
                    putchar(fsn[j].data[q] == '\n' ? '|' : fsn[j].data[q]);
            }
            printf("\n");
        }
        if (locked)
            printf("LOCK LEFT %d\n", locked);
    }
    return 0;
}
