# SHARC sine reference checks

These modules are shared by the current C/ASM reference producers and their
unit tests. For mod development, use the
[normal source workflow](../sharc-sine/README.md); authors do not call these
helpers.

| Module | Current use |
|---|---|
| `qualify.py` | Pinned tool options, executable-section inspection, symbol/data checks and reference table validation |
| `execute.py` | Reject SHARC stores that combine a DAG address with a source register from the same DAG |
| `h1_spills.py` | Verify the core bridge's full-width PX saves and restoration, including both PX halves and low register bits |

The [C reference producer](../sharc-sine/rebuild.py) rebuilds the current
bridge and sine. The [ASM reference producer](../sharc-sine-asm/rebuild.py)
rebuilds the assembly sine with the shared C entry wrapper. Both require the
[pinned Selache setup](../sharc-sine/README.md#build-c-or-assembly-source),
inspect final sections and reject changes to the reference bytes. The
normal `build-c` and `build-asm` commands accept source edits within the mod
contract.

The register and instruction checks run through:

```sh
python tests/test_units.py
```

They include deliberate malformed stores and spill mutations. These are
byte/encoding checks, not an emulator or hardware timing test. Instruction
fields and full-width preservation use the
[SHARC+ Core Programming Reference, revision 1.5](https://www.analog.com/media/en/dsp-documentation/processor-manuals/sc58x-2158x-prm.pdf).
The [callback contract](../../docs/SHARC-DN2-ABI.md) states the mod's register,
stack and memory obligations.

The helper and example sources are GPL-2.0-or-later. Source/tool licensing
and compiler limitations are described with the
[C example](../sharc-sine/README.md#source-and-toolchain).
