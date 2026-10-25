# Digitone II SHARC sine

A format-2 mod for the Digitone II OS 1.12 that plays a sine on the DSP:
one voice, peak 0.125, MIDI notes 36 to 84. Core's bridge picks the newest
triggered stock voice slot and calls the mod with its pitch once it has
settled. The mod renders 32 mono samples a block, with a 32-sample attack
and release ramp, and the bridge copies them to both master channels.
Releasing a different slot leaves the note playing.

There is a C version (here) and an [assembly version](../sharc-sine-asm/README.md).
Both need core-dn2 0.2 or newer (`resources.core`), and their build commands
build that core too. This is one processor that owns the output, not a
machine you pick for a sound or an effect on a track.

## Build C or assembly source

One command compiles the current source, makes the SHARC artifact, builds
core and the mod with the normal SDK, lints them, and writes verified
firmware. Edit the source as you like: nothing in elekloader needs to
change for it. Both commands use the 32 KiB layout.

You need [mise](https://mise.jdx.dev/), the usual
[ColdFire cross tools](../../docs/ADAPTING.md#1-what-you-need) for core-dn2,
and the free [Selache](https://github.com/js216/selache) compiler,
assembler, linker and inspector, at a pinned revision. Build Selache once:

```sh
git clone https://github.com/js216/selache.git "$HOME/src/selache"
cd "$HOME/src/selache"
git switch --detach 2b26d3b75c53063575bc5c820fa0d38879335187
mise exec rust@1.98.1 -- cargo build --release --locked -p selcc -p selas -p seld -p seldump
```

Then, back in your elekloader checkout, set the paths to your stock OS and
the tools. No Python packages are needed:

```sh
cd examples/sharc-sine
cp mise.local.example.toml mise.local.toml
# Edit DN2_STOCK and SELACHE in mise.local.toml.
mise trust
mise install
mise run build-c
mise run build-asm
```

In `mise.local.toml`:
- `DN2_STOCK` is your Digitone II OS 1.12 file (SHA-256
  `7e5b8a733d4ab1fdf7e5d22757f7d8eddaa3cbf71d0858874a791844c02ceb35`);
- `SELACHE` is the clean pinned Selache checkout, with its four release
  tools built;
- `ELEKLOADER_CROSS` is the ColdFire tools' prefix (`m68k-elf-` in the
  template);
- `DN2_DIGIHEALTH`, optional, adds a DN2 DigiHealth `.elemod`, for remote
  buttons and diagnostics.

| command | kernel | output, below this folder |
|---|---|---|
| `mise run build-c` | `sine.c` | `out/c/firmware.syx` |
| `mise run build-asm` | `../sharc-sine-asm/sine.s` | `out/asm/firmware.syx` |

Both kernels share `entry.c`. Each build empties its output folder before it
compiles, so a failed build cannot leave old firmware behind.
`out/<variant>/build-report.json` records the hashes of the sources, the
tools and the output, their revisions, and the host. Git ignores the outputs
and your local settings. `mise run test` checks, without firmware or a SHARC
compiler, that bad output is refused, the bounds, the compiler pin and the
handling of old output.

This has been run on macOS arm64, with Python 3.12.13, Rust 1.98.1, Apple's
command-line tools and m68k-elf-gcc 16.2.0. No other host has been tried.
Selache is only this example's compiler: elekloader takes a compatible
artifact from any tool.

## Runtime limits

The layout gives 16,320 bytes of instructions, a 64-byte NOP guard and
16 KiB of data. Each example declares 128 bytes of stack; a mod may declare
up to 428, and more code or data space adds no stack. Follow the
[callback contract](../../docs/SHARC-DN2-ABI.md) for registers, state, the
gate and memory. The packaging checks what is declared; it cannot prove
what the instructions do, what they write, or that they meet the audio
deadline.

[SHARC-DN2.md](../../docs/SHARC-DN2.md#qualification) says what was checked
in builds, in the emulator and on a unit, and where that stops. The
examples are no general guarantee about a compiler or about timing on the
hardware.

## Package or reproduce the reference artifact

Without a SHARC compiler, the normal SDK can package the artifact checked in
here, `sine.sharc.json`. From the top of the checkout, with `DN2_CORE` set
to a matching core-dn2 `.elemod`:

```sh
python -m elekloader.sdk.build examples/sharc-sine --stock "$DN2_STOCK"
python -m elekloader.lint examples/sharc-sine/out/sharc-sine-0.1.elemod --stock "$DN2_STOCK" --with "$DN2_CORE" --json
python -m elekloader.patch --stock "$DN2_STOCK" --mod "$DN2_CORE" --mod examples/sharc-sine/out/sharc-sine-0.1.elemod --out sine.syx --version SINE
```

These use the checked-in artifact: they do not compile an edited source.
It has the same 32 KiB layout as a source build. Maintainers can rebuild
its mod and core bytes exactly, with the table and full-width spill checks:

```sh
mise exec python@3.12.13 -- python examples/sharc-sine/rebuild.py /path/to/selache
```

That check refuses any change to the source on purpose: develop a mod with
the `mise run` tasks instead. The
[artifact format](../../docs/FORMAT.md#digitone-ii-sharc-mod) does not depend
on this tool.

## Source and toolchain

The examples are original code, GPL-2.0-or-later. The pinned Selache tools
are GPL-3.0; the only library file included is Selache's MIT
[`stdint.h`](https://github.com/js216/selache/blob/2b26d3b75c53063575bc5c820fa0d38879335187/libsel/include/stdint.h).
No startup or runtime library is linked.

Selache has known limits in how it encodes negative float initializers and
handles options, so the sine keeps a positive half-table and sets the sign
at run time. An option's name does not prove the data widths or the
instructions: inspect the code it emits. The
[maintainer checks](../sharc-sine-probe/README.md) cover the reference's
sections, data and register preservation.

ADI's proprietary [CrossCore Embedded Studio](https://www.analog.com/en/resources/evaluation-hardware-and-software/software/adswt-cces.html)
could also make an artifact, but no CCES build has been tried here. It
would need its own artifact export and a check of its calling convention.
