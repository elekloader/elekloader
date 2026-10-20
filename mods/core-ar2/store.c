/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-ar2 0.2: mod storage on the Analog Rytm MKII (OS 1.73 and 1.74, the
 * same addresses in both).
 *
 * Mods declare fields in their mod.json ("storage"); elekloader places them
 * at build time from the build's lockfile (docs/ADAPTING.md, "Mod storage")
 * and gives each one an absolute symbol, its descriptor, which a mod passes to
 * core_store_get and core_store_set. Zero always means the mod's default.
 *
 * Each item keeps its fields in a live home inside stock's own objects, so
 * copy, paste, undo and pattern switch carry them with no hooks:
 *
 *   scope     live home                         persisted in
 *   sound     live sound +0x6d..+0x6f (3 B)     stored sound +0x70..+0x72, with
 *                                               the layout ID (+0x73) and the
 *                                               sound generation (+0x77)
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
 * store; a project file, Total Recall's buffer and the like carry theirs.
 *
 * The SysEx project commands that copy, swap or write whole stored songs go
 * through st_song_copy, which leaves the target's store bytes, and those for
 * whole stored patterns through st_pattern_copy, which clears the target
 * slot's fields (three sites each, at different addresses on 1.73 and 1.74).
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

/* does the image's store hold this scope's data in this layout? */
static int store_ok(unsigned char *img, int scope)
{
    unsigned id;
    if (!img)
        return 0;
    id = (unsigned)B(store_byte(img, 0)) << 24 | (unsigned)B(store_byte(img, 1)) << 16 |
         (unsigned)B(store_byte(img, 2)) << 8 | B(store_byte(img, 3));
    return known_id(id) && B(store_byte(img, 3 + scope)) == gen(scope);
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

int st_pat_save(unsigned char *dst, unsigned char *live, int a)
{
    unsigned char *img = pattern_image(dst);
    int r = st_pat_save_body(dst, live, a), n = pattern_index(img, dst);
    unsigned t, j, T = SPLIT_T, P = SPLIT_P;
    if (n < 0 || !live)
        return r;
    for (t = 0; t < 13; t++)
        for (j = 0; j < T; j++)
            B(store_byte(img, HEADER + (13 * n + t) * T + j)) = live[917 * t + TRACK_HOME + j];
    for (j = 0; j < P; j++)
        B(store_byte(img, PATTERNS_AT + P * n + j)) = live[PATTERN_HOME + j];
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

/* ---- sounds and kits: their own bytes, with their own copy of the ID ---- */

int st_sound_save(unsigned char *dst, unsigned char *live, int a)
{
    int r = st_sound_save_body(dst, live, a);
    if (dst && live) {
        dst[SOUND_SPARE] = live[SOUND_HOME];
        dst[SOUND_SPARE + 1] = live[SOUND_HOME + 1];
        dst[SOUND_SPARE + 2] = live[SOUND_HOME + 2];
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
        for (j = 0; j < 3; j++)
            live[SOUND_HOME + j] = ok ? src[SOUND_SPARE + j] : 0;
    }
    return r;
}

int st_kit_save(unsigned char *dst, unsigned char *live, int a)
{
    int r = st_kit_save_body(dst, live, a), j;
    if (dst && live) {
        for (j = 0; j < 8; j++)
            dst[KIT_SPARE + j] = live[KIT_HOME + j];
        wr32(dst + KIT_SPARE + 8, core_store_ids[0]);
        dst[KIT_SPARE + 12] = gen(SC_KIT);
    }
    return r;
}

int st_kit_load(unsigned char *live, unsigned char *src)
{
    int r = st_kit_load_body(live, src), j;
    if (live && src) {
        int ok = st_image && known_id(rd32(src + KIT_SPARE + 8)) &&
                 src[KIT_SPARE + 12] == gen(SC_KIT);
        for (j = 0; j < 8; j++)
            live[KIT_HOME + j] = ok ? src[KIT_SPARE + j] : 0;
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

int st_sound_init(unsigned char *sound, int a, int b, int c)
{
    int r = st_sound_init_body(sound, a, b, c), j;
    if (sound)
        for (j = 0; j < 3; j++)
            sound[SOUND_HOME + j] = 0;
    return r;
}

/* CREATE NEW and the project reset: a fresh store, and no global data */
int st_proj_init(void *project, int a, int b, int c)
{
    int r = st_proj_init_body(project, a, b, c), g, j;
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
int st_proj_load(void *live, unsigned char *src, void *progress)
{
    int r = st_proj_load_body(live, src, progress), g, j, s;
    unsigned char *from = is_image(src) ? src : st_image;
    unsigned dropped = 0;
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

int st_file_save(unsigned char *dst, void *live, int a, int flags, void *progress)
{
    int r;
    st_image = dst;
    r = PROJECT_SAVE(dst, live, a, flags, progress);
    st_image = S;
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
    return scope == SC_PROJECT ? store_byte(S, PROJECT_AT + k) : base + k;
}

unsigned core_store_get(unsigned field, void *item, int index)
{
    unsigned scope = field >> 28, w = field >> 22 & 63, o = field & 0x3fffff, v = 0, i;
    volatile unsigned char *base = field_base(scope, item, index);
    unsigned short sr;
    if (scope != SC_PROJECT && !base)
        return 0;
    sr = irq_mask();
    for (i = 0; i < w; i++, o++)
        v = v << 1 | (B(field_byte(scope, base, o >> 3)) >> (7 - (o & 7)) & 1);
    irq_restore(sr);
    return v;
}

void core_store_set(unsigned field, void *item, int index, unsigned value)
{
    unsigned scope = field >> 28, w = field >> 22 & 63, o = field & 0x3fffff, i;
    volatile unsigned char *base = field_base(scope, item, index), *p;
    unsigned short sr;
    if (scope != SC_PROJECT && !base)
        return;
    sr = irq_mask();
    for (i = 0; i < w; i++, o++) {
        unsigned bit = 0x80 >> (o & 7);
        p = field_byte(scope, base, o >> 3);
        if (value >> (w - 1 - i) & 1)
            *p |= bit;
        else
            *p &= ~bit;
    }
    irq_restore(sr);
}
