/* SPDX-License-Identifier: GPL-2.0-or-later */
/* drive-loader (Analog Rytm MKII, OS 1.74): the /mods route of stock's Data
 * router (README, "The /mods route"). A RouteTypeHandler added to the
 * router's resolver (route.s) lists DL_DIR's files plus .build and .status,
 * and resolves /mods/<name> to a RAM entry over the staging buffer. Stock
 * streams every transfer through that buffer inside Elektron's data
 * container; the entry's functions fill it from the file before a read and
 * commit it to the file after a successful upload (stock calls the entry's
 * +0x4c function from WriteClose), or delete the file after a Clear.
 *
 * Calls come from the SysEx receive tasks, never the UI task. Struct
 * results come back through a hidden pointer in %a0, which route.s's thunks
 * pass to these functions as their first argument. */

#include "fs.h"

/* std::string: copy-on-write, one pointer to the chars */
#define STR_NEW ((void (*)(void *, const char *, void *))0x401bcff0)
#define STR_EMPTY 0x419f2a0cu                  /* the empty string, never freed */
#define NEW ((void *(*)(unsigned))0x40082a40)  /* operator new */
#define DELETE ((void (*)(void *))0x40082a4c)  /* operator delete */

/* ekFS's Directory (0x130 bytes) */
#define DIR_NEW ((void (*)(void *, const char *, int))0x4007ed48)
#define DIR_DEL ((void (*)(void *))0x4007ee02)
#define DIR_COUNT ((int (*)(void *))0x4007e1bc)
#define DIR_CHILD 0x4007e794                   /* (dir, i) -> %a0: shared_ptr {node, ctrl} */
#define SP_RELEASE ((void (*)(void *))0x40170bd8)     /* (&ctrl) */
#define NODE_KIND 4                            /* u16, 1: a folder */
#define NODE_NAME 0x14

/* the resolver's route tables and their vector calls (move-construct, realloc-insert) */
#define PATTERN ((int (*)(void *, void *))0x4002f1d0)     /* (&pattern, &segments) */
#define STR_DEL ((void (*)(void *))0x401bb540)
#define LIST_TABLE 0x0c
#define ENTRY_TABLE 0x18

#define EMOD 0x454d4f44u                       /* 'EMOD', the file type of a /mods upload */
#define TYPE_DESC 0x454c4b44u                  /* 'ELKD', .build */
#define TYPE_TEXT 0x54455854u                  /* 'TEXT', .status */
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

/* the boot linker's status records (its own .bss); absent until it links */
struct dl_status_rec {
    unsigned char state, code;
    unsigned short arg;
    char id[28];
};
extern struct dl_status_rec dl_status[] __attribute__((weak));
extern int dl_status_n __attribute__((weak));
extern unsigned char dl_skip_reason __attribute__((weak));
extern char dl_detail[][32] __attribute__((weak));      /* a refusal's name argument */
extern unsigned dl_boot_us[] __attribute__((weak));     /* the boot's time: whole, checks, link, inits */

static char dl_note[64];
static unsigned dl_upload_epoch;               /* dl_staging_epoch when the upload's header came */
int dl_last_io[4];                             /* the last commit's raw results, for .status */                       /* the route's last commit or delete */
static char dl_status_text[2048];
static unsigned dl_dir_obj[0x130 / 4];         /* under dl_lock */

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

/* ---- the std::functions an entry carries ---- */

/* a closure: what the function is for and the file's name; on the heap, as
 * stock's are, so the router may copy and destroy entries as it likes */
enum { C_FILE = 1, C_BUILD, C_STATUS };
struct clo {
    int what;
    int reading;                          /* set by the fill: counted in dl_reads */
    char name[DL_NAME_MAX + 1];
};

/* A read's reader copies the entry, closures and all, after the fill, and destroys the
 * copies at the read's close or drop (ReadClose 0x4008b716, or the next transfer's open,
 * 0x4008af8e). The fill marks its own closure, so the copies of it that live count the
 * open reads: while any does, the MODS page keeps off the staging buffer
 * (dl_staging_take_idle). An upload's entry stays in the Data handler until the next
 * upload, so only reads are counted. The router never calls a manager with dl_lock
 * held. */
/* stock's reader (ctor 0x4002beba): its vtable, and where it keeps its copy of the
 * entry's fill function */
#define READER_VT 0x401ca970u
#define READER_FILL 0x30
unsigned dl_page_now(void);                   /* page.c */

static void count(const struct clo *c, void *const *where, int by)
{
    unsigned char *r = (unsigned char *)where - READER_FILL;
    if (!c->reading)
        return;
    dl_lock();
    dl_reads += by;
    if (by < 0)
        dl_reader_remove(r);
    else if (*(unsigned *)r == READER_VT)
        dl_reader_add(r, dl_page_now());
    dl_unlock();
}

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
        count(c, (void *const *)dest, 1);
        break;
    }
    case 3:
        count(*dest, (void *const *)dest, -1);
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

