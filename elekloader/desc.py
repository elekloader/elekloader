# SPDX-License-Identifier: GPL-2.0-or-later
"""The build descriptor, `core_desc` (docs/FORMAT.md, "The build descriptor").

A mod that imports `core_desc` (the +Drive mods loader) makes the linker write one binary block
into the build's RAM image, after the tables: what the flashed build is, for code that links more
mods into it on the unit and for the host's build-checker. It holds the modules (core first), their
exports, every table with its room for more entries and the sort key of each entry it has, the
names the modules claim, the RAM line and the mod storage's room. With it, every table that may
take more entries gets spare zero entries after its zero entry.

All words big-endian, records 4-byte aligned, offsets from the block's start, names as offsets
into its string pool (UTF-8, NUL-terminated):

  +0x00 'ELKD'; +0x04 u16 ABI major, u16 minor; +0x08 u32 size; +0x0c u32 CRC-32 of the block
  with this word zeroed; +0x10 u16 data family, u16 data device id, u32 OS release (the data
  container's, 0 where the profile does not know them); +0x18 u32 string pool offset, u32 its
  size; +0x20 u16 module, export, table and claim counts; +0x28 u32 offsets of the module, export,
  table and claim arrays, the key arrays, the area record and the storage record; +0x44 u32 the
  device key (a name), +0x48 u32 the OS version (a name); zero to +0x60.

  module (16): name, version, u8 kind (0 core, 1 flashed mod, 2 a mod that imports core_desc),
    u8 flags (bit 0: has sites), u16 index, u16 first export, u16 export count
  export (8): name, u32 address. Grouped by module, in module order; by name within a module.
  table (24): name, u32 address, u16 entry size, u16 count, u16 capacity, u16 flags (bit 0: more
    entries may be added, bit 1: a render table, bit 2: a DSP table, its address in words),
    u32 offset of its keys, u32 0
  keys: one u32 per entry, in table order: order << 16 | module index; the entries the linker
    makes itself (storage, payloads, .fast copies) have order 0xFFFF
  claim (8): u16 kind (1: a resource name), u16 module index, u32 name
  area (20): the flashed build's RAM end, the +Drive mods' area (lo, hi), the staging buffer's
    address and size (a mod's exports drive_staging, drive_staging_size; 0 without)
  storage (12 + 16 a scope): u32 address and u32 length of the lockfile's trailer in the main OS
    image (the host's: nothing on the unit reads it), u16 scope count, u16 0, then per scope: name,
    u32 room in bits, u32 bits used, u32 generation
"""
import struct
import sys
import zlib

from .elemod import ModError

SYM = 'core_desc'
MAGIC = b'ELKD'
ABI = (1, 0)
HEADER = 0x60
SPARE = 16                       # a table's spare entries, unless its collection says
F_MORE, F_RENDER, F_DSP = 1, 2, 4
LINKER_ORDER = 0xFFFF            # the key's order of an entry the linker makes itself
KIND_CORE, KIND_FLASHED, KIND_LOADER = 0, 1, 2
CLAIM_NAME = 1
STAGING = ('drive_staging', 'drive_staging_size')
MODULE, EXPORT, TABLE, CLAIM, AREA, SCOPE = 16, 8, 24, 8, 20, 16


class _Pool:
    def __init__(self):
        self.raw, self.at = bytearray(), {}

    def __call__(self, s):
        s = str(s)
        if s not in self.at:
            self.at[s] = len(self.raw)
            self.raw += s.encode('utf-8') + b'\0'
        return self.at[s]


