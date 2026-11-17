/* SPDX-License-Identifier: GPL-2.0-or-later */
/* boot.c's dl_read_more, the MODS page's stretch-a-tick reader, built for the computer over a
 * made-up file: tests/test_drive_page.py builds it with the host's cc. A file over the buffer
 * (the live stage) is read to its end all the same, so the page knows its size and CRC and can
 * leave its check to the boot (page.c inspect, "big"), without a byte past the buffer.
 * argv: SIZE MAX; prints "RESULT r GOT n CRC xxxxxxxx GUARD ok|bad" */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../mods/drive-loader-ar2/boot.c"

static unsigned char *file;
static unsigned fsize, fpos;

int dl_fs_open(dl_file *f, const char *path, int write) { (void)f, (void)path, (void)write; fpos = 0; return 0; }
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
    if (argc != 3)
        return 2;
    fsize = (unsigned)strtoul(argv[1], 0, 0);
    max = (unsigned)strtoul(argv[2], 0, 0);
    file = malloc(fsize + 1);
    for (i = 0; i < fsize; i++)
        file[i] = (unsigned char)(i * 7 + 3);
    arena = malloc(max + 2 * 4096);
    memset(arena, 0xa5, max + 2 * 4096);
    booting = 1;                          /* no sleeps */
    dl_read_open(&rd, "x.elemod");
    while ((r = dl_read_more(&rd, arena + 4096, max)) == 0)
        ;
    for (i = 0; i < 4096 && arena[i] == 0xa5 && arena[4096 + max + i] == 0xa5; i++)
        ;
    printf("RESULT %d GOT %u CRC %08x GUARD %s\n", r, rd.got, ~rd.crc, i == 4096 ? "ok" : "bad");
    return 0;
}
