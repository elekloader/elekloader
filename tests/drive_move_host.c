/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The /mods route's folder requests (route.c: move, make_folder, keep_active), built for the
 * computer over a made-up ekFS with stock 1.74's rules as Ghidra reads them (README, "Firmware
 * calls"): rename (0x4007bcd2) fails when FROM is missing, TO is there or TO's folder isn't, and
 * moves a folder only within its own parent; mkdir (0x4007c1dc) makes one folder whose parent is
 * there; rmdir an empty folder only; a file opened to write makes its folders.
 * tests/test_drive_route.py builds it with the host's cc and drives it.
 * stdin: the tree, paths below DL_DIR one a line (a folder ends in "/", a file may end in
 * "=TEXT", "|" a newline in it), then "--", then requests:
 *   mv FROM|TO     an upload of "FROM\nTO" to .mv     -> "ok" or the refusal
 *   mkdir FOLDER   an upload to FOLDER/.dir           -> "ok" or "failed"
 * After each, the tree: "TREE p1 ; p2 ; ..." sorted, and "ACTIVE text" with "|" for newlines. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DL_HOST_FS
static void *host_new(unsigned n) { return calloc(1, n ? n : 1); }
#define NEW host_new
#define DELETE free
#include "../mods/drive-loader-ar2/route.c"

unsigned char drive_staging[DL_STAGING_SIZE];
volatile int dl_staging_owner;
volatile unsigned dl_staging_epoch;
volatile int dl_reads;
const unsigned char core_desc[16];
void dl_lock(void) {}
void dl_unlock(void) {}

/* ---- the made-up ekFS: absolute paths ("/mods/a/x.elemod") ---- */
#define MAXN 128
static struct { char path[DL_PATH_MAX]; int dir; char *data; unsigned n; } fsn[MAXN];
static int nfs;

static int find(const char *p)
{
    int i;
    for (i = 0; i < nfs; i++)
        if (fsn[i].path[0] && !strcmp(fsn[i].path, p))
            return i;
    return -1;
}

static void dirname_of(char *out, const char *p)
{
    const char *s = strrchr(p, '/');
    size_t n = s ? (size_t)(s - p) : 0;
    memcpy(out, p, n);
    out[n] = 0;
}

static int is_dir(const char *p) { int i = find(p); return !p[0] || (i >= 0 && fsn[i].dir); }

static int fs_add(const char *p, int dir)
{
    int i = nfs++;
    strcpy(fsn[i].path, p);
    fsn[i].dir = dir;
    fsn[i].data = 0;
    fsn[i].n = 0;
    return i;
}

int dl_fs_mkdir(const char *p)
{
    char d[DL_PATH_MAX];
    dirname_of(d, p);
    if (find(p) >= 0 || !is_dir(d))
        return -1;
    fs_add(p, 1);
    return 0;
}

static void mkdir_p(const char *p)
{
    char sub[DL_PATH_MAX];
    size_t i, n = strlen(p);
    for (i = 1; i <= n; i++)
        if (i == n || p[i] == '/') {
            memcpy(sub, p, i);
            sub[i] = 0;
            if (find(sub) < 0)
                dl_fs_mkdir(sub);
        }
}

int dl_fs_rmdir(const char *p)
{
    int i = find(p), j;
    size_t k = strlen(p);
    if (i < 0 || !fsn[i].dir)
        return -1;
    for (j = 0; j < nfs; j++)
        if (fsn[j].path[0] && !strncmp(fsn[j].path, p, k) && fsn[j].path[k] == '/')
            return -1;
    fsn[i].path[0] = 0;
    return 0;
}

int dl_fs_rename(const char *a, const char *b)
{
    char da[DL_PATH_MAX], db[DL_PATH_MAX], t[DL_PATH_MAX];
    int i = find(a), j;
    size_t k = strlen(a);
    dirname_of(da, a);
    dirname_of(db, b);
    if (i < 0 || find(b) >= 0 || !is_dir(db) || (fsn[i].dir && strcmp(da, db)))
        return -1;
    for (j = 0; j < nfs; j++)
        if (j == i || (fsn[j].path[0] && !strncmp(fsn[j].path, a, k) && fsn[j].path[k] == '/')) {
            snprintf(t, sizeof t, "%s%s", b, fsn[j].path + k);
            strcpy(fsn[j].path, t);
        }
    return 0;
}

int dl_fs_delete(const char *p)
{
    int i = find(p);
    if (i < 0 || fsn[i].dir)
        return -1;
    fsn[i].path[0] = 0;
    return 0;
}

int dl_fs_file_size(const char *p)
{
    int i = find(p);
    return i < 0 || fsn[i].dir ? -1 : (int)fsn[i].n;
}

