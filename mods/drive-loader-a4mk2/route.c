/* SPDX-License-Identifier: GPL-2.0-or-later */
/* drive-loader-a4mk2 (Analog Four MKII, OS 1.56): the /mods route of stock's Data router (README,
 * "The /mods route"), drive-loader-ar2's route over the loader's own store (store.c). A
 * RouteTypeHandler added to the router's resolver (route.s) lists DL_DIR's files plus .build,
 * .status and .store, and resolves /mods/<name> to a +Drive entry (kind 2) over the file's own
 * sectors. Stock's +Drive reader streams a read straight from them; an upload goes straight into a
 * free run the store reserves when stock makes the writer (+0x3c), and the commit (+0x4c, from
 * WriteClose after the content hash passed) makes it the file, replacing any older one: there is
 * no RAM stage (decided 2026-11-14). A Clear deletes the file.
 *
 * The entry's functions, in stock's order for an upload (trace-phase0-sites.md, section 2): +0x1c
 * (the entry's copy, its +0x10 already the header's payload size) checks and keeps the copy in the
 * closure; +0x3c (kind 2 only, when the writer is made) reserves the run and writes its byte offset
 * into the copy's +0x0c, which the writer reads next; +0x6c checks the header after the first
 * chunk; +0x4c commits. +0x1c also runs before a read, and +0x3c alone before a Clear's writer, so
 * a reservation is made only when +0x1c ran first for the same resolution of the entry. Each
 * function slot holds its own copy of the closure, so that state is the route's (`up`, one upload
 * at a time per Data handler, lesson 2.6), tagged with the resolution's serial, not the closure's.
 *
 * Calls come from the SysEx receive tasks, never the UI task. Struct results come back through a
 * hidden pointer in %a0, which route.s's thunks pass to these functions as their first argument. */

#include "fs.h"

/* std::string(cstr, allocator) and its dtor (core-a4mk2's screen.c uses the same two); operator new
 * and delete. tests/drive_a4_route_host.c brings its own. */
#ifndef STR_NEW
#define STR_NEW ((void (*)(void *, const char *, void *))0x4018471e)
#define STR_DEL ((void (*)(void *))0x40182c6e)
#endif
#ifndef NEW
#define NEW ((void *(*)(unsigned))0x400653b8)  /* operator new (core's core_new) */
#define DELETE ((void (*)(void *))0x400653c4)  /* operator delete */
#endif

/* the resolver's route tables: the pattern parse (&pattern, &segments) */
#define PATTERN ((int (*)(void *, void *))0x4001ab98)
#define LIST_TABLE 0x0c
#define ENTRY_TABLE 0x18

#define EMOD 0x454d4f44u                       /* 'EMOD', the file type of a /mods upload */
#define TYPE_DESC 0x454c4b44u                  /* 'ELKD', .build */
#define TYPE_TEXT 0x54455854u                  /* 'TEXT', .status */
#define KIND_RAM 1
#define KIND_DRIVE 2                           /* +0x0c is a byte offset on the +Drive */
#define P_READ 0x2
#define P_WRITE 0x4
#define P_CLEAR 0x8

/* the router's entry descriptor (0x7c bytes) */
#define E_TYPE 0x00
#define E_KIND 0x04
#define E_PERM 0x08
#define E_ADDR 0x0c
#define E_SIZE 0x10
#define E_INDEX 0x14
#define E_VERSION 0x18
#define E_PRE 0x1c                             /* expected<void,string>(const Entry *) */
#define E_FILL 0x2c                            /* void(), after a read stream is made */
#define E_RESERVE 0x3c                         /* void(), kind 2: before the +Drive writer is made */
#define E_CHANGE 0x4c                          /* void(const u8 *header or 0) */
#define E_HEADER 0x6c                          /* expected<bool,string>(const u8 *header) */
#define ENTRY_SIZE 0x7c

/* the data container's 31-byte header, as the router hands it over */
#define H_TYPE 0x0d
#define H_PAYLOAD 0x19
#define H_COMPRESSED 0x1d

extern const unsigned char core_desc[];
extern unsigned char drive_staging[];
extern volatile int dl_staging_owner;
extern int dl_list_inv(), dl_entry_inv(), dl_pre_inv(), dl_hdr_inv();

/* the boot linker's status records: pointers to dl_status_n of them on the heap (0 until
 * it links) */
struct dl_status_rec {
    unsigned char state, code;
    unsigned short arg;
    char id[28];
};
extern struct dl_status_rec *dl_status __attribute__((weak));
extern int dl_status_n __attribute__((weak));
extern unsigned char dl_skip_reason __attribute__((weak));
extern char (*dl_detail)[32] __attribute__((weak));     /* a refusal's name argument */
extern unsigned dl_boot_us[] __attribute__((weak));     /* the boot's time: whole, checks, link, inits */
/* the sites the boot applied (dl.h, struct dl_site): address, length, its mod's line */
struct dl_site_rec {
    unsigned addr;
    unsigned short len, line;
    unsigned orig;
};
extern struct dl_site_rec *dl_sites __attribute__((weak));
extern unsigned dl_applied_n __attribute__((weak));
extern volatile int dl_check_n __attribute__((weak));   /* the MODS page's check: files done, -1 none yet */
extern volatile int dl_check_all __attribute__((weak)); /* ... of the open folder's files */
extern char dl_check_dir[] __attribute__((weak));       /* ... that folder, below DL_DIR */
extern unsigned char dl_store_io __attribute__((weak)); /* /mods/.store: 0 none, 1 kept, 2 written, 3 failed */
extern unsigned dl_store_info[] __attribute__((weak));  /* ... the live layout ID, fields, parked fields */

