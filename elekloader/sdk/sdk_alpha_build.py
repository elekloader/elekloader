# SPDX-License-Identifier: GPL-2.0-or-later
"""Private empty-machine experiment; emits ordinary format-2 mods."""
import argparse
import json
import os
import struct
import sys
import tempfile

from elekloader import elemod, formats, link
from elekloader.sdk import build


def sdk_alpha_registrations(doc, sections):
    """Discover compiler-emitted adapters; never repeat machine declarations in JSON."""
    def sdk_alpha_bytes(where, size):
        sec, off = where
        raw = sections.get(sec, b'')
        if off < 0 or off + size > len(raw):
            raise build.BuildError('sdk alpha: truncated registration')
        return raw[off:off + size]

    def sdk_alpha_pointer(where, offset):
        sec, base = where
        rs = [r for r in doc['relocs'] if r[0] == sec and r[1] == base + offset]
        if len(rs) != 1 or rs[0][2] != 'abs32':
            raise build.BuildError('sdk alpha: missing registration pointer')
        _, _, _, target, add = rs[0]
        if target.startswith('sec:'):
            return target[4:], add
        if target.startswith('sym:') and target[4:] in doc['symbols']:
            s, o = doc['symbols'][target[4:]]
            return s, o + add
        raise build.BuildError('sdk alpha: registration must point into its own mod')

    def sdk_alpha_name(where, offset, limit):
        at = sdk_alpha_pointer(where, offset)
        # Read one byte at a time so a string at the end of a section is valid.
        text = bytearray()
        for i in range(limit + 1):
            b = sdk_alpha_bytes((at[0], at[1] + i), 1)[0]
            if not b and text:
                return
            if b < 32 or b > 126 or i == limit:
                break
            text.append(b)
        raise build.BuildError('sdk alpha: machine names need 1..%d printable ASCII characters' % limit)

    registrations, ids = [], set()
    for name in sorted(doc['exports']):
        if not name.startswith('sdk_alpha_registration_'):
            continue
        at = doc['symbols'][name]
        raw = sdk_alpha_bytes(at, 32)
        mid, = struct.unpack_from('>i', raw)
        if not 4 <= mid <= 127 or mid in ids:
            raise build.BuildError('sdk alpha: duplicate or invalid machine slot %d (expected 4..127)' % mid)
        ui = sdk_alpha_pointer(at, 28)
        abi = [r for r in doc['relocs'] if r[:2] == [ui[0], ui[1]]]
        if len(abi) != 1 or abi[0][2:] != ['abs32', 'sym:sdk_alpha_dt1_empty_ui', 0]:
            raise build.BuildError('sdk alpha: invalid empty-machine ABI')
        machine = sdk_alpha_pointer(ui, 4)
        data = sdk_alpha_bytes(machine, 32)
        if struct.unpack_from('>i', data)[0] != mid or struct.unpack_from('>iii', raw, 16) != (3, mid, 0x55493330):
            raise build.BuildError('sdk alpha: inconsistent machine adapter')
        if any(data[12:28]) or any(r[0] == machine[0] and machine[1] + 12 <= r[1] < machine[1] + 28
                                   for r in doc['relocs']):
            raise build.BuildError('sdk alpha: this slice requires zero pages and zero knobs')
        sdk_alpha_name(machine, 4, 16)
        sdk_alpha_name(machine, 8, 4)
        ids.add(mid)
        registrations.append((name, mid))
    if not registrations:
        raise build.BuildError('sdk alpha: no ELEK_REGISTER_MACHINE declaration found')
    return registrations


def sdk_alpha_build(moddir, stock, out=None):
    with tempfile.TemporaryDirectory(prefix='sdk-alpha-') as tmp:
        path, mod = build.build(moddir, stock, tmp)
        with open(path) as fh:
            doc = json.load(fh)
        if doc['target']['device'] != 'digitakt-mk1' or doc['target']['os'] != '1.54':
            raise build.BuildError('sdk alpha: only Digitakt mk1 OS 1.54 is supported')
        firmware, dev, _ = formats.load(stock)
        image = formats.main_image(firmware, dev)
        sections = {s: elemod.parts_bytes(d['parts'], image, dev)
                    for s, d in mod.sections.items() if 'parts' in d}
        registrations = sdk_alpha_registrations(doc, sections)
        if any(c['to'] == 'core_machines' for c in doc['contribute']):
            raise build.BuildError('sdk alpha: declare machines only in C')
        for symbol, mid in registrations:
            claim = 'machine:%d' % mid
            names = doc['resources'].setdefault('names', [])
            if claim in names:
                raise build.BuildError('sdk alpha: declare machines only in C')
            names.append(claim)
            doc['contribute'].append({'to': 'core_machines', 'order': 50, 'data': '00000000',
                                      'relocs': [[0, 'abs32', 'sym:' + symbol, 0]], 'claims': []})
        for required in ('core', 'machine-pages'):
            if required not in doc['requires']:
                doc['requires'].append(required)
        mod = link.Mod2(doc, doc['id'])
        out = out or os.path.join(moddir, 'out')
        os.makedirs(out, exist_ok=True)
        dest = os.path.join(out, os.path.basename(path))
        with open(dest, 'w') as fh:
            json.dump(doc, fh, indent=1)
            fh.write('\n')
        return dest, mod


def sdk_alpha_main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('moddir')
    ap.add_argument('--stock', required=True)
    ap.add_argument('--out')
    args = ap.parse_args(argv)
    try:
        path, _ = sdk_alpha_build(args.moddir, args.stock, args.out)
    except (build.BuildError, OSError, KeyError, ValueError) as e:
        print('BUILD FAILED: %s' % e)
        return 1
    print('BUILT %s' % path)
    return 0


if __name__ == '__main__':
    sys.exit(sdk_alpha_main())
