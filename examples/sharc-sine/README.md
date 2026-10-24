# Digitone II SHARC sine

This format-2 mod plays a monophonic, 0.125-peak sine from MIDI notes 36–84
on Digitone II OS 1.12. The core bridge selects the newest triggered stock
voice slot and calls the mod with its settled pitch. The mod renders 32 mono
samples with a 32-sample attack/release ramp; the bridge copies them to both
master channels. A different slot's release leaves the selected note playing.

The C and [assembly](../sharc-sine-asm/README.md) examples require core-dn2
**0.2 or newer** through `resources.core`. Their build commands build that
core automatically. This is one exclusive output processor, not a selectable
sound machine or per-track effect.

## Build C or assembly source

The commands compile the current source, create a prelinked SHARC artifact,
build the core and mod through the normal SDK, lint them, and write verified
firmware. Source edits do not require an mod allowlist update.
Both commands use the bounded 32 KiB layout automatically.

Install [mise](https://mise.jdx.dev/) and the normal
[ColdFire cross tools](../../docs/ADAPTING.md#1-what-you-need) for building
core-dn2. Build the pinned free [Selache](https://github.com/js216/selache)
compiler, assembler, linker and inspector once:

```sh
git clone https://github.com/js216/selache.git "$HOME/src/selache"
cd "$HOME/src/selache"
git switch --detach 2b26d3b75c53063575bc5c820fa0d38879335187
mise exec rust@1.98.1 -- cargo build --release --locked -p selcc -p selas -p seld -p seldump
```

Return to the Elekloader checkout, then configure your stock OS and compiler
paths. No Python packages are required:

```sh
cd examples/sharc-sine
cp mise.local.example.toml mise.local.toml
# Edit DN2_STOCK and SELACHE in mise.local.toml.
mise trust
mise install
mise run build-c
mise run build-asm
```

`DN2_STOCK` must name your exact Digitone II OS 1.12 file (SHA-256
`7e5b8a733d4ab1fdf7e5d22757f7d8eddaa3cbf71d0858874a791844c02ceb35`).
`SELACHE` must contain the clean pinned checkout and its four release tools.
`ELEKLOADER_CROSS` selects the ColdFire prefix; the template uses `m68k-elf-`.
Optional `DN2_DIGIHEALTH` includes a DN2 DigiHealth `.elemod` for remote
buttons and diagnostics.

| Command | Kernel | Output below this directory |
|---|---|---|
| `mise run build-c` | `sine.c` | `out/c/firmware.syx` |
| `mise run build-asm` | `../sharc-sine-asm/sine.s` | `out/asm/firmware.syx` |

Both kernels share `entry.c`. Every build replaces its output directory
before compilation, so a failed compile cannot leave stale firmware there.
`out/<variant>/build-report.json` records source, tool and output hashes,
revisions and the host. Outputs and local configuration are ignored by Git.
`mise run test` checks malformed-output refusals, bounds, the compiler pin
and stale-output handling without firmware or a SHARC compiler.

The source workflow has been exercised on macOS arm64 with Python 3.12.13,
Rust 1.98.1, Apple command-line tools and m68k-elf-gcc 16.2.0. Other build
hosts have not been qualified. Selache is the example's producer;
Elekloader itself accepts compatible artifacts from other tools.

## Runtime limits

The layout permits 16,320 linked instruction bytes, a 64-byte NOP
guard and 16 KiB data. Each example declares 128 stack bytes. The maximum
mod stack declaration is 428 bytes; more code/data space does not add stack.
Follow the [callback contract](../../docs/SHARC-DN2-ABI.md) for register,
state, gate and memory obligations. Packaging checks the declared envelope;
it cannot prove arbitrary instructions, runtime writes or audio deadlines.

The [implementation guide](../../docs/SHARC-DN2.md#qualification) describes
source-build, emulator and bounded physical evidence and their limits.
The examples do not establish a general compiler or hardware timing guarantee.

## Package or reproduce the reference artifact

The normal SDK can package the checked-in author-owned artifact without a
SHARC compiler. From the checkout root, set `DN2_CORE` to a matching built
core-dn2 module:

```sh
python -m elekloader.sdk.build examples/sharc-sine --stock "$DN2_STOCK"
python -m elekloader.lint examples/sharc-sine/out/sharc-sine-0.1.elemod --stock "$DN2_STOCK" --with "$DN2_CORE" --json
python -m elekloader.patch --stock "$DN2_STOCK" --mod "$DN2_CORE" --mod examples/sharc-sine/out/sharc-sine-0.1.elemod --out sine.syx --version SINE
```

These commands embed `sine.sharc.json`; they do not compile edited source.
The checked-in reference uses the same 32 KiB layout as the source build. Maintainers can reproduce
its mod and core bytes, including table and full-width spill checks:

```sh
mise exec python@3.12.13 -- python examples/sharc-sine/rebuild.py /path/to/selache
```

This exact-reference check intentionally rejects source changes. Use the
mise build tasks for mod development. The
[artifact format](../../docs/FORMAT.md#digitone-ii-sharc-mod) is independent
of this producer.

## Source and toolchain

The examples are original GPL-2.0-or-later code. The external pinned Selache
tools declare GPL-3.0; the only included library file is MIT
[`stdint.h`](https://github.com/js216/selache/blob/2b26d3b75c53063575bc5c820fa0d38879335187/libsel/include/stdint.h).
No startup or runtime library is linked. Selache's negative float initializer
encoding and option handling have known limitations; the sine uses a positive
half-table and runtime sign handling. Option names alone do not establish
data widths or instruction correctness; emitted code must be inspected.
The [maintainer checks](../sharc-sine-probe/README.md) cover reference
sections, data and register preservation.

ADI [CrossCore Embedded Studio](https://www.analog.com/en/resources/evaluation-hardware-and-software/software/adswt-cces.html)
is an optional proprietary producer, but no CCES build is qualified here.
It needs its own artifact export and calling-convention validation.
