/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-ar2 0.11: moments and getters (README, "Moments (0.11)"). Every
 * address is OS 1.74's.
 *
 *   - ev_sysex_in: every SysEx message the router (0x40083dd8, MIDI task)
 *     gets that isn't a universal one, before stock; a handler that returns
 *     nonzero takes it, and nothing after it (other handlers, core's own
 *     pattern dump in, stock) sees it.
 *   - core_midi_try_send: a whole MIDI message out of USB, now or never: 1
 *     sent, 0 not now. From a task it goes through stock's sender
 *     (0x400029e0) under its mutex's try-lock; from an interrupt it calls
 *     the sender at once at level 5 when no task holds the mutex and the
 *     same checks pass, else it goes into core's ring, which the next
 *     sequencer tick or ev_tick sends.
 *   - the UI's current kit, track and pattern, with their items for
 *     core_store_get and core_store_set, and the live project's name;
 *   - ev_kit: a kit select (0x400a65f6) and a whole kit pushed to the
 *     engine (0x4011f90a), at the next ev_tick;
 *   - ev_project_slot: a +Drive slot cleared, initialized or pasted outside
 *     a save (0x40034bd2, 0x400351d2), at the next ev_tick.
 */

#include <stdint.h>

/* the project model, 0 until 0x4017fa0a first makes it; never call that
 * from here: the kit select site runs during boot, before it exists, and
 * the call would make it there (0x3d7ac bytes, too early) */
#define APP() (*(volatile unsigned *)0x419f28a8)
#define APP_TRACKS ((unsigned (*)(unsigned))0x400ae492)
#define CURRENT_TRACK ((int (*)(unsigned))0x400b693e)
#define ACTIVE_SLOT ((int (*)(void *))0x400aed54)
#define MODEL_KIT 0x1a4                         /* the UI's current kit: the working kits' +0x44 */
#define LIVE_PROJECT ((unsigned char *)0x407c89cb)
#define PROJECT_NAME ((const char *)LIVE_PROJECT + 8)
#define LIVE_KITS (LIVE_PROJECT + 0xa8cf18)
#define LIVE_KIT_SIZE 2531
#define LIVE_KIT_SOUNDS 42
#define LIVE_SOUND_SIZE 152
#define LIVE_PATTERNS (LIVE_PROJECT + 85784)
#define LIVE_PATTERN_SIZE 85760
#define PLAYING (*(volatile int *)0x405c8c90)  /* the sequencer's: 1 playing, 0 stopped */

/* stock's USB MIDI out (0x400857ca's USB half) */
#define TX_MUTEX ((void *)0x42f67a28)
#define TX_OWNER (*(volatile unsigned *)0x42f67a28) /* the mutex's owner, 0: free */
#define TRY_LOCK ((int (*)(void *))0x4000119c)  /* 1 taken; the owner is the current task */
#define GIVE ((void (*)(void *))0x400011ee)
#define SEND ((void (*)(int, const unsigned char *))0x400029e0)
#define PORT_STATE (*(volatile int *)0x413db254)        /* 1: configured */
#define USB_ON (*(volatile unsigned *)0xfc0b01c4)
#define TX_SYSEX (*(volatile unsigned char *)0x413db244) /* 0xF0 while a SysEx is under way */
#define TX_LEVEL (*(volatile int *)0x413db258)   /* bytes submitted, not yet done */
#define TX_AT (*(volatile int *)0x413db25c)      /* the next byte of the 4 KB buffer */
#define TX_ROOM 0xffb                            /* the sender waits above this */
/* the sender's transfer requests (0x40003a64): a free list whose head is
 * here, linked at +0x34; with none free, or the head still busy (+4's top
 * bit), the sender waits for one to complete */
#define REQ_FREE (*(unsigned char *volatile *)0x413db390)
#define REQ_NEXT 0x34

void core_rows_now_reset(void);
int core_step_pattern_now(void);

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

static int in_interrupt(void)
{
    unsigned short sr;
    __asm__ volatile("move.w %%sr,%0" : "=d"(sr));
    return (sr & 0x0700) != 0;
}

/* ---- the current kit, track, pattern and the project's name ---- */

int core_current_kit(void)
{
    unsigned app = APP();
    int k;
    if (!app)
        return -1;
    k = *(volatile int *)(app + MODEL_KIT);
    return (unsigned)k < 128 ? k : -1;
}

void *core_current_kit_item(void)
{
    int k = core_current_kit();
    return k < 0 ? 0 : LIVE_KITS + LIVE_KIT_SIZE * k;
}

int core_current_track(void)
{
    unsigned app = APP();
    int t;
    if (!app)
        return -1;
    t = CURRENT_TRACK(APP_TRACKS(app));
    return (unsigned)t < 13 ? t : -1;
}

