# Digitone II SHARC implementation

Core-dn2 0.2 supports one SHARC master-output mod on Digitone II
OS 1.12. The processor is ADSP-21569, using SHARC+ VISA instructions; the
silicon stepping has not been observed. Start with the
[C/ASM example](../examples/sharc-sine/README.md). The
[callback contract](SHARC-DN2-ABI.md) defines the runtime obligations, and
[FORMAT](FORMAT.md#digitone-ii-sharc-mod) defines the compiler-independent
artifact accepted by the SDK.

## Stock identity and writer

The Python and TypeScript device profiles pin the following inputs:

| Input | SHA-256 |
|---|---|
| OS 1.12 SysEx | `7e5b8a733d4ab1fdf7e5d22757f7d8eddaa3cbf71d0858874a791844c02ceb35` |
| Stored section 7 | `5ff93df8747bdf9eb28acf50391fb484d6aa6782bfe969b2b3807a8412645660` |
| Decoded section 7 | `336e340aa0cdcd34e314cfa44849f709a3134f6bd4cd57dfc7e15702c83115e2` |

Section 7 is 602,076 stored bytes and 836,956 decoded bytes. Its 95
[boot records](SHARC-BOOT.md) include two boot images and ordered load
replacements. The writer preserves their order and bytes except for the
explicitly profiled hook and insertion bookkeeping. The bootloader and all
other protected sections remain stock. Core-only builds leave section 7
unchanged.

The stock tests verify these hashes, record structure and an exact no-change
round trip. They accept the owner's stock file through `ELEKLOADER_DN2_SYX`;
no firmware is distributed with the repository.

## Memory layout

All ranges below are half-open loader-byte addresses:

| Purpose | Range |
|---|---|
| Core bridge and state | `[0x282dd600,0x282dda00)` |
| Bridge pitch table and padding | `[0x282dda00,0x282dde00)` |
| L2 mod | `[0x20090000,0x20098000)` |
| Stock instruction replaced by the bridge | `[0x2838489a,0x283848a0)` |

The source workflow divides the 32 KiB L2 mod area into 16 KiB code
and 16 KiB data. The final 64 code bytes are a NOP read-ahead guard, leaving
16,320 linked instruction bytes. The 428-byte maximum declared mod stack is
independent of that reservation; the bridge contributes another 116 bytes
to the 544-byte combined limit.

The [ADSP-2156x data sheet, Tables 2–6](https://www.analog.com/media/en/technical-documentation/data-sheets/adsp-21562-21563-21565-21566-21567-21569.pdf)
distinguishes byte, normal-word and short-word views. L1 loader addresses add
`0x28000000` to core-byte addresses. Instruction addresses count two-byte
parcels: the stock master-output hook at PC16 `0x1c244d` maps to loader
byte `0x2838489a`, while data pointer `0x268438` maps to `0x28268438`. The L2 mod starts at PC16 `0xbc8000`;
its data uses byte addresses. Capacity and address equivalence do not grant
ownership.

The profile excludes `[0x200fa000,0x20100000)` for the DN2 special window and
Boot ROM workspace, and the configured L1 cache tails. Those exclusions use
[the memory investigation](https://github.com/naturarum/dn2-cfw/blob/5c95c78180348c825518f2bda8d2cf1afb2a312a/notes/T3.2-sharc-memmap.md#L293-L303)
and its [cache correction](https://github.com/naturarum/dn2-cfw/blob/5c95c78180348c825518f2bda8d2cf1afb2a312a/notes/T3.2-sharc-memmap.md#L586-L593).

## Independent size limits

| Constraint | Limit |
|---|---:|
| Stored DSP section | `0x100000` bytes |
| Decoded DSP stream | `0xfffff` bytes |
| Decoded update, including its 8-byte preamble | `0x200000` bytes |
| Flash endpoint | `0x380000`; the container begins at `0x80000` |

The DSP limits come from the stock sender's two 1 MiB staging buffers; the
second also carries a command byte. The 828-byte sender region at main
address `0x400cf4b0` is hash-checked against the published
[sender analysis](https://github.com/ElekLabs/digikit/blob/38dfbe4ae88263511eed95691c07fc07e17b6f26/docs/findings/06-sharc-engine-and-startup.md#L5221-L5255).
The update cap is a conservative measured USB limit, not a proved receive
buffer boundary. These encoding budgets are separate from mod RAM and stack.
The writer also checks packing, HMAC, flash bounds and safe in-place
unpacking of the main image.

## Memory evidence and limits

The last boot-loaded L2 byte ends at `0x2008823c`. An unloaded interval alone
is not proof of free memory. Exact-stock static analysis with
[Digikit's corrected access-width analysis](https://github.com/simonjohansson/digikit/commit/3ed5849c1fe55a7c1926c023e5617a51a1564989)
found no decoded direct accesses, data references or resolved pointer
footprints overlapping the 32 KiB mod range. It resolved 2,979 pointer rows
and left 192 unknown, against 25,878 indirect accesses. Incomplete indirect,
SIMD and circular addressing analysis prevents a complete ownership proof.

An unmodified-stock coupled-emulator canary remained intact at 44 checkpoints
across 5,136 DSP blocks with MIDI, transport, pattern, page and effects-send
requests; deliberate byte corruption was detected. This misses reads,
same-value writes and overwrite-and-restore between checkpoints. It does not
establish physical cache behavior. The implementation changes no cache,
interrupt or memory-controller configuration. The reservation remains
experimental and bounded to 32 KiB.

## Qualification

The current C/ASM source workflow has been checked through fresh builds,
Python/TypeScript parity and coupled-emulator execution.
The C and ASM builds produced identical 99,904-frame PCM captures for notes
60/69/72, finite equal stereo channels, peak 0.125 and release to silence,
with no modeled halt or missing block. A clean checkout at a path containing
spaces reproduced the firmware bytes with the same inputs.

Physical USB checks exercised the same bridge and placements with keyed C
and ASM sine variants, including variants with doubled phase increments.
Each L2 variant completed ten minutes of changing notes and releases over
36..84: 392 note/release pairs, equal finite stereo channels and final
silence, with no reported host capture drops. Bounded pattern/page,
raised-effects-send and project-load checks also completed. These are
implementation and placement evidence; the exact current firmware's build
and emulator results are distinct from those physical candidates.

The sine replaces the master output, so its capture cannot establish the
quality of underlying stock voices or effects. These checks do not establish
arbitrary-program safety, DAC behavior, heavy stock-project stress,
physical cache/interrupt behavior, total DSP load or cycle margin. Each
changed program needs its own execution and timing qualification. The
64-byte code guard and declared stack bounds are packaging policies, not
proofs of actual instruction fetches or stack use.

## Sources and licensing

The boot-record parser and synthetic tests are original Elekloader code,
using the [ADI hardware reference](https://www.analog.com/media/en/dsp-documentation/processor-manuals/adsp-2156x_hwr.pdf)
and independent external checks. The Python implementation is
GPL-2.0-or-later and its TypeScript port GPL-3.0-or-later. Digikit is an
external research and execution tool; no current Digikit parser or emulator
source is copied into this support.

The examples use external
[Selache `2b26d3b75c53063575bc5c820fa0d38879335187`](https://github.com/js216/selache/tree/2b26d3b75c53063575bc5c820fa0d38879335187)
tools under GPL-3.0. Their only included library file is the MIT
[`stdint.h`](https://github.com/js216/selache/blob/2b26d3b75c53063575bc5c820fa0d38879335187/libsel/include/stdint.h);
no startup or runtime library is linked. The example sources and checked-in
artifacts are author-owned, not extracted stock firmware.
