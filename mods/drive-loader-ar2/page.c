/* SPDX-License-Identifier: GPL-2.0-or-later */
/* drive-loader (Analog Rytm MKII, OS 1.74): the MODS page (README, "The MODS page").
 *
 * A MODS row at the bottom of SETTINGS > GLOBAL (ev_settings, core_additem) opens a stock
 * VerticalSettingsMenuView listing the .elemod files in DL_DIR, one row each, with a tick.
 * [YES] on a row toggles it. Every change runs the boot linker's own pass 1, checks and
 * layout on the new set (dl_check, dlink.c) against core_desc: a set that would load whole
 * is written to DL_DIR/active for the next boot, one that wouldn't is never saved and the
 * refused row says why. The page never loads or unloads code.
 *
 * Everything here runs in the UI task. The route runs in the SysEx tasks: the page takes
 * the staging buffer only while it reads files, at the page's opening and at each change,
 * and not while a read of a file is open (dl_staging_take_idle), since stock streams a read
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
#define SETTINGS_VIEW ((void (*)(void *, const char *, int, void *, void *, int, int))0x400780e8)
#define SETTINGS_VIEW_SIZE 0x194
#define VIEW_SP 0x4007762cu              /* (parent, view + 0x38) -> %a0: shared_ptr<View> */
#define PUSH ((void (*)(void *, void *))0x40077a16)   /* (parent, &shared_ptr): show it */
#define SP_RELEASE ((void (*)(void *))0x40170bd8)     /* (&ctrl) */
#define STR_NEW ((void (*)(void *, const char *, void *))0x401bcff0)
#define STR_DEL ((void (*)(void *))0x401bb540)
/* printf-style text: (canvas, font, x, y, align 0 left, box 0 none, clear-as-wide-as, fmt, ...) */
#define TEXT ((void (*)(void *, unsigned, int, int, int, int, const char *, const char *, ...))0x4007154c)
#define FONT 0x40279e24u

extern char core_desc[];
extern void core_additem(void *menu, const void *row);
extern void dl_mods_label(void), dl_row_label(void), dl_mods_icon(void);   /* page.s */
extern void dl_call_a0(unsigned fn, void *res, void *a, unsigned b);   /* route.s */
int dl_list_files(char (*names)[DL_NAME_MAX + 1], int max);           /* route.c */

#define ROWS DL_MAX_MODS
#define READ_STALL 600                   /* ev_ticks (30 s) a read may stand still and count */
#define LABEL_FITS 12                    /* characters left of the value column */
#define ACTIVE_MAX (DL_MAX_MODS * (DL_LINE_NAME_MAX + 12) + 64)

struct row {
    char name[DL_NAME_MAX + 1];          /* the file in DL_DIR */
    u32 crc, size;
    u8 on;                               /* ticked: in the saved set */
    u8 file_code;                        /* a refusal of the file itself (pass 1), or 0 */
    u8 set_code;                         /* the last check's refusal of this row, or 0 */
    u8 boot;                             /* its boot record: DL_ST_*, or DL_ST_NONE */
    u8 boot_code;
    u16 set_arg;
    char id[DL_ID_MAX];
    char ver[DL_VER_MAX];
    char detail[32];                     /* the refusal's name argument */
};

static struct row rows[ROWS];
static int nrows;
static char names[ROWS][DL_NAME_MAX + 1];
static struct dl_choice choice[DL_MAX_MODS];
static int choice_row[DL_MAX_MODS];
static struct dl_status cst[DL_MAX_MODS];
static char cdet[DL_MAX_MODS][32];
static char cver[DL_MAX_MODS][DL_VER_MAX];
static char text[ACTIVE_MAX];
static char label[48], value[8];

enum { H_BOOT, H_BUSY, H_SAVED, H_NOT_SAVED, H_SAVE_FAILED, H_EMPTY };
static u8 head;                          /* what the first row says */
static u8 redraw;                        /* ask the next ev_tick for a frame */
static volatile u32 ticks, drawn_at;     /* the page's rows were drawn at this tick */

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

