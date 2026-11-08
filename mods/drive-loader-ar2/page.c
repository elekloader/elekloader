/* SPDX-License-Identifier: GPL-2.0-or-later */
/* drive-loader (Analog Rytm MKII, OS 1.74): the MODS page (README, "The MODS page").
 *
 * A MODS row at the bottom of SETTINGS > GLOBAL (ev_settings, core_additem) opens stock's
 * two-pane file view, the one SAMPLES uses, on the .elemod files in DL_DIR. It works like
 * SAMPLES (decided 2026-11-07): [YES] ticks a file, RIGHT opens the action panel, and LOAD TO
 * BUILD saves exactly the ticked files as the build. LEFT opens VIEW BUILD / VIEW +DRIVE,
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
int dl_list_files(char (*names)[DL_NAME_MAX + 1], int max);           /* route.c */

#define READ_STALL 600                   /* ev_ticks (30 s) a read may stand still and count */
#define LINE_MAX (DL_NAME_MAX + 10)      /* an /mods/active line: name, space, 8 digits, \n */

struct row {
    char name[DL_NAME_MAX + 1];          /* the file in DL_DIR */
    u32 crc, size;
    u8 on;                               /* ticked: in the saved set */
    u8 file_code;                        /* a refusal of the file itself (pass 1), or 0 */
    u8 set_code;                         /* the last check's refusal of this row, or 0 */
    u8 boot;                             /* its boot record: DL_ST_*, or DL_ST_NONE */
    u8 boot_code;
    u16 set_arg;
    u16 boot_arg;                        /* the boot record's number (NO ROOM's KB, FULL's spare) */
    char id[DL_ID_MAX];
    char ver[DL_VER_MAX];
    char detail[32];                     /* the file's refusal's name argument */
    char boot_detail[32];                /* the boot record's */
    u8 checked;                          /* read and checked (crc, id, ver, file_code known) */
};

/* The list grows like SAMPLES' (decided 2026-11-07): every array the page keeps per file
 * comes from the heap when the page reads DL_DIR, sized to the folder (cap), and goes back
 * at the next read. A set (choice .. cver) is a subset of the files, and at most
 * DL_MAX_MODS. */
static struct row *rows;
static int nrows, cap;
static struct dl_choice *choice;
static int *choice_row;
static struct dl_status *cst;
static char (*cdet)[32];
static char (*cver)[DL_VER_MAX];
static u8 *drive_ticks;                  /* the file list's ticks while the build list shows */
static int *build_rows, nbuild;
static int *sel;
static u8 *keep_now, *was;               /* a new set's files, and the old one's */
static char label[48];

static void page_free(void)
{
    void **p[] = {(void **)&rows, (void **)&choice, (void **)&choice_row, (void **)&cst,
                  (void **)&cdet, (void **)&cver, (void **)&drive_ticks, (void **)&build_rows,
                  (void **)&sel, (void **)&keep_now, (void **)&was};
    u32 i;
    for (i = 0; i < sizeof p / sizeof p[0]; i++) {
        dl_free(*p[i]);
        *p[i] = 0;
    }
    nrows = nbuild = cap = 0;
}

