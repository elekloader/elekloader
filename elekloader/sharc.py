# SPDX-License-Identifier: GPL-2.0-or-later
"""The DN2's observed ADI boot-record subset, separate from the Octatrack DSP.

Original implementation from the documented 16-byte header contract and small
synthetic vectors. No external parser code is included. See docs/SHARC-BOOT.md.
"""
from dataclasses import dataclass
import functools
import operator
import re
import struct

from .elemod import ModError, sha

LOAD, FILL, FIRST, INIT, FINAL = 0x0001, 0x0101, 0x5001, 0x0801, 0x8001
KINDS = (LOAD, FILL, FIRST, INIT, FINAL)


def _uint32(value):
    return type(value) is int and 0 <= value <= 0xffffffff


def byte_span(profile, address, size, unit='loader-byte', *, check_protected=True):
    """Canonical loader-byte interval; `size` is bytes in every address view.

    Allocation excludes protected ranges; an existing record may only be mapped.
    """
    if not _uint32(address) or not _uint32(size) or not size:
        raise ModError('SHARC address and nonempty byte count must be uint32')
    if unit == 'loader-byte':
        intervals = [(base, base + hi - lo, base) for lo, hi, base in profile['l1_aliases']]
        intervals += [(lo, hi, lo) for lo, hi in (profile['l2_bytes'], profile['ddr_bytes'])]
    elif unit == 'core-byte':
        intervals = list(profile['l1_aliases'])
        intervals += [(lo, hi, lo) for lo, hi in (profile['l2_bytes'], profile['ddr_bytes'])]
    elif unit == 'pc16':
        intervals = [(lo // 2, hi // 2, base) for lo, hi, base in profile['l1_aliases']]
        intervals += [profile['l2_pc16']]
    else:
        raise ModError('unsupported SHARC address unit: %s' % unit)
    scale = 2 if unit == 'pc16' else 1
    for lo, hi, base in intervals:
        start = base + (address - lo) * scale
        end = start + size
        if lo <= address < hi and end <= base + (hi - lo) * scale and end <= 0x100000000:
            if check_protected and any(start < stop and begin < end
                                       for begin, stop, _reason in profile['protected']):
                raise ModError('SHARC byte span overlaps a protected region')
            return start, end
    raise ModError('SHARC byte span is outside the supported address map')


def suspect_shifts(code):
    """Byte offsets in CODE (loader order) of what look like shifts by an immediate count whose
    sign is not extended (Type 6b: a negative count needs DATAEX 0xf). One halts a real DSP; the
    pinned Selache emits it for `LSHIFT/ASHIFT ... BY -n`. The scan tries every halfword without
    instruction boundaries, so a hit is a lead to check in the listing, not proof."""
    offsets = []
    for offset in range(0, len(code) - 5, 2):
        a, b, c = struct.unpack_from('<3H', code, offset)
        word = (a << 32) | (b << 16) | c
        if word & 0xffc187ff0000 == 0x020000000000 and (word >> 27) & 15 != (15 if word & 0x8000 else 0):
            offsets.append(offset)
    return offsets


def pc16(profile, address):
    """Map a loader byte address to an instruction pointer, without truncation."""
    byte_span(profile, address, 2)
    intervals = [(lo // 2, hi // 2, base) for lo, hi, base in profile['l1_aliases']]
    for lo, hi, base in intervals + [profile['l2_pc16']]:
        if base <= address < base + 2 * (hi - lo) and (address - base) % 2 == 0:
            return lo + (address - base) // 2
    raise ModError('SHARC callback has no aligned PC16 address')


@dataclass(frozen=True)
class Record:
    offset: int
    flags: int
    target: int
    count: int
    argument: int
    raw: bytes


def parse(stream, profile):
    """Read an ordered stream without normalizing headers, loads or overlaps."""
    if len(stream) > profile['decoded_max']:
        raise ModError('SHARC decoded stream exceeds the profile limit')
    records, firsts = [], []
    offset, active = 0, None
    final = False
    while offset < len(stream):
        def fail(message):
            raise ModError('SHARC boot record at +0x%x: %s' % (offset, message))
        if len(stream) - offset < 16:
            fail('truncated header')
        header = stream[offset:offset + 16]
        code, target, count, argument = struct.unpack('<4I', header)
        if code >> 24 != 0xad:
            fail('invalid header signature')
        if functools.reduce(operator.xor, header):
            fail('invalid header checksum')
        flags = code & 0xffff
        if flags not in KINDS:
            fail('unsupported flags 0x%04x' % flags)
        end = offset + 16 + (count if flags == LOAD else 0)
        if end > len(stream):
            fail('truncated payload')
        if flags in (LOAD, FILL):
            if active is None:
                fail('load or fill outside a boot image')
            if not count or target % 4 or count % 4:
                fail('load or fill address/count must be nonempty and four-byte aligned')
            if flags == LOAD and argument:
                fail('load argument must be zero')
            try:
                byte_span(profile, target, count, check_protected=False)
            except ModError as error:
                fail(str(error))
        else:
            if count:
                fail('entry record requires zero count')
            try:
                byte_span(profile, target, 2, 'pc16', check_protected=False)
            except ModError as error:
                fail(str(error))
            if flags == FIRST:
                if active is not None:
                    fail('FIRST before the preceding boot image ends')
                active = target
                firsts.append((offset, argument))
            else:
                if active is None or target != active or argument:
                    fail('entry does not match its FIRST boot image')
                active = None
                if flags == FINAL:
                    final = True
                    if end != len(stream):
                        fail('bytes after FINAL')
        records.append(Record(offset, flags, target, count, argument, bytes(stream[offset:end])))
        offset = end
    if not final:
        raise ModError('SHARC boot stream has no FINAL record')
    for index, (offset, argument) in enumerate(firsts):
        expected = firsts[index + 1][0] if index + 1 < len(firsts) else len(stream)
        if offset + 16 + argument != expected:
            raise ModError('SHARC FIRST at +0x%x: wrong next-boot-image distance' % offset)
    return records


def _header(flags, target, count, argument):
    raw = bytearray(struct.pack('<4I', 0xad000000 | flags, target, count, argument))
    raw[2] = functools.reduce(operator.xor, raw)
    return bytes(raw)


def insert(stream, profile, *, loads=(), fills=(), patches=()):
    """Insert once before FINAL; load tuples are (address, bytes), fills (address, count, word).

    New records use owned ranges; an exact stock instruction may be replaced
    only at a separately qualified patch range.
    """
    records = parse(stream, profile)
    if sha(stream) != profile['decoded_sha256']:
        raise ModError('SHARC stock DSP hash does not match the profile')
    additions, spans = [], []
    for target, data in loads:
        if not isinstance(data, bytes):
            raise ModError('SHARC load payload must be bytes')
        spans.append((target, len(data)))
        additions.append((LOAD, target, len(data), 0, data))
    for target, count, word in fills:
        if not _uint32(word):
            raise ModError('SHARC fill word must be uint32')
        spans.append((target, count))
        additions.append((FILL, target, count, word, b''))
    if not additions and not patches:
        return bytes(stream)
    patched = bytearray(stream)
    for target, old, new in patches:
        if not isinstance(old, bytes) or not isinstance(new, bytes) or not old or len(old) != len(new) \
                or target % 2 or len(old) % 2:
            raise ModError('SHARC patch needs equal nonempty instruction bytes')
        lo, hi = byte_span(profile, target, len(old), check_protected=False)
        qualified = profile.get('data_sites', {}).get(target)
        if qualified != (old.hex(), new.hex()) and not any(
                start <= lo and hi <= end for start, end in profile.get('patch_ranges', ())):
            raise ModError('SHARC patch is outside the profile patch range')
        overlaps = [r for r in records if r.flags in (LOAD, FILL)
                    and r.target < hi and lo < r.target + r.count]
        if len(overlaps) != 1 or overlaps[0].flags != LOAD \
                or not (overlaps[0].target <= lo and hi <= overlaps[0].target + overlaps[0].count):
            raise ModError('SHARC patch needs one unambiguous stock load')
        at = overlaps[0].offset + 16 + lo - overlaps[0].target
        if patched[at:at + len(old)] != old:
            raise ModError('SHARC patch stock instruction does not match')
        patched[at:at + len(old)] = new
    occupied = [byte_span(profile, r.target, r.count, check_protected=False)
                for r in records if r.flags in (LOAD, FILL)]
    for target, count in spans:
        lo, hi = byte_span(profile, target, count)
        if target % 4 or count % 4:
            raise ModError('SHARC new load or fill must be four-byte aligned')
        if not any(start <= lo and hi <= end for start, end in profile['owned_ranges']):
            raise ModError('SHARC new load or fill is outside the profile-owned ranges')
        if any(lo < end and start < hi for start, end in occupied):
            raise ModError('SHARC new load or fill overlaps an existing load or fill')
        occupied.append((lo, hi))
    extra = sum(16 + len(data) for _flags, _target, _count, _word, data in additions)
    if len(stream) + extra > profile['decoded_max']:
        raise ModError('SHARC decoded stream exceeds the profile limit')
    first = next(r for r in reversed(records) if r.flags == FIRST)
    if first.argument + extra > 0xffffffff:
        raise ModError('SHARC FIRST distance overflows uint32')
    inserted = b''.join(_header(flags, target, count, word) + data
                        for flags, target, count, word, data in additions)
    result = b''.join((patched[:first.offset], _header(FIRST, first.target, 0, first.argument + extra),
                       patched[first.offset + 16:records[-1].offset], inserted, records[-1].raw))
    parse(result, profile)
    return result


def module_payload(doc, profile, core=False):
    """Validate the narrow prelinked format-2 payload; offsets are owner bytes."""
    if not profile or not profile.get('abi'):
        raise ModError('SHARC payload has no supported device ABI')
    keys = {'abi', 'segments', 'zero', 'entries', 'stack_bytes'}
    if (set(doc) if isinstance(doc, dict) else None) != keys:
        raise ModError('SHARC payload needs exactly abi, segments, zero, entries, stack_bytes')
    abi = profile['abi'] if core else profile.get('l2_abi')
    if not abi or doc['abi'] != abi:
        raise ModError('SHARC ABI does not match the device profile')
    owner = profile['core_range'] if core else profile['l2_app_range']
    size = owner[1] - owner[0]
    spans, segments, zeros = [], [], []
    if not isinstance(doc['segments'], list) or not doc['segments']:
        raise ModError('SHARC payload needs code segments')
    for item in doc['segments']:
        if not isinstance(item, dict) or set(item) != {'offset_bytes', 'kind', 'hex'}:
            raise ModError('SHARC segment needs exactly offset_bytes, kind, hex')
        off, kind, h = item['offset_bytes'], item['kind'], item['hex']
        if not _uint32(off) or kind not in ('code', 'data') or not isinstance(h, str) or len(h) > size * 2 \
                or not re.fullmatch(r'(?:[0-9a-fA-F]{2})+', h):
            raise ModError('SHARC segment has invalid offset, kind or hex bytes')
        data = bytes.fromhex(h)
        if off % 4 or len(data) % 4 or off + len(data) > size:
            raise ModError('SHARC segment is unaligned or exceeds its owner')
        spans.append((off, off + len(data)))
        segments.append((off, kind, data))
    if not any(kind == 'code' for _off, kind, _data in segments):
        raise ModError('SHARC payload has no code')
    if not isinstance(doc['zero'], list):
        raise ModError('SHARC zero ranges must be an array')
    for item in doc['zero']:
        if not isinstance(item, dict) or set(item) != {'offset_bytes', 'size_bytes'}:
            raise ModError('SHARC zero range needs exactly offset_bytes, size_bytes')
        off, count = item['offset_bytes'], item['size_bytes']
        if not _uint32(off) or not _uint32(count) or not count or off % 4 or count % 4 or off + count > size:
            raise ModError('SHARC zero range is unaligned or exceeds its owner')
        spans.append((off, off + count))
        zeros.append((off, count))
    at = 12 if core else 0             # the core's first 12 bytes are consumer-owned binding
    for lo, hi in sorted(spans):
        if lo != at:
            raise ModError('SHARC payload has a gap or overlap in its owner')
        at = hi
    if at != size:
        raise ModError('SHARC payload has a gap or overlap in its owner')
    entries = doc['entries']
    if entries is not None:
        keys = {'hook_offset_bytes'} if core else {'init_offset_bytes', 'render_offset_bytes'}
        if not isinstance(entries, dict) or set(entries) != keys:
            raise ModError('SHARC entries do not match core/mod role')
        for off in entries.values():
            if not _uint32(off) or off % 2 or not any(start <= off < start + len(data)
                    for start, kind, data in segments if kind == 'code'):
                raise ModError('SHARC entry is not in owned code')
    elif core:
        raise ModError('SHARC core needs a hook entry')
    if entries is not None and not core:
        guard = profile['code_guard_bytes']
        for start, kind, data in segments:
            if kind != 'code':
                continue
            if len(data) < guard or data[-guard:] != b'\x01\x00' * (guard // 2):
                raise ModError('SHARC mod code needs its profiled NOP read-ahead guard')
            if any(start + len(data) - guard <= off < start + len(data) for off in entries.values()):
                raise ModError('SHARC entry is in the instruction read-ahead guard')
    stack = doc['stack_bytes']
    if not _uint32(stack) or stack > profile['stack_max']:
        raise ModError('SHARC stack contribution exceeds the profile limit')
    if entries is None and stack:
        raise ModError('inert SHARC payload must declare zero stack')
    result = {'segments': segments, 'zero': zeros, 'entries': entries, 'stack_bytes': stack,
              'owner': owner}
    return result


# DN2 machine kernels (dn2-kernel-v1): machines-dn2's dispatcher and every mod's kernels, relocatable
# (sdk/sharc_reloc.py), placed anywhere in the profile's pool by place_kernels.
KERNEL_ABI = 'dn2-kernel-v1'
DISPATCH = ('dn2_voice_hook.', 'dn2_filter_hook.', 'dn2_filter_capture.', 'dn2_voice_table.', 'dn2_filter_table.')
# Each kind's ids, as machines-dn2 stores them, and its table: SDK and mk2 synths share the voice table.
KERNEL_IDS = {'sdk': (5, 127), 'mk2-voice': (5, 31), 'mk2-filter': (6, 31)}
KERNEL_TABLE = {'sdk': 'voice', 'mk2-voice': 'voice', 'mk2-filter': 'filter'}
TABLE_IDS = 128
TABLE_ENTRY = struct.Struct('<IIII')     # entry PC16 (0: stock), state base, state bytes a slot, SDK knob map (0: mk2)
KNOBS_MAX = 15                           # ELEK_UI_KNOB_MAX
RAW_MAX = 32512                          # a knob's raw value, 0..127.0 in 8.8
GUARD = b'\x01\x00' * 32                 # 64 bytes of NOP after each code block: the read-ahead guard


def _hex(value, what):
    if not isinstance(value, str) or not re.fullmatch(r'(?:[0-9a-fA-F]{2})*', value):
        raise ModError('%s must be hex bytes' % what)
    return bytes.fromhex(value)


def _int32(value):
    return type(value) is int and -0x80000000 <= value <= 0x7fffffff


def kernel_payload(doc, profile):
    """Validate a relocatable kernel artifact (sdk.sharc.kernel_artifact or machine_artifact)."""
    if not isinstance(doc, dict) or not profile or doc.get('abi') != profile.get('kernel_abi'):
        raise ModError('SHARC kernel: abi must be %s on this device' % KERNEL_ABI)
    keys = {'abi', 'code', 'data', 'code_pc16', 'data_address', 'data_align', 'relocs', 'kernels', 'exports'}
    if set(doc) != keys:
        raise ModError('SHARC kernel needs exactly ' + ', '.join(sorted(keys)))
    code, data = (_hex(doc[k], 'SHARC kernel ' + k) for k in ('code', 'data'))
    if not code or len(code) % 2:
        raise ModError('SHARC kernel: code must be whole 16-bit parcels')
    align = doc['data_align']
    if type(align) is not int or align not in (4, 8, 16, 32, 64, 128) \
            or not _uint32(doc['code_pc16']) or not _uint32(doc['data_address']) or doc['data_address'] % align:
        raise ModError('SHARC kernel: invalid base or data alignment')
    sizes, relocs, taken = {'code': len(code), 'data': len(data)}, [], set()
    for r in doc['relocs'] if isinstance(doc['relocs'], list) else [None]:
        if not (isinstance(r, list) and len(r) == 4 and r[0] in sizes and _uint32(r[1])
                and r[2] in ((3, 4) if r[0] == 'code' else (4,)) and r[3] in ('code', 'data')
                and r[1] + r[2] <= sizes[r[0]]) or any((r[0], r[1] + i) in taken for i in range(r[2])):
            raise ModError('SHARC kernel: invalid relocation %r' % (r,))
        taken.update((r[0], r[1] + i) for i in range(r[2]))
        relocs.append(tuple(r))
    exports = doc['exports']
    if not isinstance(exports, dict) or any(
            n not in DISPATCH or not isinstance(v, list) or len(v) != 2 or v[0] not in sizes
            or not _uint32(v[1]) or v[1] >= sizes[v[0]] for n, v in exports.items()):
        raise ModError('SHARC kernel: exports are the dispatcher\'s labels, %s' % ', '.join(DISPATCH))
    if exports and set(exports) != set(DISPATCH):
        raise ModError('SHARC kernel: a dispatcher defines all of ' + ', '.join(DISPATCH))
    kernels, seen = [], set()
    for k in doc['kernels'] if isinstance(doc['kernels'], list) else [None]:
        if not isinstance(k, dict) or not {'kind', 'id', 'entry'} <= set(k) <= {'kind', 'id', 'entry', 'state_bytes', 'knobs'} \
                or k['kind'] not in KERNEL_IDS or ('knobs' in k) > (k['kind'] == 'sdk') \
                or ('state_bytes' in k) > (k['kind'] != 'sdk'):
            raise ModError('SHARC kernel: a kernel is {kind: sdk, id, entry, knobs} or '
                           '{kind: mk2-voice|mk2-filter, id, entry[, state_bytes]}')
        kind, mid, entry = k['kind'], k['id'], k['entry']
        if type(mid) is not int or not KERNEL_IDS[kind][0] <= mid <= KERNEL_IDS[kind][1]:
            raise ModError('SHARC kernel: %s id %s is outside %d..%d' % (kind, mid, *KERNEL_IDS[kind]))
        if (KERNEL_TABLE[kind], mid) in seen:
            raise ModError('SHARC kernel: two %s kernels with id %d' % (KERNEL_TABLE[kind], mid))
        seen.add((KERNEL_TABLE[kind], mid))
        if not _uint32(entry) or 2 * entry >= len(code):
            raise ModError('SHARC kernel: %s %d entry is outside its code' % (kind, mid))
        out = {'kind': kind, 'id': mid, 'entry': entry}
        if kind == 'sdk':
            knobs = k.get('knobs')
            if not isinstance(knobs, list) or len(knobs) > KNOBS_MAX or any(
                    not isinstance(r, list) or len(r) != 2 or not all(map(_int32, r)) or r[0] >= r[1] for r in knobs):
                raise ModError('SHARC kernel: sdk %d knobs are up to %d [min, max] pairs of int32, min below max'
                               % (mid, KNOBS_MAX))
            out['knobs'] = [tuple(r) for r in knobs]
        else:
            state = k.get('state_bytes', 0)
            if not _uint32(state) or state % 4:
                raise ModError('SHARC kernel: %s %d state_bytes must be whole 32-bit words' % (kind, mid))
            out['state_bytes'] = state
        kernels.append(out)
    if not kernels and not exports:
        raise ModError('SHARC kernel: no kernel and no dispatcher')
    return {'abi': KERNEL_ABI, 'code': code, 'data': data, 'code_pc16': doc['code_pc16'],
            'data_address': doc['data_address'], 'data_align': align, 'relocs': relocs,
            'kernels': kernels, 'exports': {n: tuple(v) for n, v in exports.items()}}


def rebase(artifact, code_pc16, data_address):
    """The kernel's (code, data) moved to PC16 code_pc16 and byte data_address: each relocation is a
    code or data address field (big-endian in code, as the executable stores it; little-endian in
    data) that moves with its kind. Raises ModError."""
    if data_address % artifact['data_align']:
        raise ModError('SHARC kernel: data base 0x%x is not %d-byte aligned' % (data_address, artifact['data_align']))
    out = {'code': bytearray(artifact['code']), 'data': bytearray(artifact['data'])}
    delta = {'code': code_pc16 - artifact['code_pc16'], 'data': data_address - artifact['data_address']}
    for section, at, width, kind in artifact['relocs']:
        order = 'big' if section == 'code' else 'little'
        value = (int.from_bytes(out[section][at:at + width], order) + delta[kind]) % (1 << 8 * width)
        out[section][at:at + width] = value.to_bytes(width, order)
    return bytes(out['code']), bytes(out['data'])


def _guard(code):
    """The NOPs after a code block: the guard, plus one parcel when the code ends mid-word."""
    return GUARD + b'\x01\x00' * (len(code) // 2 % 2)


def _jump(pc):
    """A 48-bit absolute JUMP to PC16 pc, in loader order (each 16-bit parcel swapped)."""
    return _parcels(b'\x06\x3e\x00' + pc.to_bytes(3, 'big'))


def _parcels(code):
    """The executable's byte order and the loader's differ by each 16-bit parcel's two bytes."""
    return b''.join(code[i:i + 2][::-1] for i in range(0, len(code), 2))


def knob_map(knobs):
    """An SDK machine's knob map for the dispatcher: the count, then per knob min, span / RAW_MAX and
    span % RAW_MAX, so that min + q * raw + (r * raw + RAW_MAX / 2) / RAW_MAX is sdk_alpha_map."""
    words = [len(knobs)]
    for lo, hi in knobs:
        span = (hi - lo) % (1 << 32)
        words += [lo % (1 << 32), span // RAW_MAX, span % RAW_MAX]
    return struct.pack('<%dI' % len(words), *words)


def place_kernels(payloads, profile):
    """Lay the dispatcher (the payload with exports) and every kernel out in the profile's pool:
    -> (loads, fills, patches, layout). Each block of code takes its guard; data keeps its alignment;
    each kernel gets 16 slots of state, an SDK machine its knob map. The dispatcher's tables name each
    kernel by id. Raises ModError when the pool is too small."""
    lo, hi = profile['pool']
    dispatcher = next(p for _id, p in payloads if p['exports'])
    order = [(m, p) for m, p in payloads if p is dispatcher] + [(m, p) for m, p in payloads if p is not dispatcher]
    at, placed, loads, fills, layout = lo, {}, [], [], []

    def take(size, align=4):
        nonlocal at
        start = -(-at // align) * align
        at = start + size
        return start
    for mid, p in order:
        code_at = take(len(p['code']) + len(_guard(p['code'])))
        data_at = take(len(p['data']), p['data_align'])
        placed[mid] = (pc16(profile, code_at), data_at)
        layout.append({'id': mid, 'code': [code_at, len(p['code']) + len(_guard(p['code']))],
                       'data': [data_at, len(p['data'])]})
    tables = {'voice': bytearray(TABLE_IDS * TABLE_ENTRY.size), 'filter': bytearray(TABLE_IDS * TABLE_ENTRY.size)}
    for mid, p in order:
        for k in p['kernels']:
            knobs = knob_map(k['knobs']) if k['kind'] == 'sdk' else b''
            knob_at = take(len(knobs)) if knobs else 0
            if knobs:
                loads.append((knob_at, knobs))
            each = profile['kernel_state_bytes'] if k['kind'] == 'sdk' else k['state_bytes']
            state = take(16 * each, 8) if each else 0
            if each:
                fills.append((state, 16 * each, 0))
            TABLE_ENTRY.pack_into(tables[KERNEL_TABLE[k['kind']]], TABLE_ENTRY.size * k['id'],
                                  placed[mid][0] + k['entry'], state, each, knob_at)
    if at > hi:
        raise ModError('DN2 kernels need %d bytes; the machine pool has %d' % (at - lo, hi - lo))
    for mid, p in order:
        code, data = rebase(p, *placed[mid])
        if p is dispatcher:
            data = bytearray(data)
            for kind in tables:
                section, off = p['exports']['dn2_%s_table.' % kind]
                if section != 'data' or off + len(tables[kind]) > len(data):
                    raise ModError('SHARC dispatcher: dn2_%s_table. is not %d bytes of data' % (kind, len(tables[kind])))
                data[off:off + len(tables[kind])] = tables[kind]
            data = bytes(data)
        code_at = next(e['code'][0] for e in layout if e['id'] == mid)
        loads.append((code_at, _parcels(code) + _guard(code)))
        if data:
            loads.append((placed[mid][1], data + bytes(-len(data) % 4)))
    hook = {n: placed[order[0][0]][0] + dispatcher['exports'][n][1]
            for n in ('dn2_voice_hook.', 'dn2_filter_hook.', 'dn2_filter_capture.')}
    patches = []
    for key, label in (('voice_hook', 'dn2_voice_hook.'), ('filter_hook', 'dn2_filter_hook.')):
        site, old = profile[key][:2]
        patches.append((site, bytes.fromhex(old), _jump(hook[label])))
    site, old = profile['filter_capture'][:2]
    patches.append((site, bytes.fromhex(old), struct.pack('<I', hook['dn2_filter_capture.'])))
    for key in ('voice_stack', 'filter_stack'):       # each kind's L1 stack: the guard word, then zeros
        s_lo, s_hi = profile[key]
        loads.append((s_lo, struct.pack('<I', profile['stack_guard'])))
        fills.append((s_lo + 4, s_hi - s_lo - 4, 0))
    return loads, fills, patches, {'pool': [lo, hi], 'used': at - lo, 'mods': layout}


RAW_KEYS = {'loads', 'patches', 'objects', 'tables', 'contribute'}
RELOC_TYPES = ('sharc.abs24', 'sharc.abs32', 'hi16', 'lo16', 'le32')


def _parts(parts, what):
    """Load parts: ["hex", "..."] or ["stock", "0x...", n] -> ([bytes | (addr, n)], length)."""
    data, n = [], 0
    for part in parts if isinstance(parts, list) else [None]:
        if isinstance(part, list) and len(part) == 2 and part[0] == 'hex' and isinstance(part[1], str) \
                and re.fullmatch(r'(?:[0-9a-fA-F]{2})*', part[1]):
            data.append(bytes.fromhex(part[1]))
            n += len(data[-1])
        elif isinstance(part, list) and len(part) == 3 and part[0] == 'stock' \
                and isinstance(part[2], int) and part[2] > 0:
            # bytes of the stock DSP stream's loads, read from the user's file at link time
            data.append((int(part[1], 16), part[2]))
            n += part[2]
        else:
            raise ModError('%s: a part must be ["hex", "..."] or ["stock", "0x...", n]' % what)
    return data, n


def _relocs(rel, size, what):
    out = []
    for r in rel or []:
        if not (isinstance(r, list) and len(r) == 4 and isinstance(r[0], int) and r[1] in RELOC_TYPES
                and isinstance(r[2], str) and r[2].startswith('sym:') and isinstance(r[3], int)):
            raise ModError('%s: a relocation is [offset, type, "sym:NAME", addend], type one of %s'
                           % (what, ', '.join(RELOC_TYPES)))
        width = {'sharc.abs24': 6, 'sharc.abs32': 6, 'hi16': 2, 'lo16': 2, 'le32': 4}[r[1]]
        if r[0] < 0 or r[0] + width > size:
            raise ModError('%s: a relocation at +%d runs past its bytes' % (what, r[0]))
        out.append((r[0], r[1], r[2][4:], r[3]))
    return out


def _object(doc, what):
    """A relocatable DSP object (digikit's .dko.json layout): sections, symbols, relocs."""
    if not isinstance(doc, dict) or not {'name', 'sections', 'symbols', 'relocs'} <= set(doc):
        raise ModError('%s: an object has name, sections, symbols and relocs' % what)
    secs = []
    for sc in doc['sections']:
        kind, align = sc.get('kind'), sc.get('align', 4)
        if kind not in ('text', 'data', 'bss') or align not in (1, 2, 4, 8, 16, 32, 64) or not sc.get('name'):
            raise ModError('%s: section %r needs a name, a kind (text, data, bss) and an align'
                           % (what, sc.get('name')))
        if kind == 'bss':
            data, size = b'', int(sc['size'])
        else:
            data = bytes.fromhex(sc['hex'])
            size = len(data)
            if kind == 'text' and size % 2:
                raise ModError('%s: text section %s is not whole parcels' % (what, sc['name']))
        secs.append({'name': sc['name'], 'kind': kind, 'align': max(8 if kind == 'text' else 4, align),
                     'data': data, 'size': size})
    secs.sort(key=lambda x: ('text', 'data', 'bss').index(x['kind']))   # stable: digikit's layout
    names = {sc['name'] for sc in secs}
    syms = {}
    for sy in doc['symbols']:
        sec = sy.get('section')
        if sec is None and 'abs' not in sy:
            continue                            # an import
        if sec is not None and sec not in names:
            raise ModError('%s: symbol %s in unknown section %s' % (what, sy.get('name'), sec))
        syms[sy['name']] = (sec, int(sy['abs'] if sec is None else sy['value']), bool(sy.get('global')))
    rels = []
    for r in doc['relocs']:
        sc = next((x for x in secs if x['name'] == r.get('section')), None)
        if sc is None or sc['kind'] != 'text' or r.get('type') not in ('sharc.abs24', 'sharc.abs32'):
            raise ModError('%s: a relocation needs a text section and type sharc.abs24 or sharc.abs32' % what)
        if r['offset'] * 2 + 6 > sc['size']:
            raise ModError('%s: a relocation at parcel %d runs past %s' % (what, r['offset'], sc['name']))
        rels.append((sc['name'], int(r['offset']) * 2, r['type'], r['symbol'], int(r.get('addend', 0))))
    return {'name': doc['name'], 'sections': secs, 'symbols': syms, 'relocs': rels}


def raw_payload(doc, profile):
    """A raw SHARC payload (`sharc_raw`): loads at fixed addresses in owned L2, relocatable objects the
    linker places there, tables mods add rows to, and whole-instruction patches of stock L1 code (their
    bytes may name symbols). Checked against the profile; the stream is checked again by insert()."""
    if not profile or not profile.get('raw'):
        raise ModError('this device takes no raw SHARC payload')
    if not isinstance(doc, dict) or not set(doc) <= RAW_KEYS:
        raise ModError('sharc_raw has only %s' % ', '.join(sorted(RAW_KEYS)))
    loads, patches, objects, tables, contrib = [], [], [], {}, []
    for item in doc.get('loads', []):
        if not isinstance(item, dict) or set(item) not in ({'addr', 'hex'}, {'addr', 'parts'}):
            raise ModError('sharc_raw load needs exactly addr and hex or parts')
        addr = int(item['addr'], 16)
        data, n = _parts([['hex', item['hex']]] if 'hex' in item else item['parts'], 'sharc_raw load')
        lo, hi = byte_span(profile, addr, n)
        if addr % 4 or n % 4 or not any(s <= lo and hi <= e for s, e in profile['owned_ranges']):
            raise ModError('sharc_raw load 0x%x is unaligned or outside the owned L2' % addr)
        loads.append((addr, data[0] if len(data) == 1 and isinstance(data[0], bytes) else data))
    for item in doc.get('patches', []):
        if not isinstance(item, dict) or not {'addr', 'stock', 'new'} <= set(item) <= {'addr', 'stock', 'new', 'relocs'}:
            raise ModError('sharc_raw patch needs addr, stock, new (and relocs)')
        addr = int(item['addr'], 16)
        old, new = bytes.fromhex(item['stock']), bytes.fromhex(item['new'])
        if not old or len(old) != len(new) or addr % 2 or len(old) % 2:
            raise ModError('sharc_raw patch 0x%x needs equal, even, nonempty stock and new' % addr)
        lo, hi = byte_span(profile, addr, len(old), check_protected=False)
        if not any(s <= lo and hi <= e for s, e in profile['patch_ranges']):
            raise ModError('sharc_raw patch 0x%x is outside the profile patch range' % addr)
        patches.append((addr, old, new, _relocs(item.get('relocs'), len(new), 'sharc_raw patch 0x%x' % addr)))
    for i, ob in enumerate(doc.get('objects', [])):
        objects.append(_object(ob, 'sharc_raw object %d' % i))
    for name, t in doc.get('tables', {}).items():
        if not isinstance(t, dict) or not isinstance(t.get('row'), int) or t['row'] <= 0 or t['row'] % 4 \
                or t.get('kind') not in ('list', 'indexed'):
            raise ModError('sharc_raw table %s: {"kind": "list" or "indexed", "row": bytes (a multiple of 4)}'
                           % name)
        if t['kind'] == 'list':
            end, _n = _parts([['hex', t.get('end', '')]], 'table %s end' % name)
            if not isinstance(t.get('max'), int) or t['max'] <= 0:
                raise ModError('sharc_raw table %s: a list table needs max (rows)' % name)
            tables[name] = {'kind': 'list', 'row': t['row'], 'max': t['max'], 'end': end[0]}
        else:
            if not isinstance(t.get('rows'), int) or t['rows'] <= 0:
                raise ModError('sharc_raw table %s: an indexed table needs rows' % name)
            dflt, n = _parts(t.get('default', [['hex', '00' * t['row']]]), 'table %s default' % name)
            if n != t['row']:
                raise ModError('sharc_raw table %s: its default row is not %d bytes' % (name, t['row']))
            tables[name] = {'kind': 'indexed', 'row': t['row'], 'rows': t['rows'], 'default': dflt}
    for c in doc.get('contribute', []):
        if not isinstance(c, dict) or not isinstance(c.get('to'), str):
            raise ModError('sharc_raw contribute: {"to", "parts", "relocs", "order" or "index"}')
        data, n = _parts(c.get('parts', []), 'sharc_raw contribute to %s' % c['to'])
        contrib.append({'to': c['to'], 'order': int(c.get('order', 50)), 'index': c.get('index'),
                        'data': data, 'len': n,
                        'relocs': _relocs(c.get('relocs'), n, 'sharc_raw contribute to %s' % c['to'])})
    if not (loads or patches or objects or tables or contrib):
        raise ModError('sharc_raw is empty')
    return {'loads': loads, 'patches': patches, 'objects': objects, 'tables': tables, 'contribute': contrib}


def _put(buf, at, kind, v):
    """Write value `v` at byte `at` of `buf` by relocation kind (digikit's encodings)."""
    if kind == 'sharc.abs24':
        if not 0 <= v < 1 << 24:
            raise ModError('SHARC relocation: 0x%x does not fit 24 bits' % v)
        buf[at + 2] = (v >> 16) & 0xff
        buf[at + 4:at + 6] = (v & 0xffff).to_bytes(2, 'little')
    elif kind == 'sharc.abs32':
        v &= 0xffffffff
        buf[at + 2:at + 4] = (v >> 16).to_bytes(2, 'little')
        buf[at + 4:at + 6] = (v & 0xffff).to_bytes(2, 'little')
    elif kind == 'hi16':
        buf[at:at + 2] = ((v >> 16) & 0xffff).to_bytes(2, 'little')
    elif kind == 'lo16':
        buf[at:at + 2] = (v & 0xffff).to_bytes(2, 'little')
    else:                                      # le32: a data word
        buf[at:at + 4] = (v & 0xffffffff).to_bytes(4, 'little')


def l2_pc(profile, addr):
    """An L2 byte address -> the short-word PC of the instruction there."""
    lo, hi, base = profile['l2_pc16']
    pc = lo + (addr - base) // 2
    if addr % 2 or not lo <= pc < hi:
        raise ModError('SHARC code at 0x%x has no L2 short-word PC' % addr)
    return pc


def link_raw(payloads, stream, profile):
    """Link the raw payloads of a build, in order (core first): the fixed loads stay where they are;
    each object is placed, in order, at the lowest free address of the owned L2 (its bss reserved, not
    loaded), then each table; symbols resolve across payloads (an object's text symbols are short-word
    PCs, its data symbols and the tables byte addresses). -> (loads, patches, symbols)."""
    loads = [(a, resolve_load(stream, profile, d)) for _id, p in payloads for a, d in p['loads']]
    stock = [byte_span(profile, r.target, r.count, check_protected=False)
             for r in parse(stream, profile) if r.flags in (LOAD, FILL)]
    taken = sorted([(a, a + len(d)) for a, d in loads] + stock)
    (own_lo, own_hi), = profile['owned_ranges']

    def place(size, align):
        at = (own_lo + align - 1) // align * align
        for lo, hi in taken:
            if at + size <= lo:
                break
            if hi > at:
                at = (hi + align - 1) // align * align
        if at + size > own_hi:
            raise ModError('the raw SHARC objects and tables need L2 to 0x%x, past 0x%x' % (at + size, own_hi))
        taken.append((at, at + size))
        taken.sort()
        return at

    symbols, owner, placed = {}, {}, []

    def define(mid, name, v):
        if name in symbols and owner[name] != mid:
            raise ModError('the SHARC symbol %s is defined by %s and %s' % (name, owner[name], mid))
        symbols[name], owner[name] = v, mid

    for mid, p in payloads:
        for ob in p['objects']:
            align = max(sc['align'] for sc in ob['sections'])
            at, size, base = 0, 0, {}
            for sc in ob['sections']:          # text and data first, then bss (as digikit lays them out)
                if sc['kind'] != 'bss':
                    at = (at + sc['align'] - 1) // sc['align'] * sc['align']
                    base[sc['name']] = at
                    at += sc['size']
            loaded = (at + 3) // 4 * 4
            at = loaded
            for sc in ob['sections']:
                if sc['kind'] == 'bss':
                    at = (at + sc['align'] - 1) // sc['align'] * sc['align']
                    base[sc['name']] = at
                    at += sc['size']
            addr = place(max(at, 4), align)
            placed.append((mid, ob, addr, base, loaded))
            for nm, (sec, v, glob) in ob['symbols'].items():
                if sec is None:
                    val = v
                else:
                    kind = next(sc['kind'] for sc in ob['sections'] if sc['name'] == sec)
                    val = l2_pc(profile, addr + base[sec]) + v if kind == 'text' else addr + base[sec] + v
                if glob:
                    define(mid, nm, val)
                ob.setdefault('_local', {})[nm] = val
    tabs = {}
    for mid, p in payloads:
        for name, t in p['tables'].items():
            if name in tabs:
                raise ModError('the SHARC table %s is declared by %s and %s' % (name, tabs[name][0], mid))
            n = t['max'] + 1 if t['kind'] == 'list' else t['rows']
            size = t['row'] * n if t['kind'] == 'indexed' else t['row'] * t['max'] + len(t['end'])
            tabs[name] = (mid, t, place((size + 3) // 4 * 4, 4))
            define(mid, name, tabs[name][2])

    def value(mid, name, local=None):
        if local and name in local:
            return local[name]
        if name not in symbols:
            raise ModError('%s: the SHARC symbol %s is defined by no mod in the build' % (mid, name))
        return symbols[name]

    out = list(loads)
    for mid, ob, addr, base, loaded in placed:
        img = bytearray(loaded)
        for sc in ob['sections']:
            if sc['kind'] != 'bss':
                img[base[sc['name']]:base[sc['name']] + sc['size']] = sc['data']
        for sec, off, kind, sym, add in ob['relocs']:
            _put(img, base[sec] + off, kind, value(mid, sym, ob['_local']) + add)
        if loaded:
            out.append((addr, bytes(img)))
    rows = {name: [] for name in tabs}
    for mid, p in payloads:
        for c in p['contribute']:
            if c['to'] not in tabs:
                raise ModError('%s adds to the SHARC table %s, which no mod declares' % (mid, c['to']))
            t = tabs[c['to']][1]
            if c['len'] != t['row']:
                raise ModError('%s: a row of the SHARC table %s is not %d bytes' % (mid, c['to'], t['row']))
            row = bytearray(resolve_load(stream, profile, c['data']))
            for off, kind, sym, add in c['relocs']:
                _put(row, off, kind, value(mid, sym) + add)
            rows[c['to']].append((c['order'], mid, c['index'], bytes(row)))
    for name, (mid, t, addr) in tabs.items():
        if t['kind'] == 'list':
            rs = sorted(rows[name], key=lambda r: r[:2])
            if len(rs) > t['max']:
                raise ModError('the SHARC table %s takes %d rows, not %d' % (name, t['max'], len(rs)))
            data = b''.join(r[3] for r in rs) + t['end']
        else:
            body = [resolve_load(stream, profile, t['default'])] * t['rows']
            by = {}
            for _o, m, i, r in rows[name]:
                if not isinstance(i, int) or not 0 <= i < t['rows']:
                    raise ModError('%s: a row of the SHARC table %s needs an index 0-%d' % (m, name, t['rows'] - 1))
                if i in by:
                    raise ModError('%s and %s both give row %d of the SHARC table %s' % (by[i], m, i, name))
                by[i], body[i] = m, r
            data = b''.join(body)
        out.append((addr, data + bytes(-len(data) % 4)))
    patches = []
    for mid, p in payloads:
        for a, old, new, rel in p['patches']:
            new = bytearray(new)
            for off, kind, sym, add in rel:
                _put(new, off, kind, value(mid, sym) + add)
            patches.append((a, old, bytes(new)))
    return out, patches, symbols


def stock_bytes(stream, profile, addr, n):
    """-> `n` bytes the stock DSP stream loads at loader address `addr` (one stock load holds them)."""
    for r in parse(stream, profile):
        if r.flags == LOAD and r.target <= addr and addr + n <= r.target + r.count:
            return r.raw[16 + addr - r.target:16 + addr - r.target + n]
    raise ModError('sharc_raw stock part 0x%x+%d is not inside one stock load' % (addr, n))


def resolve_load(stream, profile, data):
    """A raw load's data: bytes, or its parts with each stock part read from `stream`."""
    if isinstance(data, bytes):
        return data
    return b''.join(p if isinstance(p, bytes) else stock_bytes(stream, profile, p[0], p[1]) for p in data)
