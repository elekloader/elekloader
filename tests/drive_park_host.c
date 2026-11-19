/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The Rytm Drive Loader park.c (mods/drive-loader-ar2) on the computer: what the MODS page lists
 * as parked and FORGET DATA's rewrite of /mods/.store. tests/test_drive_park.py builds it with
 * the host's cc and checks it against elekloader/drive_store.py.
 *
 * stdin, one request a line:
 *   store HEX      the store's bytes to work on
 *   count          dl_park_list counting only -> "COUNT n"
 *   list           dl_park_list -> "LIST n", then "PARK id fields codes ccs b1 .. b6" each
 *   forget ID|ID   dl_park_forget -> "FORGET HEX" (or "FORGET -1") */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "park.c"

void dl_memcpy(void *d, const void *s, u32 n) { memmove(d, s, n); }
void dl_memset(void *d, int c, u32 n) { memset(d, c, n); }
u32 dl_crc32_add(u32 c, const u8 *p, u32 n)
{
    int k;
    while (n--) {
        c ^= *p++;
        for (k = 0; k < 8; k++)
            c = c & 1 ? c >> 1 ^ 0xEDB88320u : c >> 1;
    }
    return c;
}

static u8 raw[1 << 20], out[1 << 20];
static char line[1 << 21];

int main(void)
{
    u32 len = 0;
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!strncmp(line, "store ", 6)) {
            const char *h = line + 6;
            for (len = 0; h[0] && h[1]; h += 2) {
                unsigned v;
                sscanf(h, "%2x", &v);
                raw[len++] = (u8)v;
            }
        } else if (!strcmp(line, "count")) {
            printf("COUNT %d\n", dl_park_list(raw, len, 0, 0));
        } else if (!strcmp(line, "list")) {
            static struct dl_park p[64];
            int i, j, n = dl_park_list(raw, len, p, 64);
            printf("LIST %d\n", n);
            for (i = 0; i < n; i++) {
                printf("PARK %s %u %u %u", p[i].id, p[i].fields, p[i].codes, p[i].ccs);
                for (j = 0; j < DL_STORE_SCOPES; j++)
                    printf(" %u", p[i].bits[j]);
                printf("\n");
            }
        } else if (!strncmp(line, "forget ", 7)) {
            const char *ids[32];
            char *s = line + 7, *t;
            int n = 0;
            s32 m;
            while (n < 32 && (t = strtok(n ? 0 : s, "|")))
                ids[n++] = t;
            m = dl_park_forget(raw, len, ids, n, out);
            if (m < 0)
                printf("FORGET -1\n");
            else {
                s32 i;
                printf("FORGET ");
                for (i = 0; i < m; i++)
                    printf("%02x", out[i]);
                printf("\n");
            }
        }
        fflush(stdout);
    }
    return 0;
}
