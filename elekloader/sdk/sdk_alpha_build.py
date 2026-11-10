# SPDX-License-Identifier: GPL-2.0-or-later
"""Builds an SDK machine (elekloader/sdk_alpha_machine.h) into an ordinary format-2 mod."""
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


# The SDK's support matrix, the one place it lives: each supported release, the companions it
# requires, its first machine id, the optional features its companions implement, and the core table
# that also needs a native descriptor (the Digitakt's core_machines: a struct cm_machine the builder
# writes). Every machine goes to the companion's table SDK_ALPHA_TABLE; claims come from the profile's
# slot_claims, and how many machines it holds from its table_limits. The refusals and the tests derive from it.
Target = collections.namedtuple('Target', 'companions first_id features native')
SDK_ALPHA_TARGETS = {
    ('digitakt-mk1', '1.54'): Target(('core', 'machine-pages'), 4,
                                     frozenset(('render', 'defaults', 'mono', 'midi_cc')), 'core_machines'),
    ('digitone-mk1', '1.44'): Target(('core', 'machines'), 4,
                                     frozenset(('render', 'defaults', 'mono', 'midi_cc')), None),
    # the Digitone II's synth type 4 is MIDI; its render is SHARC code ('kernels'): the C is built with
    # ELEK_TARGET_SHARC and .render names a label in mod.json's "sharc": {"sources": [...]}
    ('digitone-mk2', '1.12'): Target(('core', 'machines-dn2'), 5, frozenset(('kernels', 'defaults', 'mono', 'midi_cc')),
                                     None),
}
SDK_ALPHA_TABLE = 'sdk_alpha_machines'   # pointers to struct elek_machine, read by each companion
SDK_ALPHA_ABI = 'sdk_alpha_abi_v1'       # the one symbol the companions export to SDK machines
# core's struct cm_machine (digitakt-mk1/core3.h): id, name, short name, icon, params (3: SLICE's
# parameters), render (its own id: an empty voice for SDK render), and no page tail: machine-pages
# finds the SDK machine in SDK_ALPHA_TABLE.
CM_MACHINE = struct.Struct('>iIIIiiII')
# sdk_alpha_machine.h's limits (#define ELEK_UI_*) and the layout of its structs in a ColdFire build
# (its ELEK_OFFSET and ELEK_SIZE asserts); tests/test_sdk_alpha.py checks they agree.
LIMITS = {'ELEK_UI_LABEL_MAX': 5, 'ELEK_UI_KNOB_MAX': 15, 'ELEK_UI_PAGE_MAX': 2, 'ELEK_UI_PAGE_LABEL_MAX': 16,
          'ELEK_UI_NAME_MAX': 16, 'ELEK_UI_SHORT_NAME_MAX': 4}
LAYOUT = {
    ('elek_machine', 'id'): 0, ('elek_machine', 'name'): 4, ('elek_machine', 'short_name'): 8,
    ('elek_machine', 'knobs'): 12, ('elek_machine', 'knob_count'): 16, ('elek_machine', 'pages'): 20,
    ('elek_machine', 'render'): 96, ('elek_machine', 'play'): 100, ('elek_machine', 'prio'): 101,
    ('elek_machine', None): 102,
    ('elek_page', 'label'): 0, ('elek_page', 'encoder'): 4, ('elek_page', None): 36,
    ('elek_knob', 'label'): 0, ('elek_knob', 'min'): 4, ('elek_knob', 'max'): 8, ('elek_knob', 'def'): 12,
    ('elek_knob', 'glyph'): 16, ('elek_knob', None): 20, ('elek_glyph', None): 32,
}
ELEK_UI_LABEL_MAX, ELEK_UI_KNOB_MAX, ELEK_UI_PAGE_MAX = (
    LIMITS['ELEK_UI_LABEL_MAX'], LIMITS['ELEK_UI_KNOB_MAX'], LIMITS['ELEK_UI_PAGE_MAX'])
(MACHINE_NAME, MACHINE_SHORT_NAME, MACHINE_KNOBS, MACHINE_KNOB_COUNT, MACHINE_PAGES, MACHINE_RENDER, MACHINE_PLAY,
 MACHINE_PRIO, MACHINE_SIZE) = (LAYOUT['elek_machine', f] for f in ('name', 'short_name', 'knobs', 'knob_count', 'pages',
                                                                     'render', 'play', 'prio', None))
