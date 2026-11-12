/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-a4mk2 0.1: mod storage on the Analog Four MKII, OS 1.56. core-ar2 0.3's
 * design (mods/core-ar2/store.c) with the A4's objects; every address is
 * 1.56's.
 *
 * Mods declare fields in their mod.json ("storage"); elekloader places them
 * at build time from the build's lockfile (docs/ADAPTING.md, "Mod storage")
 * and gives each one an absolute symbol, its descriptor, which a mod passes to
 * core_store_get and core_store_set. Zero bits always mean the mod's default:
 * 0, or from 0.6 the field's "init" (field_init below).
 *
 * Each item keeps its fields in a live home inside stock's own objects, so
 * copy, paste, undo and pattern switch carry them with no hooks:
 *
 *   scope     live home                         persisted in
 *   sound     live sound +0xd4, +0xd5, +0xe0,   stored sound +0xdc, +0xdd, +0xe8,
 *             +0xe1 (4 B)                       +0xe9 (stock's raw copy writes them),
 *                                               the layout ID at +0xea, +0xeb,
 *                                               +0xf1, +0xf2 and the generation +0xf3
 *   kit       live kit +0x950..+0x95f (16 B)    stored kit +0x5b0..+0x5bf (its FX
 *                                               block +0x18), the ID +0x5c0, the
 *                                               generation +0x5c4
 *   global    core_store_globals (8 B a slot)   stored slot: bytes +3..+7 of its
 *                                               first three 16-B entries (+0xd1),
 *                                               the ID and generation after the data
 *   track     live track +0x2ea.. (64 B)        the tagged store: the first T B
 *   pattern   live pattern +0x16eda.. (64 B)    the tagged store: the first P B
 *   project   the tagged store itself           (it is the store)
 *
 * Every byte is one stock never reads, and every stored byte one stock never
 * writes or only ever writes as a copy of the live home or as 0:
 *   - sound: the load never writes the live bytes; the save copies live
 *     +0x14..+0xe3 raw into stored +0x1c..+0xeb and the load never reads
 *     +0xdc..+0xdd or +0xe8..+0xeb; it neither writes nor reads +0xf1..+0xf3.
 *     The voice push (0x40100a04) sends live +0x14..+0xf3 to the engine
 *     through a map that gives these words the slot every unused word goes to;
 *   - kit: the load and save never touch live +0x950..+0x95f; the FX block's
 *     +0x18..+0x37 is neither saved nor loaded (and the FX push maps it to the
 *     same unused slot);
 *   - global: the save writes 0 to bytes +3..+7 of each of a slot's 128
 *     entries and the load never reads them (it zeroes the live ones);
 *   - track, pattern: neither saved nor loaded; pattern init writes the 4
 *     bytes before the pattern home (+0x16ed6), not the home.
 *
 * The tagged store is byte +0 of all 256 four-byte entries of the 17 songs in
 * a whole project image (4,352 B), which the song save and load never touch
 * (they use bytes +1..+3). It holds a header (the layout ID, then one
 * generation per scope: sound, kit, global, track, pattern, project), then T B
 * for each of the 129 x 6 pattern tracks, then P B for each of the 129
 * patterns, then the project's bytes. The stored project (the working copy at
 * 0x4045c974, which the OS keeps across power cycles) carries the build's own
 * store; a project file and the like carry theirs.
 *
 * What differs from core-ar2 0.3 (the Analog Rytm MKII):
 *   - the sound keeps 32 bits and needs no velocity routes (no route.s);
 *   - the kit keeps 128 bits, and a kit init hook clears them (stock's kit
 *     init leaves live +0x950 as it was);
 *   - a global slot carries its own copy of the layout ID and generation, as
 *     sounds and kits do, so a global slot sent and received as SysEx keeps
 *     its mod data in the same layout (the Rytm's has no room for an ID);
 *   - six tracks a pattern, and the pattern-track scope is 129 x 6 items;
 *   - stock's own pattern record of the working pattern (stored +0x18) is
 *     index 0, the 128 patterns 1..128, as on the Rytm;
 *   - stock writes each parameter edit of the current kit straight into the
 *     stored project's copy of it, so core_store_set writes a field of that
 *     kit or its sounds through to the copy too (write_through);
 *   - SAVE PROJECT AS writes only the project's name into the stored
 *     project (stock's data is already there, edit by edit), so after a
 *     whole save of the live project into another image core copies its own
 *     bytes from that image into the stored project (st_file_save): a power
 *     cycle straight after the save keeps the mods' fields as it keeps
 *     stock's.
 *
 * The SysEx project commands that copy, swap or write whole stored songs go
 * through st_song_copy, which leaves the target's store bytes, and those for
 * whole stored patterns through st_pattern_copy, which clears the target
 * slot's fields (three sites each).
 *
 * Stock saves an edited pattern track, a pattern's settings and its
 * parameter locks into the stored project one part at a time, not through
 * the pattern serializer, so each of those saves also writes the pattern's
 * track and pattern fields.
 *
 * Data is used only when it was written by this layout or one of its
 * ancestors (core_store_ids) and its scope's generation is this build's:
 * sounds, kits and global slots by their own copy of the ID, everything else
 * by the header of the image it is read from. Anything else loads as zeros.
 */

#define S ((unsigned char *)0x4045c974)         /* the stored project */
#define IMAGE_SIZE 0x1f72d6
#define IMAGE_VERSION 0x17
#define B(p) (*(volatile unsigned char *)(p))

#define SC_SOUND 1
#define SC_KIT 2
#define SC_GLOBAL 3
#define SC_TRACK 4
#define SC_PATTERN 5
#define SC_PROJECT 6

#define SOUND_BYTES 4
#define SOUND_STORED 8                          /* the stored sound's copy of live +x is at +x + 8 */
#define KIT_HOME 0x950
#define KIT_SPARE 0x5b0
#define KIT_BYTES 16
#define GLOBAL_BYTES 8
#define GLOBALS_AT 0x1f4f3e                     /* the four stored slots in an image */
#define GLOBAL_STORED 0x8e5
#define GLOBAL_LIVE ((unsigned char *)0x412aa6de) /* live global slot 0; slots are 0x9b0 apart */
#define GLOBAL_LIVE_SIZE 0x9b0
#define TRACKS_A_PATTERN 6
#define TRACK_HOME 0x2ea
#define PATTERN_HOME 0x16eda
#define HOME_BYTES 64

#define STORE_BYTES 4352
#define HEADER 10
#define TRACKS (129 * TRACKS_A_PATTERN)
#define SONGS_AT 0x1e4834                       /* the working song, then the 16 songs */
#define SONG_SIZE 0x518
#define SONG_ENTRIES 0x114

/* the stored copy of the current kit, which stock writes each parameter edit
 * into, and where the kits are */
#define WORKING_KIT (S + 0x1989c6)
#define WORKING_KIT_INDEX (S + 0x199330)
#define LIVE_KITS ((unsigned char *)0x411e9936)  /* the live project +0xb95cea: the kits as edited */
#define LIVE_KIT_SIZE 0x960
#define KIT_SOUNDS 0x1c                         /* a live kit's 4 sounds, 0x152 B each */
#define STORED_KIT_SOUNDS 0x20                  /* a stored kit's, 0x15e B each */

/* From the linker, out of the lockfile: the IDs this build reads (its own
 * first, then its ancestors', zero-terminated), the generations (sound, kit,
 * global and track in __store_gen; pattern and project, then T and P, in
 * __store_gen2). */
extern const unsigned core_store_ids[];
extern char __store_gen[], __store_gen2[];

typedef int (*psave_t)(void *dst, void *live, int flags, void *progress);
typedef int (*pload_t)(void *live, void *src, void *progress);
#define PROJECT_SAVE ((psave_t)0x4001141e)
#define PROJECT_LOAD ((pload_t)0x40011054)

/* the pattern serializer's parts that stock also calls on their own, each
 * (stored part, live part, ...) -> nonzero when done */
typedef int (*part_t)(unsigned char *dst, unsigned char *live, int a, int b);
#define TRACK_SAVE ((part_t)0x40009b9c)         /* stored track (0x2ef B), live track (0x3b1 B), steps */
#define SETTINGS_SAVE ((part_t)0x40009834)      /* pattern +0x329e, live pattern +0x16ec6 */
#define LOCK_SAVE ((part_t)0x4000a1e6)          /* pattern +0x119e, live +0x1626, track, parameter */
#define LOCKS_SAVE ((part_t)0x4000a02c)         /* pattern +0x119e, live +0x1626 */
#define REC_PATTERN 0x32ae
#define REC_TRACKS 4                            /* the stored pattern's first track */
#define REC_TRACK 0x2ef
#define REC_LOCKS 0x119e
#define REC_SETTINGS 0x329e
#define LIVE_TRACK 0x3b1
#define LIVE_LOCKS 0x1626
#define LIVE_SETTINGS 0x16ec6
#define PATTERN_AT 0x18                         /* the working pattern's record */
#define PATTERNS_REC_AT 0x32c6                  /* the 128 patterns' records */

/* store.s: each routine's displaced first instructions, then a jump back */
extern int st_sound_save_body(void *dst, void *live);
extern int st_sound_load_body(void *live, void *src);
extern int st_pat_save_body(void *dst, void *live);
extern int st_pat_load_body(void *live, void *src);
extern int st_kit_save_body(void *dst, void *live);
extern int st_kit_load_body(void *live, void *src);
extern int st_glob_save_body(void *dst, void *live);
extern int st_glob_load_body(void *live, void *src);
extern int st_pat_init_body(void *pattern, int a);
extern int st_track_init_body(void *track, int a);
extern int st_sound_init_body(void *sound);
extern int st_kit_init_body(void *kit);
extern int st_proj_init_body(void *project);
extern int st_proj_load_body(void *live, void *src, void *progress);

/* ev_project: f(how, dropped) after CREATE NEW (how 0) and after every project
 * load (how 1), with the stored project in place; dropped has bit 1 << scope
 * set for each of global, track, pattern and project whose data the project
 * had none of for this layout, now zeros */
typedef void (*ev_project_t)(int how, unsigned dropped);
extern const ev_project_t ev_project[];

static void project_event(int how, unsigned dropped)
{
    const ev_project_t *f;
    for (f = ev_project; *f; f++)
        (*f)(how, dropped);
}

/* the live homes of the four global slots: stock's live records have no spare */
volatile unsigned char core_store_globals[4][GLOBAL_BYTES];

/* the image a project file is being saved from or loaded into (the file
 * hooks set it), else the stored project; 0 while a file of another project
 * version loads */
static unsigned char *st_image = S;

static unsigned gen(int scope)
{
    unsigned g = scope <= SC_TRACK ? (unsigned)__store_gen : (unsigned)__store_gen2;
    return g >> (24 - 8 * ((scope - 1) & 3)) & 255;
}
#define SPLIT_T ((unsigned)__store_gen2 >> 8 & 255)
#define SPLIT_P ((unsigned)__store_gen2 & 255)
#define PATTERNS_AT (HEADER + TRACKS * SPLIT_T)
#define PROJECT_AT (PATTERNS_AT + 129 * SPLIT_P)

static volatile unsigned char *store_byte(unsigned char *img, unsigned k)
{
    return img + SONGS_AT + SONG_SIZE * (k >> 8) + SONG_ENTRIES + 4 * (k & 255);
}

/* sound byte k (0..3): live +0xd4, +0xd5, +0xe0, +0xe1 */
static unsigned sound_at(unsigned k)
{
    return k < 2 ? 0xd4 + k : 0xde + k;
}

/* the stored sound's ID bytes (+0xea, +0xeb, +0xf1, +0xf2) and generation */
static const unsigned char sound_id_at[4] = {0xea, 0xeb, 0xf1, 0xf2};
#define SOUND_GEN 0xf3

/* stored global slot byte k (0..12): bytes +3..+7 of entries 0, 1, 2 */
static unsigned global_at(unsigned k)
{
    return 0xd1 + 16 * (k / 5) + 3 + k % 5;
}
#define GLOBAL_ID GLOBAL_BYTES                  /* k of the ID's first byte; the generation follows */

static unsigned rd32(const volatile unsigned char *p)
{
    return (unsigned)p[0] << 24 | (unsigned)p[1] << 16 | (unsigned)p[2] << 8 | p[3];
}

static void wr32(volatile unsigned char *p, unsigned v)
{
    p[0] = v >> 24;
    p[1] = v >> 16;
    p[2] = v >> 8;
    p[3] = v;
}

static int known_id(unsigned id)
{
    const unsigned *p;
    if (!id)
        return 0;
    for (p = core_store_ids; *p; p++)
        if (*p == id)
            return 1;
    return 0;
}

/* the image's store's layout ID */
static unsigned store_id(unsigned char *img)
{
    return (unsigned)B(store_byte(img, 0)) << 24 | (unsigned)B(store_byte(img, 1)) << 16 |
           (unsigned)B(store_byte(img, 2)) << 8 | B(store_byte(img, 3));
}

/* does the image's store hold this scope's data in this layout? */
static int store_ok(unsigned char *img, int scope)
{
    if (!img)
        return 0;
    return known_id(store_id(img)) && B(store_byte(img, 3 + scope)) == gen(scope);
}

static void store_header(unsigned char *img)
{
    int s;
    B(store_byte(img, 0)) = core_store_ids[0] >> 24;
    B(store_byte(img, 1)) = core_store_ids[0] >> 16;
    B(store_byte(img, 2)) = core_store_ids[0] >> 8;
    B(store_byte(img, 3)) = core_store_ids[0];
    for (s = SC_SOUND; s <= SC_PROJECT; s++)
        B(store_byte(img, 3 + s)) = gen(s);
}

static void store_clear(unsigned char *img, unsigned from, unsigned to)
{
    for (; from < to; from++)
        B(store_byte(img, from)) = 0;
}

static void store_copy(unsigned char *to, unsigned char *from, unsigned k, unsigned end)
{
    for (; k < end; k++)
        B(store_byte(to, k)) = B(store_byte(from, k));
}

static int in_image(const unsigned char *img, const unsigned char *p)
{
    return img && p >= img && p < img + IMAGE_SIZE;
}

static int is_image(const unsigned char *b)
{
    return (unsigned)b >= 0x40000000u && (unsigned)b + IMAGE_SIZE <= 0x48000000u &&
           rd32(b) == 0xbeefbaba && rd32(b + 4) == IMAGE_VERSION && rd32(b + IMAGE_SIZE - 4) == 0xbabeface;
}

/* the whole project image a pattern record sits in: the current image, or
 * any other buffer that holds one */
static unsigned char *pattern_image(unsigned char *p)
{
    unsigned n;
    if (in_image(st_image, p))
        return st_image;
    if (is_image(p - PATTERN_AT))
        return p - PATTERN_AT;
    for (n = 0; n < 128; n++)
        if (is_image(p - PATTERNS_REC_AT - REC_PATTERN * n))
            return p - PATTERNS_REC_AT - REC_PATTERN * n;
    return 0;
}

/* the pattern's index in the store (0 = the working pattern, 1..128), or -1 */
static int pattern_index(unsigned char *img, unsigned char *p)
{
    unsigned o;
    if (!in_image(img, p))
        return -1;
    o = p - img;
    if (o == PATTERN_AT)
        return 0;
    if (o >= PATTERNS_REC_AT && (o - PATTERNS_REC_AT) % REC_PATTERN == 0 &&
        (o - PATTERNS_REC_AT) / REC_PATTERN < 128)
        return 1 + (o - PATTERNS_REC_AT) / REC_PATTERN;
    return -1;
}

static int global_slot(const unsigned char *live)
{
    unsigned o = live - GLOBAL_LIVE;
    return live >= GLOBAL_LIVE && o % GLOBAL_LIVE_SIZE == 0 && o / GLOBAL_LIVE_SIZE < 4 ?
           (int)(o / GLOBAL_LIVE_SIZE) : -1;
}

/* is a stored global slot's mod data this layout's? */
static int global_ok(const unsigned char *slot)
{
    unsigned id = 0, k;
    for (k = 0; k < 4; k++)
        id = id << 8 | slot[global_at(GLOBAL_ID + k)];
    return known_id(id) && slot[global_at(GLOBAL_ID + 4)] == gen(SC_GLOBAL);
}

/* ---- the pattern serializers: pattern-track, pattern and project data ---- */

/* a live pattern's track and pattern fields into pattern n of an image's store */
static void pattern_out(unsigned char *img, int n, const unsigned char *live, int tracks, int pattern)
{
    unsigned t, j, T = SPLIT_T, P = SPLIT_P;
    if (tracks)
        for (t = 0; t < TRACKS_A_PATTERN; t++)
            for (j = 0; j < T; j++)
                B(store_byte(img, HEADER + (TRACKS_A_PATTERN * n + t) * T + j)) =
                    live[LIVE_TRACK * t + TRACK_HOME + j];
    if (pattern)
        for (j = 0; j < P; j++)
            B(store_byte(img, PATTERNS_AT + P * n + j)) = live[PATTERN_HOME + j];
}

int st_pat_save(unsigned char *dst, unsigned char *live)
{
    unsigned char *img = pattern_image(dst);
    int r = st_pat_save_body(dst, live), n = pattern_index(img, dst);
    if (n < 0 || !live)
        return r;
    pattern_out(img, n, live, 1, 1);
    if (n == 0 && img != S) {           /* a whole project leaves: its header and project data */
        store_copy(img, S, 0, HEADER);
        store_copy(img, S, PROJECT_AT, STORE_BYTES);
    }
    return r;
}

int st_pat_load(unsigned char *live, unsigned char *src)
{
    unsigned char *img = pattern_image(src);
    int r = st_pat_load_body(live, src), n = pattern_index(img, src);
    int tok = n >= 0 && store_ok(img, SC_TRACK), pok = n >= 0 && store_ok(img, SC_PATTERN);
    unsigned t, j, T = SPLIT_T, P = SPLIT_P;
    if (!live)
        return r;
    for (t = 0; t < TRACKS_A_PATTERN; t++)
        for (j = 0; j < HOME_BYTES; j++)
            live[LIVE_TRACK * t + TRACK_HOME + j] =
                tok && j < T ? B(store_byte(img, HEADER + (TRACKS_A_PATTERN * n + t) * T + j)) : 0;
    for (j = 0; j < HOME_BYTES; j++)
        live[PATTERN_HOME + j] = pok && j < P ? B(store_byte(img, PATTERNS_AT + P * n + j)) : 0;
    if (n == 0 && img != S) {           /* a whole project arrives: take its project data */
        if (store_ok(img, SC_PROJECT))
            store_copy(S, img, PROJECT_AT, STORE_BYTES);
        else
            store_clear(S, PROJECT_AT, STORE_BYTES);
    }
    return r;
}

/* ---- the parts of a pattern stock saves on their own ----
 * Stock saves an edited track (st_track_save), a pattern's settings
 * (st_settings_save) and its parameter locks (st_lock_save, st_locks_save)
 * into the stored project, each by itself. Each of those saves also writes
 * the whole pattern's track and pattern fields: their live homes are what a
 * mod last set, and they have no edit marks of their own. Only into an image
 * whose header is this layout's (the stored project's always is, from the
 * first project load at boot). No stock routine loads one of these parts by
 * itself: a pattern comes back whole, through st_pat_load. */

static void part_out(unsigned char *rec, unsigned char *live)
{
    unsigned char *img = pattern_image(rec);
    int n = pattern_index(img, rec);
    if (n < 0 || !live)
        return;
    pattern_out(img, n, live, store_ok(img, SC_TRACK), store_ok(img, SC_PATTERN));
}

/* 0x400b40ee's call: (stored track, live track, steps a, steps b); the track
 * is found from where its record sits */
int st_track_save(unsigned char *dst, unsigned char *live, int a, int b)
{
    int r = TRACK_SAVE(dst, live, a, b), t;
    unsigned char *rec;
    if (!dst || !live)
        return r;
    for (t = 0; t < TRACKS_A_PATTERN; t++) {
        rec = dst - REC_TRACKS - REC_TRACK * t;
        if (pattern_index(pattern_image(rec), rec) >= 0) {
            part_out(rec, live - LIVE_TRACK * t);
            break;
        }
    }
    return r;
}

/* 0x400a19f6's call: (stored pattern +0x329e, live pattern +0x16ec6) */
int st_settings_save(unsigned char *dst, unsigned char *live)
{
    int r = SETTINGS_SAVE(dst, live, 0, 0);
    if (dst && live)
        part_out(dst - REC_SETTINGS, live - LIVE_SETTINGS);
    return r;
}

/* 0x400a2fda's call: one parameter's locks on one track */
int st_lock_save(unsigned char *dst, unsigned char *live, int a, int b)
{
    int r = LOCK_SAVE(dst, live, a, b);
    if (dst && live)
        part_out(dst - REC_LOCKS, live - LIVE_LOCKS);
    return r;
}

/* 0x400a2ff6's call: all of a pattern's locks */
int st_locks_save(unsigned char *dst, unsigned char *live)
{
    int r = LOCKS_SAVE(dst, live, 0, 0);
    if (dst && live)
        part_out(dst - REC_LOCKS, live - LIVE_LOCKS);
    return r;
}

/* ---- sounds, kits and global slots: their own bytes, with their own copy of the ID ---- */

int st_sound_save(unsigned char *dst, unsigned char *live)
{
    int r = st_sound_save_body(dst, live), j;
    if (dst && live) {
        for (j = 0; j < SOUND_BYTES; j++)
            dst[sound_at(j) + SOUND_STORED] = live[sound_at(j)];
        for (j = 0; j < 4; j++)
            dst[sound_id_at[j]] = core_store_ids[0] >> (24 - 8 * j);
        dst[SOUND_GEN] = gen(SC_SOUND);
    }
    return r;
}

void core_dst_clean_sound(unsigned char *sound, int foreign);
void core_dst_clean_kit(unsigned char *kit, int foreign);
void core_dst_keep_sound(unsigned char *live, const unsigned char *src);
void core_dst_keep_kit(unsigned char *live, const unsigned char *src);

int st_sound_load(unsigned char *live, unsigned char *src)
{
    int r = st_sound_load_body(live, src), j;
    if (live && src) {
        unsigned id = 0;
        int ok;
        for (j = 0; j < 4; j++)
            id = id << 8 | src[sound_id_at[j]];
        ok = st_image && known_id(id) && src[SOUND_GEN] == gen(SC_SOUND);
        for (j = 0; j < SOUND_BYTES; j++)
            live[sound_at(j)] = ok ? src[sound_at(j) + SOUND_STORED] : 0;
        live[0xe2] = live[0xe3] = 0;    /* where the save's raw copy goes under the ID */
        core_dst_keep_sound(live, src);  /* gap 7: stock's load wrote 0 over a mod target */
        core_dst_clean_sound(live, !ok); /* 0.5: a code of another layout, or with no "mod" parameter here, is None */
    }
    return r;
}

int st_kit_save(unsigned char *dst, unsigned char *live)
{
    int r = st_kit_save_body(dst, live), j;   /* its 4 sounds go through st_sound_save */
    if (dst && live) {
        for (j = 0; j < KIT_BYTES; j++)
            dst[KIT_SPARE + j] = live[KIT_HOME + j];
        wr32(dst + KIT_SPARE + KIT_BYTES, core_store_ids[0]);
        dst[KIT_SPARE + KIT_BYTES + 4] = gen(SC_KIT);
    }
    return r;
}

void core_rows_now_reset(void);
void core_rows_forget(void);

void core_lock_cancel(unsigned field, void *item);

/* a kit load is a kit change: no trig of the new kit has played yet, so no
 * voice keeps a mod p-lock (rows.c, 0.5) */
int st_kit_load(unsigned char *live, unsigned char *src)
{
    int r = st_kit_load_body(live, src), j;
    core_rows_now_reset();
    if (live && src) {
        int ok = st_image && known_id(rd32(src + KIT_SPARE + KIT_BYTES)) &&
                 src[KIT_SPARE + KIT_BYTES + 4] == gen(SC_KIT);
        for (j = 0; j < KIT_BYTES; j++)
            live[KIT_HOME + j] = ok ? src[KIT_SPARE + j] : 0;
        core_dst_keep_kit(live, src);    /* gap 7: stock's load wrote 0 over a mod target */
        core_dst_clean_kit(live, !ok);  /* 0.5: macro targets and FX and CV Dests, as the sounds' */
    }
    return r;
}

int st_glob_save(unsigned char *dst, unsigned char *live)
{
    int r = st_glob_save_body(dst, live), g = global_slot(live), j;
    if (dst) {
        for (j = 0; j < GLOBAL_BYTES; j++)
            dst[global_at(j)] = g >= 0 ? core_store_globals[g][j] : 0;
        for (j = 0; j < 4; j++)
            dst[global_at(GLOBAL_ID + j)] = core_store_ids[0] >> (24 - 8 * j);
        dst[global_at(GLOBAL_ID + 4)] = gen(SC_GLOBAL);
    }
    return r;
}

int st_glob_load(unsigned char *live, unsigned char *src)
{
    int r = st_glob_load_body(live, src), g = global_slot(live), j;
    if (src && g >= 0) {
        int ok = st_image && global_ok(src);
        for (j = 0; j < GLOBAL_BYTES; j++)
            core_store_globals[g][j] = ok ? src[global_at(j)] : 0;
    }
    return r;
}

/* ---- inits: stock leaves the spare bytes as they were ---- */

int st_pat_init(unsigned char *pattern, int a)
{
    int r = st_pat_init_body(pattern, a), j;   /* its track inits go through st_track_init */
    if (pattern)
        for (j = 0; j < HOME_BYTES; j++)
            pattern[PATTERN_HOME + j] = 0;
    return r;
}

int st_track_init(unsigned char *track, int a)
{
    int r = st_track_init_body(track, a), j;
    if (track)
        for (j = 0; j < HOME_BYTES; j++)
            track[TRACK_HOME + j] = 0;
    return r;
}

int st_sound_init(unsigned char *sound)
{
    int r = st_sound_init_body(sound), j;
    if (sound)
        for (j = 0; j < SOUND_BYTES; j++)
            sound[sound_at(j)] = 0;
    return r;
}

int st_kit_init(unsigned char *kit)
{
    int r = st_kit_init_body(kit), j;         /* its 4 sounds go through st_sound_init */
    if (kit)
        for (j = 0; j < KIT_BYTES; j++)
            kit[KIT_HOME + j] = 0;
    return r;
}

/* CREATE NEW and the project reset: a fresh store, and no global data */
int st_proj_init(void *project)
{
    int r = st_proj_init_body(project), g, j;
    core_rows_now_reset();                     /* no playing mod p-locks (rows.c, 0.5) */
    store_clear(S, 0, STORE_BYTES);
    store_header(S);
    for (g = 0; g < 4; g++)
        for (j = 0; j < GLOBAL_BYTES; j++)
            core_store_globals[g][j] = 0;
    project_event(0, 0);
    return r;
}

/* After any project load: the stored project's store drops what this layout
 * can't read, then takes this build's header, and the mods hear of it, with
 * the scopes the loaded image (a file's buffer, else the stored project) had
 * nothing for. A file of another project version (st_image 0) brings no
 * project data. The global slots were checked one by one as they loaded. */
int st_proj_load(void *live, unsigned char *src, void *progress)
{
    int r = st_proj_load_body(live, src, progress), s;
    unsigned char *from = is_image(src) ? src : st_image;
    unsigned dropped = 0;
    /* its mod p-locks' codes are its layout's (rows.c, 0.5); no trig of it
     * has played yet */
    if (!from || !known_id(store_id(from)))
        core_rows_forget();
    core_rows_now_reset();
    if (!from || !st_image || !global_ok(from + GLOBALS_AT))
        dropped |= 1 << SC_GLOBAL;
    for (s = SC_TRACK; s <= SC_PROJECT; s++)
        if (!store_ok(from, s))
            dropped |= 1 << s;
    if (!store_ok(S, SC_TRACK))
        store_clear(S, HEADER, PATTERNS_AT);
    if (!store_ok(S, SC_PATTERN))
        store_clear(S, PATTERNS_AT, PROJECT_AT);
    if (!st_image || !store_ok(S, SC_PROJECT))
        store_clear(S, PROJECT_AT, STORE_BYTES);
    store_header(S);
    project_event(1, dropped);
    return r;
}

/* ---- the project file calls: the serializers work on the file's buffer ---- */

/* 0x40020772 (the buffer save), 0x400a77e4 (the file save) and 0x400a503c
 * (the background save): (image, live project, parts, progress) */
/* A whole save of the live project into another image (SAVE PROJECT AS's
 * file, a buffer) leaves the stored project as it was: stock writes only the
 * project's name there, its own data having reached it edit by edit. A mod's
 * fields reach it only through stock's saves, so after such a save core
 * copies the bytes it owns from the image the save just wrote into the
 * stored project, part by part as the save wrote them (flags: 2 the global
 * slots, 4 the kits and their sounds, 8 the patterns, 0x20 the sound pool).
 * Then a power cycle straight after the save keeps the mods' fields, as it
 * keeps stock's own edits. Only core's bytes are copied. */
#define LIVE_PROJECT ((void *)0x40653c4c)
#define KITS_AT 0x199334                        /* an image's 128 kits, then */
#define KIT_STORED 0x96a
#define POOL_AT 0x1e9ed0                        /* its 128 pool sounds */
#define SOUND_STORED_SIZE 0x15e

static void sound_bytes(unsigned char *to, const unsigned char *from)
{
    unsigned j;
    for (j = 0; j < SOUND_BYTES; j++)
        to[sound_at(j) + SOUND_STORED] = from[sound_at(j) + SOUND_STORED];
    for (j = 0; j < 4; j++)
        to[sound_id_at[j]] = from[sound_id_at[j]];
    to[SOUND_GEN] = from[SOUND_GEN];
}

static void kit_bytes(unsigned char *to, const unsigned char *from)
{
    unsigned j;
    for (j = 0; j < KIT_BYTES + 5; j++)         /* the data, the ID and the generation */
        to[KIT_SPARE + j] = from[KIT_SPARE + j];
    for (j = 0; j < 4; j++)
        sound_bytes(to + STORED_KIT_SOUNDS + SOUND_STORED_SIZE * j,
                    from + STORED_KIT_SOUNDS + SOUND_STORED_SIZE * j);
}

static void to_stored(const unsigned char *img, int flags)
{
    unsigned i, j;
    if (flags & 2)
        for (i = 0; i < 4; i++)
            for (j = 0; j < GLOBAL_ID + 5; j++)
                S[GLOBALS_AT + GLOBAL_STORED * i + global_at(j)] = img[GLOBALS_AT + GLOBAL_STORED * i + global_at(j)];
    if (flags & 4) {
        for (i = 0; i < 128; i++)
            kit_bytes(S + KITS_AT + KIT_STORED * i, img + KITS_AT + KIT_STORED * i);
        if (rd32(img + (WORKING_KIT_INDEX - S)) == rd32(WORKING_KIT_INDEX))   /* the same kit's copy */
            kit_bytes(WORKING_KIT, img + (WORKING_KIT - S));
    }
    if (flags & 0x20)
        for (i = 0; i < 128; i++)
            sound_bytes(S + POOL_AT + SOUND_STORED_SIZE * i, img + POOL_AT + SOUND_STORED_SIZE * i);
    if (flags & 8)
        store_copy(S, (unsigned char *)img, 0, STORE_BYTES);
}

int st_file_save(unsigned char *dst, void *live, int flags, void *progress)
{
    int r;
    st_image = dst;
    r = PROJECT_SAVE(dst, live, flags, progress);
    st_image = S;
    if (r && dst != S && live == LIVE_PROJECT && is_image(dst))
        to_stored(dst, flags);
    return r;
}

/* 0x400209ec: (live project, image, progress) */
int st_file_load(void *live, unsigned char *src, void *progress)
{
    int r;
    st_image = src && rd32(src) == 0xbeefbaba && rd32(src + 4) == IMAGE_VERSION ? src : 0;
    r = PROJECT_LOAD(live, src, progress);   /* through st_proj_load */
    st_image = S;
    return r;
}

/* ---- the SysEx project commands: whole stored songs ----
 * The SysEx handler copies, swaps and writes stored song records (0x518 B,
 * the working song and songs 1..16 of a project image) with memcpy. Byte +0
 * of each of a record's 256 entries is the tagged store, which belongs to the
 * image, not the song: copy everything else and leave the target's store
 * bytes. In a swap the temporary buffer keeps its own (garbage) bytes, so
 * both slots end with their own. Any other length is a plain copy. */

void *st_song_copy(unsigned char *dst, const unsigned char *src, unsigned n)
{
    unsigned i;
    for (i = 0; i < n; i++)
        if (n != SONG_SIZE || i < SONG_ENTRIES || i >= SONG_ENTRIES + 4 * 256 ||
            ((i - SONG_ENTRIES) & 3))
            dst[i] = src[i];
    return dst;
}

/* The same commands copy, write and swap whole stored patterns (0x32ae B)
 * with memcpy. A pattern's track and pattern fields are kept by slot in the
 * image's store, so they can't follow it: clear the target slot's, so the
 * pattern loads with its mods' defaults instead of the slot's old values.
 * Only a scope this layout reads there is cleared. A swap's temporary buffer
 * is in no image and is left alone. */

void *st_pattern_copy(unsigned char *dst, const unsigned char *src, unsigned n)
{
    unsigned char *img;
    unsigned i, t, j, T = SPLIT_T, P = SPLIT_P;
    int k;
    for (i = 0; i < n; i++)
        dst[i] = src[i];
    if (n != REC_PATTERN || !(img = pattern_image(dst)) || (k = pattern_index(img, dst)) < 0)
        return dst;
    if (store_ok(img, SC_TRACK))
        for (t = 0; t < TRACKS_A_PATTERN; t++)
            for (j = 0; j < T; j++)
                B(store_byte(img, HEADER + (TRACKS_A_PATTERN * k + t) * T + j)) = 0;
    if (store_ok(img, SC_PATTERN))
        for (j = 0; j < P; j++)
            B(store_byte(img, PATTERNS_AT + P * k + j)) = 0;
    return dst;
}

/* ---- for mods: a field's value in an item ----
 * field: the descriptor elekloader gives the field's symbol: the scope in
 * bits 28..31, the width (1..32) in bits 22..27 and the bit offset in the
 * scope's bytes in bits 0..21, most significant bit first.
 * item and index, by scope: a sound, the live sound; a kit, the live kit; a
 * global slot, index 0..3; a pattern track, the live pattern and index 0..5
 * (the track); a pattern, the live pattern; the project, neither.
 *
 * One byte can hold fields of several mods, and mods use them from the UI
 * and from the audio interrupt, so a field is read and written with
 * interrupts masked: no write is lost to another's, and no reader sees half
 * a value. */

static unsigned short irq_mask(void)
{
    unsigned short sr;
    __asm__ volatile("move.w %%sr,%0\n\tmove.w #0x2700,%%sr" : "=d"(sr) : : "memory");
    return sr;
}

static void irq_restore(unsigned short sr)
{
    __asm__ volatile("move.w %0,%%sr" : : "d"(sr) : "memory");
}

static volatile unsigned char *field_base(unsigned scope, unsigned char *item, int index)
{
    switch (scope) {
    case SC_SOUND:
        return item;
    case SC_KIT:
        return item ? item + KIT_HOME : 0;
    case SC_GLOBAL:
        return (unsigned)index < 4 ? core_store_globals[index] : 0;
    case SC_TRACK:
        return item && (unsigned)index < TRACKS_A_PATTERN ? item + LIVE_TRACK * index + TRACK_HOME : 0;
    case SC_PATTERN:
        return item ? item + PATTERN_HOME : 0;
    }
    return 0;
}

static volatile unsigned char *field_byte(unsigned scope, volatile unsigned char *base, unsigned k)
{
    if (scope == SC_SOUND)
        return base + sound_at(k);
    return scope == SC_PROJECT ? store_byte(S, PROJECT_AT + k) : base + k;
}

/* Stock writes each parameter edit of the current kit both into the live kit
 * and into the stored project's copy of that kit (S +0x1989c6, the kit whose
 * index is at S +0x199330), which is what a power cycle brings back: the
 * kit's other copies reach the stored project only through kit saves. A
 * field set in that kit or in one of its sounds is written through to the
 * copy the same way, with its layout ID and generation. */

static void write_through(unsigned scope, unsigned char *item)
{
    unsigned k = rd32(WORKING_KIT_INDEX), j, t;
    unsigned char *kit, *dst;
    if (k >= 128 || !item)
        return;
    kit = LIVE_KITS + LIVE_KIT_SIZE * k;
    if (scope == SC_KIT && item == kit) {
        for (j = 0; j < KIT_BYTES; j++)
            WORKING_KIT[KIT_SPARE + j] = kit[KIT_HOME + j];
        wr32(WORKING_KIT + KIT_SPARE + KIT_BYTES, core_store_ids[0]);
        WORKING_KIT[KIT_SPARE + KIT_BYTES + 4] = gen(SC_KIT);
        return;
    }
    if (scope != SC_SOUND)
        return;
    for (t = 0; t < 4; t++)
        if (item == kit + KIT_SOUNDS + 0x152 * t) {
            dst = WORKING_KIT + STORED_KIT_SOUNDS + 0x15e * t;
            for (j = 0; j < SOUND_BYTES; j++)
                dst[sound_at(j) + SOUND_STORED] = item[sound_at(j)];
            for (j = 0; j < 4; j++)
                dst[sound_id_at[j]] = core_store_ids[0] >> (24 - 8 * j);
            dst[SOUND_GEN] = gen(SC_SOUND);
        }
}

/* 0.6: a field's init (core-ar2 0.10's). Its descriptor carries the field's
 * slot in the table core_field_inits (1..63) in bits 16..21, 0 for a field
 * without one; no scope keeps 2^16 bits (the largest, the project's, keeps
 * 34,736), so the offset is bits 0..15. The field's bits keep value ^ init:
 * cleared bits (a new project, an init, another layout's data) read the
 * init, and every value goes through unchanged. Every core read and write
 * of a field comes here (rows.c, midi.c, value.c's slow read; value.c's fast
 * read XORs the same init), so p-locks, MIDI and the value read see the
 * value; the saves, loads, SysEx and the write-through copy the bits. */
extern const unsigned core_field_inits[];

static unsigned field_init(unsigned field)
{
    unsigned k = field >> 16 & 63;
    return k ? core_field_inits[k - 1] : 0;
}

unsigned core_store_get(unsigned field, void *item, int index)
{
    unsigned scope = field >> 28, w = field >> 22 & 63, o = field & 0xffff, v = 0, i, k, n;
    volatile unsigned char *base = field_base(scope, item, index);
    unsigned short sr;
    if (scope != SC_PROJECT && !base)
        return 0;
    n = ((o & 7) + w + 7) >> 3;               /* the bytes it spans */
    sr = irq_mask();
    if (n <= 4) {                             /* (0.5) a byte at a time: every field but a 26..32-bit one off its byte */
        for (k = o >> 3, i = 0; i < n; i++, k++)
            v = v << 8 | B(field_byte(scope, base, k));
        v >>= 8 * n - (o & 7) - w;
        if (w < 32)
            v &= (1u << w) - 1;
    } else
        for (i = 0; i < w; i++, o++)
            v = v << 1 | (B(field_byte(scope, base, o >> 3)) >> (7 - (o & 7)) & 1);
    irq_restore(sr);
    return v ^ field_init(field);
}

void core_store_set(unsigned field, void *item, int index, unsigned value)
{
    unsigned scope = field >> 28, w = field >> 22 & 63, o = field & 0xffff, i;
    volatile unsigned char *base = field_base(scope, item, index), *p;
    unsigned short sr;
    if (scope != SC_PROJECT && !base)
        return;
    value ^= field_init(field);
    sr = irq_mask();
    for (i = 0; i < w; i++, o++) {
        unsigned bit = 0x80 >> (o & 7);
        p = field_byte(scope, base, o >> 3);
        if (value >> (w - 1 - i) & 1)
            *p |= bit;
        else
            *p &= ~bit;
    }
    if (scope == SC_SOUND || scope == SC_KIT)
        write_through(scope, item);
    irq_restore(sr);
    if (scope == SC_SOUND || scope == SC_KIT)
        core_lock_cancel(field, item);         /* no slide of it now (rows.c, 0.5) */
}

/* 0.6: the modified mark (core-ar2 0.8's). After an edit of the live
 * project, a project load asks first; stock marks the project modified
 * (SET_MODIFIED(model, 1)) from the UI task on the project's change signal,
 * and an edit of only mod fields sends none (the page edits' mark is
 * 0x400befce's call). The mark is the byte 0x4045c8c2 of the settings
 * block, which the load reads (0x4003df98, through 0x400a5bea): ANY CHANGES
 * WILL BE LOST for the active slot, SAVE ... BEFORE LOADING for another.
 * core_project_edited asks for the mark from any task or the audio
 * interrupt; core_edit_tick (ev_tick, the UI task) makes it, with the
 * project model in the shared_ptr at 0x418717c8 (written only by its
 * get-or-create, 0x401494de). Core calls it for its own edits (a mod page's knob, a
 * lockable parameter's CC or NRPN); a mod calls it after its own keys or
 * settings change a field. */
#define PROJECT_MODEL (*(void *volatile *)0x418717c8)   /* a shared_ptr: the model, its count at +4 */
#define SET_MODIFIED ((int (*)(void *, int))0x400a5bba)  /* 0 when no project is loaded */

static volatile unsigned char edited;

void core_project_edited(void)
{
    edited = 1;
}

void core_edit_tick(void *ctrl)
{
    void *model = PROJECT_MODEL;
    (void)ctrl;
    if (!edited || !model)
        return;
    edited = 0;
    SET_MODIFIED(model, 1);
}

/* ---- ev_project_save (0.6, core-ar2 0.10's): a whole save of the live project ----
 * Every front-panel save of the project ends in one routine,
 * 0x400a771a(project model, slot, progress), which holds the file save site
 * 0x400a77e4 (st_file_save): it serializes the live project into the slot's
 * image, writes it to the +Drive and, when that worked, makes the slot the
 * active one (0x400a5b74) and clears the modified mark; it returns a byte, 0
 * when the save failed. Two jobs call it: the save job (0x4003da46, built by
 * 0x4003d448 with its SAVING box, which 0x4003ecbe queues after its
 * OVERWRITE and WRITE PROTECTED asks), and the load job (0x4003daf0) for
 * LOAD of an empty slot, after CREATE NEW (0x400a9f1e(model, -1), so
 * ev_project 0 first), returning to 0x4003dc26. No other whole save writes
 * a slot: the other calls of the file save (0x4001141e) copy the project
 * into RAM. Core calls the mods' ev_project_save handlers f(pre, how) around
 * it as core-ar2 does (store.h): how's kind in bits 0..7 (1 the active
 * slot, 2 another, 3 the empty slot's LOAD), the slot in bits 8..15 (255:
 * none) and, in the call after, bit 16 when the save failed. */
typedef void (*ev_project_save_t)(unsigned pre, unsigned how);
extern const ev_project_save_t ev_project_save[];
extern int st_proj_save_body(void *model, int slot, void *progress);

#define ACTIVE_SLOT ((int (*)(void *))0x400a5b9c)     /* -1: none */
#define SAVE_NEW_RET 0x4003dc26u                        /* the empty slot's LOAD: the call's return */
#define SAVE_SAME 1
#define SAVE_OTHER 2
#define SAVE_NEW 3
#define SAVE_FAILED 0x10000

static void save_event(unsigned pre, unsigned how)
{
    const ev_project_save_t *f;
    for (f = ev_project_save; *f; f++)
        (*f)(pre, how);
}

int st_proj_save(void *model, int slot, void *progress)
{
    int active = ACTIVE_SLOT(model), to = slot < 0 ? active : slot, r;
    unsigned how;
    if ((unsigned)__builtin_return_address(0) == SAVE_NEW_RET)
        how = SAVE_NEW;
    else
        how = to >= 0 && to == active ? SAVE_SAME : SAVE_OTHER;
    how |= ((unsigned)to < 128 ? (unsigned)to : 255) << 8;
    save_event(1, how);
    r = st_proj_save_body(model, slot, progress);
    save_event(0, (r & 255) ? how : how | SAVE_FAILED);
    return r;
}