/* the current track's sound in the current kit; 0 on the FX track (its
 * parameters are the kit's) */
void *core_current_sound_item(void)
{
    unsigned char *kit = core_current_kit_item();
    int t = core_current_track();
    return kit && t >= 0 && t < 12 ? kit + LIVE_KIT_SOUNDS + LIVE_SOUND_SIZE * t : 0;
}

int core_current_pattern(void)
{
    return core_step_pattern_now();
}

void *core_current_pattern_item(void)
{
    int p = core_current_pattern();
    return p < 0 ? 0 : LIVE_PATTERNS + LIVE_PATTERN_SIZE * p;
}

/* the live project's name: 16 bytes, not always zero-terminated. SAVE AS
 * has set the new name before ev_project_save's pre 1. */
const char *core_project_name(void)
{
    return PROJECT_NAME;
}

/* ---- ev_sysex_in ---- */

typedef int32_t (*ev_sysex_in_t)(const unsigned char *data, int32_t len, int32_t iface);
extern const ev_sysex_in_t ev_sysex_in[];

int core_sysex_in(const unsigned char *data, int len, int iface)
{
    const ev_sysex_in_t *f;
    for (f = ev_sysex_in; *f; f++)
        if ((*f)(data, len, iface))
            return 1;
    return 0;
}

/* ---- core_midi_try_send ---- */

/* whether msg is one whole message the sender takes as it is: a status byte
 * first, and a SysEx (or one inside) ended in the same call, so stock's
 * framing state never stays open on a mod's bytes */
static int whole(int len, const unsigned char *msg)
{
    int i, ex = 0;
    if (len < 1 || len > 512 || !(msg[0] & 0x80))
        return 0;
    for (i = 0; i < len; i++)
        if (msg[i] == 0xf0)
            ex = 1;
        else if (msg[i] == 0xf7)
            ex = 0;
    return !ex;
}

/* from a task: send now if nothing would make the sender wait; the sender
 * puts one 4-byte event in the buffer for each 1 to 3 bytes */
static int requests_free(int need)
{
    unsigned char *r = REQ_FREE;
    if (!r || (signed char)r[4] < 0)
        return 0;
    for (; r; r = *(unsigned char **)(r + REQ_NEXT))
        if (--need == 0)
            return 1;
    return 0;
}

/* whether the sender takes len bytes now without waiting: the port up, no
 * stock SysEx open, 4 bytes a message byte below its level's ceiling, not
 * a long message near the buffer's end, and the requests it needs free.
 * With the mutex held (a task) or no task holding it (an interrupt). */
static int room(int len)
{
    int at;
    if (PORT_STATE != 1 || !USB_ON || TX_SYSEX == 0xf0)
        return 0;
    at = TX_AT;
    if (TX_LEVEL + 4 * len > TX_ROOM ||
        (len > 0x2f && at > 0xfbf && at - 0x3f <= TX_LEVEL))
        return 0;
    return requests_free(at + 4 * len > 0xfff ? 2 : 1);
}

/* core's ring: messages that waited, sent in order; each is its length
 * byte then its bytes. ring_in moves with interrupts masked; ring_out only
 * where the sender is core's: a task holding the mutex, or an interrupt
 * while no task holds it and no other interrupt is sending (irq_busy). */
#define RING 1024
#define RING_MSG 128
static unsigned char ring[RING];
static volatile unsigned ring_in, ring_out;     /* free-running */
static volatile int irq_busy;

/* the interrupt path's counts and its longest time at level 5, in DMA
 * timer 0 ticks (README, "Checked so far") */
#define DTCN0 (*(volatile unsigned *)0xfc07000c)
struct {
    char magic[4];
    uint32_t now, queued, drained, refused, max_ticks;
} core_midi_irq = {{'M', 'I', 'R', 'Q'}};

static int queue(int len, const unsigned char *msg)
{
    unsigned short sr;
    int i;
    if (len > RING_MSG)
        return 0;
    sr = irq_mask();
    if (RING - (ring_in - ring_out) < (unsigned)len + 1) {
        irq_restore(sr);
        return 0;
    }
    ring[ring_in % RING] = (unsigned char)len;
    for (i = 0; i < len; i++)
        ring[(ring_in + 1 + i) % RING] = msg[i];
    ring_in += len + 1;
    irq_restore(sr);
    return 1;
}

/* from a task holding the mutex: the ring's messages while the sender has
 * room; 1 when it is empty */
