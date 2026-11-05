# SPDX-License-Identifier: GPL-2.0-or-later
"""Private machine UI experiment; emits ordinary format-2 mods."""
import argparse
import collections
import json
import os
import re
import struct
import sys
import tempfile

from elekloader import devices, elemod, formats, link
from elekloader.sdk import build


# The SDK's support matrix, the one place it lives: each supported release, the table a
# registration goes to, its claim, the companions it requires, its first machine id and the
# optional features its companions implement. The refusals and the tests derive from it.
Target = collections.namedtuple('Target', 'table claim companions first_id features')
SDK_ALPHA_TARGETS = {
    ('digitakt-mk1', '1.54'): Target('core_machines', 'machine:%d', ('core', 'machine-pages'),
                                     4, frozenset(('render', 'defaults'))),
    ('digitone-mk1', '1.44'): Target('sdk_alpha_machines', 'dnmachine:%d', ('core', 'machines'),
                                     4, frozenset(('render', 'defaults'))),
    # the Digitone II's synth type 4 is MIDI
    ('digitone-mk2', '1.12'): Target('sdk_alpha_machines', 'mk2-synth:%d', ('core', 'machines-dn2'),
                                     5, frozenset()),
}
SDK_ALPHA_ABI = 'sdk_alpha_dt1_ui_v1'    # the one symbol the companions export to SDK machines
# sdk_alpha_machine.h's limits (#define ELEK_UI_*) and the layout of its structs in a ColdFire build
# (its ELEK_OFFSET and ELEK_SIZE asserts); tests/test_sdk_alpha.py checks they agree.
LIMITS = {'ELEK_UI_LABEL_MAX': 5, 'ELEK_UI_KNOB_MAX': 15, 'ELEK_UI_PAGE_MAX': 2, 'ELEK_UI_PAGE_LABEL_MAX': 16,
          'ELEK_UI_NAME_MAX': 16, 'ELEK_UI_SHORT_NAME_MAX': 4}
LAYOUT = {
    ('sdk_alpha_registration', 'id'): 0, ('sdk_alpha_registration', 'params'): 16,
    ('sdk_alpha_registration', 'ui'): 28, ('sdk_alpha_registration', None): 32,
    ('sdk_alpha_machine_ui', 'machine'): 4,
    ('elek_machine', 'id'): 0, ('elek_machine', 'name'): 4, ('elek_machine', 'short_name'): 8,
    ('elek_machine', 'knobs'): 12, ('elek_machine', 'knob_count'): 16, ('elek_machine', 'pages'): 20,
    ('elek_machine', 'render'): 96, ('elek_machine', None): 100,
    ('elek_page', 'label'): 0, ('elek_page', 'encoder'): 4, ('elek_page', None): 36,
    ('elek_knob', 'label'): 0, ('elek_knob', 'min'): 4, ('elek_knob', 'max'): 8, ('elek_knob', 'def'): 12,
    ('elek_knob', 'glyph'): 16, ('elek_knob', None): 20, ('elek_glyph', None): 32,
}
ELEK_UI_LABEL_MAX, ELEK_UI_KNOB_MAX, ELEK_UI_PAGE_MAX = (
    LIMITS['ELEK_UI_LABEL_MAX'], LIMITS['ELEK_UI_KNOB_MAX'], LIMITS['ELEK_UI_PAGE_MAX'])
MACHINE_NAME, MACHINE_SHORT_NAME, MACHINE_KNOBS, MACHINE_KNOB_COUNT, MACHINE_PAGES, MACHINE_RENDER, MACHINE_SIZE = (
    LAYOUT['elek_machine', f] for f in ('name', 'short_name', 'knobs', 'knob_count', 'pages', 'render', None))
PAGE_LABEL, PAGE_ENCODER, PAGE_SIZE = (LAYOUT['elek_page', f] for f in ('label', 'encoder', None))
KNOB_LABEL, KNOB_MIN, KNOB_GLYPH, KNOB_SIZE = (LAYOUT['elek_knob', f] for f in ('label', 'min', 'glyph', None))
GLYPH_SIZE = LAYOUT['elek_glyph', None]


