/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-a4mk2 0.5: mod parameters on MIDI, the lock service's fourth part
 * (README, "Mod parameters on MIDI"): core-ar2 0.8's midi.c with the Analog
 * Four MKII's NRPN numbering and track kinds. Every address is OS 1.56's.
 *
 * An A4 NRPN is MSB x 128 + LSB, looked up in a 484-entry table of its set's
 * kind, and the set is chosen by the channel's track (dispatch 0x4006818e),
 * never by the MSB; on a channel shared with PERF it is the active track's.
 * Stock's synth, FX and CV tables use MSBs 1, 2 and 3 and leave MSB 0 empty
 * (the performance set's MSB 0 LSB 0..9 is another set's), so a mod parameter
 * is NRPN MSB 0, LSB its lock code (0x70..0x7f, the number its p-lock rows
 * and destination bytes hold), on its track's channel: the same number on
 * every track kind. Its value is the data entry MSB (0..127; the LSB is
 * ignored), or for a parameter above 127 both (0..16383), as stock sends a
 * fine parameter's. A parameter declared "cc": true gets one of the CCs no
 * table of stock's maps on a track's channel (none of the synth, FX, CV,
 * MISC, TRACK and PERF kinds' nor bank select), from the lockfile, which
 * keeps it for the field from build to build; a kit field gets none that the
 * FX or CV tables map. The linker gives core the table core_lock_ccs,
 * {field descriptor, CC}:
 *
 *   - in: the three parameter sets' NRPN in (vtable +0x8c, 0x4009ff56) and
 *     CC in (+0x90, 0x400a0b62) give a mod parameter's to core (midi.s),
 *     which sets the parameter in the track's sound of the current kit (the
 *     kit on FX and CV), clamped to its range, as its knob does, and with
 *     the routines' record flag (theirs, or MIDI CONFIG's, 0x400acd80) locks
 *     it on the playing step while live recording runs (rows.c), as stock's
 *     own set (+0x38) does;
 *   - out: when a mod's knob turns a parameter (core_lock_turned, rows.c),
 *     core sends it as stock's parameter out (+0x88, 0x4009fde4) sends a
 *     stock one: when MIDI out applies, as an NRPN or a CC as PARAM OUTPUT
 *     is set (a parameter with no CC sends none), on the track's channel.
 *
 * Stock's tables are left alone: an unknown number finds no id there, so a
 * mod NRPN or CC never reaches stock's sets, hooked or not.
 */

#define FIRST_CODE 0x70
#define LAST_CODE 0x7f
#define TRACKS 6
#define SOUND_VT 0x401a164cu                    /* the parameter sets: synth, FX, CV */
#define FX_VT 0x401a16ecu
#define CV_VT 0x401a178cu
#define S ((unsigned char *)0x4045c974)         /* the stored project */
#define WORKING_KIT_INDEX (S + 0x199330)
#define LIVE_KITS 0x411e9936u                   /* the kits as edited */
#define LIVE_KIT_SIZE 0x960
#define KIT_SOUNDS 0x1c
#define SOUND_SIZE 0x152
#define APP ((unsigned (*)(void))0x401494de)
#define MIDI_CONFIG ((unsigned (*)(unsigned))0x400a51ca)         /* (app): MIDI CONFIG */
#define REC_ON_RECEIVE ((unsigned char (*)(unsigned))0x400acd80) /* the in routines' record flag */
#define OUT_APPLIES ((int (*)(unsigned))0x400ab768)              /* MIDI out of parameters on */
#define PARAM_OUT_KIND ((int (*)(unsigned))0x400ab6e4)           /* PARAM OUTPUT: 0 NRPN, 1 CC */
#define CHANNEL ((int (*)(int))0x40068652)                       /* a track's channel (+0x94's) */
#define SEND_NRPN ((void (*)(int, int, int, int))0x40068b22)     /* (channel, nrpn, 14 bits, 1) */
#define SEND_CC ((void (*)(int, int, int))0x400689ce)            /* (channel, cc, value) */

extern const unsigned core_lock_ccs[];                          /* {field, CC}..., {0, 0} */

struct core_lock_param;
const struct core_lock_param *core_lock_by_code(int kind, unsigned code);
int core_lock_kind_of_track(int track);
int core_lock_kind(unsigned field);
int core_lock_code(unsigned field);
unsigned core_lock_field(const struct core_lock_param *p);
int core_lock_min(const struct core_lock_param *p);
int core_lock_max(const struct core_lock_param *p);
int core_lock_shift(const struct core_lock_param *p);
int core_lock_clamp(const struct core_lock_param *p, int v);
void core_store_set(unsigned field, void *item, int index, unsigned value);
void core_project_edited(void);
void core_lock_record(int track, int code, int value);

