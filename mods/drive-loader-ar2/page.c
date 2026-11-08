/* SPDX-License-Identifier: GPL-2.0-or-later */
/* drive-loader (Analog Rytm MKII, OS 1.74): the MODS page (README, "The MODS page").
 *
 * A MODS row at the bottom of SETTINGS > GLOBAL (ev_settings, core_additem) opens stock's
 * two-pane file view, the one SAMPLES uses, on the folders and .elemod files in DL_DIR, one
 * folder at a time to DL_DEPTH deep. It works like SAMPLES (decided 2026-11-07): [YES] opens a
 * folder or ticks a file, RIGHT opens the action panel, and LOAD TO BUILD saves exactly the
 * ticked files, from every folder, as the build. LEFT opens VIEW BUILD / VIEW +DRIVE,
 * which switches the list to the build. Every change runs the boot linker's own pass 1,
 * checks and layout on the new set (dl_check, dlink.c) against core_desc: a set that would
 * load whole is written to DL_DIR/active for the next boot, one that wouldn't is never saved
 * and a box says why. The page never loads or unloads code.
 *
 * Everything here runs in the UI task. The route runs in the SysEx tasks: the page takes
 * the staging buffer only while it reads one file (the background check of the listed files,
 * one at a time while the page shows, and the files of a change), and not while a read of a file is open (dl_staging_take_idle), since stock streams a read
 * out of the buffer after the fill; a read that hasn't moved for READ_STALL ticks was left
 * by its client and doesn't count. The route answers "busy" while the page holds it, and
 * every ekFS call is made under dl_lock. All addresses are OS 1.74's (README, "Firmware
 * calls"). */

#include "dl.h"
#include "fs.h"

/* stock's menus (core-ar2 README; trace 2, section 1) */
#define NEW ((void *(*)(unsigned))0x40082a40)
#define ITEM_CTOR ((void (*)(void *, void *, void *, void *, void *, int, int))0x40072fbc)
#define MENU_ADD ((void (*)(void *, void *))0x400727f2)
#define FN_MGR 0x4019411au               /* std::function's manager for a plain pointer */
#define VIEW_SP 0x4007762cu              /* (parent, view + 0x38) -> %a0: shared_ptr<View> */
#define PUSH ((void (*)(void *, void *))0x40077a16)   /* (parent, &shared_ptr): show it */
#define SP_RELEASE ((void (*)(void *))0x40170bd8)     /* (&ctrl) */
#define STR_NEW ((void (*)(void *, const char *, void *))0x401bcff0)
#define STR_DEL ((void (*)(void *))0x401bb540)
/* printf-style text: (canvas, font, x, y, align 0 left, box 0 none, clear-as-wide-as, fmt, ...) */
#define TEXT ((void (*)(void *, unsigned, int, int, int, int, const char *, const char *, ...))0x4007154c)

extern char core_desc[];
extern void core_additem(void *menu, const void *row);
extern void dl_mods_label(void), dl_page_label(void), dl_mods_icon(void);   /* page.s */
extern void dl_call_a0(unsigned fn, void *res, void *a, unsigned b);   /* route.s */

#define READ_STALL 600                   /* ev_ticks (30 s) a read may stand still and count */
#define LINE_MAX (DL_NAME_MAX + 10)      /* an /mods/active line: path, space, 8 digits, \n */

/* Folders (decided 2026-11-08): the file list shows one folder of DL_DIR at a time, as SAMPLES
 * does, up to DL_DEPTH folders deep: "..", then its folders, then its .elemod files. YES on a
 * folder opens it, YES on ".." goes up with the cursor back on the folder just left. A file is
 * known by its path below DL_DIR ("drums/kicks/x.elemod"), the name /mods/active keeps. Unlike
 * SAMPLES the ticks stay when the folder changes (decided 2026-11-08): they are the build, and a
 * build takes mods from any folder. So the page keeps the ticks itself, one flag a file (pick),
 * and stock's tick vector only mirrors the open folder's. */

struct file {                            /* a file the page knows: ticked, saved, loaded, or listed */
    char name[DL_NAME_MAX + 1];          /* its path below DL_DIR */
    u32 crc, size;
    u8 pick;                             /* ticked */
    u8 on;                               /* in the saved set */
    u8 keep, was;                        /* a new set's files, and the old one's (a save) */
    u8 here;                             /* in the open folder */
    u8 file_code;                        /* a refusal of the file itself (pass 1), or 0 */
    u8 boot;                             /* its boot record: DL_ST_*, or DL_ST_NONE */
    u8 boot_code;
    u16 boot_arg;                        /* the boot record's number (NO ROOM's KB, FULL's spare) */
    u8 checked;                          /* read and checked (crc, id, ver, file_code known) */
    char id[DL_ID_MAX];
    char ver[DL_VER_MAX];
    char detail[32];                     /* the file's refusal's name argument */
    char boot_detail[32];                /* the boot record's */
};

enum { R_UP, R_DIR, R_FILE };
struct lrow {                            /* a row of the open folder */
    u8 kind;
    u16 sel;                             /* a folder: its ticked files, at any depth below it */
    int f;                               /* a file: its index in files */
    char name[DL_SEG_MAX + 1];           /* a folder: its name */
};

/* The list grows like SAMPLES' (decided 2026-11-07): the arrays come from the heap when the page
 * reads a folder, sized to what it keeps (the ticked, saved and loaded files) and the folder's
 * entries, and go back at the next read. A save's set (choice .. cdet) is taken for the save
 * alone. */
static struct file *files;
static int nfiles, fcap;
static struct lrow *lrows;
static int nlrows;
static int *build_rows, nbuild;
static int *sel;
static char cwd[DL_NAME_MAX + 1];        /* the open folder below DL_DIR ("" DL_DIR itself) */
static int depth;                        /* its folders below DL_DIR */
static struct dl_choice *choice;         /* a save's set, from the heap while it runs */
static struct dl_status *cst;
static char (*cdet)[32];
static char label[48];

static void set_free(void)
{
    dl_free(choice);
    dl_free(cst);
    dl_free(cdet);
    choice = 0;
    cst = 0;
    cdet = 0;
}

static void page_free(void)
{
    dl_free(files);
    dl_free(lrows);
    dl_free(build_rows);
    dl_free(sel);
    files = 0;
    lrows = 0;
    build_rows = sel = 0;
    nfiles = fcap = nlrows = nbuild = 0;
    set_free();
}

static u8 redraw;                        /* ask the next ev_tick for a frame */
static volatile u32 ticks, drawn_at;     /* the page's rows were drawn at this tick */
static int next_check;                   /* the next row the background checks (check_next) */
static volatile u32 last_input;          /* the tick of the last key or encoder event */
static unsigned held[2];                 /* our shared_ptr to the page's view: it outlives a close */

/* ---- text ---- */

static char *cat(char *at, char *end, const char *s)
{
    while (*s && at < end - 1)
        *at++ = *s++;
    *at = 0;
    return at;
}

static char *num(char *at, char *end, u32 v)
{
    char t[12];
    int i = 0;
    do
        t[i++] = (char)('0' + v % 10);
    while ((v /= 10) && i < 11);
    while (i && at < end - 1)
        *at++ = t[--i];
    *at = 0;
    return at;
}

static char *hex8(char *at, char *end, u32 v)
{
    int i;
    for (i = 28; i >= 0 && at < end - 1; i -= 4)
        *at++ = "0123456789abcdef"[(v >> i) & 15];
    *at = 0;
    return at;
}

static void copy_str(char *d, const char *s, u32 max)
{
    u32 i;
    for (i = 0; i < max - 1 && s[i]; i++)
        d[i] = s[i];
    d[i] = 0;
}

