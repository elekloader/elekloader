/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * TELOS for the Octatrack: a project's mods (as TELOS on the Digitone mk1 does).
 *
 * Every project save carries the mods that were running: a core_projdata block, "TLMS", with a hash
 * of each mod's id and one of its version (the flashed build's mods and the card's that loaded).
 * When a loaded project's mods differ from what runs, a dialog counts what YES would change
 * (+ turn on, - turn off, ~ the project's version of a mod, both on the card) and what it can't
 * (? not on this card or in this build, ! another version, or one the boot refused). YES ticks the
 * card's mods as the project had them and saves the set for the next start; MAIN MENU > MODS shows
 * the ticks. The next start's own load of that project says only what is still not as it had it.
 * Otherwise the start's load says nothing, nor does a project without the block.
 *
 * The block goes after the other mods' (order 95): when core's project data is full, theirs keep
 * their room and the project has no list.
 */
#include "octatrack/core.h"
#include "telos.h"

extern char core_desc[];

#define SET_MAX     32
#define MAGIC       0x544d3031u          /* "TM01" */
#define PENDING     "/ELEKLOADER/MODS/project"

static struct {                          /* the project's block: what a save writes */
    u32 magic;
    u32 n;
    u32 pair[SET_MAX * 2];               /* id, version */
} pdata __attribute__((aligned(4)));


struct set { u32 n, id[SET_MAX], ver[SET_MAX]; };
static struct set run;                   /* what runs: the flashed build's mods (not core) and the card's */

static int has(const struct set *s, u32 id)
{
    u32 i;
    for (i = 0; i < s->n; i++)
        if (s->id[i] == id)
            return (int)i;
    return -1;
}

static void add(struct set *s, u32 id, u32 ver)
{
    if (s->n < SET_MAX && has(s, id) < 0) {
        s->id[s->n] = id;
        s->ver[s->n++] = ver;
    }
}

static u32 hash(const char *s) { return dl_hash(s, dl_strlen(s)); }

static int loaded_now(int i) { return telos_cm[i].boot_on && telos_cm[i].state == DL_ST_LOADED; }

/* What runs, and the block a save writes. */
static void run_build(void)
{
    u32 i, n, pool, mods;
    run.n = 0;
    pool = dl_rd32((const u8 *)core_desc + 0x18);
    n = dl_rd16((const u8 *)core_desc + 0x20);
    mods = dl_rd32((const u8 *)core_desc + 0x28);
    for (i = 0; i < n; i++) {
        const u8 *m = (const u8 *)core_desc + mods + 16 * i;
        if (m[8] != 1)                   /* flashed mods only: core and TELOS are the firmware */
            continue;
        add(&run, hash(core_desc + pool + dl_rd32(m)), hash(core_desc + pool + dl_rd32(m + 4)));
    }
    for (i = 0; i < (u32)telos_ncm; i++)
        if (loaded_now((int)i))
            add(&run, telos_cm[i].hid, telos_cm[i].hver);
    pdata.magic = MAGIC;
    pdata.n = run.n;
    for (i = 0; i < SET_MAX; i++) {
        pdata.pair[2 * i] = i < run.n ? run.id[i] : 0;
        pdata.pair[2 * i + 1] = i < run.n ? run.ver[i] : 0;
    }
}

/* ---- the check ---------------------------------------------------------------------------------- */
static struct { u8 entry, on; } act[NMOD * 2];
static u8 nact;
static u8 n_on, n_off, n_swap, n_missing, n_other;
static u32 proj[SET_MAX * 2], nproj;     /* the loaded project's list */
volatile u32 telos_pj[4];                /* [0] loads, [1] on | off << 8 | swap << 16 | ask << 24,
                                          * [2] missing | other << 8, [3] the last load's found */

static void action(int entry, int on)
{
    if (nact < sizeof act / sizeof act[0]) {
        act[nact].entry = (u8)entry;
        act[nact++].on = (u8)on;
    }
}

/* The card file of (id, ver), any version when ver is 0, or -1. */
static int entry_of(u32 id, u32 ver)
{
    int i;
    for (i = 0; i < telos_ncm; i++)
        if (telos_cm[i].hid == id && (!ver || telos_cm[i].hver == ver))
            return i;
    return -1;
}

/* What it takes for the project's mods to run. issues_only: only what can't be switched (the start
 * after a YES). */
static void check(const u32 *pair, u32 n, int issues_only)
{
    struct set saved;
    u32 i;
    int k;
    nact = n_on = n_off = n_swap = n_missing = n_other = 0;
    saved.n = 0;
    for (i = 0; i < n && i < SET_MAX; i++)
        add(&saved, pair[2 * i], pair[2 * i + 1]);
    for (i = 0; i < saved.n; i++) {
        u32 id = saved.id[i], ver = saved.ver[i];
        int r = has(&run, id), want = entry_of(id, ver);
        if (r >= 0 && run.ver[r] == ver)
            continue;                    /* running as saved */
        if (r >= 0) {                    /* running at another version */
            int cur = entry_of(id, run.ver[r]);
            if (want >= 0 && cur >= 0 && !issues_only) {
                action(cur, 0);
                action(want, 1);
                n_swap++;
            } else {
                n_other++;
            }
            continue;
        }
        if (want < 0)
            want = entry_of(id, 0);      /* another version of it, at least */
        if (want >= 0 && telos_cm[want].boot_on && telos_cm[want].state != DL_ST_LOADED)
            n_other++;                   /* on, and the start refused it */
        else if (want >= 0 && !issues_only) {
            action(want, 1);
            n_on++;
        } else if (want < 0)
            n_missing++;
    }
    if (!issues_only)                    /* the card's mods the project did not use: off */
        for (k = 0; k < telos_ncm; k++)
            if (loaded_now(k) && has(&saved, telos_cm[k].hid) < 0) {
                action(k, 0);
                n_off++;
            }
    telos_pj[1] = n_on | (u32)n_off << 8 | (u32)n_swap << 16 | (u32)(nact != 0) << 24;
    telos_pj[2] = n_missing | (u32)n_other << 8;
}

/* ---- the load, on the engine task --------------------------------------------------------------- */
static volatile u8 show;                 /* 1: ask (LOAD PROJECT MODS?), 2: tell (issues only) */
static u8 boot_load;                     /* the next load is the start's own */
static struct ot_file pf;

static void proj_loaded(int32_t found)
{
    int boot = boot_load;
    u32 i;
    telos_pj[0]++;
    telos_pj[3] = (u32)found;
    boot_load = 0;
    if (boot) {
        /* a start after a YES: the list YES set up, and what is still not as it had it */
        static u32 pend[1 + SET_MAX * 2];
        int got = 0;
        if (ot_file_open(&pf, PENDING, "r") >= 0) {
            got = ot_file_read(&pf, pend, 4) == 1 && pend[0] && pend[0] <= SET_MAX
                  && ot_file_read(&pf, pend + 1, 8 * pend[0]) == 1;
            ot_file_close(&pf);
        }
        if (got) {
            check(pend + 1, pend[0], 1);
            if (n_missing || n_other)
                show = 2;
            if (ot_file_open(&pf, PENDING, "w") >= 0)    /* once */
                ot_file_close(&pf);
        }
    } else if (found && pdata.magic == MAGIC && pdata.n <= SET_MAX) {
        nproj = pdata.n;
        for (i = 0; i < 2 * nproj; i++)
            proj[i] = pdata.pair[i];
        check(proj, nproj, 0);
        show = nact ? 1 : (n_missing || n_other) ? 2 : 0;
    }
    {
        /* what runs into the block; core writes the project's file (its .work) only for a block
         * marked changed, about 2 s later, and SAVE PROJECT keeps it */
        static u32 was[2 + SET_MAX * 2];
        u32 k, diff = 0;
        for (k = 0; k < sizeof was / 4; k++)
            was[k] = ((const u32 *)&pdata)[k];
        run_build();
        for (k = 0; k < sizeof was / 4; k++)
            diff |= was[k] ^ ((const u32 *)&pdata)[k];
        if (diff)
            core_projdata_dirty(OT_TAG('T', 'L', 'M', 'S'));
    }
}

/* After the boot's link and the scan (telos.c): what runs, and the start's load to come. */
void telos_proj_boot(void)
{
    run_build();
    boot_load = 1;
}

const struct core_projdata telos_projdata = { OT_TAG('T', 'L', 'M', 'S'), sizeof pdata, &pdata, proj_loaded };

/* ---- the dialog --------------------------------------------------------------------------------- */
#define DIALOG_OPEN (*(volatile u32 *)0x460e5cd0)

static void write_pending(u32 unused)
{
    static u32 out[1 + SET_MAX * 2];
    u32 i;
    (void)unused;
    out[0] = nproj;
    for (i = 0; i < 2 * nproj; i++)
        out[1 + i] = proj[i];
    if (ot_file_open(&pf, PENDING, "w") >= 0) {
        ot_file_write(&pf, out, 4 + 8 * nproj);
        ot_file_close(&pf);
    }
}

static void answer(int c)
{
    u32 i;
    if (c == 0) {                        /* YES: tick as the project had them */
        for (i = 0; i < nact; i++)
            telos_cm[act[i].entry].on = act[i].on;
        telos_post_active();
        ot_engine_post(write_pending, 0);
    }
    telos_redraw_later();
}

static char *num(char *d, char tag, u32 v)
{
    *d++ = tag;
    if (v >= 10)
        *d++ = (char)('0' + (v / 10) % 10);
    *d++ = (char)('0' + v % 10);
    *d++ = ' ';
    return d;
}

static u16 wait;

/* ev_tick (menu.c's), the sys task: the dialog, once the load's own screens have gone. */
void telos_proj_tick(void)
{
    static char l1[32];
    char *d = l1;
    if (!show) {
        wait = 0;
        return;
    }
    if (++wait < 60 || DIALOG_OPEN)      /* a second after the load */
        return;
    if (n_on)
        d = num(d, '+', n_on);
    if (n_off)
        d = num(d, '-', n_off);
    if (n_swap)
        d = num(d, '~', n_swap);
    if (n_missing)
        d = num(d, '?', n_missing);
    if (n_other)
        d = num(d, '!', n_other);
    d[d > l1 ? -1 : 0] = 0;
    if (show == 1)
        telos_say("LOAD PROJECT MODS?", l1, "YES: SET FOR NEXT START", answer);
    else
        telos_say("PROJECT MODS NOT ALL HERE", l1, "", 0);
    show = 0;
}