/* a refusal, as short as the row's width allows (the codes: dl.h) */
static void reason(char *at, char *end, int code, u32 arg, const char *d)
{
    static const char *const what[] = {
        "", "FILE MISSING", "FILE CHANGED", "TOO BIG", "BAD FILE", "NOT FORMAT 2",
        "OTHER DEVICE/OS", "CAN'T USE", "SAME ID AS", "NEEDS CORE", "NEEDS", "CONFLICTS",
        "EXPORT TAKEN", "CLAIMED", "NOT FOUND", "TABLE", "FULL", "NO ROOM", "OUT OF RANGE",
        "TOO MANY NAMES", "OVER 32 MODS"};
    at = cat(at, end, code < (int)(sizeof what / sizeof what[0]) ? what[code] : "REFUSED");
    if (code == DL_R_ROOM) {
        at = cat(at, end, " +");
        at = num(at, end, arg);
        cat(at, end, "K");
    } else if (d && d[0]) {
        cat(cat(at, end, " "), end, d);
    }
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
    char *end = text + sizeof text, *at = text, tmp[DL_PATH_MAX], path[DL_PATH_MAX];
    dl_file f;
    int i, r;
    u32 n;
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
    return r;
}

/* the set of ticked rows as choices, row `flip` toggled -> how many */
static int chosen(int flip)
{
    int i, n = 0;
    for (i = 0; i < nrows; i++)
        if ((rows[i].on != 0) != (i == flip) && n < DL_MAX_MODS) {
            copy_str(choice[n].name, rows[i].name, DL_LINE_NAME_MAX);
            choice[n].crc = rows[i].crc;
            choice_row[n++] = i;
        }
    return n;
}

static int check(int n)
{
    int r;
    dl_lock();                           /* every file read in the check */
    r = dl_check((u32)core_desc, choice, n, drive_staging, DL_STAGING_SIZE, cst, cdet, cver);
    dl_unlock();
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
            if (dl_status[i].state != DL_ST_LOADED && !r->detail[0])
                copy_str(r->detail, dl_detail[i], sizeof r->detail);
            return;
        }
}

/* Read DL_DIR: each .elemod's size, CRC-32, id and version and what pass 1 says of it,
 * and the ticks from DL_DIR/active. Holds the staging buffer. -> 0, or -1 busy. */
static int scan(void)
{
    int i, k, n, m;
    s32 len;
    if (!dl_staging_take_idle(ticks, READ_STALL))
        return -1;
    dl_memset(rows, 0, sizeof rows);
    nrows = 0;
    dl_lock();
    n = dl_list_files(names, ROWS);
    for (i = 0; i < n && nrows < ROWS; i++) {
        struct row *r = &rows[nrows];
        if (!ends_elemod(names[i]))
            continue;
        copy_str(r->name, names[i], sizeof r->name);
        len = dl_read_file(r->name, drive_staging, DL_STAGING_SIZE);
        if (len == -2)
            r->file_code = DL_R_TOO_BIG;
        else if (len < 0)
            r->file_code = DL_R_MISSING;
        else {
            r->size = (u32)len;
            r->crc = dl_crc32(drive_staging, (u32)len);
        }
        nrows++;
    }
    len = dl_read_file("active", (u8 *)text, sizeof text - 1);
    dl_unlock();

    /* the ticks: a line whose file is here; a changed file stays ticked (its new CRC goes
     * into the next save), and the boot record says what the boot made of it */
    if (len > 0) {
        m = dl_parse_active(text, (u32)len, choice);
        for (k = 0; k < m; k++)
            for (i = 0; i < nrows; i++)
                if (!dl_strcmp(choice[k].name, rows[i].name))
                    rows[i].on = 1;
    }

    /* every file's id, version and pass-1 verdict: one check of them all, whose set
     * refusals mean nothing here (only codes about the file itself are kept) */
    for (i = 0, m = 0; i < nrows; i++)
        if (!rows[i].file_code) {
            copy_str(choice[m].name, rows[i].name, DL_LINE_NAME_MAX);
            choice[m].crc = rows[i].crc;
            choice_row[m++] = i;
        }
    if (check(m) >= 0)
        for (k = 0; k < m; k++) {
            struct row *r = &rows[choice_row[k]];
            int c = cst[k].code;
            copy_str(r->id, cst[k].id, sizeof r->id);
            copy_str(r->ver, cver[k], sizeof r->ver);
            if (cst[k].state != DL_ST_LOADED && c && (c <= DL_R_FEATURE || c == DL_R_CORE)) {
                r->file_code = (u8)c;
                copy_str(r->detail, cdet[k], sizeof r->detail);
            }
        }
    dl_staging_give();
    for (i = 0; i < nrows; i++)
        boot_record(&rows[i]);
    return 0;
}

