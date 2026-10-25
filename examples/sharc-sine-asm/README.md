# Digitone II SHARC assembly sine

The [C sine](../sharc-sine/README.md) with its sample loop written in SHARC+
assembly. Both use the same C entry wrapper, phase table, 0.125 peak, note
range and gain ramp. Core-dn2 0.1 picks the note and copies the mono block
to both master channels.

## Build

Set up as the C sine's [source build](../sharc-sine/README.md#build-c-or-assembly-source)
says, then, in `examples/sharc-sine`:

```sh
mise run build-asm
```

It assembles this folder's `sine.s`, compiles the shared `entry.c`, and
builds verified firmware with the normal SDK, lint and patcher, in the
32 KiB layout. The firmware is `examples/sharc-sine/out/asm/firmware.syx`.

The [callback contract](../../docs/SHARC-DN2-ABI.md) applies to every edited
program. Only one mod can own the output and the 32 KiB.
[SHARC-DN2.md](../../docs/SHARC-DN2.md#runtime-limits) describes the runtime
limits. Qualify each changed executable and its timing on the target device.

## Package or reproduce the reference artifact

The checked-in `sine.sharc.json` is this mod's code, in the 32 KiB layout.
Package it without a SHARC compiler, with `DN2_CORE` set to a built
core-dn2 0.1 `.elemod`:

```sh
python -m elekloader.sdk.build examples/sharc-sine-asm --stock "$DN2_STOCK"
python -m elekloader.lint examples/sharc-sine-asm/out/sharc-sine-asm-0.1.elemod --stock "$DN2_STOCK" --with "$DN2_CORE" --json
python -m elekloader.patch --stock "$DN2_STOCK" --mod "$DN2_CORE" --mod examples/sharc-sine-asm/out/sharc-sine-asm-0.1.elemod --out sine-asm.syx --version SINE
```

To rebuild exactly that artifact with the pinned Selache tools:

```sh
mise exec python@3.12.13 -- python examples/sharc-sine-asm/rebuild.py /path/to/selache
```

Before it replaces the artifact, it checks the final sections, that every
symbol resolves, the entry points, the code and data bytes, and the
arithmetic instructions' fields. It keeps the 128-byte stack declaration
and refuses any change to the source: use `mise run build-asm` to edit it.

The arithmetic and register-shift checks follow the
[SHARC+ Core Programming Reference, revision 1.5](https://www.analog.com/media/en/dsp-documentation/processor-manuals/sc58x-2158x-prm.pdf),
sections 18-3, 18-4 and 18-10. The sign operation uses the fixed-point
register field with the floating extension cleared (section 19-13), and the
shift count is held in a register. These checks cover this kernel's
instructions, not the whole instruction set. Its sources and tools are
licensed as the C example's are ([source and toolchain](../sharc-sine/README.md#source-and-toolchain)).
