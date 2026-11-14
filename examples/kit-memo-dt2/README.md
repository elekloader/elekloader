# kit-memo (Digitakt II)

The smallest mod on core-dt2 1.1's kit data, project data and value rows.
Each kit keeps a memo (OFF, A, B or C), set in SETTINGS > PERSONALIZE >
KIT MEMO for the kit on. It is saved with the kit, comes back when the kit
is loaded, and goes with the kit when the pattern is copied. The project
keeps the last memo set.

How it works (`memo.c`): it contributes one `core_kitdata_t` (a long a kit)
and one `core_projdata_t`, and adds its row with `core_addvalue` from
`ev_personalize`. It has no sites of its own
([docs/ADAPTING.md](../../docs/ADAPTING.md), "Project data, kit data and
value rows (core-dt2 1.1, Digitakt II)").

```bash
python -m elekloader.sdk.build mods/core-dt2 --stock Digitakt_II_OS1.17.syx
python -m elekloader.sdk.build examples/kit-memo-dt2 --stock Digitakt_II_OS1.17.syx
python -m elekloader.patch --stock Digitakt_II_OS1.17.syx \
    --mod mods/core-dt2/out/core-1.1.elemod \
    --mod examples/kit-memo-dt2/out/kit-memo-1.0.elemod --out memo.syx --version KM10
```