static int ends_elemod(const char *s)
{
    u32 n = dl_strlen(s);
    return n > 7 && !dl_strcmp(s + n - 7, ".elemod");
}

/* the last part of a path: its file name */
static const char *base_of(const char *p)
{
    const char *b = p;
    for (; *p; p++)
        if (*p == '/')
            b = p + 1;
    return b;
}

/* `p` lies below the folder `dir` (a path below DL_DIR, "" DL_DIR itself) */
static int below(const char *p, const char *dir)
{
    if (!*dir)
        return 1;
    while (*dir && *dir == *p)
        dir++, p++;
    return !*dir && *p == '/';
}

/* rel "/" name into out (DL_NAME_MAX + 1 bytes). -> 0 when it doesn't fit. */
static int join(char *out, const char *rel, const char *name)
{
    u32 a = dl_strlen(rel), b = dl_strlen(name);
    if (a + (a ? 1 : 0) + b > DL_NAME_MAX)
        return 0;
    dl_memcpy(out, rel, a);
    if (a)
        out[a++] = '/';
    dl_memcpy(out + a, name, b + 1);
    return 1;
}

/* a refusal (the codes: dl.h). Stock's message box splits its text at newlines (0x400764cc)
 * and is as wide as its widest line;
 * about 32 characters fit the screen (DL6B, r-47b821), so a longer reason puts its detail on
 * a line of its own (decided 2026-11-07: multiple lines are fine) */
#define BOX_LINE 30

/* a refusal into [at, end); `line` is where its line starts in the same buffer */
static void reason_line(const char *line, char *at, char *end, int code, u32 arg, const char *d)
{
    static const char *const what[] = {
        "", "FILE MISSING", "FILE CHANGED", "TOO BIG", "BAD FILE", "NOT FORMAT 2",
        "OTHER DEVICE/OS", "CAN'T USE", "SAME ID AS", "NEEDS CORE", "NEEDS", "CONFLICTS",
        "EXPORT TAKEN", "CLAIMED", "NOT FOUND", "TABLE", "FULL", "NO ROOM", "OUT OF RANGE",
        "TOO MANY NAMES", "OVER 256 MODS"};
    at = cat(at, end, code < (int)(sizeof what / sizeof what[0]) ? what[code] : "REFUSED");
    if (code == DL_R_ROOM) {
        at = cat(at, end, " +");
        at = num(at, end, arg);
        cat(at, end, "K");
        return;
    }
    if (d && d[0]) {
        u32 tail = dl_strlen(d) + (code == DL_R_FULL ? 12 : 0);
        at = cat(at, end, (u32)(at - line) + 1 + tail > BOX_LINE ? "\n" : " ");
        at = cat(at, end, d);
    }
    if (code == DL_R_FULL) {             /* "FULL ev_x (0 SPARE)" */
        at = num(cat(at, end, " ("), end, arg);
        cat(at, end, " SPARE)");
    }
}

static void reason(char *at, char *end, int code, u32 arg, const char *d)
{
    reason_line(at, at, end, code, arg, d);
}

/* ---- files ---- */

static void path_of(char *out, const char *name)
{
    char *end = out + DL_PATH_MAX;
    cat(cat(out, end, DL_DIR "/"), end, name);
}

/* DL_DIR/active, from files[].on. -> 0, or < 0 when it wasn't written. */
static int save_active(void)
{
    char *text, *end, *at, tmp[DL_PATH_MAX], path[DL_PATH_MAX];
    dl_file f;
    int i, r, on = 0;
    u32 n;
    for (i = 0; i < nfiles; i++)
        on += files[i].on;
    n = 64 + (u32)on * LINE_MAX;
    text = dl_alloc(n);
    if (!text)
        return -1;
    end = text + n;
    at = text;
    at = cat(at, end, "# chosen on the unit's MODS page\n");
    for (i = 0; i < nfiles; i++)
        if (files[i].on) {
            at = cat(at, end, files[i].name);
            at = cat(at, end, " ");
            at = hex8(at, end, files[i].crc);
            at = cat(at, end, "\n");
        }
    n = (u32)(at - text);
    path_of(tmp, "active.tmp");
    path_of(path, "active");
    dl_lock();
    dl_memset(&f, 0, sizeof f);
    r = dl_fs_open(&f, tmp, 1);
    if (!r)
        r = dl_fs_write(&f, text, n);
    dl_fs_close(&f);
    if (!r && dl_fs_file_size(tmp) != (int)n)
        r = -1;
    if (!r) {
        dl_fs_delete(path);
        r = dl_fs_rename(tmp, path);
    } else {
        dl_fs_delete(tmp);
    }
    dl_unlock();
    dl_free(text);
    return r;
}

/* the boot linker's record of a file: the line of the boot's set that named its path */
static void boot_record(struct file *r)
{
    u32 i;
    r->boot = DL_ST_NONE;
    for (i = 0; i < dl_status_n && i < DL_MAX_MODS; i++)
        if (!dl_strcmp(dl_boot_choice[i].name, r->name)) {
            r->boot = dl_status[i].state;
            r->boot_code = dl_status[i].code;
            r->boot_arg = dl_status[i].arg;
            copy_str(r->boot_detail, dl_detail[i], sizeof r->boot_detail);
            return;
        }
}

static int find_file(const char *path)
{
    int i;
    for (i = 0; i < nfiles; i++)
        if (!dl_strcmp(files[i].name, path))
            return i;
    return -1;
}

static int npicks(void)
{
    int i, n = 0;
    for (i = 0; i < nfiles; i++)
        n += files[i].pick;
    return n;
}

/* each folder row's count of ticked files below it */
static void count_sel(void)
{
    int i, k;
    char p[DL_NAME_MAX + 1];
    for (i = 0; i < nlrows; i++) {
        struct lrow *l = &lrows[i];
        if (l->kind != R_DIR)
            continue;
        l->sel = 0;
        if (!join(p, cwd, l->name))
            continue;
        for (k = 0; k < nfiles; k++)
            if (files[k].pick && below(files[k].name, p))
                l->sel++;
    }
}

/* Read the open folder's listing (cwd): its folders, and each .elemod's size, without reading
 * any mod (decided 2026-11-07: opening the page with many large files held the key path and USB
 * for seconds, T178). Each file is read later, one at a time (inspect). The files the page keeps
 * across folders (ticked, saved, loaded at boot) stay with what is known of them. -> 0, -2 no
 * memory (nothing changed). */