/* ---- the rows' callbacks (std::function invokers: the functor's address first; its
 * first word is the row's index, or the GLOBAL menu for the MODS row) ---- */

static void say(char *at, char *end, const struct row *r)
{
    if (r->set_code)
        reason(at, end, r->set_code, r->set_arg, r->detail);
    else if (r->file_code)
        reason(at, end, r->file_code, 0, r->detail);
    else if (r->on && r->boot == DL_ST_LOADED)
        cat(at, end, "LOADED");
    else if (r->on && (r->boot == DL_ST_REFUSED || r->boot == DL_ST_DROPPED))
        reason(cat(at, end, "BOOT: "), end, r->boot_code, 0, r->detail);
    else if (r->on)
        cat(at, end, "ON NEXT BOOT");
    else if (r->boot == DL_ST_LOADED)
        cat(at, end, "OFF NEXT BOOT");
    else {
        at = cat(at, end, r->ver[0] ? r->ver : "?");
        at = cat(at, end, " ");
        at = num(at, end, (r->size + 1023) / 1024);
        cat(at, end, "K");
    }
}

/* the label of row `i` (0 the header): a mod row shows its id (its file's name until
 * one is known) and, every other second and a half, what there is to say of it */
unsigned dl_row_text(int i)
{
    char *end = label + sizeof label, *at = label;
    static const char *const skip[] = {
        "", "SKIPPED: [NO] HELD", "SKIPPED: CRASH", "NO +DRIVE", "OLD DESCRIPTOR",
        "SET UNSTABLE", "OVER 32 LINES"};
    label[0] = 0;
    if (i == 0) {
        switch (head) {
        case H_BUSY: cat(at, end, "BUSY: TRY AGAIN"); break;
        case H_SAVED: cat(at, end, "SAVED: REBOOT"); break;
        case H_NOT_SAVED: cat(at, end, "NOT SAVED"); break;
        case H_SAVE_FAILED: cat(at, end, "SAVE FAILED"); break;
        case H_EMPTY: cat(at, end, "NO MODS ON +DRIVE"); break;
        default:
            if (dl_skip_reason && dl_skip_reason < sizeof skip / sizeof skip[0])
                cat(at, end, skip[dl_skip_reason]);
            else {
                u32 k, n = 0;
                for (k = 0; k < dl_status_n && k < DL_MAX_MODS; k++)
                    n += dl_status[k].state == DL_ST_LOADED;
                cat(num(at, end, n), end, " LOADED");
            }
        }
        return (unsigned)label;
    }
    if (i < 1 || i > nrows)
        return (unsigned)label;
    if ((ticks / 30) & 1)
        say(at, end, &rows[i - 1]);
    else
        cat(at, end, rows[i - 1].id[0] ? rows[i - 1].id : rows[i - 1].name);
    return (unsigned)label;
}

/* draw: the tick in the value column (x is that column's left edge) */
void dl_row_draw(void **f, void *item, void *canvas, int x, int y)
{
    int i = (int)*f;
    (void)item;
    drawn_at = ticks;
    if (i < 1 || i > nrows)
        return;
    if (dl_strlen((const char *)dl_row_text(i)) > LABEL_FITS)
        return;                          /* a long reason takes the row's whole width */
    copy_str(value, rows[i - 1].on ? "[X]" : "[ ]", sizeof value);
    TEXT(canvas, FONT, x, y + 2, 0, 0, "[X]", "%s", value);
}

