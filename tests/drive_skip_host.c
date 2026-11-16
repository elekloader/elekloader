/* SPDX-License-Identifier: GPL-2.0-or-later */
/* drive-loader-a4mk2's payload skip (skip.c), run on the computer: tests/test_drive_payload_skip.py
 * builds it with the host's cc and compares it with its twin, elekloader/drive.py's stage().
 *
 *   drive_skip_host FILE OUT [MAX]
 *
 * Stages FILE into a buffer of MAX bytes (default: the file's size + 1) as the boot does, writes
 * the staged bytes to OUT and prints one JSON line: the result, the payload skipped, and how many
 * bytes of the payload's hex any read brought into the buffer. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../mods/drive-loader-a4mk2/skip.h"

void dl_memcpy(void *d, const void *s, u32 n) { memcpy(d, s, n); }
void dl_memset(void *d, int c, u32 n) { memset(d, c, n); }

static u8 *file;
static u32 file_n;
static u32 hex_lo = 0xFFFFFFFF, hex_hi, hex_read, reads;

static s32 read_at(void *ctx, u32 at, u8 *dst, u32 n)
{
    (void)ctx;
    reads++;
    if (at >= file_n)
        return 0;
    if (n > file_n - at)
        n = file_n - at;
    memcpy(dst, file + at, n);
    if (at < hex_hi && at + n > hex_lo)  /* hex bytes this read brought in */
        hex_read += (at + n < hex_hi ? at + n : hex_hi) - (at > hex_lo ? at : hex_lo);
    return (s32)n;
}

int main(int argc, char **argv)
{
    FILE *f;
    long len;
    u8 *buf;
    u32 max, i;
    s32 r;
    struct dl_pay pay;
    if (argc < 3)
        return 2;
    f = fopen(argv[1], "rb");
    if (!f)
        return 2;
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    file = malloc(len + 1);
    if (fread(file, 1, len, f) != (size_t)len)
        return 2;
    fclose(f);
    file_n = (u32)len;
    max = argc > 3 ? (u32)strtoul(argv[3], 0, 0) : file_n + 1;
    if (argc > 5) {                      /* the hex's range, from the twin, to count reads into it */
        hex_lo = (u32)strtoul(argv[4], 0, 0);
        hex_hi = (u32)strtoul(argv[5], 0, 0);
    }
    buf = malloc(max + 1);
    r = dl_stage_file(read_at, 0, buf, max, &pay);
    f = fopen(argv[2], "wb");
    if (r > 0)
        fwrite(buf, 1, (size_t)r, f);
    fclose(f);
    printf("{\"r\": %d, \"len\": %u, \"off\": %u, \"sha256\": \"", r, pay.len, pay.off);
    for (i = 0; i < 32; i++)
        printf("%02x", pay.sha256[i]);
    printf("\", \"hex_read\": %u, \"reads\": %u}\n", hex_read, reads);
    return 0;
}