def sdk_alpha_registrations(doc, sections, layouts=None):
    """Discover compiler-emitted adapters; never repeat machine declarations in JSON.

    layouts, when given, is filled with each machine's declared layout, by machine id:
    {'name', 'knobs': [{'id', 'label', 'min', 'max', 'def'}], 'pages': [{'label', 'knobs': {encoder 0-7:
    knob}}], 'render': bool}. The built mod does not change."""
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

    def relocated(where, offset):
        return any(r[0] == where[0] and r[1] == where[1] + offset for r in doc['relocs'])

    def pointer_or_null(where, offset):
        """The pointer at where + offset, or None when it is null."""
        if relocated(where, offset):
            return sdk_alpha_pointer(where, offset)
        if any(sdk_alpha_bytes((where[0], where[1] + offset), 4)):
            raise build.BuildError('sdk alpha: pointers must point into the mod')
        return None

    registrations, ids = [], set()
    for name in sorted(doc['exports']):
        if not name.startswith('sdk_alpha_registration_'):
            continue
        at = doc['symbols'][name]
        raw = sdk_alpha_bytes(at, LAYOUT['sdk_alpha_registration', None])
        mid, = struct.unpack_from('>i', raw)
        if not 4 <= mid <= 127 or mid in ids:
            raise build.BuildError('sdk alpha: duplicate or invalid machine slot %d (expected 4..127)' % mid)
        ui = sdk_alpha_pointer(at, LAYOUT['sdk_alpha_registration', 'ui'])
        abi = [r for r in doc['relocs'] if r[:2] == [ui[0], ui[1]]]
        if len(abi) != 1 or abi[0][2:] != ['abs32', 'sym:' + SDK_ALPHA_ABI, 0]:
            raise build.BuildError('sdk alpha: invalid machine UI ABI')
        machine = sdk_alpha_pointer(ui, LAYOUT['sdk_alpha_machine_ui', 'machine'])
        data = sdk_alpha_bytes(machine, MACHINE_SIZE)
        if struct.unpack_from('>i', data)[0] != mid or struct.unpack_from(
                '>iii', raw, LAYOUT['sdk_alpha_registration', 'params']) != (3, mid, 0x55493330):
            raise build.BuildError('sdk alpha: inconsistent machine adapter')
        knob_count, = struct.unpack_from('>I', data, MACHINE_KNOB_COUNT)
        if knob_count > ELEK_UI_KNOB_MAX:
            raise build.BuildError('sdk alpha: a machine has at most %d knobs' % ELEK_UI_KNOB_MAX)
        knobs = pointer_or_null(machine, MACHINE_KNOBS)
        if knob_count and not knobs:
            raise build.BuildError('sdk alpha: knob_count needs knobs (use ELEK_KNOBS)')
        knob_layouts = []
        for k in range(knob_count):
            ref = (knobs[0], knobs[1] + KNOB_SIZE * k)
            kd = sdk_alpha_bytes(ref, KNOB_SIZE)
            label = sdk_alpha_name(ref, KNOB_LABEL, ELEK_UI_LABEL_MAX)
            low, high, default = struct.unpack_from('>iii', kd, KNOB_MIN)
            if high <= low:
                raise build.BuildError('sdk alpha: knob %s needs max above min (%d..%d)' % (label, low, high))
            glyph = pointer_or_null(ref, KNOB_GLYPH)
            if glyph:
                if glyph[1] % 2:
                    raise build.BuildError('sdk alpha: glyph must be word-aligned')
                sdk_alpha_bytes(glyph, GLYPH_SIZE)
            knob_layouts.append({'id': k, 'label': label, 'min': low, 'max': high,
                                 'def': min(max(default, low), high)})
        page_layouts = []
        for page in range(ELEK_UI_PAGE_MAX):
            at_page = (machine[0], machine[1] + MACHINE_PAGES + PAGE_SIZE * page)
            title = pointer_or_null(at_page, PAGE_LABEL)
            if title:
                if len(page_layouts) != page:
                    raise build.BuildError('sdk alpha: page %d needs page %d before it' % (page + 1, page))
                page_layouts.append({'label': sdk_alpha_name(at_page, PAGE_LABEL, LIMITS['ELEK_UI_PAGE_LABEL_MAX']),
                                     'knobs': {}})
            for encoder in range(8):
                ref = pointer_or_null(at_page, PAGE_ENCODER + 4 * encoder)
                if not ref:
                    continue
                if not title:
                    raise build.BuildError('sdk alpha: page %d has knobs but no label' % (page + 1))
                k, off = divmod(ref[1] - knobs[1], KNOB_SIZE) if knobs and ref[0] == knobs[0] else (-1, 0)
                if off or not 0 <= k < knob_count:
                    raise build.BuildError('sdk alpha: page %d encoder %s: a page holds &knobs[ID] '
                                           'of the machine\'s own knobs' % (page + 1, 'ABCDEFGH'[encoder]))
                page_layouts[page]['knobs'][encoder] = knob_layouts[k]
        render = relocated(machine, MACHINE_RENDER)
        if render:
            sdk_alpha_pointer(machine, MACHINE_RENDER)          # a function in the mod itself
        elif any(data[MACHINE_RENDER:MACHINE_RENDER + 4]):
            raise build.BuildError('sdk alpha: render must be a function in the mod')
        machine_name = sdk_alpha_name(machine, MACHINE_NAME, LIMITS['ELEK_UI_NAME_MAX'])
        sdk_alpha_name(machine, MACHINE_SHORT_NAME, LIMITS['ELEK_UI_SHORT_NAME_MAX'])
        if layouts is not None:
            layouts[mid] = {'name': machine_name, 'pages': page_layouts, 'knobs': knob_layouts,
                            'render': render}
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
            names = {d.key: d.name for d in devices.devices()}
            supported = ['%s OS %s' % (names[d], v) for d, v in SDK_ALPHA_TARGETS]
            raise build.BuildError('sdk alpha: only %s and %s are supported' % (', '.join(supported[:-1]), supported[-1]))
        table, claim_of, companions, low, features = SDK_ALPHA_TARGETS[device, release]
        firmware, dev, _ = formats.load(stock)
        image = formats.main_image(firmware, dev)
        sections = {s: elemod.parts_bytes(d['parts'], image, dev)
                    for s, d in mod.sections.items() if 'parts' in d}
        layouts = {}
        registrations = sdk_alpha_registrations(doc, sections, layouts)
        unresolved = sorted(set(doc['imports']) - {SDK_ALPHA_ABI})
        soft_float = [n for n in unresolved if re.fullmatch(r'__\w*?[sdtx]f\w*', n)]    # libgcc's
        if soft_float:
            raise build.BuildError('sdk alpha: the machine uses floating point (%s): the ColdFire has '
                                   'none; ELEK_SAMPLE takes constants only, use elek_level'
                                   % ', '.join(soft_float))
        if unresolved:
            raise build.BuildError('sdk alpha: the machine imports %s, which no companion exports'
                                   % ', '.join(unresolved))
        for _, mid in registrations:
            if mid < low:
                raise build.BuildError('sdk alpha: duplicate or invalid machine slot %d (expected %d..127)' % (mid, low))
            if 'defaults' not in features and any(knob['def'] != knob['min'] for knob in layouts[mid]['knobs']):
                raise build.BuildError('sdk alpha: defaults are not supported on %s %s yet' % (device, release))
            if layouts[mid]['render'] and 'render' not in features:
                raise build.BuildError('sdk alpha: render is not supported on %s %s yet' % (device, release))
        if any(c['to'] == table for c in doc['contribute']):
            raise build.BuildError('sdk alpha: declare machines only in C')
        for symbol, mid in registrations:
            claim = claim_of % mid
            names = doc['resources'].setdefault('names', [])
            if claim in names or claim in mod.implied_names(image):
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
        name = os.path.basename(path)
        if listed is not None and sum(map(len, listed.values())) > 1:     # one file a target
            name = '%s-%s-%s-os%s.elemod' % (doc['id'], doc['version'], device, release)
        dest = os.path.join(out, name)
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
