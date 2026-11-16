/* SPDX-License-Identifier: GPL-2.0-or-later */
/* drive-loader-a4mk2 (Analog Four MKII, OS 1.56): the MODS page (PAGE.md).
 *
 * A MODS row at the bottom of SETTINGS > GLOBAL (ev_settings, core_additem: page.s draws its
 * lightning-bolt icon as stock's GLOBAL rows draw theirs) opens a page on
 * the folders and .elemod files in DL_DIR, one folder at a time to DL_DEPTH deep. The A4 has no
 * SAMPLES file view to build on, so the page draws itself with core's std1 helpers (core_fill,
 * core_text) over the whole frame in ev_draw, and takes every key and encoder turn while it shows
 * (ev_key, ev_enc); stock's GLOBAL menu stays underneath and gets nothing until the page closes
 * (decided 2026-11-14: std1 helpers where they reach). It works as the Rytm's page does: [YES]
 * opens a folder or ticks a file, RIGHT opens the action panel, and LOAD TO BUILD saves exactly
 * the ticked files, from every folder, as the build. LEFT opens VIEW BUILD / VIEW +DRIVE, which
 * switches the list to the build. Every change runs the boot linker's own pass 1, checks and
 * layout on the new set (dl_check, dlink.c) against core_desc: a set that would load whole is
 * written to DL_DIR/active for the next boot, one that wouldn't is never saved and a box says why.
 * The page never loads or unloads code: APPLY & RESTART, in the build list while the saved set
 * differs from the running one, reboots the unit (core_restart) so the next boot loads it.
 *
 * Everything here runs in the UI task. The route runs in the SysEx tasks: the page takes the
 * staging buffer only while it reads one file (the background check of the listed files, one at
 * a time while the page shows, and the files of a change), and every store call is made under
 * dl_lock. The model (files, folders, ticks, the stepped save, FORGET DATA) is the Rytm page's
 * (drive-loader-ar2 page.c); only the view is new. No stock address is used here (DL_BUS_NOW reads
 * the ColdFire's DTIM0 count register). */

#include <std1.h>
#include <analog-four-mk2/screen.h>
#include <analog-four-mk2/system.h>
#include "dl.h"
#include "fs.h"
#include "skip.h"
#include "park.h"

extern char core_desc[];
extern void core_additem(void *menu, const void *row);              /* core-a4mk2 settings.s */
extern void dl_mods_label(void), dl_mods_icon(void);                /* page.s */

/* ---- the panel (analog-four-mk2/keys.h, screen.c) ---- */
#define K_LEFT 0x40
#define K_RIGHT 0x41
#define K_UP 0x42                        /* keys.h KEYID_UP_DOWN(0): which of the two is up is
                                          * [emu] and still to confirm (PAGE.md, "Keys") */
#define K_DOWN 0x43
#define K_YES 0x44
#define K_NO 0x45
#define EV_KEY 12                        /* KeyEvent +12 the key, +16 its flags (bit 0 down) */
#define EV_FLAGS 16
#define F_DOWN 0x01u
#define F_FUNC 0x02u
#define F_REPEAT 0x08u
#define EV_ENC 12                        /* EncoderEvent +12 the encoder (A..J 0..9, LEVEL/DATA 10,
                                          * 11 on an Analog Keys), +16 its delta as the Rytm's
                                          * (to confirm: PAGE.md, "Keys") */
#define EV_DELTA 16
#define ENC_DATA 10
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
    u8 big;                              /* larger than the live stage but not the +Drive area:
                                          * read for its size and CRC, and its pass 1 and layout
                                          * left to the next boot (STAGE.md) */
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

/* Parked data (the build list's PARK rows, as the Rytm's): the mods /mods/.store keeps fields of that no chosen mod
 * declares any more, read when the page opens and after FORGET DATA. The build list shows them
 * after the build's rows. */
static struct dl_park *parks;
static int npark;
static u8 *forgetting;                   /* FORGET DATA's marks, one a parked mod */
static u8 *store_raw, *store_out;        /* FORGET DATA's read of .store and its rewrite */
static s32 store_len;

/* The file being read, a stretch a tick (read_step): the staging buffer is the page's from its
 * open until the caller has checked what it read and given the buffer back. */
static struct dl_reader rd;
static const char *rd_name;              /* whose read: a file's or a save line's name */
static u8 reading;
static s32 rd_len;                       /* at a read's end: its bytes, or -1 missing, -2 too big, -3 */
static u32 rd_crc;

/* a read given up (its file, its line or the page gone): the buffer back */
static void read_stop(void)
{
    if (!reading)
        return;
    dl_lock();
    dl_read_close(&rd);
    dl_unlock();
    dl_staging_give();
    reading = 0;
}

static void set_free(void)
{
    read_stop();
    dl_free(choice);
    dl_free(cst);
    dl_free(cdet);
    dl_free(store_raw);
    dl_free(store_out);
    choice = 0;
    cst = 0;
    cdet = 0;
    store_raw = store_out = 0;
}

static void page_free(void)
{
    dl_free(files);
    dl_free(lrows);
    dl_free(build_rows);
    dl_free(sel);
    dl_free(parks);
    dl_free(forgetting);
    files = 0;
    lrows = 0;
    build_rows = sel = 0;
    parks = 0;
    forgetting = 0;
    nfiles = fcap = nlrows = nbuild = npark = 0;
    set_free();
}

static u8 redraw;                        /* ask the next ev_tick for a frame */
static volatile u32 ticks, drawn_at;     /* the page's rows were drawn at this tick */
static int next_check;                   /* the next row the background checks (check_next) */
static volatile u32 last_input;          /* the tick of the last key or encoder event */

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

/* A status record's id is the mod's own once pass 1 has read it; before that (a file missing,
 * too big or not JSON) dlink.c's begin_line leaves the first DL_ID_MAX - 1 bytes of the file's
 * path there. -> 1 when `id` is the mod's id. */
static int real_id(const char *id, const char *path)
{
    u32 i;
    if (!id[0])
        return 0;
    for (i = 0; i < DL_ID_MAX - 1 && path[i]; i++)
        if (id[i] != path[i])
            return 1;
    return id[i] != 0;
}

/* a file's name as the rows show it: without its folders and without ".elemod" (decided
 * 2026-11-09: "syntakt-art-0.1") */
