/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The MODS page's build list (page.c build_rows) after the page's folder changes and after the
 * route's changes over USB, built for the computer: tests/test_drive_page.py builds it with the
 * host's cc and drives it. The FCR emulator run (2026-11-09) drew one mod three times in VIEW
 * BUILD after the page had changed folder: read_folder gave build_rows new, zeroed memory and
 * only some of its callers made the build list again.
 * stdin: the +Drive's DL_DIR as paths below it, one a line (a folder ends in "/"), then "--",
 * then the saved build (/mods/active's names, in order), one a line, then "--", then requests:
 *   open REL      the page opens the folder REL ("" DL_DIR itself), as go() and the opening do
 *   mv FROM|TO    a move over USB (route.c move: the tree changes, dl_move_from/to are set),
 *                 then the page's next tick (follow_route)
 *   build         the build list's labels, as VIEW BUILD shows them -> "BUILD l1|l2|..."
 *   drive         the open folder's rows, label and right-hand column -> "DRIVE l1=c1|l2=c2|..."
 *   check         the background check's progress as /mods/.status shows it -> "CHECK n/N FOLDER"
 *   checked PATH  the background check has read PATH (no id learnt)
 *   id PATH|ID    a check has named PATH's mod ID (the save's)
 *   boot PATH|ID  the boot's record of PATH holds ID (the mod's id, or dlink's path prefix),
 *                 as the page's opening reads it (boot_record)
 *   info N        INFO's box on build row N (from 0) -> "INFO line1~line2~..."
 *   store HEX     /mods/.store's bytes, as the page's opening reads them (parks_from); the build
 *                 list then shows the parked mods after the build's rows
 *   forget ID|ID  park.c's FORGET DATA of those mods on the last store -> "FORGET HEX" (or -1)
 *   setid N ID    the set's record N names ID (a site refusal's other mod)
 *   reason C A D  the "!" box of refusal code C, arg A, detail D (the rest of the line), against
 *                 the set's records -> "REASON line1~line2"
 * Only the functions these call are stubbed; the rest of page.c is linked against nothing and
 * never runs. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../mods/drive-loader-ar2/page.c"
#include "../mods/drive-loader-ar2/park.c"

u32 dl_crc32_add(u32 c, const u8 *p, u32 n)   /* dlink.c's, which isn't linked here */
{
    int k;
    while (n--)
        for (c ^= *p++, k = 0; k < 8; k++)
            c = c & 1 ? 0xEDB88320 ^ (c >> 1) : c >> 1;
    return c;
}

static u8 store_hex[65536];
static s32 store_n;
static struct dl_status set_recs[DL_MAX_MODS];

static void out_text(const char *kind, const char *t)
{
    printf("%s ", kind);
    for (; *t; t++)
        putchar(*t == '\n' ? '~' : *t);
    printf("\n");
}

static char tree[64][DL_NAME_MAX + 2];
static int ntree;