static int drain_ring(void)
{
    unsigned char m[RING_MSG];
    int len, i;
    while (ring_out != ring_in) {
        len = ring[ring_out % RING];
        if (!room(len))
            return 0;
        for (i = 0; i < len; i++)
            m[i] = ring[(ring_out + 1 + i) % RING];
        SEND(len, m);
        ring_out += len + 1;
    }
    return 1;
}

/* level 5 at least, as the companion's step clock sent (the audio
 * interrupt below it can't cut into the sender) */
static unsigned short irq_raise5(void)
{
    unsigned short sr, raised;
    __asm__ volatile("move.w %%sr,%0" : "=d"(sr));
    if ((sr & 0x0700) >= 0x0500)
        return sr;
    raised = (unsigned short)((sr & 0xf8ff) | 0x0500);
    __asm__ volatile("move.w %0,%%sr" : : "d"(raised) : "memory");
    return sr;
}

/* one message to the sender from an interrupt, at level 5 for the call
 * only; 0 when it has no room */
static int irq_one(int len, const unsigned char *msg)
{
    unsigned short sr = irq_raise5();
    unsigned t0 = DTCN0;
    int ok = room(len);
    if (ok)
        SEND(len, msg);
    t0 = DTCN0 - t0;
    if (t0 > core_midi_irq.max_ticks)
        core_midi_irq.max_ticks = t0;
    irq_restore(sr);
    return ok;
}

/* from an interrupt: the ring's messages queued before this call, then
 * msg, while no task holds the mutex (a task inside the sender) and no
 * other interrupt is sending (irq_busy); never the try-lock or the give,
 * which are a task's. A message queued by an interrupt that cuts in stays
 * behind msg. msg 0: the ring only */
static int irq_send(int len, const unsigned char *msg)
{
    unsigned char m[RING_MSG];
    unsigned short sr;
    unsigned end;
    int sent = 0, n, i;
    sr = irq_mask();
    if (irq_busy || TX_OWNER != 0) {
        irq_restore(sr);
        goto later;
    }
    irq_busy = 1;
    end = ring_in;
    irq_restore(sr);
    while (ring_out != end) {
        n = ring[ring_out % RING];
        for (i = 0; i < n; i++)
            m[i] = ring[(ring_out + 1 + i) % RING];
        if (!irq_one(n, m))
            break;
        ring_out += n + 1;
        core_midi_irq.drained++;
    }
    if (msg && ring_out == end)
        sent = irq_one(len, msg);
    irq_busy = 0;
later:
    if (!msg)
        return 0;
    if (sent) {
        core_midi_irq.now++;
        return 1;
    }
    if (queue(len, msg)) {
        core_midi_irq.queued++;
        return 1;
    }
    core_midi_irq.refused++;
    return 0;
}

/* the sequencer clock's exit, before ev_seq_tick (seq.c): what waited */
void core_midi_irq_drain(void)
{
    if (ring_out != ring_in)
        irq_send(0, 0);
}

/* from a task: under the try-lock, the ring first, then msg if the ring
 * emptied and there is room; a message that would overtake a queued one
 * is queued instead */
static int task_send(int len, const unsigned char *msg)
{
    int r = 0;
    if (!TRY_LOCK(TX_MUTEX))
        return ring_out != ring_in ? queue(len, msg) : 0;
    if (!drain_ring())
        r = queue(len, msg);
    else if (room(len)) {
        SEND(len, msg);
        r = 1;
    }
    GIVE(TX_MUTEX);
    return r;
}

static void drain(void)
{
    if (TRY_LOCK(TX_MUTEX)) {
        drain_ring();
        GIVE(TX_MUTEX);
    }
}

int core_midi_try_send(int len, const unsigned char *msg)
{
    if (!msg || !whole(len, msg))
        return 0;
    if (in_interrupt())
        return irq_send(len, msg);
    return task_send(len, msg);
}

/* ---- ev_kit and ev_project_slot, delivered at ev_tick (UI task) ---- */

typedef void (*ev_kit_t)(int32_t kit, int32_t how);
extern const ev_kit_t ev_kit[];
typedef void (*ev_project_slot_t)(int32_t slot, uint32_t how);
extern const ev_project_slot_t ev_project_slot[];

#define KIT_SELECTED 1
#define KIT_PUSHED 2
#define SLOT_CLEARED 1
#define SLOT_INIT 2
#define SLOT_PASTED 3
#define SLOT_FAILED 0x10000

static volatile int selected = -1;              /* the last select's kit, -1: none */
static volatile unsigned pushed[4];             /* the kits pushed since the last tick */
static volatile unsigned pushed_other;          /* a push of data in no kit slot */
#define SLOTS 16
static volatile short slot_ev[SLOTS];
static volatile unsigned slot_how[SLOTS];
static volatile unsigned slots_in, slots_out;