PLAY_NAMES = ('ELEK_POLY', 'ELEK_MONO', 'ELEK_MONO_LEGATO')
PRIO_NAMES = ('ELEK_PRIO_LAST', 'ELEK_PRIO_LOW', 'ELEK_PRIO_HIGH')
PAGE_LABEL, PAGE_ENCODER, PAGE_SIZE = (LAYOUT['elek_page', f] for f in ('label', 'encoder', None))
KNOB_LABEL, KNOB_MIN, KNOB_GLYPH, KNOB_SIZE = (LAYOUT['elek_knob', f] for f in ('label', 'min', 'glyph', None))
GLYPH_SIZE = LAYOUT['elek_glyph', None]


def sdk_alpha_registrations(doc, sections):
    """The machines ELEK_REGISTER_MACHINE declared, checked: their layouts by machine id,
    {'at': (section, offset) of the struct elek_machine, 'name', 'knobs': [{'id', 'label', 'min', 'max',
    'def'}], 'pages': [{'label', 'knobs': {encoder 0-7: knob}}], 'render': False, True (a function in
    the mod) or the name of the function it imports (a SHARC label on the Digitone II), 'play' and
    'prio': its .play and .prio}. A refusal names where it is: the machine, then its page, encoder or knob."""
    def fail(where, message):
        raise build.BuildError('sdk alpha: %s: %s' % (where, message))

    def read(where, at, size, message):
        sec, off = at
        raw = sections.get(sec, b'')
        if off < 0 or off + size > len(raw):
            fail(where, message)
        return raw[off:off + size]

    def target(where, at, field):
        """The (section, offset) the pointer at `at` holds, or None when it is null."""
        sec, off = at
        rs = [r for r in doc['relocs'] if r[0] == sec and r[1] == off]
        if not rs:
            value = sections.get(sec, b'')[off:off + 4]
            if any(value):
                fail(where, '%s is 0x%s, not null or an address in this mod' % (field, value.hex()))
            return None
        if len(rs) != 1 or rs[0][2] != 'abs32':
            fail(where, '%s is not a plain 32-bit address (build with the SDK\'s flags)' % field)
        _, _, _, to, add = rs[0]
        if to.startswith('sec:'):
            return to[4:], add
        if to.startswith('sym:') and to[4:] in doc['symbols']:
            s, o = doc['symbols'][to[4:]]
            return s, o + add
        fail(where, '%s points at %s, outside this mod: point it at data in the mod' % (field, to[4:]))

    def text(where, at, field, limit):
        at = target(where, at, field)
        if not at:
            fail(where, 'has no %s (give it 1..%d printable ASCII characters)' % (field, limit))
        raw = sections.get(at[0], b'')
        end = raw.find(b'\0', at[1]) if 0 <= at[1] < len(raw) else -1
        if end < 0:
            fail(where, '%s is not a string that ends inside this mod' % field)
        value = raw[at[1]:end]
        shown = json.dumps(value.decode('latin-1'))
        if not value:
            fail(where, '%s is empty (give it 1..%d printable ASCII characters)' % (field, limit))
        bad = [b for b in value if not 32 <= b <= 126]
        if bad:
            fail(where, '%s %s has 0x%02x, which is not printable ASCII' % (field, shown, bad[0]))
        if len(value) > limit:
            fail(where, '%s %s has %d characters (at most %d)' % (field, shown, len(value), limit))
        return value.decode('ascii')

    def field(at, offset):
        return at[0], at[1] + offset

    layouts = {}
    for name in sorted(doc['exports']):
        if not name.startswith('sdk_alpha_registration_'):
            continue
        where = 'ELEK_REGISTER_MACHINE(%s)' % name[len('sdk_alpha_registration_'):]
        machine = target(where, tuple(doc['symbols'][name]), 'the registration')
        if not machine:
            fail(where, 'registers no machine')
        data = read(where, machine, MACHINE_SIZE, 'the machine runs past the end of its data '
                                                  '(rebuild with this SDK\'s header)')
        mid, = struct.unpack_from('>i', data)
        where = 'machine id %d' % mid
        machine_name = text(where, field(machine, MACHINE_NAME), 'name', LIMITS['ELEK_UI_NAME_MAX'])
        where = 'machine %s (id %d)' % (json.dumps(machine_name), mid)
        text(where, field(machine, MACHINE_SHORT_NAME), 'short_name', LIMITS['ELEK_UI_SHORT_NAME_MAX'])
        if not 4 <= mid <= 127:
            fail(where, 'id %d is outside 4..127' % mid)
        if mid in layouts:
            fail(where, 'machine %s has the same id: give each machine its own id'
                 % json.dumps(layouts[mid]['name']))
        knob_count, = struct.unpack_from('>I', data, MACHINE_KNOB_COUNT)
        if knob_count > ELEK_UI_KNOB_MAX:
            fail(where, 'has %d knobs (at most %d)' % (knob_count, ELEK_UI_KNOB_MAX))
        knobs = target(where, field(machine, MACHINE_KNOBS), 'knobs')
        if knob_count and not knobs:
            fail(where, 'knob_count is %d but knobs is null (give the knobs with ELEK_KNOBS)' % knob_count)
        knob_layouts = []
        for k in range(knob_count):
            ref = field(knobs, KNOB_SIZE * k)
            at = '%s, knobs[%d]' % (where, k)
            kd = read(at, ref, KNOB_SIZE, 'is past the end of the knobs, but knob_count is %d '
                                          '(give the knobs with ELEK_KNOBS)' % knob_count)
            label = text(at, field(ref, KNOB_LABEL), 'label', ELEK_UI_LABEL_MAX)
            at += ' ' + json.dumps(label)
            low, high, default = struct.unpack_from('>iii', kd, KNOB_MIN)
            if high <= low:
                fail(at, 'max %d is not above min %d' % (high, low))
            glyph = target(at, field(ref, KNOB_GLYPH), 'glyph')
            if glyph:
                if glyph[1] % 2:
                    fail(at, 'glyph is at an odd address (point it at a struct elek_glyph)')
                read(at, glyph, GLYPH_SIZE, 'glyph runs past the end of its data '
                                            '(point it at a struct elek_glyph)')
            knob_layouts.append({'id': k, 'label': label, 'min': low, 'max': high,
                                 'def': min(max(default, low), high)})
        page_layouts = []
        for page in range(ELEK_UI_PAGE_MAX):
            at_page = field(machine, MACHINE_PAGES + PAGE_SIZE * page)
            at = '%s, pages[%d]' % (where, page)
            title = target(at, field(at_page, PAGE_LABEL), 'label')
            if title:
                label = text(at, field(at_page, PAGE_LABEL), 'label', LIMITS['ELEK_UI_PAGE_LABEL_MAX'])
                if len(page_layouts) != page:
                    fail(where, 'pages[%d] %s is set but pages[%d] has no label'
                         % (page, json.dumps(label), page - 1))
                at += ' ' + json.dumps(label)
                page_layouts.append({'label': label, 'knobs': {}})
            for encoder in range(8):
                at_encoder = '%s, encoder %s' % (at, 'ABCDEFGH'[encoder])
                ref = target(at_encoder, field(at_page, PAGE_ENCODER + 4 * encoder), 'the knob')
                if not ref:
                    continue
                if not title:
                    fail(at, 'has knobs but no label (give the page a label)')
                k, off = divmod(ref[1] - knobs[1], KNOB_SIZE) if knobs and ref[0] == knobs[0] else (-1, 0)
                if off or not 0 <= k < knob_count:
                    fail(at_encoder, 'does not point at one of the machine\'s knobs (use &knobs[ID], %s)'
                         % ('ID below %d' % knob_count if knob_count else 'and give the knobs with ELEK_KNOBS'))
                page_layouts[page]['knobs'][encoder] = knob_layouts[k]
        sec, off = field(machine, MACHINE_RENDER)
        rs = [r for r in doc['relocs'] if r[0] == sec and r[1] == off]
        if len(rs) == 1 and rs[0][2] == 'abs32' and rs[0][3].startswith('sym:') \
                and rs[0][3][4:] not in doc['symbols']:
            render = rs[0][3][4:]           # declared, not defined: the builder pairs it (sdk_alpha_build)
        else:
            render = target(where, (sec, off), 'render') is not None
        play, prio = data[MACHINE_PLAY], data[MACHINE_PRIO]
        if play >= len(PLAY_NAMES):
            fail(where, 'play is %d: give %s' % (play, ', '.join(PLAY_NAMES)))
        if prio >= len(PRIO_NAMES):
            fail(where, 'prio is %d: give %s' % (prio, ', '.join(PRIO_NAMES)))
        layouts[mid] = {'at': machine, 'name': machine_name, 'pages': page_layouts, 'knobs': knob_layouts,
                        'render': render, 'play': play, 'prio': prio}
    if not layouts:
        raise build.BuildError('sdk alpha: no machine registered: add ELEK_REGISTER_MACHINE(machine)')
    if SDK_ALPHA_ABI not in doc['imports']:
        raise build.BuildError('sdk alpha: the mod does not use %s: rebuild it with this SDK\'s header'
                               % SDK_ALPHA_ABI)
    return layouts


