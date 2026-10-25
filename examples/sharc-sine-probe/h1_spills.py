# SPDX-License-Identifier: GPL-2.0-or-later
"""Finite R0-R8/PX spill check for the DN2 master-output bridge.

Independent encodings/bitvectors from ADI SHARC+ PRM rev1.5, Figures 2-2,
2-3, 14-15 and 16-4. This is not an emulator or whole-bridge execution proof.
The supplied inspector listing supplies instruction boundaries; all its bytes
must match the section, and each spill's exact encoding is checked separately.
"""
import random
import re

from qualify import digest, require

MASK32 = (1 << 32) - 1
MASK40 = (1 << 40) - 1
PX, PX1, PX2 = 107, 108, 109


def spill_sequences():
    save = [('s', PX1, -2), ('s', PX2, -3)]
    for reg in range(9):
        save += [('m', PX, reg), ('s', PX1, -4 - 2 * reg),
                 ('s', PX2, -5 - 2 * reg)]
    restore = []
    for reg in [*range(1, 9), 0]:
        restore += [('l', PX1, -4 - 2 * reg), ('l', PX2, -5 - 2 * reg),
                    ('m', reg, PX)]
    return save, restore + [('l', PX1, -2), ('l', PX2, -3)]


def encode_spill(op):
    """Only unconditional Type5b PX/Rn moves and Type15b DM(offset,I6)."""
    kind, dst, src = op
    if kind == 'm':
        require((dst == PX and 0 <= src <= 8) or
                (src == PX and 0 <= dst <= 8), 'unsupported PX move')
        word = (0x7000003f | ((src >> 2) << 22) | (31 << 17) |
                (((src >> 1) & 1) << 16) | ((src & 1) << 15) | (dst << 7))
    else:
        require(kind in ('s', 'l') and dst in (PX1, PX2) and -64 <= src < 64,
                'unsupported PX memory move')
        word = 0x90080000 | (6 << 25) | ((kind == 's') << 24) | (dst << 7) | (src & 127)
    return word.to_bytes(4, 'big')  # ELF code order, before loader parcel swap.


def _spill_operation(raw, mnemonic):
    op = None
    if len(raw) == 4:
        word = int.from_bytes(raw, 'big')
        if word >> 27 == 14:  # Type5b move; do not trust printed operands.
            src = (((word >> 22) & 31) << 2) | (((word >> 16) & 1) << 1) | ((word >> 15) & 1)
            dst = (word >> 7) & 127
            if PX in (src, dst):
                op = ('m', dst, src)
        elif word >> 28 == 9:  # Type15b memory move.
            reg = (word >> 7) & 127
            if reg in (PX1, PX2):
                offset = word & 127
                op = ('s' if word & (1 << 24) else 'l', reg,
                      offset - 128 if offset & 64 else offset)
    if op is not None:
        require(raw == encode_spill(op), 'noncanonical PX spill encoding')
    else:
        require(not re.search(r'\bpx[12]?\b', mnemonic, re.I), 'unsupported PX instruction')
    return op


def _execute(ops, registers, px, memory):
    for kind, dst, src in ops:
        if kind == 'm':
            if dst == PX:
                px = registers[src] << 24
            else:
                registers[dst] = (px >> 24) & MASK40
        elif kind == 's':
            memory[src] = (px >> (0 if dst == PX1 else 32)) & MASK32
        else:
            shift = 0 if dst == PX1 else 32
            px = (px & ~(MASK32 << shift)) | (memory[src] << shift)
    return px


def _roundtrips(save, restore):
    rng = random.Random(0x21569)
    cases = lost_low_byte = 0
    for low in range(256):
        for high in (0, MASK32, 0x80000000, 0x12345678):
            initial = {n: (((high ^ (n * 0x01010101)) & MASK32) << 8) | (low ^ n)
                       for n in range(9)}
            original_px = rng.getrandbits(64)
            memory = {}
            _execute(save, dict(initial), original_px, memory)
            require(set(memory) == set(range(-21, -1)), 'unexpected spill slots')
            registers = {n: rng.getrandbits(40) for n in range(9)}
            px = _execute(restore, registers, rng.getrandbits(64), memory)
            require(registers == initial and px == original_px, '40-bit roundtrip failed')
            # R0 is restored last. Omitting its PX1 load leaves R8's different
            # low byte in PX, so every one of these 1024 cases must fail.
            broken = {n: 0 for n in range(9)}
            _execute(restore[:24] + restore[25:], broken, 0, memory)
            require(broken[0] != initial[0], 'omitted PX1 restore escaped detection')
            if low:
                require((initial[0] >> 8) << 8 != initial[0], '32-bit negative escaped')
                lost_low_byte += 1
            cases += 1
    return dict(roundtrips=cases, omitted_px1_failures=cases,
                normal32_low_byte_failures=lost_low_byte)


def verify_full_width_spills(code, base_pc16, disassembly):
    """Check this bridge's fixed spill policy against bytes and a seldump listing.

    Code is an ELF section in instruction byte order; base_pc16 is its address
    in 16-bit instruction parcels, not a core-byte or loader-byte address.
    This checks the spill subsequence, not intervening code, call effects,
    control flow, stack ownership, interrupt nesting, timing or hardware.
    """
    rows = re.findall(r'(?m)^  ([0-9a-f]{8})\s+([0-9a-f]+)\s+(.+)$', disassembly)
    decoded = bytearray()
    operations, offsets = [], []
    for pc, encoded, mnemonic in rows:
        require(int(pc, 16) == base_pc16 + len(decoded) // 2, 'disassembly gap/overlap')
        require(len(encoded) in (4, 8, 12) and not mnemonic.startswith('.'),
                'unknown instruction in listing')
        raw = bytes.fromhex(encoded)
        op = _spill_operation(raw, mnemonic)
        if op is not None:
            operations.append(op)
            offsets.append(len(decoded))
        decoded.extend(raw)
    require(decoded == code, 'listing does not match every code byte')
    save, restore = spill_sequences()
    require(operations == save + restore, 'changed or missing full-width spills')
    return dict(code_sha256=digest(code), spill_instructions=len(operations),
                spill_offsets=offsets, **_roundtrips(save, restore))


def self_test():
    """Firmware-free omission, corruption and incorrect-source checks."""
    save, restore = spill_sequences()
    original = [encode_spill(op) for op in save + restore]

    def check(instructions):
        offset, listing = 0, []
        for raw in instructions:
            listing.append(f'  {offset // 2:08x}   {raw.hex()}   px')
            offset += len(raw)
        return verify_full_width_spills(b''.join(instructions), 0, '\n'.join(listing))

    result = check(original)
    mutants = {
        'omitted PX1 restore': original[:53] + original[54:],
        'reserved move bit': original[:2] +
            [(int.from_bytes(original[2], 'big') ^ (1 << 14)).to_bytes(4, 'big')] + original[3:],
        'wrong source register': original[:2] + [encode_spill(('m', PX, 1))] + original[3:],
        'normal32 substitute': original[:2] + [bytes.fromhex('703e003f')] + original[3:],
    }
    for name, instructions in mutants.items():
        try:
            check(instructions)
        except ValueError:
            continue
        raise ValueError(f'{name}: incorrectly accepted')
    return dict(**result, rejected_mutations=list(mutants))