static int read_folder(void)
{
    int i, n, kept = 0, k;
    s32 size;
    struct dl_ent *ents;
    struct file *nf;
    struct lrow *nl;
    int *nb, *ns;
    char p[DL_NAME_MAX + 1], path[DL_PATH_MAX];
    dl_lock();
    n = dl_list_dir(cwd, 0, 0);
    if (n < 0)
        n = 0;
    ents = dl_alloc((u32)(n > 0 ? n : 1) * sizeof *ents);
    if (!ents) {
        dl_unlock();
        return -2;
    }
    n = dl_list_dir(cwd, ents, n);
    if (n < 0)
        n = 0;
    for (i = 0; i < nfiles; i++)
        kept += files[i].pick || files[i].on || files[i].boot == DL_ST_LOADED;
    k = kept + n > 0 ? kept + n : 1;
    nf = dl_alloc((u32)k * sizeof *nf);
    nl = dl_alloc((u32)(n + 1) * sizeof *nl);
    nb = dl_alloc((u32)k * sizeof *nb);
    ns = dl_alloc((u32)k * sizeof *ns);
    if (!nf || !nl || !nb || !ns) {
        dl_unlock();
        dl_free(ents);
        dl_free(nf);
        dl_free(nl);
        dl_free(nb);
        dl_free(ns);
        return -2;
    }
    for (i = 0, kept = 0; i < nfiles; i++)
        if (files[i].pick || files[i].on || files[i].boot == DL_ST_LOADED) {
            dl_memcpy(&nf[kept], &files[i], sizeof *nf);
            nf[kept++].here = 0;
        }
    dl_free(files);
    dl_free(lrows);
    dl_free(build_rows);
    dl_free(sel);
    files = nf;
    nfiles = kept;
    fcap = k;
    lrows = nl;
    build_rows = nb;
    sel = ns;
    nlrows = 0;
    if (depth)                           /* "..", as stock's folders have it */
        lrows[nlrows++].kind = R_UP;
    for (i = 0; i < n; i++)              /* the folders, to DL_DEPTH below DL_DIR */
        if (ents[i].dir && depth < DL_DEPTH && ents[i].name[0] != '.' && join(p, cwd, ents[i].name)) {
            lrows[nlrows].kind = R_DIR;
            copy_str(lrows[nlrows++].name, ents[i].name, DL_SEG_MAX + 1);
        }
    for (i = 0; i < n; i++) {            /* then the .elemod files */
        struct file *r;
        int f;
        if (ents[i].dir || !ends_elemod(ents[i].name) || !join(p, cwd, ents[i].name))
            continue;
        f = find_file(p);
        if (f < 0) {
            if (nfiles >= fcap)
                break;
            f = nfiles++;
            r = &files[f];
            dl_memset(r, 0, sizeof *r);
            copy_str(r->name, p, sizeof r->name);
            boot_record(r);
            path_of(path, p);
            size = dl_fs_file_size(path);
            if (size < 0) {
                r->file_code = DL_R_MISSING;
                r->checked = 1;
            } else {
                r->size = (u32)size;
                if (r->size > DL_STAGING_SIZE) {   /* as the boot: a file past the staging buffer */
                    r->file_code = DL_R_TOO_BIG;
                    r->checked = 1;
                }
            }
        }
        files[f].here = 1;
        lrows[nlrows].kind = R_FILE;
        lrows[nlrows++].f = f;
    }
    dl_unlock();
    dl_free(ents);
    count_sel();
    next_check = 0;
    return 0;
}

/* Opening the page: the saved set from DL_DIR/active (ticked: the ticks start as the build), the
 * mods the last boot loaded, then DL_DIR's listing. -> 0, -2 no memory. */
static int scan(void)
{
    int i, k, m = 0, n = 0;
    s32 len = 0, size;
    char *text = 0, path[DL_PATH_MAX];
    struct dl_choice *set = 0;
    page_free();
    cwd[0] = 0;
    depth = 0;
    dl_lock();
    path_of(path, "active");
    size = dl_fs_file_size(path);
    if (size > 0 && (text = dl_alloc((u32)size + 1)) != 0)
        len = dl_read_file("active", (u8 *)text, (u32)size);
    dl_unlock();
    m = len > 0 ? dl_parse_active(text, (u32)len, 0) : 0;
    if (m > 0 && (set = dl_alloc((u32)m * sizeof *set)) != 0)
        dl_parse_active(text, (u32)len, set);
    else
        m = 0;
    dl_free(text);
    for (i = 0; i < (int)dl_status_n && i < DL_MAX_MODS && dl_boot_choice; i++)
        n++;
    files = dl_alloc((u32)(m + n > 0 ? m + n : 1) * sizeof *files);
    if (!files) {
        dl_free(set);
        return -2;
    }
    fcap = m + n;
    for (k = 0; k < m; k++)              /* a changed file stays ticked: its new CRC goes into the next save */
        if (find_file(set[k].name) < 0) {
            struct file *r = &files[nfiles++];
            copy_str(r->name, set[k].name, sizeof r->name);
            r->pick = r->on = 1;
        }
    dl_free(set);
    for (i = 0; i < n; i++)              /* DROP rows: loaded now, gone after the next boot */
        if (dl_status[i].state == DL_ST_LOADED && find_file(dl_boot_choice[i].name) < 0) {
            struct file *r = &files[nfiles++];
            copy_str(r->name, dl_boot_choice[i].name, sizeof r->name);
        }
    for (i = 0; i < nfiles; i++)
        boot_record(&files[i]);
    if (read_folder()) {
        page_free();
        return -2;
    }
    return 0;
}

/* One file read: its CRC-32 and size, then a check of it alone for its id, version and
 * pass-1 verdict (a check's set refusals mean nothing here: only codes about the file itself
 * are kept), so no number of files reaches a set's limits. Holds the staging buffer and the
 * loader's mutex for this one file only. -> 0 (the file is checked), -1 busy. */
static int inspect(struct file *r)
{
    static struct dl_choice one;
    static struct dl_status one_st;
    static char one_det[1][32], one_ver[1][DL_VER_MAX];
    s32 len;
    int c;
    if (r->checked)
        return 0;
    if (!dl_staging_take_idle(ticks, READ_STALL))
        return -1;
    dl_lock();
    len = dl_read_file(r->name, drive_staging, DL_STAGING_SIZE);
    dl_unlock();
    r->checked = 1;
    if (len == -2)
        r->file_code = DL_R_TOO_BIG;
    else if (len < 0)
        r->file_code = DL_R_MISSING;
    else {
        r->size = (u32)len;
        r->crc = dl_crc32(drive_staging, (u32)len);
        copy_str(one.name, r->name, DL_LINE_NAME_MAX);
        one.crc = r->crc;
        if (dl_check_buf((u32)core_desc, &one, len, drive_staging, DL_STAGING_SIZE, &one_st, one_det,
                         one_ver) >= 0) {        /* the file just read, not read again */
            c = one_st.code;
            copy_str(r->id, one_st.id, sizeof r->id);
            copy_str(r->ver, one_ver[0], sizeof r->ver);
            if (one_st.state != DL_ST_LOADED && c && (c <= DL_R_FEATURE || c == DL_R_CORE)) {
                r->file_code = (u8)c;
                copy_str(r->detail, one_det[0], sizeof r->detail);
            }
        }
    }
    dl_staging_give();
    return 0;
}


/* ---- the page: stock's two-pane file view, as SAMPLES' ---- */

/* the two-pane view (SampleManager's and ProjectManager's base): the list, a left panel
 * (x 0..0x2b) and the action panel (x 0x43..0x7f) */
#define PANE_CTOR ((void (*)(void *, const char *, int, int, int, int))0x400734f2)
#define PANE_SIZE 0x50c
#define PANE_LEFT 0x1ac                  /* the left panel (+0x6b words): VIEW BUILD / VIEW +DRIVE */
#define PANE_RIGHT 0x358                 /* the action panel (+0xd6 words) */
#define LIST_CLEAR ((void (*)(void *))0x40072914)
#define VIEW_DIRTY ((void (*)(void *))0x40078580)       /* (view + 0x38) */
#define FOCUS ((void (*)(void *, void *))0x400734a8)    /* (view, panel); (view, view) closes the panels */
/* the list view's calls (vtable offsets; the two-pane view's table 0x401d6974): +0x18 move the cursor
 * by n rows (0x40072024: relative, clamped to the list; found 2026-11-07, the CB11 runs: with the
 * cursor_to of an absolute row, a YES on row r jumped the cursor r + 1 rows), +0x24 the
 * cursor row, +0x54 tick a row (0x40072a98), +0x58 untick it (0x40072944); the ticks are a
 * vector of rows at +0x28/+0x2c */
#define VCALL(off) ((*(unsigned **)view)[(off) / 4])
#define V_WIDTH 0x88
/* stock's confirm window and message box, as SAMPLES' LOAD TO PROJ and DELETE (0x400ea096,
 * 0x400e9d04) */
