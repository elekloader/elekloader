/* SPDX-License-Identifier: GPL-2.0-or-later */
/* boot.c's dl_read_more, the MODS page's stretch-a-tick reader, built for the computer over a
 * made-up file: tests/test_drive_page.py builds it with the host's cc. A file over the buffer
 * (the live stage) is read to its end all the same, so the page knows its size and CRC and can
 * leave its check to the boot (page.c inspect, "big"), without a byte past the buffer.
 * From drive-loader 0.12 the reader takes a file's pending update (<name>.new) when there is one.
 * argv: SIZE MAX [NEWSIZE]; prints "RESULT r GOT n CRC xxxxxxxx GUARD ok|bad OPENED path": with
 * NEWSIZE, x.elemod.new is there too, NEWSIZE bytes of its own pattern */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../mods/drive-loader-ar2/boot.c"
#include "../mods/drive-loader-ar2/skip.c"

static unsigned char *file, *file_new;
static unsigned fsize, fpos;
static int has_new;
static unsigned new_size;
static char opened[DL_PATH_MAX + 4];

static int is_new(const char *path) { size_t k = strlen(path); return k > 4 && !strcmp(path + k - 4, ".new"); }
int dl_fs_file_size(const char *path) { return is_new(path) ? (has_new ? (int)new_size : -1) : (int)fsize; }
u32 dl_strlen(const char *s) { return (u32)strlen(s); }
int dl_fs_open(dl_file *f, const char *path, int write)
{
    (void)f, (void)write;
    fpos = 0;
    strcpy(opened, path);
    if (is_new(path)) {                   /* the reads below then come from the update */
        file = file_new;
        fsize = new_size;
    }
    return 0;
}
int dl_fs_read(dl_file *f, void *buf, unsigned n)
{
    (void)f;
    if (n > fsize - fpos)
        n = fsize - fpos;
    memcpy(buf, file + fpos, n);
    fpos += n;
    return (int)n;
}
void dl_fs_close(dl_file *f) { (void)f; }
void dl_fs_seek(dl_file *f, unsigned at) { (void)f; fpos = at < fsize ? at : fsize; }
int dl_fs_size(dl_file *f) { (void)f; return (int)fsize; }
void dl_memcpy(void *d, const void *s, u32 n) { memcpy(d, s, n); }
void dl_memset(void *d, int c, u32 n) { memset(d, c, n); }
u32 dl_crc32_add(u32 c, const u8 *p, u32 n)
{
    int k;
    while (n--)
        for (c ^= *p++, k = 0; k < 8; k++)
            c = c & 1 ? 0xEDB88320 ^ (c >> 1) : c >> 1;
    return c;
}

int main(int argc, char **argv)
{
    struct dl_reader rd;
    unsigned max, i;
    unsigned char *arena;
    int r;
    if (argc != 3 && argc != 4)
        return 2;
    fsize = (unsigned)strtoul(argv[1], 0, 0);
    max = (unsigned)strtoul(argv[2], 0, 0);
    file = malloc(fsize + 1);
    for (i = 0; i < fsize; i++)
        file[i] = (unsigned char)(i * 7 + 3);
    if (argc == 4) {
        has_new = 1;
        new_size = (unsigned)strtoul(argv[3], 0, 0);
        file_new = malloc(new_size + 1);
        for (i = 0; i < new_size; i++)
            file_new[i] = (unsigned char)(i * 11 + 5);
    }
    arena = malloc(max + 2 * 4096);
    memset(arena, 0xa5, max + 2 * 4096);
    booting = 1;                          /* no sleeps */
    dl_read_open(&rd, "x.elemod");
    while ((r = dl_read_more(&rd, arena + 4096, max)) == 0)
        ;
    for (i = 0; i < 4096 && arena[i] == 0xa5 && arena[4096 + max + i] == 0xa5; i++)
        ;
    printf("RESULT %d GOT %u CRC %08x GUARD %s OPENED %s\n", r, rd.st.got, ~rd.st.crc, i == 4096 ? "ok" : "bad",
           opened);
    return 0;
}
