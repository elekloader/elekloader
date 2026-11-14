/* SPDX-License-Identifier: GPL-2.0-or-later */
/* drive-loader-a4mk2 (Analog Four MKII, OS 1.56): the loader's own file store in the +Drive's
 * sectors 0x3000..0x7a000, which stock never addresses (README, "The store"; fs.h).
 *
 * Layout: two directory copies (DS_COPY_A, DS_COPY_B), each a header sector followed by the
 * entries; then one scratch sector; then the files, each one run of sectors. A change writes the
 * whole directory into the copy that isn't the newest, entries first and its header last, with
 * the generation one higher: a power cut mid-write leaves that copy's header old or its CRC wrong,
 * and the other copy stands. Files are written before the entry that names them, into sectors no
 * entry holds, so a cut mid-write loses only that write. No header on the files: stock reads none
 * of these sectors (decided 2026-11-14).
 *
 * Every call takes the store's mutex; the sector calls take stock's card mutex themselves. */
#include "fs.h"

#ifdef DL_HOST
/* tests/drive_a4_store_host.c: a card in RAM and no mutex */
int ds_host_read(unsigned sector, unsigned bytes, void *dest);
int ds_host_write(unsigned sector, unsigned bytes, const void *src);
unsigned ds_host_sectors(void);
#define SD_READ ds_host_read
#define SD_WRITE ds_host_write
#define SD_OK() 1
#define SD_SECTORS() ds_host_sectors()
#define LOCK(m) ((void)(m))
#define UNLOCK(m) ((void)(m))
#else
/* raw sector calls (512-byte sectors; each takes the card mutex 0x428a7590, bounds-checks against
 * the card's sector count and goes through stock's DMA buffer in pieces of 64 KB): 0 or < 0. A
 * write copies `bytes` into the DMA buffer and writes whole sectors, so a write of a partial
 * sector writes whatever the buffer held after it: the store writes whole sectors only. */
#define SD_READ ((int (*)(unsigned, unsigned, void *))0x400721a6)
#define SD_WRITE ((int (*)(unsigned, unsigned, const void *))0x40072376)
#define SD_INIT_RESULT (*(volatile unsigned *)0x41865e98)     /* 0: SD init succeeded */
#define SD_PRESENT (*(volatile unsigned *)0x41809674)         /* nonzero: a card */
#define SD_SECTORS() (*(volatile unsigned *)0x41809698)       /* the card's sector count */
#define SD_OK() (SD_INIT_RESULT == 0 && SD_PRESENT != 0)
#define MUTEX_LOCK ((void (*)(void *))0x40001c54)
#define MUTEX_UNLOCK ((void (*)(void *))0x40001d86)
#define LOCK(m) MUTEX_LOCK(m)
#define UNLOCK(m) MUTEX_UNLOCK(m)
#endif

#define SECTOR 512u
#define ALIGN 8u                          /* a file's first sector: 4 KB steps */
#define RESERVATIONS 2                    /* 0 the route's upload, 1 the streamed writer */

struct ds_head {
    unsigned magic;                       /* DS_MAGIC */
    unsigned short version, ent;          /* 1, sizeof(struct ds_ent) */
    unsigned gen;                         /* the newer copy has the higher */
    unsigned count;                       /* entries */
    unsigned ents_crc;                    /* CRC-32 of the count entries */
    unsigned first, end;                  /* DS_FIRST, DS_END: a layout check */
    unsigned head_crc;                    /* CRC-32 of the words before it */
};

static unsigned ds_mutex[3];              /* {owner, wait head, wait tail}; never taken twice */
static unsigned dl_mutex[3];
static struct ds_ent ents[DS_MAX];
static unsigned count;
static unsigned char cur;                 /* the copy last read or written: 0 A, 1 B */
static unsigned char sbuf[SECTOR] __attribute__((aligned(4)));   /* a partial sector */
static unsigned char wbuf[SECTOR] __attribute__((aligned(4)));   /* the writer's tail */
static unsigned wfill;

unsigned char ds_ready;
unsigned ds_gen;

static struct { unsigned sector, nsec, token; } res[RESERVATIONS];
static unsigned tokens;

