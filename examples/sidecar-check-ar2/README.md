# sidecar-check (Analog Rytm MKII)

A test for the [core-ar2](../../mods/core-ar2) 0.11 calls the Sidecar mods
use, checked in RAM: each part of 0.11 adds what it needs here, and the
emulator and a unit's readback read the record after the magic `SCK1`.

## Part 1: locks the mod keeps itself (core 0.11)

A SIDECAR CHECK page behind the LFO key and a SIDECAR CHECK FX page behind
the FX LFO key (page ids 20 and 21) put ids 560..563 on knobs A..D. One
`core_params` entry answers for them without a trig (a value of 0..127 each,
`sc.base`), and one `core_param_locks` entry keeps their p-locks in RAM,
per id, track (0..12) and step (`sc.lock`):

| call | what the check mod does |
|---|---|
| `get` | the step's lock, or -1; counted, the last one kept (`sc.last_get`) |
| `set` | the lock set to the value |
| `turn` | the lock moved by the delta, from the value without one; with `CORE_LOCK_RECORDING` (live recording) the lock is the knob's value |
| `clear` | the lock gone |
| `label`, `name` | SCA..SCD, SIDECAR CHECK A..D |
| text (`core_params`) | the value, and with a trig held `L` and the held step (1..64) from `core_held_step`: `L5 64` |

Every call but `get` and text goes into a ring of 64 (`sc.e`, at
`sc.n % 64`), with its kind (0 get, 1 turn, 2 set, 3 clear, 4 text, 5 the
knob's turn without a trig), id, track, step (with bit 8 for live
recording), argument and answer; `sc.count` counts each kind.

## Building

```bash
python -m elekloader.sdk.build examples/sidecar-check-ar2 --stock <Rytm 1.74 .syx>
```