#define CONFIRM_NEW ((void (*)(void *, unsigned, void *, void *, void *))0x4006e478)
#define CONFIRM_SIZE 0x7c
#define CONFIRM_SP ((void (*)(void *, void *))0x401a650c)   /* (&ctrl, window) */
#define BOX_NEW ((void (*)(void *, void *))0x40076754)      /* (box, &string) */
#define BOX_SIZE 0xa0
#define BOX_SP ((void (*)(void *, void *))0x401832ec)       /* (&shared_ptr, box) */
#define SHOW ((void (*)(void *, void *, int))0x40079462)    /* (*(view + 0x64), &sp, 0) */
#define FONT_LIST 0x4027aa28u            /* the browser's font */
/* stock's text width in pixels (font, -1, &string), as its TEXT (0x4007154c) measures */
#define TEXT_W ((int (*)(unsigned, int, void *))0x40071bf2)
#define LABEL_X 8                        /* the list's label starts this far into a row */
#define STATE_GAP 3                      /* pixels kept free before the state column */

#include "clip.inc"

static int text_w(const char *s)
{
    void *str;
    int w;
    STR_NEW(&str, s, 0);
    w = TEXT_W(FONT_LIST, -1, &str);
    STR_DEL(&str);
    return w;
}

struct fn {                              /* std::function: functor[2], manager, invoker */
    void *functor[2];
    unsigned mgr;
    void *inv;
};

enum { M_DRIVE, M_BUILD };
enum { P_NONE, P_LOAD, P_UNLOAD, P_DELETE, P_CLEAR, P_RMDIR };
static u8 mode;                          /* the list: the files, or the build */
static u8 pending;                       /* what the open confirm window does */
static void *view;                       /* the page's view while it shows (UI task only) */
static char msg[80];
static char rmdir_name[DL_SEG_MAX + 1];  /* the folder DELETE DIR asked about */

static void page_rows(void);

/* the saved build is empty: the action reads LOAD TO BUILD, else UPDATE BUILD (decided 2026-11-07) */
static int build_empty(void)
{
    int i;
    for (i = 0; i < nfiles; i++)
        if (files[i].on)
            return 0;
    return 1;
}

static int list_n(void)
{
    return mode == M_BUILD ? nbuild : nlrows;
}

/* the file of list row i in this mode, or -1 for ".." and a folder */
static int file_k(int i)
{
    if (mode == M_BUILD)
        return build_rows[i];
    return lrows[i].kind == R_FILE ? lrows[i].f : -1;
}

/* the build list: the saved set, then the mods the last boot loaded that have left it
 * (DROP), which stay listed until the next boot */
static void build_list(void)
{
    int i;
    nbuild = 0;
    for (i = 0; i < nfiles && nbuild < fcap; i++)
        if (files[i].on)
            build_rows[nbuild++] = i;
    for (i = 0; i < nfiles && nbuild < fcap; i++)
        if (!files[i].on && files[i].boot == DL_ST_LOADED)
            build_rows[nbuild++] = i;
}

static int cursor_row(void)
{
    return ((int (*)(void *))VCALL(0x24))(view);
}

/* the open folder's ticked files */
static int picks_here(void)
{
    int i, n = 0;
    for (i = 0; i < nfiles; i++)
        n += files[i].pick && files[i].here;
    return n;
}

/* DELETE acts on the folder under the cursor (DELETE DIR, as SAMPLES offers on a folder) when
 * none of the open folder's files is ticked */
static int delete_dir_now(void)
{
    int row;
    if (mode != M_DRIVE || picks_here())
        return 0;
    row = cursor_row();
    return row >= 0 && row < nlrows && lrows[row].kind == R_DIR;
}

static const char *const acts_drive[] = {"SELECT ALL", "DESELECT ALL", "LOAD TO BUILD", "CLEAR BUILD", "DELETE"};
static const char *const acts_build[] = {"SELECT ALL", "DESELECT ALL", "UNLOAD"};
#define NACT_DRIVE 5
#define NACT_BUILD 3
/* functor indices: list rows are 1..list_n(), and the list has no file limit (one row a file in
 * /mods), so the other rows sit far above any list row. At 100/200/300 the 256-mod emulator
 * record drew list row 100 as SELECT ALL, rows from 200 as NO MODS ON +DRIVE and rows from 300
 * as VIEW BUILD (2026-11-08). A row needs a heap entry, so no list reaches 0x10000000. */
#define I_ACT 0x10000000                 /* functor index of the action panel's rows */
#define I_EMPTY 0x20000000               /* the file list's one row when /mods holds no file */
#define I_LEFT 0x30000000                /* and of the left panel's row */

/* a row's label: list rows 1.., the action panel I_ACT.., the left panel I_LEFT. The file list
 * shows a folder as "NAME/" and a file by its id (its file name until one is known); the build
 * list shows each mod's path below /mods (decided 2026-11-08), so two files of one name in two
 * folders tell apart. */
unsigned dl_page_text(int i)
{
    char *end = label + sizeof label;
    label[0] = 0;
    if (i == I_EMPTY)
        cat(label, end, "NO MODS ON +DRIVE");
    else if (i >= I_LEFT)
        cat(label, end, mode == M_DRIVE ? "VIEW BUILD" : "VIEW +DRIVE");
    else if (i >= I_ACT) {
        i -= I_ACT;
        if (mode == M_DRIVE && i < NACT_DRIVE)
            cat(label, end, i == 2 && !build_empty() ? "UPDATE BUILD"
                            : i == 4 && view && delete_dir_now() ? "DELETE DIR" : acts_drive[i]);
        else if (mode == M_BUILD && i < NACT_BUILD)
            cat(label, end, acts_build[i]);
    } else if (--i >= 0 && i < list_n()) {
        int k = file_k(i);
        if (k >= 0) {
            const struct file *r = &files[k];
            if (mode == M_BUILD && view)     /* the path, clipped left of the state column */
                clip_tail(label, end, r->name, ((int (*)(void *))VCALL(V_WIDTH))(view) - 9 -
                          text_w("KEEP") - LABEL_X - STATE_GAP, text_w);
            else
                cat(label, end, mode == M_BUILD ? r->name : r->id[0] ? r->id : base_of(r->name));
        } else if (lrows[i].kind == R_UP)
            cat(label, end, "..");
        else
            cat(cat(label, end, lrows[i].name), end, "/");
    }
    return (unsigned)label;
}

/* The checked count (decided 2026-11-08, the 256-mod run: a plan proves on screen that the
 * background check is done before its first key). The file list's top row shows "n/N" in its
 * right-hand column in place of its size: n the open folder's files read and checked, N its
 * files; n = N once the background has checked every one. The top row is the one with the largest y
 * (y = 0 the bottom row), learnt from the frame before: ev_draw closes each frame. The count is
 * drawn with the row, so an open panel or a box covers it as it covers the rows (drawn after
 * the frame, on ev_draw, it covered the action panel's SELECT ALL: the 256-mod emulator record,
 * 2026-11-08). */
static u8 rows_drawn, top_known;         /* a row was drawn this frame; top_y is the last frame's */
static int frame_top, top_y;

static void row_seen(int y)
{
    if (!rows_drawn || y > frame_top)
        frame_top = y;
    rows_drawn = 1;
}

void dl_page_overlay(void *bmp, void *ctrl)
{
    (void)bmp;
    (void)ctrl;
    top_known = rows_drawn;
    if (rows_drawn)
        top_y = frame_top;
    rows_drawn = 0;
}

static char *checked_count(char *t, char *end)
{
    int i, n = 0, all = 0;
    for (i = 0; i < nlrows; i++)
        if (lrows[i].kind == R_FILE) {
            all++;
            n += files[lrows[i].f].checked;
        }
    return num(cat(num(t, end, (u32)n), end, "/"), end, (u32)all);
}

