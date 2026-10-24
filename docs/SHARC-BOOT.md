# Digitone II SHARC boot records

`elekloader/sharc.py` and `js/src/sharc.ts` implement the record subset
observed in Digitone II OS 1.12. They preserve original headers, payloads,
ordering and legal boot-phase overwrites while inserting the profiled mod
and core bridge.

The format reference is the
[ADI ADSP-2156x Hardware Reference, boot chapter](https://www.analog.com/media/en/dsp-documentation/processor-manuals/adsp-2156x_hwr.pdf).
The implementation and synthetic vectors are original; the vectors were
independently checked using Digikit's external
[manual-derived checker](https://github.com/ElekLabs/digikit/blob/38dfbe4ae88263511eed95691c07fc07e17b6f26/tools/sharc_dev/sharc_bootcheck.py).
No checker source is included. Structural correctness does not establish
runtime memory ownership or successful hardware execution.

## Supported records

Each header contains four little-endian uint32 values: block code, target,
byte count and argument. The block code has signature byte `0xad`, an XOR
byte at offset 2, and flags in its low 16 bits. XOR over all 16 header bytes
must be zero. Every supported flag combination includes bit 0; other
combinations, including standalone ignored records, are refused.

| Flags | Meaning | Bytes following the header |
|---|---|---|
| `0x0001` | Load to loader-byte target; argument is zero | `count` payload bytes |
| `0x0101` | Fill loader-byte target with the uint32 argument | None |
| `0x5001` | FIRST + IGNORE; target is entry PC16; argument links boot images | None; count is zero |
| `0x0801` | INIT at the preceding FIRST entry | None; count and argument are zero |
| `0x8001` | FINAL at the preceding FIRST entry | None; count and argument are zero |

Each boot image begins with FIRST and ends in INIT or FINAL. INIT permits
another boot image; FINAL ends the stream. INIT/FINAL targets match their
FIRST target. FIRST links are relative to the end of their own headers:

```
next FIRST offset (or stream end) = FIRST offset + 16 + FIRST argument
```

The parser rejects malformed counts, headers, links, ordering and trailing
bytes. Loads/fills must be nonempty; addresses and byte counts must be
multiples of four. Entries count two-byte instruction parcels (PC16): an odd
numeric PC16 value still maps to an aligned physical instruction address.
The stock final entry `0x1c12e2` maps to loader byte `0x283825c4`.

`byte_span` / `byteSpan` converts explicit `loader-byte`, `core-byte` or
`pc16` addresses into half-open loader-byte intervals. Sizes are always in
bytes. It rejects other units, out-of-capacity spans, wraparound and
protected ranges. Original records are parsed without the new-allocation
exclusion check; a protected stock load must be preserved, not discarded.

## Insertion and hook patch

`insert` accepts loads `(address, bytes)` and fills
`(address, byte_count, uint32_word)` in loader-byte coordinates. It checks
the exact decoded-stock hash before modifying anything. New spans must lie
inside a profile-owned interval, avoid protected memory and not overlap
original or new loads/fills. Existing ordered overlaps remain intact.

Loads and fills are inserted immediately before the original FINAL. Only
the last FIRST header changes: its argument grows by the insertion length
and its XOR is recomputed. Every other original byte stays exact. The result
is parsed again. Calling without additions returns the original bytes.

The core bridge also replaces one six-byte stock instruction.
`insert(..., patches=...)` takes `(address, expected, replacement)` tuples.
The profile must explicitly grant that range, the expected bytes must match,
and exactly one LOAD record must contain it. Headers, sizes, record order
and unrelated stock bytes remain unchanged.

The DN2 profile grants the L1 bridge/pitch-table reservation, a 32 KiB L2 mod
reservation and the separate instruction patch described in the
[implementation guide](SHARC-DN2.md). The linker pins the keyed core bridge;
mods using its callback contract may change within the validated envelope.
A capacity map or boot-unloaded interval alone never grants memory.

The record layer enforces the decoded-stream budget. The container writer
independently checks stored size, transfer/flash limits, compression, HMAC
and unchanged protected sections.

## Tests

The [shared corpus](../tests/fixtures/sharc.json) is synthetic and contains
no stock firmware. Its 104-byte input contains two boot images with an
intentional ordered overwrite. Adding a four-byte load and eight-byte fill
adds 36 serialized bytes: the first FIRST argument stays 36 and the last
grows from 36 to 72. Literal expected output is shared by Python and
TypeScript, independently of the implementation under test. Both consume
the same valid/invalid records, alias cases, boundaries and refusal messages.

```sh
python tests/test_units.py
node --test "js/test/*.test.ts"
```

Setting `ELEKLOADER_DN2_SYX` enables exact-stock checks; missing input is
reported as a skip. These checks preserve all 95 stock records and 15
ordered load/fill overlaps, then reproduce and verify the original firmware
in memory. No extracted data or firmware is saved by these round-trip tests.