/* a file object holds the node's index and the read position */
int dl_fs_open(dl_file *f, const char *p, int write)
{
    int i = find(p);
    int *s = (int *)f->raw;
    s[0] = -1;
    s[1] = 0;
    if (write) {
        char d[DL_PATH_MAX];
        dirname_of(d, p);
        mkdir_p(d);
        if (i >= 0 && fsn[i].dir)
            return -1;
        if (i < 0)
            i = fs_add(p, 0);
        free(fsn[i].data);
        fsn[i].data = 0;
        fsn[i].n = 0;
    } else if (i < 0 || fsn[i].dir)
        return -1;
    s[0] = i;
    return 0;
}

int dl_fs_read(dl_file *f, void *buf, unsigned n)
{
    int *s = (int *)f->raw;
    unsigned left;
    if (s[0] < 0)
        return -1;
    left = fsn[s[0]].n - (unsigned)s[1];
    if (n > left)
        n = left;
    memcpy(buf, fsn[s[0]].data + s[1], n);
    s[1] += (int)n;
    return (int)n;
}

int dl_fs_write(dl_file *f, const void *buf, unsigned n)
{
    int *s = (int *)f->raw;
    if (s[0] < 0)
        return -1;
    fsn[s[0]].data = realloc(fsn[s[0]].data, fsn[s[0]].n + n + 1);
    memcpy(fsn[s[0]].data + fsn[s[0]].n, buf, n);
    fsn[s[0]].n += n;
    return 0;
}

void dl_fs_close(dl_file *f) { (void)f; }

int dl_list_dir(const char *rel, struct dl_ent *out, int max)
{
    char path[DL_PATH_MAX];
    int i, k = 0;
    size_t a;
    path_of(path, rel, "");
    if (!rel[0])
        path[strlen(path) - 1] = 0;
    a = strlen(path);
    for (i = 0; i < nfs; i++) {
        const char *p = fsn[i].path;
        if (!p[0] || strncmp(p, path, a) || p[a] != '/' || strchr(p + a + 1, '/'))
            continue;
        if (out && k < max) {
            strcpy(out[k].name, p + a + 1);
            out[k].dir = (unsigned char)fsn[i].dir;
        }
        k++;
    }
    return out ? (k < max ? k : max) : k + 2;   /* the count: an upper bound, with . and .. */
}

static int cmp(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static void show(void)
{
    char *v[MAXN];
    int i, n = 0, a = find(DL_DIR "/active");
    size_t k = strlen(DL_DIR "/");
    for (i = 0; i < nfs; i++)
        if (fsn[i].path[0] && strlen(fsn[i].path) > k)
            v[n++] = fsn[i].path;
    qsort(v, (size_t)n, sizeof *v, cmp);
    printf("TREE");
    for (i = 0; i < n; i++)
        printf("%s%s%s", i ? " ; " : " ", v[i] + k, fsn[find(v[i])].dir ? "/" : "");
    printf("\nACTIVE ");
    if (a >= 0)
        for (i = 0; i < (int)fsn[a].n; i++)
            putchar(fsn[a].data[i] == '\n' ? '|' : fsn[a].data[i]);
    printf("\n");
}

int main(void)
{
    char line[512], path[DL_PATH_MAX];
    fs_add(DL_DIR, 1);
    while (fgets(line, sizeof line, stdin)) {
        char *eq, *c;
        int i;
        size_t n;
        line[strcspn(line, "\r\n")] = 0;
        if (!strcmp(line, "--"))
            break;
        eq = strchr(line, '=');
        if (eq)
            *eq++ = 0;
        n = strlen(line);
        if (line[n - 1] == '/')
            line[n - 1] = 0;
        path_of(path, line, "");
        mkdir_p(path);
        if (line[n - 1] == 0) {
            if (find(path) < 0)
                fs_add(path, 1);
            continue;
        }
        i = find(path) >= 0 ? find(path) : fs_add(path, 0);
        fsn[i].dir = 0;
        if (eq) {
            for (c = eq; *c; c++)
                if (*c == '|')
                    *c = '\n';
            fsn[i].n = (unsigned)strlen(eq);
            fsn[i].data = strdup(eq);
        }
    }
    while (fgets(line, sizeof line, stdin)) {
        size_t n;
        line[strcspn(line, "\r\n")] = 0;
        if (!strncmp(line, "mv ", 3)) {
            const char *why;
            char *bar = strchr(line + 3, '|');
            if (bar)
                *bar = '\n';
            n = strlen(line + 3);
            memcpy(drive_staging, line + 3, n);
            why = move((unsigned)n);
            dl_move_seen = 1;            /* the page took it */
            printf("%s\n", why ? why : "ok");
        } else if (!strncmp(line, "mkdir ", 6)) {
            printf("%s\n", make_folder(line + 6) ? "failed" : "ok");
        }
        show();
    }
    return 0;
}