static char dl_note[64];
int dl_last_io[4];                             /* the last commit: payload size, sector, its result */
static char *dl_status_text;                  /* from the heap, sized to the boot's records */
static unsigned dl_status_cap;
#define STATUS_HEAD 640                        /* the lines before the records */
#define STATUS_LINE 84                         /* id 28, state, code, arg, detail 32, at most */
#define STATUS_SITE 56                         /* "site ", id 28, " 0x", 8 digits, " ", len */

/* ---- small helpers (no libc) ---- */

static unsigned len(const char *s)
{
    unsigned n = 0;
    while (s[n])
        n++;
    return n;
}

static void zero(void *p, unsigned n)
{
    unsigned char *b = p;
    while (n--)
        *b++ = 0;
}

static void copy(void *d, const void *s, unsigned n)
{
    unsigned char *a = d;
    const unsigned char *b = s;
    while (n--)
        *a++ = *b++;
}

static int same(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
}

static int ends(const char *s, const char *tail)
{
    unsigned n = len(s), k = len(tail);
    return n > k && same(s + n - k, tail);
}

static unsigned be32(const unsigned char *p)
{
    return (unsigned)p[0] << 24 | (unsigned)p[1] << 16 | (unsigned)p[2] << 8 | p[3];
}

static void put32(unsigned char *p, unsigned v)
{
    *(unsigned *)p = v;
}

/* a C string's end, for the text builders */
static char *cat(char *at, char *end, const char *s)
{
    while (*s && at < end - 1)
        *at++ = *s++;
    *at = 0;
    return at;
}

static char *hex8(char *at, char *end, unsigned v)
{
    static const char hx[] = "0123456789abcdef";
    int k;
    at = cat(at, end, "0x");
    for (k = 28; k >= 0 && at < end - 1; k -= 4)
        *at++ = hx[(v >> k) & 15];
    *at = 0;
    return at;
}

static char *num(char *at, char *end, unsigned v)
{
    char t[12];
    int i = 0;
    do
        t[i++] = '0' + v % 10;
    while ((v /= 10) && i < 11);
    while (i && at < end - 1)
        *at++ = t[--i];
    *at = 0;
    return at;
}

static void path_of(char *out, const char *name, const char *suffix)
{
    char *end = out + DL_PATH_MAX, *at = cat(out, end, DL_DIR "/");
    at = cat(at, end, name);
    cat(at, end, suffix);
}

static void note(const char *what, const char *name)
{
    char *end = dl_note + sizeof dl_note, *at = cat(dl_note, end, what);
    cat(cat(at, end, " "), end, name);
}

static void set_str(unsigned char *slot, const char *msg)
{
    STR_NEW(slot, msg, 0);
}

static void ok_str(unsigned char *slot)
{
    STR_NEW(slot, "", 0);
}

/* ---- the std::functions an entry carries ---- */

/* a closure: what the function is for and the file's name; on the heap, as stock's are, so the
 * router may copy and destroy entries as it likes */
enum { C_FILE = 1, C_BUILD, C_STATUS, C_DIR, C_MKDIR, C_MOVE };
#define MOVE_MAX (2 * (DL_NAME_MAX + 1))    /* a .mv upload: "FROM\nTO\n" */
struct clo {
    int what;
    char name[DL_NAME_MAX + 1];           /* a path below DL_DIR */
    unsigned size;                        /* a file's size when its entry was made */
    unsigned char exists;                 /* a file of that name was there */
    unsigned serial;                      /* the resolution (dl_entry call) the entry came from */
};

/* The upload in flight: one at a time per Data handler (lesson 2.6), so one record, shared by all
 * of an entry's functions. Each std::function slot holds a closure of its own (set_fn, and the
 * manager's clone copies again), so nothing one function writes into its closure reaches another:
 * +0x1c keeps the entry copy here, tagged with its resolution's serial, and +0x3c takes it only
 * when its own closure carries the same serial. +0x3c fills the rest with the run the store
 * reserved, +0x4c commits it when the name and the payload size match. Under dl_lock. */
static struct {
    unsigned serial;                      /* +0x1c ran for this resolution: pre is its copy */
    unsigned char *pre;
    char name[DL_NAME_MAX + 1];
    unsigned token, sector, size;
} up;
static unsigned dl_serial;                /* the last resolution's serial; 0 never */

/* the manager of every closure here: 0 type, 1 pointer, 2 clone, 3 destroy */
static const int dl_typeinfo[2];
int dl_clo_mgr(void **dest, void *const *src, int op)
{
    switch (op) {
    case 0:
        *dest = (void *)dl_typeinfo;
        break;
    case 1:
        *dest = *src;
        break;
    case 2: {
        struct clo *c = NEW(sizeof *c);
        copy(c, *src, sizeof *c);
        *dest = c;
        break;
    }
    case 3:
        DELETE(*dest);
        break;
    }
    return 0;
}

/* the manager of the route tables' two functions: no closure at all */
int dl_static_mgr(void **dest, void *const *src, int op)
{
    if (op == 0)
        *dest = (void *)dl_typeinfo;
    else if (op == 1 || op == 2)
        *dest = *src;
    return 0;
}

static void set_fn(unsigned char *slot, const struct clo *proto, void *inv)
{
    struct clo *c = NEW(sizeof *c);
    copy(c, proto, sizeof *c);
    put32(slot, (unsigned)c);
    put32(slot + 4, 0);
    put32(slot + 8, (unsigned)dl_clo_mgr);
    put32(slot + 12, (unsigned)inv);
}

/* the byte offset a file entry points at with no run of its own (a new name, an empty file): the
 * store's scratch sector, so no writer stock makes for it can reach a file */
#define SCRATCH_AT (DS_SCRATCH * 512u)

/* +0x1c, before a read stream and before an upload's first chunk: {u8 ok, string msg}. For an
 * upload the entry stock hands over is its copy with the header's payload size at +0x10; stock
 * writes the first chunk's body before the +0x6c check, so this is where an upload that can't fit
 * is refused (lesson 2.5). A read's size is the file's: not refused for room. The copy is kept in
 * `up` for +0x3c, which stock calls next on the same copy when it makes a writer. */
