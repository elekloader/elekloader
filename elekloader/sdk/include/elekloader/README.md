# SDK alpha: machine UIs in C

A private experiment, not a supported API yet: a mod declares a machine and
its page of knobs in C, with no `mod.json` sites or contributions, and the
same source builds for the Digitakt mk1, Digitone mk1 and Digitone II. The API is
[sdk_alpha_machine.h](sdk_alpha_machine.h); its comments are the reference.
It may change without notice, and mods built with it must be rebuilt when
it does.

## What a mod writes

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
    .id = 123, .name = "My Machine", .short_name = "MINE",
    .pages = pages, .page_count = 1, .present = present,
};
ELEK_REGISTER_MACHINE(machine);
```

```json
{
 "id": "my-machine", "version": "0.1", "license": "GPL-2.0-or-later",
 "devices": {"digitakt-mk1": ["1.54"], "digitone-mk1": ["1.44"], "digitone-mk2": ["1.12"]},
 "sources": ["machine.c"]
}
```

- **The machine:** `.id` 4..127 (5..127 on Digitone II), a name (1..16 characters) and a short name
  (1..4).
- **Pages and knobs:** each knob sits on an encoder, A..H. Its logical `.id`
  numbers the knobs from 0 with no gaps, whatever their position. One knob
  may appear on both pages and keeps one value.
  - A label is 1..5 characters (`ELEK_UI_LABEL_MAX`).
  - A knob runs 0..127 unless `ELEK_UI_BOUND` gives its bounds.
- **`.present`** is optional. It reads every knob's value, mapped and raw
  (0..32512), and the track it is called for. It may change a knob's label,
  hide it, or give it a 16×16 glyph. Each call starts from the declared
  labels, all visible.
- **`devices`** lists the releases the mod builds for. It replaces
  `device` and `os`.

Build with the SDK alpha's builder, one stock file at a time:

```bash
python -m elekloader.sdk.sdk_alpha_build <moddir> --stock <stock.syx>
```

It finds the registrations in the compiled code and adds what the device
needs: the table entry, the machine id's claim (`machine:<id>` on the
Digitakt, `dnmachine:<id>` on the Digitone mk1, `mk2-synth:<id>` on Digitone II) and the companions in
`requires`. It refuses:
- a stock file that `devices` doesn't list;
- more than 2 pages or 15 knobs;
- a label over 5 characters;
- bad bounds, ids or names.

Examples: [examples/sdk-hello-world](../../../../examples/sdk-hello-world)
and [examples/sdk-glyphs](../../../../examples/sdk-glyphs), with two
pages and glyphs respectively; they build for all three devices.

## Support on each device

| | Digitakt mk1 OS 1.54 | Digitone mk1 OS 1.44 |
|---|---|---|
| companion | `machine-pages` (`mods/machine-pages/sdk_alpha.c`) | `machines` 0.3, with core-dn1 4.2 (`mods/machines-dn1/sdk_alpha.c`) |
| choosing the machine | the machine menu (FUNC + SRC) | Mod Menu → MACHINES (hold a track key) |
| pages, knobs | 2 pages, 15 knobs, on SRC | 2 pages, 15 knobs: SYN2 shows the machine's pages in place of its own on its tracks; each track keeps the page it showed |
| values | 0..127, raw 0..32512 | the same, each knob in one SYN parameter whatever page shows it: 0-7 SYN2's first page's A-H, 8-14 DTUN, FDBK, MIX, ADEL, BDEL and two ratio offsets. Knobs 8, 13 and 14 turn in finer steps, as those parameters do, and 13 and 14 reach 127.99, which `.present` reads as 127 |
| `.present` | when the page is drawn, and for LFO names with `ELEK_UI_NO_PAGE` | when the page is built, then when a value, the page or the track changes |
| labels and hiding from `.present` | yes | yes; page copy, clear/undo and paste include hidden knobs |
| glyphs | yes | yes |
| LFO destinations | every knob, with its SDK label | every knob, with its SDK label |
| saving a project | brings back the machine, the values, what `.present` shows and hides, and the LFO destinations (tested through the menus); p-locks are not tested | the same, through the same menus: the machine and the values are the sound's own slots |
| sound | none of its own (the SDK has no audio callback); what the track plays is not checked | none of its own: the track plays FM |

On **Digitone II OS 1.12**, `machines-dn2` 0.2 with core-dn2 0.2 or newer
puts SDK machines in **FUNC+SYN → MACHINE SEL**. SYN cycles through their
pages, headed by the declared page label; each track keeps its page. It
supports the same bounds, labels, hiding and glyphs, with `.present` called
when the active track, page or values change. Off SYN it uses
`ELEK_UI_NO_PAGE`. Logical knob *i* uses native sound slot `25 + i`, so
placing it twice shares one value and one destination in each of the
three LFOs. Native page copy, clear/undo, paste and project save/load keep
the values. SDK machines share fifteen parameter rows, independently of
the five-page SHARC pool. Their stable IDs share SHARC synths' namespace;
a saved ID whose mod is absent remains MISSING. The DSP receives stock
FM TONE's type: knobs edit FM parameters, and the SDK supplies no audio
callback. The [DN2 companion](../../../../docs/MACHINES-DN2.md) describes
the native registration and missing-mod behavior.

## Tests

- `tests/test_sdk_alpha.py`: the builder and linker, with no emulator: what
  a registration may declare, what is refused, what the build adds for each
  device (`ELEKLOADER_STOCK_154`, `ELEKLOADER_STOCK_DN1_144`,
  `ELEKLOADER_DN2_SYX`, `ELEKLOADER_CROSS`).
- `tests/test_sdk_alpha_{machines,knobs,labels,hiding,glyphs,lfo,saving}.py`:
  what the SDK promises, on every device, in digiemu (mk1) or DigiKit
  (Digitone II, ColdFire only). A test file defines the
  machines it needs as SDK C, next to its tests, and says what a user would
  do:

  ```python
  def test_a_hidden_knob_ignores_its_encoder(self):
      hide, trgt = self.track.use(HIDING).knobs("HIDE", "TRGT")
      hide.set(127)                 # HIDING's rule: HIDE at 127 hides TRGT
      before = trgt.value
      trgt.turn(+32)
      self.assertEqual(trgt.value, before)
  ```

  - `tests/sdk_alpha_harness.py` builds every machine the test files define
    into one firmware per device, boots it once, and gives each test a fresh
    emulator from the saved state (0.25 s). It finds a knob by its label
    from the builder's layout, so a test names no machine id, encoder or
    page.
  - `tests/sdk_alpha_devices.py` has one adapter per device: its keys, its
    menus, where it keeps a track's machine and values, where its screen
    shows a knob. Add a `Device` subclass and every test runs on it.
  - Known device gaps use `@sdk_expected_failure(device)`: they count as
    expected failures, and fixing one reports an unexpected success until
    its marker is removed. An unmarked failure breaks the suite.
  - It runs with digiemu's Python:

    ```bash
    ELEKLOADER_DIGIEMU=<digiemu> ELEKLOADER_STOCK_154=<Digitakt_OS1.54.syx> \
    ELEKLOADER_STOCK_DN1_144=<Digitone_and_Digitone_Keys_OS1.44.syx> \
    ELEKLOADER_CROSS=m68k-elf- \
    uv run --project "$ELEKLOADER_DIGIEMU" python tests/test_sdk_alpha_hiding.py
    ```

    Digitone II instead needs DigiKit and its built daemon, plus the stock
    file and ColdFire cross compiler:

    ```bash
    ELEKLOADER_DIGIKIT=<digikit> ELEKLOADER_DIGI_EMUD=<digi-emud> \
    ELEKLOADER_DN2_SYX=<Digitone_II_OS1.12.syx> ELEKLOADER_CROSS=m68k-elf- \
    python tests/test_sdk_alpha_hiding.py
    ```

    Each backend owns boot, session reset and card state; the behavior
    scenarios are shared. The Digitone II adapter reads its native SYN
    view, including its page, rather than assuming a heap address.

    Without the stock files or digiemu a test is skipped, and a skipped test
    is not a pass.
  - `SDK_WATCH=1` in front of the command shows the test running in a
    window: the device's screen, what the test is doing ("HIDE.set(127)")
    and the last input. `1` is real time, `0.25` slow motion, `4` four times
    faster. A failed test stays on its last screen until you press Next, and
    closing the window lets the rest run without it.
  - Not covered: how LFOs modulate a knob's value while a note plays, and
    p-locks applied or saved with a pattern (both need the live parameter
    values; the older checks called Digitakt firmware routines by address and
    are in the history of `main`); the non-SDK machines and the stock screens.

## Open issues

- **The SDK pages' numbers on the Digitone.** They claim `page:30` and
  `page:29`, two of core-dn1's four mod page numbers (27..30), so a build
  with another mod on either refuses to link. Core could number mod pages
  itself instead.
- **Who sets SYN2's list.** With `core_page_list` the last call wins, so
  machines-dn1 must be the only mod that changes SYN2's pages. A table where
  the first mod to answer wins (like `core_param_override`) would let mods
  share keys.
- **Tests not written yet:** LFO modulation of a knob's value while a note
  plays, and p-locks (applied, saved and loaded). Both need the live
  parameter values read from the emulator.
- **Sound on the Digitone.** A UI-only machine plays FM, and its knobs are
  FM's parameters (the operator envelopes, detune, feedback, mix, delays and
  ratio offsets), so turning one changes FM's sound. What
  a machine with no sound of its own should play is not decided.
- **Releases.** Only Digitakt 1.54, Digitone mk1 1.44 and Digitone II 1.12
  are supported, not Digitakt 1.53 or Digitone 1.43.
- **The ABI marker symbols** keep the Digitakt's names (`sdk_alpha_dt1_*`)
  on the Digitone.
