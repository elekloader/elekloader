# Shared machine SDK (alpha)

The shared SDK lets a mod declare its machine, pages and controls once in C
and build that source for several Elektron devices. Each device's companion
connects those declarations to its native machine selector, parameters and
screen. This is a private experiment: the API may change, and mods must be
rebuilt when it does.

**Machine UIs work on all three releases below. Both mk1 devices run SDK
renderers and apply mapped knob defaults.** Digitone II currently accepts
UI-only machines; its builder refuses `.render` and explicit `.def`.

The [header](../elekloader/sdk/include/elekloader/sdk_alpha_machine.h) is the
reference for exact types, fields and helper behavior. This page explains
how they fit together. For other kinds of mod, start with
[Adapting your mod](ADAPTING.md).

## Supported devices

| | Digitakt mk1 | Digitone mk1 / Keys | Digitone II |
|---|---|---|---|
| Stock OS | 1.54 | 1.44 | 1.12 |
| Companions | core and `machine-pages` | core-dn1 4.2 and `machines` 0.3 | core-dn2 0.2 or later and `machines-dn2` 0.2 |
| Choose a machine | FUNC+SRC machine menu | Hold a track key, Mod Menu → MACHINES | FUNC+SYN → MACHINE SEL |
| Show its pages | SRC | SYN2, replacing its pages on that track | SYN |
| Pages / logical knobs | 2 / 15 | 2 / 15 | 2 / 15 |
| LFO destination lists | 2 | 2 | 3 |
| Labels, hiding, 16×16 glyphs | Yes | Yes | Yes |
| Mapped defaults / SDK audio callback | Implemented | Implemented | Not implemented |

Other OS releases and devices, including Digitakt II, are not SDK targets.
Support in elekloader's firmware loader does not imply support in this SDK.

A UI-only machine supplies no sound generator. The Digitones continue to
play stock FM; their SDK controls use FM sound parameters, so turning a
control can change that sound. A UI-only Digitakt machine produces an
empty voice.

## How it fits together

1. The mod defines an `elek_machine`, pages and logical knobs in C, then
   declares it with `ELEK_REGISTER_MACHINE`.
2. The SDK builder reads the compiled declaration. It validates the layout
   and adds the device's registration, machine-ID claim and required
   companions to an ordinary format-2 `.elemod`.
3. The core owns the firmware events. The device companion adapts the
   shared declaration to native pages and parameter storage. Mods need no
   firmware addresses or hook sites for these features.
4. The companion calls `.present` with a snapshot of the editing track's
   values, then applies its labels, visibility and glyphs to the native UI.

The logical knob ID is independent of its encoder or page. A knob placed
on both pages has one stored value and one destination in each LFO. Hiding
or renaming it changes its presentation without changing that identity.

## Writing a UI machine

```c
#include "elekloader/sdk_alpha_machine.h"

enum { LEVEL, TONE };
static const struct elek_ui_knob level = { .id = LEVEL, .label = "LEVEL" };
static const struct elek_ui_knob tone = {
    .id = TONE, .label = "TONE", .min = ELEK_UI_BOUND(-10), .max = ELEK_UI_BOUND(10),
};
static const struct elek_ui_page pages[] = {
    { .label = "MAIN", .knob = { [ELEK_ENCODER_A] = &level, [ELEK_ENCODER_B] = &tone } },
};
static void present(const struct elek_ui_context *ctx, struct elek_ui_presentation *out)
{
    if (ctx->knob[LEVEL]->value == 127)
        out->knob[TONE]->visible = 0;
}
static const struct elek_machine machine = {
    .id = 23, .name = "My Machine", .short_name = "MINE",
    .pages = pages, .page_count = 1, .present = present,
};
ELEK_REGISTER_MACHINE(machine);
```

- A machine ID is 4..127 on the mk1s, 5..127 on Digitone II. Give
  independently distributed machines different stable IDs. Its name has
  1..16 printable ASCII characters and its short name 1..4.
- An empty machine can omit pages. Otherwise it has at most two pages,
  each with a 1..16-character title and eight encoder positions, A..H.
  An unspecified position is empty.
- Logical knob IDs are contiguous from zero, with at most fifteen knobs
  across all pages. Reuse the same knob descriptor for repeated placements.
  Labels have 1..5 printable ASCII characters.
- Bounds default to 0..127. `ELEK_UI_BOUND(0)` supplies a real zero bound;
  it is different from omitting the bound. The callback receives a mapped
  integer and the underlying raw value, 0..32512 in native 8.8 units.
  Mapping rounds to the nearest integer, with exact halves toward max.
- On the mk1 devices, `.def = ELEK_UI_BOUND(value)` sets a mapped default.
  An explicit zero differs from omission: omission starts at the minimum.
  The builder refuses defaults outside the effective bounds. Selection and
  clear/reset use the default; paste and project load restore edited values.
  Repeated page placements share one default and one value. Native storage
  has 32,513 steps; a default between representable values uses the first
  step mapping at or above it. Explicit defaults on Digitone II are
  refused until its backend implements them.

