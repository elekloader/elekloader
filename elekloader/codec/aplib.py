# SPDX-License-Identifier: GPL-2.0-or-later
# Vendored from digikit (https://github.com/m-dwyer/digikit), dt2/aplib.py,
# under the GNU GPL version 2 or (at your option) any later version. By Em D; the match-finder and the compressed output by irpina. See NOTICE.
"""Encode the firmware family's aPLib-shaped container streams.

The default compressor chooses back-references by bit cost, with last-offset
reuse and one-byte lookahead. ``compress=False`` emits literals only for
reference checks; it adds one tag byte per eight input bytes plus a terminator.
The format differs from public aPLib and is decoded by ``elz.py``:

* Tag bits are MSB-first: 1 copies a literal byte; 0 reads a gamma-coded match.
  Gamma codes start at 1 and consume (data, stop) pairs; stop bit 1 ends them.
* Gamma 2 reuses the previous offset. Otherwise the gamma value and a raw byte
  give ``offset = ((gamma << 8) + byte) - 767``. A two-bit length of 1..3 is
  direct; 00 reads a gamma length plus 2. The copy adds 1 byte, and another
  byte when the offset exceeds 3328.
* There is no mandatory first literal. The terminator uses gamma ``2**24+2``
  and raw byte 0xFF: the combined value wraps to 767 in the 32-bit depacker,
  reaching its exit before any copy. This does not assume how Elektron's
  encoder chooses its terminator.
* ``pack_section`` prepends an 8-byte big-endian header: compressed length
  and the packed bytes' sum modulo 2**32. The depacker skips that header.

``_self_test`` checks host round-trips, including an independent store-only
decoder. Firmware-depacker, in-place staging and physical update evidence are
recorded separately in docs/DEVICES.md; host round-trips do not establish
that an update fits the device's receive buffer.
"""
import struct

REUSE = 2      # dt2/elz.py: a gamma of 2 reuses the last offset


def _gamma_bits(value):
    """MSB-first bit sequence this depacker's gamma2 reader would consume to
    decode to `value` (excluding the implicit leading 1): for each bit of
    `value` after its leading 1, emit that bit, then a raw stop-bit (1 on the
    last one, 0 otherwise) -- the inverted-continue polarity confirmed above.
    """
    if value < 2:
        raise ValueError('gamma2 cannot encode values below 2, got %d' % value)
    tail = bin(value)[3:]  # binary digits after the leading '1'
    out = []
    for i, ch in enumerate(tail):
        out.append(int(ch))
        out.append(1 if i == len(tail) - 1 else 0)
    return out


# The terminator: a match dispatch (tag bit 0) whose gamma-coded value is
# 2**24 + 2, followed by one raw byte 0xFF. See the module docstring for why.
_TERMINATOR_GAMMA_VALUE = (1 << 24) + 2
_TERMINATOR_TRAILER_BYTE = 0xFF


# Match coding follows the local elz.py decoder.
_BIAS = 767              # offset = ((g << 8) + byte) - 767
_FAR = 3328              # past this, the decoder adds 1 to the length
_MIN_MATCH = 3           # the hash key: candidates share their first three bytes
_MAX_OFFSET = (1 << 20) - 1   # Elektron's own streams reach back up to just under 1 MiB, on every device
_MAX_COUNT = 1 << 11     # Elektron's own streams never match more than 2048 bytes (issue #59)
_CANDIDATES = 8          # bound the positions tried per hash chain
_LITERAL_BITS = 9


def _match_events(offset, count):
    """-> events for one back-reference: copy `count` bytes from `offset` back.

    Raises rather than emitting something the decoder would read differently,
    because a silently wrong match produces a stream that depacks to the wrong
    bytes and nothing downstream compares compressed bytes.
    """
    if not 1 <= offset <= _MAX_OFFSET:
        raise ValueError('offset %d out of range' % offset)
    length = count - 1
    if offset > _FAR:
        length -= 1          # the decoder will add it back
    if length < 1:
        raise ValueError('count %d too small for offset %d' % (count, offset))
    raw = offset + _BIAS
    g, trailer = raw >> 8, raw & 0xFF
    if g == REUSE:
        raise ValueError('offset %d would encode as the reuse code' % offset)

    out = [(0, b'')]                                   # tag bit 0: a match
    gbits = _gamma_bits(g)
    for i, bit in enumerate(gbits):                    # the offset's gamma,
        out.append((bit, bytes((trailer,))             # its raw byte riding
                    if i == len(gbits) - 1 else b''))  # the last bit
    if 1 <= length <= 3:
        out.append(((length >> 1) & 1, b''))
        out.append((length & 1, b''))
    else:
        out.append((0, b''))                           # 00: a gamma follows
        out.append((0, b''))
        for bit in _gamma_bits(length - 2):
            out.append((bit, b''))
    return out