unsigned char drive_staging[DL_STAGING_SIZE] __attribute__((aligned(16)));
volatile int dl_staging_owner;
volatile unsigned dl_staging_epoch;

/* ---- small helpers (no libc) ---- */

static unsigned slen(const char *s)
{
    unsigned n = 0;
    while (s[n])
        n++;
    return n;
}

static void mcopy(void *d, const void *s, unsigned n)
{
    unsigned char *a = d;
    const unsigned char *b = s;
    while (n--)
        *a++ = *b++;
}

static void mzero(void *d, unsigned n)
{
    unsigned char *a = d;
    while (n--)
        *a++ = 0;
}

static int seq(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
}

static unsigned crc32(const void *p, unsigned n)
{
    const unsigned char *b = p;
    unsigned c = 0xFFFFFFFFu;
    int k;
    while (n--) {
        c ^= *b++;
        for (k = 0; k < 8; k++)
            c = c & 1 ? c >> 1 ^ 0xEDB88320u : c >> 1;
    }
    return ~c;
}

static unsigned nsec(unsigned bytes)
{
    return (bytes + SECTOR - 1) / SECTOR;
}

/* "/mods/a/x" -> "mods/a/x" in out (DS_PATH); only paths below DL_DIR. -> 0, or -1 */
static int norm(char *out, const char *path)
{
    static const char root[] = DL_DIR;
    unsigned i, n = slen(path), r = sizeof root - 1;
    if (n <= r + 1 || n - 1 >= DS_PATH)
        return -1;
    for (i = 0; i < r; i++)
        if (path[i] != root[i])
            return -1;
    if (path[r] != '/' || path[n - 1] == '/')
        return -1;
    for (i = r + 1; i < n; i++)
        if (path[i] == '/' && path[i - 1] == '/')
            return -1;
    mzero(out, DS_PATH);
    mcopy(out, path + 1, n - 1);
    return 0;
}

static int find(const char *p)
{
    unsigned i;
    for (i = 0; i < count; i++)
        if (seq(ents[i].path, p))
            return (int)i;
    return -1;
}

/* the parent of p is DL_DIR or a folder of the store */
static int parent_ok(const char *p)
{
    char q[DS_PATH];
    unsigned k = slen(p);
    int i;
    while (k && p[k - 1] != '/')
        k--;
    if (!k)
        return 0;
    mcopy(q, p, k - 1);
    q[k - 1] = 0;
    if (seq(q, DL_DIR + 1))
        return 1;
    i = find(q);
    return i >= 0 && (ents[i].flags & DS_F_DIR);
}

/* q is below folder p ("p/...") */
static int below(const char *q, const char *p)
{
    unsigned k = slen(p);
    unsigned i;
    for (i = 0; i < k; i++)
        if (q[i] != p[i])
            return 0;
    return q[k] == '/';
}

/* ---- the directory on the card ---- */

static int read_copy(unsigned at, struct ds_head *h)
{
    if (SD_READ(at, SECTOR, sbuf))
        return -1;
    mcopy(h, sbuf, sizeof *h);
    if (h->magic != DS_MAGIC || h->version != 1 || h->ent != sizeof(struct ds_ent)
        || h->count > DS_MAX || h->first != DS_FIRST || h->end != DS_END
        || h->head_crc != crc32(h, (unsigned)((char *)&h->head_crc - (char *)h)))
        return -1;
    return 0;
}

static int load_ents(unsigned at, const struct ds_head *h)
{
    if (h->count && SD_READ(at + 1, h->count * sizeof(struct ds_ent), ents))
        return -1;
    return crc32(ents, h->count * sizeof(struct ds_ent)) == h->ents_crc ? 0 : -1;
}