/* +0x1c, before a read stream and before an upload's first chunk:
 * {u8 ok, string msg} */
unsigned dl_pre(unsigned char *res, void **f, const unsigned char *entry)
{
    (void)f, (void)entry;
    zero(res, 8);
    if (dl_staging_owner) {
        set_str(res + 4, "Busy: the loader is using /mods");
        return (unsigned)res;
    }
    res[0] = 1;
    put32(res + 4, STR_EMPTY);
    return (unsigned)res;
}

/* +0x2c, a read stream is made: put the file into the staging buffer. The
 * router serves the entry's size from it, so a file that can't be read
 * reads as zeros. */
void dl_fill(void **f)
{
    struct clo *c = *f;
    char path[DL_PATH_MAX];
    dl_file fl;
    int n = 0, want;
    if (c->what != C_FILE)
        return;
    path_of(path, c->name, "");
    dl_lock();
    if (!c->reading) {
        c->reading = 1;                   /* this read is open until its copies go */
        dl_reads++;
    }
    if (dl_staging_owner) {               /* not while the entry lives (dl_staging_take_idle) */
        note("read during a check:", c->name);
        dl_unlock();
        return;
    }
    want = dl_fs_file_size(path);         /* its own open: a small file's size is read */
    if (dl_fs_open(&fl, path, 0) == 0) {
        if (want > DL_STAGING_SIZE)
            want = DL_STAGING_SIZE;
        if (want > 0)
            n = dl_fs_read(&fl, drive_staging, want);
        if (n < 0)
            n = 0;
    }
    dl_fs_close(&fl);
    zero(drive_staging + n, DL_STAGING_SIZE - n);
    if (n == 0)
        note("read failed:", c->name);
    dl_unlock();
}

/* +0x6c, the container's header has arrived: {u8 ok, string msg +4, u8 value +8}.
 * The router has already checked the family and replaced the entry's size
 * with the header's payload size, so this check alone keeps an upload
 * inside the staging buffer. */
unsigned dl_hdr(unsigned char *res, void **f, const unsigned char *h)
{
    const char *why = 0;
    (void)f;
    zero(res, 12);
    if (be32(h + H_TYPE) != EMOD)
        why = "Invalid file type";
    else if (h[H_COMPRESSED])
        why = "Compressed uploads are not supported";
    else if (be32(h + H_PAYLOAD) > DL_STAGING_SIZE)
        why = "Invalid payload size";
    else if (dl_staging_owner)
        why = "Busy: the loader is using /mods";
    if (why) {
        set_str(res + 4, why);
        return (unsigned)res;
    }
    dl_upload_epoch = dl_staging_epoch;
    res[0] = 1;
    put32(res + 4, STR_EMPTY);
    res[8] = 1;
    return (unsigned)res;
}

/* +0x4c: after an upload (the header) or a Clear (0). An upload goes to
 * <name>.tmp, then over <name>. It can't refuse: a failure is noted for
 * .status. */
void dl_change(void **f, const unsigned char *h)
{
    struct clo *c = *f;
    char path[DL_PATH_MAX], tmp[DL_PATH_MAX];
    dl_file fl;
    unsigned n;
    int r;
    if (c->what != C_FILE)
        return;
    path_of(path, c->name, "");
    dl_lock();
    if (!h) {
        note(dl_fs_delete(path) ? "delete failed:" : "deleted", c->name);
        dl_unlock();
        return;
    }
    n = be32(h + H_PAYLOAD);
    if (dl_staging_owner || n > DL_STAGING_SIZE || dl_upload_epoch != dl_staging_epoch) {
        note("upload dropped:", c->name);
        dl_unlock();
        return;
    }
    path_of(tmp, c->name, ".tmp");
    r = dl_fs_open(&fl, tmp, 1) ? 1 : 0;      /* 1 open, 2 write, 3 size, 4 rename */
    if (!r && dl_fs_write(&fl, drive_staging, n))
        r = 2;
    dl_fs_close(&fl);
    if (!r && dl_fs_file_size(tmp) != (int)n)
        r = 3;
    if (!r) {
        dl_fs_delete(path);
        if (dl_fs_rename(tmp, path))
            r = 4;
    }
    if (r && r != 4)
        dl_fs_delete(tmp);
    if (r) {
        static const char *const what[] = {"", "open failed:", "write failed:", "size differs:", "rename failed:"};
        note(what[r], c->name);
    } else {
        note("saved", c->name);
    }
    dl_unlock();
}

/* ---- .status ---- */

