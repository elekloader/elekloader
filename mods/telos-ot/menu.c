/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * TELOS for the Octatrack, 0.2: the mods on the card as rows of MAIN MENU > MODS (settings-ot 0.2's
 * ot_mod_rows). One row per .elemod file in /ELEKLOADER/MODS, its value what will be against what
 * runs (ON, OFF, +ON, -OFF, ERR), then APPLY. YES on a mod ticks or unticks it: a tick runs the boot's
 * own checks (dl_check) on the set as it would be, and a clash opens a dialog that names the other mod
 * and offers the swap. APPLY writes /ELEKLOADER/MODS/active; the set changes at the next start.
 * The design: research/telos-ot/ON-THE-UNIT.md.
 */
#include "../drive-loader-ar2/dl.h"
#include "octatrack/core.h"

extern char core_desc[];
extern u8 drive_staging[];
extern char drive_staging_size[];
extern struct dl_choice telos_choice[];
extern int telos_nchoice;
void telos_heap_reset(void);

#define FS_OPENDIR (*(int (*volatile *)(const char *, u32 *))0x46c8240e)   /* FAT: (path, iterator) -> 0 */
#define FS_READDIR (*(int (*volatile *)(u32 *, u8 *))0x46c8244e)          /* -> 0: an entry */
#define ENT_DIR 269                                  /* the entry's directory flag (0x4007f344) */
#define DIALOG ((void (*)(const char *, u32, const char *const *, u32, void (*)(int)))0x4006d57c)
#define MENU_REDRAW ((void (*)(void))0x40064d7c)

#define NMOD 32
#define TITLE 16
struct cmod {
    char name[64];                       /* the file, below /ELEKLOADER/MODS */
    char id[DL_ID_MAX];
    char title[TITLE];
    u32 crc;
    u8 boot_on, on;                      /* in active at boot; ticked now */
    u8 state, code;                      /* the boot's record (DL_ST_*, DL_R_*) */
};
static struct cmod cm[NMOD];
static int ncm;
static u8 dirent[512];

/* ---- reading the card ---------------------------------------------------------------------- */
static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

static int is_elemod(const char *s)
{
    u32 n = dl_strlen(s), i;
    static const char ext[] = ".elemod";
    if (n <= 7 || s[0] == '.')
        return 0;
    for (i = 0; i < 7; i++)
        if (lower(s[n - 7 + i]) != ext[i])
            return 0;
    return 1;
}

/* A top-level string value of the file's first bytes ("id", "title"): enough for both, which the
 * converter and the SDK write near the top. */
static void field(const u8 *b, u32 len, const char *key, char *out, u32 max, int upper)
{
    u32 k = dl_strlen(key), i, j;
    out[0] = 0;
    if (len > 4096)
        len = 4096;
    for (i = 0; i + k + 4 < len; i++) {
        if (b[i] != '"' || b[i + k + 1] != '"')
            continue;
        for (j = 0; j < k && b[i + 1 + j] == (u8)key[j]; j++)
            ;
        if (j < k)
            continue;
        i += k + 2;
        while (i < len && (b[i] == ' ' || b[i] == ':'))
            i++;
        if (i >= len || b[i] != '"')
            return;
        for (i++, j = 0; i < len && b[i] != '"' && j + 1 < max; i++, j++)
            out[j] = (char)(upper && b[i] >= 'a' && b[i] <= 'z' ? b[i] - 32 : b[i]);
        out[j] = 0;
        return;
    }
}

static int same(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
}

/* At the card's first mount, after the boot's link (telos.c): the files, their titles and CRCs,
 * and what the boot did with each. */
void telos_menu_scan(void)
{
    u32 it[4];
    int i, k;
    ncm = 0;
    if (FS_OPENDIR("/ELEKLOADER/MODS", it) != 0)
        return;
    while (ncm < NMOD && FS_READDIR(it, dirent) == 0) {
        struct cmod *m = &cm[ncm];
        s32 len;
        if (dirent[ENT_DIR] || !is_elemod((const char *)dirent))
            continue;
        dl_memset(m, 0, sizeof *m);
        for (i = 0; dirent[i] && i < 63; i++)
            m->name[i] = (char)dirent[i];
        len = dl_read_file(m->name, drive_staging, (u32)(uintptr_t)drive_staging_size);
        if (len <= 0)
            continue;
        m->crc = dl_crc32(drive_staging, (u32)len);
        field(drive_staging, (u32)len, "id", m->id, DL_ID_MAX, 0);
        field(drive_staging, (u32)len, "title", m->title, TITLE, 1);
        if (!m->title[0])
            for (i = 0; m->id[i] && i < TITLE - 1; i++)
                m->title[i] = m->id[i];
        for (k = 0; k < telos_nchoice; k++)
            if (same(telos_choice[k].name, m->name)) {
                m->boot_on = m->on = 1;
                if (dl_status && (u32)k < dl_status_n) {
                    m->state = dl_status[k].state;
                    m->code = dl_status[k].code;
                }
            }
        ncm++;
    }
    for (i = 1; i < ncm; i++) {              /* by title */
        struct cmod x = cm[i];
        for (k = i; k > 0 && dl_strcmp(x.title, cm[k - 1].title) < 0; k--)
            cm[k] = cm[k - 1];
        cm[k] = x;
    }
}