/* the right-hand column, where SAMPLES puts sizes (0x400e5f94: x + width - 9, right-aligned).
 * Files: whole KB rounded up, "!" for a file pass 1 refuses (a file over the 512 KB staging
 * buffer included). Folders: "n SEL", the ticked files below it, when there are any. Build: KEEP
 * (loaded now, stays), ADD (loads at the next boot), DROP (loaded now, gone after the next boot),
 * "!" (the last boot refused it). */
void dl_page_draw(void **f, void *item, void *canvas, int x, int y)
{
    int i = (int)*f - 1, k;
    const struct file *r;
    char t[16], *end = t + sizeof t;
    (void)item;
    drawn_at = ticks;
    if (!view || i < 0 || i >= list_n())
        return;
    row_seen(y);
    k = file_k(i);
    t[0] = 0;
    if (k < 0) {
        if (lrows[i].kind == R_DIR && lrows[i].sel)
            cat(num(t, end, lrows[i].sel), end, " SEL");
    } else if (r = &files[k], mode == M_DRIVE) {
        if (r->file_code)
            cat(t, end, "!");
        else
            cat(num(t, end, (r->size + 1023) / 1024), end, "KB");
    } else if (r->boot == DL_ST_REFUSED || r->boot == DL_ST_DROPPED)
        cat(t, end, "!");
    else if (!r->on)
        cat(t, end, "DROP");
    else
        cat(t, end, r->boot == DL_ST_LOADED ? "KEEP" : "ADD");
    if (mode == M_DRIVE && top_known && y == top_y) {
        t[0] = 0;
        checked_count(t, end);
        TEXT(canvas, FONT_LIST, x + ((int (*)(void *))VCALL(V_WIDTH))(view) - 9, y, 2, 0, "XXXXXXXXX", "%s", t);
        return;
    }
    TEXT(canvas, FONT_LIST, x + ((int (*)(void *))VCALL(V_WIDTH))(view) - 9, y, 2, 0, "XXXXXXX", "%s", t);
}

static void show_box(const char *s)
{
    void *str, *box;
    unsigned sp[2] = {0, 0};
    STR_NEW(&str, s, 0);
    box = NEW(BOX_SIZE);
    BOX_NEW(box, &str);
    BOX_SP(sp, box);
    SHOW(*(void **)((char *)view + 0x64), sp, 0);
    if (sp[1])
        SP_RELEASE(&sp[1]);
    STR_DEL(&str);
}

void dl_page_yes(void **f, int yes);

static void confirm(const char *line1)
{
    void *a, *b, *win;
    unsigned sp[2] = {0, 0};
    struct fn cb = {{0, 0}, FN_MGR, (void *)dl_page_yes};
    STR_NEW(&a, line1, 0);
    STR_NEW(&b, "CONFIRM Y/N", 0);
    win = NEW(CONFIRM_SIZE);
    CONFIRM_NEW(win, FONT_LIST, &a, &b, &cb);
    sp[0] = (unsigned)win;
    CONFIRM_SP(&sp[1], win);
    SHOW(*(void **)((char *)view + 0x64), sp, 0);
    if (sp[1])
        SP_RELEASE(&sp[1]);
    STR_DEL(&b);
    STR_DEL(&a);
}

static int is_ticked(int row)
{
    int *v = ((int **)view)[10], *e = ((int **)view)[11];
    for (; v && v < e; v++)
        if (*v == row)
            return 1;
    return 0;
}

static void tick(int row, int on)
{
    if (on && !is_ticked(row))
        ((void (*)(void *, int))VCALL(0x54))(view, row);
    else if (!on && is_ticked(row))
        ((void (*)(void *, int))VCALL(0x58))(view, row);
}

/* The ticked files, as indices into files -> how many. The file list: every ticked file, in every
 * folder (here: only the open folder's). The build list: its ticked rows (stock's vector; none
 * means none: 0x4007265e would give the cursor row). */
static int ticked(int *out, int here)
{
    int n = 0, i;
    if (mode == M_DRIVE) {
        for (i = 0; i < nfiles && n < fcap; i++)
            if (files[i].pick && (!here || files[i].here))
                out[n++] = i;
        return n;
    }
    {
        int *v = ((int **)view)[10], *e = ((int **)view)[11];
        for (; v && v < e && n < fcap; v++)
            if (*v >= 0 && *v < nbuild)
                out[n++] = build_rows[*v];
    }
    return n;
}

static void cursor_to(int row)
{
    /* never on a row the list does not have: stock's view hangs the UI on a cursor in an empty
     * list (no files on the +Drive; found 2026-11-07 on DLP7). Stock's own lists are never empty
     * (a folder always has "..", VIEW RAM always its 128 slots), so the MODS page alone needs this. */
    int now;
    if (row < 0 || row >= list_n())
        return;
    now = cursor_row();
    if (row != now)
        ((void (*)(void *, int))VCALL(0x18))(view, row - now);
}

/* "n MODS <what>" into msg */
static const char *count_msg(int n, const char *what)
{
    char *end = msg + sizeof msg;
    cat(cat(num(msg, end, (u32)n), end, n == 1 ? " MOD " : " MODS "), end, what);
    return msg;
}

/* ---- folders ---- */

/* Open a folder of the open one (name), or go up (name 0) with the cursor on the folder just
 * left, as SAMPLES (0x400e92f8). -> 0, or -2 no memory (still in the old folder). */
static int go(const char *name)
{
    char old[DL_NAME_MAX + 1], left[DL_SEG_MAX + 1];
    int i, d = depth, r;
    copy_str(old, cwd, sizeof old);
    left[0] = 0;
    if (name) {
        if (!join(cwd, old, name))
            return -2;
        depth++;
    } else {
        for (i = (int)dl_strlen(cwd); i > 0 && cwd[i - 1] != '/'; i--)
            ;
        copy_str(left, cwd + i, sizeof left);
        cwd[i > 0 ? i - 1 : 0] = 0;
        depth--;
    }
    r = read_folder();
    if (r) {
        copy_str(cwd, old, sizeof cwd);
        depth = d;
        return r;
    }
    page_rows();
    for (i = 0; left[0] && i < nlrows; i++)
        if (lrows[i].kind == R_DIR && !dl_strcmp(lrows[i].name, left)) {
            cursor_to(i);
            break;
        }
    return 0;
}

/* A new build: the boot linker's pass 1, checks and layout on it (dl_check in steps, reads
 * only), one file a tick, so the key path and USB stay free between files (decided 2026-11-08:
 * the CB11 unit run's LOAD of 18 files of 482 KB held them for minutes in one call). A set
 * where every mod would load is written to DL_DIR/active, one that wouldn't isn't, and the box
 * says why. While a save runs the page takes no other action, changes no folder and checks
 * nothing else. */
enum { J_NONE, J_LOAD, J_CLEAR, J_UNLOAD };
enum { JP_FILES, JP_START, JP_STEPS, JP_END };
static struct {
    u8 what, phase, first;
    int m, i, k, n;                      /* set size, next line or file, unloaded, ticked */
    u32 tries;                           /* ticks the buffer was busy, in a row */
} job;
#define JOB_BUSY 600                     /* ticks (30 s) of a busy buffer before the save gives up */

static void job_end(const char *box)
{
    job.what = J_NONE;
    set_free();
    show_box(box);
}

/* the save done: the ticks are the build again, the rows, the lists and the box, as each
 * action says */
static void job_saved(void)
{
    u8 what = job.what;
    int i;
    for (i = 0; i < nfiles; i++)
        files[i].pick = files[i].on;
    count_sel();
    build_list();
    page_rows();
    if (what == J_LOAD)
        cursor_to(0);                    /* as stock after LOAD TO PROJ */
    job_end(what == J_LOAD ? (job.first ? count_msg(job.n, "LOADED") : "BUILD UPDATED")
            : what == J_CLEAR ? "BUILD CLEARED" : count_msg(job.k, "UNLOADED"));
}