def encode(d):
    """A descriptor's fields (link.py gathers them) -> the block's bytes. Its size depends only on
    the counts and the names, so the linker can call it with addresses still 0 to size it."""
    pool = _Pool()
    pool(d['device_key'])
    pool(d['os'])
    mods, tables, claims = d['modules'], d['tables'], d['claims']
    exports = [x for m in mods for x in m['exports']]
    nkeys = sum(len(t['keys']) for t in tables)
    scopes = d['storage']['scopes']
    o_mod = HEADER
    o_exp = o_mod + MODULE * len(mods)
    o_tab = o_exp + EXPORT * len(exports)
    o_clm = o_tab + TABLE * len(tables)
    o_key = o_clm + CLAIM * len(claims)
    o_area = o_key + 4 * nkeys
    o_sto = o_area + AREA
    o_pool = o_sto + 12 + SCOPE * len(scopes)
    body = bytearray()
    first = 0
    for i, m in enumerate(mods):
        body += struct.pack('>IIBBHHH', pool(m['name']), pool(m['version']), m['kind'], m['flags'],
                            i, first, len(m['exports']))
        first += len(m['exports'])
    for nm, a in exports:
        body += struct.pack('>II', pool(nm), a & 0xFFFFFFFF)
    k = o_key
    for t in tables:
        body += struct.pack('>IIHHHHII', pool(t['name']), t['address'] & 0xFFFFFFFF, t['entry'],
                            t['count'], t['capacity'], t['flags'], k, 0)
        k += 4 * len(t['keys'])
    for kind, mi, name in claims:
        body += struct.pack('>HHI', kind, mi, pool(name))
    for t in tables:
        body += b''.join(struct.pack('>I', x) for x in t['keys'])
    body += struct.pack('>5I', *(v & 0xFFFFFFFF for v in d['area']))
    st = d['storage']
    body += struct.pack('>IIHH', st['trailer'][0] & 0xFFFFFFFF, st['trailer'][1], len(scopes), 0)
    for nm, room, used, gen in scopes:
        body += struct.pack('>IIII', pool(nm), room, used, gen)
    assert HEADER + len(body) == o_pool
    raw = bytes(pool.raw) + bytes(-len(pool.raw) % 4)
    size = o_pool + len(raw)
    head = struct.pack('>4sHHII HHI II HHHH 7I II', MAGIC, ABI[0], ABI[1], size, 0,
                       d['family'], d['device'], d['release'], o_pool, len(pool.raw),
                       len(mods), len(exports), len(tables), len(claims),
                       o_mod, o_exp, o_tab, o_clm, o_key, o_area, o_sto,
                       pool(d['device_key']), pool(d['os']))
    blk = bytearray(head + bytes(HEADER - len(head)) + body + raw)
    struct.pack_into('>I', blk, 0x0c, zlib.crc32(bytes(blk)) & 0xFFFFFFFF)
    return bytes(blk)


# ---- reading it back -----------------------------------------------------------------------

def decode(blk, at=0):
    """The block at `at` of `blk` -> its fields, as a dict. Raises ModError on a bad block."""
    blk = bytes(blk)
    if blk[at:at + 4] != MAGIC:
        raise ModError('no build descriptor at +0x%x' % at)
    major, minor, size, crc = struct.unpack_from('>HHII', blk, at + 4)
    if major != ABI[0]:
        raise ModError('build descriptor ABI %d, this reader reads %d' % (major, ABI[0]))
    b = blk[at:at + size]
    if len(b) != size or size < HEADER:
        raise ModError('the build descriptor is cut short')
    if zlib.crc32(b[:0x0c] + bytes(4) + b[0x10:]) & 0xFFFFFFFF != crc:
        raise ModError('the build descriptor\'s CRC does not match')

    def name(off):
        o = p_at + off
        return b[o:b.index(b'\0', o)].decode('utf-8')
    family, device, release, p_at, p_len = struct.unpack_from('>HHIII', b, 0x10)
    nmod, nexp, ntab, nclm = struct.unpack_from('>4H', b, 0x20)
    o_mod, o_exp, o_tab, o_clm, _o_key, o_area, o_sto = struct.unpack_from('>7I', b, 0x28)
    dkey, os_ = struct.unpack_from('>II', b, 0x44)
    exports = [(name(n), a) for n, a in struct.iter_unpack('>II', b[o_exp:o_exp + EXPORT * nexp])]
    mods = []
    for i in range(nmod):
        n, v, kind, flags, idx, first, cnt = struct.unpack_from('>IIBBHHH', b, o_mod + MODULE * i)
        mods.append({'name': name(n), 'version': name(v), 'kind': kind, 'flags': flags, 'index': idx,
                     'exports': exports[first:first + cnt]})
    tables = []
    for i in range(ntab):
        n, a, e, cnt, cap, flags, ko, _r = struct.unpack_from('>IIHHHHII', b, o_tab + TABLE * i)
        tables.append({'name': name(n), 'address': a, 'entry': e, 'count': cnt, 'capacity': cap,
                       'flags': flags, 'keys': list(struct.unpack_from('>%dI' % cnt, b, ko))})
    claims = [(k, m, name(n)) for k, m, n in
              (struct.unpack_from('>HHI', b, o_clm + CLAIM * i) for i in range(nclm))]
    t_at, t_len, nsc, _z = struct.unpack_from('>IIHH', b, o_sto)
    scopes = [(name(n), r, u, g) for n, r, u, g in
              (struct.unpack_from('>4I', b, o_sto + 12 + SCOPE * i) for i in range(nsc))]
    return {'abi': (major, minor), 'size': size, 'crc': crc, 'family': family, 'device': device,
            'release': release, 'device_key': name(dkey), 'os': name(os_), 'modules': mods,
            'tables': tables, 'claims': claims, 'area': struct.unpack_from('>5I', b, o_area),
            'storage': {'trailer': (t_at, t_len), 'scopes': scopes}}