unsigned dl_pre(unsigned char *res, void **f, unsigned char *entry)
{
    struct clo *c = *f;
    unsigned size = *(const unsigned *)(entry + E_SIZE), largest, total;
    zero(res, 8);
    if (dl_staging_owner) {
        set_str(res + 4, "Busy: the loader is using /mods");
        return (unsigned)res;
    }
    if (c->what != C_FILE) {              /* .dir and .mv: the floor holds them */
        if (size > (c->what == C_MOVE ? MOVE_MAX : DL_STAGING_SIZE)) {
            set_str(res + 4, "Too large");
            return (unsigned)res;
        }
    } else {
        ds_room(&largest, &total);
        if (size > largest && !(c->exists && size == c->size)) {
            set_str(res + 4, "No room on the +Drive (see /mods/.status, room)");
            return (unsigned)res;
        }
        dl_lock();
        up.serial = c->serial;
        up.pre = entry;
        dl_unlock();
    }
    res[0] = 1;
    ok_str(res + 4);
    return (unsigned)res;
}

/* +0x3c, kind 2, before stock makes the +Drive writer, which then reads +0x0c and +0x10 from the
 * same copy: reserve the run and point the copy at it. Without +0x1c first in the same resolution
 * (a Clear resolves on its own) nothing changes: the writer stays inside the file's own run. When
 * no run is free the copy is pointed at the scratch sector with size 0, so the writer writes
 * nothing and the upload fails its count. */
void dl_reserve(void **f)
{
    struct clo *c = *f;
    unsigned char *e;
    unsigned size, token, at;
    if (c->what != C_FILE)
        return;
    dl_lock();
    e = up.serial == c->serial ? up.pre : 0;
    up.serial = 0;
    up.pre = 0;
    dl_unlock();
    if (!e)
        return;
    size = *(const unsigned *)(e + E_SIZE);
    at = size ? ds_reserve(size, &token) : 0;
    dl_lock();
    zero(&up, sizeof up);
    if (at) {
        copy(up.name, c->name, len(c->name) + 1);
        up.token = token;
        up.sector = at;
        up.size = size;
        put32(e + E_ADDR, at * 512u);
    } else {
        put32(e + E_ADDR, SCRATCH_AT);
        put32(e + E_SIZE, 0);
    }
    dl_unlock();
}

/* +0x2c, a read stream is made: nothing to fill, the reader streams from the sectors */
void dl_fill(void **f)
{
    (void)f;
}

/* +0x6c, the container's header has arrived: {u8 ok, string msg +4, u8 value +8}. */
unsigned dl_hdr(unsigned char *res, void **f, const unsigned char *h)
{
    const char *why = 0;
    const struct clo *c = *f;
    unsigned n = be32(h + H_PAYLOAD);
    zero(res, 12);
    if (be32(h + H_TYPE) != EMOD)
        why = "Invalid file type";
    else if (h[H_COMPRESSED])
        why = "Compressed uploads are not supported";
    else if (c->what == C_FILE ? !up.token || n > up.size || !same(up.name, c->name)
             : n > (c->what == C_MOVE ? MOVE_MAX : DL_STAGING_SIZE))
        why = "Invalid payload size";
    else if (dl_staging_owner)
        why = "Busy: the loader is using /mods";
    if (why) {
        set_str(res + 4, why);
        return (unsigned)res;
    }
    res[0] = 1;
    ok_str(res + 4);
    res[8] = 1;
    return (unsigned)res;
}

static int make_folder(const char *rel);
static int make_parent(const char *rel);
extern volatile unsigned dl_fs_gen;
extern volatile int dl_move_seen;
static const char *move(unsigned n);

/* +0x4c: after an upload (the header) or a Clear (0). It can't refuse: a failure is noted for
 * .status. */
void dl_change(void **f, const unsigned char *h)
{
    struct clo *c = *f;
    char path[DL_PATH_MAX];
    unsigned n;
    int r;
    if (c->what == C_DIR && !h) {          /* a Clear of a folder: empty only */
        path_of(path, c->name, "");
        dl_lock();
        note(dl_fs_rmdir(path) ? "rmdir failed (not empty?):" : "removed", c->name);
        dl_fs_gen++;
        dl_unlock();
        return;
    }
    if (c->what == C_MOVE) {               /* an upload of "FROM\nTO" to .mv: move or rename, writing no file */
        const char *why;
        if (!h)
            return;
        dl_lock();
        n = be32(h + H_PAYLOAD);
        if (dl_staging_owner || n > MOVE_MAX)
            note("move dropped:", "");
        else if ((why = move(n)) != 0)
            note("move:", why);
        else
            note("moved to", (const char *)drive_staging + len((const char *)drive_staging) + 1);
        dl_unlock();
        return;
    }
    if (c->what == C_MKDIR) {              /* an upload to FOLDER/.dir: make FOLDER, writing no file */
        if (h) {
            dl_lock();
            note(make_folder(c->name) ? "mkdir failed:" : "made", c->name);
            dl_fs_gen++;
            dl_unlock();
        }
        return;
    }
    if (c->what != C_FILE)
        return;
    path_of(path, c->name, "");
    dl_lock();
    if (!h) {
        note(dl_fs_delete(path) ? "delete failed:" : "deleted", c->name);
        dl_fs_gen++;
        dl_unlock();
        return;
    }
    n = be32(h + H_PAYLOAD);
    dl_last_io[0] = (int)n;
    dl_last_io[1] = (int)up.sector;
    if (dl_staging_owner || !up.token || !same(up.name, c->name) || n > up.size) {
        note("upload dropped:", c->name);
        r = -2;
    } else if (make_parent(c->name)) {    /* the store keeps no file without its folder */
        note("commit failed: no folder for", c->name);
        r = -3;
    } else {
        r = ds_commit(path, up.token, n);
        note(r ? "commit failed:" : "saved", c->name);
    }
    dl_last_io[2] = r;
    zero(&up, sizeof up);
    dl_fs_gen++;
    dl_unlock();
}

