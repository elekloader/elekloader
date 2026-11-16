/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The A4 Drive Loader's MODS page (mods/drive-loader-a4mk2/page.c) on the computer: its std1
 * drawing, keys and turns, with a made-up /mods in memory and the linker's checks passing every
 * file. tests/test_drive_a4_page.py builds it with the host's cc and reads what it prints.
 *
 * argv[1] is a script, one step a character:
 *   o open the page (the MODS row's select)    u d l r y n   UP DOWN LEFT RIGHT YES NO (press+release)
 *   U   FUNC + DOWN                            + -   a DATA encoder turn up / down
 *   a   an encoder A turn                      t   20 ticks (1 s: past the checks' quiet time)
 *   T   200 ticks                              f   a frame: prints its text, "F" lines
 *   s   print the state ("S" line)             k   the PERF key (0x30)
 * Every key and turn prints "TAKE key=... ret=n" when the page took it (n 1) or not (n 0).
 * argv[2..] are the files in /mods ("name:size", a folder as "dir/"; "dir/x.elemod:size" inside). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned char u8;
static unsigned long long bus;
#define DL_BUS_NOW() ((unsigned)bus)

#include "../mods/drive-loader-a4mk2/page.c"

/* ---- std1's helpers: text is logged per frame, boxes are not drawn ---- */
static char flog[64][96];
static int nflog;

static int composes;                     /* core_redraw asked for a frame: the next tick composes one */
void core_redraw(void) { composes = 1; }
void core_fill(void *fb, int x, int y, int w, int h, int on)
{
    (void)fb;
    if (w == 3 && h == 3 && nflog < 64)  /* a row's tick */
        snprintf(flog[nflog++], sizeof flog[0], "%3d %2d %sTICK", x, y, on ? " " : "*");
}
int core_text(void *fb, int x, int y, unsigned style, const char *str)
{
    (void)fb;
    if (nflog < 64)
        snprintf(flog[nflog++], sizeof flog[0], "%3d %2d %s%s", x, y, style & TEXT_INVERT ? "*" : " ", str);
    return (int)strlen(str) * 4;
}
int core_text_width(int font, const char *str) { (void)font; return (int)strlen(str) * 4; }
/* core's screen calls (font 13 for the list): y from the bottom, logged as the top row */
int core_font_height(int font) { (void)font; return 6; }
void core_text_draw(void *fb, int font, int x, int y, int align, const char *str)
{
    (void)fb;
    if (nflog < 64)
        snprintf(flog[nflog++], sizeof flog[0], "%3d %2d %s%s", x, 64 - core_font_height(font) - y,
                 align & 0x10 ? "*" : " ", str);
}
/* core_additem: the row's select runs at once, with a functor whose payload is the menu */
void core_additem(void *menu, const void *row)
{
    void *f[2] = {menu, 0};
    ((void (*)(void **))((const void *const *)row)[1])(f);
}
void dl_mods_label(void) {}
void dl_mods_icon(void) {}

/* ---- the made-up /mods ---- */
struct ent { char path[DL_NAME_MAX + 1]; int dir; u32 size; };
static struct ent ents[64];
static int nents;
static char active[4096];
static int active_len = -1;
static char tmp_text[4096];
static int tmp_len = -1;

char core_desc[4];
struct dl_status *dl_status;
char (*dl_detail)[32];
u32 dl_status_n;
u8 dl_skip_reason;
struct dl_choice *dl_boot_choice;
u8 dl_stage_top;
static u8 stage[0x8000];
unsigned char *volatile dl_stage_at = stage;
volatile unsigned dl_stage_len = sizeof stage;
volatile unsigned dl_fs_gen;
volatile int dl_move_seen = 1;
char dl_move_from[DL_NAME_MAX + 1], dl_move_to[DL_NAME_MAX + 1];
const char *const dl_store_scopes[DL_STORE_SCOPES] = {"sound", "kit", "global", "track", "pattern", "project"};
static int staging;

void *dl_alloc(u32 n) { return calloc(1, n ? n : 1); }
void dl_free(void *p) { free(p); }
void dl_memcpy(void *d, const void *s, u32 n) { memmove(d, s, n); }
void dl_memset(void *d, int c, u32 n) { memset(d, c, n); }
int dl_strcmp(const char *a, const char *b) { return strcmp(a, b); }
u32 dl_strlen(const char *s) { return (u32)strlen(s); }
static int locked, guard_off;
void dl_lock(void) { locked++; }
void dl_unlock(void) { locked--; }
void dl_guard_off(void) { guard_off = 1; }
void core_restart(void)                  /* never returns: the script ends here */
{
    printf("RESTART locked=%d guard_off=%d box=%s\n", locked, guard_off, box);
    exit(0);
}
int dl_staging_take_idle(unsigned now, unsigned stall) { (void)now, (void)stall; if (staging) return 0; staging = 1; return 1; }
void dl_staging_give(void) { staging = 0; }
void dl_readers_poll(unsigned now) { (void)now; }
int dl_area(u32 desc, u32 *lo, u32 *hi) { (void)desc; *lo = 0; *hi = 0x100000; return 0; }
int dl_park_list(const u8 *raw, u32 len, struct dl_park *out, int max) { (void)raw, (void)len, (void)out, (void)max; return 0; }
s32 dl_park_forget(const u8 *raw, u32 len, const char *const *ids, int n, u8 *out) { (void)raw, (void)ids, (void)n, (void)out; return (s32)len; }

static const char *rel_of(const char *path)  /* DL_DIR "/x" -> "x" */
{
    size_t n = strlen(DL_DIR);
    return !strncmp(path, DL_DIR, n) && path[n] == '/' ? path + n + 1 : path;
}

static struct ent *find(const char *rel)
{
    int i;
    for (i = 0; i < nents; i++)
        if (!strcmp(ents[i].path, rel))
            return &ents[i];
    return 0;
}

int dl_list_dir(const char *rel, struct dl_ent *out, int max)
{
    int i, k = 0;
    size_t n = strlen(rel);
    for (i = 0; i < nents; i++) {
        const char *p = ents[i].path, *b;
        if (n && (strncmp(p, rel, n) || p[n] != '/'))
            continue;
        b = n ? p + n + 1 : p;
        if (strchr(b, '/'))
            continue;
        if (out && k < max) {
            snprintf(out[k].name, sizeof out[k].name, "%s", b);
            out[k].dir = (u8)ents[i].dir;
        }
        k++;
    }
    return out ? (k < max ? k : max) : k;
}

int dl_fs_file_size(const char *path)
{
    const char *r = rel_of(path);
    struct ent *e;
    if (!strcmp(r, "active"))
        return active_len;
    if (!strcmp(r, "active.tmp"))
        return tmp_len;
    e = find(r);
    return e && !e->dir ? (int)e->size : -1;
}

s32 dl_read_file(const char *name, u8 *buf, u32 max)
{
    if (strcmp(name, "active") || active_len < 0)
        return -1;
    if ((u32)active_len > max)
        return -2;
    memcpy(buf, active, (size_t)active_len);
    return active_len;
}

int dl_fs_open(dl_file *f, const char *path, int write) { (void)f; if (write && !strcmp(rel_of(path), "active.tmp")) { tmp_len = 0; return 0; } return -1; }
int dl_fs_write(dl_file *f, const void *buf, unsigned n) { (void)f; memcpy(tmp_text + tmp_len, buf, n); tmp_len += (int)n; return 0; }
void dl_fs_close(dl_file *f) { (void)f; }
int dl_fs_delete(const char *path)
{
    const char *r = rel_of(path);
    struct ent *e;
    if (!strcmp(r, "active")) {
        active_len = -1;
        return 0;
    }
    e = find(r);
    if (!e || e->dir)
        return -1;
    *e = ents[--nents];
    return 0;
}
int dl_fs_rename(const char *from, const char *to)
{
    if (strcmp(rel_of(from), "active.tmp") || strcmp(rel_of(to), "active"))
        return -1;
    memcpy(active, tmp_text, (size_t)tmp_len);
    active_len = tmp_len;
    tmp_len = -1;
    return 0;
}
int dl_fs_rmdir(const char *path)
{
    const char *r = rel_of(path);
    struct ent *e = find(r);
    int i;
    size_t n = strlen(r);
    for (i = 0; i < nents; i++)
        if (!strncmp(ents[i].path, r, n) && ents[i].path[n] == '/')
            return -1;                   /* not empty */
    if (!e || !e->dir)
        return -1;
    *e = ents[--nents];
    return 0;
}

int dl_read_open(struct dl_reader *rd, const char *name)
{
    memset(rd, 0, sizeof *rd);
    if (!find(name))
        return -1;
    rd->open = 1;
    rd->st.crc = 0xffffffffu;
    return 0;
}
int dl_read_more(struct dl_reader *rd, unsigned char *buf, unsigned max)
{
    (void)buf, (void)max;
    rd->st.got = 1000;                   /* every made-up file reads in one stretch */
    rd->st.crc = ~0x12345678u;
    rd->size = 1000;
    rd->open = 0;
    return 1;
}
void dl_read_close(struct dl_reader *rd) { rd->open = 0; }

int dl_parse_active(const char *text, u32 len, struct dl_choice *out)
{
    int n = 0;
    const char *p = text, *e = text + len;
    while (p < e) {
        const char *nl = memchr(p, '\n', (size_t)(e - p));
        size_t l = nl ? (size_t)(nl - p) : (size_t)(e - p);
        if (l && *p != '#') {
            if (out) {
                const char *sp = memchr(p, ' ', l);
                size_t k = sp ? (size_t)(sp - p) : l;
                memcpy(out[n].name, p, k);
                out[n].name[k] = 0;
                out[n].crc = sp ? (u32)strtoul(sp + 1, 0, 16) : 0;
            }
            n++;
        }
        p += l + 1;
    }
    return n;
}

/* the checks: every file loads, its id its bare name in upper case */
static void id_of(char *id, const char *path)
{
    const char *b = strrchr(path, '/');
    int i;
    b = b ? b + 1 : path;
    for (i = 0; b[i] && b[i] != '.' && i < DL_ID_MAX - 1; i++)
        id[i] = b[i];
    id[i] = 0;
}
int dl_check_buf(u32 desc, const struct dl_choice *ch, s32 len, u8 *buf, u32 max, struct dl_status *st,
                 char (*det)[32], char (*ver)[DL_VER_MAX])
{
    (void)desc, (void)len, (void)buf, (void)max;
    memset(st, 0, sizeof *st);
    st->state = DL_ST_LOADED;
    id_of(st->id, ch->name);
    det[0][0] = 0;
    strcpy(ver[0], "0.1");
    return 1;
}
static struct dl_status *ck_st;
static int ck_n, ck_i;
int dl_check_start(u32 desc, int n, u8 *buf, u32 max, struct dl_status *st, char (*det)[32], char (*ver)[DL_VER_MAX])
{
    (void)desc, (void)buf, (void)max, (void)det, (void)ver;
    ck_st = st;
    ck_n = n;
    ck_i = 0;
    return 0;
}
int dl_check_step_buf(const struct dl_choice *ch, s32 len, u32 crc)
{
    (void)len, (void)crc;
    ck_st[ck_i].state = DL_ST_LOADED;
    id_of(ck_st[ck_i].id, ch->name);
    ck_i++;
    return ck_n - ck_i;
}
int dl_check_finish(void) { return ck_i; }
void dl_check_cancel(void) {}

/* ---- the script ---- */
static void frame(void)
{
    int i;
    nflog = 0;
    dl_draw(0, 0);
    printf("F %d\n", nflog);
    for (i = 0; i < nflog; i++)
        printf("F %s\n", flog[i]);
}

static void key(unsigned k, unsigned flags)
{
    u8 ev[24] = {0};
    int r;
    unsigned f = flags | 1;
    memcpy(ev + 12, &k, 4);              /* the event's words, in the host's order */
    memcpy(ev + 16, &f, 4);
    r = dl_key(0, ev);
    printf("TAKE key=%02x ret=%d\n", k, r);
    f = flags & ~1u;
    memcpy(ev + 16, &f, 4);
    r = dl_key(0, ev);
    printf("TAKE key=%02x up ret=%d\n", k, r);
}

static void turn(int e, int d)
{
    u8 ev[24] = {0};
    int r;
    memcpy(ev + 12, &e, 4);
    memcpy(ev + 16, &d, 4);
    r = dl_enc(0, ev);
    printf("TAKE enc=%d ret=%d\n", e, r);
}

static void ticks_run(int n)
{
    while (n--) {
        bus += 50000;
        dl_page_tick(0);
        if (composes) {                  /* the frame it asked for, as core's ev_tick composes it */
            composes = 0;
            nflog = 0;
            dl_draw(0, 0);
        }
    }
}

int main(int argc, char **argv)
{
    const char *s;
    int i;
    for (i = 2; i < argc && nents < 64; i++) {
        char *c = strchr(argv[i], ':');
        size_t l = strlen(argv[i]);
        struct ent *e = &ents[nents++];
        if (argv[i][l - 1] == '/') {
            snprintf(e->path, sizeof e->path, "%.*s", (int)l - 1, argv[i]);
            e->dir = 1;
        } else {
            snprintf(e->path, sizeof e->path, "%.*s", c ? (int)(c - argv[i]) : (int)l, argv[i]);
            e->size = c ? (u32)atoi(c + 1) : 1000;
        }
    }
    for (s = argv[1]; *s; s++) {
        switch (*s) {
        case 'o': dl_settings(0); break;
        case 'u': key(K_UP, 0); break;
        case 'd': key(K_DOWN, 0); break;
        case 'U': key(K_DOWN, F_FUNC); break;
        case 'l': key(K_LEFT, 0); break;
        case 'r': key(K_RIGHT, 0); break;
        case 'y': key(K_YES, 0); break;
        case 'n': key(K_NO, 0); break;
        case 'k': key(0x30, 0); break;
        case '+': turn(ENC_DATA, 0x100); break;
        case '-': turn(ENC_DATA, -0x100); break;
        case 'a': turn(0, 0x100); break;
        case 't': ticks_run(20); break;
        case 'T': ticks_run(200); break;
        case 'f': frame(); break;
        case 's': {
            char b[sizeof box], a[8192];
            int j, k = 0;
            for (j = 0; box[j]; j++)
                b[j] = box[j] == ' ' ? '_' : box[j] == '\n' ? '|' : box[j];
            b[j] = 0;
            for (j = 0; j < active_len && k < (int)sizeof a - 3; j++)
                if (active[j] == '\n')
                    a[k++] = '\\', a[k++] = 'n';
                else
                    a[k++] = active[j] == ' ' ? '_' : active[j];
            a[k] = 0;
            printf("S open=%d mode=%d cur=%d top=%d panel=%d pending=%d staging=%d box=%s active=%s\n",
                   page_on, mode, cur, top, panel, pending, staging, box[0] ? b : "-", a);
            break;
        }
        }
    }
    return 0;
}