def find(image):
    """Every offset in `image` where a whole, valid build descriptor starts."""
    image, out, at = bytes(image), [], 0
    while True:
        at = image.find(MAGIC, at)
        if at < 0:
            return out
        try:
            decode(image, at)
            out.append(at)
        except (ModError, struct.error, ValueError, IndexError):
            pass
        at += 4


def dump(d):
    """A decoded descriptor -> text, one line per record."""
    out = ['build descriptor ABI %d.%d, %d bytes, CRC 0x%08x, %s %s (data family %d, device %d, '
           'release 0x%08x)' % (d['abi'] + (d['size'], d['crc'], d['device_key'], d['os'],
                                            d['family'], d['device'], d['release']))]
    kinds = {KIND_CORE: 'core', KIND_FLASHED: 'flashed', KIND_LOADER: 'loader'}
    for m in d['modules']:
        out.append('module %d %s %s (%s%s), %d exports' % (
            m['index'], m['name'], m['version'], kinds.get(m['kind'], m['kind']),
            ', sites' if m['flags'] & 1 else '', len(m['exports'])))
        out += ['  0x%08x %s' % (a, n) for n, a in m['exports']]
    for t in d['tables']:
        fl = [w for bit, w in ((F_MORE, 'more'), (F_RENDER, 'render'), (F_DSP, 'dsp')) if t['flags'] & bit]
        out.append('table %s at 0x%08x, %d-byte entries, %d of %d%s' % (
            t['name'], t['address'], t['entry'], t['count'], t['capacity'],
            (' (%s)' % ', '.join(fl)) if fl else ''))
        if t['keys']:
            out.append('  keys ' + ' '.join('%d:%d' % (k >> 16, k & 0xFFFF) for k in t['keys']))
    for kind, mi, n in d['claims']:
        out.append('claim %s by module %d: %s' % ('name' if kind == CLAIM_NAME else kind, mi, n))
    a = d['area']
    out.append('RAM end 0x%08x, +Drive area 0x%08x-0x%08x, staging 0x%08x (%d bytes)' % a)
    s = d['storage']
    out.append('lockfile trailer at 0x%08x, %d bytes' % s['trailer'])
    out += ['storage %s: %d of %d bits, generation %d' % (n, u, r, g) for n, r, u, g in s['scopes']]
    return '\n'.join(out)


def _main_image(raw, key=None):
    """A file -> the main OS image in it: a built .syx (of the device `key`, or the one whose
    SysEx id it carries), or the image itself."""
    if raw[:1] != b'\xf0':
        return raw
    from . import devices, syx
    s = syx.Syx(raw)
    devs = [d for d in devices.devices() if d.container == 'ele3'
            and (d.key == key if key else d.sysex_id == s.device_id)]
    if not devs:
        raise ModError('no device for this .syx (give --device)')
    return s.section(devs[0].main_section)


def main(argv=None):
    import argparse
    ap = argparse.ArgumentParser(prog='python -m elekloader.desc',
                                 description='Read the build descriptor (core_desc) out of a build.')
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('dump', help='print the descriptor of a built .syx or main OS image')
    p.add_argument('file')
    p.add_argument('--device', help='the device key, for a .syx')
    a = ap.parse_args(argv)
    with open(a.file, 'rb') as fh:
        img = _main_image(fh.read(), a.device)
    at = find(img)
    if not at:
        print('no build descriptor in %s' % a.file, file=sys.stderr)
        return 1
    print(dump(decode(img, at[0])))
    return 0


if __name__ == '__main__':
    sys.exit(main())