/* DL_DIR "/" rel's entries, one level, as fs.c's */
int dl_list_dir(const char *rel, struct dl_ent *out, int max)
{
    int i, k = 0;
    size_t a = strlen(rel);
    for (i = 0; i < ntree; i++) {
        const char *t = tree[i], *rest, *slash;
        if (!t[0] || (a && (strncmp(t, rel, a) || t[a] != '/')))
            continue;
        rest = t + (a ? a + 1 : 0);
        slash = strchr(rest, '/');
        if (!*rest || (slash && slash[1]))
            continue;
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

void dl_lock(void) {}
void dl_unlock(void) {}
int dl_fs_file_size(const char *path) { (void)path; return 100; }
void *dl_alloc(u32 n) { return calloc(1, n ? n : 1); }   /* zeroed, as boot.c's */
void dl_free(void *p) { free(p); }
void dl_memcpy(void *d, const void *s, u32 n) { memmove(d, s, n); }
void dl_memset(void *d, int c, u32 n) { memset(d, c, n); }
int dl_strcmp(const char *a, const char *b) { return strcmp(a, b); }
u32 dl_strlen(const char *s) { return (u32)strlen(s); }
u32 dl_status_n;                         /* no boot records */
struct dl_choice *dl_boot_choice;
struct dl_status *dl_status;
char (*dl_detail)[32];
/* route.c's */
volatile unsigned dl_fs_gen;
volatile int dl_move_seen = 1;
char dl_move_from[DL_NAME_MAX + 1], dl_move_to[DL_NAME_MAX + 1];

/* the tree after a move of `from` to `to` (a file, or a folder with what is below it) */
static void tree_move(const char *from, const char *to)
{
    char t[DL_NAME_MAX + 2];
    size_t k = strlen(from);
    int i;
    for (i = 0; i < ntree; i++)
        if (!strncmp(tree[i], from, k) && (tree[i][k] == 0 || tree[i][k] == '/')) {
            snprintf(t, sizeof t, "%s%s", to, tree[i] + k);
            strcpy(tree[i], t);
        }
}

static void open_folder(const char *rel)
{
    int i;
    copy_str(cwd, rel, sizeof cwd);
    for (depth = 0, i = 0; cwd[0] && cwd[i]; i++)
        depth += cwd[i] == '/';
    depth += cwd[0] != 0;
    if (read_folder())
        printf("no memory\n");
}

int main(void)
{
    static char line[140000];
    int i, part = 0;
    files = calloc(64, sizeof *files);   /* the saved build, as scan() takes it from active */
    fcap = 64;
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!strcmp(line, "--")) {
            if (++part == 2)
                break;
            continue;
        }
        if (!part && ntree < 64)
            strcpy(tree[ntree++], line);
        else if (part == 1 && nfiles < 64) {
            copy_str(files[nfiles].name, line, sizeof files[nfiles].name);
            files[nfiles].pick = files[nfiles].on = 1;
            nfiles++;
        }
    }
    open_folder("");                     /* dl_mods_open: scan, then build_list */
    build_list();
    mode = M_DRIVE;
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!strncmp(line, "open ", 5) || !strcmp(line, "open"))
            open_folder(line[4] ? line + 5 : "");
        else if (!strncmp(line, "mv ", 3)) {
            char *to = strchr(line + 3, '|');
            if (!to)
                continue;
            *to++ = 0;
            tree_move(line + 3, to);
            copy_str(dl_move_from, line + 3, sizeof dl_move_from);
            copy_str(dl_move_to, to, sizeof dl_move_to);
            dl_move_seen = 0;
            dl_fs_gen++;
            follow_route();
        } else if (!strncmp(line, "checked ", 8) || !strncmp(line, "id ", 3) || !strncmp(line, "boot ", 5)) {
            char *arg = strchr(line, ' ') + 1, *id = strchr(arg, '|');
            int f;
            if (id)
                *id++ = 0;
            f = find_file(arg);
            if (f < 0)
                continue;
            if (line[0] == 'c')
                files[f].checked = 1;
            else if (line[0] == 'i')
                copy_str(files[f].id, id ? id : "", sizeof files[f].id);
            else {
                static struct dl_choice bc;
                static struct dl_status bs;
                static char bd[1][32];
                copy_str(bc.name, arg, sizeof bc.name);
                memset(&bs, 0, sizeof bs);
                bs.state = DL_ST_LOADED;
                strncpy(bs.id, id ? id : "", DL_ID_MAX - 1);
                dl_boot_choice = &bc;
                dl_status = &bs;
                dl_detail = bd;
                dl_status_n = 1;
                files[f].id[0] = 0;
                boot_record(&files[f]);
                dl_status_n = 0;
            }
        } else if (!strncmp(line, "info ", 5)) {
            char t[160], *c;
            int row = atoi(line + 5);
            if (row < 0 || row >= nbuild)
                continue;
            info_text(&files[build_rows[row]], t, t + sizeof t);
            for (c = t; *c; c++)
                if (*c == '\n')
                    *c = '~';
            printf("INFO %s\n", t);
        } else if (!strncmp(line, "store ", 6)) {
            const char *h = line + 6;
            for (store_n = 0; h[0] && h[1] && store_n < (s32)sizeof store_hex; h += 2) {
                unsigned v;
                sscanf(h, "%2x", &v);
                store_hex[store_n++] = (u8)v;
            }
            parks_from(store_hex, store_n);
        } else if (!strncmp(line, "forget ", 7)) {
            static u8 out[65536];
            const char *ids[16];
            char *p = line + 7;
            int n = 0;
            s32 k;
            while (p && n < 16) {
                ids[n++] = p;
                p = strchr(p, '|');
                if (p)
                    *p++ = 0;
            }
            k = dl_park_forget(store_hex, (u32)store_n, ids, n, out);
            printf("FORGET ");
            if (k < 0)
                printf("-1");
            for (i = 0; i < k; i++)
                printf("%02x", out[i]);
            printf("\n");
        } else if (!strncmp(line, "setid ", 6)) {
            char *id;
            int n = (int)strtol(line + 6, &id, 10);
            if (n >= 0 && n < DL_MAX_MODS && *id)
                copy_str(set_recs[n].id, id + 1, DL_ID_MAX);
        } else if (!strncmp(line, "reason ", 7)) {
            char t[160], *d;
            int c = (int)strtol(line + 7, &d, 10);
            u32 a = (u32)strtoul(d, &d, 0);
            reason(t, t + sizeof t, c, a, *d ? d + 1 : "", set_recs, DL_MAX_MODS);
            out_text("REASON", t);
        } else if (!strncmp(line, "parkinfo ", 9)) {
            char t[160];
            int k = atoi(line + 9);
            if (k >= 0 && k < npark) {
                park_text(&parks[k], t, t + sizeof t);
                out_text("PARKINFO", t);
            }
        } else if (!strcmp(line, "check")) {
            publish_check();
            printf("CHECK %d/%d %s\n", dl_check_n, dl_check_all, dl_check_dir);
        } else if (!strcmp(line, "drive")) {
            char t[16];
            printf("DRIVE");
            for (i = 0; i < nlrows; i++) {
                row_column(i, t, t + sizeof t);
                printf("%c%s=%s", i ? '|' : ' ', (const char *)dl_page_text(i + 1), t);
            }
            printf("\n");
        } else if (!strcmp(line, "build")) {
            mode = M_BUILD;
            printf("BUILD");
            for (i = 0; i < list_n(); i++) {
                char t[16];
                row_column(i, t, t + sizeof t);
                printf("%c%s", i ? '|' : ' ', (const char *)dl_page_text(i + 1));
                if (park_k(i) >= 0)
                    printf("=%s", t);
            }
            printf("\n");
            mode = M_DRIVE;
        }
    }
    return 0;
}