/* ---- the rows ---------------------------------------------------------------------------------- */
static void put(char *buf, int n, const char *s)
{
    int i;
    for (i = 0; s[i] && i < n - 1; i++)
        buf[i] = s[i];
    buf[i] = 0;
}

static int changes(void)
{
    int i, c = 0;
    for (i = 0; i < ncm; i++)
        c += cm[i].on != cm[i].boot_on;
    return c;
}

static int rows_count(void) { return ncm ? ncm + 1 : 0; }

/* The state leads the label (the value column starts at a fixed place, and the list's scroll arrow
 * covers its third letter on the top and bottom rows): "ON  TUNER", "+ON SCENES P2". */
static const char *state_of(int i)
{
    const struct cmod *m = &cm[i];
    int running = m->boot_on && m->state == DL_ST_LOADED;
    return m->on ? (running ? "ON   " : m->boot_on ? "ERR  " : "+ON  ") : (running ? "-OFF " : "OFF  ");
}

static void rows_label(int i, char *buf, int n)
{
    char t[40];
    int k, c;
    if (i >= ncm) {
        c = changes();
        put(t, sizeof t, c ? "APPLY: 0 CHANGES" : "APPLY");
        if (c)
            t[7] = (char)('0' + (c > 9 ? 9 : c));
        if (c == 1)
            t[15] = 0;                   /* "CHANGE" */
        put(buf, n, t);
        return;
    }
    put(t, sizeof t, state_of(i));
    for (k = 0; cm[i].title[k] && 5 + k < (int)sizeof t - 1; k++)
        t[5 + k] = cm[i].title[k];
    t[5 + k] = 0;
    put(buf, n, t);
}

static void rows_value(int i, char *buf, int n)
{
    (void)i;
    put(buf, n, "");
}

/* ---- the check: the boot's own rules on the set as it would be --------------------------------- */
static struct dl_choice set[NMOD];
static int set_idx[NMOD];
static struct dl_status cst[NMOD];
static char cdet[NMOD][32];

static void add(int *n, int i)
{
    put(set[*n].name, DL_LINE_NAME_MAX, cm[i].name);
    set[*n].crc = cm[i].crc;
    set_idx[(*n)++] = i;
}

/* Check the ticked mods plus `with` (or a pair). -> the set's size; cst/cdet hold the verdicts. */
static int check(int with, int only)
{
    int n = 0, i;
    if (only >= 0) {
        add(&n, with);
        add(&n, only);
    } else {
        for (i = 0; i < ncm; i++)
            if (cm[i].on || i == with)
                add(&n, i);
    }
    telos_heap_reset();
    if (dl_check((u32)(uintptr_t)core_desc, set, n, drive_staging, (u32)(uintptr_t)drive_staging_size,
                 cst, cdet, 0) < 0)
        return -1;
    return n;
}

static int verdict(int n, int mod)       /* its record in the last check, or -1 */
{
    int k;
    for (k = 0; k < n; k++)
        if (set_idx[k] == mod)
            return k;
    return -1;
}

static const char *reason(int code)
{
    switch (code) {
    case DL_R_MISSING: case DL_R_CHANGED: return "FILE CHANGED";
    case DL_R_TOO_BIG: return "FILE TOO BIG";
    case DL_R_JSON: case DL_R_FORMAT: return "NOT A MOD FILE";
    case DL_R_TARGET: return "FOR ANOTHER OS";
    case DL_R_FEATURE: return "NEEDS A FLASHED BUILD";
    case DL_R_DUPLICATE: return "SAME MOD";
    case DL_R_CORE: return "NEEDS A NEWER CORE";
    case DL_R_REQUIRES: case DL_R_IMPORT: return "NEEDS A MOD NOT HERE";
    case DL_R_CONFLICTS: return "THEY CONFLICT";
    case DL_R_EXPORT: return "SAME NAMES";
    case DL_R_CLAIM: return "SAME RESOURCE";
    case DL_R_TABLE: return "NEEDS A FLASHED TABLE";
    case DL_R_FULL: return "A TABLE IS FULL";
    case DL_R_ROOM: return "NO ROOM IN THE AREA";
    case DL_R_SITE: return "SAME CODE";
    case DL_R_STOCK: return "CODE NOT STOCK";
    case DL_R_FLASHED: return "CHANGES FLASHED CODE";
    default: return "REFUSED";
    }
}

static int clash_code(int c)
{
    return c == DL_R_DUPLICATE || c == DL_R_CONFLICTS || c == DL_R_EXPORT || c == DL_R_CLAIM
           || c == DL_R_SITE;
}

static char line1[32], line2[32];
static const char *lines[3];
static int pend_on, pend_off;
static u16 redraw_in;
#define DIALOG_OPEN (*(volatile u32 *)0x460e5cd0)          /* a stock confirmation dialog is open */
#define MENU_OPEN (*(volatile u32 *)0x400cbf4c)            /* the MAIN MENU's window */

