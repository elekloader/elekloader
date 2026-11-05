# Shared machine SDK (alpha)

The shared SDK lets a mod declare its machine, pages and controls once in C
and build that source for several Elektron devices. Each device's companion
connects those declarations to its native machine selector, parameters and
screen. This is a private experiment: the API may change, and mods must be
rebuilt when it does.

**Machine UIs work on all three releases below. Both mk1 devices run SDK
renderers and apply mapped knob defaults.** Digitone II currently accepts
UI-only machines; its builder refuses `.render` and a knob that starts
above its `min`.

The [header](../elekloader/sdk/include/elekloader/sdk_alpha_machine.h) is the
reference for exact types, fields and helper behavior. This page explains
how they fit together. For other kinds of mod, start with
[Adapting your mod](ADAPTING.md).

Try a machine on your computer before building it for a device:
`python -m elekloader.sdk.sdk_alpha_host <moddir>` runs its `present` and
`render` with your C compiler, with no device or emulator
([Run it on your computer](#run-it-on-your-computer)).

## Supported devices

| | Digitakt mk1 | Digitone mk1 / Keys | Digitone II |
|---|---|---|---|
| Stock OS | 1.54 | 1.44 | 1.12 |
| Companions | core and `machine-pages` | core-dn1 4.2 or later and `machines` 0.3 | core-dn2 0.2 or later and `machines-dn2` 0.2 |
| Choose a machine | FUNC+SRC machine menu | Hold a track key, Mod Menu → MACHINES | FUNC+SYN → MACHINE SEL |
| Show its pages | SRC | SYN2, replacing its pages on that track | SYN |
| Pages / logical knobs | 2 / 15 | 2 / 15 | 2 / 15 |
| LFO destination lists | 2 | 2 | 3 |
| Labels, hiding, 16×16 glyphs | Yes | Yes | Yes |
| Mapped defaults / SDK audio callback | Implemented | Implemented | Not implemented |

Other OS releases and devices, including Digitakt II, are not SDK targets.
The builder's `SDK_ALPHA_TARGETS` (elekloader/sdk/sdk_alpha_build.py) is the matrix it enforces.
Support in elekloader's firmware loader does not imply support in this SDK.

A UI-only machine supplies no sound generator. The Digitones continue to
play stock FM; their SDK controls use FM sound parameters, so turning a
control can change that sound. A UI-only Digitakt machine produces an
empty voice.

## How it fits together

1. The mod defines an `elek_machine`, its knobs and pages in C, then
   declares it with `ELEK_REGISTER_MACHINE`.
2. The SDK builder reads the compiled declaration. It validates the layout
   and adds the device's registration, machine-ID claim and required
   companions to an ordinary format-2 `.elemod`.
3. The core owns the firmware events. The device companion adapts the
   shared declaration to native pages and parameter storage. Mods need no
   firmware addresses or hook sites for these features.
4. The companion calls `.present` with a snapshot of the editing track's
   values, then applies its labels, visibility and glyphs to the native UI.

A knob's ID is its index in the machine's `knobs` array, independent of
its encoder or page. A knob placed on both pages has one stored value and
one destination in each LFO. Hiding or renaming it changes its
presentation without changing that identity.

## Writing a UI machine

```c
#include "elekloader/sdk_alpha_machine.h"

enum { LEVEL, TONE };
static const struct elek_knob knobs[] = {
    [LEVEL] = { "LEVEL", .max = 127, .def = 100 },
    [TONE] = { "TONE", .min = -10, .max = 10 },          /* starts at 0 */
};
static void present(const struct elek_ui *ui)
{
    elek_label_num(ui->view[TONE].label, "T", ui->knob[TONE]);    /* "T-10".."T10" */
    ui->view[TONE].hidden = ui->knob[LEVEL] == 0;
}
static const struct elek_machine machine = {
    .id = 23, .name = "My Machine", .short_name = "MINE", ELEK_KNOBS(knobs),
    .pages = { { "MAIN", { [ELEK_A] = &knobs[LEVEL], [ELEK_B] = &knobs[TONE] } } },
    .present = present,
};
ELEK_REGISTER_MACHINE(machine);
```

- A machine ID is 4..127 on the mk1s, 5..127 on Digitone II. Give
  independently distributed machines different stable IDs. Its name has
  1..16 printable ASCII characters and its short name 1..4.
- `ELEK_KNOBS(knobs)` gives the machine its knobs: at most fifteen. A
  knob's label has 1..5 printable ASCII characters.
- `.pages` holds up to two pages, each with a 1..16-character label and
  eight encoders, `ELEK_A`..`ELEK_H`. An encoder holds `&knobs[ID]` or
  nothing. A page without a label is absent, so a machine with no pages
  leaves `.pages` out; the second page needs the first. Place the same
  knob on both pages to share it. The builder refuses a page pointing
  anywhere but the machine's own `knobs`.
- `max` is required and must be above `min` (which defaults to 0). Values
  map the knob's native range onto `min..max`, rounding to the nearest
  integer, with exact halves toward max. The screen shows that mapped
  value under the knob, or `#` when it needs more than five characters.
- `def` is the value a knob starts at, clamped into `min..max`; omitted, it
  is 0, so a `-10..10` knob starts centred and a `10..20` one at 10. On the
  mk1 devices, selection and clear/reset use the default; paste and
  project load restore edited values. Native storage has 32,513 steps; a
  default between representable values uses the first step mapping at or
  above it. Digitone II refuses a knob that starts above its `min` until
  its backend implements defaults.

### Presentation

`.present` is optional. It gets one `const struct elek_ui *ui`: the
`track`, the `page` shown, and `ui->knob[ID]`, every knob's value mapped
to its `min..max`, including those on other pages. `ELEK_UI_NO_PAGE`
means the call is not for a page shown, for example when naming LFO
destinations.

It writes `ui->view[ID]`, which every call starts from the declaration:
the knob's `label`, `hidden` 0 and its `glyph`. Write a label with
`elek_label_set(label, "SAW")` or `elek_label_num(label, "F", value)`
(there is no libc), set `hidden`, or pick a glyph. A hidden knob keeps
its value and LFO identity, and its encoder does not edit it while that
page is shown. Page copy, clear/undo and paste include hidden controls.

A glyph is sixteen rows of sixteen bits, top row first and bit 15 leftmost.
A set bit lights a pixel; a null glyph selects the stock control graphic.
It replaces the control graphic while retaining the label and binding.
Menus and the screensaver cover SDK glyphs as they cover native controls.

A label shows at most five characters; nonprintable bytes become `?`.
`elek_label_num` cuts its prefix to fit the number, and writes `#` when
the number alone needs more. Glyph storage must remain readable while
displayed.

Every device calls `.present` when the machine, track, page or a knob's
value has changed since its last call, and may call it again with the same
inputs. Keep it pure: do not depend on an exact callback count or use it as
an audio clock. Native displays may uppercase labels.

### Native storage and persistence

The companion maps logical controls onto native sound parameters. Project
save/load restores machine identity, values and LFO destinations;
presentation is recomputed from the restored values.

- **Digitakt:** uses the machine-pages parameter mappings, including its
  extra values for the second page.
- **Digitone mk1:** knobs 0..7 use the first SYN2 page's parameters;
  8..14 use DTUN, FDBK, MIX, ADEL, BDEL and two ratio offsets. Knobs
  8, 13 and 14 turn in finer steps. The ratio offsets can reach 127.99;
  the SDK clamps their raw snapshot to 127.0 before mapping it.
- **Digitone II:** logical knob `i` uses sound slot `25 + i`. All SDK
  machines share fifteen parameter rows, separate from the native SHARC
  page pool. The [DN2 companion](MACHINES-DN2.md) describes its ID and
  missing-mod behavior.

These bindings preserve native storage and destination identity. They do
not establish that SDK audio receives locks or LFO modulation: that needs
audio-backend qualification. Held-step and sound-lock editing contexts
are not qualified by the shared UI tests either.

## Building

The example above can use this `mod.json`:

```json
{
 "id": "my-machine", "version": "0.1", "license": "GPL-2.0-or-later",
 "devices": {"digitakt-mk1": ["1.54"], "digitone-mk1": ["1.44"], "digitone-mk2": ["1.12"]},
 "sources": ["machine.c"]
}
```

`devices` replaces `device` and `os`; do not supply both forms. Build for
one stock OS file at a time with a ColdFire cross compiler, using the
ordinary build command; it runs the SDK step when the source registers a
machine:

```bash
ELEKLOADER_CROSS=m68k-elf- \
python -m elekloader.sdk.build <moddir> --stock <stock.syx>
```

When `devices` lists more than one OS, each output's name carries its
device and OS, such as `my-machine-0.1-digitone-mk1-os1.44.elemod`.

A machine claims `machine:<id>` on Digitakt, `dnmachine:<id>` on Digitone
mk1, or `mk2-synth:<id>` on Digitone II: the linker reads the id from its
registration (the device profile's `slot_claims`), so the built mod lists
no claim. SDK and native SHARC synths share the DN2 synth namespace. The
linker refuses conflicting claims. Build the
required companions for the same stock file, then use the normal
[lint and patch loop](ADAPTING.md).

The builder refuses unsupported targets, invalid names, bounds, IDs and
layouts, duplicate declarations in JSON, and imports no companion exports
(such as libgcc's floating point). All private SDK declarations
and companions use `sdk_alpha_dt1_ui_v1`, with one descriptor layout.
Rebuild SDK mods and companions together when that layout changes.

Working UI examples are [sdk-hello-world](../examples/sdk-hello-world)
(two pages and a shared knob) and [sdk-glyphs](../examples/sdk-glyphs)
(a callback selecting glyphs). [sdk-sine](../examples/sdk-sine) adds a
render: a sine at the note on both mk1s, tuned by FINE, whose glyph shows
which way.

### Run it on your computer

Before building for a device, run the machine's `present` and `render`
with your computer's C compiler (`cc`, or `$CC`):

```bash
python -m elekloader.sdk.sdk_alpha_host examples/sdk-sine
```

```
SDK Sine (id 123), Q31 (ColdFire)
  FINE  -50..50, default 0
present, the other knobs at FINE=0:
  FINE=-50   FINE "FINE" glyph
  FINE=0     FINE "FINE" glyph
  FINE=50    FINE "FINE" glyph
render: note 60, 375 blocks: peak 0.500, about 261.6 Hz
```

Each knob goes to its min, default and max, with the others at their
defaults, and each line shows every knob's label as `present` left it,
plus `hidden` and `glyph` where set. The render line gives the peak (1.0 is
full scale) and the pitch it found.

| option | what it does |
|---|---|
| `--note N` | the MIDI note to render (default 60) |
| `--knob NAME=V` | a knob's value for the render, instead of its default; repeat it for more knobs |
| `--blocks N` | how long to render, in blocks of 32 samples (default 375: 0.25 s) |
| `--wav out.wav` | write the render as a 48 kHz mono WAV, to listen to |
| `--float` | build with the SHARC's float samples instead of the ColdFire's Q31 |
| `--glyphs` | draw each glyph `present` shows, as 16 rows of `#` and `.` |

It holds the rest fixed: velocity 100, track 0, page 0, and one note held
for the whole render (`start` on the first block only). The mod must
register exactly one machine.

## Audio contract

Both mk1 devices call this callback on the main ColdFire. Digitone II's
shared SDK backend is pending; its native audio interface remains
available separately.

The callback is:

```c
void render(const struct elek_voice *v, elek_sample *out);
```

It fills all `ELEK_VOICE_FRAMES` (32) mono samples at
`ELEK_SAMPLE_RATE` (48 kHz). `v->knob[i]` is the mapped value of knob `i`
for this voice, with locks and modulation applied where the backend
supplies them. It is separate from `.present`'s editing snapshot.

`elek_voice` also gives the owning `track`, the pitch in 16.16 MIDI
semitones, the velocity 1..127, `start` and the voice's state. `start` is
nonzero in a note's first block; the mod decides what to reset. On the
Digitone mk1 the AMP applies the velocity after render, so scaling by it
applies it twice: use it for timbre. On the Digitakt mk1, render runs
before the native AMP; whether that applies velocity has not been
checked.

### Samples and an external kernel

`elek_sample` is Q31 `int32_t` in a ColdFire build and `float` in a build
with `ELEK_TARGET_SHARC`. Q31 represents approximately -1..1 with integers.
Use the helpers so the same C can operate on both representations:

| Helper | Purpose |
|---|---|
| `ELEK_SAMPLE(x)` | A compile-time sample constant; never pass a runtime variable on ColdFire |
| `elek_mul(a, b)` | Multiply samples; Q31 rounds and saturates |
| `elek_add(a, b)` | Add samples; Q31 saturates, float retains headroom |
| `elek_clip(a)` | Clip float to -1..1; Q31 already fits that range |
| `elek_level(value, max)` | Convert an integer control to a level; `max` must be positive |
| `elek_from_q15(v)` | A signed integer, -32768..32767, as a sample; -32768 is -1.0 |
| `elek_phase_inc(pitch)` | Integer phase increment per sample; a full cycle is 2^32 |

Use sample helpers for arithmetic and do not divide samples. Float and
Q31 do not promise identical clipping or rounding; algorithms must account
for that. The pitch helper clamps to MIDI notes 0..127 and does not
generate a waveform or provide antialiasing.

This render fragment calls an external kernel with the target's sample
type. `LEVEL` is a knob ID defined by the machine:

```c
struct voice_state { uint32_t phase; };
void sine_block(elek_sample *out, uint32_t *phase, uint32_t step);

static void render(const struct elek_voice *v, elek_sample *out)
{
    struct voice_state *s = ELEK_STATE(struct voice_state, v);
    elek_sample gain = elek_level(v->knob[LEVEL], 127);
    sine_block(out, &s->phase, elek_phase_inc(v->pitch));
    for (int i = 0; i < ELEK_VOICE_FRAMES; ++i)
        out[i] = elek_clip(elek_mul(out[i], gain));
}
```

The machine declares `.render = render`. The kernel can be portable C
or a separate assembly implementation for each processor. Assembly must
match that processor's sample representation and calling convention;
ColdFire instructions cannot execute on SHARC. Automatic selection and
packaging of those kernels are not implemented by the SDK builder yet.

### Voice state

Each voice has `ELEK_VOICE_STATE_MAX` (256) bytes of state, zeroed at boot
and when the voice changes machine, and kept between calls and notes.
`ELEK_STATE(struct my_state, v)` returns it as a `struct my_state *`, and
refuses to compile when the struct does not fit. Render computes
everything else from `v->knob`, which already carries the voice's locks
and LFOs, and caches derived values in voice state.

Audio code may read its arguments, its voice state and constant data. A
writable global is not a portable UI-to-audio channel: on DN2 it would be
two independent copies. A build-time check for such access is not
implemented. Avoid allocation, blocking, file/USB access and UI/firmware
calls from render code.

Passing the builder's checks defines a valid record, not an available
audio backend.

## Audio across mk1 and mk2

The backends reuse each device's native voice path:

| Target | Native path available to an adapter | Work still required for the shared SDK |
|---|---|---|
| Digitakt mk1 | ColdFire track blocks, 32 Q31 samples; one rendering slot per track | Implemented; hardware and worst-case load qualification remain |
| Digitone mk1 | Main ColdFire replaces an allocated FM voice with 32 Q31 samples through the machines companion | Implemented; hardware, lock/modulation timing and worst-case load qualification remain |
| Digitone II | SHARC voice callback, 32 floats before the native envelope, filter and mixer | Build/package the audio artifact, bind it to SDK registration, and supply missing voice metadata |

Digitakt replaces the track block before the existing native overdrive,
filter and AMP stages; each track is one voice. Controls come from
`cm_src_value`, the render parameter buffer, rather than the editing kit.
Each track has its own voice state, retained across notes and retriggers
and cleared when its rendered machine changes. The start flag, pitch and
velocity come from the native render exports.

Digitone mk1's second ColdFire still generates FM. The shared renderer
replaces each allocated voice on the main CPU through `machines-dn1`.
There are eight independent state blocks, one for each allocated voice,
and `track` comes from the native voice-owner map. Pitch, velocity and
start come from the companion's native voice record. State survives notes
and retriggers, and is cleared when the voice changes machine.
The wrapper scales SDK full scale to `DNM_PEAK` (1/16); the companion's AMP
envelope and velocity, then the native multimode filter, mixer and effects,
follow the renderer.

DN1 audio uses the same SYN parameter mapping as its UI, including DTUN,
delays and ratio offsets; it does not use DN2's `25 + i` slots. The adapter
retains the native block-parameter buffer from `ev_render_params` and reads
it after controllers and LFOs have run. That supplies per-voice sound/lock
values and native modulation instead of the editing kit. The host adapter
test checks distinct voice values and post-event edits; the emulator checks
LFO values reaching render. Exact sound-lock and modulation timing relative
to the stock DSP/filter pipeline still needs device qualification.

For a mk1 build, UI and audio can be compiled into the same ColdFire mod.
The intended DN2 build compiles the source for both processors: pages and
`.present` for ColdFire, the renderer and its dependencies for SHARC. It
must separate the reachable code/data and package a SHARC artifact. A
ColdFire function pointer cannot invoke SHARC code. This two-compiler build
is not implemented yet.

DN2's existing [native voice input](../elekloader/sdk/include/digitone-mk2/voice.h)
already exposes 40 per-voice parameter values from the stock engine, with
locks and modulation. The SDK mapping puts its fifteen knobs in the first
fifteen of those slots. Connecting that path to SDK audio still needs
validation. Pages, labels and glyphs stay on ColdFire.

The native DN2 input has pitch, start, slot, controls and state, but lacks
the owning track and velocity required by `elek_voice`. Core/bridge
work must provide those values; the current DN2 SDK adapter does not.

The existing DN2 native audio limits are one SHARC synth per build, synth
IDs 5..31, a 2,048-byte callback stack, and bounded code/data regions. A
future SDK audio backend must enforce them, even though UI-only SDK IDs
extend to 127. See [SHARC track synths](MACHINES-DN2-VOICE.md) for the native
ABI, memory and constraints. Whole-output effects, custom filters and
Digitakt II are outside this shared render contract.

## Limits and qualification

- The API is private and specific to the listed OS versions. New ABI
  fields do not imply that a device implements them.
- DN2 registers at most sixteen SDK machines per build. This is a core
  capacity limit, separate from the two-page/fifteen-knob layout limit.
- Digitone mk1 SDK pages occupy `page:30` and `page:29`. Another mod using
  either conflicts. `machines-dn1` must also be the only mod replacing
  SYN2's page list: the last `core_page_list` call wins.
- The shared API has no arbitrary full-screen drawing, custom widget/input
  lifecycle or user-supplied value formatter.
- UI tests cover native destination selection/naming and project
  restoration. They do not qualify audible LFO modulation, held-step or
  sound-lock presentation, or pattern-lock save/load.
- The mk1 emulator sound tests check pitch, octave intervals, FINE and
  UI-only fallback through the native output. Defaults tests cover selection,
  clear, repeated placements, paste and project restoration. DN1 also exercises overlapping
  voice/state identity and LFO controls reaching render. These do not
  establish physical-device timing or worst-case render load.
- Host audio tests check C sample helpers, pitch, level and waveform
  continuity. A host float build is not execution on the SHARC, and does
  not establish device sound, timing, polyphony or compiler compatibility.

Device audio qualification must separately cover note starts, retriggers,
release, overlapping voices, per-voice locks and modulation, controls while
playing, machine switching and persistence. Gain/envelope placement and
worst-case load require checks on each target. Backend support should say
which behaviors have been demonstrated rather than silently substituting
stored UI values for effective voice controls.

## Tests

- [Builder tests](../tests/test_sdk_alpha.py) validate registration,
  layouts, ABI fields and refusals using each supplied stock file and a
  ColdFire compiler.
- [Render-contract tests](../tests/test_sdk_alpha_render.py) compile
  [sdk-sine](../examples/sdk-sine) twice with the host compiler, Q31 and
  float. They check sample helpers, and the sine's pitch, start and level
  on both. They also exercise the real
  mk1 adapters' state isolation, effective-control mapping and defaults,
  and the part every companion shares (`mods/sdk-alpha/machine.c`): value
  mapping across the bounds, defaults, value text, label sanitising and when
  `.present` runs. `test_units.py` runs this suite.
- [Host-run tests](../tests/test_sdk_alpha_host.py) run
  `sdk_alpha_host` on the examples; `test_units.py` runs them too.
- [Sound tests](../tests/test_sdk_alpha_sound.py) capture the emulator output;
  [defaults tests](../tests/test_sdk_alpha_defaults.py) exercise mapped defaults.
  Targets without the corresponding backend are explicitly skipped.
- `tests/test_sdk_alpha_{machines,knobs,labels,hiding,glyphs,lfo,saving}.py`
  exercise shared behavior through device adapters. Each test starts from
  a restored emulator state. Firmware addresses, navigation, storage and
  screen geometry live in `tests/sdk_alpha_devices.py`, not the scenarios.

Run the host audio contract without firmware:

```bash
python tests/test_sdk_alpha_render.py -v
```

The mk1 suites use digiemu's Python (use `test_sdk_alpha_sound.py` or
`test_sdk_alpha_defaults.py` for mk1 audio/defaults):

```bash
ELEKLOADER_DIGIEMU=<digiemu> ELEKLOADER_STOCK_154=<Digitakt_OS1.54.syx> \
ELEKLOADER_STOCK_DN1_144=<Digitone_and_Digitone_Keys_OS1.44.syx> \
ELEKLOADER_CROSS=m68k-elf- \
uv run --project <digiemu> python tests/test_sdk_alpha_hiding.py
```

The DN2 UI suites use DigiKit's ColdFire-only daemon:

```bash
ELEKLOADER_DIGIKIT=<digikit> ELEKLOADER_DIGI_EMUD=<digi-emud> \
ELEKLOADER_DN2_SYX=<Digitone_II_OS1.12.syx> ELEKLOADER_CROSS=m68k-elf- \
python tests/test_sdk_alpha_hiding.py
```

`SDK_WATCH=1` displays the test's screen, actions and result in a window;
`0.25` runs slower and `4` faster.
Missing input files or emulator dependencies cause skips, not passes.
These UI runs do not execute an SDK renderer on the DSP.
