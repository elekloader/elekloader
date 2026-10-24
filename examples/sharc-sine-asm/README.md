# Digitone II SHARC assembly sine

This companion to the [C sine](../sharc-sine/README.md) implements the sample
kernel in SHARC+ assembly. Both use the same C entry wrapper, phase table,
0.125 peak amplitude, note range and gain ramp. Core-dn2 0.2 selects the note
and copies the mono block to both master channels.

## Build

Follow the shared [source setup](../sharc-sine/README.md#build-c-or-assembly-source),
then run from `examples/sharc-sine`:

```sh
mise run build-asm
```

The command assembles this directory's `sine.s`, compiles the shared
`entry.c` and builds verified firmware through the normal SDK, lint and
patcher. It selects the 32 KiB layout and writes `out/asm/firmware.syx` below
`examples/sharc-sine`.

The [callback contract](../../docs/SHARC-DN2-ABI.md) applies to every edited
program. Only one mod may own the output and reservation. Emulator and
bounded physical evidence are summarized in the
[implementation guide](../../docs/SHARC-DN2.md#qualification); instruction
counts are not hardware timing measurements.

## Package or reproduce the reference artifact

The checked-in `sine.sharc.json` contains author-owned mod bytes in the 32 KiB
layout. It can be packaged without a SHARC compiler, with `DN2_CORE` naming a
built core-dn2 0.2 module:

```sh
python -m elekloader.sdk.build examples/sharc-sine-asm --stock "$DN2_STOCK"
python -m elekloader.lint examples/sharc-sine-asm/out/sharc-sine-asm-0.1.elemod --stock "$DN2_STOCK" --with "$DN2_CORE" --json
python -m elekloader.patch --stock "$DN2_STOCK" --mod "$DN2_CORE" --mod examples/sharc-sine-asm/out/sharc-sine-asm-0.1.elemod --out sine-asm.syx --version SINE
```

To reproduce that exact artifact with the pinned Selache tools:

```sh
mise exec python@3.12.13 -- python examples/sharc-sine-asm/rebuild.py /path/to/selache
```

The reference producer checks final sections, symbol closure, entrypoints,
code/data bytes and arithmetic instruction fields before replacing the
artifact. It retains the 128-byte stack declaration and rejects source
changes. Use `mise run build-asm` when editing the source.

Arithmetic and register-shift checks use the
[SHARC+ Core Programming Reference, revision 1.5](https://www.analog.com/media/en/dsp-documentation/processor-manuals/sc58x-2158x-prm.pdf),
sections 18-3/4 and 18-10. The sign operation uses the fixed-point register
field with the floating extension cleared (section 19-13); the shift count
is held in a register. These checks cover this kernel's emitted instructions,
not the whole ISA. Source and toolchain licensing match the
[C example](../sharc-sine/README.md#source-and-toolchain).