/* one step of the save: a file read (its CRC, or its pass 1), or the start, or the end */
static void save_step(void)
{
    int i, ok, k;
    if (!job.what)
        return;
    if (job.phase == JP_FILES) {         /* the kept files the background hasn't read yet */
        while (job.i < nfiles && (!files[job.i].keep || files[job.i].checked))
            job.i++;
        if (job.i < nfiles) {
            if (inspect(&files[job.i])) {
                if (++job.tries > JOB_BUSY)
                    job_end("BUSY: TRY AGAIN");
                return;
            }
            if (files[job.i].file_code == DL_R_MISSING) {   /* gone since it was ticked (deleted over USB):
                                                             * left out, as a file no folder lists */
                files[job.i].keep = files[job.i].pick = 0;
                job.m--;
            }
            job.tries = 0;
            job.i++;
            return;
        }
        job.phase = JP_START;
    }
    if (job.phase == JP_START) {
        u32 s = (u32)(job.m > 0 ? job.m : 1);
        choice = dl_alloc(s * sizeof *choice);
        cst = dl_alloc(s * sizeof *cst);
        cdet = dl_alloc(s * sizeof *cdet);
        if (!choice || !cst || !cdet) {
            job_end("NO MEMORY");
            return;
        }
        for (i = 0, k = 0; i < nfiles && k < job.m; i++)
            if (files[i].keep) {
                copy_str(choice[k].name, files[i].name, DL_LINE_NAME_MAX);
                choice[k++].crc = files[i].crc;
            }
        if (dl_check_start((u32)core_desc, job.m, drive_staging, DL_STAGING_SIZE, cst, cdet, 0) < 0) {
            job_end("NOT SAVED");
            return;
        }
        job.i = 0;
        job.phase = JP_STEPS;
        return;
    }
    if (job.phase == JP_STEPS && job.i < job.m) {
        if (!dl_staging_take_idle(ticks, READ_STALL)) {
            if (++job.tries > JOB_BUSY) {
                dl_check_cancel();
                job_end("BUSY: TRY AGAIN");
            }
            return;
        }
        job.tries = 0;
        dl_lock();
        dl_check_step(&choice[job.i++]);
        dl_unlock();
        dl_staging_give();
        return;
    }
    ok = dl_check_finish();
    if (ok != job.m) {
        msg[0] = 0;
        for (k = 0; k < job.m && ok >= 0; k++)
            if (cst[k].state != DL_ST_LOADED) {
                reason(msg, msg + sizeof msg, cst[k].code ? cst[k].code : DL_R_LIMIT, cst[k].arg, cdet[k]);
                break;
            }
        job_end(msg[0] ? msg : "NOT SAVED");
        return;
    }
    for (i = 0; i < nfiles; i++) {
        files[i].was = files[i].on;
        files[i].on = files[i].keep;
    }
    if (save_active()) {
        for (i = 0; i < nfiles; i++)
            files[i].on = files[i].was;
        job_end("SAVE FAILED");
        return;
    }
    job_saved();
}

/* Start a save of files[].keep (what: J_*). -> 0 started, -1 not (a box says why). */
static int save_set(u8 what, int n, int k)
{
    int i, m = 0;
    if (job.what) {
        show_box("BUSY: TRY AGAIN");
        return -1;
    }
    for (i = 0; i < nfiles; i++)
        m += files[i].keep != 0;
    if (m > DL_MAX_MODS) {               /* the boot links at most DL_MAX_MODS lines */
        show_box("OVER 256 MODS");
        return -1;
    }
    dl_memset(&job, 0, sizeof job);
    job.what = what;
    job.first = (u8)build_empty();
    job.m = m;
    job.n = n;
    job.k = k;
    job.phase = JP_FILES;
    if (m) {                             /* "CHECKING 18 MODS": the box the result replaces */
        char *end = msg + sizeof msg;
        cat(num(cat(msg, end, "CHECKING "), end, (u32)m), end, m == 1 ? " MOD" : " MODS");
        show_box(msg);
    } else {
        while (job.what)                 /* an empty set: no file to read, done at once */
            save_step();
    }
    return 0;
}

static void keep_none(void)
{
    int i;
    for (i = 0; i < nfiles; i++)
        files[i].keep = 0;
}

/* LOAD TO BUILD / UPDATE BUILD: the ticked files, in every folder, are the whole build (decided
 * 2026-11-07, 2026-11-08) */
static void do_load(void)
{
    int n = ticked(sel, 0), i;
    keep_none();
    for (i = 0; i < n; i++)
        files[sel[i]].keep = 1;
    save_set(J_LOAD, n, 0);
}

/* CLEAR BUILD: an empty build (LOAD TO BUILD with nothing ticked does nothing; decided 2026-11-07) */
static void do_clear(void)
{
    keep_none();
    save_set(J_CLEAR, 0, 0);
}

/* UNLOAD in the build list: the build without the ticked mods */
static void do_unload(void)
{
    int n = ticked(sel, 0), i, k = 0;
    for (i = 0; i < nfiles; i++)
        files[i].keep = files[i].on;
    for (i = 0; i < n; i++)
        if (files[sel[i]].keep) {
            files[sel[i]].keep = 0;
            k++;
        }
    if (!k) {
        show_box("MOD NOT LOADED");
        return;
    }
    save_set(J_UNLOAD, n, k);
}

/* DELETE: the open folder's ticked files, none of them in the saved build (as SAMPLES, whose
 * DELETE takes the ticked files of the folder it shows) */
static void do_delete(void)
{
    char path[DL_PATH_MAX];
    int n = ticked(sel, 1), i, k, done = 0;
    for (i = 0; i < n; i++)
        if (files[sel[i]].on) {
            show_box("MOD IN USE");          /* unload it first */
            return;
        }
    if (!dl_staging_take_idle(ticks, READ_STALL)) {   /* no read of a file open */
        show_box("BUSY: TRY AGAIN");
        return;
    }
    dl_lock();
    for (i = 0; i < n; i++) {
        path_of(path, files[sel[i]].name);
        if (!dl_fs_delete(path)) {
            done++;
            files[sel[i]].pick = 0;      /* gone: read_folder drops it */
        }
    }
    dl_unlock();
    dl_staging_give();
    k = read_folder();
    if (k) {
        show_box("NO MEMORY");
        return;
    }
    build_list();
    page_rows();
    cursor_to(0);
    show_box(done == n ? count_msg(done, "DELETED") : "DELETE FAILED");
}

/* DELETE DIR: an empty folder only, as SAMPLES (0x400e77b2, stock's rmdir: "ERROR (NOT EMPTY?)"
 * otherwise; stock never deletes a folder's files for it) */
static void do_rmdir(void)
{
    char p[DL_NAME_MAX + 1], path[DL_PATH_MAX];
    int r, i;
    if (!join(p, cwd, rmdir_name))
        return;
    path_of(path, p);
    dl_lock();
    r = dl_fs_rmdir(path);
    dl_unlock();
    if (r) {
        show_box("ERROR (NOT EMPTY?)");
        return;
    }
    if (read_folder()) {
        show_box("NO MEMORY");
        return;
    }
    page_rows();
    for (i = 0; i < nlrows && lrows[i].kind != R_FILE; i++)
        ;
    cursor_to(i < nlrows ? i - 1 : nlrows - 1);   /* about where the folder was */
}

void dl_page_yes(void **f, int yes)
{
    (void)f;
    if ((yes & 0xff) && view) {          /* the action panel stays open, as stock's */
        if (pending == P_LOAD)
            do_load();
        else if (pending == P_UNLOAD)
            do_unload();
        else if (pending == P_DELETE)
            do_delete();
        else if (pending == P_CLEAR)
            do_clear();
        else if (pending == P_RMDIR)
            do_rmdir();
    }
    pending = P_NONE;
}

