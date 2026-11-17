/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The /mods route's folder requests (route.c: move, make_folder, keep_active), built for the
 * computer over a made-up ekFS with stock 1.74's rules as Ghidra reads them (README, "Firmware
 * calls"): rename (0x4007bcd2) fails when FROM is missing, TO is there or TO's folder isn't, and
 * moves a folder only within its own parent; mkdir (0x4007c1dc) makes one folder whose parent is
 * there; rmdir an empty folder only; a file opened to write makes its folders.
 * tests/test_drive_route.py builds it with the host's cc and drives it.
 * stdin: the tree, paths below DL_DIR one a line (a folder ends in "/", a file may end in
 * "=TEXT", "|" a newline in it; a first line "!nomods": no DL_DIR yet, as on a fresh card), then
 * "--", then requests:
 *   mv FROM|TO     an upload of "FROM\nTO" to .mv     -> "ok" or the refusal
 *   mkdir FOLDER   an upload to FOLDER/.dir           -> "ok" or "failed"
 *   entry NAME     a file's entry (file_perm)          -> "ok SIZE read write clear" or the refusal
 *   stage N        the live stage: N bytes inside a guarded arena (route.c dl_stage_at, dl_stage_len)
 *   pre SIZE       an upload's +0x1c with SIZE in the entry (dl_pre)   -> "ok" or the refusal
 *   hdr SIZE       its +0x6c with a header of SIZE payload bytes (dl_hdr) -> "ok" or the refusal
 *   read NAME      a read's fill (dl_fill)                -> "read N" (bytes the file put in the stage)
 *   put NAME SIZE  an upload's commit of SIZE bytes from the stage (dl_hdr, dl_change) -> what .status
 *                  notes, and the file's size
 *   status         .status's stage line
 * Each of the last five ends its line in " GUARD ok" when no byte around the stage changed (" GUARD
 * bad" when one did); nothing with the stage left at the floor.
 * A tree line "NAME=@N" is a file of N bytes.
 * After each, the tree: "TREE p1 ; p2 ; ..." sorted, and "ACTIVE text" with "|" for newlines. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DL_HOST_FS
static void *host_new(unsigned n) { return calloc(1, n ? n : 1); }
#define NEW host_new
#define DELETE free
static const char *host_str;                 /* the last std::string the route made */
static void host_str_new(void *slot, const char *s, void *a) { (void)slot, (void)a; host_str = s; }
#define STR_NEW host_str_new
#include "../mods/drive-loader-ar2/route.c"

/* the arena the stage command puts the stage in, with GUARD bytes of 0xa5 around it */
#define GUARD 4096
#define ARENA_MAX (1u << 21)
static unsigned char arena[GUARD + ARENA_MAX + GUARD];

static void stage(unsigned n)
{
    memset(arena, 0xa5, sizeof arena);
    dl_stage_at = arena + GUARD;
    dl_stage_len = n;
}

static void guard(void)
{
    const unsigned char *lo, *hi, *p;
    if (dl_stage_at == drive_staging)
        return;
    lo = dl_stage_at;
    hi = dl_stage_at + dl_stage_len;
    for (p = arena; p < arena + sizeof arena; p++)
        if ((p < lo || p >= hi) && *p != 0xa5) {
            printf(" GUARD bad");
            return;
        }
    printf(" GUARD ok");
}

/* a C_FILE closure for name, as dl_entry makes it */
static struct clo *file_clo(const char *name)
{
    static struct clo c;
    char path[DL_PATH_MAX];
    int size;
    memset(&c, 0, sizeof c);
    c.what = C_FILE;
    strcpy(c.name, name);
    path_of(path, name, "");
    size = dl_fs_file_size(path);
    c.size = size < 0 ? 0 : (unsigned)size;
    return &c;
}

static void header(unsigned char *h, unsigned size)
{
    memset(h, 0, 31);
    h[H_TYPE] = 'E', h[H_TYPE + 1] = 'M', h[H_TYPE + 2] = 'O', h[H_TYPE + 3] = 'D';
    h[H_PAYLOAD] = size >> 24, h[H_PAYLOAD + 1] = size >> 16, h[H_PAYLOAD + 2] = size >> 8;
    h[H_PAYLOAD + 3] = size;
}

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
        if (!strcmp(line, "!nomods")) {
            fsn[find(DL_DIR)].path[0] = 0;
            continue;
        }
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
        if (eq && eq[0] == '@') {           /* "=@N": N bytes of zeros */
            fsn[i].n = (unsigned)strtoul(eq + 1, 0, 0);
            fsn[i].data = calloc(1, fsn[i].n + 1);
        } else if (eq) {
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
        } else if (!strncmp(line, "entry ", 6)) {
            int size, perm = 0;
            const char *why;
            path_of(path, line + 6, "");
            size = dl_fs_file_size(path);
            why = file_perm(line + 6, &size, &perm);
            if (why)
                printf("%s\n", why);
            else
                printf("ok %d%s%s%s\n", size, perm & P_READ ? " read" : "", perm & P_WRITE ? " write" : "",
                       perm & P_CLEAR ? " clear" : "");
        } else if (!strncmp(line, "mkdir ", 6)) {
            printf("%s\n", make_folder(line + 6) ? "failed" : "ok");
        } else if (!strncmp(line, "stage ", 6)) {
            stage((unsigned)strtoul(line + 6, 0, 0));
            printf("ok\n");
        } else if (!strncmp(line, "pre ", 4) || !strncmp(line, "hdr ", 4)) {
            unsigned char res[12], ent[ENTRY_SIZE], h[31];
            struct clo *c = file_clo("x.elemod");
            void *f = c;
            unsigned size = (unsigned)strtoul(line + 4, 0, 0);
            memset(ent, 0, sizeof ent);
            *(unsigned *)(ent + E_SIZE) = size;
            header(h, size);
            if (line[0] == 'p')
                dl_pre(res, &f, ent);
            else
                dl_hdr(res, &f, h);
            printf("%s", res[0] ? "ok" : host_str);
            guard();
            printf("\n");
        } else if (!strncmp(line, "read ", 5)) {
            struct clo *c = file_clo(line + 5);
            void *f = c;
            unsigned k, n = 0;
            dl_fill(&f);
            for (k = 0; k < c->size && k < dl_stage_len; k++)
                n += dl_stage_at[k] != 0;
            printf("read %u", n);
            guard();
            printf("\n");
        } else if (!strncmp(line, "put ", 4)) {
            char *sp = strrchr(line, ' ');
            unsigned char res[12], h[31];
            unsigned size = (unsigned)strtoul(sp + 1, 0, 0), k;
            struct clo *c;
            void *f;
            *sp = 0;
            c = file_clo(line + 4);
            f = c;
            header(h, size);
            dl_hdr(res, &f, h);
            for (k = 0; k < size && k < dl_stage_len; k++)
                dl_stage_at[k] = (unsigned char)(k % 251 + 1);
            dl_change(&f, h);
            path_of(path, line + 4, "");
            printf("%s %d", dl_note, dl_fs_file_size(path));
            guard();
            printf("\n");
        } else if (!strcmp(line, "status")) {
            const char *t;
            status_text();
            t = strstr(dl_status_text, "stage: ");
            printf("%.*s", (int)strcspn(t, "\n"), t);
            guard();
            printf("\n");
        }
        show();
    }
    return 0;
}