static int mount(void)
{
    struct ds_head a, b;
    int va, vb, first;
    if (ds_ready)
        return 0;
    if (!SD_OK() || SD_SECTORS() < DS_END)
        return -1;
    va = read_copy(DS_COPY_A, &a) == 0;
    vb = read_copy(DS_COPY_B, &b) == 0;
    count = 0;
    ds_gen = 0;
    cur = 1;                              /* no valid copy: the first write goes to A */
    first = va && (!vb || (int)(a.gen - b.gen) > 0) ? 0 : vb ? 1 : -1;
    if (first >= 0 && load_ents(first ? DS_COPY_B : DS_COPY_A, first ? &b : &a) == 0) {
        count = first ? b.count : a.count;
        ds_gen = first ? b.gen : a.gen;
        cur = (unsigned char)first;
    } else if (first >= 0 && (first ? va : vb)
               && load_ents(first ? DS_COPY_A : DS_COPY_B, first ? &a : &b) == 0) {
        count = first ? a.count : b.count; /* the newer copy's entries were torn: the older */
        ds_gen = first ? a.gen : b.gen;
        cur = (unsigned char)!first;
    }
    mzero(res, sizeof res);
    ds_ready = 1;
    return 0;
}

/* the directory into the copy that isn't the current one, header last. -> 0, or -1 (the store
 * is read again from the card, so memory never runs ahead of it) */
static int write_dir(void)
{
    struct ds_head h;
    unsigned at = cur ? DS_COPY_A : DS_COPY_B;
    unsigned bytes = count * sizeof(struct ds_ent);
    mzero(&h, sizeof h);
    h.magic = DS_MAGIC;
    h.version = 1;
    h.ent = sizeof(struct ds_ent);
    h.gen = ds_gen + 1;
    h.count = count;
    h.ents_crc = crc32(ents, bytes);
    h.first = DS_FIRST;
    h.end = DS_END;
    h.head_crc = crc32(&h, (unsigned)((char *)&h.head_crc - (char *)&h));
    mzero(sbuf, SECTOR);
    mcopy(sbuf, &h, sizeof h);
    if (bytes) {                          /* whole sectors: the last one's tail is past the CRC */
        unsigned whole = bytes & ~(SECTOR - 1);
        if ((whole && SD_WRITE(at + 1, whole, ents))
            || (bytes > whole && (mzero(wbuf, SECTOR), mcopy(wbuf, (char *)ents + whole, bytes - whole),
                                  SD_WRITE(at + 1 + whole / SECTOR, SECTOR, wbuf))))
            goto bad;
    }
    if (SD_WRITE(at, SECTOR, sbuf))
        goto bad;
    cur = !cur;
    ds_gen = h.gen;
    return 0;
bad:
    ds_ready = 0;
    mount();
    return -1;
}

/* ---- free runs ---- */

static unsigned runs[DS_MAX + RESERVATIONS][2];   /* [start, end) sorted, under the mutex */

static unsigned gather(void)
{
    unsigned i, k = 0, j;
    for (i = 0; i < count; i++)
        if (!(ents[i].flags & DS_F_DIR) && ents[i].size) {
            runs[k][0] = ents[i].sector;
            runs[k][1] = ents[i].sector + nsec(ents[i].size);
            k++;
        }
    for (i = 0; i < RESERVATIONS; i++)
        if (res[i].token) {
            runs[k][0] = res[i].sector;
            runs[k][1] = res[i].sector + res[i].nsec;
            k++;
        }
    for (i = 1; i < k; i++) {             /* insertion sort by start */
        unsigned a = runs[i][0], b = runs[i][1];
        for (j = i; j && runs[j - 1][0] > a; j--) {
            runs[j][0] = runs[j - 1][0];
            runs[j][1] = runs[j - 1][1];
        }
        runs[j][0] = a;
        runs[j][1] = b;
    }
    return k;
}

/* the free runs, each start aligned: the best fit for want sectors (want 0: the largest).
 * -> its start, 0 none; *got its length */
static unsigned pick(unsigned want, unsigned *got, unsigned *total)
{
    unsigned k = gather(), i, at = DS_DATA, best = 0, best_len = 0, sum = 0;
    for (i = 0; i <= k; i++) {
        unsigned end = i < k ? runs[i][0] : DS_END;
        if (end > at) {
            unsigned len = end - at;
            sum += len;
            if (want ? len >= want && (!best || len < best_len) : len > best_len) {
                best = at;
                best_len = len;
            }
        }
        if (i < k && runs[i][1] > at)
            at = (runs[i][1] + ALIGN - 1) & ~(ALIGN - 1);
    }
    if (got)
        *got = best_len;
    if (total)
        *total = sum;
    return best;
}

