/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The Drive Loader's boot linker (mods/drive-loader-ar2/dlink.c, json.c), run on the
 * computer: tests/test_drive_link.py builds it with the host's cc and drives it.
 *
 *   drive_link_host RAM.bin RAM_BASE STOCK.bin STOCK_BASE DESC_ADDR MODS_DIR OUT.bin
 *
 * RAM.bin is the flashed build's RAM from RAM_BASE up to 0x48000000 (the rest zero),
 * STOCK.bin the main OS image at STOCK_BASE (stock parts copy from it). MODS_DIR holds the
 * files and `active`. OUT.bin gets the RAM afterwards; stdout a JSON summary. With
 * DL_HOST_CHECK set it runs the MODS page's dl_check instead of dl_link: the same summary
 * from the check's own records, plus each file's version. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../mods/drive-loader-ar2/dl.h"

static u8 *ram, *stock;
static u32 ram_base, ram_len, stock_base, stock_len;
static const char *dir;
static u8 zero[16];

u8 *dl_host_mem(u32 a)
{
    if (a >= ram_base && a - ram_base < ram_len)
        return ram + (a - ram_base);
    if (a >= stock_base && a - stock_base < stock_len)
        return stock + (a - stock_base);
    fprintf(stderr, "drive_link_host: no memory at 0x%08x\n", a);
    memset(zero, 0, sizeof zero);
    exit(3);
}

void *dl_alloc(u32 n)
{
    return calloc(n ? n : 1, 1);
}

void dl_free(void *p)
{
    free(p);
}

void dl_breathe(void)
{
}

static u8 *slurp(const char *path, u32 *n)
{
    FILE *f = fopen(path, "rb");
    u8 *b;
    long len;
    if (!f)
        return 0;
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    b = malloc(len + 1);
    if (fread(b, 1, len, f) != (size_t)len)
        exit(4);
    fclose(f);
    *n = (u32)len;
    return b;
}

s32 dl_read_file(const char *name, u8 *buf, u32 max)
{
    char path[1024];
    u32 n;
    u8 *b;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    b = slurp(path, &n);
    if (!b)
        return -1;
    if (n > max) {
        free(b);
        return -2;
    }
    memcpy(buf, b, n);
    free(b);
    return (s32)n;
}

static void jstr(const char *s)
{
    putchar('"');
    for (; *s; s++) {
        if (*s == '"' || *s == '\\')
            printf("\\%c", *s);
        else if ((u8)*s < 0x20)
            printf("\\u%04x", *s);
        else
            putchar(*s);
    }
    putchar('"');
}

int main(int argc, char **argv)
{
    static struct dl_choice ch[DL_MAX_MODS];
    static u8 staging[0x80000];
    u32 n, i;
    u8 *active;
    char path[1024];
    int k, loaded;
    FILE *f;
    if (argc != 8) {
        fprintf(stderr, "usage: drive_link_host RAM.bin RAM_BASE STOCK.bin STOCK_BASE DESC MODS OUT\n");
        return 2;
    }
    ram_base = strtoul(argv[2], 0, 0);
    ram_len = 0x48000000u - ram_base;
    ram = calloc(ram_len, 1);
    {
        u8 *r = slurp(argv[1], &n);
        memcpy(ram, r, n < ram_len ? n : ram_len);
    }
    stock = slurp(argv[3], &stock_len);
    stock_base = strtoul(argv[4], 0, 0);
    dir = argv[6];
    snprintf(path, sizeof path, "%s/active", dir);
    active = slurp(path, &n);
    k = active ? dl_parse_active((const char *)active, n, ch) : 0;
    if (k < 0) {
        f = fopen(argv[7], "wb");
        fwrite(ram, 1, ram_len, f);
        fclose(f);
        printf("{\"skip\": \"count\"}\n");
        return 0;
    }
    if (getenv("DL_HOST_CHECK")) {
        static struct dl_status st[DL_MAX_MODS];
        static char det[DL_MAX_MODS][32], ver[DL_MAX_MODS][DL_VER_MAX];
        if (getenv("DL_HOST_STEP")) {    /* the MODS page's save: the same check in steps */
            int j;
            loaded = dl_check_start(strtoul(argv[5], 0, 0), k, staging, sizeof staging, st, det, ver);
            for (j = 0; loaded >= 0 && j < k; j++)
                if (!strcmp(getenv("DL_HOST_STEP"), "buf")) {   /* the page's read a stretch at a time */
                    s32 len = dl_read_file(ch[j].name, staging, sizeof staging);
                    u32 c = 0xFFFFFFFF, o;
                    for (o = 0; len > 0 && o < (u32)len; o += DL_BREATHE)
                        c = dl_crc32_add(c, staging + o, (u32)len - o < DL_BREATHE ? (u32)len - o : DL_BREATHE);
                    dl_check_step_buf(&ch[j], len, ~c);
                } else {
                    dl_check_step(&ch[j]);
                }
            if (loaded >= 0)
                loaded = dl_check_finish();
        } else {
            loaded = dl_check(strtoul(argv[5], 0, 0), ch, k, staging, sizeof staging, st, det, ver);
        }
        f = fopen(argv[7], "wb");
        fwrite(ram, 1, ram_len, f);
        fclose(f);
        printf("{\"loaded\": %d, \"status\": [", loaded);
        for (i = 0; i < (u32)(k > 0 ? k : 0); i++) {
            char id[DL_ID_MAX + 1];
            memcpy(id, st[i].id, DL_ID_MAX);
            id[DL_ID_MAX] = 0;
            printf("%s{\"state\": %d, \"code\": %d, \"arg\": %d, \"id\": ", i ? ", " : "",
                   st[i].state, st[i].code, st[i].arg);
            jstr(id);
            printf(", \"detail\": ");
            jstr(det[i]);
            printf(", \"version\": ");
            jstr(ver[i]);
            printf("}");
        }
        printf("], \"boot_status_n\": %u}\n", dl_status_n);
        return 0;
    }
    loaded = dl_link(strtoul(argv[5], 0, 0), ch, k, staging, sizeof staging);
    f = fopen(argv[7], "wb");
    fwrite(ram, 1, ram_len, f);
    fclose(f);
    printf("{\"loaded\": %d, \"skip\": %d, \"used\": [%u, %u], \"inits\": [", loaded, dl_skip_reason,
           dl_used_lo, dl_used_hi);
    for (i = 0; i < dl_inits_n; i++)
        printf("%s%u", i ? ", " : "", dl_inits[i]);
    printf("], \"status\": [");
    for (i = 0; i < dl_status_n; i++) {
        char id[DL_ID_MAX + 1];
        memcpy(id, dl_status[i].id, DL_ID_MAX);
        id[DL_ID_MAX] = 0;
        printf("%s{\"state\": %d, \"code\": %d, \"arg\": %d, \"id\": ", i ? ", " : "",
               dl_status[i].state, dl_status[i].code, dl_status[i].arg);
        jstr(id);
        printf(", \"detail\": ");
        jstr(dl_detail[i]);
        printf("}");
    }
    printf("]}\n");
    return 0;
}
