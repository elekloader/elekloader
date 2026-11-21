# knob-flags-check (Analog Rytm MKII)

A test for [core-ar2](../../mods/core-ar2) 0.17's knob flags
(`core_knob_flags`, docs/ADAPTING.md "Knob flags (core-ar2 0.17, Analog
Rytm MKII)"). One page, KNOB FLAGS, behind the LFO key (after LFO and
stock's own pages):

| knob | id | what it has | expected |
|---|---|---|---|
| A | stock 67, LFO DEST | stock's | stock's list on a turn and a push |
| B (KFL) | own 0 | look 67, a `core_knob_flags` entry with `CORE_KNOB_LIST` | the destination list on a turn and a push |
| C (KFM) | own 1 | look 67, an entry with every other bit (0xfffeffff) | as D: core masks the bits |
| D (KFN) | own 2 | look 67, no entry | no list; a turn is this mod's delta |

The values are the mod's (`core_params`), with stock's LFO DEST text. What
it records, in its exported `kf` after the magic KFC1, is in the comment at
the top of `flags.c`. It takes no site.

```bash
python -m elekloader.sdk.build mods/core-ar2 --stock Analog-Rytm_MKII_OS1.74.syx
python -m elekloader.sdk.build examples/knob-flags-check-ar2 --stock Analog-Rytm_MKII_OS1.74.syx
python -m elekloader.patch --stock Analog-Rytm_MKII_OS1.74.syx \
    --mod mods/core-ar2/out/core-0.17.elemod --mod examples/knob-flags-check-ar2/out/knob-flags-check-0.1.elemod \
    --out ar-kf.syx --version KF74
```

## Not run yet

Built and linked in the cloud; not yet run in the Rytm emulator or on a
unit. What the runs must confirm, each with the page proven on screen
first (header KNOB FLAGS, labels KFL KFM KFN):

1. **Getter.** `kf.flags[0]` has 0x10000; `kf.flags[1]` and `kf.flags[2]`
   don't and are equal; `kf.flags67` is stock's for LFO DEST.
2. **List on turn.** Turning B opens the destination list (screen), and B's
   `deltas` stays put while it is open.
3. **List on push.** Pushing B opens the list.
4. **YES and NO.** NO closes the list with B's value unchanged. YES closes
   it; what B's value does is recorded, not assumed: core 0.17 opens the
   list but does not route its commit to a mod (stock's setter 0x400a906c
   writes through the sound set for the id; lfo-rnd takes the setter and
   the read, 0x400a8e66, itself).
5. **Masked and none.** Turning C or D opens no list and moves its
   `deltas` and `value`; C and D behave the same.
6. **A second knob while the list is open.** With B's list open, turn D:
   record whether the list stays or closes (all own ids share container
   index -1, so stock may treat D as the list's knob; lfo-rnd keeps its own
   `list_same` and `knob_cidx` gates for that). `kf.list_id` is B's id
   while B's list is open.
7. **Stock unchanged.** A (LFO DEST) and the LFO page's own DEST knob open
   their list and commit as on stock; other stock knobs open none.
8. **+Drive.** The same with the example loaded from the +Drive
   (drive-loader-ar2 0.10), whose loader checks `core_knob_flags` ids.