static unsigned reserve(int slot, unsigned size)
{
    unsigned got, at;
    res[slot].token = 0;                  /* a newer reservation drops the older */
    at = pick(size ? nsec(size) : 0, &got, 0);
    if (!at || (size && got < nsec(size)))
        return 0;
    res[slot].sector = at;
    res[slot].nsec = size ? nsec(size) : got;
    res[slot].token = ++tokens ? tokens : ++tokens;
    return at;
}

/* make p the reservation's run (its first nsec(size) sectors) */
static int commit(const char *p, int slot, unsigned token, unsigned size)
{
    int i;
    if (!res[slot].token || res[slot].token != token || nsec(size) > res[slot].nsec || !parent_ok(p))
        return -1;
    i = find(p);
    if (i >= 0 && (ents[i].flags & DS_F_DIR))
        return -1;
    if (i < 0) {
        if (count >= DS_MAX)
            return -1;
        i = (int)count++;
        mzero(&ents[i], sizeof ents[i]);
        mcopy(ents[i].path, p, slen(p));
    }
    ents[i].sector = size ? res[slot].sector : 0;
    ents[i].size = size;
    ents[i].flags = 0;
    res[slot].token = 0;
    return write_dir();
}

static void drop(unsigned i)
{
    count--;
    for (; i < count; i++)
        mcopy(&ents[i], &ents[i + 1], sizeof ents[i]);
}

/* ---- the calls ---- */

int ds_mount(void)
{
    int r;
    LOCK(ds_mutex);
    r = mount();
    UNLOCK(ds_mutex);
    return r;
}

void ds_format(void)
{
    LOCK(ds_mutex);
    ds_ready = 0;
    if (mount() == 0) {
        count = 0;
        mzero(res, sizeof res);
        if (write_dir() == 0)             /* both copies: an older one can't come back */
            write_dir();
    }
    UNLOCK(ds_mutex);
}

void ds_room(unsigned *largest, unsigned *total)
{
    unsigned got = 0, sum = 0;
    LOCK(ds_mutex);
    if (mount() == 0)
        pick(0, &got, &sum);
    UNLOCK(ds_mutex);
    *largest = got * SECTOR;
    *total = sum * SECTOR;
}

unsigned ds_reserve(unsigned size, unsigned *token)
{
    unsigned at = 0;
    LOCK(ds_mutex);
    *token = 0;
    if (mount() == 0 && size && (at = reserve(0, size)) != 0)
        *token = res[0].token;
    UNLOCK(ds_mutex);
    return at;
}

int ds_commit(const char *path, unsigned token, unsigned size)
{
    char p[DS_PATH];
    int r = -1;
    LOCK(ds_mutex);
    if (norm(p, path) == 0 && mount() == 0)
        r = commit(p, 0, token, size);
    UNLOCK(ds_mutex);
    return r;
}

int ds_file(const char *path, unsigned *sector, unsigned *size)
{
    char p[DS_PATH];
    int i = -1;
    LOCK(ds_mutex);
    if (norm(p, path) == 0 && mount() == 0 && (i = find(p)) >= 0 && !(ents[i].flags & DS_F_DIR)) {
        *sector = ents[i].sector;
        *size = ents[i].size;
    } else
        i = -1;
    UNLOCK(ds_mutex);
    return i < 0 ? -1 : 0;
}

int dl_fs_open(dl_file *f, const char *path, int write)
{
    char p[DS_PATH];
    int i, r = -1;
    mzero(f, sizeof *f);
    LOCK(ds_mutex);
    if (norm(p, path) || mount())
        goto out;
    i = find(p);
    if (!write) {
        if (i >= 0 && !(ents[i].flags & DS_F_DIR)) {
            f->sector = ents[i].sector;
            f->size = ents[i].size;
            f->open = 1;
            r = 0;
        }
    } else if ((i < 0 || !(ents[i].flags & DS_F_DIR)) && parent_ok(p) && !res[1].token
               && (f->sector = reserve(1, 0)) != 0) {
        f->cap = res[1].nsec * SECTOR;
        f->token = res[1].token;
        mcopy(f->path, p, DS_PATH);
        f->open = 2;
        wfill = 0;
        r = 0;
    }
out:
    UNLOCK(ds_mutex);
    return r;
}

