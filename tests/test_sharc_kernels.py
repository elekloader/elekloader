# SPDX-License-Identifier: GPL-2.0-or-later
"""DN2 machine kernels (dn2-kernel-v1): the artifact's checks, relocation by difference, the packaging of
SDK and mk2 machines, the placement in the pool and the linker's checks. Synthetic kernels are never run
here; tests/test_dn2_dispatch.py runs the dispatcher."""
import copy
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from elekloader import devices, formats, link, sharc   # noqa: E402
from elekloader.devices.digitone_mk2 import DEVICE   # noqa: E402
from elekloader.elemod import ModError   # noqa: E402
from elekloader.sdk.sharc_reloc import rebase as reloc_rebase, relocatable   # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
PROFILE = DEVICE.releases['1.12'].sharc
REL = DEVICE.releases['1.12']
TARGET = devices.target_of(DEVICE, REL)
STOCK = os.environ.get('ELEKLOADER_DN2_SYX')
FIXTURES = ROOT / 'tests' / 'fixtures' / 'sharc-kernel'
DISPATCHER = json.loads((ROOT / 'mods' / 'machines-dn2' / 'dispatch.sharc.json').read_text())
INERT = json.loads((ROOT / 'examples' / 'sharc-sine' / 'sine.sharc.json').read_text())
INERT.update(entries=None, stack_bytes=0)


def kernel(*kernels, code=None, data=None):
    """A kernel artifact at the probe base: its code jumps to its own word 2 (a PM24 field) and its data
    points at its own byte 4 (an ADDR32 field). `kernels`: (kind, id[, extra]) tuples."""
    code = code or bytes.fromhex('063e00bc8402') + bytes(6)
    data = data if data is not None else struct.pack('<I', 0x20098004) + bytes(12)
    return {'abi': 'dn2-kernel-v1', 'code': code.hex(), 'data': data.hex(), 'code_pc16': 0xbc8400,
            'data_address': 0x20098000, 'data_align': 8,
            'relocs': [['code', 3, 3, 'code'], ['data', 0, 4, 'data']],
            'kernels': [{'kind': k[0], 'id': k[1], 'entry': 2, **({'knobs': []} if k[0] == 'sdk' else {}),
                         **(k[2] if len(k) > 2 else {})} for k in kernels],
            'exports': {}}