def sdk_alpha_sharc(doc, layouts, moddir, spec, work, target):
    """A 'kernels' target's render: each machine's .render names a global label of the mod's SHARC
    assembly (mod.json's "sharc": {"sources": [...]}), which the DSP calls as C would. Puts the kernels
    in doc['sharc'], and points each .render, which the ColdFire never calls, at an rts."""
    sources = [os.path.join(moddir, n) for n in (spec.get('sources', []) if isinstance(spec, dict) else [])]
    labels = set()
    for path in sources:
        with open(path) as fh:
            text = re.sub(r'/\*.*?\*/|//[^\n]*', '', fh.read(), flags=re.S)
        for names in re.findall(r'\.GLOBAL\s+([^;]*);', text, re.I):
            labels.update(n.strip() for n in names.split(',') if n.strip())
    kernels, imported = [], set()
    for mid, layout in sorted(layouts.items()):
        where = 'sdk alpha: machine %s (id %d)' % (json.dumps(layout['name']), mid)
        if layout['render'] is True:
            raise build.BuildError('%s: render is C, which %s cannot play: write it in SHARC assembly, declare '
                                   'it under #ifdef ELEK_TARGET_SHARC and list the file in mod.json\'s "sharc": '
                                   '{"sources": [...]}' % (where, target))
        if layout['render']:
            label = layout['render'] + '.'          # the SHARC name of the C function
            if label not in labels:
                raise build.BuildError('%s: render is %s, but no file in mod.json\'s "sharc": {"sources": [...]} '
                                       'has the global label %s' % (where, layout['render'], label))
            imported.add(layout['render'])
            kernels.append(('sdk', mid, label, [(k['min'], k['max']) for k in layout['knobs']]))
    if not kernels:
        return
    from elekloader.sdk import sharc
    try:
        doc['sharc'] = sharc.kernel_artifact(sources, work, kernels=kernels)
    except ValueError as e:
        raise build.BuildError(str(e)) from e
    run = doc['sections']['.run']
    at = run['len'] + run['len'] % 2
    run['parts'].append(['hex', (bytes(at - run['len']) + b'\x4e\x75').hex()])
    run['len'] = at + 2
    for r in doc['relocs']:
        if r[3].startswith('sym:') and r[3][4:] in imported:
            r[3], r[4] = 'sec:.run', at
    doc['imports'] = [n for n in doc['imports'] if n not in imported]