static void slot_event(int slot, unsigned how)
{
    unsigned short sr = irq_mask();
    if (slots_in - slots_out < SLOTS) {
        slot_ev[slots_in % SLOTS] = (short)slot;
        slot_how[slots_in % SLOTS] = how;
        slots_in++;
    }
    irq_restore(sr);
}

/* 0x400a65f6(working kits, kit, a, b): the UI selects a kit (the pattern's
 * kit, KIT menu LOAD, project load, CREATE NEW, boot). It doesn't push the
 * kit to the engine: each track moves to it at its next trig. A change of
 * kit while stopped drops the tracks' playing-trig mod locks (0.9 finding
 * 1); while playing, each track's next trig sets its own, as stock's locks
 * do, so locks a new pattern's first trigs set before the UI's select stay. */
void core_kit_select_body(void *working, int kit, int a, int b);

void core_kit_select(void *working, int kit, int a, int b)
{
    int was = core_current_kit(), now;
    core_kit_select_body(working, kit, a, b);
    now = core_current_kit();
    if (now >= 0 && now != was && PLAYING != 1)
        core_rows_now_reset();
    if (now >= 0)
        selected = now;
}

/* 0x4011f90a(kit data): a whole kit to the engine (RELOAD KIT, project
 * load, CREATE NEW, boot, a SysEx kit in, the kit browser's edits of the
 * current kit). UI, MIDI or job task. */
void core_kit_push_body(const unsigned char *kit);

void core_kit_push(const unsigned char *kit)
{
    unsigned o = (unsigned)(kit - LIVE_KITS);
    unsigned short sr;
    core_kit_push_body(kit);
    sr = irq_mask();
    if (kit >= LIVE_KITS && o % LIVE_KIT_SIZE == 0 && o / LIVE_KIT_SIZE < 128)
        pushed[o / LIVE_KIT_SIZE / 32] |= 1u << (o / LIVE_KIT_SIZE % 32);
    else
        pushed_other = 1;
    irq_restore(sr);
}

/* 0x40034bd2(store, slot): the project manager's CLEAR and INIT NEW (and
 * Transfer's clear), a new project written to the slot; slot -1 (Transfer)
 * initializes the live project (CREATE NEW on it, after which no slot is
 * active). Job or MIDI task. */
unsigned core_slot_clear_body(void *store, int slot);

unsigned core_slot_clear(void *store, int slot)
{
    unsigned app = APP();
    int active = slot == -1 && app ? ACTIVE_SLOT((void *)app) : slot;
    unsigned r = core_slot_clear_body(store, slot);
    if (slot == -1)
        slot_event((unsigned)active < 128 ? active : 255, SLOT_INIT | ((r & 255) ? 0 : SLOT_FAILED));
    else if ((unsigned)slot < 128)
        slot_event(slot, SLOT_CLEARED | ((r & 255) ? 0 : SLOT_FAILED));
    return r;
}

/* 0x400351d2(store, slot, from a file): the project manager's paste of a
 * copied project into a slot. Job task. */
unsigned core_slot_paste_body(void *store, int slot, int file);

unsigned core_slot_paste(void *store, int slot, int file)
{
    unsigned r = core_slot_paste_body(store, slot, file);
    if ((unsigned)slot < 128)
        slot_event(slot, SLOT_PASTED | ((r & 255) ? 0 : SLOT_FAILED));
    return r;
}

/* ev_tick (order 2): the queued sends, then the events, in the UI task */
void core_moments_tick(void *ctrl)
{
    const ev_kit_t *f;
    const ev_project_slot_t *g;
    unsigned p[4], other, i, how;
    unsigned short sr;
    int k, s;
    (void)ctrl;
    if (ring_out != ring_in)
        drain();
    sr = irq_mask();
    k = selected;
    selected = -1;
    for (i = 0; i < 4; i++) {
        p[i] = pushed[i];
        pushed[i] = 0;
    }
    other = pushed_other;
    pushed_other = 0;
    irq_restore(sr);
    if (k >= 0)
        for (f = ev_kit; *f; f++)
            (*f)(k, KIT_SELECTED);
    for (i = 0; i < 128; i++)
        if (p[i / 32] >> (i % 32) & 1)
            for (f = ev_kit; *f; f++)
                (*f)((int32_t)i, KIT_PUSHED);
    if (other)
        for (f = ev_kit; *f; f++)
            (*f)(-1, KIT_PUSHED);
    while (slots_out != slots_in) {
        sr = irq_mask();
        s = slot_ev[slots_out % SLOTS];
        how = slot_how[slots_out % SLOTS];
        slots_out++;
        irq_restore(sr);
        for (g = ev_project_slot; *g; g++)
            (*g)(s, how);
    }
}
