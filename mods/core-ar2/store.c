/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-ar2 0.3: mod storage on the Analog Rytm MKII, OS 1.74 only (core 0.2
 * is the last with 1.73). Every address here is 1.74's.
 *
 * Mods declare fields in their mod.json ("storage"); elekloader places them
 * at build time from the build's lockfile (docs/ADAPTING.md, "Mod storage")
 * and gives each one an absolute symbol, its descriptor, which a mod passes to
 * core_store_get and core_store_set. Bits at zero always mean the mod's
 * default: from 0.10 a field declared with "init": N reads N there, as the
 * field's bits keep value ^ N (field_init below).
 *
 * Each item keeps its fields in a live home inside stock's own objects, so
 * copy, paste, undo and pattern switch carry them with no hooks:
 *
 *   scope     live home                         persisted in
 *   sound     live sound +0x6d..+0x6f (3 B)     stored sound +0x70..+0x72, with
 *             and the 4 velocity routes' amount the layout ID (+0x73) and the
 *             low bytes +0x71, +0x74, +0x77,    sound generation (+0x77), and
 *             +0x7a (from 0.3; route.s)         +0x78..+0x7b
 *   kit       live kit FX words 38..41          stored kit +0x7c6+0x50..+0x57,
 *             (+0x796..+0x79d, 8 B)             with the layout ID (+0x58) and
 *                                               the kit generation (+0x5c)
 *   global    core_store_globals (5 B a slot)   stored slot +0x4b..+0x4f
 *   track     live track +785.. (64 B)          the tagged store: the first T B
 *   pattern   live pattern +85696.. (64 B)      the tagged store: the first P B
 *   project   the tagged store itself           (it is the store)
 *
 * The tagged store is byte +2 of all 256 entries of the 17 songs in a whole
 * project image (4,352 B), which stock never reads or writes. It holds a
 * header (the layout ID, then one generation per scope: sound, kit, global,
 * track, pattern, project), then T B for each of the 129 x 13 pattern tracks,
 * then P B for each of the 129 patterns, then the project's bytes. The
 * stored project (the working copy at 0x405c8f20) carries the build's own
 * store; a project file and the like carry theirs (a Total Recall take sends
 * the stored project itself).
 *
 * The SysEx project commands that copy, swap or write whole stored songs go
 * through st_song_copy, which leaves the target's store bytes, and those for
 * whole stored patterns through st_pattern_copy, which clears the target
 * slot's fields (three sites each).
 *
 * From 0.7 core_store_set writes a field straight into its item's stored
 * copy (write_through), and after a whole save of the live project into
 * another image (SAVE PROJECT AS) core copies its own bytes from it into the
 * stored project (st_file_save), so a power cycle without a save keeps them.
 *
 * Stock saves an edited pattern track, a pattern's settings and its
 * parameter locks into the stored project one part at a time, not through
 * the pattern serializer (from 0.3: st_track_save and the others below), so
 * each of those saves also writes the pattern's track and pattern fields.
 *
 * Data is used only when it was written by this layout or one of its
 * ancestors (core_store_ids) and its scope's generation is this build's:
 * sounds and kits by their own copy of the ID, everything else by the header
 * of the image it is read from. Anything else loads as zeros, and so does a
 * global slot from a SysEx dump, which has no room for an ID.
 */

#define S ((unsigned char *)0x405c8f20)         /* the stored project */
#define IMAGE_SIZE 0x1f9f32
#define B(p) (*(volatile unsigned char *)(p))

#define SC_SOUND 1
#define SC_KIT 2
#define SC_GLOBAL 3
#define SC_TRACK 4
#define SC_PATTERN 5
#define SC_PROJECT 6

#define SOUND_HOME 0x6d
#define SOUND_SPARE 0x70
#define SOUND_SPARE2 0x78       /* the route bytes' copies, 0.3 */
#define SOUND_BYTES 7
#define KIT_HOME 0x796
#define KIT_SPARE (0x7c6 + 0x50)
#define TRACK_HOME 785
#define PATTERN_HOME 85696
#define HOME_BYTES 64
#define GLOBALS_AT 0x1f9dee
#define GLOBAL_LIVE ((unsigned char *)0x413192ff) /* live global slot 0; slots are 228 B apart */

#define STORE_BYTES 4352
#define HEADER 10
#define TRACKS (129 * 13)

/* the live project, and where its parts sit in it and in a project image.
 * The live kits come twice: as edited (LIVE_KITS) and as last saved
 * (LIVE_KITS + LIVE_KITS_SAVED); a whole save writes the saved ones into the
 * image's 128 kits and the edited current kit into the image's working kit,
 * the copy of the kit whose index is at WORKING_KIT_INDEX. */
#define LIVE_PROJECT ((unsigned char *)0x407c89cb)
#define LIVE_KITS (LIVE_PROJECT + 0xa8cf18)
#define LIVE_KITS_SAVED 0x4f180
#define LIVE_KIT_SIZE 2531
#define LIVE_KIT_SOUNDS 42                      /* a live kit's 12 sounds, 152 B each */
#define LIVE_SOUND_SIZE 152
#define LIVE_POOL (LIVE_PROJECT + 0xb4b27c)     /* the 128 pool sounds */
#define LIVE_PATTERN0 (LIVE_PROJECT + 24)       /* the working pattern, then the 128 */
#define LIVE_PATTERNS (LIVE_PROJECT + 85784)
#define LIVE_PATTERN_SIZE 85760
#define WORKING_KIT 0x19c9c5
#define WORKING_KIT_INDEX 0x19d3f7
#define KITS_AT 0x19d3fb
#define KIT_STORED 2610
#define STORED_KIT_SOUNDS 46                    /* a stored kit's 12 sounds, 162 B each */
#define SOUND_STORED 162
#define POOL_AT 0x1f4397

/* From the linker, out of the lockfile: the IDs this build reads (its own
 * first, then its ancestors', zero-terminated), the generations (sound, kit,
 * global and track in __store_gen; pattern and project, then T and P, in
 * __store_gen2). */