static void ask(const char *verb, int what)
{
    char *end = msg + sizeof msg, *at;
    int n = ticked(sel, what == P_DELETE);
    if (!n) {
        show_box("NO MODS SELECTED");
        return;
    }
    if (what == P_LOAD && !build_empty()) {   /* "UPDATE BUILD? (2 MODS)" */
        at = num(cat(msg, end, "UPDATE BUILD? ("), end, (u32)n);
        cat(at, end, n == 1 ? " MOD)" : " MODS)");
    } else {                             /* "LOAD 2 MODS?", as stock's "LOAD 2 SAMPLES?" */
        at = cat(cat(msg, end, verb), end, " ");
        at = num(at, end, (u32)n);
        cat(at, end, n == 1 ? " MOD?" : " MODS?");
    }
    pending = (u8)what;
    confirm(msg);
}

/* A "!" row's whole refusal into out (as dl_page_draw marks it): in the file list the file's
 * own (pass 1), in the build list the last boot's, after "BOOT: ". -> 0 for a row without one. */
static int row_reason(const struct file *r, char *out, char *end)
{
    out[0] = 0;
    if (mode == M_DRIVE) {
        if (!r->file_code)
            return 0;
        reason(out, end, r->file_code, 0, r->detail);
        return 1;
    }
    if (r->boot != DL_ST_REFUSED && r->boot != DL_ST_DROPPED)
        return 0;
    reason_line(out, cat(out, end, "BOOT: "), end, r->boot_code ? r->boot_code : DL_R_LIMIT, r->boot_arg,
                r->boot_detail);
    return 1;
}

/* a file row's tick, the page's and the view's; -> 0 when the build is full (nothing changed) */
static int set_pick(int row, int on)
{
    int k = file_k(row);
    if (k < 0)
        return 1;
    if (mode == M_DRIVE) {
        if (on && !files[k].pick && npicks() >= DL_MAX_MODS)
            return 0;
        files[k].pick = (u8)on;
    }
    tick(row, on);
    return 1;
}

/* YES on a list row, as in SAMPLES: ".." goes up, a folder opens, a file is ticked or unticked
 * and the cursor steps down. Ticking a "!" row also shows why it has one (decided 2026-11-07: the
 * whole reason, reachable from the page). */
static void row_yes(void)
{
    static char why[80];
    int row = cursor_row(), on, k;
    if (row < 0 || row >= list_n())
        return;
    if (mode == M_DRIVE && lrows[row].kind != R_FILE) {
        if (job.what) {                  /* a save holds the files where they are */
            show_box("BUSY: TRY AGAIN");
            return;
        }
        if (go(lrows[row].kind == R_UP ? 0 : lrows[row].name))
            show_box("NO MEMORY");
        return;
    }
    k = file_k(row);
    on = mode == M_DRIVE ? !files[k].pick : !is_ticked(row);   /* no read here: a tick is never held
                                          * up by a check (CB11 record runs: YES presses during a
                                          * check were lost) */
    if (!set_pick(row, on)) {
        show_box("OVER 256 MODS");
        return;
    }
    count_sel();
    if (row + 1 < list_n())
        cursor_to(row + 1);
    VIEW_DIRTY((char *)view + 0x38);
    if (on && row_reason(&files[k], why, why + sizeof why))
        show_box(why);
}

void dl_page_select(void **f)
{
    int i = (int)*f, r;
    if (!view)
        return;
    if (job.what && i >= I_ACT) {         /* a save is running: no other action until it ends */
        show_box("BUSY: TRY AGAIN");
        return;
    }
    if (i < I_ACT) {
        row_yes();
        return;
    }
    if (i >= I_LEFT) {                   /* as SAMPLES' VIEW RAM (0x400e9062): switch, rebuild, back to the list */
        mode = mode == M_DRIVE ? M_BUILD : M_DRIVE;
        page_rows();                     /* back from BUILD: the file list's ticks, kept in files */
        FOCUS(view, view);
        return;
    }
    i -= I_ACT;
    if (i == 0 || i == 1) {              /* SELECT ALL, DESELECT ALL: the open folder's files, as
                                          * SAMPLES' (0x400e6620: files only, no folder, no depth) */
        for (r = 0; r < list_n(); r++)
            if (!set_pick(r, i == 0)) {
                show_box("OVER 256 MODS");
                break;
            }
        count_sel();
        VIEW_DIRTY((char *)view + 0x38);
    } else if (i == 2)
        ask(mode == M_DRIVE ? "LOAD" : "UNLOAD", mode == M_DRIVE ? P_LOAD : P_UNLOAD);
    else if (i == 3 && mode == M_DRIVE) {
        pending = P_CLEAR;
        confirm("CLEAR THE BUILD?");
    } else if (i == 4 && mode == M_DRIVE) {
        if (delete_dir_now()) {          /* as SAMPLES: "DELETE DIR?" over CONFIRM Y/N */
            copy_str(rmdir_name, lrows[cursor_row()].name, sizeof rmdir_name);
            pending = P_RMDIR;
            confirm("DELETE DIR?");
        } else
            ask("DELETE", P_DELETE);
    }
}

static int nothing(void)
{
    return 0;
}

static void add_item(void *list, int i, int index)
{
    struct fn l = {{(void *)i, 0}, FN_MGR, (void *)dl_page_label};
    struct fn s = {{(void *)i, 0}, FN_MGR, (void *)dl_page_select};
    struct fn d = {{(void *)i, 0}, i < I_ACT ? FN_MGR : 0, i < I_ACT ? (void *)dl_page_draw : 0};
    struct fn c = {{0, 0}, FN_MGR, (void *)nothing};
    void *it = NEW(0x54);
    ITEM_CTOR(it, &l, &s, &d, &c, index, 8);
    MENU_ADD(list, it);
}

/* (Re)build the list and both panels. In the file list the ticks are the page's own (files[].pick),
 * which start as the saved build on entering MODS and after a change (decided 2026-11-07) and stay
 * across folders (decided 2026-11-08). */
static void page_rows(void)
{
    int i;
    void *right = (char *)view + PANE_RIGHT, *left = (char *)view + PANE_LEFT;
    LIST_CLEAR(view);
    ((int *)view)[11] = ((int *)view)[10];   /* no ticks carried across lists */
    for (i = 0; i < list_n(); i++)
        add_item(view, i + 1, i);
    /* stock's file view hangs the UI when it is first shown with no row (DLP7, 2026-11-07); its
     * own lists never are (a folder has "..", VIEW RAM its slots). So a file list with no row
     * gets one that says so and does nothing (YES on it changes nothing: it is no file). Only
     * DL_DIR itself can be empty: a folder below it has "..". */
    if (mode == M_DRIVE && !nlrows)
        add_item(view, I_EMPTY, 0);
    LIST_CLEAR(right);
    for (i = 0; i < (mode == M_BUILD ? NACT_BUILD : NACT_DRIVE); i++)
        add_item(right, I_ACT + i, -1);
    LIST_CLEAR(left);
    add_item(left, I_LEFT, -1);
    if (mode == M_DRIVE) {
        for (i = 0; i < nlrows; i++)
            if (lrows[i].kind == R_FILE && files[lrows[i].f].pick)
                tick(i, 1);
        cursor_to(0);
    }
    VIEW_DIRTY((char *)view + 0x38);
}

/* The MODS row's select: a new page each time, on DL_DIR itself. The GLOBAL view keeps one child
 * in a single shared_ptr slot (+0x84) that every push replaces, and the view stack holds its own
 * reference while the page shows, so the page and its rows are freed once it is closed and
 * another child is pushed or GLOBAL itself goes. */