/* ---- .status ---- */

static const char *const dl_state_names[] = {"-", "loaded", "refused", "dropped"};
static const char *const dl_skip_names[] = {
    "-", "key held", "after a failed start", "drive not ready", "descriptor ABI",
    "unstable", "more than 256 lines", "no memory"};

/* -> 0 when the heap has no room for the text */
static int status_text(void)
{
    char *end, *at;
    int i, n = &dl_status_n ? dl_status_n : 0;
    unsigned want;
    if (!&dl_status || !dl_status)
        n = 0;
    want = STATUS_HEAD + STATUS_LINE * (unsigned)n
         + STATUS_SITE * (&dl_applied_n && &dl_sites && dl_sites ? dl_applied_n : 0);
    if (want > dl_status_cap) {              /* the records are the boot's: this happens once */
        char *t = NEW(want);
        if (!t)
            return 0;
        if (dl_status_text)
            DELETE(dl_status_text);
        dl_status_text = t;
        dl_status_cap = want;
    }
    end = dl_status_text + dl_status_cap;
    at = dl_status_text;
    at = cat(at, end, "drive-loader-a4mk2 route\nfolder " DL_DIR "\nlast: ");
    at = cat(at, end, dl_note[0] ? dl_note : "-");
    at = cat(at, end, "\nio:");
    for (i = 0; i < 4; i++) {
        at = cat(at, end, " ");
        at = num(at, end, (unsigned)dl_last_io[i]);
    }
    at = cat(at, end, "\ncheck: ");     /* the MODS page's background check (page.c, decided 2026-11-09) */
    if (&dl_check_n && dl_check_n >= 0) {
        at = num(at, end, (unsigned)dl_check_n);
        at = cat(at, end, "/");
        at = num(at, end, (unsigned)dl_check_all);
        at = cat(at, end, " " DL_DIR);
        if (dl_check_dir[0])
            at = cat(cat(at, end, "/"), end, dl_check_dir);
    } else
        at = cat(at, end, "-");
    at = cat(at, end, "\n");
    if (&dl_boot_us) {
        static const char *const part[] = {"boot: ", " us (checks ", ", link ", ", inits "};
        for (i = 0; i < 4; i++) {
            at = cat(at, end, part[i]);
            at = num(at, end, dl_boot_us[i]);
        }
        at = cat(at, end, ")\n");
    }
    {                                  /* the store: the largest upload that fits, and the rest */
        unsigned largest, total;
        ds_room(&largest, &total);
        at = cat(at, end, "room: ");
        at = num(at, end, largest);
        at = cat(at, end, " bytes in one run, ");
        at = num(at, end, total);
        at = cat(at, end, " free; directory generation ");
        at = num(at, end, ds_gen);
        at = cat(at, end, "\n");
    }
    if (&dl_store_io && dl_store_io) {   /* +Drive storage (boot.c, dstore.h) */
        static const char *const io[] = {"-", "kept", "written", "write failed"};
        at = cat(at, end, "store: id ");
        at = hex8(at, end, dl_store_info[0]);
        at = cat(at, end, ", ");
        at = num(at, end, dl_store_info[1]);
        at = cat(at, end, " fields, ");
        at = num(at, end, dl_store_info[2]);
        at = cat(at, end, " parked, ");
        at = cat(at, end, dl_store_io < 4 ? io[dl_store_io] : "-");
        at = cat(at, end, "\n");
    }
    if (&dl_skip_reason && dl_skip_reason) {
        at = cat(at, end, "skipped: ");
        if (dl_skip_reason < sizeof dl_skip_names / sizeof dl_skip_names[0])
            at = cat(at, end, dl_skip_names[dl_skip_reason]);
        else
            at = num(at, end, dl_skip_reason);
        at = cat(at, end, "\n");
    }
    for (i = 0; i < n; i++) {
        const struct dl_status_rec *s = &dl_status[i];
        char id[29];
        copy(id, s->id, 28);
        id[28] = 0;
        at = cat(at, end, id[0] ? id : "?");
        at = cat(at, end, " ");
        at = cat(at, end, s->state < 4 ? dl_state_names[s->state] : "?");
        at = cat(at, end, " ");
        at = num(at, end, s->code);
        at = cat(at, end, " ");
        at = num(at, end, s->arg);
        if (&dl_detail && dl_detail && dl_detail[i][0]) {
            char d[33];
            copy(d, dl_detail[i], 32);
            d[32] = 0;
            at = cat(cat(at, end, " "), end, d);
        }
        at = cat(at, end, "\n");
    }
    if (&dl_applied_n && &dl_sites && dl_sites) {   /* each applied site, by its mod, in id order */
        unsigned k;
        for (k = 0; k < dl_applied_n; k++) {
            const struct dl_site_rec *t = &dl_sites[k];
            char id[29];
            int d;
            if (t->line < n)
                copy(id, dl_status[t->line].id, 28);
            else
                id[0] = 0;
            id[28] = 0;
            at = cat(cat(at, end, "site "), end, id[0] ? id : "?");
            at = cat(at, end, " 0x");
            for (d = 28; d >= 0; d -= 4) {
                char h[2] = {"0123456789abcdef"[t->addr >> d & 15], 0};
                at = cat(at, end, h);
            }
            at = cat(at, end, " ");
            at = num(at, end, t->len);
            at = cat(at, end, "\n");
        }
    }
    return 1;
}

/* ---- the routes ---- */

#include "names.inc"

static void *heap_ents(int n)
{
    return NEW((unsigned)(n > 0 ? n : 1) * sizeof(struct dl_ent));
}

