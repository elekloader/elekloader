/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-ar2: mod folders in the +Drive file browser (README, "Mod folders in
 * the +Drive browser"; elekloader/sdk/include/analog-rytm-mk2/drive.h).
 * Every address is OS 1.74's; nothing of stock's code or tables is copied,
 * core calls it and reads its RAM. The sites' bodies are in drive.s.
 *
 * How the browser reads the +Drive: a directory model (vtable 0x401d7cbc)
 * holds a vector of entry records at +0x104/+0x108 and a dir handle at
 * +0x110 (+0 the folder's id, +12 the read position). Its fill (0x4007e950)
 * empties the vector, rewinds the handle and calls readdir (0x4007c0b4)
 * until it returns <= 0, then the browser asks node calls about each id:
 * resolve 0x40080504 (a node: byte 0 bit 0 a folder, byte 1 bit 0
 * write-protected, bit 1 a size at +8), exists 0x400807d2, the size walks
 * 0x40080910 and 0x400808b6. SAMPLES and the slot browser are SampleManager
 * views (vtables 0x401e8438, 0x401e710c) with the model at +0x538 and their
 * list at +0x38; after an edit stock refreshes a folder with the model's
 * method +8 (0x4007ede0) and marks the list (0x40078580), and the view's
 * tick (0x400e4d84) runs each UI tick while the view is shown, called
 * through the list's vtable +0x30 (0x401e84f8, 0x401e71cc).
 *
 * Mod entries are ids 0x7E000000 | slot << 21 | file << 20 | folder << 10
 * | index: stock's own ids are 2..65535 on the card and 0x1000002.. in RAM.
 * Core answers every node call for them, serves their folders in readdir
 * when the model's fill reads them, and appends the mod folders after the
 * root's last entry in that fill only (the USB transfer and the Drive
 * Loader read the root by other paths and never see them). The guards keep
 * them away from stock's writes and its sample loader.
 *
 * A refill (core_drive_refill, any task) is a flag; the shown view's tick
 * (UI task) reads the folder again when it is the one open.
 */

#include <stdint.h>

/* ---- the SDK's (drive.h) ---- */
struct core_drive_entry {
    const char *name;
    uint32_t size;
    int32_t folder;
};
struct core_drive_folder {
    const char *name;
    int32_t (*list)(uint32_t folder);
    int32_t (*entry)(uint32_t folder, uint32_t i, struct core_drive_entry *e);
    int32_t (*parent)(uint32_t folder);
    int32_t (*load)(uint32_t folder, uint32_t i);
};
extern const struct core_drive_folder *const core_drive_folders[];   /* zero-terminated */

/* ---- stock's ---- */
#define ROOT_ID 2u                    /* the +Drive root's id */
#define FILL_LO 0x4007e950u           /* the model's fill: its readdir calls */
#define FILL_HI 0x4007eb70u
#define VIEW_MODEL 0x538              /* SampleManager: its directory model */
#define VIEW_LIST 0x38                /* its list */
#define MODEL_DIR 0x110               /* the model's dir handle; +0 the folder's id */
#define MODEL_REFRESH 8               /* the model's vtable: read the folder again */
#define LIST_DIRTY ((void (*)(void *))0x40078580)
#define VIEW_TICK ((void (*)(void *))0x400e4d84)

/* the browser's entry record (0x114 bytes) */
struct rec {
    uint32_t id;
    uint16_t type;                    /* 1 a folder, 0 a file */
    uint16_t pad;
    uint32_t parent, pos0, pos1;
    char name[256];
};
struct dirh { uint32_t id, block, offset, index; };
/* a node; the filesystem reads past the first 16 bytes, so 128 zeroed */
struct node { uint8_t b0, b1; uint16_t pad; uint32_t f4, f8, f12; uint8_t rest[112]; };

/* ---- ids ---- */
#define TOP 0x7E000000u
#define IS_MOD(id) (((uint32_t)(id) & 0xFF000000u) == TOP)
#define SLOT(id) (((id) >> 21) & 7u)
#define FILE_BIT 0x00100000u
#define FOLDER(id) (((id) >> 10) & 0x3ffu)
#define INDEX(id) ((id) & 0x3ffu)
#define DIR_ID(s, f) (TOP | (uint32_t)(s) << 21 | (uint32_t)(f) << 10)
#define FILE_ID(s, f, i) (DIR_ID(s, f) | FILE_BIT | (uint32_t)(i))
#define LIMIT 1023u                   /* folders and indices below it */
#define TAKEN 0x7EFFFFFFu             /* LOAD TO PROJ's reference for a file a mod took (folder 1023: no id) */
#define ROOT_MARK 0x7E7E0000u         /* the root's handle +12 while the mod folders are given */
#define SLOTS 8

/* ---- the folders ---- */
static const struct core_drive_folder *folders[SLOTS];
static volatile uint8_t nfolders = 0xff;          /* 0xff: not read yet */
static volatile uint8_t pend[SLOTS];
static volatile int32_t want[SLOTS];             /* the folder to read again; -1 any */
static uint8_t clash;                            /* bit s: a real root entry has slot s's name, this listing */

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

/* 1 when name (ended by 0 or '/') is folder's name, any case */
static int same(const char *name, const char *folder)
{
    while (*folder) {
        if (lower((uint8_t)*name) != lower((uint8_t)*folder))
            return 0;
        name++, folder++;
    }
    return *name == 0 || *name == '/';
}

static int valid_name(const char *n)
{
    int k;
    if (!n || !n[0])
        return 0;
    for (k = 0; n[k]; k++)
        if (k >= 23 || n[k] == '/')
            return 0;
    return !same(n, "mods");
}

static uint32_t count(void)
{
    uint32_t n = nfolders, k, j;
    if (n != 0xff)
        return n;
    n = 0;
    for (k = 0; core_drive_folders[k] && n < SLOTS; k++) {
        const struct core_drive_folder *f = core_drive_folders[k];
        if (!valid_name(f->name) || !f->list || !f->entry)
            continue;
        for (j = 0; j < n; j++)
            if (same(f->name, folders[j]->name))
                break;
        if (j == n)
            folders[n++] = f;
    }
    nfolders = (uint8_t)n;
    return n;
}

static const struct core_drive_folder *of(uint32_t id)
{
    uint32_t s = SLOT(id);
    return s < count() ? folders[s] : 0;
}

/* 1 when name (ended by 0 or '/') is any mod folder's */
static int a_folder_name(const char *name)
{
    uint32_t s, n = count();
    for (s = 0; s < n; s++)
        if (same(name, folders[s]->name))
            return 1;
    return 0;
}

void core_drive_refill(const struct core_drive_folder *f, int32_t folder)
{
    uint32_t s, n = count();
    for (s = 0; s < n; s++)
        if (folders[s] == f) {
            if (pend[s] && want[s] != folder)
                folder = -1;
            want[s] = folder;
            pend[s] = 1;
            return;
        }
}

/* ---- the records ---- */
static void put(struct rec *r, uint32_t id, uint16_t type, uint32_t parent, const char *name)
{
    int k = 0;
    r->id = id;
    r->type = type;
    r->parent = parent;
    r->pos0 = r->pos1 = 0;
    if (name)
        for (; k < 63 && name[k]; k++)
            r->name[k] = name[k];
    r->name[k] = 0;
}

/* entry i of slot s's folder f, or 0 */
static int get(uint32_t s, uint32_t f, uint32_t i, struct core_drive_entry *e)
{
    const struct core_drive_folder *d = folders[s];
    int32_t n = d->list(f);
    e->name = 0;
    e->size = 0;
    e->folder = -1;
    if (n < 0 || i >= (uint32_t)n || i >= LIMIT)
        return 0;
    if (d->entry(f, i, e))
        return 0;
    return e->folder < 0 || (uint32_t)e->folder < LIMIT;
}

/* one entry of a mod folder: ".", "..", then the mod's */
static int32_t serve(struct dirh *d, struct rec *r)
{
    uint32_t s = SLOT(d->id), f = FOLDER(d->id), i = d->index;
    struct core_drive_entry e;
    if (s >= count() || (d->id & FILE_BIT))
        return -1;
    d->index = i + 1;
    if (i == 0) {
        if (pend[s] && (want[s] < 0 || (uint32_t)want[s] == f))
            pend[s] = 0;            /* read now */
        put(r, d->id, 1, d->id, ".");
        return 1;
    }
    if (i == 1) {
        int32_t p = (f && folders[s]->parent) ? folders[s]->parent(f) : -1;
        put(r, (p >= 0 && (uint32_t)p < LIMIT) ? DIR_ID(s, p) : ROOT_ID, 1, d->id, "..");
        return 1;
    }
    i -= 2;
    if (!get(s, f, i, &e))
        return 0;
    if (e.folder >= 0)
        put(r, DIR_ID(s, e.folder), 1, d->id, e.name);
    else
        put(r, FILE_ID(s, f, i), 0, d->id, e.name);
    return 1;
}

/* the root's mod folders, after its last entry (the fill only) */
static int32_t append(struct dirh *d, struct rec *r)
{
    uint32_t s = d->index - ROOT_MARK, n = count();
    for (; s < n; s++)
        if (!(clash & (1u << s)))
            break;
    if (s >= n)
        return 0;
    d->index = ROOT_MARK + s + 1;
    put(r, DIR_ID(s, 0), 1, ROOT_ID, folders[s]->name);
    folders[s]->list(0);            /* the mod may ask for it now */
    return 1;
}

/* ---- the sites (drive.s calls these) ---- */
int32_t core_drive_readdir_stock(struct dirh *, struct rec *);
uintptr_t core_drive_resolve_stock(uint32_t);   /* stock answers in %d0 */
int32_t core_drive_exists_stock(uint32_t);
int32_t core_drive_size_stock(uint32_t);
int32_t core_drive_count_stock(uint32_t);
int32_t core_drive_load_stock(void *, uint32_t *, int32_t);
int32_t core_drive_check_stock(uint32_t *, void *);
int32_t core_drive_mkdir_stock(const char *);

/* readdir(handle, record) -> 1 an entry, 0 the end, -1 an error; ret: the caller */
int32_t core_drive_readdir(struct dirh *d, struct rec *r, uint32_t ret)
{
    int32_t got;
    uint32_t s, n;
    if (!d || !r)
        return core_drive_readdir_stock(d, r);
    if (IS_MOD(d->id))
        return serve(d, r);
    if (d->id != ROOT_ID || ret < FILL_LO || ret >= FILL_HI || !(n = count()))
        return core_drive_readdir_stock(d, r);
    if (d->index >= ROOT_MARK && d->index <= ROOT_MARK + SLOTS)
        return append(d, r);
    if (d->index == 0)
        clash = 0;                  /* a new listing */
    got = core_drive_readdir_stock(d, r);
    if (got > 0) {
        for (s = 0; s < n; s++)
            if (same(r->name, folders[s]->name))
                clash |= 1u << s;
        return got;
    }
    if (got < 0)
        return got;                 /* an error stays one */
    d->index = ROOT_MARK;
    return append(d, r);
}

static struct node dir_node = {1, 1, 0, 0, 0, 0, {0}};    /* a folder, write-protected */
static struct node file_node = {0, 3, 0, 0, 0, 1, {0}};   /* write-protected, its size at +8, drawn as text */

static uint32_t file_size(uint32_t id)
{
    struct core_drive_entry e;
    uint32_t s = SLOT(id);
    if (s >= count() || !get(s, FOLDER(id), INDEX(id), &e) || e.folder >= 0)
        return 0;
    return e.size;
}

uintptr_t core_drive_resolve(uint32_t id)
{
    if (!IS_MOD(id))
        return core_drive_resolve_stock(id);
    if (id & FILE_BIT) {
        file_node.f4 = file_node.f8 = file_size(id);
        return (uintptr_t)&file_node;
    }
    return (uintptr_t)&dir_node;
}

int32_t core_drive_exists(uint32_t id)
{
    if (IS_MOD(id))
        return of(id) != 0 && id != TAKEN;
    return core_drive_exists_stock(id);
}

int32_t core_drive_size(uint32_t id)
{
    if (IS_MOD(id))
        return (id & FILE_BIT) ? (int32_t)file_size(id) : -1;
    return core_drive_size_stock(id);
}

int32_t core_drive_count(uint32_t id)
{
    if (IS_MOD(id))
        return (id & FILE_BIT) ? (int32_t)file_size(id) : -1;
    return core_drive_count_stock(id);
}

/* a file reference's check (1..4 found, -1 not): a mod's isn't found, so
 * stock's loaders pass it over */
int32_t core_drive_check(uint32_t *ref, void *out)
{
    if (ref && IS_MOD(ref[0]))
        return -1;
    return core_drive_check_stock(ref, out);
}

/* load(manager, &ref, slot): a file a mod took counts as loaded; another
 * mod reference fails before stock copies it into the slot */
int32_t core_drive_load(void *mgr, uint32_t *ref, int32_t slot)
{
    if (ref && ref[0] == TAKEN)
        return 1;
    if (ref && IS_MOD(ref[0]))
        return 0;
    return core_drive_load_stock(mgr, ref, slot);
}

/* a new entry in the root (drive.s, core_drive_link: mkdir, file create,
 * rename, move): 1 when name is a mod folder's, refused */
int32_t core_drive_named(const char *name)
{
    return name && count() && a_folder_name(name);
}

/* mkdir(path) -> 0, -1: never /<a mod folder> or below, before stock's code
 * (the USB transfer's mkdir makes each level in turn) */
int32_t core_drive_mkdir(const char *path)
{
    if (path && path[0] == '/' && count() && a_folder_name(path + 1))
        return -1;
    return core_drive_mkdir_stock(path);
}

/* LOAD TO PROJ's file reference (out, model, index), for its job only (the
 * browser asks the same call for every row it draws): 1 a mod file's,
 * answered here, 0 stock's */
int32_t core_drive_ref(uint32_t *out, const uint8_t *model, int32_t index)
{
    const uint8_t *begin = *(const uint8_t *const *)(model + 0x104);
    const uint8_t *end = *(const uint8_t *const *)(model + 0x108);
    const struct rec *r;
    const struct core_drive_folder *d;
    uint32_t id;
    if (index < 0 || begin + 8 * index >= end)
        return 0;
    r = *(const struct rec *const *)(begin + 8 * index);
    if (!r || !IS_MOD(r->id))
        return 0;
    id = r->id;
    out[0] = out[1] = id;
    out[2] = out[3] = 0;
    d = of(id);
    if (d && (id & FILE_BIT) && d->load && d->load(FOLDER(id), INDEX(id)))
        out[0] = TAKEN;
    return 1;
}

/* SampleManager's tick, UI task: its list's vtable +0x30 (stock's thunk
 * 0x400e4db6 takes the list, view+0x38, back to the view; the view's own
 * +0x98 is never called). A refill of the open mod folder reads it again,
 * as stock does after a CREATE DIR */
void core_drive_list_tick(void *list)
{
    void *view = (uint8_t *)list - VIEW_LIST;
    uint8_t *model = *(uint8_t **)((uint8_t *)view + VIEW_MODEL);
    if (model) {
        uint32_t id = *(uint32_t *)(model + MODEL_DIR), s = SLOT(id);
        if (IS_MOD(id) && !(id & FILE_BIT) && s < count() && pend[s]
            && (want[s] < 0 || (uint32_t)want[s] == FOLDER(id))) {
            void (*refresh)(void *) = *(void (**)(void *))(*(uint8_t **)model + MODEL_REFRESH);
            pend[s] = 0;
            refresh(model);
            LIST_DIRTY(list);
        }
    }
    VIEW_TICK(view);
}
