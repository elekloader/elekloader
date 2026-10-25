# SHARC sine reference checks

Helpers for the C and assembly reference builds (`rebuild.py`) and their
unit tests. Mod authors do not call them: develop a mod with the
[normal source build](../sharc-sine/README.md).

| module | what it does |
|---|---|
| `qualify.py` | pins the tool options; inspects the executable sections; checks symbols, data and the reference table |
| `execute.py` | refuses a SHARC store whose address and source register come from the same DAG |
| `h1_spills.py` | checks that the bridge saves and restores PX in full width: both halves, and the registers' low bits |

The [C reference build](../sharc-sine/rebuild.py) rebuilds the bridge and
the sine; the [assembly reference build](../sharc-sine-asm/rebuild.py)
rebuilds the assembly sine with the shared C entry wrapper. Both need the
[pinned Selache](../sharc-sine/README.md#build-c-or-assembly-source),
inspect the final sections, and refuse any change to the reference bytes.
The `build-c` and `build-asm` tasks are the ones that take your edits.

The register and instruction checks run with:

```sh
python tests/test_units.py
```

They include stores and spills broken on purpose. They check bytes and
encodings: they are not an emulator or a timing test. The instruction
fields and the full-width saves follow the
[SHARC+ Core Programming Reference, revision 1.5](https://www.analog.com/media/en/dsp-documentation/processor-manuals/sc58x-2158x-prm.pdf).
The [callback contract](../../docs/SHARC-DN2-ABI.md) says what a mod must
keep: registers, stack and memory.

These helpers are GPL-2.0-or-later. The licences of the sources and tools,
and Selache's limits, are with the
[C example](../sharc-sine/README.md#source-and-toolchain).
