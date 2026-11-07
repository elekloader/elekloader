/* SPDX-License-Identifier: GPL-2.0-or-later */
/* drive-loader (Analog Rytm MKII, OS 1.74): ekFS calls and the loader mutex
 * (fs.h). Every ekFS call takes stock's ekFS mutex (0x419f5320) itself and
 * returns an error while ekFS isn't mounted. */

#include "fs.h"

/* std::string (copy-on-write, one pointer): ctor from a C string, dtor */
#define STR_NEW ((void (*)(void *, const char *, void *))0x401bcff0)
#define STR_DEL ((void (*)(void *))0x401bb540)

#define FILE_NEW ((void (*)(void *, void *, int))0x4007dcfa)    /* (file, &path, 0 read / 1 write) */
#define FILE_DEL ((void (*)(void *))0x4007d956)                 /* closes when open */
#define FILE_READ ((int (*)(void *, void *, unsigned))0x4007d822)
#define FILE_WRITE ((int (*)(void *, const void *, unsigned))0x4007d860)
#define FILE_NODE ((void *(*)(void *))0x4007d9d2)
#define NODE_SIZE ((int (*)(void *))0x4007d9ec)                 /* data size, without the header */
#define FS_DELETE ((int (*)(const char *))0x4007da78)
#define FS_RENAME ((int (*)(const char *, const char *))0x4007da8c)  /* true on success */

#define MUTEX_LOCK ((void (*)(void *))0x400010bc)
#define MUTEX_UNLOCK ((void (*)(void *))0x400011ee)

#define F_OPEN 0x18                       /* u8: the open succeeded */

unsigned dl_mutex[3];                     /* {owner, wait head, wait tail} */
volatile int dl_staging_owner;            /* 1: a loader part holds the staging buffer */
volatile unsigned dl_staging_epoch;       /* counts each take: an upload staged across one is stale */

extern int dl_last_io[4];

int dl_fs_open(dl_file *f, const char *path, int write)
{
    void *s;
    STR_NEW(&s, path, 0);
    FILE_NEW(f, &s, write ? 1 : 0);
    STR_DEL(&s);
    dl_last_io[0] = f->raw[F_OPEN] | f->raw[7] << 8 | write << 16;
    return f->raw[F_OPEN] ? 0 : -1;
}

int dl_fs_read(dl_file *f, void *buf, unsigned n)
{
    return FILE_READ(f, buf, n);
}

int dl_fs_write(dl_file *f, const void *buf, unsigned n)
{
    const unsigned char *p = buf;
    while (n) {
        unsigned k = n > 0x8000 ? 0x8000 : n;
        int r = FILE_WRITE(f, p, k);
        dl_last_io[1] = r;
        if (r < 0 || (unsigned)r != k)
            return -1;
        p += k;
        n -= k;
    }
    return 0;
}

/* stock's node size (0x4007d9ec) is the node's length less 16, never below
 * 0: FsSample's files start with a 16-byte header of their own. The loader's
 * files have none, so a file is that plus 16, and one of 16 bytes or less
 * is counted by reading it. Read mode only. */
int dl_fs_size(dl_file *f)
{
    void *node;
    int n;
    if (!f->raw[F_OPEN])
        return -1;
    node = FILE_NODE(f);
    dl_last_io[2] = (int)node;
    if (!node)
        return -1;
    n = NODE_SIZE(node);
    dl_last_io[3] = n;
    if (n <= 0) {
        unsigned char b[32];
        n = FILE_READ(f, b, sizeof b) - 16;
        if (n < -16)
            return -1;
    }
    return n + 16;
}

void dl_fs_close(dl_file *f)
{
    FILE_DEL(f);
}

/* stock's delete gives no result worth trusting (not traced): the file
 * being gone afterwards is the result */
int dl_fs_delete(const char *path)
{
    if (dl_fs_file_size(path) < 0)
        return -1;
    FS_DELETE(path);
    return dl_fs_file_size(path) < 0 ? 0 : -1;
}

int dl_fs_rename(const char *from, const char *to)
{
    return FS_RENAME(from, to) & 0xff ? 0 : -1;
}

int dl_fs_file_size(const char *path)
{
    dl_file f;
    int n = -1;
    if (dl_fs_open(&f, path, 0) == 0)
        n = dl_fs_size(&f);
    dl_fs_close(&f);
    return n;
}

void dl_lock(void)
{
    MUTEX_LOCK(dl_mutex);
}

void dl_unlock(void)
{
    MUTEX_UNLOCK(dl_mutex);
}

volatile int dl_reads;                /* live copies of open reads' fill closures (route.c) */

/* the open reads' readers (stock's, 0xd8 bytes; route.c adds and removes them under
 * dl_lock), each with its progress (+0xb0 the body's running CRC, +0xb4 its bytes
 * served) and the tick that progress last changed */
#define READERS 4
static unsigned char *readers[READERS];
static unsigned reader_pos[READERS], reader_at[READERS];

void dl_reader_add(unsigned char *r, unsigned now)
{
    int i;
    for (i = 0; i < READERS; i++)
        if (!readers[i]) {
            readers[i] = r;
            reader_pos[i] = ~0u;
            reader_at[i] = now;
            return;
        }
}

void dl_reader_remove(unsigned char *r)
{
    int i;
    for (i = 0; i < READERS; i++)
        if (readers[i] == r)
            readers[i] = 0;
}

static unsigned progress(const unsigned char *r)
{
    return *(const unsigned *)(r + 0xb0) ^ *(const unsigned *)(r + 0xb4);
}

/* from the UI tick while a read is open: note each reader's progress */
void dl_readers_poll(unsigned now)
{
    int i;
    unsigned p;
    if (!dl_reads)
        return;
    dl_lock();
    for (i = 0; i < READERS; i++)
        if (readers[i] && (p = progress(readers[i])) != reader_pos[i]) {
            reader_pos[i] = p;
            reader_at[i] = now;
        }
    dl_unlock();
}

/* dl_staging_take, but also refused while a read of a /mods file is open: the router
 * streams a read out of the buffer after the fill, so a page check during one would hand
 * the client the page's bytes (route.c, count). A read whose reader hasn't moved for
 * `stall` ticks was left by its client (stock keeps it until the next transfer opens,
 * through a USB disconnect too), and doesn't count. */
int dl_staging_take_idle(unsigned now, unsigned stall)
{
    int ok, i, stalled = 0;
    dl_lock();
    for (i = 0; i < READERS; i++)
        if (readers[i] && now - reader_at[i] >= stall)
            stalled++;
    ok = !dl_staging_owner && dl_reads == stalled;
    if (ok) {
        dl_staging_owner = 1;
        dl_staging_epoch++;
    }
    dl_unlock();
    return ok;
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