/* select ([YES]): toggle a row, check the new set, save it when it would load whole */
void dl_row_select(void **f)
{
    int i = (int)*f - 1, n, ok, k;
    struct row *r;
    if (i < 0 || i >= nrows)
        return;
    r = &rows[i];
    redraw = 1;
    if (r->file_code && !r->on) {
        head = H_NOT_SAVED;              /* a file pass 1 refuses can't be ticked */
        return;
    }
    n = chosen(i);
    if (!dl_staging_take_idle(ticks, READ_STALL)) {
        head = H_BUSY;
        return;
    }
    ok = check(n);
    dl_staging_give();
    for (k = 0; k < nrows; k++)
        rows[k].set_code = 0;
    if (ok < 0) {
        head = H_NOT_SAVED;
        return;
    }
    for (k = 0; k < n; k++) {
        struct row *c = &rows[choice_row[k]];
        if (cst[k].state != DL_ST_LOADED) {
            c->set_code = cst[k].code ? cst[k].code : DL_R_LIMIT;
            c->set_arg = cst[k].arg;
            copy_str(c->detail, cdet[k], sizeof c->detail);
        }
    }
    if (ok != n) {                       /* a set that can't link is never saved */
        head = H_NOT_SAVED;
        return;
    }
    r->on = !r->on;
    head = save_active() ? H_SAVE_FAILED : H_SAVED;
    if (head == H_SAVE_FAILED)
        r->on = !r->on;
}

static int nothing(void)
{
    return 0;
}

struct fn {                              /* std::function: functor[2], manager, invoker */
    void *functor[2];
    unsigned mgr;
    void *inv;
};

static void add_row(void *menu, int i)
{
    struct fn l = {{(void *)i, 0}, FN_MGR, (void *)dl_row_label};
    struct fn s = {{(void *)i, 0}, FN_MGR, (void *)dl_row_select};
    struct fn d = {{(void *)i, 0}, i ? FN_MGR : 0, i ? (void *)dl_row_draw : 0};
    struct fn c = {{0, 0}, FN_MGR, (void *)nothing};
    void *it = NEW(0x54);
    ITEM_CTOR(it, &l, &s, &d, &c, -1, 8);
    MENU_ADD(menu, it);
}

/* The MODS row's select: a new page each time. The GLOBAL view keeps one child in a
 * single shared_ptr slot (+0x84) that every push replaces, and the view stack holds its
 * own reference while the page shows, so the page and its rows are freed once it is
 * closed and another child is pushed or GLOBAL itself goes. */
void dl_mods_open(void **f)
{
    void *menu = *f, *p, *title;
    unsigned sp[2] = {0, 0};
    int i;
    head = scan() ? H_BUSY : H_BOOT;
    if (head == H_BOOT && !nrows)
        head = H_EMPTY;
    p = NEW(SETTINGS_VIEW_SIZE);
    STR_NEW(&title, "MODS", 0);
    /* (this, name, rows to reserve, &title, icon, height, width) */
    SETTINGS_VIEW(p, "ModsMenuView", nrows + 1, &title, 0, 0x40, 0x80);
    STR_DEL(&title);
    for (i = 0; i <= nrows; i++)
        add_row(p, i);
    dl_call_a0(VIEW_SP, sp, menu, (unsigned)((char *)p + 0x38));
    PUSH(menu, sp);
    if (sp[1])
        SP_RELEASE(&sp[1]);
}

static const void *const mods_row[4] = {
    (const void *)dl_mods_label, (const void *)dl_mods_open,
    (const void *)dl_mods_icon, (const void *)nothing,
};

/* ev_settings: GLOBAL's rows are built; MODS goes last */
void dl_settings(void *menu)
{
    core_additem(menu, mods_row);
}

/* from dl_tick: a frame now and then while the page shows, so the labels alternate,
 * and one after a change */
unsigned dl_page_now(void)
{
    return ticks;
}

void dl_page_tick(void *ctrl)
{
    ticks++;
    dl_readers_poll(ticks);
    if (redraw || (ticks - drawn_at < 40 && ticks % 30 == 0)) {
        redraw = 0;
        *((volatile u8 *)ctrl + 0x20) = 1;
    }
}
