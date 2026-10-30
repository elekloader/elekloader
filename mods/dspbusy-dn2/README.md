# DSP busy measure for the Digitone II

An optional, code-free companion to core-dn2 0.3 on OS 1.12. It declares
one data site in the DSP's idle loop, which jumps to `dn2_idle_entry` in
core's peek stub. A build must include a SHARC mod to load that stub;
without one the linker refuses the companion.

DigiHealth's `digiusb dspbusy` reads the measure. Without the companion
the busy record stays zero and the DSP peek still works. Omit it for
faster coupled emulation. The record and what it measures are described
in [SHARC-DN2.md, "The DSP peek"](../../docs/SHARC-DN2.md#the-dsp-peek).

## Build and check

No compiler is needed for this companion:

```sh
python -m elekloader.sdk.build mods/dspbusy-dn2 --stock Digitone_II_OS1.12.syx
python -m elekloader.lint mods/dspbusy-dn2/out/dspbusy-dn2-0.1.elemod --stock Digitone_II_OS1.12.syx --with core-0.3.elemod --with sharc-mod.elemod
```

Add its `.elemod` alongside the core and your SHARC mods when patching.
The loader checks its exact stock hash and replacement against the
profile, and refuses code sites, relocations and overlaps at that address.

`tests/test_sharc_filter.py` checks the companion's packaging, dependency
and ownership. `tests/test_dn2_dsp_peek.py` checks both busy-record states
in DigiKit's coupled emulator; their required environment is listed in
[docs/TESTS.md](../../docs/TESTS.md).