void dl_mods_open(void **f)
{
    static const char *const skip[] = {
        "", "SKIPPED: [NO] HELD", "SKIPPED: CRASH", "NO +DRIVE", "OLD DESCRIPTOR",
        "SET UNSTABLE", "OVER 256 LINES", "NO MEMORY"};
    void *menu = *f, *p;
    unsigned sp[2] = {0, 0};
    int busy;
    if (job.what) {                      /* a save left running when its page closed */
        if (job.phase == JP_STEPS)
            dl_check_cancel();
        job.what = J_NONE;
    }
    view = 0;                            /* the last page (not showing: GLOBAL is) goes */
    if (held[1])
        SP_RELEASE(&held[1]);
    held[0] = held[1] = 0;
    busy = scan();
    build_list();
    mode = M_DRIVE;
    pending = P_NONE;
    p = NEW(PANE_SIZE);
    /* left panel on (+0x508), action panel on (+0x509); with no file and no folder both stay
     * shut, as there is nothing to act on or view (decided 2026-11-07) */
    PANE_CTOR(p, "ModsManager", 0x80, nlrows != 0, nlrows != 0, 0);
    view = p;
    page_rows();
    dl_call_a0(VIEW_SP, sp, menu, (unsigned)((char *)p + 0x38));
    PUSH(menu, sp);
    held[0] = sp[0];                     /* kept, not released: the background may mark the view */
    held[1] = sp[1];                     /* dirty after the page closes; it goes at the next opening */
    if (busy)
        show_box("NO MEMORY");
    else if (dl_skip_reason && dl_skip_reason < sizeof skip / sizeof skip[0])
        show_box(skip[dl_skip_reason]);
}

static const void *const mods_row[4] = {
    (const void *)dl_mods_label, (const void *)dl_mods_open,
    (const void *)dl_mods_icon, (const void *)nothing,
};

/* ev_key, ev_enc: note the time, take nothing (the page's checks wait QUIET ticks after it) */
int dl_key(void *brain, void *ev)
{
    (void)brain, (void)ev;
    last_input = ticks;
    return 0;
}

int dl_enc(void *brain, void *ev)
{
    (void)brain, (void)ev;
    last_input = ticks;
    return 0;
}

/* ev_settings: GLOBAL's rows are built; MODS goes last */
void dl_settings(void *menu)
{
    core_additem(menu, mods_row);
}

/* from dl_tick: a frame after a change */
unsigned dl_page_now(void)
{
    return ticks;
}

/* The open folder's files are read and checked one at a time in the background (inspect), one
 * folder at a time as the user opens it (a scan of every folder at once would hold the UI task:
 * remote keys expire after 2 s), one every CHECK_EVERY ticks, only while the page's rows are
 * being drawn (within SHOWN ticks of a draw: the page shows); each check marks the view dirty, so
 * its rows are drawn again and the next check follows. (A frame request through ctrl + 0x20 alone
 * doesn't draw the rows: CB11 record run 2026-11-07 22:21, the background stopped after one file.)
 * The view is ours to mark: the page holds its own reference to it (held) until the next MODS
 * opening. A ticked file in another folder is read when a save needs it. */
#define CHECK_EVERY 2
#define SHOWN 10
#define QUIET 20                         /* ticks (1 s) after a key or a turn with no check: a file's
                                          * check holds the UI task, and a key sent meanwhile can
                                          * expire (CB11 unit run 2, 2026-11-08) */

static void check_next(void)
{
    static u32 probed;
    if (!view || !lrows || job.what || ticks % CHECK_EVERY || ticks - last_input < QUIET)
        return;
    while (next_check < nlrows && (lrows[next_check].kind != R_FILE || files[lrows[next_check].f].checked))
        next_check++;
    if (next_check >= nlrows)
        return;
    if (ticks - drawn_at > SHOWN) {      /* not drawn lately: ask for a frame; a page that shows */
        if (ticks - probed > SHOWN) {    /* draws its rows (drawn_at), one that doesn't stays still */
            probed = ticks;
            VIEW_DIRTY((char *)view + 0x38);
        }
        return;
    }
    if (!inspect(&files[lrows[next_check].f]))   /* its row now shows its id or "!"; the frame that */
        VIEW_DIRTY((char *)view + 0x38);          /* draws it keeps drawn_at fresh, so the next follows */
}

/* The route's changes over USB (route.c): dl_fs_gen counts them, and a move leaves its FROM and TO
 * until the page takes them (dl_move_seen). */
extern volatile unsigned dl_fs_gen;
extern volatile int dl_move_seen;
extern char dl_move_from[], dl_move_to[];
static unsigned seen_gen;

/* `p` (DL_NAME_MAX + 1 bytes) after a move of `from` to `to`: renamed when it is from or below it */
static void follow_move(char *p, const char *from, const char *to)
{
    char t[DL_NAME_MAX + 1];
    u32 k = dl_strlen(from), n = dl_strlen(p);
    if ((n == k && !dl_strcmp(p, from)) || below(p, from)) {
        if (dl_strlen(to) + n - k > DL_NAME_MAX)
            return;
        copy_str(t, p + k, sizeof t);
        copy_str(p, to, DL_NAME_MAX + 1);
        copy_str(p + dl_strlen(to), t, DL_NAME_MAX + 1 - dl_strlen(to));
    }
}

/* DL_DIR "/" rel is a folder: its parent lists it as one. Under dl_lock. */
static int folder_there(const char *rel)
{
    char parent[DL_NAME_MAX + 1];
    const char *b = base_of(rel);
    struct dl_ent *ents;
    int n, i, r = 0;
    copy_str(parent, rel, (u32)(b - rel) > 0 ? (u32)(b - rel) : 1);
    n = dl_list_dir(parent, 0, 0);
    ents = dl_alloc((u32)(n > 0 ? n : 1) * sizeof *ents);
    if (!ents)
        return 1;                        /* no memory: keep the folder */
    n = dl_list_dir(parent, ents, n);
    for (i = 0; i < n && !r; i++)
        r = ents[i].dir && !dl_strcmp(ents[i].name, b);
    dl_free(ents);
    return r;
}

/* After a change over USB: the page's files and its open folder follow a move, the open folder
 * goes up while it is gone (a move or DELETE DIR over USB), and the folder is read again, as after
 * the page's own changes. Not while a save runs: a move waits for it (the route answers "Busy"). */
static void follow_route(void)
{
    unsigned g;
    int i;
    if (job.what || (seen_gen == dl_fs_gen && dl_move_seen))
        return;
    dl_lock();
    if (!dl_move_seen) {
        for (i = 0; i < nfiles; i++)
            follow_move(files[i].name, dl_move_from, dl_move_to);
        follow_move(cwd, dl_move_from, dl_move_to);
        dl_move_seen = 1;
    }
    g = dl_fs_gen;
    if (lrows)
        while (cwd[0] && !folder_there(cwd)) {
            i = (int)(base_of(cwd) - cwd);
            cwd[i > 0 ? i - 1 : 0] = 0;
        }
    dl_unlock();
    seen_gen = g;
    if (!lrows)                          /* the page hasn't been opened */
        return;
    for (depth = 0, i = 0; cwd[0] && cwd[i]; i++)
        depth += cwd[i] == '/';
    depth += cwd[0] != 0;
    if (read_folder())
        return;
    build_list();
    if (view) {
        page_rows();
        cursor_to(0);
    }
    redraw = 1;
}

void dl_page_tick(void *ctrl)
{
    ticks++;
    dl_readers_poll(ticks);
    follow_route();
    check_next();
    if (job.what && !(ticks % CHECK_EVERY) && ticks - last_input >= QUIET)
        save_step();
    if (redraw) {
        redraw = 0;
        *((volatile u8 *)ctrl + 0x20) = 1;
    }
}