int dl_fs_read(dl_file *f, void *buf, unsigned n)
{
    unsigned char *b = buf;
    unsigned done = 0;
    if (f->open != 1)
        return -1;
    if (n > f->size - f->pos)
        n = f->size - f->pos;
    LOCK(ds_mutex);
    while (done < n) {
        unsigned at = f->sector + f->pos / SECTOR, off = f->pos % SECTOR, k;
        if (off || n - done < SECTOR) {   /* a partial sector, through sbuf */
            k = SECTOR - off < n - done ? SECTOR - off : n - done;
            if (SD_READ(at, SECTOR, sbuf))
                break;
            mcopy(b + done, sbuf + off, k);
        } else {                          /* whole sectors, straight into buf */
            k = (n - done) & ~(SECTOR - 1);
            if (SD_READ(at, k, b + done))
                break;
        }
        done += k;
        f->pos += k;
    }
    UNLOCK(ds_mutex);
    return done < n ? -1 : (int)done;
}

int dl_fs_write(dl_file *f, const void *buf, unsigned n)
{
    const unsigned char *b = buf;
    int r = 0;
    if (f->open != 2 || f->bad)
        return -1;
    if (n > f->cap - f->size) {
        f->bad = 1;
        return -1;
    }
    LOCK(ds_mutex);
    while (n && !r) {
        unsigned at = f->sector + (f->size - wfill) / SECTOR, k;
        if (wfill || n < SECTOR) {        /* into the tail buffer, written when full */
            k = SECTOR - wfill < n ? SECTOR - wfill : n;
            mcopy(wbuf + wfill, b, k);
            wfill += k;
            if (wfill == SECTOR) {
                r = SD_WRITE(at, SECTOR, wbuf);
                wfill = 0;
            }
        } else {
            k = n & ~(SECTOR - 1);
            r = SD_WRITE(at, k, b);
        }
        b += k;
        n -= k;
        f->size += k;
    }
    UNLOCK(ds_mutex);
    if (r)
        f->bad = 1;
    return r ? -1 : 0;
}

int dl_fs_size(dl_file *f)
{
    return f->open == 1 ? (int)f->size : -1;
}

void dl_fs_close(dl_file *f)
{
    if (f->open == 2) {
        LOCK(ds_mutex);
        if (!f->bad && wfill) {           /* the last partial sector, zero-padded */
            mzero(wbuf + wfill, SECTOR - wfill);
            if (SD_WRITE(f->sector + f->size / SECTOR, SECTOR, wbuf))
                f->bad = 1;
        }
        wfill = 0;
        if (f->bad || commit(f->path, 1, f->token, f->size))
            res[1].token = 0;             /* dropped: the old file, if any, stays */
        UNLOCK(ds_mutex);
    }
    f->open = 0;
}

int dl_fs_delete(const char *path)
{
    char p[DS_PATH];
    int i, r = -1;
    LOCK(ds_mutex);
    if (norm(p, path) == 0 && mount() == 0 && (i = find(p)) >= 0 && !(ents[i].flags & DS_F_DIR)) {
        drop((unsigned)i);
        r = write_dir();
    }
    UNLOCK(ds_mutex);
    return r;
}

int dl_fs_rename(const char *from, const char *to)
{
    char a[DS_PATH], b[DS_PATH];
    unsigned la, lb, i;
    int k, r = -1;
    LOCK(ds_mutex);
    if (norm(a, from) || norm(b, to) || mount() || (k = find(a)) < 0 || find(b) >= 0 || !parent_ok(b)
        || below(b, a))
        goto out;
    la = slen(a);
    lb = slen(b);
    if (ents[k].flags & DS_F_DIR)         /* every path below it must still fit */
        for (i = 0; i < count; i++)
            if (below(ents[i].path, a) && slen(ents[i].path) - la + lb >= DS_PATH)
                goto out;
    for (i = 0; i < count; i++)
        if (i == (unsigned)k || below(ents[i].path, a)) {
            char t[DS_PATH];
            unsigned n = slen(ents[i].path);
            mzero(t, DS_PATH);
            mcopy(t, b, lb);
            mcopy(t + lb, ents[i].path + la, n - la);
            mcopy(ents[i].path, t, DS_PATH);
        }
    r = write_dir();
out:
    UNLOCK(ds_mutex);
    return r;
}

