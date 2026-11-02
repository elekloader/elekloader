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
    machine_kind = next((k for k in ('voice', 'filter') if not core and isinstance(doc, dict)
                 and profile.get(k + '_abi') and doc.get('abi') == profile[k + '_abi']), None)
    keys = {'abi', 'segments', 'zero', 'entries', 'stack_bytes'}
    if machine_kind:
        keys.add('machine_id')
    given = set(doc) - {'voice_state'} if isinstance(doc, dict) and machine_kind else set(doc) if isinstance(doc, dict) else None
    if given != keys:
        if machine_kind:
            raise ModError('SHARC %s payload needs exactly abi, segments, zero, entries, stack_bytes, machine_id' % machine_kind)
        raise ModError('SHARC payload needs exactly abi, segments, zero, entries, stack_bytes')
    abi = profile['abi'] if core else profile[machine_kind + '_abi'] if machine_kind else profile.get('l2_abi')
    if not abi or doc['abi'] != abi:
        raise ModError('SHARC ABI does not match the device profile')
    owner = profile['core_range'] if core else profile[machine_kind + '_range'] if machine_kind else profile['l2_app_range']
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
        keys = {'hook_offset_bytes'} if core else {'render_offset_bytes'} if machine_kind else {'init_offset_bytes', 'render_offset_bytes'}
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
    if machine_kind:
        machine = doc['machine_id']
        prefix = profile[machine_kind + '_adapter_bytes']
        state = size - profile[machine_kind + '_state_bytes']
        first = 5 if machine_kind == 'voice' else 6
        if not _uint32(machine) or not first <= machine < 32:
            raise ModError('SHARC %s machine_id must be %d..31' % (machine_kind, first))
        if entries is None:
            raise ModError('SHARC %s payload needs a render entry' % machine_kind)
        if segments[0][:2] != (0, 'code') or len(segments[0][2]) != prefix \
                or sha(segments[0][2]) != profile[machine_kind + '_adapter_sha256']:
            raise ModError('SHARC %s adapter does not match the qualified prefix' % machine_kind)
        if (state, profile[machine_kind + '_state_bytes']) not in zeros \
                or any(off < prefix or off + len(data) > state for off, _kind, data in segments[1:]) \
                or any(off < prefix or off + count > state for off, count in zeros if off != state) \
                or entries['render_offset_bytes'] < prefix:
            raise ModError('SHARC %s mod overlaps the adapter reservation' % machine_kind)
        voice = doc.get('voice_state')
        if 'voice_state' in doc:
            if not isinstance(voice, dict) or set(voice) != {'offset_bytes', 'voice_bytes'}:
                raise ModError('SHARC %s voice_state needs exactly offset_bytes, voice_bytes' % machine_kind)
            off, each = voice['offset_bytes'], voice['voice_bytes']
            if not _uint32(off) or not _uint32(each) or not each or off % 8 or each % 4 \
                    or not any(z <= off and off + 16 * each <= z + n for z, n in zeros if z != state):
                raise ModError('SHARC %s voice_state must be 16 zeroed voices inside the mod' % machine_kind)
    stack = doc['stack_bytes']
    if machine_kind and (not _uint32(stack) or stack > profile[machine_kind + '_stack_max']):
        raise ModError('SHARC %s stack exceeds the profile limit' % machine_kind)
    if not machine_kind and (not _uint32(stack) or stack > profile['stack_max']):   # machines run on their own stack
        raise ModError('SHARC stack contribution exceeds the profile limit')
    if entries is None and stack:
        raise ModError('inert SHARC payload must declare zero stack')
    result = {'segments': segments, 'zero': zeros, 'entries': entries, 'stack_bytes': stack,
              'owner': owner}
    if machine_kind:
        result['machine_id'] = doc['machine_id']
        result['machine_kind'] = machine_kind
        voice = doc.get('voice_state')
        result['voice_state'] = (voice['offset_bytes'], voice['voice_bytes']) if voice else None
    return result