/* The page's arrays for n files. -> 0 when the heap has no room (nothing kept). */
static int page_alloc(int n)
{
    u32 k = (u32)(n > 0 ? n : 1), s = k < DL_MAX_MODS ? k : DL_MAX_MODS;
    page_free();
    rows = dl_alloc(k * sizeof *rows);
    choice = dl_alloc(s * sizeof *choice);
    choice_row = dl_alloc(s * sizeof *choice_row);
    cst = dl_alloc(s * sizeof *cst);
    cdet = dl_alloc(s * sizeof *cdet);
    cver = dl_alloc(s * sizeof *cver);
    drive_ticks = dl_alloc(k);
    build_rows = dl_alloc(k * sizeof *build_rows);
    sel = dl_alloc(k * sizeof *sel);
    keep_now = dl_alloc(k);
    was = dl_alloc(k);
    if (!rows || !choice || !choice_row || !cst || !cdet || !cver || !drive_ticks || !build_rows
        || !sel || !keep_now || !was) {
        page_free();
        return 0;
    }
    cap = (int)k;
    return 1;
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

/* DL_DIR/active, from rows[].on. -> 0, or < 0 when it wasn't written. */
static int save_active(void)
{
    char *text, *end, *at, tmp[DL_PATH_MAX], path[DL_PATH_MAX];
    dl_file f;
    int i, r, on = 0;
    u32 n;
    for (i = 0; i < nrows; i++)
        on += rows[i].on;
    n = 64 + (u32)on * LINE_MAX;
    text = dl_alloc(n);
    if (!text)
        return -1;
    end = text + n;
    at = text;
    at = cat(at, end, "# chosen on the unit's MODS page\n");
    for (i = 0; i < nrows; i++)
        if (rows[i].on) {
            at = cat(at, end, rows[i].name);
            at = cat(at, end, " ");
            at = hex8(at, end, rows[i].crc);
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

/* the boot linker's record of a row: the line of the boot's set that named its file */
static void boot_record(struct row *r)
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

/* Read DL_DIR's listing: each .elemod's name and size, and the ticks from DL_DIR/active, without
 * reading any mod (decided 2026-11-07: opening the page with many large files held the key
 * path and USB for seconds, T178). Each file is read later, one at a time (inspect). The
 * page's arrays are taken anew for the folder's count. -> 0, -2 no memory. */
static int scan(void)
{
    int i, k, n, m;
    s32 len, size;
    char (*names)[DL_NAME_MAX + 1], *text = 0, path[DL_PATH_MAX];
    struct dl_choice *ticks_set = 0;
    dl_lock();
    n = dl_list_files(0, 0);
    names = page_alloc(n) ? dl_alloc((u32)(n > 0 ? n : 1) * sizeof *names) : 0;
    if (!names) {
        dl_unlock();
        page_free();
        return -2;
    }
    n = dl_list_files(names, n);
    for (i = 0; i < n && nrows < cap; i++) {
        struct row *r = &rows[nrows];
        if (!ends_elemod(names[i]))
            continue;
        copy_str(r->name, names[i], sizeof r->name);
        path_of(path, r->name);
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
        nrows++;
    }
    dl_free(names);
    path_of(path, "active");
    size = dl_fs_file_size(path);
    len = size > 0 && (text = dl_alloc((u32)size + 1)) != 0
          ? dl_read_file("active", (u8 *)text, (u32)size) : 0;
    dl_unlock();

    /* the ticks: a line whose file is here; a changed file stays ticked (its new CRC goes
     * into the next save), and the boot record says what the boot made of it */
    m = len > 0 ? dl_parse_active(text, (u32)len, 0) : 0;
    if (m > 0 && (ticks_set = dl_alloc((u32)m * sizeof *ticks_set)) != 0) {
        dl_parse_active(text, (u32)len, ticks_set);
        for (k = 0; k < m; k++)
            for (i = 0; i < nrows; i++)
                if (!dl_strcmp(ticks_set[k].name, rows[i].name))
                    rows[i].on = 1;
    }
    dl_free(ticks_set);
    dl_free(text);
    for (i = 0; i < nrows; i++)
        boot_record(&rows[i]);
    next_check = 0;
    return 0;
}

/* One file read: its CRC-32 and size, then a check of it alone for its id, version and
 * pass-1 verdict (a check's set refusals mean nothing here: only codes about the file itself
 * are kept), so no number of files reaches a set's limits. Holds the staging buffer and the
 * loader's mutex for this one file only. -> 0 (the row is checked), -1 busy. */
static int inspect(struct row *r)
{
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
        copy_str(choice[0].name, r->name, DL_LINE_NAME_MAX);
        choice[0].crc = r->crc;
        if (dl_check_buf((u32)core_desc, &choice[0], len, drive_staging, DL_STAGING_SIZE, cst, cdet,
                         cver) >= 0) {           /* the file just read, not read again */
            c = cst[0].code;
            copy_str(r->id, cst[0].id, sizeof r->id);
            copy_str(r->ver, cver[0], sizeof r->ver);
            if (cst[0].state != DL_ST_LOADED && c && (c <= DL_R_FEATURE || c == DL_R_CORE)) {
                r->file_code = (u8)c;
                copy_str(r->detail, cdet[0], sizeof r->detail);
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

struct fn {                              /* std::function: functor[2], manager, invoker */
    void *functor[2];
    unsigned mgr;
    void *inv;
};

enum { M_DRIVE, M_BUILD };
enum { P_NONE, P_LOAD, P_UNLOAD, P_DELETE, P_CLEAR };
static u8 mode;                          /* the list: the files, or the build */
static u8 pending;                       /* what the open confirm window does */
static void *view;                       /* the page's view while it shows (UI task only) */
static char msg[80];

static void page_rows(void);

/* the saved build is empty: the action reads LOAD TO BUILD, else UPDATE BUILD (decided 2026-11-07) */
static int build_empty(void)
{
    int i;
    for (i = 0; i < nrows; i++)
        if (rows[i].on)
            return 0;
    return 1;
}

static int list_n(void)
{
    return mode == M_BUILD ? nbuild : nrows;
}

static int file_k(int i)                 /* the file of list row i in this mode */
{
    return mode == M_BUILD ? build_rows[i] : i;
}

/* the build list: the saved set, then the mods the last boot loaded that have left it
 * (DROP), which stay listed until the next boot */
static void build_list(void)
{
    int i;
    nbuild = 0;
    for (i = 0; i < nrows && nbuild < cap; i++)
        if (rows[i].on)
            build_rows[nbuild++] = i;
    for (i = 0; i < nrows && nbuild < cap; i++)
        if (!rows[i].on && rows[i].boot == DL_ST_LOADED)
            build_rows[nbuild++] = i;
}

static const char *const acts_drive[] = {"SELECT ALL", "DESELECT ALL", "LOAD TO BUILD", "CLEAR BUILD", "DELETE"};
static const char *const acts_build[] = {"SELECT ALL", "DESELECT ALL", "UNLOAD"};
#define NACT_DRIVE 5
#define NACT_BUILD 3
#define I_ACT 100                        /* functor index of the action panel's rows */
#define I_LEFT 300                       /* and of the left panel's row */
#define I_EMPTY 200                      /* the file list's one row when /mods holds no file */

/* a row's label: list rows 1.., the action panel I_ACT.., the left panel I_LEFT */
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
            cat(label, end, i == 2 && !build_empty() ? "UPDATE BUILD" : acts_drive[i]);
        else if (mode == M_BUILD && i < NACT_BUILD)
            cat(label, end, acts_build[i]);
    } else if (--i >= 0 && i < list_n()) {
        const struct row *r = &rows[file_k(i)];
        cat(label, end, r->id[0] ? r->id : r->name);
    }
    return (unsigned)label;
}

/* the right-hand column, where SAMPLES puts sizes (0x400e5f94: x + width - 9, right-aligned).
 * Files: whole KB rounded up, "!" for a file pass 1 refuses (a file over the 512 KB staging
 * buffer included). Build: KEEP (loaded now, stays), ADD (loads at the next boot), DROP
 * (loaded now, gone after the next boot), "!" (the last boot refused it). */
void dl_page_draw(void **f, void *item, void *canvas, int x, int y)
{
    int i = (int)*f - 1;
    const struct row *r;
    char t[16], *end = t + sizeof t;
    (void)item;
    drawn_at = ticks;
    if (!view || i < 0 || i >= list_n())
        return;
    r = &rows[file_k(i)];
    t[0] = 0;
    if (mode == M_DRIVE) {
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
    TEXT(canvas, FONT_LIST, x + ((int (*)(void *))VCALL(V_WIDTH))(view) - 9, y, 2, 0, "XXXXMB", "%s", t);
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

/* the ticked rows, as files -> how many (none means none: 0x4007265e would give the cursor row) */
static int ticked(int *out)
{
    int *v = ((int **)view)[10], *e = ((int **)view)[11], n = 0;
    for (; v && v < e && n < cap; v++)
        if (*v >= 0 && *v < list_n())
            out[n++] = file_k(*v);
    return n;
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

static void cursor_to(int row)
{
    /* never on a row the list does not have: stock's view hangs the UI on a cursor in an empty
     * list (no files on the +Drive; found 2026-11-07 on DLP7). Stock's own lists are never empty
     * (a folder always has "..", VIEW RAM always its 128 slots), so the MODS page alone needs this. */
    int now;
    if (row < 0 || row >= list_n())
        return;
    now = ((int (*)(void *))VCALL(0x24))(view);
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

/* A new build: the boot linker's pass 1, checks and layout on it (dl_check in steps, reads
 * only), one file a tick, so the key path and USB stay free between files (decided 2026-11-08:
 * the CB11 unit run's LOAD of 18 files of 482 KB held them for minutes in one call). A set
 * where every mod would load is written to DL_DIR/active, one that wouldn't isn't, and the box
 * says why. While a save runs the page takes no other action and checks nothing else. */
enum { J_NONE, J_LOAD, J_CLEAR, J_UNLOAD };
enum { JP_FILES, JP_START, JP_STEPS, JP_END };
static struct {
    u8 what, phase, first;
    int m, i, k, n;                      /* set size, next line or row, unloaded, ticked */
    u32 tries;                           /* ticks the buffer was busy, in a row */
} job;
#define JOB_BUSY 600                     /* ticks (30 s) of a busy buffer before the save gives up */

static void job_end(const char *box)
{
    job.what = J_NONE;
    show_box(box);
}

/* the save done: the rows, the lists and the box, as each action says */
static void job_saved(void)
{
    u8 what = job.what;
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
    if (job.phase == JP_FILES) {         /* the kept rows the background hasn't read yet */
        while (job.i < nrows && (!keep_now[job.i] || rows[job.i].checked))
            job.i++;
        if (job.i < nrows) {
            if (inspect(&rows[job.i])) {
                if (++job.tries > JOB_BUSY)
                    job_end("BUSY: TRY AGAIN");
                return;
            }
            job.tries = 0;
            job.i++;
            return;
        }
        job.phase = JP_START;
    }
    if (job.phase == JP_START) {
        for (i = 0, job.m = 0; i < nrows; i++)
            if (keep_now[i]) {
                copy_str(choice[job.m].name, rows[i].name, DL_LINE_NAME_MAX);
                choice[job.m].crc = rows[i].crc;
                choice_row[job.m++] = i;
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
    for (i = 0; i < nrows; i++) {
        was[i] = rows[i].on;
        rows[i].on = keep_now[i];
    }
    if (save_active()) {
        for (i = 0; i < nrows; i++)
            rows[i].on = was[i];
        job_end("SAVE FAILED");
        return;
    }
    job_saved();
}

/* Start a save of keep_now (what: J_*). -> 0 started, -1 not (a box says why). */
static int save_set(u8 what, int n, int k)
{
    int i, m = 0;
    if (job.what) {
        show_box("BUSY: TRY AGAIN");
        return -1;
    }
    for (i = 0; i < nrows; i++)
        m += keep_now[i] != 0;
    if (m > DL_MAX_MODS) {               /* the boot links at most DL_MAX_MODS lines */
        show_box("OVER 256 MODS");
        return -1;
    }
    dl_memset(&job, 0, sizeof job);
    job.what = what;
    job.first = (u8)build_empty();
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

/* LOAD TO BUILD / UPDATE BUILD: the ticked files are the whole build (decided 2026-11-07) */
static void do_load(void)
{
    int n = ticked(sel), i;
    dl_memset(keep_now, 0, (u32)cap);
    for (i = 0; i < n; i++)
        keep_now[sel[i]] = 1;
    save_set(J_LOAD, n, 0);
}

/* CLEAR BUILD: an empty build (LOAD TO BUILD with nothing ticked does nothing; decided 2026-11-07) */
static void do_clear(void)
{
    dl_memset(keep_now, 0, (u32)cap);
    save_set(J_CLEAR, 0, 0);
}

/* UNLOAD in the build list: the build without the ticked mods */
static void do_unload(void)
{
    int n = ticked(sel), i, k = 0;
    for (i = 0; i < nrows; i++)
        keep_now[i] = rows[i].on;
    for (i = 0; i < n; i++)
        if (keep_now[sel[i]]) {
            keep_now[sel[i]] = 0;
            k++;
        }
    if (!k) {
        show_box("MOD NOT LOADED");
        return;
    }
    save_set(J_UNLOAD, n, k);
}

/* DELETE: the ticked files, none of them in the saved build */
static void do_delete(void)
{
    char path[DL_PATH_MAX];
    int n = ticked(sel), i, k, done = 0;
    for (i = 0; i < n; i++)
        if (rows[sel[i]].on) {
            show_box("MOD IN USE");          /* unload it first */
            return;
        }
    if (!dl_staging_take_idle(ticks, READ_STALL)) {   /* no read of a file open */
        show_box("BUSY: TRY AGAIN");
        return;
    }
    dl_lock();
    for (i = 0; i < n; i++) {
        path_of(path, rows[sel[i]].name);
        done += !dl_fs_delete(path);
    }
    dl_unlock();
    dl_staging_give();
    k = scan();
    if (k) {
        show_box(k == -2 ? "NO MEMORY" : "BUSY: TRY AGAIN");
        return;
    }
    build_list();
    page_rows();
    cursor_to(0);
    show_box(done == n ? count_msg(done, "DELETED") : "DELETE FAILED");
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
    }
    pending = P_NONE;
}

static void ask(const char *verb, int what)
{
    char *end = msg + sizeof msg, *at;
    int n = ticked(sel);
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

/* YES on a list row, as in SAMPLES: tick or untick it, and step down */
/* A "!" row's whole refusal into out (as dl_page_draw marks it): in the file list the file's
 * own (pass 1), in the build list the last boot's, after "BOOT: ". -> 0 for a row without one. */
static int row_reason(const struct row *r, char *out, char *end)
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

/* YES on a list row, as in SAMPLES: tick or untick it, and step down. Ticking a "!" row also
 * shows why it has one (decided 2026-11-07: the whole reason, reachable from the page). */
static void toggle_advance(void)
{
    static char why[80];
    int row = ((int (*)(void *))VCALL(0x24))(view), on;
    if (row < 0 || row >= list_n())
        return;
    on = !is_ticked(row);                /* no read here: a tick is never held up by a check (CB11
                                          * record runs: YES presses during a check were lost) */
    tick(row, on);
    if (row + 1 < list_n())
        cursor_to(row + 1);
    VIEW_DIRTY((char *)view + 0x38);
    if (on && row_reason(&rows[file_k(row)], why, why + sizeof why))
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
        toggle_advance();
        return;
    }
    if (i >= I_LEFT) {                   /* as SAMPLES' VIEW RAM (0x400e9062): switch, rebuild, back to the list */
        if (mode == M_DRIVE)             /* the file list's ticks wait for the way back */
            for (r = 0; r < nrows; r++)
                drive_ticks[r] = (u8)is_ticked(r);
        mode = mode == M_DRIVE ? M_BUILD : M_DRIVE;
        page_rows();
        if (mode == M_DRIVE)             /* back from BUILD: the ticks as they were, not the build */
            for (r = 0; r < nrows; r++)
                tick(r, drive_ticks[r]);
        FOCUS(view, view);
        return;
    }
    i -= I_ACT;
    if (i == 0 || i == 1) {              /* SELECT ALL, DESELECT ALL */
        for (r = 0; r < list_n(); r++)
            tick(r, i == 0);
        VIEW_DIRTY((char *)view + 0x38);
    } else if (i == 2)
        ask(mode == M_DRIVE ? "LOAD" : "UNLOAD", mode == M_DRIVE ? P_LOAD : P_UNLOAD);
    else if (i == 3 && mode == M_DRIVE) {
        pending = P_CLEAR;
        confirm("CLEAR THE BUILD?");
    } else if (i == 4 && mode == M_DRIVE)
        ask("DELETE", P_DELETE);
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

/* (Re)build the list and both panels. In the file list the ticks start as the saved build
 * (on entering MODS and after a change; decided 2026-11-07). */
static void page_rows(void)
{
    int i;
    void *right = (char *)view + PANE_RIGHT, *left = (char *)view + PANE_LEFT;
    LIST_CLEAR(view);
    ((int *)view)[11] = ((int *)view)[10];   /* no ticks carried across lists */
    for (i = 0; i < list_n(); i++)
        add_item(view, i + 1, i);
    /* stock's file view hangs the UI when it is first shown with no row (DLP7, 2026-11-07); its
     * own lists never are (a folder has "..", VIEW RAM its slots). So a file list with no file
     * gets one row that says so and does nothing (YES on it changes nothing: it is no file). */
    if (mode == M_DRIVE && !nrows)
        add_item(view, I_EMPTY, 0);
    LIST_CLEAR(right);
    for (i = 0; i < (mode == M_BUILD ? NACT_BUILD : NACT_DRIVE); i++)
        add_item(right, I_ACT + i, -1);
    LIST_CLEAR(left);
    add_item(left, I_LEFT, -1);
    if (mode == M_DRIVE) {
        for (i = 0; i < nrows; i++)
            tick(i, rows[i].on);
        cursor_to(0);
    }
    VIEW_DIRTY((char *)view + 0x38);
}

/* The MODS row's select: a new page each time. The GLOBAL view keeps one child in a single
 * shared_ptr slot (+0x84) that every push replaces, and the view stack holds its own
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
    if (busy)
        nrows = 0;
    build_list();
    mode = M_DRIVE;
    pending = P_NONE;
    p = NEW(PANE_SIZE);
    /* left panel on (+0x508), action panel on (+0x509); with no file both stay shut, as there
     * is nothing to act on or view (decided 2026-11-07) */
    PANE_CTOR(p, "ModsManager", 0x80, nrows != 0, nrows != 0, 0);
    view = p;
    page_rows();
    dl_call_a0(VIEW_SP, sp, menu, (unsigned)((char *)p + 0x38));
    PUSH(menu, sp);
    held[0] = sp[0];                     /* kept, not released: the background may mark the view */
    held[1] = sp[1];                     /* dirty after the page closes; it goes at the next opening */
    if (busy)
        show_box(busy == -2 ? "NO MEMORY" : "BUSY: TRY AGAIN");
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

/* The files the page lists are read and checked one at a time in the background (inspect),
 * one every CHECK_EVERY ticks, only while the page's rows are being drawn (within SHOWN ticks
 * of a draw: the page shows); each check marks the view dirty, so its rows are drawn again and
 * the next check follows. (A frame request through ctrl + 0x20 alone doesn't draw the rows:
 * CB11 record run 2026-11-07 22:21, the background stopped after one file.) The view is ours to
 * mark: the page holds its own reference to it (held) until the next MODS opening. */
#define CHECK_EVERY 2
#define SHOWN 10
#define QUIET 20                         /* ticks (1 s) after a key or a turn with no check: a file's
                                          * check holds the UI task, and a key sent meanwhile can
                                          * expire (CB11 unit run 2, 2026-11-08) */

static void check_next(void)
{
    static u32 probed;
    if (!view || !rows || job.what || ticks % CHECK_EVERY || ticks - last_input < QUIET)
        return;
    while (next_check < nrows && rows[next_check].checked)
        next_check++;
    if (next_check >= nrows)
        return;
    if (ticks - drawn_at > SHOWN) {      /* not drawn lately: ask for a frame; a page that shows */
        if (ticks - probed > SHOWN) {    /* draws its rows (drawn_at), one that doesn't stays still */
            probed = ticks;
            VIEW_DIRTY((char *)view + 0x38);
        }
        return;
    }
    if (!inspect(&rows[next_check]))     /* its row now shows its id or "!"; the frame that draws */
        VIEW_DIRTY((char *)view + 0x38);  /* it keeps drawn_at fresh, so the next check follows */
}

void dl_page_tick(void *ctrl)
{
    ticks++;
    dl_readers_poll(ticks);
    check_next();
    if (job.what && !(ticks % CHECK_EVERY) && ticks - last_input >= QUIET)
        save_step();
    if (redraw) {
        redraw = 0;
        *((volatile u8 *)ctrl + 0x20) = 1;
    }
}