/* the path below DL_DIR names a folder there (its parent lists it as one); under dl_lock */
static int is_folder(const char *rel)
{
    char parent[DL_NAME_MAX + 1];
    const char *base = rel;
    unsigned i, k = len(rel);
    struct dl_ent *ents;
    int n, j, r = 0;
    for (i = 0; i < k; i++)
        if (rel[i] == '/')
            base = rel + i + 1;
    copy(parent, rel, (unsigned)(base - rel));
    parent[base > rel ? base - rel - 1 : 0] = 0;
    n = dl_list_dir(parent, 0, 0);
    ents = heap_ents(n);
    n = dl_list_dir(parent, ents, n);
    for (j = 0; j < n && !r; j++)
        r = ents[j].dir && same(ents[j].name, base);
    DELETE(ents);
    return r;
}

/* Make the folder DL_DIR "/" rel and any of its parents not there yet, DL_DIR itself included,
 * as an upload makes the folders it needs; a folder already there is kept. Under dl_lock. -> 0
 * when rel is a folder afterwards, or -1 */
static int make_folder(const char *rel)
{
    char sub[DL_NAME_MAX + 1], path[DL_PATH_MAX];
    unsigned i, k = len(rel);
    for (i = 1; i <= k; i++) {
        if (i < k && rel[i] != '/')
            continue;
        copy(sub, rel, i);
        sub[i] = 0;
        if (is_folder(sub))
            continue;
        path_of(path, sub, "");
        if (dl_fs_file_size(path) >= 0)   /* a file of the folder's name: no folder there */
            return -1;
        dl_fs_mkdir(path);
    }
    return is_folder(rel) ? 0 : -1;
}

/* Make the folders a file's path below DL_DIR names, as the Rytm's stock file system makes them
 * when it writes the file (DL_DIR itself is always there). Under dl_lock. -> 0, or -1 */
static int make_parent(const char *rel)
{
    char sub[DL_NAME_MAX + 1];
    unsigned k = len(rel);
    while (k && rel[k - 1] != '/')
        k--;
    if (!k)
        return 0;
    copy(sub, rel, k - 1);
    sub[k - 1] = 0;
    return make_folder(sub);
}

/* What is under DL_DIR "/" rel: how many folder levels (0: no folder in it), to DL_DEPTH + 1 at
 * most, and the longest path below it past rel's own length (0: empty). Under dl_lock. */
static void measure(const char *rel, int at, int *levels, unsigned *longest)
{
    char sub[DL_NAME_MAX + 1];
    struct dl_ent *ents;
    int n, i;
    unsigned a = len(rel);
    *levels = 0;
    *longest = 0;
    if (at > DL_DEPTH + 1)
        return;
    n = dl_list_dir(rel, 0, 0);
    ents = heap_ents(n);
    n = dl_list_dir(rel, ents, n);
    for (i = 0; i < n; i++) {
        unsigned b = len(ents[i].name), here = 1 + b, l = 0;
        int d = 0;
        if (here > *longest)
            *longest = here;
        if (!ents[i].dir || a + 1 + b > DL_NAME_MAX)
            continue;
        copy(sub, rel, a);
        sub[a] = '/';
        copy(sub + a + 1, ents[i].name, b + 1);
        measure(sub, at + 1, &d, &l);
        if (1 + d > *levels)
            *levels = 1 + d;
        if (here + l > *longest)
            *longest = here + l;
    }
    DELETE(ents);
}

/* the last move, for the MODS page (page.c dl_page_moved): the page renames its own files and its
 * open folder after it, and re-reads the folder, at its next tick. dl_fs_gen counts every change
 * the route makes, so the page re-reads after an upload, a delete or a new folder too. A move
 * waits for the page to have taken the last one (dl_move_seen). Under dl_lock. */
volatile unsigned dl_fs_gen;
volatile int dl_move_seen = 1;
char dl_move_from[DL_NAME_MAX + 1], dl_move_to[DL_NAME_MAX + 1];

/* /mods/active with every line naming FROM, or a file below it, naming TO instead (moved_name),
 * written over active (the store keeps the old one until the new one is whole). Under dl_lock. -> 0 (also
 * when there is no active, or nothing in it moved), or -1 */
static int keep_active(const char *from, const char *to)
{
    char path[DL_PATH_MAX], name[DL_NAME_MAX + 1];
    char *text, *out;
    dl_file fl;
    int size, n = 0, r = 0, changed = 0;
    unsigned i = 0, o = 0;
    path_of(path, "active", "");
    size = dl_fs_file_size(path);
    if (size <= 0)
        return 0;
    text = NEW((unsigned)size + 1);
    if (!text)
        return -1;
    if (dl_fs_open(&fl, path, 0) == 0)
        n = dl_fs_read(&fl, text, (unsigned)size);
    dl_fs_close(&fl);
    for (i = 0, o = 1; n == size && i < (unsigned)n; i++)
        o += text[i] == '\n';
    out = n == size ? NEW((unsigned)size + o * DL_NAME_MAX) : 0;   /* each line's name may grow */
    if (!out) {
        DELETE(text);
        return -1;
    }
    i = o = 0;
    while (i < (unsigned)n) {                /* each line: the name before its last run of blanks */
        unsigned s = i, e = i, cut, ne;
        while (e < (unsigned)n && text[e] != '\n')
            e++;
        for (cut = e; cut > s && text[cut - 1] != ' ' && text[cut - 1] != '\t'; cut--)
            ;
        for (ne = cut; ne > s && (text[ne - 1] == ' ' || text[ne - 1] == '\t' || text[ne - 1] == '\r'); ne--)
            ;
        while (s < ne && (text[s] == ' ' || text[s] == '\t'))
            s++;
        if (cut > i && text[s] != '#' && ne > s && moved_name(name, text + s, ne - s, from, to)) {
            unsigned k = len(name);
            copy(out + o, text + i, s - i);
            o += s - i;
            copy(out + o, name, k);
            o += k;
            copy(out + o, text + ne, e - ne);
            o += e - ne;
            changed = 1;
        } else {
            copy(out + o, text + i, e - i);
            o += e - i;
        }
        if (e < (unsigned)n)
            out[o++] = '\n';
        i = e + 1;
    }
    DELETE(text);
    if (changed) {                       /* the store replaces active only at the close */
        r = dl_fs_open(&fl, path, 1) || dl_fs_write(&fl, out, o) ? -1 : 0;
        dl_fs_close(&fl);
        if (!r && dl_fs_file_size(path) != (int)o)
            r = -1;
    }
    DELETE(out);
    return r;
}