int dl_fs_file_size(const char *path)
{
    unsigned s, z;
    return ds_file(path, &s, &z) ? -1 : (int)z;
}

int dl_fs_rmdir(const char *path)
{
    char p[DS_PATH];
    unsigned i;
    int k, r = -1;
    LOCK(ds_mutex);
    if (norm(p, path) || mount() || (k = find(p)) < 0 || !(ents[k].flags & DS_F_DIR))
        goto out;
    for (i = 0; i < count; i++)
        if (below(ents[i].path, p))
            goto out;
    drop((unsigned)k);
    r = write_dir();
out:
    UNLOCK(ds_mutex);
    return r;
}

int dl_fs_mkdir(const char *path)
{
    char p[DS_PATH];
    int r = -1;
    LOCK(ds_mutex);
    if (seq(path, DL_DIR))
        r = 0;                            /* DL_DIR is always there */
    else if (norm(p, path) == 0 && mount() == 0 && find(p) < 0 && parent_ok(p) && count < DS_MAX) {
        mzero(&ents[count], sizeof ents[count]);
        mcopy(ents[count].path, p, slen(p));
        ents[count].flags = DS_F_DIR;
        count++;
        r = write_dir();
    }
    UNLOCK(ds_mutex);
    return r;
}

int dl_list_dir(const char *rel, struct dl_ent *out, int max)
{
    char base[DS_PATH + 1];
    unsigned b, i;
    int k = 0;
    static const char root[] = DL_DIR;
    b = sizeof root - 2;                  /* DL_DIR without its "/" */
    mcopy(base, root + 1, b);
    if (rel[0]) {
        unsigned n = slen(rel);
        if (b + 1 + n >= DS_PATH)
            return 0;
        base[b++] = '/';
        mcopy(base + b, rel, n);
        b += n;
    }
    base[b] = 0;
    LOCK(ds_mutex);
    if (mount() == 0)
        for (i = 0; i < count && (!out || k < max); i++) {
            const char *s = ents[i].path, *nm;
            unsigned j;
            if (!below(s, base))
                continue;
            nm = s + b + 1;
            for (j = 0; nm[j] && nm[j] != '/'; j++)
                ;
            if (nm[j] || j > DL_SEG_MAX)
                continue;                 /* deeper down, or a name too long to list */
            if (out) {
                mcopy(out[k].name, nm, j + 1);
                out[k].dir = (ents[i].flags & DS_F_DIR) != 0;
            }
            k++;
        }
    UNLOCK(ds_mutex);
    return k;
}

void dl_lock(void)
{
    LOCK(dl_mutex);
}

void dl_unlock(void)
{
    UNLOCK(dl_mutex);
}

/* ---- the staging floor and the live stage (fs.h) ---- */

unsigned char *volatile dl_stage_at = drive_staging;
volatile unsigned dl_stage_len = DL_STAGING_SIZE;

void dl_stage_tail(unsigned lo, unsigned hi)
{
    lo = (lo + 15) & ~15u;
    if (hi <= lo || hi - lo <= DL_STAGING_SIZE)
        return;
    dl_stage_at = (unsigned char *)(unsigned long)lo;
    dl_stage_len = hi - lo;
}

int dl_staging_take(void)
{
    int ok;
    dl_lock();
    ok = !dl_staging_owner;
    dl_staging_owner = 1;
    if (ok)
        dl_staging_epoch++;
    dl_unlock();
    return ok;
}

void dl_staging_give(void)
{
    dl_lock();
    dl_staging_owner = 0;
    dl_unlock();
}

volatile int dl_reads;

int dl_staging_take_idle(unsigned now, unsigned stall)
{
    (void)now;
    (void)stall;
    return dl_staging_take();
}

void dl_reader_add(unsigned char *r, unsigned now)
{
    (void)r;
    (void)now;
}

void dl_reader_remove(unsigned char *r)
{
    (void)r;
}

void dl_readers_poll(unsigned now)
{
    (void)now;
}
