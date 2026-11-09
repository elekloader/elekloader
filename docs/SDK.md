# Shared machine SDK (alpha)

The shared SDK lets a mod declare a machine once in C (its knobs, its
pages, what they show and, optionally, its sound) and build that one source
for several Elektron devices. Each device's companion puts the machine in
the native machine selector, pages, parameters and LFO destinations. The
mod names no firmware address and no hook site. The API is alpha: it may
change, and mods must be rebuilt when it does.

| | Digitakt mk1 | Digitone mk1 / Keys | Digitone II |
|---|---|---|---|
| Stock OS | 1.54 | 1.44 | 1.12 |
| Core and companion | `mods/core` (4.3 or later), `mods/machine-pages` | `mods/core-dn1` (5.3 or later), `mods/machines-dn1` | `mods/core-dn2` (0.4 or later), `mods/machines-dn2` |
| Choose a machine | FUNC+SRC machine menu | hold a track key, Mod Menu → MACHINES | FUNC+SYN → MACHINE SEL |
| Its pages show on | SRC | SYN2, in place of its pages on that track | SYN |
| Pages, labels, hiding, glyphs | yes | yes | yes |
| `render` and knob defaults | yes | yes | yes, `render` in SHARC assembly ([`render` on the Digitone II](#render-on-the-digitone-ii)) |
| Mono machines ([`play` and `prio`](#play-and-prio)) | yes, live keys (the trig keys, keyboard mode, MIDI notes; not the sequencer's trigs) | yes, live keys (KEYBOARD mode, MIDI notes; not the sequencer's trigs), and one voice a track | yes: the track's own PLAY MODE and MONO NOTE PRIO, which machines-dn2 sets from the machine |
| MIDI CCs in `render` (`v->cc`) | yes | yes | yes |

Other devices and OS releases, the Digitakt II among them, are not SDK
targets. The builder's `SDK_ALPHA_TARGETS`
(elekloader/sdk/sdk_alpha_build.py) is the table it enforces. The
[header](../elekloader/sdk/include/elekloader/sdk_alpha_machine.h) has
every type and field. For other kinds of mod, start with
[Adapting your mod](ADAPTING.md).

## Quickstart

A saw with two pages: LEVEL, and TUNE in semitones, whose value shows a
sign when it is tuned up, and whose glyph is then an arrow. Put it in
`my-saw/machine.c`:

```c
#include "elekloader/sdk_alpha_machine.h"

enum { LEVEL, TUNE };                       /* knob ids: their index in knobs[] */
static const struct elek_glyph sharp = { .rows = { [4] = 0x0180, 0x03c0, 0x07e0, 0x0ff0, 0x0180, 0x0180 } };
static const struct elek_knob knobs[] = {
    [LEVEL] = { "LEVEL", .max = 127, .def = 100 },
    [TUNE] = { "TUNE", .min = -12, .max = 12 },             /* def omitted: 0 */
};

static void present(const struct elek_ui *ui)
{
    int32_t tune = ui->knob[TUNE];
    ui->view[TUNE].value = elek_num(ui, tune > 0 ? "+" : "", tune);   /* "-12" .. "+12" */
    ui->view[TUNE].glyph = tune > 0 ? &sharp : 0;                    /* 0: the stock graphic */
}

struct state { uint32_t phase; };           /* this voice's, kept from block to block */

static void render(const struct elek_voice *v, elek_sample *out)
{
    struct state *s = ELEK_STATE(struct state, v);
    uint32_t inc = elek_phase_inc(v->pitch + v->knob[TUNE] * 65536);   /* semitones, 16.16 */
    elek_sample level = elek_level(v->knob[LEVEL], 127);
    if (v->start)
        s->phase = 0;
    for (int i = 0; i < ELEK_VOICE_FRAMES; ++i) {
        elek_sample saw = elek_from_q15((int32_t)(s->phase >> 16) - 32768);
        out[i] = elek_mul(elek_mul(saw, level), ELEK_SAMPLE(0.5));
        s->phase += inc;
    }
}

static const struct elek_machine machine = {
    .id = 40, .name = "My Saw", .short_name = "SAW", ELEK_KNOBS(knobs),
    .pages = {
        { "MAIN", { [ELEK_A] = &knobs[LEVEL], [ELEK_B] = &knobs[TUNE] } },
        { "TUNE", { [ELEK_A] = &knobs[TUNE] } },
    },
    .present = present, .render = render,
};
ELEK_REGISTER_MACHINE(machine);
```

and `my-saw/mod.json`:

```json
{
 "id": "my-saw", "version": "0.1", "license": "GPL-2.0-or-later",
 "devices": {"digitakt-mk1": ["1.54"], "digitone-mk1": ["1.44"]},
 "sources": ["machine.c"]
}
```

`devices` replaces `device` and `os`. The Digitone II is left out because
its `render` is SHARC assembly ([`render` on the Digitone II](#render-on-the-digitone-ii));
[sdk-hello-world](../examples/sdk-hello-world),
[sdk-glyphs](../examples/sdk-glyphs) and [sdk-sine](../examples/sdk-sine)
build for all three.

From the elekloader checkout, run it on your computer first, with no
device or emulator ([Run it on your computer](#run-it-on-your-computer)):

```bash
python -m elekloader.sdk.sdk_alpha_host my-saw
```

It prints the labels `present` gives with each knob at its min, default
and max (`TUNE=12 ... TUNE "T12" glyph`), then the render's level and
pitch: `peak 0.394, about 261.6 Hz`.

Then build core, the companion and the mod for one stock file, with a
ColdFire cross compiler, into an empty folder. Set `CORE` and `COMPANION`
from the table above; for the Digitakt:

```bash
export ELEKLOADER_CROSS=m68k-elf-
STOCK=Digitakt_OS1.54.syx CORE=mods/core COMPANION=mods/machine-pages
python -m elekloader.sdk.build $CORE --stock $STOCK --out build
python -m elekloader.sdk.build $COMPANION --stock $STOCK --out build
python -m elekloader.sdk.build my-saw --stock $STOCK --out build
```

Each prints `BUILT build/...`; a mod listing several targets gets one
file each, such as `my-saw-0.1-digitakt-mk1-os1.54.elemod`. Then lint and
patch:

```bash
python -m elekloader.lint build/my-saw-*.elemod --stock $STOCK \
  --with build/core-*.elemod --with build/machine*.elemod
python -m elekloader.patch --stock $STOCK --mod build/core-*.elemod \
  --mod build/machine*.elemod --mod build/my-saw-*.elemod --out test.syx --version SAW1
```

Lint prints two `OK:` lines, the second `OK: links as core 4.3 >
machine-pages 1.10 > my-saw 0.1`; patch ends with `WROTE test.syx`. Load it
as in [Adapting your mod](ADAPTING.md).

## Reference

### Machine and knobs

- `.id` is the machine's native slot: 4..127, 5..127 on the Digitone II.
  Give each independently distributed machine its own stable id: the
  linker refuses two machines with the same one, SDK or native.
- `.name` has 1..16 printable ASCII characters, `.short_name` 1..4.
- `ELEK_KNOBS(knobs)` gives the knobs, at most 15. A knob's id is its index
  in that array, whatever page or encoder shows it: it is its stored value
  and its LFO destination, and a new label or hiding does not change it.
- A knob's `label` has 1..5 printable ASCII characters. `max` is required
  and above `min` (0 when omitted). The value the code sees is the native
  value mapped onto `min..max`, rounded to the nearest integer, halves up.
- `def` is the value the knob starts at, clamped into `min..max`; omitted,
  it is 0, so a `-12..12` knob starts at 0 and a `10..20` one at 10.
  Selecting the machine and clearing a sound use it; paste and project load
  restore edited values.

### Pages

`.pages` is a fixed `pages[2]`. Each has a `label` of 1..16 characters and
`encoder[8]`, `ELEK_A`..`ELEK_H`, each `&knobs[ID]` or nothing. A page
without a label is absent, and the second needs the first. One knob may
sit on both pages: it keeps one value. A machine that leaves `.pages` out
shows one blank page titled with its name. The value under a knob is its
mapped value, or the text `present` gives it ([Device differences](#device-differences)
has its width).

### `present` and `elek_view`

`.present` is optional. It gets one `const struct elek_ui *ui`: the
`track`, the `page` shown (`ELEK_UI_NO_PAGE` when the call is not for a
page, such as naming LFO destinations, or for a page-less machine's blank
page) and `ui->knob[ID]`, every knob's mapped value, on every page.

It sets `ui->view[ID]`, which each call starts from the declaration:

```c
ui->view[OSC].label = "OSC2";                                    /* text: assign it */
ui->view[TYPE].value = ui->knob[TYPE] ? "PULS" : "SAW";
ui->view[FINE].value = elek_fixed(ui, fine > 0 ? "+" : "", fine, 1);   /* 24: "+2.4" */
ui->view[FREQ].value = elek_num(ui, freq > 0 ? "+" : "", freq);        /* "+7", "-12" */
```

- `label`: the knob's name. Null is the declared one.
- `value`: the text for `ui->knob[ID]`, under the knob and in the pop-up.
  Null, which each call starts as, or empty shows the number. For tenths,
  declare a finer range, such as `-500..500` for -50.0..50.0 cents.
- `hidden`: nonzero blanks the knob on every page. It keeps its value and
  its LFO identity; its encoder does not edit it while hidden.
- `glyph`: sixteen rows of sixteen bits, top row first, bit 15 leftmost;
  it replaces the knob's graphic. Null is the stock graphic. Keep glyphs
  in static const storage: the screen reads them after the call.

Text is a string literal, constant data, or what `elek_num(ui, prefix, v)`
or `elek_fixed(ui, prefix, v, places)` returns: `v` in decimal, with
`places` decimals for `elek_fixed`, after the prefix, which is cut to fit.
Do not point at a local buffer: it is gone when the companion reads the
text. A text shows at most five characters, `#` when a
number alone needs more; nonprintable bytes show as `?`, and a device may
show letters in upper case.

A companion calls `.present` when the machine, the track, the page or a
knob's value has changed since its last call, and may call it again with
the same inputs. To show a value it was not given (a lock, an LFO's, the
pop-up's), it calls `.present` again with that value in `ui->knob[ID]`,
the other inputs as before, and uses only `view[ID].value`. Keep it pure:
no state, no counting calls.

### `render` and `elek_voice`

```c
void render(const struct elek_voice *v, elek_sample *out);
```

`.render` is optional. It fills one voice's `ELEK_VOICE_FRAMES` (32) mono
samples at `ELEK_SAMPLE_RATE` (48 kHz). `v` gives:

- `knob[ID]`: this voice's values, mapped, with its locks and LFOs (not
  `.present`'s editing values);
- `track` from 0, `pitch` in MIDI semitones, 16.16 (69 << 16 is 440 Hz),
  and `velocity` 1..127;
- `start`, nonzero in a note's first block: the code decides what to reset;
- `state`, read with `ELEK_STATE`;
- `idle`, the frames the voice was not rendered since its last render
  (below), up to `UINT32_MAX`, and 0 in the first render after its state
  is zeroed.

**Silent voices.** A voice whose AMP envelope has ended, with no note
starting it, is not rendered: the companion writes silence for it and
counts its frames. A machine whose state should move on regardless
catches up from `idle`: a free-running oscillator adds `inc * idle` to its
phase.

**MIDI CCs.** `v->cc[n]`, `n` 0..127, is the value, 0..127, of MIDI CC
`n` if one came on the voice's track since this voice's last render, and
-1 otherwise:

```c
if (v->cc[9] >= 0)              /* CC 9, any value: start over */
    s->phase = 0;
```

[sdk-sine](../examples/sdk-sine) restarts its wave on CC 9, in C and in
its Digitone II SHARC render.

- Every CC on the track's channel (or on the auto channel, while the
  track is the active one) reaches every voice of the track, once, at its
  next render. A silent voice gets it when it renders again.
- Several of the same CC before a render read as the last one. Which of
  two different CCs came first is not kept.
- The stock still applies the CC: a knob's CC still turns the knob. The
  machine decides what any CC means to it.
- On the Digitakt mk1, knob ids 0-7 take CC 16-23 and ids 8-14 CC 24-30,
  in knob id order (machine-pages 1.10).
- A voice changes machine at the track's next note. Its first render on
  the machine gets the CCs that came since its track took that machine,
  so a CC sent after choosing the machine reaches its first note.

**Voice state.** The companion keeps a 256-byte block
(`ELEK_VOICE_STATE_MAX`) for each voice, and passes the same block to every
`render` call for that voice, so what the code writes there is there in the
next block and the next note. It is zeroed at boot and when the voice
switches to another machine, never otherwise. `ELEK_STATE(struct my_state,
v)` returns it as a `struct my_state *`, and fails to compile when the
struct does not fit.

`render` may read its arguments (`cc` among them), its voice state and
constant data. A writable global is not a channel from the UI. Do not allocate, block or
call the firmware. Without `.render` the device's own sound plays: the
Digitones play FM, whose parameters the SDK knobs then turn; the Digitakt
plays nothing.

### Samples

`elek_sample` is Q31 `int32_t` on the ColdFire and `float` in a build with
`ELEK_TARGET_SHARC`. The same C builds for both when samples go through the
helpers:

| helper | what it does |
|---|---|
| `ELEK_SAMPLE(x)` | a constant, -1.0..1.0; never a variable on the ColdFire, which has no floating point |
| `elek_mul(a, b)` | multiply; Q31 rounds and saturates |
| `elek_add(a, b)` | add; Q31 saturates |
| `elek_clip(a)` | clip float to -1..1; Q31 already fits |
| `elek_level(value, max)` | an integer 0..`max` as a level 0..full scale; `max` > 0 |
| `elek_from_q15(v)` | an integer -32768..32767 as a sample |
| `elek_phase_inc(pitch)` | a 16.16 pitch as a phase step a sample, 2^32 a cycle, notes 0..127 |

Do not divide samples. Float and Q31 do not clip or round alike.

### `render` on the Digitone II

On the Digitone II the sound runs on the DSP, a SHARC, so `render` is SHARC
assembly. The builder compiles a Digitone II build's C with
`ELEK_TARGET_SHARC`, and `.render` names the routine, declared under that
macro with `render`'s signature
([sdk-sine](../examples/sdk-sine/sine.c)):

```c
#ifdef ELEK_TARGET_SHARC
void sine_render(const struct elek_voice *v, elek_sample *out);     /* sine-dn2.s */
#define RENDER sine_render
#else
static void render(const struct elek_voice *v, elek_sample *out) { ... }
#define RENDER render
#endif
```

mod.json lists the assembly as `"sharc": {"sources": ["sine-dn2.s"]}`:
relative `.s` files in the mod's folder, which the other devices ignore.
The builder assembles them with the pinned Selache (`SELACHE`, a clean
checkout at `2b26d3b`, built with `cargo build --release`) and pairs each
`.render` with the global label of its C name and a dot (`sine_render.`).
It refuses a C render (`render is C, which digitone-mk2 1.12 cannot play`)
and a missing label (`render is sine_render, but no file in mod.json's
"sharc": {"sources": [...]} has the global label sine_render.`).

machines-dn2's dispatcher calls the routine for each voice slot that plays
the machine, with R4 = `v` and R8 = `out`, 32 floats. `v` is
`struct elek_voice` as the header lays it out: `knob` at +0, `track` +4,
`start` +8, `pitch` +12, `velocity` +16, `state` +20, `idle` +24 (always
0) and `cc` +28.

- `knob[ID]` is `sdk_alpha_map`'s value for this voice's raw value, as on
  the other devices;
- `track` comes from the DSP's map of voice slots to tracks;
- `pitch` is 16.16 and `velocity` 1..127;
- `start` is set in the block after the note's trigger, whose own block
  is silent;
- `state` is the slot's 256 bytes, zeroed when the slot's machine changes;
- `cc` is 128 words, CC n's value or -1 at word n: `R0=DM(7,I0); I1=R0;
  R0=DM(9,I1);` with I0 = R4 reads `cc[9]`.

The routine may change what the dispatcher saves and restores around the
call: R0–R15, I0–I5, I12, I13, M0–M4, L0–L5, LCNTR and the ASTAT and
STKY flags. It keeps everything else as it found it, the C constants
M5 = 0, M6 = 1, M7 = -1, M13 = 0, M14 = 1 and M15 = -1, MODE1 and the
hardware stacks' depth among them, and returns as C does, which restores
I6 and I7:

```
I12=DM(M7,I6);
JUMP(M14,I12)(DB);
RFRAME;
NOP;
```

It runs on the synths' 4 KiB stack in L1, which interrupts share; a call
that overruns it mutes that machine
([MACHINES-DN2-VOICE.md](MACHINES-DN2-VOICE.md#context-and-memory)).

### `play` and `prio`

```c
.play = ELEK_MONO, .prio = ELEK_PRIO_LAST,
```

`.play` says how the machine's track plays live keys. These are the
Digitone II's PLAY MODE and MONO NOTE PRIO (TRK SETUP), and every device
carries them out the same way:

| `.play` | the track |
|---|---|
| `ELEK_POLY` (0, the default) | plays every key, as the device allocates its voices |
| `ELEK_MONO` | plays one note at a time: the held key `.prio` chooses, with the AMP restarted and `start` set in `render` at each change |
| `ELEK_MONO_LEGATO` | the same, but a change while a note sounds moves its pitch, with the AMP running on and `start` clear (single trigger) |

| `.prio` | the key that sounds |
|---|---|
| `ELEK_PRIO_LAST` (0, the default) | the last pressed; when it is released, the newest key still held |
| `ELEK_PRIO_LOW` | the lowest held |
| `ELEK_PRIO_HIGH` | the highest held |

On the Digitakt mk1 and Digitone mk1 the companion carries them out on
live keys, and keeps at most 16 held keys a track; a press past them drops
the oldest. On the Digitone II the OS does: machines-dn2 sets the track's
PLAY MODE and MONO NOTE PRIO to the machine's values (`ELEK_POLY` sets
POLY and leaves MONO NOTE PRIO), and puts them back after a change in TRK
SETUP. A track whose machine is not an SDK machine keeps its settings. See
[sdk-mono](../examples/sdk-mono/mono.c).

### Limits and refusals

The header's `ELEK_UI_*` names the limits above. A mod may register
several machines; the linker refuses more than the device holds (below).

The builder refuses what breaks a rule above, and says where, as the C
would: `sdk alpha: machine "Two Page" (id 40), knobs[0]: label "CUTOFF" has
6 characters (at most 5)`. It refuses a mono machine where the companion
does not carry it out (`ELEK_MONO is not supported on <device> <os> yet`). It also refuses floating point, imports no
companion exports, and machine claims written in `mod.json` (each machine
claims its id itself). The messages are in
elekloader/sdk/sdk_alpha_build.py. Every SDK mod imports
`sdk_alpha_abi_v1`: rebuild SDK mods and companions together when it
changes.

## Device differences

| | Digitakt mk1 | Digitone mk1 | Digitone II |
|---|---|---|---|
| First machine id | 4 | 4 | 5 (4 is MIDI) |
| SDK machines in a build | 32 | 15 | 16 |
| LFO destination lists | 2 | 2 | 3 |
| Value text | a number `#` past five characters under the knob, whole in the pop-up | a number `#` past five characters | a number `#` past five characters |
| Voices | one a track | eight, on four tracks; a mono machine's track sounds on one at a time | sixteen voice slots, which the OS allocates to tracks |
| After `render` | the native overdrive, filter and AMP; whether the AMP applies velocity is not checked | the companion's AMP envelope, which applies velocity (use it for timbre, not level), then the filter | the voice envelope, the selected stock or custom filter, pan, sends and mixer, as an mk2 synth's |

Only a mono machine's companion follows the keys' releases (the support
table above); otherwise the AMP envelope ends the note. How each companion stores knobs and renders voices is in its own
documentation: [machine-pages](../mods/machine-pages/README.md),
[machines-dn1](../mods/machines-dn1/README.md) and
[Digitone II machines](MACHINES-DN2.md). How the SDK is tested is in
[TESTS.md, "The shared machine SDK"](TESTS.md#the-shared-machine-sdk).

## Run it on your computer

Before building for a device, run the machine's `present` and `render`
with your computer's C compiler (`cc`, or `$CC`):

```bash
python -m elekloader.sdk.sdk_alpha_host examples/sdk-sine
```

```
SDK Sine (id 123), Q31 (ColdFire)
  FINE  -50..50, default 0
present on page 0, track 0, the other knobs at FINE=0:
  FINE=-50   FINE "FINE"="-50" glyph
  FINE=0     FINE "FINE"="0" glyph
  FINE=50    FINE "FINE"="+50" glyph
render: note 60, velocity 100, track 0, 375 blocks: peak 0.500, about 261.6 Hz
```

Each knob goes to its min, default and max, with the others at their
`--knob` values (or defaults), and each line shows every knob's label as
`present` left it, then its value text quoted (or, when `present` sets
none, the number the device shows), plus `hidden` and `glyph` where set. The render line
gives the peak (1.0 is full scale) and the pitch it found.

| option | what it does |
|---|---|
| `--note N` | the MIDI note to render, fractions allowed, such as 60.5 (default 60) |
| `--knob NAME=V` | a knob's value for `present` and the render, instead of its default; repeat it for more knobs |
| `--velocity V` | the note's velocity, 1..127 (default 100) |
| `--track T` | the track, from 0, for `present` and the render (default 0) |
| `--page P` | the page `present` is shown, from 0 (default 0); refused if the machine has no such page |
| `--retrigger N` | start a new note (`start` set) every N blocks (default: one held note) |
| `--cc B:N=V` | MIDI CC N = V on the track before block B, in the next render's `v->cc`; repeat it for more |
| `--blocks N` | how long to render, in blocks of 32 samples (default 375: 0.25 s) |
| `--wav out.wav` | write the render as a 48 kHz mono WAV, to listen to |
| `--float` | build with the SHARC's float samples instead of the ColdFire's Q31 |
| `--glyphs` | draw each glyph `present` shows, as 16 rows of `#` and `.` |
| `--keys "60+ 64+ 64- 60-"` | presses (`+`) and releases (`-`) of MIDI notes for a mono machine, at `--velocity`: each line shows what the companion does and the keys then held |

`--keys` runs the companions' own key logic
(elekloader/sdk/include/elekloader/sdk_alpha_keys.h):

```bash
python -m elekloader.sdk.sdk_alpha_host examples/sdk-mono --keys "60+ 64+ 64- 60-"
```

```
keys at velocity 100, ELEK_MONO, ELEK_PRIO_LAST:
  60+   play 60 (velocity 100): the AMP restarts, start set   held 60
  64+   play 64 (velocity 100): the AMP restarts, start set   held 60 64
  64-   play 60 (velocity 100): the AMP restarts, start set   held 60
  60-   silence                                               held -
```

The mod must register exactly one machine.