/* ev_tick, the sys task, 60 a second: the redraw a dialog's answer asked for, once the dialog has
 * gone and the menu is back (it keeps trying for 5 s). */
void telos_tick(void)
{
    if (!redraw_in)
        return;
    if (redraw_in > 1000) {              /* the second redraw, 3 ticks after the first */
        if (--redraw_in == 1000) {
            redraw_in = 0;
            MENU_REDRAW();
        }
        return;
    }
    if (--redraw_in > 300 - 6 || DIALOG_OPEN || !MENU_OPEN)
        return;
    /* twice: the menu draws a row's label before its getter, which is where settings-ot asks the
     * rows again, so the first redraw fills the labels and the second shows them */
    redraw_in = redraw_in > 1000 ? 0 : 1000 + 3;
    MENU_REDRAW();
}
static void noop_answer(int c) { (void)c; }

static void swap_answer(int c)
{
    if (c == 0 && pend_on >= 0 && pend_off >= 0) {       /* YES */
        cm[pend_off].on = 0;
        cm[pend_on].on = 1;
    }
    pend_on = pend_off = -1;
    redraw_in = 300;                     /* after the dialog has put the menu back */
}

static void say(const char *a, const char *b, const char *c, void (*answer)(int))
{
    put(line1, sizeof line1, a);
    put(line2, sizeof line2, b);
    lines[0] = line1;
    lines[1] = line2;
    lines[2] = c;
    DIALOG("TELOS", 3, lines, 3, answer);
}

static void cat(char *d, int n, const char *a, const char *b)
{
    int i = 0, j = 0;
    while (a[j] && i < n - 1)
        d[i++] = a[j++];
    for (j = 0; b[j] && i < n - 1; j++)
        d[i++] = b[j];
    d[i] = 0;
}

static void tick(int i)
{
    int n = check(i, -1), k, other = -1, code = 0;
    char who[32];
    if (n < 0) {
        say("NO MEMORY FOR THE CHECK", "", "", noop_answer);
        return;
    }
    k = verdict(n, i);
    if (k >= 0 && cst[k].state == DL_ST_LOADED) {
        /* it would load: did it push a ticked mod out? */
        for (k = 0; k < n; k++)
            if (set_idx[k] != i && cst[k].state != DL_ST_LOADED && clash_code(cst[k].code)
                && cm[set_idx[k]].on) {
                other = set_idx[k];
                code = cst[k].code;
                break;
            }
        if (other < 0) {
            cm[i].on = 1;
            return;
        }
    } else if (k >= 0) {
        code = cst[k].code;
        if (clash_code(code))            /* which ticked mod: the pair that fails */
            for (k = 0; k < ncm && other < 0; k++)
                if (k != i && cm[k].on) {
                    int m = check(i, k), a, b;
                    a = verdict(m, i);
                    b = verdict(m, k);
                    if (m > 0 && ((a >= 0 && cst[a].state != DL_ST_LOADED)
                                  || (b >= 0 && cst[b].state != DL_ST_LOADED)))
                        other = k;
                }
    }
    if (other >= 0) {
        cat(who, sizeof who, "CLASHES WITH ", cm[other].title);
        pend_on = i;
        pend_off = other;
        say(who, reason(code), "YES: SWAP  NO: CANCEL", swap_answer);
        return;
    }
    cat(who, sizeof who, "CAN'T LOAD ", cm[i].title);
    say(who, reason(code), "FROM THE CARD", noop_answer);
}

/* ---- APPLY: the ticked files into active, on the engine task ------------------------------------- */
static char text[NMOD * (DL_LINE_NAME_MAX + 12)];
static struct ot_file af;

static void hex8(char *d, u32 v)
{
    static const char dg[] = "0123456789abcdef";
    int i;
    for (i = 0; i < 8; i++)
        d[i] = dg[(v >> (28 - 4 * i)) & 15];
}

static void write_active(u32 unused)
{
    u32 n = 0, j;
    int i;
    (void)unused;
    for (i = 0; i < ncm; i++)
        if (cm[i].on) {
            for (j = 0; cm[i].name[j]; j++)
                text[n++] = cm[i].name[j];
            text[n++] = ' ';
            hex8(text + n, cm[i].crc);
            n += 8;
            text[n++] = '\n';
        }
    if (ot_file_open(&af, "/ELEKLOADER/MODS/active", "w") >= 0) {
        ot_file_write(&af, text, n);
        ot_file_close(&af);
    }
}

static void rows_action(int i)
{
    if (i >= ncm) {
        ot_engine_post(write_active, 0);
        say("SAVED TO THE CARD", "APPLIES AT THE NEXT START", "", noop_answer);
        return;
    }
    if (cm[i].on)
        cm[i].on = 0;
    else
        tick(i);
}

const struct ot_mod_rows telos_rows = {
    OT_MOD_ROWS_ABI, "CARD MODS", rows_count, rows_label, rows_value, rows_action,
};