def _reuse_events(offset, count):
    """-> events for one back-reference that reuses the last offset: a gamma of
    2 (the reuse code) in place of the offset, then the length as in a match."""
    length = count - 1 - (1 if offset > _FAR else 0)
    if length < 1:
        raise ValueError('count %d too small for offset %d' % (count, offset))
    out = [(0, b'')] + [(bit, b'') for bit in _gamma_bits(REUSE)]
    if length <= 3:
        out += [((length >> 1) & 1, b''), (length & 1, b'')]
    else:
        out += [(0, b''), (0, b'')] + [(bit, b'') for bit in _gamma_bits(length - 2)]
    return out


def _gamma_cost(value):
    return 2 * (value.bit_length() - 1)


def _match_cost(offset, count, reuse):
    """-> the bits a match costs, or None when the decoder cannot express it."""
    length = count - 1 - (1 if offset > _FAR else 0)
    if length < 1:
        return None
    lc = 2 if length <= 3 else 2 + _gamma_cost(length - 2)
    return 1 + (_gamma_cost(REUSE) if reuse else _gamma_cost((offset + _BIAS) >> 8) + 8) + lc


def _parse(data):
    """-> [('L', byte) | ('M', offset, count) | ('R', offset, count), ...]: a
    literal, a match, or a match that reuses the last offset.

    Each step takes the back-reference that saves the most bits over
    literals: the last offset, then the newest _CANDIDATES positions that
    share the next three bytes, within _MAX_OFFSET (ties keep the first
    found). It looks one byte ahead, and emits a literal instead when the
    match there saves more than a literal costs. The last offset is reused
    only once a match has set it (the decoder starts it at 1; Elektron's
    streams never rely on that). Every position is hashed once, in order.
    """
    n = len(data)
    head, prev = {}, [-1] * n
    hashed = [0]                          # positions below this are in the chains

    def hash_to(p):
        while hashed[0] <= p:
            q = hashed[0]
            if q + _MIN_MATCH <= n:
                k = (data[q] << 16) | (data[q + 1] << 8) | data[q + 2]
                prev[q] = head.get(k, -1)
                head[k] = q
            hashed[0] = q + 1

    def run(i, j, limit):
        k = 0
        while k < limit and data[i + k] == data[j + k]:
            k += 1
        return k

    def best(i, last):
        """-> (bits saved, offset, count, reuse) of the best back-reference at i."""
        hash_to(i)
        limit = min(_MAX_COUNT, n - i)
        out = (0, 0, 0, False)
        if last and last <= i:
            k = run(i, i - last, limit)
            c = _match_cost(last, k, True) if k >= 2 else None
            if c is not None and _LITERAL_BITS * k - c > out[0]:
                out = (_LITERAL_BITS * k - c, last, k, True)
        j, tried = (prev[i] if i + _MIN_MATCH <= n else -1), 0
        while j >= 0 and tried < _CANDIDATES:
            off = i - j
            if off > _MAX_OFFSET:
                break
            k = run(i, j, limit)
            c = _match_cost(off, k, off == last)
            if c is not None and _LITERAL_BITS * k - c > out[0]:
                out = (_LITERAL_BITS * k - c, off, k, off == last)
            j, tried = prev[j], tried + 1
        return out

    out, i, last = [], 0, 0
    while i < n:
        gain, off, count, reuse = best(i, last)
        if gain > 0 and i + 1 < n and best(i + 1, last)[0] > gain + _LITERAL_BITS:
            gain = 0                          # a literal here, and the better match next
        if gain > 0:
            out.append(('R' if reuse else 'M', off, count))
            last = off
            i += count
        else:
            out.append(('L', data[i]))
            i += 1
    return out


def _events_lz(data):
    """_events(), but emitting back-references where they pay."""
    events = []
    for e in _parse(data):
        if e[0] == 'L':
            events.append((1, bytes((e[1],))))
        elif e[0] == 'M':
            events.extend(_match_events(e[1], e[2]))
        else:
            events.extend(_reuse_events(e[1], e[2]))
    events.append((0, b''))
    gbits = _gamma_bits(_TERMINATOR_GAMMA_VALUE)
    last = len(gbits) - 1
    for k, bit in enumerate(gbits):
        events.append((bit, bytes((_TERMINATOR_TRAILER_BYTE,)) if k == last else b''))
    return events


def _events(data):
    """-> [(tag_bit, payload_bytes), ...] in stream order: one event per tag
    bit the depacker will read, paired with whatever raw bytes it reads
    immediately after that bit (empty for bits that consume none)."""
    events = [(1, bytes((b,))) for b in data]
    events.append((0, b''))                      # enter the match/terminator path
    gbits = _gamma_bits(_TERMINATOR_GAMMA_VALUE)
    last = len(gbits) - 1
    for i, bit in enumerate(gbits):
        events.append((bit, bytes((_TERMINATOR_TRAILER_BYTE,)) if i == last else b''))
    return events


