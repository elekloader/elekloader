# The Digitone II SHARC callbacks

This is the contract between core-dn2 0.2's bridge and a SHARC mod on the
Digitone II OS 1.12, ABI `dn2-master-mono32-l2-v1`: what the bridge does,
and what the mod's code must do. It is experimental.

One mod at a time owns the master output. It is not a machine you pick for a
sound, a per-voice processor or a stereo effect. [FORMAT.md](FORMAT.md#digitone-ii-sharc-mod)
describes the file, and the [sine example](../examples/sharc-sine/README.md)
is a working mod in C and in assembly.

## The two calls

```c
void sharc_init(uint32_t phase_increment);
void sharc_render(float *mono32);
```

**render** normally runs once per block of 32 samples at 48 kHz, after the
stock master processing. A block lasts about 0.667 ms and the stock work
shares it: that is not the mod's time budget. `mono32` points to the
block, 32 binary32 floats (128 bytes) at core byte `0x268438` (loader
`0x28268438`). Replace them, or change them in place. When render returns,
the bridge copies the block over the right channel, at core byte
`0x2684b8`, so the output is mono. The mod owns no other audio buffer.

**init** is called when the pitch changes; it is not a boot call. Its
argument, in R4, is a phase step: add it to a uint32 phase once per sample
at 48 kHz. Render may run before the first init, so state that starts at
zero must be valid.

Both return nothing.

## Which note

The bridge follows the stock voices:
- it picks the newest allocated voice slot that was triggered. When several
  trigger at once, it picks the highest slot bit;
- it waits for the next block, when that slot's pitch has settled, then
  calls init;
- it handles MIDI notes 36 to 84. For any other pitch it clears its state,
  and the block is silent.

## The gate

A uint32 at core byte `0x2dd9f0` (loader `0x282dd9f0`) is nonzero while the
selected slot is held. Releasing that slot clears it; releasing another slot
does not. Init sees the old gate, or a cleared one: the bridge sets the new
gate after init returns. Render keeps running with the gate at zero, for
release tails. The examples' entry wrapper ramps over 32 samples on attack
and release.

Do not write the bridge's state or its binding words.

## Registers

The bridge saves and restores:
- R0 to R8 in full (through PX), and both halves of PX;
- I4, I5 and I12; L4 and L5;
- ASTATX, ASTATY, STKYX and STKYY.

A callback may change these, with one exception: the bridge clears L4 and
L5 before it calls the mod, and they must be zero again when the call
returns. The bridge uses them for the stereo copy before it restores
stock's values.

Leave everything else you use as you found it. That includes:
- R9 to R15, and the other processing element;
- the DAG index, base and length registers the bridge does not save, and
  the modifier registers;
- the accumulators, the FIFO state and the bank selection;
- MODE1, MODE2, MMASK, IMASK, and the cache and memory configuration;
- the hardware PC, loop and status stacks: their contents and their depth.

Do not assume that an inactive bank, a DAG length or a stack base is zero.
Changing SIMD, memory widths, interrupts or peripherals needs its own
testing for context and concurrency.

## The calling convention and the stack

The calls use Selache's short-word function convention. The bridge sets I6
from I7 and pushes the old frame pointer and the return PC minus one. Return
with the matching frame restore and an indirect delayed return; the C
wrapper and the assembly kernel show the sequence.
- M6 = 1, M7 = -1, M13 = 0 and M14 = 1 are stock's values; keep them.
- I6 and I7 must be back at their values from before the call.
- Keep both stacks' bases and lengths in stock's circular configuration: do
  not switch to a flat stack.

The bridge declares 116 bytes of stack, and the bridge and the mod together
may declare at most 544, so a mod has at most 428 for its calls, locals and
spills. The examples declare 128. A declaration is not a measurement: the
checker cannot work out real stack use from machine code. A larger code or
data area gives no more stack.

## Memory

| what | where |
|---|---|
| the mod's 32 KiB | loader `[0x20090000,0x20098000)`. Link code for PC16 `0xbc8000`; data uses L2 byte addresses. Offsets in the file count from the start |
| the audio block | core byte `0x268438`, 128 bytes; the bridge writes the right channel at `0x2684b8` |
| the gate | core byte `0x2dd9f0` |
| the pitch table | core byte `0x2ddd98` (loader `0x282ddd98`), 48 bytes |

Keep your state in your 32 KiB and the audio block, and leave all other
memory as stock has it. The examples split the area into 16 KiB of code,
guard included, and 16 KiB of data.

**The guard.** The last 64 bytes of every code segment of an active mod
must be short-word NOPs (`0100` in loader order), and the entries must come
before them. In the examples' layout that leaves 16,320 bytes of
instructions. An inert mod runs no code. The 64 bytes are a conservative
rule, not a proof for every cache or prefetch mode: the
[SHARC+ programming reference](https://www.analog.com/media/en/dsp-documentation/processor-manuals/sc58x-2158x-prm.pdf),
revision 1.5, sections 4-5 and 4-6, describes the VISA alignment buffer,
fetches in flight, and fetches that continue at memory boundaries. The
checker checks the padding and where the entries are. It does not decode
instructions, so it cannot prove that branches and writes stay inside the
contract.

**The pitch table** holds twelve uint32 phase steps, for notes 36 to 47;
higher octaves shift the step. The linker writes these 48 bytes from the
device profile and zeros the rest of the table's 1 KiB. The bridge and its
table live in their own L1 reservation, which a mod cannot write to.

The mod's code and data may change without any hash being added to
elekloader. The bridge's range, the six-byte hook, which mod owns which
range, and the size limits are still checked on every build.

## Testing a program

The packaging checks are the same whatever made the code, and they check the
file's shape, not the program. Test each executable for its calling
convention, its real stack use and writes, its output, interrupts and audio
deadlines. [SHARC-DN2.md](SHARC-DN2.md#qualification) separates the
emulator checks of the current sources from the tests on a unit, which ran
sine variants with this bridge and layout. Emulator instruction counts
and short captures are not hardware cycle measurements, sustained stress
tests or a guarantee for the whole instruction set.