extern const unsigned core_store_ids[];
extern char __store_gen[], __store_gen2[];

typedef int (*psave_t)(void *dst, void *live, int a, int flags, void *progress);
typedef int (*pload_t)(void *live, void *src, void *progress);
#define PROJECT_SAVE ((psave_t)0x4000d10e)
#define PROJECT_LOAD ((pload_t)0x4000cd2e)

/* the pattern serializer's parts that stock also calls on their own, each
 * (stored part, live part, ...) -> nonzero when done */
typedef int (*part_t)(unsigned char *dst, unsigned char *live, int a, int b, int c);
#define TRACK_SAVE ((part_t)0x40008dc8)         /* stored track (641 B), live track (917 B) */
#define SETTINGS_SAVE ((part_t)0x40008b06)      /* pattern +13089, live pattern +85683 */
#define LOCK_SAVE ((part_t)0x4000940c)          /* pattern +8337, live +11921, a, param, track */
#define LOCKS_SAVE ((part_t)0x40009216)         /* pattern +8337, live +11921, a */
#define REC_TRACKS 4                            /* the stored pattern's first track */
#define REC_TRACK 641
#define REC_LOCKS 8337
#define REC_SETTINGS 13089
#define LIVE_TRACK 917
#define LIVE_LOCKS 11921
#define LIVE_SETTINGS 85683
#define ROUTE_DEFAULTS ((void (*)(unsigned char *))0x4001224e)  /* a velocity route set's */

/* store.s: each routine's displaced first instructions, then a jump back */
extern int st_sound_save_body(void *dst, void *live, int a);
extern int st_sound_load_body(void *live, void *src);
extern int st_pat_save_body(void *dst, void *live, int a);
extern int st_pat_load_body(void *live, void *src);
extern int st_kit_save_body(void *dst, void *live, int a);
extern int st_kit_load_body(void *live, void *src);
extern int st_glob_save_body(void *dst, void *live, int a);
extern int st_glob_load_body(void *live, void *src);
extern int st_pat_init_body(void *pattern, int a);
extern int st_track_init_body(void *track, int a);
extern int st_sound_init_body(void *sound, int a, int b, int c);
extern int st_proj_init_body(void *project, int a, int b, int c);
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
volatile unsigned char core_store_globals[4][5];

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
    return img + 0x1eecfb + 1304 * (k >> 8) + 0x116 + 4 * (k & 255);
}

/* sound byte k (0..6) of the live sound's fields, from +0x6d: its 3 spare
 * bytes, then the 4 velocity routes' amount low bytes (+0x71 + 3i) */
static unsigned sound_at(unsigned k)
{
    return k < 3 ? k : 3 * k - 5;
}

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

/* the image's layout ID: the store's bytes 0..3, which are 4 apart */
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
           rd32(b) == 0xbeefbace && rd32(b + 4) == 9 && rd32(b + 0x1f9f2e) == 0xbacef00c;
}

/* the whole project image a pattern record sits in: the current image, or
 * any other buffer that holds one (Total Recall's recall uses its own) */
static unsigned char *pattern_image(unsigned char *p)
{
    unsigned n;
    if (in_image(st_image, p))
        return st_image;
    if (is_image(p - 0x18))
        return p - 0x18;
    for (n = 0; n < 128; n++)
        if (is_image(p - 0x3345 - 13101 * n))
            return p - 0x3345 - 13101 * n;
    return 0;
}

/* the pattern's index in the store (0 = the working pattern, 1..128), or -1 */
static int pattern_index(unsigned char *img, unsigned char *p)
{
    unsigned o;
    if (!in_image(img, p))
        return -1;
    o = p - img;
    if (o == 0x18)
        return 0;
    if (o >= 0x3345 && (o - 0x3345) % 13101 == 0 && (o - 0x3345) / 13101 < 128)
        return 1 + (o - 0x3345) / 13101;
    return -1;
}

/* the image a stored global slot sits in, or 0 (a SysEx dump) */
static unsigned char *global_image(unsigned char *p)
{
    unsigned g;
    if (in_image(st_image, p))
        return st_image;
    if (in_image(S, p))
        return S;
    for (g = 0; g < 4; g++)
        if (is_image(p - GLOBALS_AT - 80 * g))
            return p - GLOBALS_AT - 80 * g;
    return 0;
}

static int global_slot(const unsigned char *live)
{
    unsigned o = live - GLOBAL_LIVE;
    return live >= GLOBAL_LIVE && o % 228 == 0 && o / 228 < 4 ? (int)(o / 228) : -1;
}

/* ---- the pattern serializers: pattern-track, pattern and project data ---- */

/* a live pattern's track and pattern fields into pattern n of an image's store */
static void pattern_out(unsigned char *img, int n, const unsigned char *live, int tracks, int pattern)
{
    unsigned t, j, T = SPLIT_T, P = SPLIT_P;
    if (tracks)
        for (t = 0; t < 13; t++)
            for (j = 0; j < T; j++)
                B(store_byte(img, HEADER + (13 * n + t) * T + j)) = live[LIVE_TRACK * t + TRACK_HOME + j];
    if (pattern)
        for (j = 0; j < P; j++)
            B(store_byte(img, PATTERNS_AT + P * n + j)) = live[PATTERN_HOME + j];
}