### Presentation

`.present` is optional. Its context includes the track, the displayed
machine-page index, and values for every logical knob, including those on
other pages. `ELEK_UI_NO_PAGE` means the call is not for a displayed machine
page, for example when naming LFO destinations. An empty machine has no
knob array.

Every call starts from the declared labels and glyphs, with all populated
controls visible. The callback can replace a label, set `visible` to zero,
or supply a glyph. A hidden knob keeps its value and LFO identity, and its
encoder does not edit it while that page is shown. Page copy, clear/undo
and paste include hidden controls.

A glyph is sixteen rows of sixteen bits, top row first and bit 15 leftmost.
A set bit lights a pixel; a null glyph selects the stock control graphic.
It replaces the control graphic while retaining the label and binding.
Menus and the screensaver cover SDK glyphs as they cover native controls.

Labels are copied immediately after `.present` returns, truncated to five
characters; nonprintable bytes become `?`. Their source must still be
readable then: use literals or persistent buffers, not callback-local
arrays. Glyph storage must remain readable while displayed.

The Digitakt evaluates presentation when drawing and for LFO names. The
Digitones also refresh it when the active machine, track, page or values
change. On the mk1s, a machine with `.render` and `.shared` is also
presented on every UI tick for each track playing it, active or not
(`ctx->track` says which). Do not depend on an exact callback count or
use `.present` as an audio clock. Native displays may uppercase labels.

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
one stock OS file at a time with a ColdFire cross compiler:

```bash
ELEKLOADER_CROSS=m68k-elf- \
python -m elekloader.sdk.sdk_alpha_build <moddir> --stock <stock.syx>
```

The builder adds `machine:<id>` on Digitakt, `dnmachine:<id>` on Digitone
mk1, or `mk2-synth:<id>` on Digitone II. SDK and native SHARC synths share
the DN2 synth namespace. The linker refuses conflicting claims. Build the
required companions for the same stock file, then use the normal
[lint and patch loop](ADAPTING.md).

The builder refuses unsupported targets, invalid names, bounds, IDs and
layouts, and duplicate declarations in JSON. All private SDK declarations
and companions use `sdk_alpha_dt1_ui_v1`, with one descriptor layout.
Rebuild SDK mods and companions together when that layout changes.

Working UI examples are [sdk-hello-world](../examples/sdk-hello-world)
(two pages and a shared knob) and [sdk-glyphs](../examples/sdk-glyphs)
(a callback selecting glyphs). [sdk-sine](../examples/sdk-sine) adds a
render: a sine at the note on both mk1s, tuned by FINE, whose glyph shows
which way.

## Audio contract

Both mk1 devices call this callback on the main ColdFire. Digitone II's
shared SDK backend is pending; its native audio interface remains
available separately.

The callback is:

```c
void render(const struct elek_audio_context *ctx,
            const struct elek_voice *voice, elek_sample *out);
```

It fills all `ELEK_VOICE_FRAMES` (32) mono samples at
`ELEK_SAMPLE_RATE` (48 kHz). `ctx->knob[i]` is the mapped value of logical
knob `i` for this voice, with locks and modulation applied where the
backend supplies them. It is separate from `.present`'s editing snapshot.

`elek_voice` describes the allocated voice `slot`, owning `track`, pitch
in 16.16 MIDI semitones, velocity 1..127, gate, note-start flags and a
state pointer. A voice slot is not a track number on a polyphonic device.
`ELEK_VOICE_START` notifies the renderer of a note start; the mod decides
what to reset. Resetting phase on a retrigger can cause a waveform jump.

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
| `elek_phase_inc(pitch)` | Integer phase increment per sample; a full cycle is 2^32 |

Use sample helpers for arithmetic and do not divide samples. Float and
Q31 do not promise identical clipping or rounding; algorithms must account
for that. The pitch helper clamps to MIDI notes 0..127 and does not
generate a waveform or provide antialiasing.

This render fragment calls an external kernel with the target's sample
type. `LEVEL` is a logical control ID defined by the machine:

```c
struct voice_state { uint32_t phase; };
void sine_block(elek_sample *out, uint32_t *phase, uint32_t step);

static void render(const struct elek_audio_context *ctx,
                   const struct elek_voice *v, elek_sample *out)
{
    struct voice_state *s = v->state;
    elek_sample gain = elek_level(ctx->knob[LEVEL], 127);
    sine_block(out, &s->phase, elek_phase_inc(v->pitch));
    for (int i = 0; i < ELEK_VOICE_FRAMES; ++i)
        out[i] = elek_clip(elek_mul(out[i], gain));
}
```

