/* SPDX-License-Identifier: GPL-2.0-or-later */
/* drive-loader-ar2 (from drive-loader-a4mk2, 0.8): the Drive payload table and its reader (skip.h). The boot notes each
 * payload dl_stage_file skipped, keeps those of the mods it loaded, and sidecar-core reads them
 * from the +Drive later, from its own task, when the Pi is ready. From 0.12 the table is walked
 * through the versioned API (analog-rytm-mk2/drivepay.h, drive_payload_abi), the route and the
 * MODS page ask it which files are in use (dl_payload_held), and .status lists it. */
#include "dl.h"
#include "fs.h"
#include "skip.h"

struct drive_payload drive_payloads[DL_PAYLOADS];
u32 drive_payloads_n, drive_payloads_lost;

/* the payloads that found the table full, by the FNV-1a of their path (DL_LOST_IDS at most; more
 * are counted alone): a file staged twice (pass 2) counts once, and after dl_payload_keep only the
 * loaded mods' stay, for .status's count and the MODS page's mark */
static u32 lost_ids[DL_LOST_IDS];
static u32 lost_ids_n;

static u32 fnv(const char *s);

void dl_payload_reset(void)
{
    drive_payloads_n = drive_payloads_lost = lost_ids_n = 0;
}

static int lost_has(u32 h)
{
    u32 i;
    for (i = 0; i < lost_ids_n; i++)
        if (lost_ids[i] == h)
            return 1;
    return 0;
}

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
            u32 h = fnv(path);
            if (lost_has(h))
                return;                  /* pass 2: counted at pass 1 */
            if (lost_ids_n < DL_LOST_IDS)
                lost_ids[lost_ids_n++] = h;
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
    (void)sector;
    if ((size = (unsigned)dl_fs_file_size(path)) < 0x80000000u)   /* ekFS: no sector, the size alone */
        e->size = size;
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

/* a line's file as dl_read_file names it, DL_DIR "/" name, into out (DL_PATH_MAX) */
static void line_path(char *out, const char *name)
{
    const char *d = DL_DIR;
    u32 k = 0;
    while (*d && k < DL_PATH_MAX - 1)
        out[k++] = *d++;
    if (k < DL_PATH_MAX - 1)
        out[k++] = '/';
    while (*name && k < DL_PATH_MAX - 1)
        out[k++] = *name++;
    out[k] = 0;
}

