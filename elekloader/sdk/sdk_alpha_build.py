# SPDX-License-Identifier: GPL-2.0-or-later
"""Private machine UI experiment; emits ordinary format-2 mods."""
import argparse
import json
import os
import struct
import sys
import tempfile

from elekloader import elemod, formats, link
from elekloader.sdk import build


# Each supported release: the table a registration goes to, its claim, the companions it requires.
SDK_ALPHA_TARGETS = {
    ('digitakt-mk1', '1.54'): ('core_machines', 'machine:%d', ('core', 'machine-pages')),
    ('digitone-mk1', '1.44'): ('sdk_alpha_machines', 'dnmachine:%d', ('core', 'machines')),
}


def sdk_alpha_registrations(doc, sections, layouts=None):
    """Discover compiler-emitted adapters; never repeat machine declarations in JSON.

    layouts, when given, is filled with each machine's declared layout, by machine id:
    {'name', 'pages': [{'label', 'knobs': {encoder 0-7: {'id', 'label', 'min', 'max', 'def'}}}]}.
    The built mod does not change."""
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
                return text.decode('ascii')
            if b < 32 or b > 126 or i == limit:
                break
            text.append(b)
        raise build.BuildError('sdk alpha: names and labels need 1..%d printable ASCII characters' % limit)

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
        if len(abi) != 1 or abi[0][2:] not in (
                ['abs32', 'sym:sdk_alpha_dt1_empty_ui', 0],
                ['abs32', 'sym:sdk_alpha_dt1_ui_v3', 0],
                ['abs32', 'sym:sdk_alpha_dt1_ui_v4', 0],
                ['abs32', 'sym:sdk_alpha_dt1_ui_v5', 0]):
            raise build.BuildError('sdk alpha: invalid machine UI ABI')
        machine = sdk_alpha_pointer(ui, 4)
        v5 = abi[0][3] == 'sym:sdk_alpha_dt1_ui_v5'     # .def and .render
        data = sdk_alpha_bytes(machine, 36 if v5 else 32)
        if struct.unpack_from('>i', data)[0] != mid or struct.unpack_from('>iii', raw, 16) != (3, mid, 0x55493330):
            raise build.BuildError('sdk alpha: inconsistent machine adapter')
        page_layouts = []
        page_count, = struct.unpack_from('>I', data, 16)
        if page_count > 2:
            raise build.BuildError('sdk alpha: a machine has at most 2 pages')
        if any(data[20:28]) or any(
                r[0] == machine[0] and machine[1] + 20 <= r[1] < machine[1] + 28
                for r in doc['relocs']):
            raise build.BuildError('sdk alpha: reserved machine fields must be zero')
        if not page_count:
            if any(data[12:16]) or any(
                    r[0] == machine[0] and r[1] == machine[1] + 12 for r in doc['relocs']):
                raise build.BuildError('sdk alpha: empty machines require zero pages')
        else:
            glyphs = abi[0][3] in ('sym:sdk_alpha_dt1_ui_v4', 'sym:sdk_alpha_dt1_ui_v5')
            if abi[0][3] not in ('sym:sdk_alpha_dt1_ui_v3', 'sym:sdk_alpha_dt1_ui_v4',
                                 'sym:sdk_alpha_dt1_ui_v5'):
                raise build.BuildError('sdk alpha: pages require the pages ABI')
            pages = sdk_alpha_pointer(machine, 12)
            knobs = {}
            for page in range(page_count):
                at_page = (pages[0], pages[1] + 36 * page)
                pd = sdk_alpha_bytes(at_page, 36)
                page_layout = {'label': sdk_alpha_name(at_page, 0, 16), 'knobs': {}}
                page_layouts.append(page_layout)
                for encoder in range(8):
                    offset = 4 + 4 * encoder
                    relocated = any(r[0] == pages[0] and r[1] == at_page[1] + offset
                                    for r in doc['relocs'])
                    if not relocated and not any(pd[offset:offset + 4]):
                        continue
                    ref = sdk_alpha_pointer(at_page, offset)
                    kd = sdk_alpha_bytes(ref, 36 if v5 else 28 if glyphs else 24)
                    kid, = struct.unpack_from('>I', kd)
                    if kid >= 15:
                        raise build.BuildError('sdk alpha: knob IDs must be in 0..14 (at most 15 knobs)')
                    if kid in knobs and knobs[kid] != ref:
                        raise build.BuildError('sdk alpha: different knobs cannot share a logical ID')
                    knobs[kid] = ref
                    label = sdk_alpha_name(ref, 4, 5)   # ELEK_UI_LABEL_MAX
                    low, has_low, high, has_high = struct.unpack_from('>iIiI', kd, 8)
                    if has_low not in (0, 1) or has_high not in (0, 1):
                        raise build.BuildError('sdk alpha: bound supplied must be 0 or 1')
                    low, high = low if has_low else 0, high if has_high else 127
                    if low > high:
                        raise build.BuildError('sdk alpha: knob min exceeds max')
                    default, has_default = struct.unpack_from('>iI', kd, 28) if v5 else (0, 0)
                    if has_default not in (0, 1):
                        raise build.BuildError('sdk alpha: bound supplied must be 0 or 1')
                    if has_default and not low <= default <= high:
                        raise build.BuildError('sdk alpha: knob def must be within min..max')
                    page_layout['knobs'][encoder] = {'id': kid, 'label': label, 'min': low, 'max': high,
                                                      'def': default if has_default else low}
                    if glyphs:
                        relocated = any(r[0] == ref[0] and r[1] == ref[1] + 24
                                        for r in doc['relocs'])
                        if relocated or any(kd[24:28]):
                            glyph = sdk_alpha_pointer(ref, 24)
                            if glyph[1] % 2:
                                raise build.BuildError('sdk alpha: glyph must be word-aligned')
                            sdk_alpha_bytes(glyph, 32)
            if set(knobs) != set(range(len(knobs))):
                raise build.BuildError('sdk alpha: knob IDs must be contiguous from zero')
        machine_name = sdk_alpha_name(machine, 4, 16)
        sdk_alpha_name(machine, 8, 4)
        if layouts is not None:
            layouts[mid] = {'name': machine_name, 'pages': page_layouts}
        ids.add(mid)
        registrations.append((name, mid))
    if not registrations:
        raise build.BuildError('sdk alpha: no ELEK_REGISTER_MACHINE declaration found')
    return registrations