static const char *const dl_state_names[] = {"-", "loaded", "refused", "dropped"};
static const char *const dl_skip_names[] = {
    "-", "key held", "after a failed start", "drive not ready", "descriptor ABI",
    "unstable", "more than 32 lines"};

static void status_text(void)
{
    char *end = dl_status_text + sizeof dl_status_text, *at = dl_status_text;
    int i, n;
    at = cat(at, end, "drive-loader route\nfolder " DL_DIR "\nlast: ");
    at = cat(at, end, dl_note[0] ? dl_note : "-");
    at = cat(at, end, "\nio:");
    for (i = 0; i < 4; i++) {
        at = cat(at, end, " ");
        at = num(at, end, (unsigned)dl_last_io[i]);
    }
    at = cat(at, end, "\n");
    if (&dl_boot_us) {
        static const char *const part[] = {"boot: ", " us (checks ", ", link ", ", inits "};
        for (i = 0; i < 4; i++) {
            at = cat(at, end, part[i]);
            at = num(at, end, dl_boot_us[i]);
        }
        at = cat(at, end, ")\n");
    }
    if (&dl_skip_reason && dl_skip_reason) {
        at = cat(at, end, "skipped: ");
        if (dl_skip_reason < 7)
            at = cat(at, end, dl_skip_names[dl_skip_reason]);
        else
            at = num(at, end, dl_skip_reason);
        at = cat(at, end, "\n");
    }
    n = &dl_status_n ? dl_status_n : 0;
    if (!&dl_status)
        n = 0;
    for (i = 0; i < n && i < 32; i++) {
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
        if (&dl_detail && dl_detail[i][0]) {
            char d[33];
            copy(d, dl_detail[i], 32);
            d[32] = 0;
            at = cat(cat(at, end, " "), end, d);
        }
        at = cat(at, end, "\n");
    }
}

/* ---- the routes ---- */

static int name_ok(const char *s)
{
    unsigned n = len(s), i;
#ifdef DL_TEST_ARMED
    if (same(s, ".armed"))               /* test builds only: plant the crash flag */
        return 1;
#endif
    if (!n || n > DL_NAME_MAX || s[0] == '.')
        return 0;
    for (i = 0; i < n; i++)
        if (s[i] == '/' || s[i] == '\\' || (unsigned char)s[i] < 0x20)
            return 0;
    return 1;
}

static int writable(const char *s)
{
#ifdef DL_TEST_ARMED
    if (same(s, ".armed"))
        return 1;
#endif
    return same(s, "active") || (ends(s, ".elemod") && len(s) > 7);
}

/* /mods/<name>: expected<entry, string>, 0x84 bytes: {u8 has_value, string
 * error +4, entry +8}. caps[0] is the name. It runs at a ReadOpen, a
 * WriteOpen, an upload's first chunk and a Clear, and can't tell which. */
unsigned dl_entry(unsigned char *res, void **f, void **caps)
{
    unsigned char *e = res + 8;
    const char *name;
    const char *why = 0;
    struct clo c;
    int size = 0, perm = 0;
    (void)f;
    zero(res, 8 + ENTRY_SIZE);
    name = caps[0] != caps[1] ? *(const char **)caps[0] : "";
    zero(&c, sizeof c);
    if (same(name, ".build")) {
        put32(e + E_TYPE, TYPE_DESC);
        put32(e + E_ADDR, (unsigned)core_desc);
        size = be32(core_desc + 8);
        perm = P_READ;
        c.what = C_BUILD;
    } else if (same(name, ".status")) {
        dl_lock();
        status_text();
        dl_unlock();
        put32(e + E_TYPE, TYPE_TEXT);
        put32(e + E_ADDR, (unsigned)dl_status_text);
        size = len(dl_status_text);
        perm = P_READ;
        c.what = C_STATUS;
    } else if (!name_ok(name)) {
        why = "Invalid name";
    } else if (dl_staging_owner) {
        why = "Busy: the loader is using /mods";
    } else {
        char path[DL_PATH_MAX];
        path_of(path, name, "");
        dl_lock();
        size = dl_fs_file_size(path);
        dl_unlock();
        if (size > DL_STAGING_SIZE)
            why = "File too large";
        else if (size < 0 && !writable(name))
            why = "Invalid name";
        else {
            if (size < 0)
                size = 0;
            perm = writable(name) ? P_READ | P_WRITE | P_CLEAR : P_READ | P_CLEAR;
            put32(e + E_TYPE, EMOD);
            put32(e + E_ADDR, (unsigned)drive_staging);
            c.what = C_FILE;
            copy(c.name, name, len(name) + 1);
        }
    }
    if (why) {
        zero(e, ENTRY_SIZE);
        set_str(res + 4, why);
        return (unsigned)res;
    }
    put32(e + E_KIND, 1);
    *(unsigned short *)(e + E_PERM) = perm;
    put32(e + E_SIZE, size);
    put32(e + E_VERSION, 2);
    if (c.what == C_FILE) {
        set_fn(e + E_PRE, &c, dl_pre_inv);
        set_fn(e + E_FILL, &c, dl_fill);
        set_fn(e + E_CHANGE, &c, dl_change);
        set_fn(e + E_HEADER, &c, dl_hdr_inv);
    }
    res[0] = 1;
    put32(res + 4, STR_EMPTY);
    return (unsigned)res;
}

