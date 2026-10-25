# Digitone II SHARC boot records

The Digitone II's DSP code, section 7, is a stream of ADI boot records.
`elekloader/sharc.py` and its port `js/src/sharc.ts` read the kinds of
record that OS 1.12 uses, and add the mod's and the bridge's records to the
stream. They keep the original headers, payloads and order, including the
loads that overwrite earlier ones during boot.

The format is in the
[ADI ADSP-2156x Hardware Reference, boot chapter](https://www.analog.com/media/en/dsp-documentation/processor-manuals/adsp-2156x_hwr.pdf).
The code and the synthetic test vectors are elekloader's own. The vectors
were also checked with digikit's separate
[checker written from the manual](https://github.com/ElekLabs/digikit/blob/38dfbe4ae88263511eed95691c07fc07e17b6f26/tools/sharc_dev/sharc_bootcheck.py);
none of its code is included. A well-formed stream does not show that the
memory is free at run time, or that the code runs on the hardware.

## The records

Each header is four little-endian uint32s: the block code, the target, the
byte count and the argument. The block code's top byte is `0xad`, its byte
at offset 2 makes the XOR of all 16 header bytes zero, and its low 16 bits
are the flags. Every flag value below has bit 0 set; any other value,
including a record that is only IGNORE, is refused.

| flags | meaning | after the header |
|---|---|---|
| `0x0001` | LOAD: copy to the target, a loader byte address; the argument is zero | `count` bytes |
| `0x0101` | FILL: fill the target with the argument, a uint32 | nothing |
| `0x5001` | FIRST + IGNORE: starts a boot image; the target is its entry (PC16), the argument links to the next image | nothing; the count is zero |
| `0x0801` | INIT, at the entry of the FIRST before it | nothing; count and argument are zero |
| `0x8001` | FINAL, at that entry; it ends the stream | nothing; count and argument are zero |

A boot image starts with FIRST and ends with INIT or FINAL, whose target is
the FIRST's. After INIT another image may follow; FINAL is the last record.
A FIRST's argument counts from the end of its own header:

```
next FIRST's offset (or the stream's end) = FIRST's offset + 16 + its argument
```

The parser refuses a bad count, header, link or order, and bytes after
FINAL. A load or fill is not empty, and its address and count are multiples
of four. Entries count two-byte instruction parcels (PC16), so an odd PC16
value still maps to an aligned instruction address: the stock final entry
`0x1c12e2` is loader byte `0x283825c4`.

`byte_span` (`byteSpan`) turns a `loader-byte`, `core-byte` or `pc16`
address and a size, always in bytes, into a half-open range of loader
bytes. It refuses any other unit, a range past the memory's end, a range
that wraps around, and protected memory. Stock's own records are read
without the protected-memory check: a stock load into protected memory is
kept, not dropped.

## Adding records, and the hook

`insert` takes loads `(address, bytes)` and fills
`(address, byte count, uint32 word)`, in loader bytes. It first checks that
the decoded stream is stock's, by its hash. A new range must lie inside one
the profile owns, keep out of protected memory, and not overlap any load or
fill, stock's or new. Stock's own overlaps stay as they are.

The new records go just before stock's FINAL. Only the last FIRST header
changes: its argument grows by the length added, and its XOR byte is
recomputed. Every other byte stays stock's, and the result is parsed again.
With nothing to add, `insert` returns the stream unchanged.

For the bridge, `insert(..., patches=...)` also replaces stock
instructions, given as `(address, expected, replacement)`. The profile must
grant that range, the expected bytes must be there, and exactly one LOAD
must contain them. Headers, sizes, the order of the records and every other
byte stay stock's.

The Digitone II profile grants the bridge's L1 range with its pitch table,
the mod's 32 KiB in L2, and one six-byte patch, all described in
[SHARC-DN2.md](SHARC-DN2.md). The linker pins the bridge's bytes; a mod's
bytes may change within the [callback contract](SHARC-DN2-ABI.md). Memory
never counts as owned because it exists or because the boot loads nothing
there.

The record code checks the decoded size limit. The container's writer checks
the stored size, the update and flash limits, the packing, the seal (HMAC)
and the protected sections again, on its own.

## Tests

The [shared test vectors](../tests/fixtures/sharc.json) are synthetic: no
stock firmware. The 104-byte input has two boot images, with a deliberate
load over an earlier one. Adding a four-byte load and an eight-byte fill
adds 36 bytes: the first FIRST's argument stays 36, and the last one's grows
from 36 to 72. The expected output is written out literally, so it does not
come from the code under test. Python and TypeScript read the same valid
and invalid records, address cases, boundaries and refusal messages.

```sh
python tests/test_units.py
node --test "js/test/*.test.ts"
```

With `ELEKLOADER_DN2_SYX` set, the tests also check the real stock stream:
all 95 records and the 15 loads and fills that overlap in order are kept,
and the stock firmware is written back and verified, in memory. These tests
save no firmware or extracted data. Without the file they are reported as
skipped.