void dl_payload_keep(const struct dl_status *st, const struct dl_choice *ch, int n)
{
    u32 i, kept = 0, lost = 0, over;
    int line;
    char id[DL_ID_MAX], path[DL_PATH_MAX];
    over = drive_payloads_lost > lost_ids_n ? drive_payloads_lost - lost_ids_n : 0;
    for (i = 0; i < lost_ids_n; i++) {   /* the lost ones of the mods that loaded stay */
        for (line = 0; line < n; line++) {
            if (st[line].state != DL_ST_LOADED)
                continue;
            line_path(path, ch[line].name);
            if (fnv(path) == lost_ids[i])
                break;
        }
        if (line < n)
            lost_ids[lost++] = lost_ids[i];
    }
    lost_ids_n = lost;
    drive_payloads_lost = lost + over;   /* past DL_LOST_IDS, loaded or not: counted as lost */
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

/* ---- the versioned API (analog-rytm-mk2/drivepay.h): sidecar-core imports these weakly ---- */

u32 drive_payload_count(void)
{
    return drive_payloads_n;
}

const struct drive_payload *drive_payload_at(u32 i)
{
    return i < drive_payloads_n ? &drive_payloads[i] : 0;
}

static int handle_ok(const struct drive_payload *p)
{
    return p && p >= drive_payloads && p < drive_payloads + drive_payloads_n;
}

s32 drive_payload_info(const struct drive_payload *p, u32 *id, u32 *len, u8 *sha256)
{
    if (!handle_ok(p))
        return -1;
    if (id)
        *id = p->id;
    if (len)
        *len = p->len;
    if (sha256)
        dl_memcpy(sha256, p->sha256, 32);
    return 0;
}

/* ---- in use (route.c, page.c): a loaded mod's file whose payload the table keeps ---- */

static int below_dir(const char *p, const char *dir)
{
    while (*dir && *p == *dir)
        p++, dir++;
    return !*dir && *p == '/';
}

int dl_payload_held(const char *path, int dir)
{
    u32 i;
    for (i = 0; i < drive_payloads_n; i++)
        if (dir ? below_dir(drive_payloads[i].path, path) : same_str(drive_payloads[i].path, path))
            return 1;
    return 0;
}

int dl_payload_lost_of(const char *path)
{
    return lost_has(fnv(path));
}

/* ---- .status's lines (route.c status_text) ---- */

static char *put_s(char *at, char *end, const char *s)
{
    while (*s && at < end - 1)
        *at++ = *s++;
    *at = 0;
    return at;
}

static char *put_u(char *at, char *end, u32 v)
{
    char t[11];
    int k = 0;
    do
        t[k++] = (char)('0' + v % 10);
    while (v /= 10);
    while (k && at < end - 1)
        *at++ = t[--k];
    *at = 0;
    return at;
}

static char *put_x(char *at, char *end, u32 v)
{
    int d;
    for (d = 28; d >= 0 && at < end - 1; d -= 4)
        *at++ = "0123456789abcdef"[v >> d & 15];
    *at = 0;
    return at;
}

u32 dl_payload_status_size(void)
{
    return DL_PAY_STATUS_HEAD + DL_PAY_STATUS_LINE * 2 * drive_payloads_n;
}

char *dl_payload_status(char *at, char *end)
{
    char pend[DL_PATH_MAX + 4];
    u32 i, k;
    at = put_s(at, end, "payloads: ");
    at = put_u(at, end, drive_payloads_n);
    at = put_s(at, end, " kept, ");
    at = put_u(at, end, drive_payloads_lost);
    at = put_s(at, end, " lost\n");
    if (dl_updates[0] || dl_updates[1]) {   /* the boot's swaps of <name>.new (boot.c) */
        at = put_s(at, end, "updates at boot: ");
        at = put_u(at, end, dl_updates[0]);
        at = put_s(at, end, " applied, ");
        at = put_u(at, end, dl_updates[1]);
        at = put_s(at, end, " failed\n");
    }
    for (i = 0; i < drive_payloads_n; i++) {
        const struct drive_payload *p = &drive_payloads[i];
        at = put_s(at, end, "payload ");
        at = put_x(at, end, p->id);
        at = put_s(at, end, " ");
        at = put_u(at, end, p->len);
        at = put_s(at, end, " ");
        at = put_s(at, end, p->path);
        at = put_s(at, end, "\n");
    }
    for (i = 0; i < drive_payloads_n; i++) {   /* an upload over a file in use waits as <name>.new */
        for (k = 0; drive_payloads[i].path[k] && k < DL_PATH_MAX - 1; k++)
            pend[k] = drive_payloads[i].path[k];
        put_s(pend + k, pend + sizeof pend, DL_NEW);
        if (dl_fs_file_size(pend) >= 0) {
            at = put_s(at, end, "pending ");
            at = put_s(at, end, drive_payloads[i].path);
            at = put_s(at, end, "\n");
        }
    }
    return at;
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
    if (!handle_ok(p) || at > p->len)
        return -1;
    if (n > p->len - at)
        n = p->len - at;
    dl_lock();
    (void)sector;
    size = (unsigned)dl_fs_file_size(p->path);
    if (size >= 0x80000000u || size != p->size || p->off > size || p->len > (size - p->off) / 2) {
        dl_unlock();
        return -2;
    }
    dl_memset(&f, 0, sizeof f);
    if (dl_fs_open(&f, p->path, 0) < 0)
        r = -2;
    while (!r && got < n) {
        dl_fs_seek(&f, p->off + 2 * at + 2 * got);   /* ekFS reads from where the file object says */
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
