# DN2 keyed SHARC callback contract

This covers `dn2-master-mono32-l2-v1`, provided by core-dn2 0.2 on Digitone II
OS 1.12. It provides one exclusive master-output mod. It does not
register a selectable machine, per-voice processor or stereo effect.
The [artifact envelope](FORMAT.md#digitone-ii-sharc-mod) defines the packaged bytes.
The [C/ASM source workflow](../examples/sharc-sine/README.md) uses
the 32 KiB layout with a 428-byte mod stack ceiling.

## Calls and audio

```c
void sharc_init(uint32_t phase_increment);
void sharc_render(float *mono32);
```

The first argument arrives in R4. `phase_increment` advances a uint32 phase
accumulator at 48 kHz. `mono32` is the core-byte pointer `0x268438`, to 32
binary32 floats (128 bytes); its loader alias is `0x28268438`. The callback
may replace those samples or process them in place. On return, the bridge
copies this block to the right channel at core-byte address `0x2684b8`.
It offers no additional audio-buffer ownership. Both callbacks return void.

The bridge runs after stock master processing at the profiled output hook.
Render normally runs once per 32-sample block at 48 kHz. A block lasts about
0.667 ms, shared with stock work; that is not an available DSP time budget.
Render may run before the first init, so cold-zero state must be valid.
Init is a pitch-change call, not a boot callback: the bridge selects the
newest allocated triggered slot, waits until the following render block for
its settled pitch, then calls init. A new simultaneous trigger selects the
highest allocated slot bit. Only MIDI notes 36..84 are supported by this
reader; an unsupported pitch clears its state and emits a silent block.

Read the uint32 gate at core-byte `0x2dd9f0` (loader `0x282dd9f0`): nonzero
means the selected slot remains held. A matching release clears it; another
slot's release does not. Init sees the old/cleared gate; the bridge publishes
the selected gate after init returns. Render continues with gate zero for
release tails. The supplied entry wrapper uses a 32-sample attack/release
ramp. Authors must not write the bridge state or its binding words.

## Register, stack and system-state obligations

The pinned bridge saves/restores full R0..R8 via PX, both original PX halves,
I4/I5/I12, L4/L5, ASTATX/ASTATY and STKYX/STKYY. Callbacks may clobber the
saved data/index/status registers. L4/L5 are cleared before entering mod
code and must return as zero: the bridge uses them for the stereo copy
before restoring the stock values. The bridge does not save every register
on behalf of arbitrary programs.

Callbacks must preserve all other live registers and state they use,
including R9..R15, the other processing element, unsaved DAG index/base/length
registers, modifier registers, accumulators, FIFO state and bank selection.
Preserve MODE1/MODE2, MMASK, IMASK, cache/memory configuration and hardware
PC/loop/status stack contents and depth. Do not assume inactive banks, DAG
lengths or stack bases are zero. Changing SIMD, memory widths, interrupts or
peripherals needs separate context and concurrency qualification.

Calls use the existing Selache short-word function convention: the bridge
sets I6 from I7 and pushes the old frame pointer and return PC minus one.
Return with the matching frame restoration and indirect delayed return;
the C wrapper and assembly kernel show the working sequence. M6=1, M7=-1,
M13=0 and M14=1 are required stock modifier values and must remain unchanged.
I6/I7 must return to their pre-call values. Both stack bases/lengths remain
the stock circular configuration; do not replace them with a flat stack.
The bridge's declared contribution is 116 bytes. The combined declared limit
is 544 bytes, leaving at most 428 for mod calls, locals and spills. The
examples declare 128 mod bytes. This declaration is not a measured high-water
mark and the checker cannot infer actual stack use from machine code.

## Owned bytes and read-ahead

The mod owns loader bytes `[0x20090000,0x20098000)` (32 KiB). Artifact
offsets are relative to that base. Compile code for PC16 `0xbc8000` and use
L2 byte addresses for data. The source layout uses 16 KiB for code, including
the guard, and 16 KiB for data. The larger area supplies no extra stack.

The last 64 bytes of every active mod code segment must be repeated
short-word NOPs (`0100` in loader order). Callback entries must precede that
tail, leaving at most 16,320 linked instruction bytes in the example layout.
Inert mods execute no code. The bridge and its pitch table stay in the
separately owned L1 reservation; mod artifacts cannot write there.

The 64-byte margin is a conservative policy, not proof of every cache or
prefetch mode. The SHARC+ programming reference, revision 1.5, sections
4-5/4-6, describes the VISA alignment buffer, in-flight fetches and fetches
continuing at memory boundaries:
[Analog Devices programming reference](https://www.analog.com/media/en/dsp-documentation/processor-manuals/sc58x-2158x-prm.pdf).
The checker validates padding and entry bounds; it does not decode arbitrary
instructions or prove that branches and writes remain within the contract.

The keyed bridge reads twelve fixed uint32 phase steps at core-byte
`0x2ddd98` (loader `0x282ddd98`). The linker installs these 48 bytes from
the device profile and zeroes the surrounding L1 pitch-table reservation.
They represent notes 36..47; higher octaves shift the step.
Other mod code and data may change without adding executable hashes. Keep
state within owned bytes and the supplied output buffer; preserve stock
memory everywhere else. The bridge's core range, six-byte stock master-output hook,
owner identities and firmware budgets remain independently checked.

## Qualification

Packaging is compiler-independent and certifies the envelope, not the
program. Qualify each generated executable for calling-convention behavior,
actual stack and writes, numerical output, interrupts and audio deadlines.
The [implementation guide](SHARC-DN2.md#qualification) distinguishes
current-source emulator checks from bounded physical tests of sine variants
using this bridge and the 32 KiB reservation.
Emulator instruction counts and short captures are not hardware cycle
measurements, sustained stress or a full ISA guarantee.