def sdk_alpha_build(moddir, stock, out=None):
    with open(os.path.join(moddir, 'mod.json')) as fh:
        source = json.load(fh)
    listed = source.get('devices')
    if listed is not None and not isinstance(listed, dict):
        raise build.BuildError('mod.json: "devices" maps each device to its OS versions')
    if listed is not None and {'device', 'os'} & set(source):
        raise build.BuildError('mod.json: "devices" replaces "device" and "os"; give one or the other')
    with tempfile.TemporaryDirectory(prefix='sdk-alpha-') as tmp:
        path, mod = build.build(moddir, stock, tmp)
        with open(path) as fh:
            doc = json.load(fh)
        device, release = doc['target']['device'], doc['target']['os']
        if listed is not None and release not in listed.get(device, []):
            raise build.BuildError('mod.json is for %s; the stock file is %s %s' % (
                ', '.join('%s %s' % (d, ' '.join(v)) for d, v in sorted(listed.items())), device, release))
        if (device, release) not in SDK_ALPHA_TARGETS:
            raise build.BuildError('sdk alpha: only Digitakt mk1 OS 1.54 and Digitone mk1 OS 1.44 are supported')
        table, claim_of, companions = SDK_ALPHA_TARGETS[device, release]
        firmware, dev, _ = formats.load(stock)
        image = formats.main_image(firmware, dev)
        sections = {s: elemod.parts_bytes(d['parts'], image, dev)
                    for s, d in mod.sections.items() if 'parts' in d}
        layouts = {}
        registrations = sdk_alpha_registrations(doc, sections, layouts)
        if any(c['to'] == table for c in doc['contribute']):
            raise build.BuildError('sdk alpha: declare machines only in C')
        for symbol, mid in registrations:
            claim = claim_of % mid
            names = doc['resources'].setdefault('names', [])
            if claim in names:
                raise build.BuildError('sdk alpha: declare machines only in C')
            names.append(claim)
            doc['contribute'].append({'to': table, 'order': 50, 'data': '00000000',
                                      'relocs': [[0, 'abs32', 'sym:' + symbol, 0]], 'claims': []})
        for required in companions:
            if required not in doc['requires']:
                doc['requires'].append(required)
        mod = link.Mod2(doc, doc['id'])
        mod.sdk_alpha_layouts = layouts     # for the test harness; not part of the built file
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
