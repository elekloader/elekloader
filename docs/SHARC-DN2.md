# Digitone II SHARC: how it works and what was checked

Core-dn2 0.2 lets one mod run code on the Digitone II's DSP, at the master
output, on OS 1.12. The DSP is an ADSP-21569 running SHARC+ VISA
instructions; its silicon stepping has not been observed. This page is for
maintainers. Mod authors start with the
[sine example](../examples/sharc-sine/README.md); the
[callback contract](SHARC-DN2-ABI.md) says what a mod's code must do, and
[FORMAT](FORMAT.md#digitone-ii-sharc-mod) what the file holds.

## The stock file and the writer

The Python and TypeScript device profiles pin these inputs:

| input | SHA-256 |
|---|---|
| OS 1.12 SysEx | `7e5b8a733d4ab1fdf7e5d22757f7d8eddaa3cbf71d0858874a791844c02ceb35` |
| section 7, stored | `5ff93df8747bdf9eb28acf50391fb484d6aa6782bfe969b2b3807a8412645660` |
| section 7, decoded | `336e340aa0cdcd34e314cfa44849f709a3134f6bd4cd57dfc7e15702c83115e2` |

Section 7, the DSP's code, is 602,076 bytes stored and 836,956 decoded. It
is 95 [boot records](SHARC-BOOT.md): two boot images, and loads that
overwrite earlier ones in order. The writer keeps their order and bytes,
except for the profile's one hook and the bookkeeping of the records it
adds. The bootloader and the other protected sections stay stock, and a
build with no SHARC mod leaves section 7 stock too.

The stock tests check these hashes, the records' structure, and that writing
with no change gives the stock file back. They take your stock file from
`ELEKLOADER_DN2_SYX`; the repository holds no firmware.

## Memory

Every range is half-open, in loader bytes:

| what | range |
|---|---|
| core's bridge and its state | `[0x282dd600,0x282dda00)` |
| the bridge's pitch table and padding | `[0x282dda00,0x282dde00)` |
| the mod | `[0x20090000,0x20098000)` (L2) |
| the stock instruction the bridge replaces | `[0x2838489a,0x283848a0)` |

The examples split the mod's 32 KiB into 16 KiB of code and 16 KiB of data.
The last 64 bytes of code are a NOP guard against instructions read ahead,
which leaves 16,320 bytes of instructions. The stack is separate from all
of this: the bridge declares 116 bytes, and a mod may declare up to 428, for
544 in all.

**Addresses.** The
[ADSP-2156x data sheet, tables 2 to 6](https://www.analog.com/media/en/technical-documentation/data-sheets/adsp-21562-21563-21565-21566-21567-21569.pdf),
gives the byte, normal-word and short-word views of memory. An L1 loader
address is the core byte address plus `0x28000000`. Instruction addresses
(PC16) count two-byte parcels: the stock master-output hook at PC16
`0x1c244d` is loader byte `0x2838489a`, and the data pointer `0x268438` is
loader byte `0x28268438`. The mod's code starts at PC16 `0xbc8000`; its data
uses byte addresses. An address existing, or mapping onto another, does not
make it free.

**Left alone.** The profile keeps new code and data out of
`[0x200fa000,0x20100000)`, the DN2's special window and the boot ROM's
workspace, and out of the L1 cache tails as configured. These come from
[the memory investigation](https://github.com/naturarum/dn2-cfw/blob/5c95c78180348c825518f2bda8d2cf1afb2a312a/notes/T3.2-sharc-memmap.md#L293-L303)
and its [cache correction](https://github.com/naturarum/dn2-cfw/blob/5c95c78180348c825518f2bda8d2cf1afb2a312a/notes/T3.2-sharc-memmap.md#L586-L593).

## Size limits

Each is checked on its own:

| what | limit |
|---|---:|
| section 7, stored | `0x100000` bytes |
| section 7, decoded | `0xfffff` bytes |
| the update, with its 8-byte preamble | `0x200000` bytes |
| flash | up to `0x380000`; the container starts at `0x80000` |

The section 7 limits come from the stock sender's two 1 MiB staging
buffers; the second also holds a command byte. The tests hash the sender's
828 bytes at main OS address `0x400cf4b0` against the published
[sender analysis](https://github.com/ElekLabs/digikit/blob/38dfbe4ae88263511eed95691c07fc07e17b6f26/docs/findings/06-sharc-engine-and-startup.md#L5221-L5255).
The update limit is a cautious figure measured over USB, not a proven size
of the receive buffer. None of these limits is about mod RAM or stack. The
writer also checks the packing, the seal (HMAC), the flash bounds, and that
the main OS can be unpacked in place safely.

## How sure we are the memory is free

The last byte the stock boot loads into L2 is at `0x2008823c`. Nothing being
loaded somewhere does not prove it is free. A static analysis of the exact
stock code, with
[digikit's corrected access-width analysis](https://github.com/simonjohansson/digikit/commit/3ed5849c1fe55a7c1926c023e5617a51a1564989),
found no direct access, data reference or resolved pointer range that
touches the mod's 32 KiB. It resolved 2,979 pointer rows and left 192
unknown, out of 25,878 indirect accesses. Indirect, SIMD and circular
addressing are not fully analysed, so this is not a complete proof.

In the coupled emulator, a canary over unmodified stock stayed intact at 44
checkpoints over 5,136 DSP blocks, with MIDI, transport, pattern, page and
effect-send requests; corrupting a byte on purpose was detected. That cannot
see reads, writes of the same value, or a write undone before the next
checkpoint, and it says nothing about the real cache. The implementation
changes no cache, interrupt or memory-controller setting. The area stays
experimental and limited to 32 KiB.

## Qualification

The current C and assembly sources were checked with fresh builds,
Python/TypeScript parity, and the coupled emulator. The C and assembly
builds gave identical 99,904-frame captures for notes 60, 69 and 72: finite
samples, equal stereo channels, a peak of 0.125, silence after release, and
no halt or missing block in the emulator. A clean checkout at a path with
spaces built the same firmware bytes from the same inputs.

On a unit, over USB, sine variants in C and assembly ran with the same
bridge and the same placement, some with doubled phase steps. Each L2
variant ran ten minutes of changing notes and releases over notes 36 to 84:
392 note/release pairs, equal finite stereo channels, silence at the end,
and no capture drops reported by the host. Limited checks of pattern and
page changes, raised effect sends and project loads also completed. That is
evidence for the implementation and the placement. The unit ran those
variants; the firmware the current examples build was checked in the
emulator, as above.

The sine replaces the master output, so its capture cannot show how the
stock voices or effects sound underneath. None of this establishes that any
program is safe, how the DAC behaves, stress with heavy stock projects, the
real cache and interrupt behaviour, the total DSP load or the cycle margin.
Test each changed program on its own, including its timing. The 64-byte
guard and the declared stack are packaging rules, not proofs of what the
DSP really fetches or how much stack a program really uses.

## Sources and licences

The boot-record parser and its synthetic tests are elekloader's own code,
written from the
[ADI hardware reference](https://www.analog.com/media/en/dsp-documentation/processor-manuals/adsp-2156x_hwr.pdf)
and checked independently. The Python is GPL-2.0-or-later and its
TypeScript port GPL-3.0-or-later. digikit is an outside research and
emulation tool; none of its current parser or emulator code is copied here.

The examples use
[Selache `2b26d3b75c53063575bc5c820fa0d38879335187`](https://github.com/js216/selache/tree/2b26d3b75c53063575bc5c820fa0d38879335187),
whose tools are GPL-3.0. The only library file the examples include is
Selache's MIT
[`stdint.h`](https://github.com/js216/selache/blob/2b26d3b75c53063575bc5c820fa0d38879335187/libsel/include/stdint.h);
no startup or runtime library is linked. The example sources and the
checked-in artifacts are their authors' own, not extracted from stock
firmware.
