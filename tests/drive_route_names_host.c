/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The /mods route's names (mods/drive-loader-ar2/names.inc) built for the computer:
 * tests/test_drive_route.py builds it with the host's cc and drives it.
 * Each stdin line is a path below /mods as the router hands it to the route: its segments, one
 * capture each. Each output line: "ok FOLDERS WRITABLE JOINED", with " mkdir FOLDER" after it for
 * an upload that makes a folder (FOLDER/.dir), or "refused". A line "rmdir N" asks about a Clear of
 * a folder with N entries: "cleared" or the refusal. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../mods/drive-loader-ar2/fs.h"

static unsigned len(const char *s) { return (unsigned)strlen(s); }
static void copy(void *d, const void *s, unsigned n) { memmove(d, s, n); }
static int same(const char *a, const char *b) { return strcmp(a, b) == 0; }
static int ends(const char *s, const char *tail)
{
    unsigned n = len(s), k = len(tail);
    return n > k && same(s + n - k, tail);
}

#include "../mods/drive-loader-ar2/names.inc"

int main(void)
{
    char line[512], out[DL_NAME_MAX + 1];
    while (fgets(line, sizeof line, stdin)) {
        const char *v[16];
        void *caps[2];
        int n = 0, folders;
        char *p = line, *e = line + strcspn(line, "\r\n");
        *e = 0;
        if (!strncmp(line, "rmdir ", 6)) {   /* "rmdir N": a Clear of a folder holding N entries */
            const char *why = dir_clear_why(atoi(line + 6));
            printf("%s\n", why ? why : "cleared");
            continue;
        }
        while (n < 16) {                    /* split on '/', keeping empty segments */
            char *s = strchr(p, '/');
            v[n++] = p;
            if (!s)
                break;
            *s = 0;
            p = s + 1;
        }
        caps[0] = (void *)v;
        caps[1] = (void *)(v + n);
        folders = join_caps(out, caps);
        if (folders < 0)
            printf("refused\n");
        else if (mkdir_of(out))
            printf("ok %d %d %s mkdir %.*s\n", folders, writable(out), out, (int)mkdir_of(out), out);
        else
            printf("ok %d %d %s\n", folders, writable(out), out);
    }
    return 0;
}
