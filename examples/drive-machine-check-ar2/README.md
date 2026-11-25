# drive-machine-check-ar2

A test for core-ar2 0.22's +Drive machines (core-ar2 README, "+Drive
machines (0.22)") that needs no +Drive. The Drive Loader merges a +Drive
mod's `core_machines` entry into a spare entry in `ev_boot_drive`, after
`.boot` has walked the table; this mod does the same from its own
`ev_boot_drive` handler. It puts DRV CHECK (slot 37, the BD voice, playing
as BD CLASSIC; knob B named "Tune Check") in the first spare entry, and
imports `core_desc` so the build has spare entries.

Build it with core and nothing else:

```bash
python -m elekloader.sdk.build mods/core-ar2 --stock <stock 1.74 .syx>
python -m elekloader.sdk.build examples/drive-machine-check-ar2 --stock <stock 1.74 .syx>
python -m elekloader.patch --stock <stock 1.74 .syx> --mod <core.elemod> --mod <drive-machine-check.elemod> --out dmc.syx --version DMC1
```

The record `dmc` (find its address in the link map, or the magic `DMC1`
in RAM) holds, as longs:

| long | what |
|---|---|
| 0 | the magic `DMC1` |
| 1 | 1 the entry went into a spare entry, -1 none was free, -2 a flashed entry has slot 37 |
| 2 | slot 37's name pointer at the end of the handler, before core walks the table again |
| 3..6 | at the first tick: slot 37's name and label pointers, home track << 8 \| voice mask, engine |
| 7 | bit 0 the name is DRV CHECK's, bit 1 the engine is no longer stock's: 3 when the entry reached the machine table |
| 8 | DRV CHECK's engine calls (a BD trig with the BD track set to DRV CHECK) |
| 9 | the record id of slot 37's sound index 10 when its name is "Tune Check", else 0 |
| 10 | ticks |

With core 0.21 long 7 stays 0 (the entry never reaches the table); with
0.22 it is 3, and DRV CHECK is in the BD track's machine list.