extern void dl_call_a0(unsigned fn, void *res, void *a, unsigned b);

/* one list item, 0x14 bytes: {index, string name, size, u16 ops, u8 valid, u8, u8} */
static void item(unsigned char *it, int index, const char *name, int size, int ops)
{
    zero(it, 0x14);
    put32(it, index);
    STR_NEW(it + 4, name, 0);
    put32(it + 8, size < 0 ? 0 : size);
    *(unsigned short *)(it + 0xc) = ops;
    it[0xe] = 1;
}

/* the files in DL_DIR (not its folders), up to `max`, as items from `at`;
 * -> how many */
static int walk(unsigned char *at, int max)
{
    int i, n, k = 0;
    DIR_NEW(dl_dir_obj, DL_DIR, 0);
    n = DIR_COUNT(dl_dir_obj);
    for (i = 0; i < n && k < max; i++) {
        unsigned sp[2] = {0, 0};
        const unsigned char *node;
        dl_call_a0(DIR_CHILD, sp, dl_dir_obj, i);
        node = (const unsigned char *)sp[0];
        if (node && *(const unsigned short *)(node + NODE_KIND) != 1) {
            const char *nm = (const char *)node + NODE_NAME;
            if (at) {
                char path[DL_PATH_MAX];
                path_of(path, nm, "");
                item(at + 0x14 * k, k + 1, nm, dl_fs_file_size(path),
                     P_READ | P_CLEAR | (writable(nm) ? P_WRITE : 0));
            }
            k++;
        }
        if (sp[1])
            SP_RELEASE(&sp[1]);
    }
    DIR_DEL(dl_dir_obj);
    return k;
}

/* the names of DL_DIR's files (not its folders), up to `max`, for the MODS page;
 * the caller holds dl_lock. -> how many */
int dl_list_files(char (*names)[DL_NAME_MAX + 1], int max)
{
    int i, n, k = 0;
    DIR_NEW(dl_dir_obj, DL_DIR, 0);
    n = DIR_COUNT(dl_dir_obj);
    for (i = 0; i < n && k < max; i++) {
        unsigned sp[2] = {0, 0};
        const unsigned char *node;
        dl_call_a0(DIR_CHILD, sp, dl_dir_obj, i);
        node = (const unsigned char *)sp[0];
        if (node && *(const unsigned short *)(node + NODE_KIND) != 1) {
            const char *nm = (const char *)node + NODE_NAME;
            unsigned q = len(nm);
            if (q <= DL_NAME_MAX) {
                copy(names[k], nm, q + 1);
                k++;
            }
        }
        if (sp[1])
            SP_RELEASE(&sp[1]);
    }
    DIR_DEL(dl_dir_obj);
    return k;
}

#define LIST_MAX 254

/* /mods: optional<vector<item>>, 0x14 bytes: {u8 has_value, string error +4,
 * begin +8, end +0xc, cap +0x10}. The router frees the items and their names. */
unsigned dl_list(unsigned char *res, void **f, void *caps)
{
    unsigned char *v;
    int n;
    (void)f, (void)caps;
    zero(res, 0x14);
    v = NEW(0x14 * (LIST_MAX + 2));
    dl_lock();
    n = walk(v, LIST_MAX);
    dl_unlock();
    item(v + 0x14 * n, n + 1, ".build", be32(core_desc + 8), P_READ);
    n++;
    item(v + 0x14 * n, n + 1, ".status", 0, P_READ);
    n++;
    res[0] = 1;
    put32(res + 4, STR_EMPTY);
    put32(res + 8, (unsigned)v);
    put32(res + 0xc, (unsigned)(v + 0x14 * n));
    put32(res + 0x10, (unsigned)(v + 0x14 * (LIST_MAX + 2)));
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

/* the handler's register_route (vtable +0xc), called once by stock's add
 * (0x4002da4a) from route.s's site */
void dl_register_route(void *self, unsigned char *resolver)
{
    (void)self;
    add(resolver + LIST_TABLE, "/mods", dl_list_inv);
    add(resolver + ENTRY_TABLE, "/mods/*", dl_entry_inv);
}