/* the store's rename moves a file, or a folder with everything below it, anywhere (store.c).
 * Under dl_lock. -> 0, or -1 */
static int move_tree(const char *from, const char *to, int dir)
{
    char a[DL_PATH_MAX], b[DL_PATH_MAX];
    (void)dir;
    path_of(a, from, "");
    path_of(b, to, "");
    return dl_fs_rename(a, b);
}

/* The move a .mv upload asks for: the staging buffer's n bytes, "FROM\nTO" (a last "\n" may
 * follow), paths below DL_DIR. The store's rename moves the file or folder (README, "The /mods
 * route"), then /mods/active follows it (keep_active), and the MODS page at its next tick. The
 * staging buffer keeps the two names, NUL-ended, for the note. Under dl_lock. -> 0, or the
 * refusal */
static const char *move(unsigned n)
{
    char *from = (char *)drive_staging, *to = 0, a[DL_PATH_MAX], b[DL_PATH_MAX];
    char parent[DL_NAME_MAX + 1];
    const char *why;
    unsigned i, k, longest = 0;
    int dir, levels = 0;
    if (n && from[n - 1] == '\n')
        n--;
    for (i = 0; i < n; i++) {
        if (!from[i])
            return "Invalid name";
        if (from[i] == '\n' && !to) {
            from[i] = 0;
            to = from + i + 1;
        }
    }
    if (!to)
        return "Invalid name";
    from[n] = 0;
    if (rel_folders(from) < 0 || rel_folders(to) < 0)
        return "Invalid name";
    if (!dl_move_seen)
        return "Busy: the MODS page";
    path_of(a, from, "");
    path_of(b, to, "");
    dir = dl_fs_file_size(a) < 0;
    if (dir && !is_folder(from))
        return "Not there";
    if (dir)
        measure(from, rel_folders(from) + 1, &levels, &longest);
    why = move_why(from, to, dir, levels, longest);
    if (why)
        return why;
    if (dl_fs_file_size(b) >= 0 || is_folder(to))
        return "Already there";
    k = len(to);
    while (k && to[k - 1] != '/')
        k--;
    copy(parent, to, k ? k - 1 : 0);
    parent[k ? k - 1 : 0] = 0;
    if (parent[0] && !is_folder(parent))
        return "No such folder";
    if (!dir && dl_list_dir(parent, 0, 0) >= DS_MAX)
        return "Folder full";
    if (move_tree(from, to, dir))
        return "move failed (see the listings)";
    copy(dl_move_from, from, len(from) + 1);
    copy(dl_move_to, to, len(to) + 1);
    dl_move_seen = 0;
    dl_fs_gen++;
    return keep_active(from, to) ? "moved, but active not rewritten" : 0;
}

/* reads stream from the sectors: a file of any size reads */
static int big_perm(int size, int perm)
{
    (void)size;
    return perm;
}

/* a file's entry, at any size: -> the refusal, or 0 with *size (0 for a new file) and *perm
 * (tests/drive_move_host.c runs it) */
static const char *file_perm(const char *name, int *size, int *perm)
{
    if (*size < 0 && !writable(name))
        return "Invalid name";
    if (*size < 0)
        *size = 0;
    *perm = big_perm(*size, writable(name) ? P_READ | P_WRITE | P_CLEAR : P_READ | P_CLEAR);
    return 0;
}

/* /mods/<name>: expected<entry, string>, 0x84 bytes: {u8 has_value, string
 * error +4, entry +8}. caps[0] is the name. It runs at a ReadOpen, a
 * WriteOpen, an upload's first chunk and a Clear, and can't tell which. */