static char *bare_name(char *at, char *end, const char *path)
{
    const char *b = base_of(path);
    u32 n = dl_strlen(b);
    if (ends_elemod(b))
        n -= 7;
    while (n-- && at < end - 1)
        *at++ = *b++;
    *at = 0;
    return at;
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

/* a refusal (the codes: dl.h). The page's message box splits its text at newlines and about 30
 * characters fit the screen, so a longer reason puts its detail on a line of its own (decided
 * 2026-11-07 for the Rytm: multiple lines are fine) */
#define BOX_LINE 30

/* the next word of `s` (to a space or its end) into [at, end) -> past it and its space */
static const char *word(const char *s, char *at, char *end)
{
    while (*s && *s != ' ' && at < end - 1)
        *at++ = *s++;
    *at = 0;
    while (*s && *s != ' ')
        s++;
    return *s ? s + 1 : s;
}

/* DL_R_STORE's second line from its detail "WHAT NEED HAVE" (decided 2026-11-10 with the
 * storage loader): "sound: 62 of 56 bits", "codes-synth: 3 of 1 free" (a track kind's lock
 * codes), "core_handles: 3 of 2 spare" */
static void store_line(char *at, char *end, const char *d)
{
    char what[32], need[12], have[12];
    int i, scope = 0, codes;
    d = word(word(word(d, what, what + sizeof what), need, need + sizeof need), have, have + sizeof have);
    codes = what[0] == 'c' && what[1] == 'o' && what[2] == 'd' && what[3] == 'e' && what[4] == 's';
    for (i = 0; i < DL_STORE_SCOPES; i++)
        scope |= !dl_strcmp(what, dl_store_scopes[i]);
    at = cat(cat(cat(cat(cat(cat(at, end, what), end, ": "), end, need), end, " of "), end, have), end,
             scope ? " bits" : codes ? " free" : " spare");
    (void)at;
}

/* DL_R_CLASH's text from its arg (kind << 8 | line) and detail ("400a005e" and, for a flashed
 * site, the flashed module's name): "SITE 0x400a005e" then who has it. The other +Drive mod is
 * named from the set's records (set, nset: the boot's or the save's). */
static void site_line(char *at, char *end, u32 arg, const char *d, const struct dl_status *set, u32 nset)
{
    char addr[12];
    u32 kind = arg >> 8, line = arg & 0xff;
    d = word(d, addr, addr + sizeof addr);
    at = cat(cat(cat(at, end, " 0x"), end, addr), end, "\n");
    if (kind == DL_SITE_CORE)
        cat(cat(at, end, "TAKEN BY "), end, *d ? d : "THE BUILD");
    else if (kind == DL_SITE_MOD) {
        char id[DL_ID_MAX];
        if (set && line < nset && set[line].id[0])
            copy_str(id, set[line].id, sizeof id);
        else
            copy_str(id, "ANOTHER MOD", sizeof id);
        cat(cat(at, end, "TAKEN BY "), end, id);
    } else if (kind == DL_SITE_PRELINK)
        cat(at, end, "IN A PRE-LINK RANGE");
    else if (kind == DL_SITE_STOCK)
        cat(at, end, "NOT STOCK BYTES");
    else
        cat(at, end, "REFUSED");
}

/* a refusal into [at, end); `line` is where its line starts in the same buffer; set and nset are
 * the set's records (a site's other mod) */
static void reason_line(const char *line, char *at, char *end, int code, u32 arg, const char *d,
                        const struct dl_status *set, u32 nset)
{
    static const char *const what[] = {
        "", "FILE MISSING", "FILE CHANGED", "TOO BIG", "BAD FILE", "NOT FORMAT 2",
        "OTHER DEVICE/OS", "CAN'T USE", "SAME ID AS", "NEEDS CORE", "NEEDS", "CONFLICTS",
        "EXPORT TAKEN", "CLAIMED", "NOT FOUND", "TABLE", "FULL", "NO ROOM", "OUT OF RANGE",
        "TOO MANY NAMES", "OVER 256 MODS",
        /* 21 to 28: TELOS's (DL_SITES, DL_REGIONS), which this loader doesn't build with */
        "REFUSED", "REFUSED", "REFUSED", "REFUSED", "REFUSED", "REFUSED", "REFUSED", "REFUSED",
        "NO STORAGE ROOM", "SITE", "EVENT NOT MAPPED"};
    at = cat(at, end, code < (int)(sizeof what / sizeof what[0]) ? what[code] : "REFUSED");
    if (code == DL_R_STORE) {            /* always on two lines: the detail is the whole of it */
        store_line(cat(at, end, "\n"), end, d ? d : "");
        return;
    }
    if (code == DL_R_CLASH) {
        site_line(at, end, arg, d ? d : "", set, nset);
        return;
    }
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

static void reason(char *at, char *end, int code, u32 arg, const char *d, const struct dl_status *set,
                   u32 nset)
{
    reason_line(at, at, end, code, arg, d, set, nset);
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
            if (!r->id[0] && real_id(dl_status[i].id, r->name))   /* the build list's label from */
                copy_str(r->id, dl_status[i].id, sizeof r->id);    /* the page's opening on */
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

static void build_list(void);

/* Read the open folder's listing (cwd): its folders and .elemod files, without opening any file
 * (decided 2026-11-07: opening the page with many large files held the key path and USB for
 * seconds, T178; T230, 2026-11-09: opening each of 257 files for its size alone, on the unit's
 * card, held the UI task long enough that the first DOWN after YES was refused). Each file is
 * read later, one at a time (inspect), which learns its size. The files the page keeps
 * across folders (ticked, saved, loaded at boot) stay with what is known of them. -> 0, -2 no
 * memory (nothing changed). The arrays are new, so the build list (build_rows) is made again
 * here, for every caller (a folder opened or left, a change over USB, a delete). */
static int read_folder(void)
{
    int i, n, kept = 0, k;
    struct dl_ent *ents;
    struct file *nf;
    struct lrow *nl;
    int *nb, *ns;
    char p[DL_NAME_MAX + 1];
    read_stop();                         /* the rows' names go */
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
        if (ents[i].dir && depth < DL_DEPTH && ents[i].name[0] != '.' && ents[i].name[0] != ' ' &&
            join(p, cwd, ents[i].name)) {
            lrows[nlrows].kind = R_DIR;
            copy_str(lrows[nlrows++].name, ents[i].name, DL_SEG_MAX + 1);
        }
    for (i = 0; i < n; i++) {            /* then the .elemod files */
        struct file *r;
        int f;
        if (ents[i].dir || !ends_elemod(ents[i].name) || ents[i].name[0] == ' ' || !join(p, cwd, ents[i].name))
            continue;
        f = find_file(p);
        if (f < 0) {
            if (nfiles >= fcap)
                break;
            f = nfiles++;
            r = &files[f];
            dl_memset(r, 0, sizeof *r);
            copy_str(r->name, p, sizeof r->name);
            boot_record(r);              /* its size, a file gone or one past the staging buffer:
                                          * the background check's read (inspect) */
        }
        files[f].here = 1;
        lrows[nlrows].kind = R_FILE;
        lrows[nlrows++].f = f;
    }
    dl_unlock();
    dl_free(ents);
    count_sel();
    build_list();                        /* build_rows is new (zeroed): left as it is, every build row
                                          * read files[0] (the FCR emulator run, 2026-11-09: VIEW BUILD
                                          * after a folder change drew one mod three times) */
    next_check = 0;
    return 0;
}

/* The parked mods from raw (/mods/.store's bytes, len of them), into parks; none for a missing
 * or bad file (the boot then places afresh, so nothing is parked). -> 0, -2 no memory. */
static int parks_from(const u8 *raw, s32 len)
{
    int n = len > 0 ? dl_park_list(raw, (u32)len, 0, 0) : 0;
    dl_free(parks);
    dl_free(forgetting);
    parks = 0;
    forgetting = 0;
    npark = 0;
    if (n <= 0)
        return 0;
    parks = dl_alloc((u32)n * sizeof *parks);
    forgetting = dl_alloc((u32)n);
    if (!parks || !forgetting)
        return -2;
    npark = dl_park_list(raw, (u32)len, parks, n);
    if (npark < 0)
        npark = 0;
    return 0;
}

/* DL_DIR/.store whole into a new heap buffer (*raw) -> its length, or < 0 (none: missing, or no
 * memory, -2). Under dl_lock. */
static s32 read_store(u8 **raw)
{
    char path[DL_PATH_MAX];
    s32 size, len;
    *raw = 0;
    path_of(path, DL_STORE_NAME);
    size = dl_fs_file_size(path);
    if (size <= 0)
        return -1;
    *raw = dl_alloc((u32)size);
    if (!*raw)
        return -2;
    len = dl_read_file(DL_STORE_NAME, *raw, (u32)size);
    if (len < 0) {
        dl_free(*raw);
        *raw = 0;
    }
    return len;
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
    {
        u8 *raw;
        s32 slen;
        dl_lock();
        slen = read_store(&raw);
        dl_unlock();
        k = slen == -2 || parks_from(raw, slen);
        dl_free(raw);
        if (k) {
            page_free();
            return -2;
        }
    }
    if (read_folder()) {
        page_free();
        return -2;
    }
    return 0;
}

/* One stretch (32 KB) of name's read, from its open: one a tick, so no tick blocks the UI task
 * in a whole file's read. Whole-file reads every 2 ticks let stock's UI event queue grow (about
 * 40 events a second with 481 KB files); past 16 queued, analoghealth refuses a remote key as busy,
 * the refused key never reaches ev_key to start the QUIET pause, and after 2 s it expires
 * (emulator, 2026-11-08: a key 1.8 s into the check of 18 files of 481 KB; a stretch a tick
 * kept the queue at 8 or under). A read of another name first gives up the one
 * running. -> 1 more to read, 0 read (rd_len, rd_crc; the caller holds the staging buffer and
 * gives it back after its check), -1 busy (the buffer is the route's). */
static int read_step(const char *name)
{
    int res;
    if (reading && rd_name != name)
        read_stop();
    if (!reading) {
        if (!dl_staging_take_idle(ticks, READ_STALL))
            return -1;
        dl_lock();
        res = dl_read_open(&rd, name);
        dl_unlock();
        if (res < 0) {
            rd_len = -1;
            rd_crc = 0;
            return 0;
        }
        reading = 1;
        rd_name = name;
    }
    dl_lock();
    res = dl_read_more(&rd, dl_stage_at, dl_stage_len);
    dl_unlock();
    if (!res)
        return 1;
    reading = 0;
    rd_len = res == 1 ? (s32)rd.st.got : res;
    rd_crc = ~rd.st.crc;                 /* -2 too: the reader read it to its end; the staged
                                          * file's CRC (payload hex skipped), as /mods/active's */
    return 0;
}

/* A file of n bytes the boot reads into the +Drive area itself (dl.h, dl_stage_top): pass 1
 * takes it when it is no larger than the area. */
static int fits_area(u32 n)
{
    u32 lo, hi;
    return dl_stage_top && dl_area((u32)core_desc, &lo, &hi) == 0 && n <= hi - lo;
}

/* One file read, a stretch a call (read_step): its CRC-32 and size, then a check of it alone
 * for its id, version and pass-1 verdict (a check's set refusals mean nothing here: only codes
 * about the file itself are kept), so no number of files reaches a set's limits. Holds the
 * staging buffer from the read's open to the check's end. -> 0 (the file is checked), 1 more
 * to read, -1 busy. */
static int inspect(struct file *r)
{
    static struct dl_choice one;
    static struct dl_status one_st;
    static char one_det[1][32], one_ver[1][DL_VER_MAX];
    s32 len;
    int c;
    if (r->checked)
        return 0;
    c = read_step(r->name);
    if (c)
        return c;
    len = rd_len;
    r->checked = 1;
    r->big = 0;
    if (len == -2 && fits_area(rd.st.got)) {   /* the boot reads it in the area: it checks it */
        r->big = 1;
        r->size = rd.size;
        r->crc = rd_crc;
    } else if (len == -2)
        r->file_code = DL_R_TOO_BIG;
    else if (len < 0)
        r->file_code = DL_R_MISSING;
    else {
        r->size = rd.size;               /* the row's KB: the file on the +Drive, payload too */
        r->crc = rd_crc;
        copy_str(one.name, r->name, DL_LINE_NAME_MAX);
        one.crc = r->crc;
        if (dl_check_buf((u32)core_desc, &one, len, dl_stage_at, dl_stage_len, &one_st, one_det,
                         one_ver) >= 0) {        /* the file just read, not read again */
            c = one_st.code;
            if (real_id(one_st.id, r->name))
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

/* ---- the page's view: drawn with std1's helpers ---- */

/* The frame is 128 x 64, y 0 the top row: ROWS list rows of ROW_H pixels from the top, no
 * title, as the Rytm's MODS page (stock's SAMPLES view) draws its list. The cursor's row is a
 * set bar with light text, an outline while a side panel has the keys. The list's and the
 * panes' text is stock's font 13 (6 rows and a descent, bold), the font the Rytm's file view
 * draws with (its font object 13 on 1.74) and the same font on the A4, drawn by core's screen
 * calls (core_text_draw, y from the bottom); the boxes keep std1's TEXT_SMALL. */
#define ROW_H 8
#define ROWS 8
#define ROW_Y(r) ((r) * ROW_H)
#define FONT_LIST 13                     /* stock's font 13 (core-a4mk2 screen calls) */
#define TEXT_DY 0                        /* a row's text sits this far below its top */
#define SPLIT_X 65                       /* the action panel's line, as MANAGE PROJECTS' pane */
#define BAR_L 3                          /* the cursor bar: x BAR_L..BAR_R, BAR_H rows, between */
#define BAR_R 119                        /* the edges as SOUND BROWSER and MANAGE PROJECTS draw it */
#define BAR_H 7
#define LABEL_X 8                        /* the label starts this far into a row (the tick before it) */
#define COLUMN_X 117                     /* the right-hand column ends here (right-aligned) */
#define TRACK_X 121                      /* MANAGE PROJECTS' scroll column: a dotted track at x 122..124, */
#define EDGE_X 127                       /* the thumb a box over x 121..125; the edge lines with the
                                          * page-arrow marks at x 0..1 and 126..127 */
#define STATE_GAP 3                      /* pixels kept free before the right-hand column */
#define BOX_TICKS 60                     /* a message box closes by itself after 3 s */

#include "clip.inc"

static int text_w(const char *s)
{
    return core_text_width(FONT_LIST, s);
}

/* s in FONT_LIST with its top on row y, std1's alignment and TEXT_INVERT; returns its width */
static int list_text(void *fb, int x, int y, unsigned style, const char *s)
{
    int w = text_w(s);
    if ((style & 3) == TEXT_RIGHT)
        x -= w - 1;
    core_text_draw(fb, FONT_LIST, x, 64 - core_font_height(FONT_LIST) - y, CORE_ALIGN_LEFT | (style & TEXT_INVERT ? CORE_TEXT_INVERT : 0), s);
    return w;
}

enum { M_DRIVE, M_BUILD };
enum { P_NONE, P_LOAD, P_UNLOAD, P_DELETE, P_CLEAR, P_RMDIR, P_FORGET, P_RESTART };
enum { PN_NONE, PN_LEFT, PN_RIGHT };
static u8 page_on;                       /* the page shows (UI task only) */
static u8 mode;                          /* the list: the files, or the build */
static u8 pending;                       /* what the open confirm window does (P_NONE: none open) */
static u8 panel;                         /* an open side panel */
static int cur, top;                     /* the list's cursor row and its first drawn row */
static int pcur;                         /* the panel's cursor row */
static char msg[80];                     /* the confirm window's question, and boxes' texts */
static char box[160];                    /* the message box's text ("" none) */
static u32 box_until;                    /* the tick it closes by itself, 0 never (INFO) */
static char rmdir_name[DL_SEG_MAX + 1];  /* the folder DELETE DIR asked about */
static u8 swallow[32];                   /* keys whose press the page took: their release too */

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
    return mode == M_BUILD ? nbuild + npark : nlrows;
}

/* the file of list row i in this mode, or -1 for "..", a folder and a parked mod */
static int file_k(int i)
{
    if (mode == M_BUILD)
        return i < nbuild ? build_rows[i] : -1;
    return lrows[i].kind == R_FILE ? lrows[i].f : -1;
}

/* the parked mod of list row i (the build list's rows after the build's), or -1 */
static int park_k(int i)
{
    return mode == M_BUILD && i >= nbuild && i < nbuild + npark ? i - nbuild : -1;
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
    return cur;
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
static const char *const acts_build[] = {"SELECT ALL", "DESELECT ALL", "UNLOAD", "INFO", "FORGET DATA",
                                         "APPLY & RESTART"};
#define NACT 5                           /* the rows both panels always have */

/* The saved set differs from the running one: a build row reads ADD (saved, not loaded now) or
 * DROP (loaded now, not saved), and a "!" row of a saved mod the last boot refused or dropped
 * counts as ADD, since another boot tries it again. Parked data doesn't count: FORGET DATA
 * rewrites it at once. */
static int set_differs(void)
{
    int i;
    for (i = 0; i < nfiles; i++)
        if (files[i].on != (files[i].boot == DL_ST_LOADED))
            return 1;
    return 0;
}

/* the action panel's rows: the build list adds APPLY & RESTART while the sets differ */
static int n_acts(void)
{
    return mode == M_BUILD && set_differs() ? NACT + 1 : NACT;
}

static const char *act_label(int i)
{
    if (mode == M_BUILD)
        return acts_build[i];
    return i == 2 && !build_empty() ? "UPDATE BUILD" : i == 4 && delete_dir_now() ? "DELETE DIR" : acts_drive[i];
}

/* The build list's ticks: the file list's are files[].pick (they are the build), the build
 * list's are only UNLOAD's and FORGET DATA's choice, kept per list row and dropped whenever the
 * list is made again, as stock's tick vector was on the Rytm. */
static u8 *bticks;
static int nbticks;

static int is_ticked(int row)
{
    int k;
    if (mode == M_DRIVE)
        return (k = file_k(row)) >= 0 && files[k].pick;
    return row >= 0 && row < nbticks && bticks[row];
}

static void tick(int row, int on)
{
    if (mode == M_BUILD && row >= 0 && row < nbticks)
        bticks[row] = (u8)on;
}

/* The list's label of row i (the cursor's or not): the file list shows a folder by its bare name
 * and a file by its file name without ".elemod", never its id (decided 2026-11-09: the rows don't
 * change under the check). The build list shows each mod's id (decided 2026-11-09): the boot's
 * record names the mods it read, the save's check the ones added since; a mod with no id known
 * shows its file name as the file list does. Every label is clipped from the left behind "<" to
 * the room left of its right-hand column (clip.inc). */
static void row_label(int i, char *out, char *end, int room)
{
    char name[DL_ID_MAX > DL_SEG_MAX + 1 ? DL_ID_MAX : DL_SEG_MAX + 1];
    int k = file_k(i);
    if (park_k(i) >= 0)
        copy_str(name, parks[park_k(i)].id, sizeof name);
    else if (k >= 0 && mode == M_BUILD && files[k].id[0])
        copy_str(name, files[k].id, sizeof name);
    else if (k >= 0)
        bare_name(name, name + sizeof name, files[k].name);
    else if (lrows[i].kind == R_UP)
        copy_str(name, "..", sizeof name);
    else
        copy_str(name, lrows[i].name, sizeof name);
    clip_tail(out, end, name, room, text_w);
}

/* The background check's progress (decided 2026-11-08: a plan proves the check is done before its
 * first key). It is not drawn: /mods/.status has a line "check: n/N FOLDER" (route.c), n the open
 * folder's files read and checked, N its files, "check: -" before the page has read a folder.
 * dl_page_tick publishes it under dl_lock, so the route's read never sees a half-written folder. */
volatile int dl_check_n = -1, dl_check_all;
char dl_check_dir[DL_NAME_MAX + 1];

static void publish_check(void)
{
    int i, n = 0, all = 0;
    if (!lrows)
        return;
    for (i = 0; i < nlrows; i++)
        if (lrows[i].kind == R_FILE) {
            all++;
            n += files[lrows[i].f].checked;
        }
    if (n == dl_check_n && all == dl_check_all && !dl_strcmp(cwd, dl_check_dir))
        return;
    dl_lock();
    copy_str(dl_check_dir, cwd, sizeof dl_check_dir);
    dl_check_all = all;
    dl_check_n = n;
    dl_unlock();
}

/* list row i's right-hand column into t, as the Rytm's (where SAMPLES puts sizes). Files: whole
 * KB rounded up once the check has read the file (blank before), "!" for a file pass 1 refuses.
 * Folders and "..": "(D)"; a folder with n ticked files below it "(n)" in its place (decided
 * 2026-11-09). Build: KEEP (loaded now, stays), ADD (loads at the next boot), DROP (loaded now,
 * gone after the next boot), "!" (the last boot refused it); a parked mod PARK. */
static void row_column(int i, char *t, char *end)
{
    int k = file_k(i);
    const struct file *r;
    t[0] = 0;
    if (park_k(i) >= 0)
        cat(t, end, "PARK");
    else if (k < 0) {
        if (lrows[i].kind == R_DIR && lrows[i].sel)
            cat(num(cat(t, end, "("), end, lrows[i].sel), end, ")");
        else
            cat(t, end, "(D)");
    } else if (r = &files[k], mode == M_DRIVE) {
        if (r->file_code)
            cat(t, end, "!");
        else if (r->checked)
            cat(num(t, end, (r->size + 1023) / 1024), end, "KB");
    } else if (r->boot == DL_ST_REFUSED || r->boot == DL_ST_DROPPED)
        cat(t, end, "!");
    else if (!r->on)
        cat(t, end, "DROP");
    else
        cat(t, end, r->boot == DL_ST_LOADED ? "KEEP" : "ADD");
}

/* a message box: closed by any key, or by itself after BOX_TICKS (sticky: only by a key) */
static void box_show(const char *s, int sticky)
{
    copy_str(box, s, sizeof box);
    box_until = sticky ? 0 : ticks + BOX_TICKS + (ticks + BOX_TICKS == 0);
    redraw = 1;
}

static void show_box(const char *s)
{
    box_show(s, 0);
}

static void confirm(const char *line1)
{
    copy_str(msg, line1, sizeof msg);
    redraw = 1;                          /* pending (set by the caller) opens the window */
}

/* the cursor on list row `row`, the drawn rows following it */
static void cursor_to(int row)
{
    int n = list_n();
    if (row < 0 || row >= n)
        row = 0;
    cur = row;
    if (cur < top)
        top = cur;
    if (cur >= top + ROWS)
        top = cur - ROWS + 1;
    if (top > (n > ROWS ? n - ROWS : 0))
        top = n > ROWS ? n - ROWS : 0;
    if (top < 0)
        top = 0;
    redraw = 1;
}

/* The ticked files, as indices into files -> how many. The file list: every ticked file, in every
 * folder (here: only the open folder's). The build list: its ticked rows. */
static int ticked(int *out, int here)
{
    int n = 0, i;
    if (mode == M_DRIVE) {
        for (i = 0; i < nfiles && n < fcap; i++)
            if (files[i].pick && (!here || files[i].here))
                out[n++] = i;
        return n;
    }
    for (i = 0; i < nbuild && i < nbticks && n < fcap; i++)
        if (bticks[i])
            out[n++] = build_rows[i];
    return n;
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
 * left, as SAMPLES. -> 0, or -2 no memory (still in the old folder). */
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
enum { J_NONE, J_LOAD, J_CLEAR, J_UNLOAD, J_FORGET };
enum { JP_FILES, JP_START, JP_STEPS, JP_END };
enum { FP_READ, FP_WRITE, FP_COMMIT };   /* FORGET DATA's steps, one a tick */
static struct {
    u8 what, phase, first;
    int m, i, k, n;                      /* set size, next line or file, unloaded, ticked */
    int c;                               /* the set's files the check reads: m less the big ones */
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
    if (what == J_LOAD && job.c < job.m) {   /* a big file in the set: only the boot checks it */
        job_end("SAVED: BIG FILE\nCHECKED AT NEXT BOOT");
        return;
    }
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
            k = inspect(&files[job.i]);
            if (k > 0) {                 /* a stretch read; the rest on the next ticks */
                job.tries = 0;
                return;
            }
            if (k) {
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
        job.c = 0;                       /* a big file stays out of the check: the boot checks it */
        choice = dl_alloc(s * sizeof *choice);
        cst = dl_alloc(s * sizeof *cst);
        cdet = dl_alloc(s * sizeof *cdet);
        if (!choice || !cst || !cdet) {
            job_end("NO MEMORY");
            return;
        }
        for (i = 0, k = 0; i < nfiles && k < job.m; i++)
            if (files[i].keep && !files[i].big) {
                copy_str(choice[k].name, files[i].name, DL_LINE_NAME_MAX);
                choice[k++].crc = files[i].crc;
            }
        job.c = k;
        if (dl_check_start((u32)core_desc, job.c, dl_stage_at, dl_stage_len, cst, cdet, 0) < 0) {
            job_end("NOT SAVED");
            return;
        }
        job.i = 0;
        job.phase = JP_STEPS;
        return;
    }
    if (job.phase == JP_STEPS && job.i < job.c) {
        k = read_step(choice[job.i].name);  /* a stretch a tick, as the background's reads */
        if (k < 0) {
            if (++job.tries > JOB_BUSY) {
                dl_check_cancel();
                job_end("BUSY: TRY AGAIN");
            }
            return;
        }
        job.tries = 0;
        if (k > 0)
            return;
        dl_check_step_buf(&choice[job.i], rd_len, rd_crc);
        job.i++;
        dl_staging_give();
        return;
    }
    ok = dl_check_finish();
    if (ok != job.c) {
        msg[0] = 0;
        for (k = 0; k < job.c && ok >= 0; k++)
            if (cst[k].state != DL_ST_LOADED) {
                reason(msg, msg + sizeof msg, cst[k].code ? cst[k].code : DL_R_LIMIT, cst[k].arg, cdet[k], cst,
                       (u32)job.c);
                break;
            }
        job_end(msg[0] ? msg : "NOT SAVED");
        return;
    }
    for (i = 0, k = 0; i < nfiles; i++) {
        files[i].was = files[i].on;
        files[i].on = files[i].keep;
        if (files[i].keep && !files[i].big && k < job.c && real_id(cst[k++].id, files[i].name))   /* the save's check names */
            copy_str(files[i].id, cst[k - 1].id, sizeof files[i].id);          /* the rows added since boot */
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

/* FORGET DATA's text for a parked mod (INFO, and ticking its row): its parked bits per scope,
 * two scopes a line, then the lock codes and CCs kept for it. out holds 160. */
static void park_text(const struct dl_park *p, char *out, char *end)
{
    char *at = cat(out, end, "PARKED DATA"), *line = at, item[24];
    int i, first = 1;
    for (i = 0; i < DL_STORE_SCOPES; i++) {
        char *e = item + sizeof item;
        if (!p->bits[i])
            continue;
        cat(num(cat(cat(item, e, dl_store_scopes[i]), e, ": "), e, p->bits[i]), e, " bits");
        if (!first && (u32)(at - line) + 2 + dl_strlen(item) <= BOX_LINE)
            at = cat(at, end, ", ");
        else {
            at = cat(at, end, "\n");
            line = at;
        }
        first = 0;
        at = cat(at, end, item);
    }
    if (p->codes || p->ccs) {
        at = cat(at, end, "\n");
        if (p->codes)
            at = cat(num(at, end, p->codes), end, p->codes == 1 ? " LOCK CODE" : " LOCK CODES");
        if (p->codes && p->ccs)
            at = cat(at, end, ", ");
        if (p->ccs)
            cat(num(at, end, p->ccs), end, p->ccs == 1 ? " CC" : " CCS");
    }
}

/* FORGET DATA in steps (decided 2026-11-10: a stepped save, one file a tick, as LOAD TO BUILD):
 * read /mods/.store, write it again without the marked mods' parked fields, lock codes and CCs
 * to .store.tmp, then the old file goes and the tmp file takes its name. Their bits go back to
 * the scope only at its next repack (docs/DRIVE.md, "+Drive storage"). */
static void forget_step(void)
{
    char tmp[DL_PATH_MAX], path[DL_PATH_MAX];
    const char **ids;
    dl_file f;
    int i, n, r;
    if (job.phase == FP_READ) {
        dl_lock();
        store_len = read_store(&store_raw);
        dl_unlock();
        if (store_len == -2) {
            job_end("NO MEMORY");
            return;
        }
        if (store_len < 0 || dl_park_list(store_raw, (u32)store_len, 0, 0) <= 0) {
            job_end("NO PARKED DATA");   /* gone since the page read it: a boot repacked it */
            return;
        }
        job.phase = FP_WRITE;
        return;
    }
    if (job.phase == FP_WRITE) {
        ids = dl_alloc((u32)(npark > 0 ? npark : 1) * sizeof *ids);
        store_out = dl_alloc((u32)store_len);
        if (!ids || !store_out) {
            dl_free(ids);
            job_end("NO MEMORY");
            return;
        }
        for (i = n = 0; i < npark; i++)
            if (forgetting[i])
                ids[n++] = parks[i].id;
        store_len = dl_park_forget(store_raw, (u32)store_len, ids, n, store_out);
        dl_free(ids);
        if (store_len < 0) {
            job_end("NOT SAVED");
            return;
        }
        path_of(tmp, DL_STORE_NAME ".tmp");
        dl_lock();
        dl_memset(&f, 0, sizeof f);
        r = dl_fs_open(&f, tmp, 1);
        if (!r)
            r = dl_fs_write(&f, store_out, (u32)store_len);
        dl_fs_close(&f);
        if (!r && dl_fs_file_size(tmp) != store_len)
            r = -1;
        if (r)
            dl_fs_delete(tmp);
        dl_unlock();
        if (r) {
            job_end("SAVE FAILED");
            return;
        }
        job.phase = FP_COMMIT;
        return;
    }
    path_of(tmp, DL_STORE_NAME ".tmp");
    path_of(path, DL_STORE_NAME);
    dl_lock();
    dl_fs_delete(path);
    r = dl_fs_rename(tmp, path);
    dl_unlock();
    if (r) {
        job_end("SAVE FAILED");
        return;
    }
    n = job.n;
    if (parks_from(store_out, store_len)) {
        job_end("NO MEMORY");
        return;
    }
    page_rows();
    cursor_to(nbuild < list_n() ? nbuild : 0);
    {
        char *end = msg + sizeof msg;
        cat(num(cat(msg, end, "DATA FORGOTTEN ("), end, (u32)n), end, n == 1 ? " MOD)" : " MODS)");
    }
    job_end(msg);
}

/* the save or FORGET DATA's next step */
static void job_step(void)
{
    if (job.what == J_FORGET)
        forget_step();
    else
        save_step();
}

/* FORGET DATA in the build list: the ticked parked mods (decided 2026-11-10), after its confirm */
static int forget_marks(void)
{
    int i, n = 0, k;
    for (k = 0; k < npark; k++)
        forgetting[k] = 0;
    for (i = 0; i < nbticks; i++)
        if (bticks[i] && (k = park_k(i)) >= 0 && !forgetting[k]) {
            forgetting[k] = 1;
            n++;
        }
    return n;
}

static void ask_forget(void)
{
    char *end = msg + sizeof msg;
    int n;
    if (!npark) {
        show_box("NO PARKED DATA");
        return;
    }
    n = forget_marks();
    if (!n) {
        show_box("NO PARKED MOD SELECTED");
        return;
    }
    cat(num(cat(msg, end, "FORGET DATA? ("), end, (u32)n), end, n == 1 ? " MOD)" : " MODS)");
    pending = P_FORGET;
    confirm(msg);
}

static void do_forget(void)
{
    int n;
    if (job.what) {
        show_box("BUSY: TRY AGAIN");
        return;
    }
    n = forget_marks();                  /* the ticks again: the confirm window took no keys */
    if (!n)
        return;
    dl_memset(&job, 0, sizeof job);
    job.what = J_FORGET;
    job.phase = FP_READ;
    job.n = n;
    show_box("FORGETTING DATA");
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
    page_rows();
    cursor_to(0);
    show_box(done == n ? count_msg(done, "DELETED") : "DELETE FAILED");
}

/* DELETE DIR: an empty folder only, as the Rytm's SAMPLES ("ERROR (NOT EMPTY?)" otherwise;
 * the page never deletes a folder's files for it) */
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

/* APPLY & RESTART's YES: the page says RESTARTING and takes no more keys; dl_page_tick reboots
 * once that frame is drawn (restart_tick) */
static u8 restarting;
static u32 restart_asked;

static void do_restart(void)
{
    restarting = 1;
    restart_asked = ticks;
    panel = PN_NONE;
    box_show("RESTARTING", 1);
}

static void page_yes(int yes)
{
    if (yes && page_on) {                /* the action panel stays open, as stock's */
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
        else if (pending == P_FORGET)
            do_forget();
        else if (pending == P_RESTART)
            do_restart();
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
        reason(out, end, r->file_code, 0, r->detail, 0, 0);
        return 1;
    }
    if (r->boot != DL_ST_REFUSED && r->boot != DL_ST_DROPPED)
        return 0;
    reason_line(out, cat(out, end, "BOOT: "), end, r->boot_code ? r->boot_code : DL_R_LIMIT, r->boot_arg,
                r->boot_detail, dl_status, dl_status_n < DL_MAX_MODS ? dl_status_n : DL_MAX_MODS);
    return 1;
}

/* INFO's text for a file (decided 2026-11-09): its folder ("/mods/a/b") on one line, clipped
 * from the left behind "<" past BOX_LINE characters, then its file name, BOX_LINE characters a
 * line (DL_SEG_MAX: at most 3). out holds 160. */
static void info_text(const struct file *r, char *out, char *end)
{
    char dir[DL_PATH_MAX];
    const char *b = base_of(r->name);
    u32 n;
    char *at;
    at = cat(dir, dir + sizeof dir, DL_DIR);
    if (b > r->name) {
        at = cat(at, dir + sizeof dir, "/");
        n = (u32)(b - r->name) - 1;
        if (at + n > dir + sizeof dir - 1)
            n = (u32)(dir + sizeof dir - 1 - at);
        dl_memcpy(at, r->name, n);
        at[n] = 0;
    }
    n = dl_strlen(dir);
    at = n > BOX_LINE ? cat(cat(out, end, CLIP_MARK), end, dir + n - (BOX_LINE - 1)) : cat(out, end, dir);
    while (*b && at < end - 2) {
        *at++ = '\n';
        for (n = 0; n < BOX_LINE && *b && at < end - 1; n++)
            *at++ = *b++;
    }
    *at = 0;
}

/* INFO in the build list: the cursor's mod's folder and file name, in a box closed with NO; on a
 * parked mod, its parked data */
static void do_info(void)
{
    static char info[160];
    int row = cursor_row();
    if (park_k(row) >= 0) {
        park_text(&parks[park_k(row)], info, info + sizeof info);
        box_show(info, 1);
        return;
    }
    if (row < 0 || row >= nbuild) {
        show_box("NO MODS IN BUILD");
        return;
    }
    info_text(&files[build_rows[row]], info, info + sizeof info);
    box_show(info, 1);
}

/* a file row's tick; -> 0 when the build is full (nothing changed) */
static int set_pick(int row, int on)
{
    int k = file_k(row);
    if (k < 0) {
        if (park_k(row) >= 0)            /* a parked mod: ticked for FORGET DATA */
            tick(row, on);
        return 1;
    }
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
 * whole reason, reachable from the page). The placeholder row of an empty list does nothing. */
static void row_yes(void)
{
    static char why[160];
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
    if (park_k(row) >= 0) {             /* a parked mod: its tick, and its data in the box */
        on = !is_ticked(row);
        set_pick(row, on);
        if (row + 1 < list_n())
            cursor_to(row + 1);
        if (on) {
            park_text(&parks[park_k(row)], why, why + sizeof why);
            show_box(why);
        }
        return;
    }
    k = file_k(row);
    on = !is_ticked(row);                /* no read here: a tick is never held up by a check */
    if (!set_pick(row, on)) {
        show_box("OVER 256 MODS");
        return;
    }
    count_sel();
    if (row + 1 < list_n())
        cursor_to(row + 1);
    redraw = 1;
    if (on && row_reason(&files[k], why, why + sizeof why))
        show_box(why);
}

/* YES on the left panel's row (VIEW BUILD / VIEW +DRIVE): switch the list and close the panel,
 * as SAMPLES' VIEW RAM */
static void left_yes(void)
{
    mode = mode == M_DRIVE ? M_BUILD : M_DRIVE;
    panel = PN_NONE;
    page_rows();                         /* back from BUILD: the file list's ticks, kept in files */
}

/* YES on the action panel's row i */
static void act_yes(int i)
{
    int r;
    if (job.what && !(mode == M_BUILD && i == 3)) {   /* a save is running: no other action until it
                                                      * ends (INFO reads only) */
        show_box("BUSY: TRY AGAIN");
        return;
    }
    if (i == 0 || i == 1) {              /* SELECT ALL, DESELECT ALL: the open folder's files, as
                                          * SAMPLES' (files only, no folder, no depth) */
        for (r = 0; r < list_n(); r++)
            if (!set_pick(r, i == 0)) {
                show_box("OVER 256 MODS");
                break;
            }
        count_sel();
        redraw = 1;
    } else if (i == 2)
        ask(mode == M_DRIVE ? "LOAD" : "UNLOAD", mode == M_DRIVE ? P_LOAD : P_UNLOAD);
    else if (i == 3 && mode == M_BUILD)
        do_info();
    else if (i == 4 && mode == M_BUILD)
        ask_forget();
    else if (i == 3 && mode == M_DRIVE) {
        pending = P_CLEAR;
        confirm("CLEAR THE BUILD?");
    } else if (i == NACT && mode == M_BUILD && set_differs()) {
        pending = P_RESTART;             /* a reboot drops what the project hasn't saved, and core
                                          * can't tell whether it has: the window always says so */
        confirm("RESTART NOW?\nUNSAVED PROJECT\nCHANGES ARE LOST");
    } else if (i == 4 && mode == M_DRIVE) {
        if (delete_dir_now()) {          /* as SAMPLES: "DELETE DIR?" over CONFIRM Y/N */
            copy_str(rmdir_name, lrows[cursor_row()].name, sizeof rmdir_name);
            pending = P_RMDIR;
            confirm("DELETE DIR?");
        } else
            ask("DELETE", P_DELETE);
    }
}

/* (Re)make the list. In the file list the ticks are the page's own (files[].pick), which start as
 * the saved build on entering MODS and after a change (decided 2026-11-07) and stay across folders
 * (decided 2026-11-08); the build list's start empty each time. The cursor goes to the top. An
 * empty list draws one placeholder row (NO MODS ON +DRIVE, BUILD EMPTY) that YES does nothing to:
 * the Rytm's page needed it because stock's list hung on zero rows, and a stock menu on the A4
 * reads a missing row on LEFT, RIGHT and a turn with none (traced on OS 1.56); this page draws its
 * own, and keeps the row so that an empty page looks and acts the same on both
 * (decided 2026-11-14). */
static void page_rows(void)
{
    int n = list_n();
    dl_free(bticks);
    bticks = 0;
    nbticks = 0;
    if (mode == M_BUILD && n > 0 && (bticks = dl_alloc((u32)n)) != 0)
        nbticks = n;
    cur = top = 0;
    pcur = 0;
    redraw = 1;
}

/* the page closes: back to SETTINGS > GLOBAL, which never saw a key while it showed */
static void page_close(void)
{
    page_on = 0;
    panel = PN_NONE;
    pending = P_NONE;
    box[0] = 0;
    if (!job.what)
        read_stop();                     /* a background read: the buffer goes back to the route */
    core_redraw();
}

/* The MODS row's select: a new page each time, on DL_DIR itself. A save left running when the page
 * closed is dropped here (its arrays are the page's), as the Rytm's page does. */
static void mods_open(void **f)
{
    static const char *const skip[] = {
        "", "SKIPPED: [NO] HELD", "SKIPPED: CRASH", "NO +DRIVE", "OLD DESCRIPTOR",
        "SET UNSTABLE", "OVER 256 LINES", "NO MEMORY"};
    int busy;
    (void)f;                             /* the row's functor: its payload is the menu */
    read_stop();                         /* a read left running when its page closed */
    if (job.what) {                      /* a save left running when its page closed */
        if (job.what != J_FORGET && job.phase == JP_STEPS)
            dl_check_cancel();
        job.what = J_NONE;
        set_free();
    }
    busy = scan();
    mode = M_DRIVE;
    pending = P_NONE;
    panel = PN_NONE;
    box[0] = 0;
    page_on = 1;
    page_rows();
    drawn_at = ticks;
    if (busy)
        show_box("NO MEMORY");
    else if (dl_skip_reason && dl_skip_reason < sizeof skip / sizeof skip[0])
        show_box(skip[dl_skip_reason]);
    core_redraw();
}

static int nothing(void)
{
    return 0;
}

/* core_additem's row: label, select, draw (the icon), change. Not core_setting_add, whose row
 * draws no icon: MODS gets a 9 x 9 icon like the other GLOBAL rows (page.s). */
static const void *const mods_row[4] = {
    (const void *)dl_mods_label, (const void *)mods_open,
    (const void *)dl_mods_icon, (const void *)nothing,
};

/* ev_settings: GLOBAL's rows are built; MODS goes last */
void dl_settings(void *menu)
{
    core_additem(menu, mods_row);
}

/* ---- keys and turns: every one is the page's while it shows ---- */

static void move(int by)
{
    if (panel != PN_NONE) {
        int n = panel == PN_RIGHT ? n_acts() : 1;
        pcur += by;
        pcur = pcur < 0 ? 0 : pcur >= n ? n - 1 : pcur;
        redraw = 1;
        return;
    }
    if (list_n() > 0) {
        int to = cur + by, n = list_n();
        cursor_to(to < 0 ? 0 : to >= n ? n - 1 : to);
    }
}

/* one key press (or an UP / DOWN repeat) while the page shows */
static void page_key(unsigned key, unsigned flags)
{
    if (restarting)                      /* the box stays until the reset */
        return;
    if (box[0]) {                        /* any key closes the box, as stock's NO */
        box[0] = 0;
        redraw = 1;
        return;
    }
    if (pending != P_NONE) {             /* the confirm window: YES does it, NO doesn't */
        if (key == K_YES || key == K_NO)
            page_yes(key == K_YES);      /* closes it */
        redraw = 1;
        return;
    }
    if (key == K_UP || key == K_DOWN) {
        int by = (flags & F_FUNC) && panel == PN_NONE ? ROWS : 1;   /* FUNC: a page of rows */
        move(key == K_UP ? -by : by);
        return;
    }
    if (panel != PN_NONE) {
        if (key == K_YES) {
            if (panel == PN_LEFT)
                left_yes();
            else
                act_yes(pcur);
        } else if (key == K_NO || (panel == PN_RIGHT && key == K_LEFT) || (panel == PN_LEFT && key == K_RIGHT)) {
            panel = PN_NONE;
            redraw = 1;
        }
        return;
    }
    if (key == K_YES)
        row_yes();
    else if (key == K_NO)
        page_close();
    else if ((key == K_RIGHT || key == K_LEFT) && (nlrows || npark)) {   /* with no file, folder
                                          * or parked data neither panel opens (decided 2026-11-07) */
        panel = key == K_RIGHT ? PN_RIGHT : PN_LEFT;
        pcur = 0;
        redraw = 1;
    }
}

/* ev_key (native): the time of every event (the checks wait QUIET ticks after one); while the page
 * shows, every event is taken, and a key whose press the page took keeps its release from stock
 * after the page has closed */
int dl_key(void *brain, void *ev)
{
    unsigned key, flags, bit;
    (void)brain;
    last_input = ticks;
    if (!ev)
        return 0;
    key = *(const u32 *)((const u8 *)ev + EV_KEY);
    flags = *(const u32 *)((const u8 *)ev + EV_FLAGS);
    bit = key < 256 ? 1u << (key & 7) : 0;
    if (!page_on) {
        if (bit && (swallow[key >> 3] & bit) && !(flags & F_DOWN)) {
            swallow[key >> 3] &= (u8)~bit;
            return 1;
        }
        return 0;
    }
    if (flags & F_DOWN) {
        if (bit)
            swallow[key >> 3] |= (u8)bit;
        if (!(flags & F_REPEAT) || key == K_UP || key == K_DOWN)
            page_key(key, flags);
    } else if (bit)
        swallow[key >> 3] &= (u8)~bit;
    return 1;
}

/* ev_enc (native): while the page shows, every turn is taken; LEVEL / DATA moves the cursor */
int dl_enc(void *brain, void *ev)
{
    int e, d;
    (void)brain;
    last_input = ticks;
    if (!page_on)
        return 0;
    if (ev && !box[0] && pending == P_NONE) {
        e = *(const s32 *)((const u8 *)ev + EV_ENC);
        d = *(const s32 *)((const u8 *)ev + EV_DELTA);
        if (e >= ENC_DATA && d)
            move(d > 0 ? 1 : -1);
    }
    return 1;
}

/* ---- drawing ---- */

static void frame_box(void *fb, int x, int y, int w, int h)
{
    core_fill(fb, x, y, w, h, 0);
    core_fill(fb, x, y, w, 1, 1);
    core_fill(fb, x, y + h - 1, w, 1, 1);
    core_fill(fb, x, y, 1, h, 1);
    core_fill(fb, x + w - 1, y, 1, h, 1);
}

static const char *panel_label(int i)
{
    return panel == PN_RIGHT ? act_label(i) : mode == M_DRIVE ? "VIEW BUILD" : "VIEW +DRIVE";
}

static int panel_w(void);
static void draw_edge(void *fb);

static void draw_list(void *fb)
{
    char lab[48], col[16];
    int r, n = list_n();
    draw_edge(fb);
    if (!n) {                            /* the placeholder row */
        core_fill(fb, BAR_L, ROW_Y(0), BAR_R - BAR_L + 1, BAR_H, 1);
        list_text(fb, LABEL_X, ROW_Y(0) + TEXT_DY, TEXT_LEFT | TEXT_INVERT,
                  mode == M_DRIVE ? "NO MODS ON +DRIVE" : "BUILD EMPTY");
        return;
    }
    for (r = 0; r < ROWS && top + r < n; r++) {
        int i = top + r, y = ROW_Y(r), hl = i == cur && panel == PN_NONE, w;
        unsigned inv = hl ? TEXT_INVERT : 0;
        if (hl)
            core_fill(fb, BAR_L, y, BAR_R - BAR_L + 1, BAR_H, 1);
        row_column(i, col, col + sizeof col);
        w = col[0] ? text_w(col) : 0;
        if (col[0])
            list_text(fb, COLUMN_X, y + TEXT_DY, TEXT_RIGHT | inv, col);
        row_label(i, lab, lab + sizeof lab, COLUMN_X - w - STATE_GAP - LABEL_X);
        list_text(fb, LABEL_X, y + TEXT_DY, TEXT_LEFT | inv, lab);
        if (is_ticked(i))                /* the tick: a 3 x 3 square left of the label */
            core_fill(fb, 4, y + 2, 3, 3, !hl);
        if (i == cur && !hl) {           /* a side panel has the keys: the cursor's outline */
            /* MANAGE PROJECTS' outline: open on the pane's side */
            int x0 = panel == PN_LEFT ? panel_w() + 3 : BAR_L, x1 = panel == PN_RIGHT ? SPLIT_X - 3 : BAR_R;
            core_fill(fb, x0, y, x1 - x0 + 1, 1, 1);
            core_fill(fb, x0, y + BAR_H - 1, x1 - x0 + 1, 1, 1);
            core_fill(fb, panel == PN_RIGHT ? x0 : x1, y, 1, BAR_H, 1);
        }
    }
    {                                    /* the thumb: the whole track while every row shows */
        int h = n > ROWS ? 64 * ROWS / n : 64, y;
        if (h < 4)
            h = 4;
        y = n > ROWS ? (64 - h) * top / (n - ROWS) : 0;
        core_fill(fb, TRACK_X, y, 5, h, 0);
        core_fill(fb, TRACK_X, y, 5, 1, 1);
        core_fill(fb, TRACK_X, y + h - 1, 5, 1, 1);
        core_fill(fb, TRACK_X, y, 1, h, 1);
        core_fill(fb, TRACK_X + 4, y, 1, h, 1);
    }
}

/* the edges: on the right MANAGE PROJECTS' dotted scroll track and edge line with the
 * page-arrow mark at mid-height (RIGHT opens a pane), on the left SOUND BROWSER's edge line
 * with its mark (LEFT opens one) */
static void draw_edge(void *fb)
{
    int y;
    core_fill(fb, TRACK_X + 2, 0, 1, 64, 1);
    for (y = 0; y < 64; y += 2) {
        core_fill(fb, TRACK_X + 1, y, 1, 1, 1);
        core_fill(fb, TRACK_X + 3, y, 1, 1, 1);
    }
    /* while a pane is open only its own line carries a mark: the other edge is a plain line */
    if (panel == PN_LEFT) {
        core_fill(fb, EDGE_X, 0, 1, 64, 1);
    } else {
        core_fill(fb, EDGE_X, 0, 1, 29, 1);
        core_fill(fb, EDGE_X, 34, 1, 30, 1);
        core_fill(fb, EDGE_X - 1, 30, 1, 3, 1);
        core_fill(fb, EDGE_X, 31, 1, 1, 1);
    }
    if (panel == PN_RIGHT) {
        core_fill(fb, 0, 0, 1, 64, 1);
    } else {
        core_fill(fb, 0, 0, 1, 29, 1);
        core_fill(fb, 0, 34, 1, 30, 1);
        core_fill(fb, 1, 30, 1, 3, 1);
        core_fill(fb, 0, 31, 1, 1, 1);
    }
}


/* The side panels split the screen as the A4's own panes do (MANAGE PROJECTS' on the right,
 * SOUND BROWSER's on the left): the list stays, cut by a full-height line that carries the
 * page-arrow mark, and the pane's rows from the top beside it, its cursor a 7-row bar. The
 * action pane is right of x SPLIT_X; the left one (VIEW BUILD / VIEW +DRIVE) is as wide as its
 * label, its line at x panel_w() + 1. */
static int panel_w(void)
{
    return text_w(panel_label(0)) + 3;   /* the left pane's bar: x 0..panel_w() - 1 */
}

static void draw_panel(void *fb)
{
    int i, n = panel == PN_RIGHT ? n_acts() : 1;
    int line = panel == PN_RIGHT ? SPLIT_X : panel_w() + 1;
    int x0 = panel == PN_RIGHT ? SPLIT_X + 2 : 0, x1 = panel == PN_RIGHT ? 127 : panel_w() - 1;
    if (pcur >= n)                       /* APPLY & RESTART went with the change that made the
                                          * sets equal again */
        pcur = n - 1;
    if (panel == PN_RIGHT)
        core_fill(fb, SPLIT_X - 1, 0, 128 - SPLIT_X + 1, 64, 0);
    else
        core_fill(fb, 0, 0, line + 2, 64, 0);
    core_fill(fb, line, 0, 1, 29, 1);    /* the line, broken round its mark at mid-height */
    core_fill(fb, line, 30, 1, 3, 1);
    core_fill(fb, line, 34, 1, 30, 1);
    core_fill(fb, panel == PN_RIGHT ? line - 1 : line + 1, 31, 1, 1, 1);
    for (i = 0; i < n; i++) {
        int y = ROW_Y(i);
        unsigned inv = 0;
        if (i == pcur) {
            core_fill(fb, x0, y, x1 - x0 + 1, BAR_H, 1);
            inv = TEXT_INVERT;
        }
        list_text(fb, x0 + 1, y + TEXT_DY, TEXT_LEFT | inv, panel_label(i));
    }
}

/* a centred box of lines split at '\n' (the message box, the confirm window) */
static void draw_lines(void *fb, const char *s)
{
    char line[40];
    int n = 1, i, y, h, k;
    const char *p;
    for (p = s; *p; p++)
        n += *p == '\n';
    if (n > 6)
        n = 6;
    h = n * 8 + 6;
    y = (64 - h) / 2;
    frame_box(fb, 2, y, 124, h);
    for (i = 0, p = s; i < n; i++) {
        for (k = 0; *p && *p != '\n' && k < (int)sizeof line - 1; k++)
            line[k] = *p++;
        line[k] = 0;
        while (*p && *p != '\n')
            p++;
        if (*p)
            p++;
        core_text(fb, 64, y + 4 + i * 8, TEXT_SMALL | TEXT_CENTRE, line);
    }
}

/* ev_draw (native, f(bmp, ctrl)): the whole frame is the page's while it shows */
void dl_draw(void *fb, void *ctrl)
{
    (void)ctrl;
    if (!page_on)
        return;
    drawn_at = ticks;
    core_fill(fb, 0, 0, 128, 64, 0);
    draw_list(fb);
    if (panel != PN_NONE)
        draw_panel(fb);
    if (pending != P_NONE) {
        char t[sizeof msg + 16];
        cat(cat(t, t + sizeof t, msg), t + sizeof t, "\nCONFIRM Y/N");
        draw_lines(fb, t);
    }
    if (box[0])
        draw_lines(fb, box);
}
/* The open folder's files are read and checked one at a time in the background (inspect), one
 * folder at a time as the user opens it (a scan of every folder at once would hold the UI task:
 * remote keys expire after 2 s), one every CHECK_EVERY ticks, only while the page shows and its
 * frames are being drawn (within SHOWN ticks of a draw); each check asks for a frame, so its row
 * is drawn again and the next check follows. A ticked file in another folder is read when a save
 * needs it. */
#define CHECK_EVERY 2
#define SHOWN 10
#define QUIET 20                         /* ticks (1 s) after a key or a turn with no check: a file's
                                          * check holds the UI task, and a key sent meanwhile can
                                          * expire (CB11 unit run 2, 2026-11-08) */

static void check_next(void)
{
    if (!page_on || !lrows || job.what)
        return;
    if ((!reading && ticks % CHECK_EVERY) || ticks - last_input < QUIET)
        return;
    while (next_check < nlrows && (lrows[next_check].kind != R_FILE || files[lrows[next_check].f].checked))
        next_check++;
    if (next_check >= nlrows)
        return;
    if (ticks - drawn_at > SHOWN) {      /* no frame lately: ask for one and wait for it */
        redraw = 1;
        return;
    }
    if (!inspect(&files[lrows[next_check].f]))   /* checked: its row now shows its size or "!" */
        redraw = 1;
}

/* Does the MODS page show? boot.c's crash guard waits for it after a boot that applied sites. */
int dl_page_shown(void)
{
    return page_on;
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
    if (page_on)
        page_rows();
    redraw = 1;
}

/* The background's share of the UI task (decided 2026-11-09, T230): the check's and the save's
 * work on a tick is timed with the bus clock (DTCN0, dl.h "boot time"), and the work then rests
 * for WORK_SHARE - 1 times as long, so it takes at most 1/WORK_SHARE of the UI task whatever the
 * card's speed, and stock's UI event queue drains between stretches. A tick is 50 ms (20 a
 * second). On the unit, 257 files read one every 2 ticks held the UI task long enough that a
 * remote key was refused as busy (queue over 16) and never reached ev_key to start QUIET; the
 * emulator's faster card hid it. */
#ifndef DL_BUS_NOW                        /* (tests/drive_check_tick_host.c has its own) */
#define DL_BUS_NOW() (*(volatile u32 *)0xfc07000c)
#endif
#define WORK_SHARE 4
#define TICK_US 50000u
static u32 rest_until;                   /* the tick the background may work again */

/* APPLY & RESTART, RESTART_TICKS after its YES: the box shows that long (as stock's SUCCESS!
 * REBOOTING), and YES's release, and a remote key's status after it, are answered before the
 * reset (the emulator gate's first try reset inside the remote tap, 2026-11-16). Then the
 * crash guard comes off first, since the restart is the user's and the running set got this far
 * (boot.c keeps it on while the page shows after a boot that applied sites), then dl_lock, held
 * for good, so no route upload or page write is cut between its store calls, then core_restart,
 * which waits for the +Drive store and card and the panel link and UART, and resets. An upload the
 * route hasn't committed is dropped, as at a power cut; the store's directory stays whole. */
#define RESTART_TICKS 40                 /* 2 s */

void dl_guard_off(void);

static void restart_tick(void)
{
    if (!restarting || ticks - restart_asked < RESTART_TICKS)
        return;
    dl_lock();
    dl_guard_off();
    core_restart();
}

void dl_page_tick(void *ctrl)
{
    (void)ctrl;
    ticks++;
    dl_readers_poll(ticks);
    follow_route();
    if ((s32)(ticks - rest_until) >= 0) {
        u32 t0 = DL_BUS_NOW(), us;
        check_next();
        if (job.what && (reading || !(ticks % CHECK_EVERY)) && ticks - last_input >= QUIET)
            job_step();                  /* a file being read goes on each tick it may work */
        us = dl_bus_us(t0, DL_BUS_NOW());
        rest_until = ticks + 1 + (us > 10 * TICK_US ? 10 * TICK_US : us) * (WORK_SHARE - 1) / TICK_US;
    }
    publish_check();
    if (page_on && box[0] && box_until && (s32)(ticks - box_until) >= 0) {
        box[0] = 0;                      /* a box closes by itself */
        redraw = 1;
    }
    if (redraw) {
        redraw = 0;
        if (page_on)
            core_redraw();
    }
    restart_tick();
}

/* from route.c: the page's clock */
unsigned dl_page_now(void)
{
    return ticks;
}