def mod(name, *kernels, registers=None, sharc_doc=None, version='0.1', g=None):
    """A format-2 mod with descriptors (id first) registered in the profile's tables, and its kernels.
    registers: [(table, id)], by default one per kernel. g: a filter's G {label, default} for its descriptors."""
    if registers is None:
        table = {'sdk': 'sdk_alpha_machines', 'mk2-voice': 'mk2_voice_synths', 'mk2-filter': 'mk2_voice_filters'}
        registers = [(table[k[0]], k[1]) for k in kernels]
    text, symbols, relocs, contribute = '', {}, [], []
    for n, (table, mid) in enumerate(registers):
        symbols['d%d' % n] = ['.run', 32 * n]
        text += '%08x' % mid + '00' * 28
        contribute.append({'to': table, 'data': '00000000', 'relocs': [[0, 'abs32', 'sym:d%d' % n, 0]]})
    if g:
        label, default = g
        at = len(text) // 2
        symbols.update(g=['.run', at], label=['.run', at + 8])
        text += '00000000%08x' % default + label.encode('latin1').hex() + '00'
        relocs = [['.run', 32 * n + 16, 'abs32', 'sym:g', 0] for n in range(len(registers))] \
            + [['.run', at, 'abs32', 'sym:label', 0]]
    doc = {'elemod': 2, 'id': name, 'version': version, 'target': TARGET,
           'sections': {'.run': {'align': 4, 'parts': [['hex', text + '00' * (-len(text) // 2 % 4)]]}} if text else {},
           'symbols': symbols, 'relocs': relocs, 'contribute': contribute}
    if sharc_doc is not None or kernels:
        doc['sharc'] = sharc_doc if sharc_doc is not None else kernel(*kernels)
    return link.Mod2(doc, name)


CORE = link.Mod2({'elemod': 2, 'id': 'core', 'version': '0.2', 'target': TARGET,
                  'sections': {'.boot': {'align': 4, 'parts': [['hex', '4e75']]}},
                  'collections': {t: {'entry': 4} for t in ('mk2_voice_synths', 'mk2_voice_filters',
                                                            'sdk_alpha_machines')}}, 'core')
MACHINES = link.Mod2({'elemod': 2, 'id': 'machines-dn2', 'version': '0.3', 'target': TARGET, 'sections': {},
                      'sharc': DISPATCHER}, 'machines-dn2')


class PayloadTests(unittest.TestCase):
    def test_valid(self):
        p = sharc.kernel_payload(kernel(('sdk', 9, {'knobs': [[0, 127], [-12, 12]]}), ('mk2-voice', 31),
                                        ('mk2-filter', 6, {'state_bytes': 12})), PROFILE)
        self.assertEqual(p['kernels'], [{'kind': 'sdk', 'id': 9, 'entry': 2, 'knobs': [(0, 127), (-12, 12)]},
                                        {'kind': 'mk2-voice', 'id': 31, 'entry': 2, 'state_bytes': 0},
                                        {'kind': 'mk2-filter', 'id': 6, 'entry': 2, 'state_bytes': 12}])
        self.assertEqual(sharc.kernel_payload(DISPATCHER, PROFILE)['kernels'], [])

    def test_refusals(self):
        shape = 'a kernel is {kind: sdk'
        for doc, message in (
                (kernel(('sdk', 4)), 'sdk id 4 is outside 5..127'),
                (kernel(('sdk', 128)), 'outside 5..127'),
                (kernel(('mk2-voice', 32)), 'mk2-voice id 32 is outside 5..31'),
                (kernel(('mk2-filter', 5)), 'outside 6..31'),
                (kernel(('sdk', 7), ('mk2-voice', 7)), 'two voice kernels with id 7'),
                (kernel(('mk2-filter', 7), ('mk2-filter', 7)), 'two filter kernels with id 7'),
                (kernel(('sdk', 7, {'entry': 6})), 'outside its code'),
                (kernel(('sdk', 7, {'knobs': [[3, 3]]})), 'min below max'),
                (kernel(('sdk', 7, {'knobs': [[0, 2 ** 31]]})), 'pairs of int32'),
                (kernel(('sdk', 7, {'knobs': [[0, 1]] * 16})), 'up to 15'),
                (kernel(('sdk', 7, {'state_bytes': 4})), shape),
                (kernel(('mk2-voice', 7, {'knobs': []})), shape),
                (kernel(('voice', 7)), shape),
                (dict(kernel(('sdk', 7)), kernels=[{'kind': 'sdk', 'id': 7, 'entry': 2}]), 'knobs are up to'),
                (kernel(('mk2-voice', 7, {'state_bytes': 6})), 'whole 32-bit words'),
                (kernel(), 'no kernel and no dispatcher'),
                (dict(kernel(), exports={'dn2_voice_hook.': ['code', 0]}), 'defines all'),
                (dict(kernel(('sdk', 7)), relocs=[['code', 10, 4, 'code']]), 'invalid relocation'),
                (dict(kernel(('sdk', 7)), abi='dn2-kernel-v2'), 'abi must be dn2-kernel-v1')):
            with self.subTest(message):
                with self.assertRaisesRegex(ModError, message):
                    sharc.kernel_payload(doc, PROFILE)


CODE = bytes.fromhex('0f810000 0000 0c00 20000033 1f00 00000000 6300 0000 0000')   # opcodes; fields zero


def probe_link(objects, code, data, align=8):
    """seld's shapes: a PM32 data address and a PM32 return address (bytes 2..5 of a 48-bit
    instruction, big-endian), a PM24 jump (bytes 3..5); a data word with a function's PC16 and
    one with a data address (little-endian). The hardware loop's PC-relative offset never moves."""
    if data % align:
        raise ValueError('data section moved')
    c, d = bytearray(CODE), bytearray(16)
    c[2:6] = (data + 12).to_bytes(4, 'big')
    c[12:18] = c[12:14] + (code + 3 - 1).to_bytes(4, 'big')
    c[21:24] = ((code + 9) & 0xffffff).to_bytes(3, 'big')
    d[4:8] = (code + 9).to_bytes(4, 'little')
    d[8:12] = (data + 12).to_bytes(4, 'little')
    return bytes(c), bytes(d), {'f.': code + 9, 'x.': data + 12}


class RelocTests(unittest.TestCase):
    def test_fields_and_rebase(self):
        art = relocatable(probe_link, [])
        self.assertEqual(art['relocs'], [('code', 2, 4, 'data'), ('code', 14, 4, 'code'), ('code', 21, 3, 'code'),
                                         ('data', 4, 4, 'code'), ('data', 8, 4, 'data')])
        self.assertEqual(art['symbols'], {'f.': ('code', 9), 'x.': ('data', 12)})
        self.assertEqual(art['data_align'], 8)
        for code, data in ((0xbc8400, 0x20090000), (0xbcffff, 0x2009fff8), (0xffffff, 0x200a0008)):
            self.assertEqual(reloc_rebase(art, code, data), probe_link([], code, data)[:2])
        with self.assertRaisesRegex(ValueError, 'not 8-byte aligned'):
            reloc_rebase(art, 0xbc8400, 0x20090004)

    def test_a_layout_that_moves_is_refused(self):
        def shifting(objects, code, data):
            c, d, s = probe_link(objects, code, data)
            return c, d[:-1] if code % 2 else d, s
        with self.assertRaisesRegex(ValueError, 'changes size'):
            relocatable(shifting, [])

        def not_an_address(objects, code, data):
            c, d, s = probe_link(objects, code, data)
            return c, d[:12] + (code * 2).to_bytes(4, 'little'), s
        with self.assertRaisesRegex(ValueError, 'not an address field'):
            relocatable(not_an_address, [])


@unittest.skipUnless(os.environ.get('SELACHE'), 'SELACHE not set: the Selache builds are not checked')
class PackagingTests(unittest.TestCase):
    """kernel_artifact (SDK machines, the dispatcher) and machine_artifact (mk2 synths and filters)."""

    def setUp(self):
        from elekloader.sdk.sharc import selache_tools
        self.tools = selache_tools()
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.work = Path(self.tmp.name)

    def obj(self, source, name='k'):
        path = self.work / (name + '.doj')
        if str(source).endswith('.c'):
            subprocess.run([str(self.tools / 'selcc'), '-proc', 'ADSP-21569', '-char-size-8', '-O1', '-no-std-inc',
                            '-I', str(Path(os.environ['SELACHE']) / 'libsel/include'),
                            '-I', str(ROOT / 'elekloader/sdk/include'), '-c', '-o', str(path), str(source)], check=True)
        else:
            subprocess.run([str(self.tools / 'selas'), '-proc', 'ADSP-21569', '-o', str(path), str(source)], check=True)
        return path

    def source(self, name, text):
        path = self.work / name
        path.write_text(text)
        return path

    def test_kernels_by_label(self):
        from elekloader.sdk.sharc import kernel_artifact
        doc = kernel_artifact([FIXTURES / 'kernels.s'], self.work,
                              [('sdk', 7, 'elek_voice_7.', [(0, 127)]), ('sdk', 9, 'elek_filter_9.', ())])
        self.assertEqual(doc['kernels'], [{'kind': 'sdk', 'id': 7, 'entry': 0, 'knobs': [[0, 127]]},
                                          {'kind': 'sdk', 'id': 9, 'entry': 18, 'knobs': []}])
        self.assertEqual({(r[0], r[3]) for r in doc['relocs']}, {('code', 'code'), ('code', 'data'), ('data', 'data')})
        self.assertEqual(doc['exports'], {})
        json.dumps(doc)
        for kernels, message in (([('sdk', 7, 'missing.', ())], 'no global label missing.'),
                                 ([('sdk', 7, 'probe_out.', ())], 'probe_out. is not a code label'),
                                 ([('sdk', 4, 'elek_voice_7.', ())], 'outside 5..127')):
            with self.subTest(message), self.assertRaisesRegex(ValueError, message):
                kernel_artifact([FIXTURES / 'kernels.s', FIXTURES / 'probe.s'], self.work, kernels)

    def test_the_dispatcher_is_rebuilt_as_committed(self):
        out = subprocess.run([sys.executable, str(ROOT / 'mods/machines-dn2/rebuild_dispatch.py'), '--check'],
                             capture_output=True, text=True)
        self.assertEqual(out.returncode, 0, out.stdout + out.stderr)

    def test_an_mk2_machine_from_c_takes_its_state_size(self):
        from elekloader.sdk.sharc import machine_artifact
        c = self.source('v.c', '#include "digitone-mk2/voice.h"\n'
                        'struct voice { uint32_t phase; uint8_t odd; };\nDN2_VOICE_STATE(struct voice);\n'
                        'void sharc_voice_render(const struct dn2_voice_input *in)\n'
                        '{ struct voice *v = in->state; for (int i = 0; i < 32; ++i) in->output[i] = (float)v->phase++; }\n')
        doc = machine_artifact('voice', [self.obj(c)], 7)
        self.assertEqual(doc['kernels'], [{'kind': 'mk2-voice', 'id': 7, 'entry': 0, 'state_bytes': 8}])
        f = self.source('f.c', '#include "digitone-mk2/filter.h"\n'
                        'void sharc_filter_render(const struct dn2_filter_input *in) { in->samples[0] *= in->g; }\n')
        doc = machine_artifact('filter', [self.obj(f, 'f')], 31)
        self.assertEqual(doc['kernels'], [{'kind': 'mk2-filter', 'id': 31, 'entry': 0}])   # no state: null

    def test_an_mk2_machine_from_assembly_and_refusals(self):
        from elekloader.sdk.sharc import machine_artifact
        doc = machine_artifact('filter', [self.obj(FIXTURES / 'probe.s')], 8)
        self.assertEqual(doc['kernels'][0]['state_bytes'], 8)
        with self.assertRaisesRegex(ValueError, 'voice or filter'):
            machine_artifact('synth', [], 5)
        for text, message in (
                ('.SECTION/SW seg_swco;\n.GLOBAL other.;\nother.:\nRTS;\n', 'no global label sharc_voice_render.'),
                ('.SECTION/SW seg_swco;\n.GLOBAL sharc_voice_render.;\n.EXTERN memcpy.;\nsharc_voice_render.:\n'
                 'CALL memcpy.;\nRTS;\n', 'unresolved symbol: memcpy.'),
                ('.SECTION/SW seg_swco;\n.GLOBAL sharc_voice_render.;\n.GLOBAL sharc_voice_state_bytes.;\n'
                 'sharc_voice_render.:\nsharc_voice_state_bytes.:\nRTS;\n', 'sharc_voice_state_bytes is not a word of data')):
            with self.subTest(message), self.assertRaisesRegex(ValueError, message):
                machine_artifact('voice', [self.obj(self.source('bad.s', text), 'bad')], 5)
        with self.assertRaisesRegex(ValueError, 'mk2-voice id 4 is outside 5..31'):
            machine_artifact('voice', [self.obj(FIXTURES / 'probe.s')], 4)


class PlaceTests(unittest.TestCase):
    def test_jump_encoding_matches_the_stock_jump_form(self):
        self.assertEqual(sharc._jump(0xbc8000), bytes.fromhex('3e06bc000080'))
        self.assertEqual(sharc._jump(0xbd0000), bytes.fromhex('3e06bd000000'))

    def place(self, *mods):
        return sharc.place_kernels([(mid, sharc.kernel_payload(doc, PROFILE)) for mid, doc in mods], PROFILE)

    def test_several_synths_and_filters_from_several_mods(self):
        loads, fills, patches, layout = self.place(
            ('a-synths', kernel(('sdk', 9, {'knobs': [[-12, 12], [0, 2 ** 31 - 1]]}), ('mk2-voice', 5, {'state_bytes': 12}))),
            ('machines-dn2', DISPATCHER),
            ('b-filters', kernel(('mk2-filter', 6), ('mk2-filter', 31, {'state_bytes': 40}))),
            ('c-synth', kernel(('mk2-voice', 31))))
        lo, hi = PROFILE['pool']
        self.assertEqual([m['id'] for m in layout['mods']], ['machines-dn2', 'a-synths', 'b-filters', 'c-synth'])
        self.assertEqual(layout['mods'][0]['code'][0], lo)
        self.assertLessEqual(lo + layout['used'], hi)
        loads = dict(loads)
        pc = {m['id']: sharc.pc16(PROFILE, m['code'][0]) for m in layout['mods']}
        data = {m['id']: m['data'][0] for m in layout['mods']}
        for m in layout['mods'][1:]:                  # each kernel rebased: its jump and its pointer moved
            code = loads[m['code'][0]]
            self.assertEqual(code[:6], sharc._parcels(bytes.fromhex('063e00') + (pc[m['id']] + 2).to_bytes(3, 'big')))
            self.assertEqual(code[-64:], sharc.GUARD)
            self.assertEqual(loads[data[m['id']]][:4], struct.pack('<I', data[m['id']] + 4))
        tables = loads[data['machines-dn2']]
        voice = lambda i: struct.unpack_from('<4I', tables, 16 * i)              # noqa: E731
        filt = lambda i: struct.unpack_from('<4I', tables, 2048 + 16 * i)        # noqa: E731
        state = {(f[0], f[1]) for f in fills}
        sdk, mk2, mk2b = voice(9), voice(5), voice(31)
        self.assertEqual((sdk[0], sdk[2]), (pc['a-synths'] + 2, 256))
        self.assertIn((sdk[1], 16 * 256), state)
        self.assertEqual(loads[sdk[3]], sharc.knob_map([(-12, 12), (0, 2 ** 31 - 1)]))
        self.assertEqual(struct.unpack('<7I', loads[sdk[3]]), (2, 2 ** 32 - 12, 0, 24, 0, 66052, 1023))
        self.assertEqual((mk2[0], mk2[2], mk2[3]), (pc['a-synths'] + 2, 12, 0))
        self.assertIn((mk2[1], 16 * 12), state)
        self.assertEqual(mk2b, (pc['c-synth'] + 2, 0, 0, 0))       # no state: a null pointer
        self.assertEqual(filt(6), (pc['b-filters'] + 2, 0, 0, 0))
        self.assertEqual(filt(31)[2:], (40, 0))
        self.assertIn((filt(31)[1], 16 * 40), state)
        self.assertEqual([voice(i) for i in range(128) if i not in (5, 9, 31)], [(0, 0, 0, 0)] * 125)
        self.assertEqual([filt(i)[0] for i in range(128) if i not in (6, 31)], [0] * 126)
        # The stock sites jump to the dispatcher's hooks; the filter table's type 0 entry names its capture.
        exports = DISPATCHER['exports']
        self.assertEqual(patches, [
            (PROFILE['voice_hook'][0], bytes.fromhex(PROFILE['voice_hook'][1]), sharc._jump(pc['machines-dn2'] + exports['dn2_voice_hook.'][1])),
            (PROFILE['filter_hook'][0], bytes.fromhex(PROFILE['filter_hook'][1]), sharc._jump(pc['machines-dn2'] + exports['dn2_filter_hook.'][1])),
            (PROFILE['filter_capture'][0], bytes.fromhex(PROFILE['filter_capture'][1]),
             struct.pack('<I', pc['machines-dn2'] + exports['dn2_filter_capture.'][1]))])
        for key in ('voice_stack', 'filter_stack'):
            s_lo, s_hi = PROFILE[key]
            self.assertEqual(loads[s_lo], struct.pack('<I', PROFILE['stack_guard']))
            self.assertIn((s_lo + 4, s_hi - s_lo - 4), state)

    def test_the_pool_bounds_the_kernels(self):
        big = kernel(('mk2-voice', 5, {'state_bytes': 4096}), ('mk2-voice', 6, {'state_bytes': 4096}))
        with self.assertRaisesRegex(ModError, 'DN2 kernels need .* bytes; the machine pool has 131072'):
            self.place(('machines-dn2', DISPATCHER), ('big', big))
        self.place(('machines-dn2', DISPATCHER), ('big', kernel(('mk2-voice', 5, {'state_bytes': 4096}))))


class CheckTests(unittest.TestCase):
    """Each of the linker's kernel refusals, refused and passed (tests/test_link_refusals.py)."""
    image = bytes(REL.main_len)

    def check(self, *mods):
        return link.check([CORE, *mods], self.image)

    def test_several_synths_and_filters_from_several_mods_combine(self):
        self.assertEqual(self.check(MACHINES, mod('a', ('sdk', 9), ('mk2-voice', 5)), mod('b', ('mk2-voice', 31)),
                                    mod('c', ('mk2-filter', 6), ('mk2-filter', 7))), [])

    def test_kernels_need_one_dispatcher(self):
        refusal = 'DN2 machine kernels need one dispatcher (machines-dn2); this build has none'
        self.assertIn(refusal, self.check(mod('a', ('mk2-voice', 5))))
        self.assertEqual(self.check(MACHINES, mod('a', ('mk2-voice', 5))), [])
        two = link.Mod2(dict(copy.deepcopy(MACHINES.doc), id='other'), 'other')
        self.assertIn('DN2 machine kernels need one dispatcher (machines-dn2); this build has machines-dn2 0.3, other 0.3',
                      self.check(MACHINES, two, mod('a', ('mk2-voice', 5))))

    def test_kernels_do_not_combine_with_another_sharc_mod(self):
        master = mod('master', sharc_doc=INERT)
        self.assertIn('DN2 machine kernels do not combine with master 0.1',
                      self.check(MACHINES, master, mod('a', ('mk2-voice', 5))))
        self.assertEqual(self.check(MACHINES, master), [])          # the dispatcher alone is inert

    def test_a_build_takes_one_sharc_mod(self):
        self.assertEqual(self.check(mod('m1', sharc_doc=INERT)), [])
        self.assertIn('a build takes one SHARC mod; this one has m1 0.1, m2 0.1',
                      self.check(mod('m1', sharc_doc=INERT), mod('m2', sharc_doc=INERT)))

    def test_an_id_has_one_kernel(self):
        # SDK and mk2 synths share the voice table's ids; filters have their own.
        problems = self.check(MACHINES, mod('a', ('sdk', 9), registers=[]), mod('b', ('mk2-voice', 9), registers=[]))
        self.assertIn('a 0.1 and b 0.1 both have a DN2 voice kernel with id 9', problems)
        self.assertEqual(self.check(MACHINES, mod('a', ('sdk', 9)), mod('b', ('mk2-voice', 10)),
                                    mod('c', ('mk2-filter', 9))), [])

    def test_an_mk2_registration_needs_its_kernel(self):
        for kind, table in (('mk2-voice', 'mk2_voice_synths'), ('mk2-filter', 'mk2_voice_filters')):
            with self.subTest(kind):
                self.assertEqual(self.check(MACHINES, mod('a', (kind, 7))), [])
                self.assertIn('a 0.1: its %s entry 7 needs a SHARC %s kernel with that id' % (table, kind),
                              self.check(MACHINES, mod('a', registers=[(table, 7)])))
                self.assertIn('a 0.1: its %s entry 8 needs a SHARC %s kernel with that id' % (table, kind),
                              self.check(MACHINES, mod('a', (kind, 7), registers=[(table, 7), (table, 8)])))
        # An SDK machine without a kernel plays the device's own sound.
        self.assertEqual(self.check(MACHINES, mod('a', registers=[('sdk_alpha_machines', 40)])), [])

    def test_a_kernel_needs_its_registration_in_its_mod(self):
        for kind in ('sdk', 'mk2-voice', 'mk2-filter'):
            with self.subTest(kind):
                self.assertIn('b 0.1: its SHARC %s kernel 9 has no registration with that id in its mod' % kind,
                              self.check(MACHINES, mod('b', (kind, 9), registers=[])))
        # Registered by another mod, or as the other kind, is not its own.
        problems = self.check(MACHINES, mod('a', registers=[('sdk_alpha_machines', 9)]),
                              mod('b', ('mk2-voice', 9), registers=[('mk2_voice_filters', 9)]))
        self.assertIn('b 0.1: its SHARC mk2-voice kernel 9 has no registration with that id in its mod', problems)
        self.assertEqual(self.check(MACHINES, mod('b', ('sdk', 9))), [])

    def test_refuses_an_invalid_g_and_links_a_valid_one(self):
        refusal = 'g 0.1: DN2 filter G needs a label of 1..5 printable ASCII characters and a default of 0..127'
        for g in (('MODE', 0), ('HIGH5', 127), None):
            self.assertEqual(self.check(MACHINES, mod('g', ('mk2-filter', 8), g=g)), [], g)
        for g in (('MODE', 128), ('TOOWIDE', 1), ('', 1), ('M\tX', 1)):
            self.assertEqual(self.check(MACHINES, mod('g', ('mk2-filter', 8), g=g)), [refusal], g)
        # Several filters in one mod: each is checked.
        self.assertEqual(self.check(MACHINES, mod('g', ('mk2-filter', 8), ('mk2-filter', 9), g=('MODE', 128))),
                         [refusal])


@unittest.skipUnless(STOCK, 'needs the Digitone II OS 1.12 (ELEKLOADER_DN2_SYX)')
class LinkTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.stock, _dev, _rel = formats.load(STOCK)
        cls.image = formats.main_image(cls.stock, DEVICE)
        cls.stream = cls.stock.section(PROFILE['section'])

    def linked(self, *mods):
        return link.link([CORE, *mods], self.image, sharc_stream=self.stream)

    def at(self, stream, address, size=6):
        rec = next(r for r in sharc.parse(stream, PROFILE) if r.flags == sharc.LOAD
                   and r.target <= address and address + size <= r.target + r.count)
        return rec.raw[16 + address - rec.target:16 + address - rec.target + size]

    def test_kernels_from_several_mods_are_placed_in_any_order(self):
        mods = [MACHINES, mod('a', ('sdk', 9), ('mk2-voice', 5)), mod('b', ('mk2-filter', 6), ('mk2-filter', 7))]
        out = self.linked(*mods)
        reverse = link.link(list(reversed(mods)) + [CORE], self.image, sharc_stream=self.stream)
        self.assertEqual(out.sharc_stream, reverse.sharc_stream)
        self.assertEqual(out.layout, reverse.layout)
        self.assertEqual([m['id'] for m in out.layout['sharc']['kernels']['mods']], ['machines-dn2', 'a', 'b'])
        loads, _fills, patches, _layout = sharc.place_kernels([(m.id, m.sharc) for m in mods], PROFILE)
        for site, old, new in patches:
            self.assertEqual(self.at(self.stream, site, len(old)), old)
            self.assertEqual(self.at(out.sharc_stream, site, len(new)), new)
        for address, data in loads:
            self.assertEqual(self.at(out.sharc_stream, address, len(data)), data)
        # The stock master hook is untouched; core-dn2's DSP peek rides along.
        self.assertEqual(self.at(out.sharc_stream, PROFILE['hook'][0]), self.at(self.stream, PROFILE['hook'][0]))
        self.assertEqual(self.at(out.sharc_stream, PROFILE['peek_hook'][0]), bytes.fromhex(PROFILE['peek_hook'][2]))

    def test_a_dispatcher_without_kernels_leaves_the_dsp_stock(self):
        out = self.linked(MACHINES)
        self.assertIsNone(out.sharc_stream)
        self.assertNotIn('sharc', out.layout)


if __name__ == '__main__':
    unittest.main()