unsigned dl_entry(unsigned char *res, void **f, void **caps)
{
    unsigned char *e = res + 8;
    char name[DL_NAME_MAX + 1];
    const char *why = 0;
    struct clo c;
    int size = 0, perm = 0, folders, kind = KIND_RAM;
    (void)f;
    zero(res, 8 + ENTRY_SIZE);
    folders = join_caps(name, caps);
    zero(&c, sizeof c);
    if (folders < 0) {
        why = "Invalid name";
    } else if (same(name, ".build")) {
        put32(e + E_TYPE, TYPE_DESC);
        put32(e + E_ADDR, (unsigned)core_desc);
        size = be32(core_desc + 8);
        perm = P_READ;
        c.what = C_BUILD;
    } else if (same(name, ".status")) {
        static char no_room[] = "drive-loader route\nno memory for the status\n";
        const char *text;
        dl_lock();
        text = status_text() ? dl_status_text : no_room;
        dl_unlock();
        put32(e + E_TYPE, TYPE_TEXT);
        put32(e + E_ADDR, (unsigned)text);
        size = len(text);
        perm = P_READ;
        c.what = C_STATUS;
    } else if (dl_staging_owner) {
        why = "Busy: the loader is using /mods";
    } else if (same(name, DL_STORE)) {    /* .store: read only, so a build can keep its codes and CCs */
        char path[DL_PATH_MAX];
        path_of(path, name, "");
        dl_lock();
        size = dl_fs_file_size(path);
        dl_unlock();
        if (size < 0)
            why = "No .store yet";
        else {
            put32(e + E_TYPE, EMOD);
            perm = P_READ;
            kind = KIND_DRIVE;
            c.what = C_FILE;
            c.exists = 1;
            copy(c.name, name, len(name) + 1);
        }
    } else if (same(name, DL_MOVE) && !dl_move_seen) {
        why = "Busy: the MODS page";      /* it hasn't taken the last move yet: try again */
    } else if (same(name, DL_MOVE)) {      /* .mv: an upload of "FROM\nTO" moves or renames */
        put32(e + E_TYPE, EMOD);
        put32(e + E_ADDR, (unsigned)drive_staging);
        perm = P_WRITE;
        size = 0;
        c.what = C_MOVE;
        copy(c.name, name, len(name) + 1);
    } else if (mkdir_of(name)) {           /* FOLDER/.dir: an upload of it makes FOLDER */
        char path[DL_PATH_MAX];
        name[mkdir_of(name)] = 0;
        path_of(path, name, "");
        dl_lock();
        size = dl_fs_file_size(path);
        dl_unlock();
        if (size >= 0) {
            why = "A file has that name";
        } else {
            put32(e + E_TYPE, EMOD);
            put32(e + E_ADDR, (unsigned)drive_staging);
            perm = P_WRITE;
            size = 0;
            c.what = C_MKDIR;
            copy(c.name, name, len(name) + 1);
        }
    } else {
        char path[DL_PATH_MAX];
        path_of(path, name, "");
        dl_lock();
        size = dl_fs_file_size(path);
        if (size < 0 && is_folder(name)) {   /* a folder: Clear removes it when it is empty */
            int n = dl_list_dir(name, 0, 0);
            struct dl_ent *ents = heap_ents(n);
            n = dl_list_dir(name, ents, n);
            DELETE(ents);
            dl_unlock();
            why = dir_clear_why(n);          /* a Clear can't fail after the entry is made: refuse here */
            if (why)
                goto refused;
            put32(e + E_TYPE, EMOD);
            put32(e + E_ADDR, (unsigned)drive_staging);
            perm = P_CLEAR;
            size = 0;
            c.what = C_DIR;
            copy(c.name, name, len(name) + 1);
            goto made;
        }
        dl_unlock();
        c.exists = size >= 0;
        why = file_perm(name, &size, &perm);
        if (!why) {
            put32(e + E_TYPE, EMOD);
            kind = KIND_DRIVE;
            c.what = C_FILE;
            copy(c.name, name, len(name) + 1);
            if (!c.exists)
                perm &= ~P_CLEAR;         /* nothing to clear: a Clear's writer never runs */
        }
    }
    if (why) {
refused:
        zero(e, ENTRY_SIZE);
        set_str(res + 4, why);
        return (unsigned)res;
    }
made:
    c.size = (unsigned)size;
    dl_lock();                            /* this resolution's serial, for +0x1c and +0x3c (up) */
    if (!++dl_serial)
        dl_serial = 1;
    c.serial = dl_serial;
    dl_unlock();
    if (kind == KIND_DRIVE) {             /* the file's own run, or the scratch sector */
        char path[DL_PATH_MAX];
        unsigned sector = 0, bytes = 0;
        path_of(path, c.name, "");
        if (c.exists && ds_file(path, &sector, &bytes) == 0 && bytes && sector) {
            put32(e + E_ADDR, sector * 512u);
            size = (int)bytes;
        } else {
            put32(e + E_ADDR, SCRATCH_AT);
            size = 0;
        }
        c.size = (unsigned)size;
    }
    put32(e + E_KIND, kind);
    *(unsigned short *)(e + E_PERM) = perm;
    put32(e + E_SIZE, size);
    put32(e + E_VERSION, 2);
    if (c.what == C_DIR)
        set_fn(e + E_CHANGE, &c, dl_change);
    if (c.what == C_MKDIR || c.what == C_MOVE) {
        set_fn(e + E_PRE, &c, dl_pre_inv);
        set_fn(e + E_CHANGE, &c, dl_change);
        set_fn(e + E_HEADER, &c, dl_hdr_inv);
    }
    if (c.what == C_FILE) {
        set_fn(e + E_PRE, &c, dl_pre_inv);
        set_fn(e + E_FILL, &c, dl_fill);
        set_fn(e + E_RESERVE, &c, dl_reserve);
        set_fn(e + E_CHANGE, &c, dl_change);
        set_fn(e + E_HEADER, &c, dl_hdr_inv);
    }
    if (c.what == C_BUILD || c.what == C_STATUS)
        set_fn(e + E_FILL, &c, dl_fill);  /* a no-op: no read meets an empty function */
    res[0] = 1;
    ok_str(res + 4);
    return (unsigned)res;
}


/* one list item, 0x14 bytes: {index, string name, size, u16 ops, u8 valid (a filtered list sends
 * only these), u8, u8 children} */
static void item(unsigned char *it, int index, const char *name, int size, int ops)
{
    zero(it, 0x14);
    put32(it, index);
    STR_NEW(it + 4, name, 0);
    put32(it + 8, size < 0 ? 0 : size);
    *(unsigned short *)(it + 0xc) = ops;
    it[0xe] = 1;
}

/* A folder's item in a listing. Stock's list reply sends an item's +0x10 as its "children" byte:
 * 1 says it can be listed itself, as /soundbanks' banks (Rytm 1.74 traced; the A4's router has
 * the same route tables and entry, the item layout is [I] until the emulator gate). Its
 * kind stays 2 (an item); kind 1 is only for the router's own folder table (resolver +0x00). */
static void folder_item(unsigned char *it, int index, const char *name)
{
    item(it, index, name, 0, 0);
    it[0x10] = 1;
}