def _assemble(events):
    """Pack (bit, payload) events into the tag-byte-interleaved-with-payload
    layout the depacker expects: 8 bits per tag byte, MSB first, with that
    group's payload bytes appended right after it, in event order."""
    out = bytearray()
    for i in range(0, len(events), 8):
        chunk = events[i:i + 8]
        tagbyte = 0
        payload = bytearray()
        for j, (bit, pl) in enumerate(chunk):
            tagbyte |= (bit & 1) << (7 - j)
            payload += pl
        out.append(tagbyte)
        out += payload
    return bytes(out)


def pack(data, compress=True):
    """-> a valid stream for this depacker, WITHOUT the section header.

    `compress=True` (the default) emits back-references; False keeps the
    original store-only behaviour, every byte a literal, which is always
    slightly LARGER than the input and is retained because it is the simplest
    thing that can possibly be correct and is useful when a match-finder bug
    is the thing being ruled out.
    """
    return _assemble(_events_lz(data) if compress else _events(data))


def pack_section(data, compress=True):
    """pack(data), prepended with the section's 8-byte header: big-endian
    [u32 compressed_len][u32 byte_sum], where compressed_len is the packed
    stream's length (not counting this header) and byte_sum is the plain sum,
    mod 2**32, of the packed stream's bytes. Matches the convention
    dt2/container.py and emu/extract.py already use to read sections."""
    body = pack(data, compress)
    return struct.pack('>II', len(body), sum(body) & 0xFFFFFFFF) + body


def _shadow_decode(comp):
    """Independent decoder for pack(compress=False): literals and terminator.

    It does not decode matches. _self_test checks compressed streams with
    elz.py instead; firmware-depacker evidence is in docs/DEVICES.md.
    """
    pos = 0
    tag = 0
    bitcount = 0

    def read_byte():
        nonlocal pos
        b = comp[pos]
        pos += 1
        return b

    def getbit():
        nonlocal tag, bitcount
        if bitcount == 0:
            tag = read_byte()
            bitcount = 8
        bit = (tag >> 7) & 1
        tag = (tag << 1) & 0xFF
        bitcount -= 1
        return bit

    out = bytearray()
    while True:
        if getbit() == 1:
            out.append(read_byte())
            continue
        result = 1
        while True:
            result = (result << 1) | getbit()
            if getbit() == 1:
                break
        if result != _TERMINATOR_GAMMA_VALUE:
            raise NotImplementedError(
                'shadow decoder only understands this module\'s own '
                'terminator, not a real match (gamma result=%d)' % result)
        trailer = read_byte()
        if trailer != _TERMINATOR_TRAILER_BYTE:
            raise NotImplementedError(
                'unexpected terminator trailer byte 0x%02x' % trailer)
        break
    return bytes(out)


def _self_test():
    """Sanity checks runnable without Unicorn or any firmware: pack()'s
    bit-packing is round-tripped through an independently-written decoder
    (_shadow_decode), and pack_section()'s header arithmetic is checked
    directly. Firmware-depacker evidence is separate (docs/DEVICES.md).
    """
    import os

    cases = [b'', b'A', b'AB', b'\x00', b'\xff' * 3, bytes(range(256)),
             os.urandom(500),
             # The shapes a match-finder must not get wrong: a long run, a
             # repeat just past the near/far boundary, overlapping copies,
             # and a match that reaches the very end of the input.
             b'\x00' * 5000,
             b'ab' * 4000,
             (b'x' * 4000) + (b'ab' * 100) + (b'x' * 4000),
             bytes(range(64)) * 80,
             os.urandom(64) * 60,
             b'q' + os.urandom(200) + b'q' * 64]

    # The store-only path, against the independent decoder.
    for data in cases:
        got = _shadow_decode(pack(data, compress=False))
        assert got == data, ('store', data[:16], got[:16])

    # The compressing path, against dt2/elz.py -- the decoder that matches the
    # device byte for byte on every packed section of two real firmwares.
    from .elz import depack_section
    smaller = 0
    for data in cases:
        got = depack_section(pack_section(data, compress=True))
        assert got == data, ('lz', len(data), data[:16], got[:16])
        if data and len(pack(data, compress=True)) < len(data):
            smaller += 1
    assert smaller, 'compression never actually shrank anything'

    # pack_section's header: length excludes the header, sum is over the
    # packed body only, and packing never counts as *shrinking* the input.
    data = b'roundtrip me'
    section = pack_section(data)
    ln, sm = struct.unpack_from('>II', section, 0)
    body = section[8:]
    assert ln == len(body) == len(pack(data))
    assert sm == sum(body) & 0xFFFFFFFF
    # Store-only always grows; the compressing path must not grow this much
    # either, but on twelve bytes there is nothing to find, so only the
    # store-only claim is asserted here.
    assert len(pack_section(data, compress=False)) > len(data) + 8

    print('aplib self_test OK')
