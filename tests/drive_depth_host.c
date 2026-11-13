/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The 3-deep folder limit (fs.h DL_DEPTH) past its third level, built for the computer:
 * tests/test_drive_route.py builds this twice with the host's cc and drives it.
 *   -DDEPTH_PAGE   the MODS page's folder rows (page.c read_folder), each row "../", "NAME/" or
 *                  the file's path below DL_DIR
 *   -DDEPTH_ROUTE  the /mods route's listing count (route.c walk, count only: its items need
 *                  stock's std::string)
 * stdin: the +Drive's DL_DIR as paths below it, one a line (a folder ends in "/"), then "--",
 * then queries "FOLDERS REL" (REL the open folder below DL_DIR, "" DL_DIR itself). Each answer
 * is one line: the rows joined by "|" (page), or the count (route). Only the functions these two
 * call are stubbed; the rest of page.c and route.c is linked against nothing and never runs. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef DEPTH_PAGE
#include "../mods/drive-loader-ar2/page.c"
#else
#include "../mods/drive-loader-ar2/route.c"
#endif

static char tree[64][DL_NAME_MAX + 2];
static int ntree;

/* DL_DIR "/" rel's entries, as fs.c's: its folders and files, one level, without "." */
static int tree_list(const char *rel, struct dl_ent *out, int max)
{
    int i, k = 0;
    size_t a = strlen(rel);
    for (i = 0; i < ntree; i++) {
        const char *t = tree[i], *rest, *slash;
        if (a && (strncmp(t, rel, a) || t[a] != '/'))
            continue;
        rest = t + (a ? a + 1 : 0);
        slash = strchr(rest, '/');
        if (!*rest || (slash && slash[1]))
            continue;                    /* rel itself, or deeper than one level */
        if (out && k < max) {
            size_t n = slash ? (size_t)(slash - rest) : strlen(rest);
            memcpy(out[k].name, rest, n);
            out[k].name[n] = 0;
            out[k].dir = slash != 0;
        }
        k++;
    }
    return out ? (k < max ? k : max) : k;
}

#ifdef DEPTH_PAGE
int dl_list_dir(const char *rel, struct dl_ent *out, int max) { return tree_list(rel, out, max); }
#endif
void dl_lock(void) {}
void dl_unlock(void) {}
int dl_fs_file_size(const char *path) { (void)path; return 100; }

#ifdef DEPTH_PAGE
void *dl_alloc(u32 n) { return calloc(1, n ? n : 1); }
void dl_free(void *p) { free(p); }
void dl_memcpy(void *d, const void *s, u32 n) { memmove(d, s, n); }
void dl_memset(void *d, int c, u32 n) { memset(d, c, n); }
int dl_strcmp(const char *a, const char *b) { return strcmp(a, b); }
u32 dl_strlen(const char *s) { return (u32)strlen(s); }
u32 dl_status_n;                         /* no boot records */
struct dl_choice *dl_boot_choice;
struct dl_status *dl_status;
char (*dl_detail)[32];
#endif

int main(void)
{
    char line[512];
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!strcmp(line, "--"))
            break;
        if (ntree < 64)
            strcpy(tree[ntree++], line);
    }
    while (fgets(line, sizeof line, stdin)) {
        char *rel;
        int folders = (int)strtol(line, &rel, 10);
        line[strcspn(line, "\r\n")] = 0;
        rel += *rel == ' ';
#ifdef DEPTH_PAGE
        {
            int i;
            copy_str(cwd, rel, sizeof cwd);
            depth = folders;
            if (read_folder()) {
                printf("no memory\n");
                continue;
            }
            for (i = 0; i < nlrows; i++)
                printf("%s%s", i ? "|" : "", lrows[i].kind == R_UP ? "../"
                       : lrows[i].kind == R_DIR ? strcat(strcpy(line, lrows[i].name), "/")
                       : files[lrows[i].f].name);
            printf("\n");
        }
#else
        {
            struct dl_ent ents[64];
            int n = tree_list(rel, ents, 64);
            printf("%d\n", walk(0, rel, folders, ents, n));
        }
#endif
    }
    return 0;
}