def sdk_alpha_build(moddir, stock, out=None):
    with open(os.path.join(moddir, 'mod.json')) as fh:
        source = json.load(fh)
    listed = source.get('devices')
    if listed is not None and not isinstance(listed, dict):
        raise build.BuildError('mod.json: "devices" maps each device to its OS versions')
    if listed is not None and {'device', 'os'} & set(source):
        raise build.BuildError('mod.json: "devices" replaces "device" and "os"; give one or the other')
    firmware, dev, rel = formats.load(stock)
    kernels = 'kernels' in getattr(SDK_ALPHA_TARGETS.get((dev.key, rel.version)), 'features', ())
    with tempfile.TemporaryDirectory(prefix='sdk-alpha-') as tmp:
        path, mod = build.build(moddir, stock, tmp, {'cflags': ['-DELEK_TARGET_SHARC']} if kernels else None)
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
        companions, low, features, native = SDK_ALPHA_TARGETS[device, release]
        tables = (SDK_ALPHA_TABLE,) + ((native,) if native else ())
        image = formats.main_image(firmware, dev)
        sections = {s: elemod.parts_bytes(d['parts'], image, dev)
                    for s, d in mod.sections.items() if 'parts' in d}
        layouts = sdk_alpha_registrations(doc, sections)
        limit = dev.table_limits[SDK_ALPHA_TABLE]
        if len(layouts) > limit:
            raise build.BuildError('sdk alpha: %s %s holds at most %d SDK machines; this mod registers %d'
                                   % (device, release, limit, len(layouts)))
        if kernels:
            sdk_alpha_sharc(doc, layouts, moddir, source.get('sharc'), tmp, '%s %s' % (device, release))
        unresolved = sorted(set(doc['imports']) - {SDK_ALPHA_ABI})
        soft_float = [n for n in unresolved if re.fullmatch(r'__\w*?[sdtx]f\w*', n)]    # libgcc's
        if soft_float:
            raise build.BuildError('sdk alpha: the machine uses floating point (%s): the ColdFire has '
                                   'none; ELEK_SAMPLE takes constants only, use elek_level'
                                   % ', '.join(soft_float))
        if unresolved:
            raise build.BuildError('sdk alpha: the machine imports %s, which no companion exports'
                                   % ', '.join(unresolved))
        for mid, layout in layouts.items():
            where = 'sdk alpha: machine %s (id %d)' % (json.dumps(layout['name']), mid)
            if mid < low:
                raise build.BuildError('%s: id %d is outside %d..127 on %s %s' % (where, mid, low, device, release))
            if layout['render'] and not features & {'render', 'kernels'}:
                raise build.BuildError('%s: render is not supported on %s %s yet' % (where, device, release))
            if layout['play'] and 'mono' not in features:
                raise build.BuildError('%s: %s is not supported on %s %s yet'
                                       % (where, PLAY_NAMES[layout['play']], device, release))
            if 'defaults' not in features:
                for knob in layout['knobs']:
                    if knob['def'] != knob['min']:
                        raise build.BuildError('%s, knobs[%d] %s: def %d is not min %d; defaults are not '
                                               'supported on %s %s yet' % (where, knob['id'], json.dumps(knob['label']),
                                                                           knob['def'], knob['min'], device, release))
        for c in doc['contribute']:
            if c['to'] in tables:
                raise build.BuildError('sdk alpha: mod.json adds to %s: declare machines only in C, with '
                                       'ELEK_REGISTER_MACHINE' % c['to'])
        taken = set(doc['resources'].get('names', [])) | set(mod.implied_names(image))
        run = doc['sections']['.run']
        for mid, layout in sorted(layouts.items()):
            sec, off = layout['at']
            doc['contribute'].append({'to': SDK_ALPHA_TABLE, 'order': 50, 'data': '00000000',
                                      'relocs': [[0, 'abs32', 'sec:' + sec, off]], 'claims': []})
            if not native:
                continue
            at = -(-run['len'] // 4) * 4
            run['parts'].append(['hex', (bytes(at - run['len']) + CM_MACHINE.pack(mid, 0, 0, 0, 3, mid, 0, 0)).hex()])
            run['len'] = at + CM_MACHINE.size
            for field, to in ((MACHINE_NAME, 4), (MACHINE_SHORT_NAME, 8)):     # cm_machine's name, short
                r, = [r for r in doc['relocs'] if r[:2] == [sec, off + field]]
                doc['relocs'].append(['.run', at + to, 'abs32', r[3], r[4]])
            doc['contribute'].append({'to': native, 'order': 50, 'data': '00000000',
                                      'relocs': [[0, 'abs32', 'sec:.run', at]], 'claims': []})
        for required in companions:
            if required not in doc['requires']:
                doc['requires'].append(required)
        mod = link.Mod2(doc, doc['id'])
        claimed = taken & {n for t in tables for n in mod.implied_names(image, t)}   # the profile's slot_claims
        if claimed:
            raise build.BuildError('sdk alpha: mod.json claims %s, which its machines claim already: '
                                   'remove it from mod.json' % ', '.join(sorted(claimed)))
        out = out or os.path.join(moddir, 'out')
        os.makedirs(out, exist_ok=True)
        name = os.path.basename(path)
        if listed is not None and sum(map(len, listed.values())) > 1:     # one file a target
            name = '%s-%s-%s-os%s.elemod' % (doc['id'], doc['version'], device, release)
        dest = os.path.join(out, name)
        with open(dest, 'w') as fh:
            json.dump(doc, fh, indent=1)
            fh.write('\n')
        return dest, mod, layouts


def sdk_alpha_main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('moddir')
    ap.add_argument('--stock', required=True)
    ap.add_argument('--out')
    args = ap.parse_args(argv)
    try:
        path, _, _ = sdk_alpha_build(args.moddir, args.stock, args.out)
    except (build.BuildError, OSError, KeyError, ValueError) as e:
        print('BUILD FAILED: %s' % e)
        return 1
    print('BUILT %s' % path)
    return 0


if __name__ == '__main__':
    sys.exit(sdk_alpha_main())