/* DL_DIR "/" rel's folders (above DL_DEPTH only) and files as items from `at` (0: count only),
 * under dl_lock; `folders` is rel's depth. -> how many */
static int walk(unsigned char *at, const char *rel, int folders, struct dl_ent *ents, int n)
{
    int i, k = 0;
    char p[DL_NAME_MAX + 1], path[DL_PATH_MAX];
    for (i = 0; i < n; i++) {
        const char *nm = ents[i].name;
        unsigned a = len(rel);
        if (nm[0] == '.' || a + (a ? 1 : 0) + len(nm) > DL_NAME_MAX)
            continue;
        if (ents[i].dir && folders >= DL_DEPTH)
            continue;                     /* past the route's patterns: not reachable */
        copy(p, rel, a);
        if (a)
            p[a++] = '/';
        copy(p + a, nm, len(nm) + 1);
        if (at) {
            if (ents[i].dir)
                folder_item(at + 0x14 * k, k + 1, nm);
            else {
                int size;
                path_of(path, p, "");
                size = dl_fs_file_size(path);
                item(at + 0x14 * k, k + 1, nm, size,
                     big_perm(size, P_READ | P_CLEAR | (writable(p) ? P_WRITE : 0)));
            }
        }
        k++;
    }
    return k;
}

/* /mods, /mods/<folder>...: optional<vector<item>>, 0x14 bytes: {u8 has_value, string error +4,
 * begin +8, end +0xc, cap +0x10}. The router frees the items and their names. The vector is sized
 * to the folder (no cap: 254 left files of a full /mods unlisted). DL_DIR itself also lists .build
 * and .status, and .store when there is one. */
unsigned dl_list(unsigned char *res, void **f, void **caps)
{
    unsigned char *v;
    char rel[DL_NAME_MAX + 1];
    struct dl_ent *ents;
    int n, k, top, folders = 0, store_size = -1;
    (void)f;
    zero(res, 0x14);
    if (caps && caps[0] != caps[1]) {
        folders = join_caps(rel, caps) + 1;
        if (folders <= 0 || ((const char *const *)caps[0])[0][0] == '.') {
            set_str(res + 4, "Invalid name");
            return (unsigned)res;
        }
    } else
        rel[0] = 0;
    top = !rel[0];
    dl_lock();
    if (top) {
        char path[DL_PATH_MAX];
        path_of(path, DL_STORE, "");
        store_size = dl_fs_file_size(path);
    }
    n = dl_list_dir(rel, 0, 0);
    ents = heap_ents(n);
    n = dl_list_dir(rel, ents, n);
    k = walk(0, rel, folders, ents, n);
    v = NEW(0x14 * (unsigned)(k + 3));
    k = walk(v, rel, folders, ents, n);
    dl_unlock();
    DELETE(ents);
    if (top) {
        item(v + 0x14 * k, k + 1, ".build", be32(core_desc + 8), P_READ);
        k++;
        item(v + 0x14 * k, k + 1, ".status", 0, P_READ);
        k++;
        if (store_size >= 0) {
            item(v + 0x14 * k, k + 1, DL_STORE, store_size, big_perm(store_size, P_READ));
            k++;
        }
    }
    res[0] = 1;
    ok_str(res + 4);
    put32(res + 8, (unsigned)v);
    put32(res + 0xc, (unsigned)(v + 0x14 * k));
    put32(res + 0x10, (unsigned)(v + 0x14 * k));
    return (unsigned)res;
}

/* the root listing's {string name, u32 count} */
unsigned dl_name(unsigned char *res)
{
    STR_NEW(res, "mods", 0);
    put32(res + 4, 0);
    return (unsigned)res;
}

/* add one route: a pattern and a function without a closure. The entry
 * {segments vector, std::function} is 0x1c bytes, and nothing in it points
 * into itself (the strings and any closure are on the heap), so it moves
 * bitwise: the table grows into a new block here, without stock's
 * template calls. */
static void add(unsigned char *table, const char *pattern, void *inv)
{
    unsigned pat;
    unsigned e[7];                      /* {segments begin, end, cap; functor[2], manager, invoker} */
    unsigned *vec = (unsigned *)table;  /* {begin, end, cap} */
    unsigned used = vec[1] - vec[0];
    zero(e, sizeof e);
    STR_NEW(&pat, pattern, 0);
    PATTERN(&pat, e);
    STR_DEL(&pat);
    e[5] = (unsigned)dl_static_mgr;
    e[6] = (unsigned)inv;
    if (vec[1] == vec[2]) {
        unsigned room = used + 4 * 0x1c;
        unsigned char *b = NEW(room);
        copy(b, (void *)vec[0], used);
        if (vec[0])
            DELETE((void *)vec[0]);
        vec[0] = (unsigned)b;
        vec[1] = (unsigned)b + used;
        vec[2] = (unsigned)b + room;
    }
    copy((void *)vec[1], e, 0x1c);
    vec[1] += 0x1c;
}

/* the handler's register_route (vtable +0xc), called once by stock's add (0x40019596) from
 * route.s's site */
void dl_register_route(void *self, unsigned char *resolver)
{
    /* Stock's matcher takes a path only of its pattern's segment count, each `*` one segment, so a
     * route has one pattern per depth, in both tables, as /soundbanks does (one pattern a bank level):
     * folders to DL_DEPTH below DL_DIR, and the files in each. */
    static const char *const lists[] = {"/mods", "/mods/*", "/mods/*/*", "/mods/*/*/*"};
    static const char *const entries[] = {"/mods/*", "/mods/*/*", "/mods/*/*/*", "/mods/*/*/*/*"};
    unsigned i;
    (void)self;
    for (i = 0; i <= DL_DEPTH; i++) {
        add(resolver + LIST_TABLE, lists[i], dl_list_inv);
        add(resolver + ENTRY_TABLE, entries[i], dl_entry_inv);
    }
}