The machine declares `.render = render` and
`.voice_state = sizeof(struct voice_state)`. The kernel can be portable C
or a separate assembly implementation for each processor. Assembly must
match that processor's sample representation and calling convention;
ColdFire instructions cannot execute on SHARC. Automatic selection and
packaging of those kernels are not implemented by the SDK builder yet.

### Voice state and shared UI data

The contract separates two kinds of memory:

| Declaration | Owner and use | Limit |
|---|---|---|
| `.voice_state` | One block per voice slot, zeroed at boot and retained between calls and notes; `voice->state` | 256 bytes per slot |
| `.shared` | One block per track, written by `.present` through `out->shared` and read by audio through `ctx->shared` | 64 bytes per track |

`.shared` is for extra UI-computed data, such as a mode table or a seed,
that is not a native knob value. Most instruments can work from
`ctx->knob` and voice state alone. A UI-derived value does not automatically
follow per-voice locks or LFOs; compute those dependencies from the voice's
controls in the audio path.

Both mk1 adapters publish shared data through two buffers per track: `.present`
updates the inactive buffer, then publishes its index after the complete
write. The audio interrupt reads one published buffer for the whole call.
The UI tick refreshes every SDK audio track, including inactive tracks
after a project load, with `ELEK_UI_NO_PAGE`. Until a track's first
snapshot is ready its renderer produces silence; a note that starts then
still reaches the renderer with `ELEK_VOICE_START`. UI-only machines and
machines without `.shared` receive a null shared pointer.

DN2 requires a copy to the SHARC's separate memory; that transport is
pending. Its UI-only calls have `out->shared == NULL`.

Shared data must not contain pointers into UI memory. The DN2 backend also
needs a defined byte order and layout; copying an arbitrary C structure
between processors is not by itself a portable transport.

Audio code may read its arguments, its voice state, the shared snapshot
and constant data. A writable global is not a portable UI-to-audio channel:
on DN2 it would be two independent copies. A build-time check for such
access is not implemented. Avoid allocation, blocking, file/USB access and
UI/firmware calls from render code.

The builder validates these sizes and rejects either state declaration
without `.render`. Passing the checks defines a valid record, not an
available audio backend.

## Audio across mk1 and mk2

The backends reuse each device's native voice path:

| Target | Native path available to an adapter | Work still required for the shared SDK |
|---|---|---|
| Digitakt mk1 | ColdFire track blocks, 32 Q31 samples; one rendering slot per track | Implemented; hardware and worst-case load qualification remain |
| Digitone mk1 | Main ColdFire replaces an allocated FM voice with 32 Q31 samples through the machines companion | Implemented; hardware, lock/modulation timing and worst-case load qualification remain |
| Digitone II | SHARC voice callback, 32 floats before the native envelope, filter and mixer | Build/package the audio artifact, bind it to SDK registration, transport shared data, and supply missing voice metadata |

Digitakt replaces the track block before the existing native overdrive,
filter and AMP stages. The voice's `slot` and `track` are the track index.
Controls come from `cm_src_value`, the render parameter buffer, rather
than the editing kit. Each track has its own voice state, retained across
notes and retriggers and cleared when its rendered machine changes.
The start flag, pitch and velocity come from the native render exports.
Currently `gate` reflects a nonzero native AMP envelope phase; it is an
envelope-active indication, not a separately observed key-held flag.

Digitone mk1's second ColdFire still generates FM. The shared renderer
replaces each allocated voice on the main CPU through `machines-dn1`.
There are eight independent state blocks; `slot` is the allocated voice,
and `track` comes from the native voice-owner map. Pitch, velocity, start
and held-note gate come from the companion's native voice record. State
survives notes and retriggers, and is cleared when the slot changes machine.
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
validation. Pages, labels and glyphs stay on ColdFire; knob values do not
need to be resent as `.shared` data.

The native DN2 input has pitch, start, slot, controls and state, but lacks
the owning track, velocity and gate required by `elek_voice`. Core/bridge
work must provide those values. The shared-data channel must coexist with
DSP peek, publish complete updates and establish when they become visible.
Neither is supplied by the current DN2 SDK adapter.

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
  clear, repeated placements, paste and project restoration. Shared-state
  tests check inactive tracks after load. DN1 also exercises overlapping
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
- [Render-contract tests](../tests/test_sdk_alpha_render.py) compile one C
  machine twice with the host compiler, Q31 and float. They check sample
  helpers and compare the resulting sine. They also exercise the real
  mk1 adapters' state isolation, effective-control mapping, defaults and
  atomic shared publication. `test_units.py` runs this suite.
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
`0.25` runs slower and `4` faster. Known device gaps use
`@sdk_expected_failure(device)` and become unexpected successes when fixed.
Missing input files or emulator dependencies cause skips, not passes.
These UI runs do not execute an SDK renderer on the DSP.