static unsigned rd32(const unsigned char *a)
{
    return (unsigned)a[0] << 24 | (unsigned)a[1] << 16 | (unsigned)a[2] << 8 | a[3];
}

/* the set's kind: synth 0, FX 1, CV 2, else -1 (the performance set) */
static int set_kind(void *set)
{
    unsigned vt = set ? *(unsigned *)set : 0;
    return vt == SOUND_VT ? 0 : vt == FX_VT ? 1 : vt == CV_VT ? 2 : -1;
}

/* the parameter of a kind's code on the track: the track's sound of the
 * current kit (the kit itself on FX and CV), from an NRPN's 14-bit value or
 * a CC's 7, clamped to its range. A parameter up to 127 takes the NRPN's
 * MSB; one above, the NRPN's whole value and the CC's coarse step
 * (core_lock_shift). */
static void set(int kind, unsigned code, int value, int cc, int track, int rec)
{
    const struct core_lock_param *p = core_lock_by_code(kind, code);
    unsigned k = rd32(WORKING_KIT_INDEX);
    int sh;
    if (!p || core_lock_kind_of_track(track) != kind || k >= 128)
        return;
    sh = core_lock_shift(p);
    value = core_lock_clamp(p, cc ? value << sh : sh ? value : value >> 7);
    core_store_set(core_lock_field(p),
                   (unsigned char *)LIVE_KITS + LIVE_KIT_SIZE * k + (track < 4 ? KIT_SOUNDS + SOUND_SIZE * track : 0),
                   0, value);
    if (rec)
        core_lock_record(track, code, value);
    core_project_edited();                     /* (0.6) store.c */
}

/* the routines' record flag: the caller's, else MIDI CONFIG's */
static int rec_flag(int flag)
{
    return (flag & 0xff) ? 1 : REC_ON_RECEIVE(MIDI_CONFIG(APP())) != 0;
}

/* 0x4009ff56 (midi.s): (set, nrpn, value, track, flag); 1 when it was a mod
 * parameter's (MSB 0, LSB 0x70..0x7f on a synth, FX or CV set, whether a
 * parameter has the code or not: no stock id has the number) */
int core_nrpn_in(void *set_, int nrpn, int value, int track, int flag)
{
    int kind = set_kind(set_);
    if (kind < 0 || nrpn < FIRST_CODE || nrpn > LAST_CODE || (unsigned)track >= TRACKS)
        return 0;
    set(kind, nrpn, value & 16383, 0, track, rec_flag(flag));
    return 1;
}

/* 0x400a0b62 (midi.s): (set, cc, value, track, flag); 1 when it was a mod
 * parameter's */
int core_cc_in(void *set_, int cc, int value, int track, int flag)
{
    const unsigned *c;
    int kind = set_kind(set_);
    if (kind < 0 || (unsigned)track >= TRACKS)
        return 0;
    for (c = core_lock_ccs; c[0]; c += 2)
        if (c[1] == (unsigned)cc) {
            if (core_lock_kind(c[0]) != kind)
                return 0;
            set(kind, core_lock_code(c[0]), value & 127, 1, track, rec_flag(flag));
            return 1;
        }
    return 0;
}

/* a field's CC, or -1 */
static int cc_of(unsigned field)
{
    const unsigned *c;
    for (c = core_lock_ccs; c[0]; c += 2)
        if (c[0] == field)
            return c[1];
    return -1;
}

/* rows.c, core_lock_turned: a mod's knob turned a parameter of the track to
 * v (0..max); PARAM OUTPUT sends it, as stock's +0x88 sends its own */
void core_midi_turned(int track, const struct core_lock_param *p, unsigned code, int v)
{
    unsigned cfg = MIDI_CONFIG(APP());
    int sh, cc;
    if (!p || (unsigned)track >= TRACKS || !OUT_APPLIES(cfg))
        return;
    v = v < 0 ? 0 : v > core_lock_max(p) ? core_lock_max(p) : v;
    sh = core_lock_shift(p);
    switch (PARAM_OUT_KIND(cfg)) {
    case 0:
        SEND_NRPN(CHANNEL(track), (int)code, sh ? v : v << 7, 1);
        break;
    case 1:
        if ((cc = cc_of(core_lock_field(p))) >= 0)
            SEND_CC(CHANNEL(track), cc, v >> sh & 127);
        break;
    }
}
