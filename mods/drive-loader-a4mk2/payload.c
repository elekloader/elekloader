/* SPDX-License-Identifier: GPL-2.0-or-later */
/* drive-loader-a4mk2: the Drive payload table and its reader (skip.h). The boot notes each
 * payload dl_stage_file skipped, keeps those of the mods it loaded, and sidecar-core reads them
 * from the +Drive later, from its own task, when the Pi is ready. */
#include "dl.h"
#include "fs.h"
#include "skip.h"

struct drive_payload drive_payloads[DL_PAYLOADS];
u32 drive_payloads_n, drive_payloads_lost;

static int same_str(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
}

void dl_payload_note(const char *path, const struct dl_pay *pay)
{
    struct drive_payload *e = 0;
    u32 i, k;
    unsigned sector, size;
    for (i = 0; i < drive_payloads_n; i++)   /* pass 2 reads the file again */
        if (same_str(drive_payloads[i].path, path))
            e = &drive_payloads[i];
    if (!e) {
        if (drive_payloads_n >= DL_PAYLOADS) {
            drive_payloads_lost++;
            return;
        }
        e = &drive_payloads[drive_payloads_n++];
    }
    dl_memset(e, 0, sizeof *e);
    for (k = 0; path[k] && k < sizeof e->path - 1; k++)
        e->path[k] = path[k];
    e->len = pay->len;
    e->off = pay->off;
    dl_memcpy(e->sha256, pay->sha256, 32);
    if (ds_file(path, &sector, &size) == 0) {
        e->sector = sector;
        e->size = size;
    }
}

static u32 fnv(const char *s)
{
    u32 h = 0x811C9DC5u;
    while (*s)
        h = (h ^ (u8)*s++) * 0x01000193u;
    return h;
}

/* the line's file as dl_read_file names it: DL_DIR "/" name */
static int is_line(const char *path, const char *name)
{
    const char *d = DL_DIR;
    while (*d)
        if (*path++ != *d++)
            return 0;
    return *path++ == '/' && same_str(path, name);
}

void dl_payload_keep(const struct dl_status *st, const struct dl_choice *ch, int n)
{
    u32 i, kept = 0;
    int line;
    char id[DL_ID_MAX];
    for (i = 0; i < drive_payloads_n; i++) {
        for (line = 0; line < n; line++)
            if (st[line].state == DL_ST_LOADED && is_line(drive_payloads[i].path, ch[line].name))
                break;
        if (line == n)
            continue;                        /* not loaded: no payload to ship */
        dl_memcpy(id, st[line].id, DL_ID_MAX);
        id[DL_ID_MAX - 1] = 0;
        if (kept != i)
            dl_memcpy(&drive_payloads[kept], &drive_payloads[i], sizeof drive_payloads[i]);   /* no
                                         * struct copy: it would call memcpy, which nothing exports */
        drive_payloads[kept++].id = fnv(id);
    }
    drive_payloads_n = kept;
}

const struct drive_payload *drive_payload_find(u32 id)
{
    u32 i;
    for (i = 0; i < drive_payloads_n; i++)
        if (drive_payloads[i].id == id)
            return &drive_payloads[i];
    return 0;
}

static int nib(u8 c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10
         : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

s32 dl_payload_read(const struct drive_payload *p, u32 at, u8 *out, u32 n)
{
    dl_file f;
    unsigned sector, size;
    u8 hex[128];
    u32 got = 0, k, i;
    s32 r = 0;
    if (!p || p < drive_payloads || p >= drive_payloads + drive_payloads_n || at > p->len)
        return -1;
    if (n > p->len - at)
        n = p->len - at;
    dl_lock();
    if (ds_file(p->path, &sector, &size) || sector != p->sector || size != p->size
        || p->off > size || p->len > (size - p->off) / 2) {
        dl_unlock();
        return -2;
    }
    dl_memset(&f, 0, sizeof f);
    if (dl_fs_open(&f, p->path, 0) < 0)
        r = -2;
    f.pos = p->off + 2 * at;             /* a file open for reading streams from f.pos (store.c) */
    while (!r && got < n) {
        k = n - got < sizeof hex / 2 ? n - got : sizeof hex / 2;
        if (dl_fs_read(&f, hex, 2 * k) != (int)(2 * k)) {
            r = -3;
            break;
        }
        for (i = 0; i < k; i++) {
            int a = nib(hex[2 * i]), b = nib(hex[2 * i + 1]);
            if (a < 0 || b < 0) {
                r = -3;
                break;
            }
            out[got + i] = (u8)(a << 4 | b);
        }
        got += k;
    }
    dl_fs_close(&f);
    dl_unlock();
    return r ? r : (s32)got;
}