int st_pat_save(unsigned char *dst, unsigned char *live, int a)
{
    unsigned char *img = pattern_image(dst);
    int r = st_pat_save_body(dst, live, a), n = pattern_index(img, dst);
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
    for (t = 0; t < 13; t++)
        for (j = 0; j < HOME_BYTES; j++)
            live[917 * t + TRACK_HOME + j] =
                tok && j < T ? B(store_byte(img, HEADER + (13 * n + t) * T + j)) : 0;
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
 * After a front-panel edit and on a pattern change, stock saves each edited
 * track (st_track_save), a pattern's settings (st_settings_save) and its
 * parameter locks (st_lock_save, st_locks_save) into the stored project, each
 * by itself. Each of those saves also writes the whole pattern's track and
 * pattern fields: their live homes are what a mod last set, and they have no
 * edit marks of their own. Only into an image whose header is this layout's
 * (the stored project's always is, from the first project load at boot). No
 * stock routine loads one of these parts by itself: a pattern comes back
 * whole, through st_pat_load. */

static void part_out(unsigned char *rec, unsigned char *live)
{
    unsigned char *img = pattern_image(rec);
    int n = pattern_index(img, rec);
    if (n < 0 || !live)
        return;
    pattern_out(img, n, live, store_ok(img, SC_TRACK), store_ok(img, SC_PATTERN));
}

/* 0x400bf818's call: (stored track, live track, a, b, c); the track is found
 * from where its record sits */
int st_track_save(unsigned char *dst, unsigned char *live, int a, int b, int c)
{
    int r = TRACK_SAVE(dst, live, a, b, c), t;
    unsigned char *rec;
    if (!dst || !live)
        return r;
    for (t = 0; t < 13; t++) {
        rec = dst - REC_TRACKS - REC_TRACK * t;
        if (pattern_index(pattern_image(rec), rec) >= 0) {
            part_out(rec, live - LIVE_TRACK * t);
            break;
        }
    }
    return r;
}

/* 0x400aacc4's call: (stored pattern +13089, live pattern +85683) */
int st_settings_save(unsigned char *dst, unsigned char *live, int a)
{
    int r = SETTINGS_SAVE(dst, live, a, 0, 0);
    if (dst && live)
        part_out(dst - REC_SETTINGS, live - LIVE_SETTINGS);
    return r;
}

/* 0x400ac2ce's call: one parameter's locks on one track */
int st_lock_save(unsigned char *dst, unsigned char *live, int a, int param, int track)
{
    int r = LOCK_SAVE(dst, live, a, param, track);
    if (dst && live)
        part_out(dst - REC_LOCKS, live - LIVE_LOCKS);
    return r;
}

/* 0x400ac2ec's call: all of a pattern's locks */
int st_locks_save(unsigned char *dst, unsigned char *live, int a)
{
    int r = LOCKS_SAVE(dst, live, a, 0, 0);
    if (dst && live)
        part_out(dst - REC_LOCKS, live - LIVE_LOCKS);
    return r;
}

/* ---- sounds and kits: their own bytes, with their own copy of the ID ---- */

/* The LFO's destination (0.8): stock saves it (live sound +28, a slot) as
 * the stored sound's +100, an index into a table of stock's 42 parameters,
 * and loads it back through the inverse table, which has no entry for a
 * lock code: a mod parameter's would come back as a stock one. So core
 * keeps the code, less 41, in its own sound field core_lfo_dst (the linker
 * places it in a build with an "lfo" field; 0, none, otherwise), and the
 * stored byte gets DST_NONE, stock's own None (META:None, slot 0): stock,
 * or a build that does not know the code, loads an LFO with no destination,
 * and keeps it so through its own saves. Neither table has a bound, so a
 * slot above 41 saved by stock comes back as one of its parameters; and on
 * load, a slot of 42..127 that core_lfo_dst did not give (a sound from
 * stock or from another layout) would read as a code, so it loads as None. */
extern char core_lfo_dst[];
#define LIVE_DST 28
#define STORED_DST 100
#define DST_NONE 41
struct core_lock_param;
const struct core_lock_param *core_lock_lfo(unsigned code);
void core_routes_load(unsigned char *live, int ok);      /* routes.c, 0.9 */
unsigned core_store_get(unsigned field, void *item, int index);
void core_store_set(unsigned field, void *item, int index, unsigned value);

int st_sound_save(unsigned char *dst, unsigned char *live, int a)
{
    unsigned f = (unsigned)core_lfo_dst, c = live ? live[LIVE_DST] : 0;
    int mine = f && c >= 42 && c <= 127 && core_lock_lfo(c), r;
    if (f && live)
        core_store_set(f, live, 0, mine ? c - 41 : 0);
    r = st_sound_save_body(dst, live, a);
    if (dst && live) {
        int j;
        if (mine)
            dst[STORED_DST] = DST_NONE;
        for (j = 0; j < 3; j++)
            dst[SOUND_SPARE + j] = live[SOUND_HOME + j];
        for (j = 3; j < SOUND_BYTES; j++)
            dst[SOUND_SPARE2 + j - 3] = live[SOUND_HOME + sound_at(j)];
        wr32(dst + SOUND_SPARE + 3, core_store_ids[0]);
        dst[SOUND_SPARE + 7] = gen(SC_SOUND);
    }
    return r;
}

int st_sound_load(unsigned char *live, unsigned char *src)
{
    int r = st_sound_load_body(live, src), j;
    if (live && src) {
        int ok = st_image && known_id(rd32(src + SOUND_SPARE + 3)) &&
                 src[SOUND_SPARE + 7] == gen(SC_SOUND);
        /* after the body: its route load wrote each amount's low byte 0 */
        for (j = 0; j < SOUND_BYTES; j++)
            live[SOUND_HOME + sound_at(j)] =
                ok ? src[j < 3 ? SOUND_SPARE + j : SOUND_SPARE2 + j - 3] : 0;
        core_routes_load(live, ok);
        if (core_lfo_dst) {
            if (live[LIVE_DST] >= 42 && live[LIVE_DST] <= 127)
                live[LIVE_DST] = 0;
            if ((j = core_store_get((unsigned)core_lfo_dst, live, 0)) && core_lock_lfo(j + 41))
                live[LIVE_DST] = j + 41;
        }
    }
    return r;
}

/* 0x400bd412: stock's write of a sound edit into the stored sound (the
 * sound parameters' object, the stored sound, the edit). It saves the DST
 * byte through 0x40007602 too, which passes a drum track's code (for its
 * scene and performance entries), so a code would reach the stored sound,
 * where stock's load reads it through its unbounded table (0.8 left it
 * there). The object's words are the edit's own copy, not the live sound,
 * so after it core reads the code from the stored byte (stock's numbers are
 * 0..41), puts DST_NONE there, and keeps the code in core_lfo_dst of the
 * live sound the stored one is kept from (0.9). */
extern int st_sound_through_body(void *obj, unsigned char *dst, void *edit);

/* the live sound a stored sound is kept from (write_through's inverse), or 0 */
static unsigned char *live_of_stored(const unsigned char *dst)
{
    unsigned o, k, n, t;
    o = dst - (S + POOL_AT);
    if (dst >= S + POOL_AT && o % SOUND_STORED == 0 && o / SOUND_STORED < 128)
        return LIVE_POOL + LIVE_SOUND_SIZE * (o / SOUND_STORED);
    o = dst - (S + WORKING_KIT + STORED_KIT_SOUNDS);
    k = rd32(S + WORKING_KIT_INDEX);
    if (dst >= S + WORKING_KIT + STORED_KIT_SOUNDS && o % SOUND_STORED == 0 &&
        (t = o / SOUND_STORED) < 12)
        return k < 128 ? LIVE_KITS + LIVE_KIT_SIZE * k + LIVE_KIT_SOUNDS + LIVE_SOUND_SIZE * t : 0;
    if (dst < S + KITS_AT + STORED_KIT_SOUNDS)
        return 0;
    o = dst - (S + KITS_AT + STORED_KIT_SOUNDS);
    n = o / KIT_STORED;
    o %= KIT_STORED;
    if (n >= 128 || o % SOUND_STORED || (t = o / SOUND_STORED) >= 12)
        return 0;
    return LIVE_KITS + LIVE_KITS_SAVED + LIVE_KIT_SIZE * n + LIVE_KIT_SOUNDS + LIVE_SOUND_SIZE * t;
}

int st_sound_through(void *obj, unsigned char *dst, void *edit)
{
    int r = st_sound_through_body(obj, dst, edit);
    unsigned f = (unsigned)core_lfo_dst, c, v;
    unsigned char *live;
    if (!f || !dst)
        return r;
    c = dst[STORED_DST];
    if (c < 42 || c > 127) {
        /* a stock destination: the live sound's code goes */
        if ((live = live_of_stored(dst)) != 0 && core_store_get(f, live, 0) &&
            !(live[LIVE_DST] >= 42 && live[LIVE_DST] <= 127))
            core_store_set(f, live, 0, 0);
        return r;
    }
    dst[STORED_DST] = DST_NONE;
    v = core_lock_lfo(c) ? c - 41 : 0;
    if ((live = live_of_stored(dst)) != 0 && core_store_get(f, live, 0) != v)
        core_store_set(f, live, 0, v);
    return r;
}

/* a parameter object's words (vtable +40; stock returns them in %d0, where
 * gcc would read a pointer from %a0) */
static unsigned char *words_of(void *obj)
{
    return (unsigned char *)((unsigned (*)(void *))(*(void ***)obj)[10])(obj);
}

/* The FX LFO's destination (0.9): the FX LFO's DST is the high byte of the
 * live kit's FX code-4 word (+0x752), a FX slot, and stock saves and loads
 * it through two 38-entry tables with no bound (0x401c5b4c, 0x401c5be4), so a
 * code would read past them. As for a sound's LFO, core keeps a kit
 * parameter's code, less 41, in its kit field core_fx_lfo_dst (placed in a
 * build with a kit-scope "lfo" field), and stock saves stock's None (slot 0)
 * in its place: the live byte is None while stock's save runs, and the code
 * again after. On load, a slot of 42..127 (none stock's) becomes None, and
 * core_fx_lfo_dst gives the code back. Stock writes a FX edit straight into
 * the stored kit's FX words (0x400a545c, a jsr of the FX words' save
 * 0x40007f58 with the live FX block), not through the kit's save, so core
 * takes that call too (core_fx_words_save). */
extern char core_fx_lfo_dst[];
#define LIVE_FX_DST 0x752
#define LIVE_FX 0x74a
#define FX_DST_NONE 0
#define FX_WORDS_SAVE ((int (*)(unsigned char *, unsigned char *, int))0x40007f58)
const struct core_lock_param *core_lock_fx_lfo(unsigned code);

/* before a save of the live kit's FX words: the FX LFO's code into
 * core_fx_lfo_dst and None into the byte; the code, or 0 when there is
 * nothing to put back */
static unsigned fx_dst_out(unsigned char *live)
{
    unsigned f = (unsigned)core_fx_lfo_dst, c = live ? live[LIVE_FX_DST] : 0;
    int mine = f && c >= 42 && c <= 127 && core_lock_fx_lfo(c);
    if (f && live)
        core_store_set(f, live, 0, mine ? c - 41 : 0);
    if (!mine)
        return 0;
    live[LIVE_FX_DST] = FX_DST_NONE;
    return c;
}

static void fx_dst_back(unsigned char *live, unsigned c)
{
    if (c)
        live[LIVE_FX_DST] = c;
}

static int live_kit(const unsigned char *k)
{
    unsigned o = k - LIVE_KITS;
    return k >= LIVE_KITS && o % LIVE_KIT_SIZE == 0 && o / LIVE_KIT_SIZE < 128;
}

/* 0x400a545c: a FX edit's write into the stored kit (stored FX words, live
 * FX block, a) */
int core_fx_words_save(unsigned char *dst, unsigned char *fx, int a)
{
    unsigned char *live = fx && live_kit(fx - LIVE_FX) ? fx - LIVE_FX : 0;
    unsigned c = fx_dst_out(live);
    int r = FX_WORDS_SAVE(dst, fx, a);
    fx_dst_back(live, c);
    return r;
}

/* 0x400a5692: stock's write of a FX edit into the stored kit (the FX
 * parameters' object, the stored kit's FX words, the edit), the FX track's
 * counterpart of a sound's: it saves the DST byte through 0x40007602, which
 * passes a code (core_code_id, for the FX track's scene and performance
 * entries), so after it a code in the live byte becomes the stored None
 * (stock's number 37, slot 0's) and goes into core_fx_lfo_dst, which
 * core_store_set writes through; a stock slot clears core_fx_lfo_dst */
#define STORED_FX_DST 0x44
#define STORED_FX_NONE 37
extern int st_fx_through_body(void *obj, unsigned char *dst, void *edit);

int st_fx_through(void *obj, unsigned char *dst, void *edit)
{
    int r = st_fx_through_body(obj, dst, edit);
    unsigned f = (unsigned)core_fx_lfo_dst, c, v;
    unsigned char *fx, *live;
    if (!f || !obj || !dst)
        return r;
    fx = words_of(obj);
    live = fx && live_kit(fx - LIVE_FX) ? fx - LIVE_FX : 0;
    if (!live)
        return r;
    c = live[LIVE_FX_DST];
    v = c >= 42 && c <= 127 && core_lock_fx_lfo(c) ? c - 41 : 0;
    if (v)
        dst[STORED_FX_DST] = STORED_FX_NONE;
    if (core_store_get(f, live, 0) != v)
        core_store_set(f, live, 0, v);
    return r;
}

int st_kit_save(unsigned char *dst, unsigned char *live, int a)
{
    unsigned c = fx_dst_out(live);
    int r = st_kit_save_body(dst, live, a), j;
    fx_dst_back(live, c);
    if (dst && live) {
        for (j = 0; j < 8; j++)
            dst[KIT_SPARE + j] = live[KIT_HOME + j];
        wr32(dst + KIT_SPARE + 8, core_store_ids[0]);
        dst[KIT_SPARE + 12] = gen(SC_KIT);
    }
    return r;
}

void core_kit_forget(unsigned char *kit);

int st_kit_load(unsigned char *live, unsigned char *src)
{
    int r = st_kit_load_body(live, src), j;
    if (live && src) {
        int ok = st_image && known_id(rd32(src + KIT_SPARE + 8)) &&
                 src[KIT_SPARE + 12] == gen(SC_KIT);
        for (j = 0; j < 8; j++)
            live[KIT_HOME + j] = ok ? src[KIT_SPARE + j] : 0;
        if (core_fx_lfo_dst) {
            if (live[LIVE_FX_DST] >= 42 && live[LIVE_FX_DST] <= 127)
                live[LIVE_FX_DST] = FX_DST_NONE;
            if ((j = core_store_get((unsigned)core_fx_lfo_dst, live, 0)) && core_lock_fx_lfo(j + 41))
                live[LIVE_FX_DST] = j + 41;
        }
        /* its scenes' and performances' mod entries are its layout's codes
         * (scenes.c, 0.8) */
        if (!ok)
            core_kit_forget(live);
    }
    return r;
}

/* ---- the global slots ---- */

int st_glob_save(unsigned char *dst, unsigned char *live, int a)
{
    int r = st_glob_save_body(dst, live, a), g = global_slot(live), j;
    if (dst)
        for (j = 0; j < 5; j++)
            dst[0x4b + j] = g >= 0 ? core_store_globals[g][j] : 0;
    return r;
}

int st_glob_load(unsigned char *live, unsigned char *src)
{
    int r = st_glob_load_body(live, src), g = global_slot(live), j;
    if (src && g >= 0) {
        unsigned char *img = global_image(src);
        /* a SysEx dump (no image) carries no layout ID: its mod data is dropped */
        int ok = st_image && img && store_ok(img, SC_GLOBAL);
        for (j = 0; j < 5; j++)
            core_store_globals[g][j] = ok ? src[0x4b + j] : 0;
    }
    return r;
}

/* ---- inits: stock leaves the spare bytes as they were ---- */

void core_rows_clear(unsigned obj);

int st_pat_init(unsigned char *pattern, int a)
{
    int r = st_pat_init_body(pattern, a), j;   /* its track inits go through st_track_init */
    if (pattern) {
        for (j = 0; j < HOME_BYTES; j++)
            pattern[PATTERN_HOME + j] = 0;
        core_rows_clear((unsigned)pattern);    /* its mod p-locks (rows.c, 0.8) */
    }
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

int st_sound_init(unsigned char *sound, int a, int b, int c)
{
    int r = st_sound_init_body(sound, a, b, c), j;
    if (sound)
        for (j = 0; j < SOUND_BYTES; j++)
            sound[SOUND_HOME + sound_at(j)] = 0;
    return r;
}

static unsigned short irq_mask(void);
static void irq_restore(unsigned short sr);

/* 0x400c542c's call, the VELOCITY MOD menu's reset of a sound's velocity
 * routes (routes: the live sound +0x70): the routes go to stock's defaults,
 * the fields in their low bytes stay. Interrupts are masked from the low
 * bytes' read to their write back, as core_store_set writes them, so a field
 * the audio interrupt sets meanwhile is not lost; the defaults are a leaf of
 * 14 instructions. */
void st_vel_defaults(unsigned char *routes)
{
    unsigned char keep[4];
    unsigned short sr;
    int i;
    if (!routes) {
        ROUTE_DEFAULTS(routes);
        return;
    }
    sr = irq_mask();
    for (i = 0; i < 4; i++)
        keep[i] = routes[3 * i + 1];
    ROUTE_DEFAULTS(routes);
    for (i = 0; i < 4; i++)
        routes[3 * i + 1] = keep[i];
    irq_restore(sr);
}

void core_rows_now_reset(void);

/* CREATE NEW and the project reset: a fresh store, no global data, and no
 * playing trig's mod locks (rows.c) */
int st_proj_init(void *project, int a, int b, int c)
{
    int r = st_proj_init_body(project, a, b, c), g, j;
    core_rows_now_reset();
    store_clear(S, 0, STORE_BYTES);
    store_header(S);
    for (g = 0; g < 4; g++)
        for (j = 0; j < 5; j++)
            core_store_globals[g][j] = 0;
    project_event(0, 0);
    return r;
}

/* After any project load: the stored project's store drops what this layout
 * can't read, then takes this build's header, and the mods hear of it, with
 * the scopes the loaded image (a file's buffer, else the stored project) had
 * nothing for. A file of an older project version (st_image 0) brings no
 * project data. */
void core_rows_forget(void);

int st_proj_load(void *live, unsigned char *src, void *progress)
{
    int r = st_proj_load_body(live, src, progress), g, j, s;
    unsigned char *from = is_image(src) ? src : st_image;
    unsigned dropped = 0;
    /* its mod p-locks' codes are its layout's (rows.c, 0.8); no trig of it
     * has played yet */
    if (!from || !known_id(store_id(from)))
        core_rows_forget();
    core_rows_now_reset();
    for (s = SC_GLOBAL; s <= SC_PROJECT; s++)
        if (!store_ok(from, s))
            dropped |= 1 << s;
    if (!store_ok(S, SC_TRACK))
        store_clear(S, HEADER, PATTERNS_AT);
    if (!store_ok(S, SC_PATTERN))
        store_clear(S, PATTERNS_AT, PROJECT_AT);
    if (!st_image || !store_ok(S, SC_PROJECT))
        store_clear(S, PROJECT_AT, STORE_BYTES);
    if (!store_ok(S, SC_GLOBAL))
        for (g = 0; g < 4; g++)
            for (j = 0; j < 5; j++)
                S[GLOBALS_AT + 80 * g + 0x4b + j] = 0;
    store_header(S);
    project_event(1, dropped);
    return r;
}

/* ---- the project file calls: the serializers work on the file's buffer ---- */

/* SAVE PROJECT AS writes only the project's name into the stored project
 * (stock's own data is already there, edit by edit) and then the whole live
 * project into the file's buffer. So after a save of the live project into
 * another image, core copies its own bytes from that image into the stored
 * project, by the parts the save wrote (flags: 2 the global slots, 4 the
 * kits and their sounds, 8 the patterns and with them the tagged store, 0x20
 * the sound pool): a power cycle straight after the save then keeps the
 * mods' fields, as it keeps stock's. Only core's bytes are copied. */

static void sound_bytes(unsigned char *to, const unsigned char *from)
{
    unsigned j;
    for (j = 0; j < 8; j++)                     /* the 3 bytes, the ID and the generation */
        to[SOUND_SPARE + j] = from[SOUND_SPARE + j];
    for (j = 0; j < 4; j++)                     /* the route bytes' copies */
        to[SOUND_SPARE2 + j] = from[SOUND_SPARE2 + j];
}

static void kit_bytes(unsigned char *to, const unsigned char *from)
{
    unsigned j;
    for (j = 0; j < 13; j++)                    /* the 8 bytes, the ID and the generation */
        to[KIT_SPARE + j] = from[KIT_SPARE + j];
    for (j = 0; j < 12; j++)
        sound_bytes(to + STORED_KIT_SOUNDS + SOUND_STORED * j, from + STORED_KIT_SOUNDS + SOUND_STORED * j);
}

static void to_stored(const unsigned char *img, int flags)
{
    unsigned i, j;
    if (flags & 2)
        for (i = 0; i < 4; i++)
            for (j = 0; j < 5; j++)
                S[GLOBALS_AT + 80 * i + 0x4b + j] = img[GLOBALS_AT + 80 * i + 0x4b + j];
    if (flags & 4) {
        for (i = 0; i < 128; i++)
            kit_bytes(S + KITS_AT + KIT_STORED * i, img + KITS_AT + KIT_STORED * i);
        if (rd32(img + WORKING_KIT_INDEX) == rd32(S + WORKING_KIT_INDEX))  /* the same kit's copy */
            kit_bytes(S + WORKING_KIT, img + WORKING_KIT);
    }
    if (flags & 0x20)
        for (i = 0; i < 128; i++)
            sound_bytes(S + POOL_AT + SOUND_STORED * i, img + POOL_AT + SOUND_STORED * i);
    if (flags & 8)
        store_copy(S, (unsigned char *)img, 0, STORE_BYTES);
}

int st_file_save(unsigned char *dst, void *live, int a, int flags, void *progress)
{
    int r;
    st_image = dst;
    r = PROJECT_SAVE(dst, live, a, flags, progress);
    st_image = S;
    if (r && dst != S && live == LIVE_PROJECT && is_image(dst))
        to_stored(dst, flags);
    return r;
}

int st_file_load(void *live, unsigned char *src, void *progress)
{
    int r;
    st_image = src && rd32(src + 4) == 9 ? src : 0;
    r = PROJECT_LOAD(live, src, progress);   /* through st_proj_load */
    st_image = S;
    return r;
}

/* ---- ev_project_save (0.10): a whole save of the live project to the +Drive ----
 * Every front-panel save of the project (SAVE PROJECT, SAVE PROJECT AS, the
 * project menu's saves, SAVE ... BEFORE LOADING) ends in one routine,
 * 0x400b07a4(project model, slot, progress), which the "Project Save" job
 * runs: it makes the slot the active one, serializes the live project into
 * the slot's image (st_file_save), writes that to the +Drive, and clears the
 * modified mark. LOAD of an empty slot runs it too, from the "Project Load"
 * job, after CREATE NEW (ev_project 0), to write the new project there.
 * Core calls the mods' ev_project_save handlers f(pre, how) around it: pre 1
 * before anything is written, pre 0 after. how: the kind in bits 0..7 (1 a
 * save to the active slot, 2 a save to another slot, 3 the new project of an
 * empty slot's LOAD), the slot (0..127, 255 when stock has none) in bits
 * 8..15, and in the call after, bit 16 when the save failed (the slot on the
 * +Drive was not written, or only in part). A field a handler sets during
 * pre 1 is in the saved copy: the serializer reads the live homes and the
 * stored project's store. The active slot before the save is what tells a
 * save from a save to another slot; a save with no active slot (a new
 * project's first) is a save to another slot. */
typedef void (*ev_project_save_t)(unsigned pre, unsigned how);
extern const ev_project_save_t ev_project_save[];
extern int st_proj_save_body(void *model, int slot, void *progress);

#define ACTIVE_SLOT ((int (*)(void *))0x400aed54)
#define SAVE_NEW_RET 0x400530e6         /* the empty slot's LOAD: the call's return */
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

/* ---- the SysEx project commands: whole stored songs ----
 * The SysEx handler copies, swaps and writes stored song records (0x518 B,
 * song slots 1..16 of a project image) with memcpy. Byte +2 of each of a
 * record's 256 entries is the tagged store, which belongs to the image, not
 * the song: copy everything else and leave the target's store bytes. In a
 * swap the temporary buffer keeps its own (garbage) bytes, so both slots end
 * with their own. Any other length is a plain copy. */

void *st_song_copy(unsigned char *dst, const unsigned char *src, unsigned n)
{
    unsigned i;
    for (i = 0; i < n; i++)
        if (n != 0x518 || i < 0x116 || i >= 0x116 + 4 * 256 || ((i - 0x116) & 3))
            dst[i] = src[i];
    return dst;
}

/* The same commands copy, write and swap whole stored patterns (13,101 B)
 * with memcpy. A pattern's track and pattern fields are kept by slot in the
 * image's store, so they can't follow it: clear the target slot's, so the
 * pattern loads with its mods' defaults instead of the slot's old values.
 * Only a scope this layout reads there is cleared: another layout places its
 * fields elsewhere, and this layout's offsets would hit other slots' fields
 * or the project's. A swap's temporary buffer is in no image and is left
 * alone. */

void *st_pattern_copy(unsigned char *dst, const unsigned char *src, unsigned n)
{
    unsigned char *img;
    unsigned i, t, j, T = SPLIT_T, P = SPLIT_P;
    int k;
    for (i = 0; i < n; i++)
        dst[i] = src[i];
    if (n != 13101 || !(img = pattern_image(dst)) || (k = pattern_index(img, dst)) < 0)
        return dst;
    if (store_ok(img, SC_TRACK))
        for (t = 0; t < 13; t++)
            for (j = 0; j < T; j++)
                B(store_byte(img, HEADER + (13 * k + t) * T + j)) = 0;
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
 * global slot, index 0..3; a pattern track, the live pattern and index 0..12
 * (the track); a pattern, the live pattern; the project, neither.
 *
 * One byte can hold fields of several mods, and mods use them from the UI
 * and from the audio interrupt, so a field is read and written with
 * interrupts masked, as the OS's own critical sections do: no write is lost
 * to another's, and no reader sees half a value. */

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
        return item ? item + SOUND_HOME : 0;
    case SC_KIT:
        return item ? item + KIT_HOME : 0;
    case SC_GLOBAL:
        return (unsigned)index < 4 ? core_store_globals[index] : 0;
    case SC_TRACK:
        return item && (unsigned)index < 13 ? item + 917 * index + TRACK_HOME : 0;
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
 * and into the stored project's copy of that kit, which is what a power
 * cycle brings back. A field set with core_store_set is written through to
 * the stored copy of its item at once, with the item's layout ID and
 * generation where it carries its own: the edited current kit (into the
 * working kit) and its sounds, a saved kit (into its slot of the 128) and
 * its sounds, a pool sound, a global slot, and a track or pattern of the
 * working pattern or of the 128 (their slots in the stored project's store,
 * when its header is this layout's, as it is from the first load at boot).
 * Other items (an edited kit that is not the current one, a buffer outside
 * the live project) reach the stored project by stock's saves, as their
 * stock data does. Called with interrupts masked. */

static void sound_through(unsigned char *dst, const unsigned char *live)
{
    unsigned j;
    for (j = 0; j < 3; j++)
        dst[SOUND_SPARE + j] = live[SOUND_HOME + j];
    for (j = 3; j < SOUND_BYTES; j++)
        dst[SOUND_SPARE2 + j - 3] = live[SOUND_HOME + sound_at(j)];
    wr32(dst + SOUND_SPARE + 3, core_store_ids[0]);
    dst[SOUND_SPARE + 7] = gen(SC_SOUND);
}

/* the stored kit a live kit is kept in, or 0: the edited current kit's is the
 * working kit, a saved kit's is its slot of the 128 */
static unsigned char *stored_kit(const unsigned char *kit)
{
    unsigned k = rd32(S + WORKING_KIT_INDEX), o;
    if (k < 128 && kit == LIVE_KITS + LIVE_KIT_SIZE * k)
        return S + WORKING_KIT;
    o = kit - (LIVE_KITS + LIVE_KITS_SAVED);
    if (kit >= LIVE_KITS + LIVE_KITS_SAVED && o % LIVE_KIT_SIZE == 0 && o / LIVE_KIT_SIZE < 128)
        return S + KITS_AT + KIT_STORED * (o / LIVE_KIT_SIZE);
    return 0;
}

static void write_through(unsigned scope, unsigned char *item, int index)
{
    unsigned j, t, o;
    unsigned char *dst;
    int n;
    switch (scope) {
    case SC_KIT:
        if ((dst = stored_kit(item)) != 0) {
            for (j = 0; j < 8; j++)
                dst[KIT_SPARE + j] = item[KIT_HOME + j];
            wr32(dst + KIT_SPARE + 8, core_store_ids[0]);
            dst[KIT_SPARE + 12] = gen(SC_KIT);
        }
        return;
    case SC_SOUND:
        o = item - LIVE_POOL;
        if (item >= LIVE_POOL && o % LIVE_SOUND_SIZE == 0 && o / LIVE_SOUND_SIZE < 128) {
            sound_through(S + POOL_AT + SOUND_STORED * (o / LIVE_SOUND_SIZE), item);
            return;
        }
        /* a kit's sound: the kit is the whole kit before it, if one is there */
        if (item < LIVE_KITS + LIVE_KIT_SOUNDS)
            return;
        o = item - (LIVE_KITS + LIVE_KIT_SOUNDS);
        if (o % LIVE_KIT_SIZE % LIVE_SOUND_SIZE || o % LIVE_KIT_SIZE / LIVE_SOUND_SIZE >= 12)
            return;
        t = o % LIVE_KIT_SIZE / LIVE_SOUND_SIZE;
        if ((dst = stored_kit(item - LIVE_KIT_SOUNDS - LIVE_SOUND_SIZE * t)) != 0)
            sound_through(dst + STORED_KIT_SOUNDS + SOUND_STORED * t, item);
        return;
    case SC_GLOBAL:
        for (j = 0; j < 5; j++)
            S[GLOBALS_AT + 80 * index + 0x4b + j] = core_store_globals[index][j];
        return;
    case SC_TRACK:
    case SC_PATTERN:
        if (item == LIVE_PATTERN0)
            n = 0;
        else if (item >= LIVE_PATTERNS && (o = item - LIVE_PATTERNS) % LIVE_PATTERN_SIZE == 0 &&
                 o / LIVE_PATTERN_SIZE < 128)
            n = 1 + o / LIVE_PATTERN_SIZE;
        else
            return;
        if (!store_ok(S, scope))
            return;
        if (scope == SC_PATTERN)
            for (j = 0; j < SPLIT_P; j++)
                B(store_byte(S, PATTERNS_AT + SPLIT_P * n + j)) = item[PATTERN_HOME + j];
        else
            for (t = index, j = 0; j < SPLIT_T; j++)
                B(store_byte(S, HEADER + (13 * n + t) * SPLIT_T + j)) = item[917 * t + TRACK_HOME + j];
        return;
    }
}

/* 0.10: a field's init. Its descriptor carries the field's slot in the table
 * core_field_inits (1..63) in bits 16..21, 0 for a field without one; no
 * scope keeps 2^16 bits, so the offset is bits 0..15. The field's bits keep
 * value ^ init: cleared bits (a new project, a reset, another layout's data)
 * read the init, and every value goes through unchanged. Every core read and
 * write of a field comes here, so p-locks, scenes, MIDI, the smoothed value
 * and RANDOMIZE and RELOAD PAGE all see the value; the saves, loads and
 * SysEx copy the bits. */
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
    if (n <= 4) {                             /* (0.10) a byte at a time: every field but a 26..32-bit one off its byte */
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
    write_through(scope, item, index);
    irq_restore(sr);
}

/* After an edit of the live project, a project load asks first (ANY CHANGES
 * WILL BE LOST. PROCEED WITH LOADING?): stock marks the project modified
 * (0x400aed98(project, 1), which keeps the mark in the settings block at
 * 0x405c8e4e) from the UI task, on the project's change signal, and an edit
 * of only mod fields sends none. core_project_edited asks for the mark from
 * any task or the audio interrupt; core_edit_tick (ev_tick, the UI task)
 * makes it, with the project model 0x4017fa0a keeps at 0x419f28a8. Core
 * calls it for its own edits (a mod page's knob, a lockable parameter's CC
 * or NRPN, RANDOMIZE PAGE and RELOAD PAGE); a mod calls it after its own
 * keys or settings change a field. */
#define PROJECT_MODEL (*(void *volatile *)0x419f28a8)
#define SET_MODIFIED ((int (*)(void *, int))0x400aed98)

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

/* ---- handles (0.11): every handle field's value in the live project ----
 * The linker gives core_handles, the descriptor of each field a mod declared
 * "handle": true (zero-terminated). core_handle_fields calls fn(field, item,
 * index, value) for each item of the live project whose handle is not 0,
 * with the item and index core_store_get takes: the 128 kits as edited, then
 * as last saved, each followed by its 12 sounds; the 128 pool sounds; the 4
 * global slots; the 128 patterns and their 13 tracks; the project. The
 * working pattern (a copy of one of the 128) is left out. UI task. */
extern const unsigned core_handles[];

typedef void (*core_handle_fn)(unsigned field, void *item, int index, unsigned value);

static void handle_at(core_handle_fn fn, unsigned field, void *item, int index)
{
    unsigned v = core_store_get(field, item, index);
    if (v)
        fn(field, item, index, v);
}

void core_handle_fields(core_handle_fn fn)
{
    const unsigned *h;
    unsigned char *kit, *p;
    int k, t, s;
    if (!fn)
        return;
    for (h = core_handles; *h; h++)
        switch (*h >> 28) {
        case SC_SOUND:
        case SC_KIT:
            for (s = 0; s < 2; s++)
                for (k = 0; k < 128; k++) {
                    kit = LIVE_KITS + (s ? LIVE_KITS_SAVED : 0) + LIVE_KIT_SIZE * k;
                    if (*h >> 28 == SC_KIT)
                        handle_at(fn, *h, kit, 0);
                    else
                        for (t = 0; t < 12; t++)
                            handle_at(fn, *h, kit + LIVE_KIT_SOUNDS + LIVE_SOUND_SIZE * t, 0);
                }
            if (*h >> 28 == SC_SOUND)
                for (k = 0; k < 128; k++)
                    handle_at(fn, *h, LIVE_POOL + LIVE_SOUND_SIZE * k, 0);
            break;
        case SC_GLOBAL:
            for (k = 0; k < 4; k++)
                handle_at(fn, *h, 0, k);
            break;
        case SC_TRACK:
        case SC_PATTERN:
            for (k = 0; k < 128; k++) {
                p = LIVE_PATTERNS + LIVE_PATTERN_SIZE * k;
                if (*h >> 28 == SC_PATTERN)
                    handle_at(fn, *h, p, 0);
                else
                    for (t = 0; t < 13; t++)
                        handle_at(fn, *h, p, t);
            }
            break;
        case SC_PROJECT:
            handle_at(fn, *h, 0, 0);
            break;
        }
}
