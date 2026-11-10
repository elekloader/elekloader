# SPDX-License-Identifier: GPL-2.0-or-later
"""Unit tests that need no firmware (pytest, or run with python)."""
import json
import os
import random
import struct
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))

from elekloader import devices, elemod, link, syx         # noqa: E402
from elekloader.codec import aplib, elz, transport       # noqa: E402

FRAMING0 = bytes.fromhex('f000203c0a007f010500017200000000f7')[:15] + b'\xf7'
FRAMING1 = bytes.fromhex('f000203c0a007f020500017200000000f7')[:15] + b'\xf7'


def test_requirements_are_ticked_with_a_mod():
    from elekloader.gui import with_requirements
    d = {
        'lib/slicer.elemod': {'id': 'slicer', 'fits': True, 'requires': ['core']},
        'lib/health.elemod': {'id': 'health', 'fits': True, 'requires': ['core']},
        'app/core-2.0a.elemod': {'id': 'core', 'fits': True, 'requires': []},
        'lib/core-1.0.elemod': {'id': 'core', 'fits': False, 'requires': []},
        'lib/opts.elemod': {'id': 'opts', 'fits': True, 'requires': ['core', 'fast']},
    }
    # core comes with the first mod that needs it: the one made for this firmware
    assert with_requirements(d, set(), 'lib/slicer.elemod') == {
        'lib/slicer.elemod', 'app/core-2.0a.elemod'}
    # an enabled core is not doubled
    on = {'lib/slicer.elemod', 'app/core-2.0a.elemod'}
    assert with_requirements(d, on, 'lib/health.elemod') == on | {'lib/health.elemod'}
    # a requirement nothing provides is left for the check to report
    assert with_requirements(d, set(), 'lib/opts.elemod') == {
        'lib/opts.elemod', 'app/core-2.0a.elemod'}
    # a mod that requires nothing adds only itself
    assert with_requirements(d, set(), 'app/core-2.0a.elemod') == {'app/core-2.0a.elemod'}


def test_a_required_mod_comes_in_the_newest_version_the_selection_links_with():
    from elekloader.gui import with_requirements
    # machine-pages 1.3 and newer patch the CC range lookup (0x40010d50) for every machine; OB-1 0.6, made for
    # 1.2, patches it itself
    code = lambda addr, n=6: (addr, n, 'code', '')
    d = {
        'app/core-4.1.elemod': {'id': 'core', 'version': '4.1', 'fits': True, 'requires': [], 'format': 2, 'sites': []},
        'lib/machine-pages-1.2.elemod': {'id': 'pages', 'version': '1.2', 'fits': True, 'requires': ['core'],
                                         'format': 2, 'sites': [code(0x40030000)]},
        'lib/machine-pages-1.7.elemod': {'id': 'pages', 'version': '1.7', 'fits': True, 'requires': ['core'],
                                         'format': 2, 'sites': [code(0x40030000), code(0x40010d50)]},
        'lib/machine-pages-1.10.elemod': {'id': 'pages', 'version': '1.10', 'fits': False, 'requires': ['core'],
                                          'format': 2, 'sites': [code(0x40030000), code(0x40010d50)]},
        'lib/ob1.elemod': {'id': 'ob1', 'fits': True, 'requires': ['core', 'pages'], 'format': 2,
                           'sites': [code(0x40010d50)]},
        'lib/fm.elemod': {'id': 'fm', 'fits': True, 'requires': ['core', 'pages'], 'format': 2,
                          'sites': [code(0x40040000)]},
        'lib/cc.elemod': {'id': 'cc', 'fits': True, 'requires': ['core'], 'format': 2, 'sites': [code(0x40010d52, 2)]},
    }
    core, p12, p17 = 'app/core-4.1.elemod', 'lib/machine-pages-1.2.elemod', 'lib/machine-pages-1.7.elemod'
    ob1, fm, cc = 'lib/ob1.elemod', 'lib/fm.elemod', 'lib/cc.elemod'
    # the newest version made for the stock firmware, for a mod that leaves its places alone
    assert with_requirements(d, set(), fm) == {fm, p17, core}
    # the mod that patches a place 1.7 patches: 1.2, the newest that does not
    assert with_requirements(d, set(), ob1) == {ob1, p12, core}
    # 1.7 is enabled, for another mod: 1.2 in its place, and both mods build
    assert with_requirements(d, {fm, p17, core}, ob1) == {fm, ob1, p12, core}
    # and it stays 1.2 for what is ticked after it
    assert with_requirements(d, {ob1, p12, core}, fm) == {ob1, fm, p12, core}
    # a version enabled that takes the selection is kept: nothing moves to a newer one by itself
    assert with_requirements(d, {p12, core}, fm) == {fm, p12, core}
    # a mod that requires nothing of it and patches its place: the version other mods require gives way too
    assert with_requirements(d, {fm, p17, core}, cc) == {fm, cc, p12, core}
    # no version takes the selection: the one enabled stays, or the newest comes, for the check to say why
    d[p12]['sites'].append(code(0x40010d50))
    assert with_requirements(d, {fm, p17, core}, ob1) == {fm, ob1, p17, core}
    assert with_requirements(d, set(), ob1) == {ob1, p17, core}
    # versions as numbers (1.10 after 1.7), once 1.10 is made for the stock firmware
    d['lib/machine-pages-1.10.elemod']['fits'] = True
    assert with_requirements(d, set(), fm) == {fm, 'lib/machine-pages-1.10.elemod', core}


def test_versions_sort_and_the_core_a_build_takes():
    order = ['', '0.13a', '0.13b', '2', '2.0', '2.0a', '2.1', '2.9', '2.10', '3.0', '10.0']
    assert sorted(reversed(order), key=elemod.version_key) == order
    # the newest, whatever the mods need: each core keeps all of the one before it
    assert elemod.pick_core(['2.1', '3.0', '2.0a']) == 1
    assert elemod.pick_core(['2.1', '3.0', '2.0a'], '2.1') == 1
    assert elemod.pick_core(['2.1', '3.0', '3.1'], '3.0') == 2
    assert elemod.pick_core(['2.1', '2.1']) == 1                 # the last of equals
    assert elemod.pick_core(['2.1', '3.0'], '3.1') is None
    assert elemod.pick_core(['2.0a', '2.2', '2.3', '3.0']) == 3
    assert elemod.pick_core(['2.0a', '2.2', '2.10', '2.3']) == 2   # versions as numbers
    assert elemod.pick_core([]) is None
    # fits: whether each core's RAM holds the mods (the Digitone's 4.0 has 1.25 MB, 2.3 and 3.2 128 KB)
    dn = ['2.3', '3.2', '4.0', '4.1']
    assert elemod.pick_core(dn, None, [True] * 4) == 3
    assert elemod.pick_core(dn, '3.2', [True] * 4) == 3
    assert elemod.pick_core(dn, None, [True, True, False, False]) == 1     # the newest that holds them
    assert elemod.pick_core(dn, '3.2', [True, False, False, False]) == 3  # none new enough holds: the newest
    assert elemod.pick_core(dn, '4.1', [True, True, False, True]) == 3
    assert elemod.pick_core(dn, '5.0', [True] * 4) is None


def test_a_selection_too_big_for_its_core_takes_one_with_more_ram():
    from elekloader.gui import one_core, with_requirements
    kb = 1024
    d = {
        'app/core-dn1-2.3.elemod': {'id': 'core', 'version': '2.3', 'fits': True, 'requires': [],
                                    'ram_bound': 4 * kb, 'ram_area': 128 * kb},
        'app/core-dn1-3.2.elemod': {'id': 'core', 'version': '3.2', 'fits': True, 'requires': [],
                                    'ram_bound': 6 * kb, 'ram_area': 128 * kb},
        'app/core-dn1-4.0.elemod': {'id': 'core', 'version': '4.0', 'fits': True, 'requires': [],
                                    'ram_bound': 6 * kb, 'ram_area': 1280 * kb},
        'lib/tiny.elemod': {'id': 'tiny', 'fits': True, 'requires': ['core'], 'ram_bound': kb},
        'lib/whole.elemod': {'id': 'whole', 'fits': True, 'format': 1},
        'lib/huge.elemod': {'id': 'huge', 'fits': True, 'requires': ['core'], 'needs_core': '3.2',
                            'ram_bound': 2000 * kb},
    }
    for i in range(1, 6):                          # five pedals of 30 KB that need core 3.2
        d['lib/pedal%d.elemod' % i] = {'id': 'pedal%d' % i, 'fits': True, 'requires': ['core'],
                                       'needs_core': '3.2', 'ram_bound': 30 * kb}
    c23, c32, c40 = 'app/core-dn1-2.3.elemod', 'app/core-dn1-3.2.elemod', 'app/core-dn1-4.0.elemod'
    pedals = ['lib/pedal%d.elemod' % i for i in range(1, 6)]
    # a build takes the newest core, 4.0, for a small selection too
    on = set()
    for k, p in enumerate(pedals):
        on = with_requirements(d, on, p)
        assert on == set(pedals[:k + 1]) | {c40}, on
    assert with_requirements(d, set(), 'lib/tiny.elemod') == {'lib/tiny.elemod', c40}
    # a core chosen by hand that will do is kept
    assert with_requirements(d, {c32}, pedals[0]) == {c32, pedals[0]}
    assert with_requirements(d, {c23}, 'lib/tiny.elemod') == {c23, 'lib/tiny.elemod'}
    # with 3.2 chosen by hand: up to 120 KB + 3.2's 6 KB, 3.2 holds them; 150 KB, 4.0 in its place
    on = {c32}
    for k, p in enumerate(pedals[:4]):
        on = with_requirements(d, on, p)
        assert on == set(pedals[:k + 1]) | {c32}, on
    assert with_requirements(d, on, pedals[4]) == set(pedals) | {c40}
    # exactly 3.2's area: 3.2 is kept; a byte more: 4.0
    d['lib/edge.elemod'] = {'id': 'edge', 'fits': True, 'requires': ['core'], 'needs_core': '3.2',
                            'ram_bound': 122 * kb}
    assert with_requirements(d, {c32}, 'lib/edge.elemod') == {'lib/edge.elemod', c32}
    d['lib/edge.elemod']['ram_bound'] += 1
    assert with_requirements(d, {c32}, 'lib/edge.elemod') == {'lib/edge.elemod', c40}
    # held by no core: the newest, or the one chosen by hand, for the check to say how much RAM is over
    assert with_requirements(d, set(), 'lib/huge.elemod') == {'lib/huge.elemod', c40}
    assert with_requirements(d, {c32}, 'lib/huge.elemod') == {'lib/huge.elemod', c32}
    # a file with no ram_bound (a whole build, an unreadable file) counts as nothing
    assert with_requirements(d, {'lib/whole.elemod', c23}, 'lib/tiny.elemod') == {
        'lib/whole.elemod', 'lib/tiny.elemod', c23}
    # the first run's ticks: one core, the newest
    assert one_core(d, {c23, c32, c40} | set(pedals)) == set(pedals) | {c40}
    assert one_core(d, {c23, c32, c40, 'lib/tiny.elemod'}) == {c40, 'lib/tiny.elemod'}


def test_a_mod_that_patches_a_place_the_newest_core_took_takes_the_newest_core_that_leaves_it():
    from elekloader.gui import one_core, with_requirements
    # the Digitakt's core 4.1 took the master output's place (0x400721e6) for every mod; a mod made for 4.0
    # (Tone+FX 4.0c, Digi EQ 1.0c) still patches it itself
    code = lambda addr, n=6: (addr, n, 'code', '')
    d = {
        'app/core-4.0.elemod': {'id': 'core', 'version': '4.0', 'fits': True, 'requires': [], 'format': 2,
                                'sites': [code(0x40010000)]},
        'app/core-4.1.elemod': {'id': 'core', 'version': '4.1', 'fits': True, 'requires': [], 'format': 2,
                                'sites': [code(0x40010000), code(0x400721e6)]},
        'lib/tone.elemod': {'id': 'tone', 'fits': True, 'requires': ['core'], 'format': 2,
                            'sites': [code(0x400721e6), code(0x40184628, 4)]},
        'lib/health.elemod': {'id': 'health', 'fits': True, 'requires': ['core'], 'format': 2,
                              'sites': [code(0x40020000, 4)]},
        'lib/before.elemod': {'id': 'before', 'fits': True, 'requires': ['core'], 'format': 2,
                              'sites': [code(0x400721e0)]},
        'lib/master.elemod': {'id': 'master', 'fits': True, 'requires': ['core'], 'format': 2,
                              'needs_core': '4.1', 'sites': []},
        'lib/whole.elemod': {'id': 'whole', 'fits': True, 'format': 1, 'sites': [code(0x400721e6)]},
    }
    c40, c41 = 'app/core-4.0.elemod', 'app/core-4.1.elemod'
    tone, health, master = 'lib/tone.elemod', 'lib/health.elemod', 'lib/master.elemod'
    # the newest core, for a mod that leaves its places alone
    assert with_requirements(d, set(), health) == {health, c41}
    # the mod that patches a place 4.1 patches: 4.0, the newest core that does not, also in 4.1's place
    assert with_requirements(d, set(), tone) == {tone, c40}
    assert with_requirements(d, {c41}, tone) == {tone, c40}
    assert with_requirements(d, {c41, health}, tone) == {tone, health, c40}
    # and 4.0 stays for what is ticked after it
    assert with_requirements(d, {tone, c40}, health) == {tone, health, c40}
    # a site that ends where the core's begins is beside it, not on it; a byte further is on it
    assert with_requirements(d, {c41}, 'lib/before.elemod') == {'lib/before.elemod', c41}
    d['lib/before.elemod']['sites'] = [code(0x400721e1)]
    assert with_requirements(d, {c41}, 'lib/before.elemod') == {'lib/before.elemod', c40}
    # with a mod that needs 4.1, no core takes both: the newest that is new enough, for the check to say why
    assert with_requirements(d, {tone, c40}, master) == {tone, master, c41}
    assert with_requirements(d, set(), master) == {master, c41}
    # a whole build goes with no core at all (the check says so): what it patches moves no core
    assert with_requirements(d, {'lib/whole.elemod', c41}, health) == {'lib/whole.elemod', health, c41}
    # the first run's ticks: one core, the newest that takes the mods
    assert one_core(d, {c40, c41, tone, health}) == {tone, health, c40}
    assert one_core(d, {c40, c41, health}) == {health, c41}


def test_a_mod_that_needs_core_3_ticks_core_3():
    from elekloader.gui import one_core, with_requirements
    d = {
        'app/core-2.1.elemod': {'id': 'core', 'version': '2.1', 'fits': True, 'requires': []},
        'app/core-3.0.elemod': {'id': 'core', 'version': '3.0', 'fits': True, 'requires': []},
        'lib/health.elemod': {'id': 'health', 'fits': True, 'requires': ['core']},
        'lib/pages.elemod': {'id': 'pages', 'fits': True, 'requires': ['core'],
                             'needs_core': '3.0'},
        'lib/nb.elemod': {'id': 'nb', 'fits': True, 'requires': ['core', 'pages'],
                          'needs_core': '3.0'},
        'lib/later.elemod': {'id': 'later', 'fits': True, 'requires': ['core'],
                             'needs_core': '4.0'},
    }
    c21, c30 = 'app/core-2.1.elemod', 'app/core-3.0.elemod'
    # what needs nothing from 3.0 builds on 3.0 too: the newest core
    assert with_requirements(d, set(), 'lib/health.elemod') == {'lib/health.elemod', c30}
    # a core chosen by hand that will do is kept
    assert with_requirements(d, {c21}, 'lib/health.elemod') == {c21, 'lib/health.elemod'}
    assert with_requirements(d, {c30}, 'lib/health.elemod') == {c30, 'lib/health.elemod'}
    # what needs 3.0 brings it, through its requirements too, in place of 2.1 chosen by hand
    assert with_requirements(d, {'lib/health.elemod', c21}, 'lib/nb.elemod') == {
        'lib/health.elemod', 'lib/nb.elemod', 'lib/pages.elemod', c30}
    # none new enough: the newest, for the check to say why
    assert with_requirements(d, set(), 'lib/later.elemod') == {'lib/later.elemod', c30}
    assert with_requirements(d, {c21}, 'lib/later.elemod') == {'lib/later.elemod', c21}
    # the first run's ticks: one core, the newest
    assert one_core(d, {c21, c30, 'lib/health.elemod'}) == {c30, 'lib/health.elemod'}
    assert one_core(d, {c21, c30, 'lib/pages.elemod'}) == {c30, 'lib/pages.elemod'}


def test_a_digitone_machine_ticks_machines_and_its_core():
    """The shop's machines-dn1 (id machines, needs core-dn1 3.1) comes with a
    machine mod that requires it, and the core the shop lists that will do
    (3.2, not 2.3) with them."""
    from elekloader.gui import with_requirements
    d = {
        'app/core-dn1-2.3.elemod': {'id': 'core', 'version': '2.3', 'fits': True, 'requires': []},
        'app/core-dn1-3.2.elemod': {'id': 'core', 'version': '3.2', 'fits': True, 'requires': []},
        'lib/machines-0.1.elemod': {'id': 'machines', 'fits': True, 'requires': ['core'],
                                    'needs_core': '3.1'},
        'lib/dnsine-1.0.elemod': {'id': 'dnsine', 'fits': True, 'requires': ['core', 'machines']},
        'lib/tables.elemod': {'id': 'tables', 'fits': True, 'requires': ['core']},
    }
    c23, c32 = 'app/core-dn1-2.3.elemod', 'app/core-dn1-3.2.elemod'
    assert with_requirements(d, set(), 'lib/dnsine-1.0.elemod') == {
        'lib/dnsine-1.0.elemod', 'lib/machines-0.1.elemod', c32}
    # beside 2.3 chosen by hand, the machine brings machines and moves the build to 3.2
    assert with_requirements(d, {'lib/tables.elemod', c23}, 'lib/dnsine-1.0.elemod') == {
        'lib/tables.elemod', 'lib/dnsine-1.0.elemod', 'lib/machines-0.1.elemod', c32}


def test_8in7_round_trip():
    rnd = random.Random(1)
    for n in (0, 1, 6, 7, 8, 13, 14, 101, 250):
        data = bytes(rnd.getrandbits(8) for _ in range(n))
        enc = transport.encode_8in7(data)
        out, k = bytearray(), 0
        while k < len(enc):
            ms = enc[k]
            k += 1
            for m in range(7):
                if k >= len(enc):
                    break
                out.append(enc[k] | (0x80 if (ms >> (6 - m)) & 1 else 0))
                k += 1
        assert bytes(out) == data and all(b < 0x80 for b in enc)


def test_packer_round_trip():
    rnd = random.Random(2)
    cases = [b'', b'A', bytes(range(256)) * 3, b'\x00' * 5000,
             bytes(rnd.getrandbits(8) for _ in range(3000)),
             (b'ColdFire' * 400 + bytes(rnd.getrandbits(8) for _ in range(700))) * 3]
    for data in cases:
        packed = aplib.pack_section(data)
        assert syx.classify(packed) == 'packed'
        assert elz.depack_section(packed) == data


def test_packer_reaches_past_64_kib():
    state, data = 7, bytearray()
    for _ in range(70000):
        state = (1664525 * state + 1013904223) & 0xFFFFFFFF
        data.append(state >> 24)
    data += data[:4096]
    packed = aplib.pack_section(data)
    assert elz.depack_section(packed) == data
    assert len(packed) < 79000       # literals for the distant repeat would exceed 83 KB


def _container(sections):
    """[(id, dest, stored)] -> a container laid out like the mk1 files."""
    cont = bytearray(0x80)
    cont[:4] = b'ELE3'
    cont[0x14:0x18] = b'1.00'
    struct.pack_into('>I', cont, 0x1C, len(sections))
    for i, (sid, dest, data) in enumerate(sections):
        struct.pack_into('>IIII', cont, 0x20 + 16 * i, sid, len(cont), len(data), dest)
        cont += data
        cont += bytes(-len(cont) % 16)
    return bytes(cont)


def test_transport_and_parser_round_trip():
    body = b'main os image ' * 300
    cont = _container([(5, 0, b'meta'), (3, 0x40000400, aplib.pack_section(body)),
                       (4, 0x80000400, struct.pack('>II', 16, 0) + bytes(range(1, 17)))])
    stream = struct.pack('>II', len(cont), transport.content_checksum(cont)) + cont
    raw = transport.encode_syx(stream, 0x0A, FRAMING0, FRAMING1)
    s = syx.Syx(raw)
    assert s.container == cont and not any(s.tail)
    assert [t[0] for t in s.table] == [5, 3, 4]
    assert s.section(3) == body and s.section(4) == bytes(range(1, 17)) and s.version == '1.00'
    for m in s.data:
        assert transport.packet_checksum(m[1:-1], s.k) == m[-2]
    n = (s.framing[0][12] << 14) | (s.framing[0][13] << 7) | s.framing[0][14]
    assert n == len(s.data)


def test_a_longer_section_table_moves_the_data_start():
    """Seven sections (the Digitone mk1's) end the table at 0x90, and the data
    starts at 0xA0: the reader finds it there and the writer keeps it."""
    body = b'another main os ' * 300
    secs = [(5, 0, b'meta'), (2, 0x02000802, b'boot' * 10), (3, 0x40000400, aplib.pack_section(body)),
            (4, 0x80000400, b'upd' * 7), (6, 0, b'six'), (7, 0, b'dsp' * 40), (8, 0, b'eight')]
    cont = bytearray(0xA0)
    cont[:4] = b'ELE3'
    cont[0x14:0x18] = b'1.43'
    struct.pack_into('>I', cont, 0x1C, len(secs))
    for i, (sid, dest, data) in enumerate(secs):
        struct.pack_into('>IIII', cont, 0x20 + 16 * i, sid, len(cont), len(data), dest)
        cont += data
        cont += bytes(-len(cont) % 16)
    cont = bytes(cont)
    stream = struct.pack('>II', len(cont), transport.content_checksum(cont)) + cont
    raw = transport.encode_syx(stream, 0x0A, FRAMING0, FRAMING1)     # the framing's device byte
    s = syx.Syx(raw)
    assert s.data_start == 0xA0 and s.section(3) == body and [t[0] for t in s.table] == [5, 2, 3, 4, 6, 7, 8]
    dev = devices.devices()[0]
    assert syx.write(s, s.stored[3], dev) == raw
    out = syx.write(s, aplib.pack_section(body + b'!'), dev, 'ABCD')
    o = syx.Syx(out)
    assert o.data_start == 0xA0 and o.section(3) == body + b'!' and o.version == 'ABCD'
    assert all(o.stored[k] == s.stored[k] for k in (5, 2, 4, 6, 7, 8))


def test_the_update_limit_counts_the_preamble_and_is_optional():
    from dataclasses import replace
    body = b'main os image ' * 300
    cont = _container([(5, 0, b'meta'), (3, 0x40000400, aplib.pack_section(body))])
    stream = struct.pack('>II', len(cont), transport.content_checksum(cont)) + cont
    raw = transport.encode_syx(stream, 0x0A, FRAMING0, FRAMING1)
    st = syx.Syx(raw)
    dev = devices.devices()[0]
    for limit in (None, len(stream), len(stream) + 1):
        syx.verify(raw, st, body, replace(dev, update_max=limit))
    for limit in (len(cont), len(stream) - 1):
        try:
            syx.verify(raw, st, body, replace(dev, update_max=limit))
        except syx.SyxError as e:
            assert str(e) == ('decoded update stream %d bytes exceeds the measured USB update limit of %d bytes'
                              % (len(stream), limit))
        else:
            raise AssertionError('accepted an update over the limit')
    assert [(d.key, d.update_max) for d in devices.devices() if d.update_max is not None] == [
        ('digitone-mk2', 0x200000)]


def test_the_main_os_is_stored_padded_to_4():
    """Elektron stores a packed section as its header and stream, then zeros
    to a multiple of 4; the table's length counts them, the header's does not
    (issue #59). pack_main pads the same way; the Octatrack's family does not
    pad, and write() keeps whatever stream it is given."""
    from elekloader import elek, formats
    body = b'main os image ' * 300
    s = syx.Syx(transport.encode_syx(
        (lambda c: struct.pack('>II', len(c), transport.content_checksum(c)) + c)(
            _container([(5, 0, b'meta'), (3, 0x40000400, aplib.pack_section(body))])),
        0x0A, FRAMING0, FRAMING1))
    dev = devices.devices()[0]
    seen = set()
    for extra in range(8):
        img = body + b'!' * extra
        packed = aplib.pack_section(img)
        seen.add(len(packed) % 4)
        stored = syx.pack_main(img)
        assert formats.pack_main(img, dev) == stored
        assert len(stored) % 4 == 0 and stored[:len(packed)] == packed
        assert stored[len(packed):] == bytes(len(stored) - len(packed))
        o = syx.Syx(syx.write(s, stored, dev))
        assert o.stored[3] == stored and o.section(3) == img
        assert elek.pack_main(img) == packed
    assert len(seen) > 1, 'the cases do not reach an unaligned stream'


def test_elek_transport_and_card_file_round_trip():
    from elekloader import elek
    body = b'octatrack main os ' * 400
    sec = aplib.pack_section(body)
    for extra in (0, 1, 2, 62, 63, 64):                 # every short-final-message shape
        cont = elek.MAGIC + b'0178' + b'     1.00X' + sec + bytes(extra)
        raw = elek.encode_syx(cont, 0x05)
        dev_id, back = elek.decode_syx(raw)
        assert dev_id == 5 and back == cont
        seed, padded = elek.decode_bin(elek.encode_bin(cont, 0x12345678))
        assert seed == 0x12345678 and padded == cont + bytes(-len(cont) % 4)
    f = elek.ElekFile(elek.encode_syx(elek.MAGIC + b'0178' + b'     1.00X' + sec, 5))
    assert f.section(3) == body and f.version == '1.00X' and f.build == '0178'


def test_a_zip_without_a_known_os_is_refused():
    import io
    import zipfile
    from elekloader import formats
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, 'w') as z:
        z.writestr('dist/Some_OS.syx', b'\xf0\x00\x20\x3c\xf7')
        z.writestr('dist/readme.txt', b'hello')
    try:
        formats.load(buf.getvalue())
    except devices.UnknownFirmware as e:
        assert 'holds no stock firmware' in str(e) and 'dist/Some_OS.syx' in str(e)
    else:
        raise AssertionError('accepted a zip with no known OS file')
    try:
        formats.load(b'PK\x03\x04' + bytes(100))
    except formats.FormatError as e:
        assert 'zip' in str(e)
    else:
        raise AssertionError('accepted a broken zip')


def test_inplace_depack_gap():
    body = bytes(random.Random(3).getrandbits(8) for _ in range(20000))
    dev = devices.devices()[0]
    img, gap = syx.inplace_depack(aplib.pack_section(body), dev)
    assert img == body and gap > 0


def test_overlaps():
    spans = [(0, 100, 'a'), (10, 20, 'b'), (50, 60, 'c'), (100, 110, 'd')]
    pairs = {(a[2], b[2]) for a, b in elemod.overlaps(spans)}
    assert pairs == {('a', 'b'), ('a', 'c')}


def test_bytes_site_relocs():
    """A `bytes` site's relocs: well-formed ones pass, anything else is a
    BuildError naming the site, not a crash."""
    from elekloader.sdk import build
    new = bytes(12)
    ok = [[0, 'abs32', 'sym:a', 0], [8, 'abs32', 'abs', 0x10]]
    assert build.bytes_relocs(0x40001000, new, ok) == ok
    for bad in ([[0, 'abs32', 'sym:a']], [['0', 'abs32', 'sym:a', 0]], [[0, 'abs32', 'sym:a', '1']],
                [[True, 'abs32', 'sym:a', 0]], [[9, 'abs32', 'sym:a', 0]], [[-1, 'abs32', 'sym:a', 0]],
                [[0, 'pc32', 'sym:a', 0]], [[0, 'abs32', 'absfoo', 0]], [[0, 'abs32', 'sym:', 0]],
                [[0, 'abs32', 3, 0]], [[0, 'abs32', 'sym:a', 0], [2, 'abs32', 'sym:b', 0]],
                ['x'], {'0': 1}):
        try:
            build.bytes_relocs(0x40001000, new, bad)
        except build.BuildError as e:
            assert '0x40001000' in str(e), e
        else:
            raise AssertionError('accepted %r' % (bad,))


def test_devices():
    d, r = devices.identify(devices.devices()[0].releases['1.53'].syx_sha256)
    assert d.key == 'digitakt-mk1' and r.version == '1.53'
    assert d.image_end(r) == 0x4025CA40
    assert devices.for_target(devices.target_of(d, r)) == (d, r)
    for bad in ('0' * 64, ''):
        try:
            devices.identify(bad)
        except devices.UnknownFirmware:
            pass
        else:
            raise AssertionError('accepted an unknown firmware')
    try:
        devices.for_target({'syx_sha256': '0' * 64})
    except devices.UnknownFirmware:
        pass
    else:
        raise AssertionError('accepted an unknown target')


def test_dsp_payload_walk():
    """dsp.records on a made-up payload: records, two-word directives anywhere, the end; and
    the profiles' DSP areas lie inside their payloads' tags."""
    import dataclasses
    from elekloader import dsp
    from elekloader.elemod import ModError

    def w(*ws):
        return b''.join(bytes((x & 0xFF, (x >> 8) & 0xFF, x >> 16)) for x in ws)
    ot = [d for d in devices.devices() if d.key == 'octatrack'][0]
    body = w(3, 0x3000, 4, 0, 0, 0x10, 2, 0x111111, 0x222222, 1, 0x20, 1, 0x333333, 3, 0x3000)
    lo = ot.main_load + 0x10
    image = bytes(0x10) + body + w(0x123456)
    dev = dataclasses.replace(ot, dsp_payloads={'T': (lo, len(body))})
    assert dsp.records(image, dev, 'T') == [(0, 0x10, 2, lo + 6 * 2 + 9), (1, 0x20, 1, lo + 27 + 9)]
    assert dsp.p_span(image, dev, 'T', 0x11, 0x12) == lo + 21 + 3
    for bad, what in ((w(0, 0x10, 5, 1), 'does not parse at +0x0'), (w(3, 1, 0), 'at +0x6')):
        d = dataclasses.replace(ot, dsp_payloads={'T': (lo, len(bad))})
        try:
            dsp.records(bytes(0x10) + bad, d, 'T')
        except ModError as e:
            assert what in str(e), str(e)
        else:
            raise AssertionError('parsed %s' % bad.hex())
    early = w(0, 0x10, 1, 7, 5, 0xFFFFFF)                   # a word above 4 ends it
    d = dataclasses.replace(ot, dsp_payloads={'T': (lo, len(early))})
    assert dsp.records(bytes(0x10) + early, d, 'T') == [(0, 0x10, 1, lo + 9)]
    for d in devices.devices():
        for tag, a, b, nm, _what in d.dsp_areas:
            assert tag in d.dsp_payloads and a < b and nm.startswith('dsp:'), (d.key, nm)
    assert [d.key for d in devices.devices() if d.dsp_payloads] == ['octatrack']


def test_each_release_is_known_by_its_own_hash():
    """Every release of every profile is identified as itself, and its .elemod
    target names it alone."""
    seen = set()
    for d in devices.devices():
        for v, r in d.releases.items():
            assert r.version == v and r.syx_sha256 not in seen
            seen.add(r.syx_sha256)
            assert devices.identify(r.syx_sha256) == (d, r)
            assert devices.for_target(devices.target_of(d, r)) == (d, r)
    dt = devices.devices()[0]
    assert sorted(dt.releases) == ['1.53', '1.54']
    assert dt.image_end(dt.releases['1.54']) == 0x4025DA40


def test_a_mod_json_port_is_built_for_its_os():
    from elekloader.sdk import build
    dt = [d for d in devices.devices() if d.key == 'digitakt-mk1'][0]
    dn = [d for d in devices.devices() if d.key == 'digitone-mk1'][0]
    mod = {'id': 'm', 'version': '1', 'device': 'digitakt-mk1', 'os': '1.53',
           'defsym': {'X': '0x1'}, 'sites': ['a'],
           'ports': {'1.54': {'defsym': {'X': '0x2'}}}}
    got, ported = build.for_release(mod, dt, dt.releases['1.53'])
    assert got is mod and not ported
    got, ported = build.for_release(mod, dt, dt.releases['1.54'])
    assert ported and got['os'] == '1.54' and got['defsym'] == {'X': '0x2'}
    assert got['sites'] == ['a'] and mod['defsym'] == {'X': '0x1'}   # the rest kept, mod unchanged

    def refused(m, d, r, *words):
        try:
            build.for_release(m, d, r)
        except build.BuildError as e:
            assert all(w in str(e) for w in words), e
        else:
            raise AssertionError('built')
    refused(mod, dn, dn.releases['1.43'], 'digitakt-mk1 1.53 (ports: 1.54)', 'digitone-mk1 1.43')
    refused(dict(mod, ports={}), dt, dt.releases['1.54'], 'is for digitakt-mk1 1.53;')
    refused(dict(mod, ports={'1.54': {'id': 'other'}}), dt, dt.releases['1.54'], 'changes id')
    refused(dict(mod, ports={'1.54': ['defsym']}), dt, dt.releases['1.54'], '"ports"')


def test_section_alignment():
    """A section's alignment is a power of two up to 4,096 (USB descriptors want 32)."""
    d, r = devices.identify(devices.devices()[0].releases['1.53'].syx_sha256)

    def mod(align):
        return {'elemod': 2, 'id': 'a', 'version': '1', 'target': devices.target_of(d, r),
                'sections': {'.run': {'align': align, 'len': 4, 'parts': [['hex', '4e714e71']]}}}
    for ok in (1, 2, 4, 8, 16, 32, 4096):
        assert link.Mod2(mod(ok), 'a').sections['.run']['align'] == ok
    for bad in (0, 3, 24, 8192):
        try:
            link.Mod2(mod(bad), 'a')
        except elemod.ModError as e:
            assert 'alignment' in str(e)
            continue
        raise AssertionError('accepted alignment %d' % bad)


def test_a_parameter_slot_claims_its_sound_slot():
    """On the Digitone a mod that adds a parameter slot (core-dn1's core_params) claims the sound
    slot its descriptor names at +8, as soundslot:<n>, so two mods that store in one of a
    sound's slots never link together, whether their files say so or not (digitables 1.3's
    does not). The descriptor's bytes may be stock parts, read from the image."""
    dn = [d for d in devices.devices() if d.key == 'digitone-mk1'][0]
    r = dn.releases['1.43']
    image = bytearray(r.main_len)
    image[0x100:0x104] = (65).to_bytes(4, 'big')        # a long a descriptor shares with the stock

    def mod(i, parts, names=(), relocs=()):
        return link.Mod2({'elemod': 2, 'id': i, 'version': '1', 'target': devices.target_of(dn, r),
                          'sections': {'.run': {'align': 4, 'parts': parts}},
                          'symbols': {'p': ['.run', 0]}, 'relocs': [list(x) for x in relocs],
                          'contribute': [{'to': 'core_params', 'data': '00000000',
                                          'relocs': [[0, 'abs32', 'sym:p', 0]]}],
                          'resources': {'names': list(names)}}, i)

    def desc(slot):                                     # id 182, group 1, the slot, 56 more bytes
        return [['hex', '000000b6' + '00000001' + '%08x' % slot + '00' * 56]]
    a, b = mod('a', desc(26)), mod('b', desc(26))
    assert a.implied_names(bytes(image)) == ['soundslot:26'] and a.names == []
    problems = link.check([a, b], bytes(image))
    assert 'a 1 and b 1 both claim soundslot:26' in problems, problems
    assert a.names == ['soundslot:26']                  # check() adds it
    # a slot another mod claims itself (machines: soundslot:0)
    problems = link.check([mod('p', desc(0)), mod('m', desc(26), names=['soundslot:0'])], bytes(image))
    assert 'p 1 and m 1 both claim soundslot:0' in problems, problems
    # the descriptor's slot in a stock part
    c = mod('c', [['hex', '000000b7' + '00000001'], ['stock', '0x%08x' % (dn.main_load + 0x100), 4],
                  ['hex', '00' * 56]])
    assert c.implied_names(bytes(image)) == ['soundslot:65']
    # not a slot: past the 79, or a long the linker relocates
    assert mod('d', desc(79)).implied_names(bytes(image)) == []
    assert mod('e', desc(26), relocs=[('.run', 8, 'abs32', 'abs', 26)]).implied_names(bytes(image)) == []
    # the Digitakt mk1's profile names no such table
    dt = devices.devices()[0]
    t = link.Mod2({'elemod': 2, 'id': 't', 'version': '1', 'target': devices.target_of(dt, dt.releases['1.53']),
                   'sections': {'.run': {'align': 4, 'parts': desc(26)}}, 'symbols': {'p': ['.run', 0]},
                   'contribute': [{'to': 'core_params', 'data': '00000000',
                                   'relocs': [[0, 'abs32', 'sym:p', 0]]}]}, 't')
    assert t.implied_names(b'') == []



def test_a_machine_claims_its_slot():
    """On the Analog Rytm MKII a mod that adds a machine (core-ar2 0.5's core_machines) claims
    the machine slot its descriptor names at +0, as machine:<n>, so two mods that take one
    slot never link together. Slots are 0..52."""
    ar = [d for d in devices.devices() if d.key == 'analog-rytm-mk2'][0]
    r = ar.releases['1.74']
    image = bytes(r.main_len)

    def mod(i, slot):
        return link.Mod2({'elemod': 2, 'id': i, 'version': '1', 'target': devices.target_of(ar, r),
                          'sections': {'.run': {'align': 4, 'parts': [['hex', '%08x' % slot + '00' * 92]]}},
                          'symbols': {'m': ['.run', 0]},
                          'contribute': [{'to': 'core_machines', 'data': '00000000',
                                          'relocs': [[0, 'abs32', 'sym:m', 0]]}]}, i)
    a, b = mod('a', 36), mod('b', 36)
    assert a.implied_names(image) == ['machine:36']
    assert 'a 1 and b 1 both claim machine:36' in link.check([a, b], image)
    assert not [p for p in link.check([mod('c', 36), mod('d', 37)], image) if 'claim' in p]
    assert mod('e', 53).implied_names(image) == []

def test_knob_id_ranges_on_the_rytm():
    """On the Analog Rytm MKII (core-ar2 0.11) the ids a core_params or core_param_locks entry
    answers for (its first two longs) may not take 469..554, core's lockable parameters' ids;
    a core_param_locks entry takes only ids from 555, and two mods may not share one of those.
    One mod's own entries may, and two mods' entries for stock ids (below 469) may."""
    ar = [d for d in devices.devices() if d.key == 'analog-rytm-mk2'][0]
    r = ar.releases['1.74']
    image = bytes(r.main_len)

    def mod(i, *entries):
        parts = ''.join('%08x%08x' % (a, b) for _t, a, b in entries)
        return link.Mod2({'elemod': 2, 'id': i, 'version': '1', 'target': devices.target_of(ar, r),
                          'sections': {'.run': {'align': 4, 'parts': [['hex', parts]]}},
                          'symbols': {'e%d' % n: ['.run', 8 * n] for n in range(len(entries))},
                          'contribute': [{'to': t, 'data': '00000000',
                                          'relocs': [[0, 'abs32', 'sym:e%d' % n, 0]]}
                                         for n, (t, _a, _b) in enumerate(entries)]}, i)

    def ids(problems):
        return [p for p in problems if 'ids' in p]
    a = mod('a', ('core_params', 560, 573), ('core_param_locks', 560, 573))
    assert a.id_ranges(image) == [('core_params', 560, 573), ('core_param_locks', 560, 573)]
    assert ids(link.check([a, mod('b', ('core_params', 1, 4)), mod('c', ('core_params', 1, 4))], image)) == []
    assert ids(link.check([mod('s', ('core_params', 470, 483))], image)) == [
        "s 1: its core_params entry takes ids 470..483, and 469..554 are core's lockable parameters' "
        "ids; a mod's own ids start at 555"]
    assert ids(link.check([mod('l', ('core_param_locks', 400, 460))], image)) == [
        "l 1: its core_param_locks entry takes ids 400..460; that table's ids start at 555"]
    assert ids(link.check([a, mod('d', ('core_param_locks', 500, 561))], image))[0].startswith(
        'd 1: its core_param_locks entry takes ids 500..561, and 469..554')
    assert ids(link.check([a, mod('e', ('core_param_locks', 570, 600))], image)) == [
        'a 1 and e 1 both take ids 570..573 (core_params, core_param_locks)',
        'a 1 and e 1 both take ids 570..573 (core_param_locks, core_param_locks)']
    assert mod('f', ('core_params', 9, 3)).id_ranges(image) == []


def test_mk1_machines_claim_their_ids():
    """On the Digitakt mk1 a machine (core_machines) claims machine:<id>, and on the Digitone mk1
    an SDK machine (sdk_alpha_machines) claims dnmachine:<id>, from the descriptor's id at +0, so
    two mods that register one id never link together, SDK or not. Ids are 0..127."""
    for key, table, prefix in (('digitakt-mk1', 'core_machines', 'machine'),
                               ('digitone-mk1', 'sdk_alpha_machines', 'dnmachine')):
        dev = next(d for d in devices.devices() if d.key == key)
        r = dev.releases[max(dev.releases)]
        image = bytes(r.main_len)

        def mod(i, slot):
            return link.Mod2({'elemod': 2, 'id': i, 'version': '1', 'target': devices.target_of(dev, r),
                              'sections': {'.run': {'align': 4, 'parts': [['hex', '%08x' % slot + '00' * 28]]}},
                              'symbols': {'m': ['.run', 0]},
                              'contribute': [{'to': table, 'data': '00000000',
                                              'relocs': [[0, 'abs32', 'sym:m', 0]]}]}, i)
        assert mod('a', 30).implied_names(image) == ['%s:30' % prefix]
        assert 'a 1 and b 1 both claim %s:30' % prefix in link.check([mod('a', 30), mod('b', 30)], image)
        assert not [p for p in link.check([mod('a', 30), mod('b', 127)], image) if 'claim' in p]
        assert mod('outside', 128).implied_names(image) == []


def test_dn2_synth_claims_its_stable_type():
    dn = next(d for d in devices.devices() if d.key == 'digitone-mk2')
    r = dn.releases['1.12']
    image = bytes(r.main_len)

    def mod(i, slot, table='mk2_voice_synths'):
        return link.Mod2({'elemod': 2, 'id': i, 'version': '1', 'target': devices.target_of(dn, r),
                          'sections': {'.run': {'align': 4, 'parts': [['hex', '%08x' % slot + '00' * 12]]}},
                          'symbols': {'m': ['.run', 0]},
                          'contribute': [{'to': table, 'data': '00000000',
                                          'relocs': [[0, 'abs32', 'sym:m', 0]]}]}, i)
    assert mod('a', 5).implied_names(image) == ['mk2-synth:5']
    assert 'a 1 and b 1 both claim mk2-synth:5' in link.check([mod('a', 5), mod('b', 5)], image)
    assert not [p for p in link.check([mod('a', 5), mod('b', 31)], image) if 'claim' in p]
    assert mod('outside', 32).implied_names(image) == []
    assert mod('sdk', 127, 'sdk_alpha_machines').implied_names(image) == ['mk2-synth:127']
    assert 'native 1 and sdk 1 both claim mk2-synth:7' in link.check(
        [mod('native', 7), mod('sdk', 7, 'sdk_alpha_machines')], image)
    assert not [p for p in link.check(
        [mod('a', 100, 'sdk_alpha_machines'), mod('b', 127, 'sdk_alpha_machines')], image) if 'claim' in p]
    assert mod('filter', 6, 'mk2_voice_filters').implied_names(image) == ['mk2-filter:6']
    assert 'a 1 and b 1 both claim mk2-filter:6' in link.check(
        [mod('a', 6, 'mk2_voice_filters'), mod('b', 6, 'mk2_voice_filters')], image)
    assert not [p for p in link.check(
        [mod('synth', 6), mod('filter', 6, 'mk2_voice_filters')], image) if 'claim' in p]
    assert mod('pages', 7, 'mk2_voice_pages').implied_names(image) == ['mk2-pages:7']
    assert 'a 1 and b 1 both claim mk2-pages:7' in link.check(
        [mod('a', 7, 'mk2_voice_pages'), mod('b', 7, 'mk2_voice_pages')], image)
    assert not [p for p in link.check(
        [mod('synth', 7, 'mk2_voice_synths'), mod('pages', 7, 'mk2_voice_pages')], image) if 'claim' in p]


def test_dn2_machine_bundles_claim_every_pointer_entry():
    dn = next(d for d in devices.devices() if d.key == 'digitone-mk2')
    r = dn.releases['1.12']
    image = bytes(r.main_len)

    def mod(i, slots, table):
        return link.Mod2({'elemod': 2, 'id': i, 'version': '1', 'target': devices.target_of(dn, r),
                          'sections': {'.run': {'align': 4, 'parts': [
                              ['hex', ''.join('%08x' % slot + '00' * 12 for slot in slots)]]}},
                          'symbols': {'m%d' % n: ['.run', n * 16] for n in range(len(slots))},
                          'contribute': [{'to': table, 'data': '00000000' * len(slots),
                                          'relocs': [[n * 4, 'abs32', 'sym:m%d' % n, 0]
                                                     for n in range(len(slots))]}]}, i)

    for table, prefix in (('mk2_voice_synths', 'mk2-synth'), ('mk2_voice_filters', 'mk2-filter')):
        bundle = mod('bundle', [6, 7], table)
        assert bundle.implied_names(image) == [prefix + ':6', prefix + ':7']
        problems = link.check([bundle, mod('custom', [7], table)], image)
        assert 'bundle 1 and custom 1 both claim %s:7' % prefix in problems, problems
        problems = link.check([mod('bundle', [6, 7], table), mod('custom', [8], table)], image)
        assert not [p for p in problems if 'claim' in p], problems


def test_dn2_machines_declares_only_custom_machine_tables():
    """The old registration tables are gone: an entry there is refused as an undeclared table."""
    dn = next(d for d in devices.devices() if d.key == 'digitone-mk2')
    r = dn.releases['1.12']
    image = bytes(r.main_len)
    declared = json.load(open(os.path.join(os.path.dirname(HERE), 'mods/machines-dn2/mod.json')))['collections']
    machines = link.Mod2({'elemod': 2, 'id': 'machines-dn2', 'version': '1', 'target': devices.target_of(dn, r),
                          'sections': {}, 'collections': {c: {'entry': e} for c, e in declared.items()}},
                         'machines-dn2')

    def registration(table):
        return link.Mod2({'elemod': 2, 'id': 'x', 'version': '1', 'target': devices.target_of(dn, r),
                          'sections': {'.run': {'align': 4, 'parts': [['hex', '00000006' + '00' * 12]]}},
                          'symbols': {'m': ['.run', 0]},
                          'contribute': [{'to': table, 'data': '00000000',
                                          'relocs': [[0, 'abs32', 'sym:m', 0]]}]}, 'x')
    for table in ('mk2_synths', 'mk2_filters'):
        assert 'x 1 adds to table %s, which no given mod declares' % table \
            in link.check([machines, registration(table)], image)
    for table in ('mk2_voice_synths', 'mk2_voice_filters'):
        assert not [p for p in link.check([machines, registration(table)], image) if 'adds to table' in p]


def test_dn2_machine_ids_can_span_stored_parts():
    from test_sharc_kernels import MACHINES, kernel
    dn = next(d for d in devices.devices() if d.key == 'digitone-mk2')
    r = dn.releases['1.12']
    target = devices.target_of(dn, r)
    image = bytearray(r.main_len)
    machine_id = (5).to_bytes(4, 'big')
    image[0x100:0x104] = machine_id
    core = link.Mod2({'elemod': 2, 'id': 'core', 'version': '0.1', 'target': target,
                      'sections': {'.boot': {'align': 4, 'parts': [['hex', '00000000']]}},
                      'collections': {'mk2_voice_synths': {'entry': 4}}}, 'core')

    def mod(parts, relocs=()):
        return link.Mod2({'elemod': 2, 'id': 'voice', 'version': '0.1', 'target': target,
                          'sections': {'.run': {'align': 4, 'parts': parts}},
                          'symbols': {'descriptor': ['.run', 0]}, 'relocs': list(relocs),
                          'contribute': [{'to': 'mk2_voice_synths', 'data': '00000000',
                                          'relocs': [[0, 'abs32', 'sym:descriptor', 0]]}],
                          'sharc': kernel(('mk2-voice', 5))}, 'voice')

    for split in (1, 2, 3):
        for left_stock in (False, True):
            for right_stock in (False, True):
                parts = [(['stock', dn.main_load + 0x100, split] if left_stock
                          else ['hex', machine_id[:split].hex()]),
                         (['stock', dn.main_load + 0x100 + split, 4 - split] if right_stock
                          else ['hex', machine_id[split:].hex()]), ['hex', '00' * 12]]
                voice = mod(parts)
                assert voice.implied_names(image) == ['mk2-synth:5'], parts
                assert link.check([core, MACHINES, voice], image) == [], parts
                relocated = mod(parts, [('.run', 0, 'abs32', 'abs', 5)])
                assert relocated.implied_names(image) == []
                assert 'voice 0.1: its SHARC mk2-voice kernel 5 has no registration with that id in its mod' in link.check(
                    [core, MACHINES, relocated], image)


def test_kept_bytes_refuse_every_write():
    """A device's `keep`: main OS bytes that look free but the OS reads, on the Octatrack the
    16 the stock DELAY reads past its coefficient table (octabam #771). A site that writes one
    of them is refused, in a whole build (format 1) and by the linker (format 2), with the
    range and its reason; the same site 16 bytes on is not. No free image area or relocatable
    data includes kept bytes, and the other devices keep none."""
    for d in devices.devices():
        for lo, hi, why in d.keep:
            assert d.main_load <= lo < hi and why, d.key
            assert not any(a < hi and lo < b for a, b in d.image_free), (d.key, hex(lo))
            assert not any(a < hi and lo < a + n for a, n, _r, _w in d.relocatable), (d.key, hex(lo))
            assert not any(a < hi and lo < b for a, b, _w in d.protected), (d.key, hex(lo))
    assert [d.key for d in devices.devices() if d.keep] == ['octatrack']
    ot = [d for d in devices.devices() if d.key == 'octatrack'][0]
    r = ot.releases['1.40C']
    (lo, hi, why), = ot.keep
    assert (lo, hi) == (0x400d24d0, 0x400d24e0) and 'DELAY' in why
    assert ot.image_free[1] == (0x400d24e0, 0x400d2ce0)        # octabam's run, past the kept bytes
    image = bytes(r.main_len)                                  # the kept bytes read zero in stock

    def sites(at, n):
        return [{'addr': '0x%08x' % at, 'len': n, 'new': 'ff' * n, 'kind': 'data',
                 'stock_sha256': elemod.sha(bytes(n))}]

    def whole(at, n):
        return elemod.Mod({'elemod': 1, 'id': 'w', 'version': '1', 'target': devices.target_of(ot, r),
                           'sites': sites(at, n)}, 'w')

    def mod2(at, n):
        return link.Mod2({'elemod': 2, 'id': 'm', 'version': '1', 'target': devices.target_of(ot, r),
                          'sections': {}, 'sites': sites(at, n)}, 'm')
    core = link.Mod2({'elemod': 2, 'id': 'core', 'version': '0.3', 'target': devices.target_of(ot, r),
                      'sections': {'.boot': {'align': 4, 'parts': [['hex', '4e75']]}}}, 'core')
    for at, n in ((0x400d24d0, 1812), (0x400d24dc, 4), (0x400d24c0, 17)):
        want = ('m 1 site 0x%08x writes into 0x400d24d0-0x400d24e0, which stays stock on the '
                'Octatrack: %s' % (at, why))
        assert link.check([core, mod2(at, n)], image) == [want]
        assert elemod.check([whole(at, n)], image) == [want.replace('m 1', 'w 1')]
    assert link.check([core, mod2(0x400d24e0, 1812)], image) == []
    assert elemod.check([whole(0x400d24e0, 1812)], image) == []
    assert elemod.check([whole(0x400d24c0, 16)], image) == []   # just before them


def test_parts_stay_in_the_image():
    d, r = devices.identify(devices.devices()[0].releases['1.53'].syx_sha256)
    ok = elemod.parse_parts([['hex', '00ff'], ['stock', '0x40000400', 8]], 't', d, r)
    assert ok == [('hex', b'\x00\xff'), ('stock', 0x40000400, 8)]
    for bad in (['stock', '0x40000000', 8], ['stock', '0x%08x' % (d.image_end(r) - 4), 8],
                ['stock', '0x40000400', 0], ['nope']):
        try:
            elemod.parse_parts([bad], 't', d, r)
        except elemod.ModError:
            continue
        raise AssertionError('accepted %r' % bad)



def test_lint_notes_the_code_sites_a_mod_hooks_itself():
    from elekloader.lint import core_candidates

    def mod2(mod_id, sites):
        m = link.Mod2.__new__(link.Mod2)
        m.id, m.sites = mod_id, sites
        return m
    sites = [{'addr': 0x400ea1a8, 'len': 6, 'kind': 'code'},
             {'addr': 0x401ab9b0, 'len': 4, 'kind': 'data'}]
    assert core_candidates(mod2('my-mod', sites)) == [
        {'mod': 'my-mod', 'site': '0x400ea1a8', 'len': 6}]
    # the core and its companions own their sites; data sites are not events
    assert core_candidates(mod2('core', sites)) == []
    assert core_candidates(mod2('machine-pages', sites)) == []
    assert core_candidates(mod2('my-mod', sites[1:])) == []


def test_coresites_groups_the_sites_mods_share():
    from elekloader.sdk.coresites import routine_test, shared_sites
    sites = [('health', '1.0', 0x4007814a, 6), ('neighbor', '0.6', 0x40078142, 6),
             ('slicer', '2.0', 0x40074df2, 6), ('slicer', '2.0', 0x40074e00, 6),
             ('fm', '1.2', 0x40074df2, 6), ('fm', '1.2', 0x40090000, 6)]
    g = shared_sites(sites)
    assert [(x['kind'], x['overlap'], x['lo'], x['hi'], sorted(y['mod'] for y in x['sites']))
            for x in g] == [
        ('near', True, 0x40074df2, 0x40074e06, ['fm', 'slicer', 'slicer']),
        ('near', False, 0x40078142, 0x40078150, ['health', 'neighbor'])]
    assert shared_sites(sites[2:5:2])[0]['kind'] == 'same bytes'
    # one mod's own sites close together are not shared
    assert shared_sites([('a', '1', 0x100, 6), ('a', '1', 0x106, 6)]) == []
    assert [(x['lo'], x['hi']) for x in shared_sites(sites, window=0)] == [(0x40074df2, 0x40074df8)]
    # by routine: an rts between two sites parts them, however close
    code = bytes.fromhex('4e714e714e714e714e754e714e714e71')       # nop x4, rts, nop x3
    dev = type('Dev', (), {'main_load': 0x1000, 'decoder': lambda self: __import__(
        'elekloader.isa.coldfire', fromlist=['x'])})()
    same = routine_test(code, dev)
    assert same((0x1000, 2), (0x1006, 2)) and not same((0x1000, 2), (0x100a, 2))
    two = [('a', '1', 0x1000, 2), ('b', '1', 0x1006, 2), ('c', '1', 0x100c, 2)]
    assert [sorted(y['mod'] for y in x['sites']) for x in shared_sites(two, same)] == [['a', 'b']]
    assert shared_sites(two, same)[0]['kind'] == 'same routine'


def test_bounties_count_only_the_newest_version_of_each_mod():
    """The to-do list counts each mod's newest version, for each device and OS on its own: an
    older file beside it adds nothing (a converted 363861e-intercepts outranks 363861e)."""
    from elekloader.sdk import bounties
    dev = type('Dev', (), {'key': 'dt', 'name': 'DT'})()
    made = {}

    def fake(path):
        mid, ver, os_ = os.path.basename(path)[:-len('.elemod')].split('_')
        m = link.Mod2.__new__(link.Mod2)
        m.id, m.version, m.dev, m.sites = mid, ver, dev, []
        m.rel = type('Rel', (), {'version': os_})()
        return m
    with tempfile.TemporaryDirectory() as d:
        for n in ('slicer_2.1_1.53', 'slicer_2.2_1.53', 'slicer_2.1_1.54',
                  'quant_363861e_1.40C', 'quant_363861e-intercepts_1.40C', 'mono_0.13_1.53', 'mono_0.13b_1.53',
                  'eq_2.9_1.53', 'eq_2.10_1.53'):
            open(os.path.join(d, n + '.elemod'), 'w').close()
        was, bounties.elemod.load_any = bounties.elemod.load_any, fake
        try:
            got = sorted((m.id, m.version, m.rel.version) for m in bounties.newest([d]))
        finally:
            bounties.elemod.load_any = was
    assert got == [('eq', '2.10', '1.53'), ('mono', '0.13b', '1.53'), ('quant', '363861e-intercepts', '1.40C'),
                   ('slicer', '2.1', '1.54'), ('slicer', '2.2', '1.53')], got


def test_bounties_lists_each_routine_once_across_oses():
    from elekloader.sdk.bounties import bounties
    dev = type('Dev', (), {'key': 'dt', 'name': 'DT'})()

    def mod2(mod_id, os_, *sites):
        m = link.Mod2.__new__(link.Mod2)
        m.id, m.version, m.dev = mod_id, '1.0', dev
        m.rel = type('Rel', (), {'version': os_})()
        m.sites = [{'addr': a, 'len': 6, 'kind': 'code', 'relocs': [(2, 'abs32', 'sym:' + s, 0)]}
                   for a, s in sites]
        return m
    mods = [mod2('a', '1.53', (0x100, 'a1'), (0x400, 'a2')),
            mod2('a', '1.54', (0x200, 'a1'), (0x500, 'a2')),      # the same mod, moved on 1.54
            mod2('b', '1.53', (0x10c, 'b1')),                      # near a1: one routine
            mod2('c', '1.53', (0x800, 'c1')),
            mod2('c', '1.54'),                                     # its 1.54 build lacks c1
            mod2('d', '1.53'),                                     # hooks nothing itself
            mod2('f', '1.53', (0xc00, 'f1'), (0xc10, 'f2')),       # one site covered, one not
            mod2('f', '1.54', (0xd00, 'f1'), (0xd10, 'f2')),
            mod2('machine-pages', '1.53', (0x402, 'mp'), (0xc00, 'mp2')),   # a companion is core
            mod2('machine-pages', '1.54', (0x502, 'mp'), (0xd00, 'mp2')),
            mod2('core', '1.53', (0x900, 'x'))]
    [r] = bounties(mods)
    assert r['oses'] == ['1.53', '1.54']
    assert r['score'] == {'mods': 5, 'no_own_code': 1, 'pure': ['d']}
    assert [(e['id'], e['kind'], e['mods'], e['missing_os']) for e in r['entries']] == [
        ('dt@1.53:0x00000100', 'core', ['a', 'b'], []),
        ('dt@1.53:0x00000800', 'core', ['c'], ['1.54']),
        ('dt@1.53:0x00000c00', 'partly', ['f'], []),
        ('dt@1.53:0x00000400', 'migration', ['a'], [])]
    assert [x['covered'] for x in r['entries'][2]['at']['1.54']['sites']] == [True, False]
    first = r['entries'][0]['at']
    assert (first['1.53']['lo'], first['1.53']['hi'], first['1.54']['lo']) == (
        '0x00000100', '0x00000112', '0x00000200')
    assert [s['label'] for s in first['1.53']['sites']] == ['a1', 'b1']
    assert (r['routines'], r['shared']) == (4, 1)
    # without the window, a and b are two routines, and so are f's sites
    assert bounties(mods, window=0)[0]['routines'] == 6
    # the Markdown, written between the markers of a page
    from elekloader.sdk.bounties import START, END, markdown, update
    md = markdown([r])
    assert md.startswith(START) and md.rstrip().endswith(END)
    assert '| `dt@1.53:0x00000800` | a core event (no address on 1.54) | 1 | c: c1 |' in md
    with tempfile.TemporaryDirectory() as d:
        page = os.path.join(d, 'page.md')
        with open(page, 'w') as fh:
            fh.write('# Page\n\n%s\nold\n%s\n\nafter\n' % (START, END))
        update(page, md)
        with open(page) as fh:
            text = fh.read()
        assert text.startswith('# Page\n\n' + START) and text.endswith(END + '\n\nafter\n')
        assert 'old' not in text and 'c: c1' in text


def test_bounties_take_an_upstream_list():
    """An upstream list adds the stock code a newer upstream's modules hook: no entry of the files
    changes its id, a file's own hook is not listed twice, and a module the files have from that
    commit is left to its file."""
    from elekloader.sdk.bounties import bounties, markdown
    dev = type('Dev', (), {'key': 'ot', 'name': 'OT'})()

    def mod2(mod_id, ver, *sites):
        m = link.Mod2.__new__(link.Mod2)
        m.id, m.version, m.dev = mod_id, ver, dev
        m.rel = type('Rel', (), {'version': '1.40C'})()
        m.sites = [{'addr': a, 'len': 6, 'kind': 'code', 'relocs': [(2, 'abs32', 'sym:' + s, 0)]}
                   for a, s in sites]
        return m
    mods = [mod2('ob-a', 'old', (0x1000, 'a1'), (0x2000, 'a2')),
            mod2('ob-b', 'new', (0x3000, 'b1')),               # converted from the upstream's commit
            mod2('core', '0.3', (0x5000, 'ev'))]
    up = {'from': 'up/stream', 'commit': 'new', 'device': 'ot', 'os': '1.40C', 'mods': [
        {'id': 'ob-a', 'version': 'new', 'sites': [['0x00001000', 6, 'a1'],      # the file's own hook
                                                   ['0x00000ff0', 6, 'a1x'],     # lower, in a1's routine
                                                   ['0x00004000', 6, 'a3']]},    # a routine of its own
        {'id': 'ob-b', 'version': 'new', 'sites': [['0x00003100', 6, 'b2']]},    # its file stands for it
        {'id': 'ob-c', 'version': 'new', 'sites': [['0x00004010', 6, 'c1'],      # with a3
                                                   ['0x00005000', 6, 'c2']]}]}   # on the core's site
    [was] = bounties(mods)
    [r] = bounties(mods, upstream=[up])
    assert r['score'] == was['score']
    old = {e['id']: e for e in was['entries']}
    new = {e['id']: e for e in r['entries']}
    assert set(old) <= set(new)
    first = new['ot@1.40C:0x00001000']
    assert [(s['site'], s['mod'], s.get('from')) for s in first['at']['1.40C']['sites']] == [
        ('0x00000ff0', 'ob-a', 'up/stream@new'), ('0x00001000', 'ob-a', None)]
    assert first['upstream'] == ['ob-a'] and 'upstream_only' not in first
    assert first['at']['1.40C']['lo'] == '0x00000ff0'
    assert 'upstream' not in new['ot@1.40C:0x00003000']         # nothing of ob-b's from the list
    only = new['ot@1.40C:0x00004000']
    assert only['upstream_only'] and only['mods'] == ['ob-a', 'ob-c'] and only['kind'] == 'core'
    assert new['ot@1.40C:0x00005000']['kind'] == 'migration'     # the core hooks the same bytes
    assert r['upstream'] == {'from': ['up/stream@new'], 'mods': 3, 'hooking': 2, 'not_here': 1,
                             'entries': 3, 'only': 2}
    md = markdown([r])
    assert '| `ot@1.40C:0x00004000` | a core event (upstream only) | 2 | ob-a (upstream): a3; ob-c (upstream): c1 |' in md
    assert 'ob-a: a1; ob-a (upstream): a1x' in md
    # a device with only a core on it is left out of the Markdown
    assert '### OT' not in markdown(bounties([mod2('core', '0.3', (0x5000, 'ev'))]))


def test_bounties_name_the_core_sites_beside_an_entry():
    """An entry's `near` is the core's and companions' sites in its routine on other bytes, with the
    symbols they jump to; its kind stays. A core site on the entry's bytes covers them instead, and one
    in another routine (as routines() decides: by the stock code, else by distance) is not listed."""
    from elekloader.sdk.bounties import bounties, markdown
    from elekloader.sdk.coresites import routine_test

    def mod2(dev, mod_id, os_, *sites):
        m = link.Mod2.__new__(link.Mod2)
        m.id, m.version, m.dev = mod_id, '1.0', dev
        m.rel = type('Rel', (), {'version': os_})()
        m.sites = [{'addr': s[0], 'len': s[2] if len(s) > 2 else 6, 'kind': 'code',
                    'relocs': [(2, 'abs32', 'sym:' + s[1], 0)] if s[1] else []} for s in sites]
        return m
    ot = type('Dev', (), {'key': 'ot', 'name': 'OT'})()
    mods = [mod2(ot, 'a', '1.0', (0x1000, 'a1')),                  # the core beside it, in its routine
            mod2(ot, 'b', '1.0', (0x2000, 'b1'), (0x2010, 'b2')),  # the core on b1's bytes
            mod2(ot, 'c', '1.0', (0x3000, 'c1')),                  # the core 256 bytes on: another routine
            mod2(ot, 'core', '1.0', (0x1008, 'core_midi_out'), (0x2002, 'core_x'), (0x3106, 'core_far')),
            mod2(ot, 'usb', '1.0', (0x0ff0, None))]                # a companion's, jumping to no symbol
    [r] = bounties(mods)
    by = {e['id']: e for e in r['entries']}
    a = by['ot@1.0:0x00001000']
    assert a['kind'] == 'core' and not a['at']['1.0']['sites'][0]['covered']
    assert a['near'] == [{'os': '1.0', 'site': '0x00000ff0', 'len': 6, 'mod': 'usb', 'target': '?'},
                         {'os': '1.0', 'site': '0x00001008', 'len': 6, 'mod': 'core', 'target': 'core_midi_out'}]
    b = by['ot@1.0:0x00002000']
    assert b['kind'] == 'partly' and b['near'] == []           # core_x is on b1's bytes: covered, not beside
    assert by['ot@1.0:0x00003000']['kind'] == 'core' and by['ot@1.0:0x00003000']['near'] == []
    # nothing else changes: the same report as without the sites beside
    [was] = bounties(mods[:3] + [mod2(ot, 'core', '1.0', (0x2002, 'core_x'))])
    assert [{k: v for k, v in e.items() if k != 'near'} for e in r['entries']] == \
        [{k: v for k, v in e.items() if k != 'near'} for e in was['entries']]
    md = markdown([r])
    assert ('| `ot@1.0:0x00001000` | a core event; usb hooks this routine at 0x00000ff0; '
            'core at 0x00001008 (core_midi_out) | 1 | a: a1 |') in md
    assert '| `ot@1.0:0x00003000` | a core event | 1 | c: c1 |' in md
    # by the stock code: an rts between the entry's site and the core's parts them, however close
    code = bytes.fromhex('4e714e714e714e714e754e714e714e71')       # nop x4, rts, nop x3
    same = routine_test(code, type('Dev', (), {'main_load': 0x1000, 'decoder': lambda self: __import__(
        'elekloader.isa.coldfire', fromlist=['x'])})())
    assert same.reach == 1024
    mods = [mod2(ot, 'a', '1.0', (0x1000, 'a1', 2)),
            mod2(ot, 'core', '1.0', (0x1004, 'before', 2), (0x100a, 'after', 2), (0x5000, 'far', 2))]
    assert [x['target'] for x in bounties(mods, {('OT', '1.0'): same})[0]['entries'][0]['near']] == ['before']
    assert [x['target'] for x in bounties(mods)[0]['entries'][0]['near']] == ['before', 'after']
    # each OS's own: an OS other than the one the id names is said
    dt = type('Dev', (), {'key': 'dt', 'name': 'DT'})()
    mods = [mod2(dt, 'e', '1.53', (0x100, 'e1')), mod2(dt, 'e', '1.54', (0x200, 'e1')),
            mod2(dt, 'core', '1.54', (0x208, 'ev'))]
    [r] = bounties(mods)
    assert r['entries'][0]['near'] == [{'os': '1.54', 'site': '0x00000208', 'len': 6, 'mod': 'core', 'target': 'ev'}]
    assert '| a core event; core hooks this routine at 0x00000208 (ev) (on 1.54) |' in markdown([r])


def test_the_site_names_its_files_with_its_version_and_lists_the_devices():
    pack = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'packaging')
    if not os.path.exists(os.path.join(pack, 'build_web.py')):
        return      # under Pyodide (tests/test_web.mjs) the site has the package only: nothing to build
    sys.path.insert(0, pack)
    import build_web
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'web', 'index.html'),
              encoding='utf-8') as fh:
        html = fh.read()
    out = build_web.stamp(html, 'abc123')
    assert 'src="app.js?v=abc123"' in out and 'href="style.css?v=abc123"' in out
    assert out.replace('?v=abc123', '') == html
    assert '<meta name="engine" content="engine/0123abcd">' in build_web.stamp(html, 'abc123', 'engine/0123abcd')
    try:
        build_web.stamp(html.replace('src="app.js"', 'src="main.js"'), 'x')
    except SystemExit:
        pass
    else:
        raise AssertionError('a page without app.js was stamped')
    # the same list the engine gives the page (bridge.info: key, name, releases), every device
    ds = build_web.device_list()
    assert [d['key'] for d in ds] == [d.key for d in devices.devices()]
    assert all(set(d) == {'key', 'name', 'releases'} and d['releases'] == sorted(d['releases']) for d in ds)


def test_the_shop_lists_what_each_file_may_not_share():
    """The page greys out a card that cannot go in the build (app.js, clash) from each shop file's footprint: what
    the link check refuses two mods sharing, without a stock file."""
    pack = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'packaging')
    if not os.path.exists(os.path.join(pack, 'build_web.py')):
        return      # under Pyodide (tests/test_web.mjs): no packaging/
    sys.path.insert(0, pack)
    import build_web
    dt = devices.devices()[0]
    m = link.Mod2({'elemod': 2, 'id': 'a', 'version': '1', 'target': devices.target_of(dt, dt.releases['1.53']),
                   'sections': {'.run': {'align': 4, 'parts': [['hex', '00' * 8]]}},
                   'symbols': {'p': ['.run', 0], 'q': ['.run', 4]}, 'exports': ['q'],
                   'collections': {'ev_b': {'entry': 8}, 'ev_a': {'entry': 4}},
                   'contribute': [{'to': 'ev_a', 'data': '00000000', 'relocs': [[0, 'abs32', 'sym:p', 0]],
                                   'claims': [[0x40001000, 0x40001006]]}],
                   'conflicts': ['b'], 'resources': {'names': ['soundslot:3']}}, 'a')
    f = build_web.footprint(m)
    assert f == {'format': 2, 'conflicts': ['b'], 'names': ['soundslot:3'], 'exports': ['q'],
                 'tables': ['ev_a', 'ev_b'], 'spans': [[0x40001000, 0x40001006]], 'regions': []}, f
    assert json.loads(json.dumps(f)) == f                  # it goes in shop/index.json as it is


def test_merged_pull_requests_claim_entries_for_the_leaderboard():
    pack = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'packaging')
    if not os.path.exists(os.path.join(pack, 'build_web.py')):
        return      # under Pyodide (tests/test_web.mjs): no packaging/
    sys.path.insert(0, pack)
    import build_web

    def pr(n, when, who, body):
        return {'number': n, 'mergedAt': when, 'url': 'https://github.com/o/r/pull/%d' % n,
                'title': 't', 'author': {'login': who} if who else None, 'body': body}
    r = build_web.claims([
        pr(6, '2026-10-21T00:00:00Z', 'bob', '- **Claims:** digitakt-mk1@1.53:0x400d553e\n'
                                            'claims: digitone-mk1@1.43:0x40000438'),
        pr(5, '2026-10-20T00:00:00Z', 'ann', 'The live note.\n\nClaims: digitakt-mk1@1.53:0x400D553E, '
                                            'octatrack@1.40C:0x4000d99a'),
        pr(7, None, 'eve', 'Claims: digitakt-mk1@1.53:0x40077d72'),              # not merged
        pr(8, '2026-10-22T00:00:00Z', 'eve', 'mentions digitakt-mk1@1.53:0x40077d72'),   # no Claims: line
        pr(9, '2026-10-23T00:00:00Z', None, 'Claims: digitakt-mk1@1.53:0x40000438'),     # no author
    ])
    # the first merged claim takes an entry (ann's, though bob's is listed first); the address in
    # lower case as sdk.bounties writes it, the OS as it is ("1.40C")
    assert [(c['id'], c['login'], c['pr']) for c in r['claims']] == [
        ('digitakt-mk1@1.53:0x400d553e', 'ann', 5), ('octatrack@1.40C:0x4000d99a', 'ann', 5),
        ('digitone-mk1@1.43:0x40000438', 'bob', 6)]
    assert [(b['login'], b['points'], [c['pr'] for c in b['claims']]) for b in r['board']] == [
        ('ann', 2, [5, 5]), ('bob', 1, [6])]
    assert build_web.claims([]) == {'claims': [], 'board': []}
    # a Claims: line shown as an example claims nothing: in a code block, a comment or a quote
    # (#126's own description showed one in a code block, and scored it)
    shown = build_web.claims([
        pr(10, '2026-10-24T00:00:00Z', 'dan', 'How to claim:\n\n```\nClaims: digitakt-mk1@1.53:0x400d553e\n```\n'
                                            '~~~md\nClaims: digitakt-mk1@1.53:0x40000438\n~~~\n'
                                            '<!-- Claims: digitakt-mk1@1.53:0x40008904 -->\n'
                                            '> Claims: digitakt-mk1@1.53:0x40008d40\n'
                                            '```\nClaims: digitakt-mk1@1.53:0x40025338 (a block never closed)'),
        pr(11, '2026-10-25T00:00:00Z', 'dan', '```\nexample\n```\n\nClaims: `digitakt-mk1@1.53:0x40025344`'),
    ])
    assert [(c['id'], c['pr']) for c in shown['claims']] == [('digitakt-mk1@1.53:0x40025344', 11)]


def test_every_refusal_of_the_linker_has_a_fixture_that_is_refused_and_one_that_links():
    """tests/test_link_refusals.py, here too because this file is the one the workflows run: a
    message the linker or the loader can refuse with and no fixture for it fails the build."""
    sys.path.insert(0, HERE)
    import test_link_refusals as refusals
    refusals.test_every_refusal_has_a_fixture()
    refusals.test_octatrack_fixtures()
    refusals.test_digitakt_fixtures()
    refusals.test_analog_rytm_fixtures()
    refusals.test_the_image_check_refuses_a_protected_or_kept_byte_the_other_checks_let_through()


def test_dn2_dsp_peek_ignores_idle_replies():
    import unittest
    sys.path.insert(0, HERE)
    from test_dn2_dsp_peek import ColdFirePeekTests
    suite = unittest.TestLoader().loadTestsFromTestCase(ColdFirePeekTests)
    result = unittest.TextTestRunner().run(suite)
    assert result.wasSuccessful(), 'DN2 DSP peek sequence regression failed'


def test_dn2_machines_suite():
    import unittest
    suite = unittest.TestLoader().discover(HERE, pattern='test_dn2_machines.py')
    assert suite.countTestCases(), 'no DN2 machine registry tests discovered'
    result = unittest.TextTestRunner().run(suite)
    assert result.wasSuccessful(), 'DN2 machine registry suite failed'


def test_sdk_alpha_render_suite():
    import unittest
    suite = unittest.TestLoader().discover(HERE, pattern='test_sdk_alpha_render.py')
    assert suite.countTestCases(), 'no SDK render contract tests discovered'
    result = unittest.TextTestRunner().run(suite)
    assert result.wasSuccessful(), 'SDK render contract suite failed'


def test_sdk_alpha_host_suite():
    import unittest
    suite = unittest.TestLoader().discover(HERE, pattern='test_sdk_alpha_host.py')
    assert suite.countTestCases(), 'no SDK host run tests discovered'
    result = unittest.TextTestRunner().run(suite)
    assert result.wasSuccessful(), 'SDK host run suite failed'


def test_sdk_alpha_suite():
    import unittest
    suite = unittest.TestLoader().discover(HERE, pattern='test_sdk_alpha.py')
    assert suite.countTestCases(), 'no SDK builder tests discovered'
    result = unittest.TextTestRunner().run(suite)
    assert result.wasSuccessful(), 'SDK builder suite failed'


def test_sharc_suite():
    import unittest
    suite = unittest.TestLoader().discover(HERE, pattern='test_sharc*.py')
    assert suite.countTestCases(), 'no SHARC tests discovered'
    result = unittest.TextTestRunner().run(suite)
    assert result.wasSuccessful(), 'SHARC suite failed'


def test_model_cycles_bulk_area():
    """The Model:Cycles' bulk area, 0x42800000-0x47E00000: a region inside it passes (a sample pool,
    0x43800000-0x46700000), one below it or across into the linker's area does not, and the Model:Samples, whose
    sample pool is there, has no such area."""
    mc, ms = ([d for d in devices.devices() if d.key == k][0] for k in ('model-cycles', 'model-samples'))
    assert mc.areas['bulk'] == (0x42800000, 0x47E00000)
    pool = {'resources': {'regions': [{'name': 'pool', 'lo': '0x43800000', 'hi': '0x46700000'}]}}
    regions, _names = elemod.parse_resources(pool, 'x', mc)
    assert regions == [{'name': 'pool', 'lo': 0x43800000, 'hi': 0x46700000, 'area': 'bulk'}]
    for lo, hi in ((0x42700000, 0x42900000), (0x47D00000, 0x47E10000)):
        try:
            elemod.parse_resources({'resources': {'regions': [{'name': 'r', 'lo': hex(lo), 'hi': hex(hi)}]}},
                                   'x', mc)
            raise AssertionError('region 0x%08x-0x%08x passed' % (lo, hi))
        except elemod.ModError as e:
            assert 'outside every free area' in str(e)
    assert 'bulk' not in ms.areas and ms.areas['ddr'] == mc.areas['ddr']
    try:
        elemod.parse_resources(pool, 'x', ms)
        raise AssertionError('the pool passed on the Model:Samples')
    except elemod.ModError as e:
        assert 'outside every free area' in str(e)


def submit_mod_module():
    """packaging/submit_mod.py, or None where the repository isn't (under Pyodide, tests/test_web.mjs runs this file
    with the site's package only)."""
    folder = os.path.join(os.path.dirname(HERE), 'packaging')
    if not os.path.exists(os.path.join(folder, 'submit_mod.py')):
        return None
    sys.path.insert(0, folder)
    import submit_mod
    return submit_mod


def test_the_shop_list_keeps_its_layout():
    """packaging/submit_mod.py rewrites catalog.json: it writes the file as it is laid out by hand."""
    submit_mod = submit_mod_module()
    if submit_mod is None or shop_list_or_skip(submit_mod.CATALOG) is None:
        return
    with open(submit_mod.CATALOG, encoding='utf-8') as fh:
        text = fh.read()
    assert submit_mod.render(json.loads(text)) == text


def test_a_submission_is_told_the_core_it_builds_with():
    """submit_mod.py's heads-up names the core a builder takes for the file (gui.with_core): the newest the site has
    that is new enough and that it can share a build with, not the newest whatever the file patches."""
    submit_mod = submit_mod_module()
    if submit_mod is None:
        return
    fp = lambda *spans: {'id': 'core', 'format': 2, 'conflicts': [], 'names': [], 'exports': [], 'tables': [],
                         'spans': [list(s) for s in spans], 'regions': []}
    at = ('digitakt-mk1', '1.53')
    cores = {at: ['4.0', '4.1', '2.1']}
    prints = {at + ('2.1',): fp((0x40010000, 0x40010006)), at + ('4.0',): fp((0x40010000, 0x40010006)),
              at + ('4.1',): fp((0x40010000, 0x40010006), (0x400721e6, 0x400721ec))}
    mod = lambda need, *spans: {'id': 'm', 'device': at[0], 'os': at[1], 'requires': ['core'], 'need': need,
                                'footprint': dict(fp(*spans), id='m')}
    core_note = lambda f, p=prints: submit_mod.heads_up(f, {}, cores, p)[0]
    assert core_note(mod(None, (0x40020000, 0x40020004))) == 'builds with core 4.1'
    # a mod made before 4.1 took the master output's place: 4.0, and why it is not the newest
    assert core_note(mod('2.1', (0x400721e6, 0x400721ec))) == (
        "builds with core 4.0, not the newest: it can't link with core 4.1 (both patch the same place in the "
        "firmware), so it can't share a build with a mod that needs a newer core than 4.0")
    # one that needs 4.1 and patches its place: no core the site has
    assert core_note(mod('4.1', (0x400721e6, 0x400721ec))) == (
        "can't be built on the site: it can't link with core 4.1 (both patch the same place in the firmware), "
        'nor with an older one the site has')
    # without the cores' footprints (no --cores-dir): the newest, as far as the check can tell
    assert core_note(mod(None, (0x400721e6, 0x400721ec)), None) == 'builds with core 4.1'
    assert submit_mod.heads_up(dict(mod(None), requires=[]), {}, cores, prints) == []


def test_the_submit_a_mod_form_is_read():
    submit_mod = submit_mod_module()
    if submit_mod is None:
        return
    body = ('### Repository\r\n\r\nirpina/digitables\r\n\r\n### Release tag\r\n\r\nv1.3\r\n\r\n'
            '### Summary\r\n\r\nPitch tables.\r\n\r\n### Tested on a unit\r\n\r\n_No response_\r\n\r\n'
            '### Only these files\r\n\r\ndigitables-1.3.elemod\r\n\r\n### Checks\r\n\r\n- [X] I wrote these mods\r\n')
    assert submit_mod.parse_issue(body) == {'repo': 'irpina/digitables', 'tag': 'v1.3', 'summary': 'Pitch tables.',
                                            'on_unit': '', 'files': 'digitables-1.3.elemod'}
    status, report = submit_mod.submit({'repo': 'not a repo', 'tag': 'v1', 'summary': ''})
    assert status == 1 and len(report['errors']) == 2


def test_a_submission_brings_what_its_mods_need_or_is_refused():
    """What a submitted mod requires must be on the site: a core new enough (from elekloader's releases only)
    and the mods it requires, the shop's or its release's own. GitHub and the files are faked."""
    submit_mod = submit_mod_module()
    if submit_mod is None:
        return
    def mod(id_, version, device='digitakt-mk1', os_='1.53', requires=('core',), needs_core=None, license='MIT'):
        return {'id': id_, 'version': version, 'device': device, 'os': os_, 'license': license, 'named_license': True,
                'title': id_.upper(), 'core': id_ == 'core', 'requires': list(requires) if id_ != 'core' else [],
                'needs_core': needs_core, 'description': 'What %s does.' % id_}
    FACTS = {'core-4.0.elemod': mod('core', '4.0'), 'core-4.1.elemod': mod('core', '4.1'),
             'core-5.0.elemod': mod('core', '5.0'), 'mp-1.2.elemod': mod('machine-pages', '1.2'),
             'mp-1.3.elemod': mod('machine-pages', '1.3'), 'a-1.0.elemod': mod('a', '1.0'),
             'a-1.1.elemod': mod('a', '1.1', requires=('core', 'machine-pages'), needs_core='4.1'),
             'b-1.elemod': mod('b', '1', requires=('core', 'kit')), 'kit-1.elemod': mod('kit', '1'),
             'c-1.elemod': mod('c', '1', needs_core='5.0'), 'd-1.elemod': mod('d', '1', device='digitone-mk1', os_='1.43'),
             'e-1.elemod': mod('e', '1', requires=('core', 'missing')),
             'g-1.elemod': mod('g', '1', requires=('h',)), 'h-1.elemod': mod('h', '1', requires=('core', 'g'), needs_core='5.0')}
    RELEASES = {('owner/a', 'v1.1'): (['a-1.1.elemod'], True), ('owner/b', 'v1'): (['b-1.elemod', 'kit-1.elemod'], False),
                ('owner/c', 'v1'): (['c-1.elemod', 'core-5.0.elemod'], False), ('owner/d', 'v1'): (['d-1.elemod'], False),
                ('owner/e', 'v1'): (['e-1.elemod'], False), ('owner/g', 'v1'): (['g-1.elemod', 'h-1.elemod'], False),
                ('irpina/elekloader', 'mp-v1.3'): (['mp-1.3.elemod'], True)}
    def get(url, accept='', limit=0):
        if '/releases/tags/' in url:
            repo, tag = url.split('/repos/')[1].split('/releases/tags/')
            if (repo, tag) not in RELEASES:
                raise submit_mod.urllib.error.HTTPError(url, 404, 'Not Found', {}, None)
            names, pre = RELEASES[(repo, tag)]
            repo = submit_mod.MOVED.get(repo, repo)
            return json.dumps({'html_url': 'https://github.com/%s/releases/tag/%s' % (repo, tag), 'prerelease': pre,
                               'assets': [{'name': n} for n in names]}).encode()
        return ('file ' + url.rsplit('/', 1)[1]).encode()
    def facts(path, license=''):
        return dict(FACTS[os.path.basename(path)])
    shop = [('irpina/elekloader', 'core-v4.0', 'core-4.0.elemod', {'kind': 'core'}), ('irpina/elekloader', 'mp-v1.2', 'mp-1.2.elemod', {}),
            ('owner/a', 'v1.0', 'a-1.0.elemod', {'summary': 'A.'})]
    real = submit_mod.get, submit_mod.facts
    submit_mod.get, submit_mod.facts = get, facts
    try:
        with tempfile.TemporaryDirectory() as tmp:
            for d in ('shop', 'core'):
                os.mkdir(os.path.join(tmp, d))
            for _, _, f, _ in shop:
                open(os.path.join(tmp, 'shop', f), 'wb').close()
            open(os.path.join(tmp, 'core', 'core-4.1.elemod'), 'wb').close()
            catalog = os.path.join(tmp, 'catalog.json')
            items = [dict({'repo': r, 'tag': t, 'file': f, 'device': 'digitakt-mk1', 'sha256': '0' * 64}, **x) for r, t, f, x in shop]
            def run(repo, tag, files=''):
                with open(catalog, 'w', encoding='utf-8') as fh:
                    fh.write(submit_mod.render({'about': '', 'items': items}))
                status, r = submit_mod.submit({'repo': repo, 'tag': tag, 'summary': 'One sentence.', 'files': files}, catalog,
                                              os.path.join(tmp, 'shop'), os.path.join(tmp, 'core'))
                with open(catalog, encoding='utf-8') as fh:
                    got = json.load(fh)['items']
                return status, r, {row['file']: row['result'] for row in r['rows']}, {i['file']: i for i in got}
            # an update, from a pre-release: in the old entry's place, needs_core from the file
            status, r, res, got = run('owner/a', 'v1.1')
            assert status == 0 and r['prerelease'] and res['a-1.1.elemod'] == 'replaces a-1.0.elemod', res
            assert 'a-1.0.elemod' not in got and got['a-1.1.elemod']['needs_core'] == '4.1' and r['same_repo_update']
            assert '(a pre-release)' in submit_mod.report_md(status, r)
            # a required mod "Only these files" left out comes along, with its own description
            status, r, res, got = run('owner/b', 'v1', 'b-1.elemod')
            assert status == 0 and res['kit-1.elemod'] == 'added: B needs it' and not r['same_repo_update'], res
            assert got['kit-1.elemod']['summary'] == 'What kit does.' and got['b-1.elemod']['summary'] == 'One sentence.'
            # a core newer than the site's is refused, and a core from a mod's release never goes in
            status, r, res, got = run('owner/c', 'v1')
            assert status == 1 and res['c-1.elemod'].startswith('refused: it needs core 5.0 or newer'), res
            assert 'newest the site has is 4.1' in res['c-1.elemod'] and res['core-5.0.elemod'].startswith('left out'), res
            # no core at all for the device, a required mod nobody has, a draft, and two mods that need each other
            assert run('owner/d', 'v1')[2]['d-1.elemod'].endswith('the site has none; cores come from elekloader\'s own releases')
            assert run('owner/e', 'v1')[2]['e-1.elemod'] == ('refused: it needs missing for digitakt-mk1 1.53, '
                                                             'which neither the shop nor this release has')
            status, r, res, got = run('owner/f', 'v1')
            assert status == 1 and 'a draft is not one' in r['errors'][0]
            status, r, res, got = run('owner/g', 'v1')
            assert status == 1 and res['g-1.elemod'] == 'refused: a mod it needs from this release can\'t go in', res
            # a repository that moved updates its own entries, under its new name
            status, r, res, got = run('irpina/elekloader', 'mp-v1.3')
            assert status == 0 and res['mp-1.3.elemod'] == 'replaces mp-1.2.elemod', res
            assert got['mp-1.3.elemod']['repo'] == 'elekloader/elekloader' and 'mp-1.2.elemod' not in got
    finally:
        submit_mod.get, submit_mod.facts = real


def test_shop_updates_take_only_newer_files_of_the_repositorys_own_mods():
    """packaging/shop_updates.py: the newer releases of a shop repository, oldest first, each through submit_mod's
    update check. GitHub and the files are faked."""
    submit_mod = submit_mod_module()
    if submit_mod is None:
        return
    import shop_updates
    def mod(id_, version, os_='1.53'):
        return {'id': id_, 'version': version, 'device': 'digitakt-mk1', 'os': os_, 'license': 'MIT', 'named_license': True,
                'title': id_.upper(), 'core': id_ == 'core', 'requires': [] if id_ == 'core' else ['core'],
                'needs_core': None, 'description': ''}
    FACTS = {'core-4.1.elemod': mod('core', '4.1'), 'core-4.1-os1.54.elemod': mod('core', '4.1', '1.54'),
             'core-9.elemod': mod('core', '9'), 'x-1.0.elemod': mod('x', '1.0'), 'y-1.0.elemod': mod('y', '1.0'),
             'x-1.1.elemod': mod('x', '1.1'), 'x-1.1-os1.54.elemod': mod('x', '1.1', '1.54'), 'y-1.1.elemod': mod('y', '1.1'),
             'z-1.elemod': mod('z', '1'), 'x-1.2.elemod': mod('x', '1.2'), 'x-1.0.1.elemod': mod('x', '1.0.1')}
    # tag, published, pre-release, files, notes (v0.9 is older than what the shop takes; cores-only is all cores)
    RELEASES = [('v0.9', '2026-10-01', False, ['x-1.0.elemod'], ''), ('v1.0', '2026-10-02', False, ['x-1.0.elemod', 'y-1.0.elemod'], ''),
                ('v1.1', '2026-10-03', False, ['x-1.1.elemod', 'x-1.1-os1.54.elemod', 'y-1.1.elemod', 'z-1.elemod', 'core-4.1.elemod'],
                 'Fixes.\n\n- **Tested on a unit:** 1.1 ran on my Digitakt.\n'),
                ('v1.2b', '2026-10-04', True, ['x-1.2.elemod'], ''), ('v1.0.1', '2026-10-05', False, ['x-1.0.1.elemod'], ''),
                ('cores-only', '2026-10-06', False, ['core-9.elemod'], '')]
    def release(t):
        tag, when, pre, files, body = next(x for x in RELEASES if x[0] == t)
        return {'tag_name': tag, 'published_at': when + 'T00:00:00Z', 'prerelease': pre, 'draft': False, 'body': body,
                'html_url': 'https://github.com/owner/x/releases/tag/' + tag, 'assets': [{'name': n} for n in files]}
    def get(url, accept='', limit=0):
        if url.endswith('/releases?per_page=100'):
            return json.dumps([release(t[0]) for t in reversed(RELEASES)]).encode()
        if '/releases/tags/' in url:
            return json.dumps(release(url.rsplit('/', 1)[1])).encode()
        return ('file ' + url.rsplit('/', 1)[1]).encode()
    real = submit_mod.get, submit_mod.facts
    submit_mod.get, submit_mod.facts = get, lambda path, license='': dict(FACTS[os.path.basename(path)])
    try:
        with tempfile.TemporaryDirectory() as tmp:
            shop, cores = os.path.join(tmp, 'shop'), os.path.join(tmp, 'core')
            for d, names in ((shop, ['x-1.0.elemod', 'y-1.0.elemod']), (cores, ['core-4.1.elemod', 'core-4.1-os1.54.elemod'])):
                os.mkdir(d)
                for n in names:
                    open(os.path.join(d, n), 'wb').close()
            catalog = os.path.join(tmp, 'catalog.json')
            items = [{'repo': 'owner/x', 'tag': 'v1.0', 'file': 'x-1.0.elemod', 'device': 'digitakt-mk1', 'sha256': '0' * 64,
                      'summary': 'X.', 'on_unit': 'Ran on a unit.'},
                     {'repo': 'owner/x', 'tag': 'v1.0', 'file': 'y-1.0.elemod', 'device': 'digitakt-mk1', 'sha256': '1' * 64,
                      'summary': 'Y.', 'hold': 'kept for its users'}]
            with open(catalog, 'w', encoding='utf-8') as fh:
                fh.write(submit_mod.render({'about': '', 'items': items}))
            assert shop_updates.newer_releases('owner/x', {'v1.0'}) == ['v1.1', 'v1.2b', 'v1.0.1']
            out = os.path.join(tmp, 'result.json')
            import contextlib
            import io
            with contextlib.redirect_stdout(io.StringIO()):
                assert shop_updates.main(['--apply', 'owner/x', '--catalog', catalog, '--files-dir', shop, '--cores-dir',
                                          cores, '--result', out, '--report', os.path.join(tmp, 'report.md')]) == 0
            with open(out) as fh:
                result = json.load(fh)
            assert result['same_repo_update'] and result['tags'] == ['v1.1'], result
            with open(catalog, encoding='utf-8') as fh:
                got = {i['file']: i for i in json.load(fh)['items']}
            assert sorted(got) == ['x-1.1-os1.54.elemod', 'x-1.1.elemod', 'y-1.0.elemod'], sorted(got)
            # the card's summary carries over; the release's notes give the hardware note; a held entry stays
            assert got['x-1.1.elemod']['summary'] == 'X.' and got['x-1.1.elemod']['on_unit'] == '1.1 ran on my Digitakt.'
            assert got['x-1.1-os1.54.elemod']['summary'] == 'X.' and got['y-1.0.elemod']['hold']
            with open(os.path.join(tmp, 'report.md'), encoding='utf-8') as fh:
                report = fh.read()
            for said in ('left out: the shop holds y', 'left out: not in the shop yet', 'left out: cores come from elekloader',
                         'left out: a pre-release replaces a release only', 'left out: the shop has a newer version (1.1)'):
                assert said in report, said
    finally:
        submit_mod.get, submit_mod.facts = real
    assert submit_mod.as_of('Ran on a unit.', '1.0') == 'As of 1.0: Ran on a unit.'
    assert submit_mod.release_text('') == {}


def thumbs_module():
    """packaging/thumbs.py, or None where the repository isn't (under Pyodide)."""
    folder = os.path.join(os.path.dirname(HERE), 'packaging')
    if not os.path.exists(os.path.join(folder, 'thumbs.py')):
        return None
    sys.path.insert(0, folder)
    import thumbs
    return thumbs


def png_rgba(rows):
    """An 8-bit RGBA PNG of rows of (r, g, b, a), as an image editor saves one."""
    import zlib
    w, h = len(rows[0]), len(rows)
    raw = b''.join(b'\x00' + bytes(v for p in row for v in p) for row in rows)
    def chunk(k, d):
        return struct.pack('>I', len(d)) + k + d + struct.pack('>I', zlib.crc32(k + d))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))


def screen_bits(kind='drawing'):
    """A 128x64 picture: a frame, a sine and bars (a drawing), a checkerboard of shading (dithered)."""
    import math
    bits = [[0] * 128 for _ in range(64)]
    for y in range(64):
        for x in range(128):
            if kind == 'dithered':
                bits[y][x] = (x + y) % 2 if 20 < x < 110 else (x * 7 + y * 3) % 5 == 0
            elif y in (0, 63) or x in (0, 127) or y == int(32 + 20 * math.sin(x / 8)) or (x % 32 == 4 and y > 40):
                bits[y][x] = 1
    return bits


def test_shop_thumbnails_are_screen_pictures():
    """packaging/thumbs.py: exactly 128x64 in two solid colours, drawn, not dithered; nothing is converted."""
    thumbs = thumbs_module()
    if thumbs is None:
        return
    bits = screen_bits()
    canon = thumbs.canonical_png(bits)
    r = thumbs.check(canon)
    assert r['ok'] and r['bits'] == bits and len(canon) < 1024, r.get('reason')
    assert thumbs.canonical_png(thumbs.check(canon)['bits']) == canon       # the site's file is stable
    # an editor's RGBA save of two colours (amber on navy) is the same picture; the lighter colour is lit
    two = png_rgba([[(250, 190, 60, 255) if v else (20, 24, 60, 255) for v in row] for row in bits])
    assert thumbs.check(two)['bits'] == bits
    text = thumbs.braille(bits).split('\n')
    assert len(text) == 16 and all(len(t) == 64 for t in text) and text[0][0] == chr(0x2800 + 0x4f)
    refused = {
        'not a PNG': (b'GIF89a' + bytes(100), 'not a PNG'),
        'too big': (png_rgba([[(0, 0, 0, 255)] * 256 for _ in range(128)]), 'is 256x128'),
        'greys': (png_rgba([[(v * 2, v * 2, v * 2, 255) for v in range(128)] for _ in range(64)]), 'has 128 colours'),
        'transparent': (png_rgba([[(255, 255, 255, 255 if v else 0) for v in row] for row in bits]), 'transparent'),
        'flat': (thumbs.canonical_png([[0] * 128 for _ in range(64)]), 'one flat colour'),
        'dithered': (thumbs.canonical_png(screen_bits('dithered')), 'looks dithered'),
        'large': (canon + b'\0' * thumbs.MAX_BYTES, 'at most'),
    }
    for name, (data, said) in refused.items():
        r = thumbs.check(data)
        assert not r['ok'] and said in r['reason'], (name, r.get('reason'))
    assert 'not taken: it looks dithered' in thumbs.preview_md(thumbs.check(refused['dithered'][0]))
    assert '```text\n' + thumbs.braille(bits) in thumbs.preview_md(thumbs.check(canon))


def test_the_site_takes_a_thumbnail_only_as_pinned_and_checked():
    """build_web.py: a "thumb" is pinned like a file, listed once for download, checked again, and served as the
    canonical PNG; one missing, changed or failing leaves the card's drawn cover."""
    thumbs = thumbs_module()
    if thumbs is None:
        return
    import contextlib
    import hashlib
    import io
    import build_web
    good = thumbs.canonical_png(screen_bits())
    bad = thumbs.canonical_png(screen_bits('dithered'))
    sums = {n: hashlib.sha256(b).hexdigest() for n, b in (('good', good), ('bad', bad))}
    with tempfile.TemporaryDirectory() as tmp:
        cat = os.path.join(tmp, 'catalog.json')
        def items(*thumbs_):
            return [{'repo': 'o/r', 'tag': 'v1', 'file': 'm%d.elemod' % i, 'device': 'digitakt-mk1', 'sha256': '0' * 64,
                     'thumb': t} for i, t in enumerate(thumbs_)]
        def read(*thumbs_):
            with open(cat, 'w') as fh:
                json.dump({'about': '', 'items': items(*thumbs_)}, fh)
            return build_web.read_catalog(cat)
        pin = {'tag': 'v1', 'file': 'thumb.png', 'sha256': sums['good']}
        got = read(pin, dict(pin), {'commit': 'a' * 40, 'path': 'art/thumb.png', 'sha256': sums['bad']})
        assert build_web.thumb_list(got) == sorted([('thumb-release', 'o/r', 'v1', 'thumb.png', sums['good']),
                                                    ('thumb-commit', 'o/r', 'a' * 40, 'art/thumb.png', sums['bad'])],
                                                   key=lambda t: t[-1])
        for wrong in ({'tag': 'v1', 'file': 'thumb.gif', 'sha256': sums['good']}, {'tag': 'v1', 'sha256': sums['good']},
                      {'commit': 'a' * 40, 'path': '../thumb.png', 'sha256': sums['good']}, dict(pin, sha256='x')):
            try:
                read(wrong)
            except SystemExit as e:
                assert '"thumb" is' in str(e)
            else:
                raise AssertionError(wrong)
        os.mkdir(os.path.join(tmp, 'thumbs'))
        for n, b in (('good', good), ('bad', bad)):
            with open(os.path.join(tmp, 'thumbs', sums[n] + '.png'), 'wb') as fh:
                fh.write(b)
        with contextlib.redirect_stdout(io.StringIO()) as said:
            assert build_web.thumb({'file': 'a', 'repo': 'o/r', 'tag': 'v1', 'thumb': pin}, tmp) == good
            assert build_web.thumb({'file': 'b', 'repo': 'o/r', 'tag': 'v1',
                                    'thumb': dict(pin, sha256=sums['bad'])}, tmp) is None
            assert build_web.thumb({'file': 'c', 'repo': 'o/r', 'tag': 'v1', 'thumb': dict(pin, sha256='1' * 64)}, tmp) is None
            assert build_web.thumb({'file': 'd', 'repo': 'o/r', 'tag': 'v1'}, tmp) is None
        said = said.getvalue()
        assert 'thumbnail of b is left out: it looks dithered' in said and 'thumbnail of c (o/r v1) is not there' in said


def test_a_new_thumbnail_waits_for_a_maintainer():
    """submit_mod.py: a release's thumb.png goes on the entries a form submission adds or changes, and on files the
    shop has already, and never merges by itself; a daily update moves the pin only to the same picture."""
    submit_mod = submit_mod_module()
    if submit_mod is None:
        return
    import hashlib
    thumbs = thumbs_module()
    one, two = thumbs.canonical_png(screen_bits()), thumbs.canonical_png([r[::-1] for r in screen_bits()])
    dots = thumbs.canonical_png(screen_bits('dithered'))
    h = lambda b: hashlib.sha256(b).hexdigest()
    def mod(version):
        return {'id': 'x', 'version': version, 'device': 'digitakt-mk1', 'os': '1.53', 'license': 'MIT',
                'named_license': True, 'title': 'X', 'core': False, 'requires': [], 'needs_core': None, 'description': ''}
    FACTS = {'x-1.0.elemod': mod('1.0'), 'x-1.1.elemod': mod('1.1'), 'x-1.2.elemod': mod('1.2'), 'x-1.3.elemod': mod('1.3')}
    RELEASES = {'v1.0': (['x-1.0.elemod'], one), 'v1.1': (['x-1.1.elemod'], one), 'v1.2': (['x-1.2.elemod'], two),
                'v1.3': (['x-1.3.elemod'], dots)}
    def get(url, accept='', limit=0):
        if '/releases/tags/' in url:
            tag = url.rsplit('/', 1)[1]
            return json.dumps({'html_url': 'https://github.com/o/x/releases/tag/' + tag, 'prerelease': False,
                               'assets': [{'name': n} for n in RELEASES[tag][0] + ['thumb.png']]}).encode()
        if url.endswith('/thumb.png'):
            return RELEASES[url.split('/download/')[1].split('/')[0]][1]
        return ('file ' + url.rsplit('/', 1)[1]).encode()
    real = submit_mod.get, submit_mod.facts
    submit_mod.get, submit_mod.facts = get, lambda path, license='': dict(FACTS[os.path.basename(path)])
    try:
        with tempfile.TemporaryDirectory() as tmp:
            shop = os.path.join(tmp, 'shop')
            os.mkdir(shop)
            catalog = os.path.join(tmp, 'catalog.json')
            def start(items):
                for it in items:
                    open(os.path.join(shop, it['file']), 'wb').close()
                with open(catalog, 'w', encoding='utf-8') as fh:
                    fh.write(submit_mod.render({'about': '', 'items': items}))
            def run(tag, update=False):
                status, r = submit_mod.submit({'repo': 'o/x', 'tag': tag, 'summary': '' if update else 'X.'}, catalog,
                                              shop, None, update=update)
                with open(catalog, encoding='utf-8') as fh:
                    return status, r, json.load(fh)['items']
            entry = {'repo': 'o/x', 'tag': 'v1.0', 'file': 'x-1.0.elemod', 'device': 'digitakt-mk1', 'sha256': h(b'file x-1.0.elemod'),
                     'summary': 'X.'}
            # the form, for a file the shop has already: the picture goes on, and waits for a review
            start([dict(entry)])
            status, r, items = run('v1.0')
            assert status == 0 and items[0]['thumb'] == {'tag': 'v1.0', 'file': 'thumb.png', 'sha256': h(one)}, items
            assert not r['same_repo_update'] and r['rows'][0]['result'] == 'thumbnail changed (already in the shop)'
            md = submit_mod.report_md(status, r)
            assert '```text\n' + thumbs.braille(thumbs.check(one)['bits']) in md and 'A maintainer looks at' in md
            # a daily update with the same picture: the pin moves to the newer release, and it merges by itself
            status, r, items = run('v1.1', update=True)
            assert status == 0 and r['same_repo_update'] and items[0]['file'] == 'x-1.1.elemod', r
            assert items[0]['thumb'] == {'tag': 'v1.1', 'file': 'thumb.png', 'sha256': h(one)}
            # a daily update with a new picture: the file goes in, the card keeps its picture, the report says so
            status, r, items = run('v1.2', update=True)
            assert status == 0 and r['same_repo_update'] and items[0]['file'] == 'x-1.2.elemod'
            assert items[0]['thumb']['tag'] == 'v1.1' and 'keeps the shop\'s thumbnail for X' in submit_mod.report_md(status, r)
            # the same release through the form: now it changes, with a review
            status, r, items = run('v1.2')
            assert status == 0 and not r['same_repo_update'] and items[0]['thumb']['sha256'] == h(two)
            # a picture that fails never stops the mod: it goes in with the picture it had
            status, r, items = run('v1.3')
            assert status == 0 and items[0]['file'] == 'x-1.3.elemod' and items[0]['thumb']['sha256'] == h(two)
            assert '- `thumb.png`: not taken: it looks dithered' in submit_mod.report_md(status, r)
            assert r['same_repo_update']
    finally:
        submit_mod.get, submit_mod.facts = real


def test_a_release_brings_a_picture_for_each_mod():
    """submit_mod.py: thumb-<mod id>.png is that mod's picture on every device, thumb.png the others'; many new
    pictures go to the pull request's comments in parts, each part marked once."""
    submit_mod = submit_mod_module()
    if submit_mod is None:
        return
    import hashlib
    thumbs = thumbs_module()
    pic = lambda k: thumbs.canonical_png([row[k:] + row[:k] for row in screen_bits()])
    def mod(id_, device):
        return {'id': id_, 'version': '1', 'device': device, 'os': '1.43' if device == 'digitone-mk1' else '1.53',
                'license': 'MIT', 'named_license': True, 'title': id_, 'core': False, 'requires': [], 'needs_core': None,
                'description': ''}
    FACTS = {'dn-beep.elemod': mod('beep', 'digitone-mk1'), 'dt-beep.elemod': mod('beep', 'digitakt-mk1'),
             'dn-horror.elemod': mod('horror', 'digitone-mk1'), 'dt-horror.elemod': mod('horror', 'digitakt-mk1'),
             'dt-loop.elemod': mod('loop', 'digitakt-mk1')}
    PICTURES = {'thumb-beep.png': pic(1), 'thumb-horror.png': thumbs.canonical_png(screen_bits('dithered')),
                'thumb.png': pic(2), 'thumb-gone.png': pic(3), 'thumbnail.png': pic(4)}
    def get(url, accept='', limit=0):
        if '/releases/tags/' in url:
            return json.dumps({'html_url': 'https://github.com/o/splash/releases/tag/v1', 'prerelease': False,
                               'assets': [{'name': n} for n in list(FACTS) + list(PICTURES)]}).encode()
        name = url.rsplit('/', 1)[1]
        return PICTURES[name] if name in PICTURES else ('file ' + name).encode()
    real = submit_mod.get, submit_mod.facts
    submit_mod.get, submit_mod.facts = get, lambda path, license='': dict(FACTS[os.path.basename(path)])
    try:
        with tempfile.TemporaryDirectory() as tmp:
            catalog = os.path.join(tmp, 'catalog.json')
            with open(catalog, 'w', encoding='utf-8') as fh:
                fh.write(submit_mod.render({'about': '', 'items': []}))
            status, r = submit_mod.submit({'repo': 'o/splash', 'tag': 'v1', 'summary': 'A splash.'}, catalog)
            with open(catalog, encoding='utf-8') as fh:
                got = {i['file']: (i.get('thumb') or {}).get('file') for i in json.load(fh)['items']}
            assert status == 0 and not r['same_repo_update']
            # beep's own picture on both devices; horror's failed, so it gets none (not thumb.png); loop gets thumb.png
            assert got == {'dn-beep.elemod': 'thumb-beep.png', 'dt-beep.elemod': 'thumb-beep.png', 'dn-horror.elemod': None,
                           'dt-horror.elemod': None, 'dt-loop.elemod': 'thumb.png'}, got
            md = submit_mod.report_md(status, r)
            for said in ('- `thumb-beep.png`: new on beep', '- `thumb-horror.png`: not taken: it looks dithered',
                         '- `thumb.png`: new on loop', '- `thumb-gone.png`: not used: no mod this submission adds or '
                         'changes has the id gone'):
                assert said in md, said
            assert 'thumbnail.png' not in md and md.count('```text') == 2
    finally:
        submit_mod.get, submit_mod.facts = real
    # 60 new pictures: more than a comment holds, so they go out in parts, each under GitHub's limit, marked alike
    r = {'thumbs': {}, 'thumb_on': {}, 'thumb_new': ['m'], 'thumb_waits': []}
    for i in range(60):
        data = pic(i)
        r['thumbs']['thumb-m%d.png' % i] = thumbs.check(data)
        r['thumb_on']['thumb-m%d.png' % i] = {'new': {'m%d' % i}}
    parts = submit_mod.previews(r)
    assert len(parts) == 2 and all(len(p) < 65536 for p in parts)
    mark = parts[0].split('\n')[0]
    assert mark.startswith('<!-- shop-pictures ') and parts[1].startswith(mark) and 'part 2 of 2' in parts[1]
    assert sum(p.count('```text') for p in parts) == 60
    assert 'in 2 comments on the pull request' in submit_mod.report_md(0, dict(r, repo='o/x', tag='v1', rows=[], errors=[]))


def test_publish_fills_in_the_form_and_writes_notes_the_shop_reads():
    """elekloader.sdk.publish: the repository from a git remote, the release's notes (which the shop's check reads
    back), the filled-in Submit a mod form, and whether the shop lists a mod already."""
    from urllib.parse import parse_qs, urlparse
    from elekloader.sdk import publish
    for url in ('https://github.com/me/my-mod.git', 'git@github.com:me/my-mod.git', 'https://github.com/me/my-mod\n',
                'ssh://git@github.com/me/my-mod.git'):
        assert publish.repo_from_remote(url) == 'me/my-mod', url
    assert publish.repo_from_remote('https://gitlab.com/me/my-mod.git') is None
    notes = publish.release_notes('A  delay.', 'Ran on a\nDigitakt.')
    assert notes == 'Shop summary: A delay.\nTested on a unit: Ran on a Digitakt.\n'
    assert publish.release_notes() == 'Built with elekloader.\n'
    submit_mod = submit_mod_module()
    if submit_mod is not None:
        assert submit_mod.release_text(notes) == {'summary': 'A delay.', 'on_unit': 'Ran on a Digitakt.'}
    q = parse_qs(urlparse(publish.form_url('me/my-mod', 'v1.0', 'My Mod', 'One sentence.')).query)
    assert {k: v[0] for k, v in q.items()} == {'template': 'submit-mod.yml', 'title': 'Submit a mod: My Mod',
                                               'repo': 'me/my-mod', 'tag': 'v1.0', 'summary': 'One sentence.'}
    items = [{'repo': 'Me/My-Mod', 'id': 'my-mod', 'file': 'my-mod-0.9.elemod'},
             {'repo': 'me/my-mod', 'id': 'my-mod-extra', 'file': 'my-extra-1.elemod'},
             {'repo': 'other/x', 'id': 'my-mod', 'file': 'my-mod-1.elemod'}]
    assert publish.in_shop(items, 'me/my-mod', 'my-mod') and not publish.in_shop(items, 'me/my-mod', 'my')
    assert not publish.in_shop(items, 'other/y', 'my-mod')


def test_a_commit_pin_and_a_removal_through_the_forms():
    """The Submit a mod form with a commit instead of a release (the files in its tree), a bundled copy of another
    author's mod flagged for every OS of the device, and the Remove a mod form. GitHub and the files are faked."""
    submit_mod = submit_mod_module()
    if submit_mod is None:
        return
    def mod(id_, version, os_='1.53', requires=('core',)):
        return {'id': id_, 'version': version, 'device': 'digitakt-mk1', 'os': os_, 'license': 'MIT', 'named_license': True,
                'title': id_.upper(), 'core': id_ == 'core', 'requires': [] if id_ == 'core' else list(requires),
                'needs_core': None, 'description': ''}
    FACTS = {'core-4.1.elemod': mod('core', '4.1'), 'core-4.1-os1.54.elemod': mod('core', '4.1', '1.54'),
             's-1.elemod': mod('s', '1'), 's-1-os1.54.elemod': mod('s', '1', '1.54'), 'c-1-os1.54.elemod': mod('c', '1', '1.54'),
             'm-1.elemod': mod('m', '1'), 'n-1.elemod': mod('n', '1', requires=('core', 'm'))}
    SHA = 'ab' * 20
    def get(url, accept='', limit=0):
        if '/commits/' in url:
            if not url.endswith(SHA):
                raise submit_mod.urllib.error.HTTPError(url, 422, 'No commit', {}, None)
            return json.dumps({'html_url': 'https://github.com/owner/c/commit/' + SHA}).encode()
        if '/git/trees/' in url:
            return json.dumps({'tree': [{'path': p, 'type': 'blob'} for p in
                                        ('README.md', 'elemods/c-1-os1.54.elemod', 'elemods/s-1-os1.54.elemod')]}).encode()
        if '/contents/' in url:
            return ('file ' + url.split('?')[0].rsplit('/', 1)[1]).encode()
        if url.startswith('https://api.github.com/repos/') and url.count('/') == 5:
            return json.dumps({'full_name': url.split('/repos/')[1]}).encode()
        raise AssertionError(url)
    real = submit_mod.get, submit_mod.facts
    submit_mod.get, submit_mod.facts = get, lambda path, license='': dict(FACTS[os.path.basename(path)])
    try:
        with tempfile.TemporaryDirectory() as tmp:
            shop, cores = os.path.join(tmp, 'shop'), os.path.join(tmp, 'core')
            for d, names in ((shop, ['s-1.elemod', 'm-1.elemod', 'n-1.elemod']),
                             (cores, ['core-4.1.elemod', 'core-4.1-os1.54.elemod'])):
                os.mkdir(d)
                for n in names:
                    open(os.path.join(d, n), 'wb').close()
            catalog = os.path.join(tmp, 'catalog.json')
            items = [{'repo': 'owner/%s' % f[0], 'tag': 'v1', 'file': f, 'device': 'digitakt-mk1', 'sha256': '0' * 64}
                     for f in ('s-1.elemod', 'm-1.elemod', 'n-1.elemod')]
            def fresh():
                with open(catalog, 'w', encoding='utf-8') as fh:
                    fh.write(submit_mod.render({'about': '', 'items': items}))
            def got():
                with open(catalog, encoding='utf-8') as fh:
                    return {submit_mod.os.path.basename(i.get('file') or i['path']): i for i in json.load(fh)['items']}
            # a commit: its tree's .elemod files; the bundled copy of owner/s's mod is flagged though the shop has
            # s only for 1.53
            fresh()
            status, r = submit_mod.submit({'repo': 'owner/c', 'commit': SHA, 'summary': 'C.'}, catalog, shop, cores)
            res = {row['file']: row['result'] for row in r['rows']}
            assert status == 0 and res['c-1-os1.54.elemod'] == 'added', res
            assert res['s-1-os1.54.elemod'].startswith('flagged, left out: the shop has s from owner/s for digitakt-mk1')
            c = got()['c-1-os1.54.elemod']
            assert (c['commit'], c['path'], 'tag' in c, 'file' in c) == (SHA, 'elemods/c-1-os1.54.elemod', False, False)
            for bad in ({'tag': 'v1', 'commit': SHA}, {'commit': 'abc1234'}):
                assert submit_mod.submit(dict(bad, repo='owner/c', summary='C.'), catalog, shop, cores)[0] == 1
            assert submit_mod.submit({'repo': 'owner/c', 'commit': 'cd' * 20, 'summary': 'C.'}, catalog, shop, cores)[0] == 1
            # removals: one another shop mod needs waits for a review; a self-contained one doesn't
            fresh()
            status, r = submit_mod.remove({'repo': 'owner/m', 'why': 'Moved on.'}, catalog, shop)
            assert status == 0 and not r['own_only'] and 'm-1.elemod' not in got(), r
            assert r['rows'][0]['result'] == 'removed; N 1 from owner/n needs m', r['rows']
            assert 'Why, from the issue: Moved on.' in submit_mod.report_md(status, r)
            fresh()
            status, r = submit_mod.remove({'repo': 'owner/s'}, catalog, shop)
            assert status == 0 and r['own_only'] and sorted(got()) == ['m-1.elemod', 'n-1.elemod']
            fresh()
            assert submit_mod.remove({'repo': 'owner/nobody'}, catalog, shop)[0] == 1
            assert submit_mod.remove({'repo': 'owner/s', 'files': 'x.elemod'}, catalog, shop)[0] == 1
    finally:
        submit_mod.get, submit_mod.facts = real
    assert submit_mod.as_of('As of 0.9: Ran.', '1.0') == 'As of 0.9: Ran.'
    assert submit_mod.release_text('Shop summary: One line.\n* Tested on a unit: yes') == {'summary': 'One line.', 'on_unit': 'yes'}

def test_authors_can_opt_out_of_automatic_updates():
    """Updates are on for every shop mod unless its entry says "auto_update": false: set by the form's Updates box or
    a 'Shop auto-update: off' line in the release's notes, cleared by the box unticked or an 'on' line. A newer release
    that says off isn't taken: it opts the repository out. GitHub and the files are faked."""
    submit_mod = submit_mod_module()
    if submit_mod is None:
        return
    import shop_updates
    def mod(id_, version):
        return {'id': id_, 'version': version, 'device': 'digitakt-mk1', 'os': '1.53', 'license': 'MIT', 'named_license': True,
                'title': id_.upper(), 'core': id_ == 'core', 'requires': [] if id_ == 'core' else ['core'],
                'needs_core': None, 'description': ''}
    FACTS = {n + '.elemod': mod(*n.split('-')) for n in ('core-4.1', 'x-1.0', 'x-1.1', 'y-1.0', 'y-1.1', 'y-1.2', 'z-1')}
    notes = {'v1.1': '', 'v1.2': 'Shop auto-update: off\n', 'v1.0': '', 'z1': '', 'z1-off': '- **Shop auto-update:** off', 'z1-on': 'Shop auto-update: on'}
    RELEASES = [('v1.0', '2026-10-02', ['x-1.0.elemod', 'y-1.0.elemod']), ('v1.1', '2026-10-03', ['x-1.1.elemod', 'y-1.1.elemod']),
                ('v1.2', '2026-10-04', ['y-1.2.elemod'])]
    def release(t):
        tag, when, files = next(x for x in RELEASES if x[0] == t)
        return {'tag_name': tag, 'published_at': when + 'T00:00:00Z', 'prerelease': False, 'draft': False, 'body': notes[tag],
                'html_url': 'https://github.com/owner/x/releases/tag/' + tag, 'assets': [{'name': n} for n in files]}
    def get(url, accept='', limit=0):
        if url.endswith('/releases?per_page=100'):
            return json.dumps([release(t[0]) for t in reversed(RELEASES)]).encode()
        if '/releases/tags/' in url:
            tag = url.rsplit('/', 1)[1]
            if tag.startswith('z'):
                return json.dumps({'html_url': 'https://github.com/owner/z/releases/tag/' + tag, 'prerelease': False,
                                   'body': notes[tag], 'assets': [{'name': 'z-1.elemod'}]}).encode()
            return json.dumps(release(tag)).encode()
        return ('file ' + url.rsplit('/', 1)[1]).encode()
    real = submit_mod.get, submit_mod.facts
    submit_mod.get, submit_mod.facts = get, lambda path, license='': dict(FACTS[os.path.basename(path)])
    try:
        with tempfile.TemporaryDirectory() as tmp:
            shop, cores = os.path.join(tmp, 'shop'), os.path.join(tmp, 'core')
            for d, names in ((shop, ['x-1.0.elemod', 'y-1.0.elemod']), (cores, ['core-4.1.elemod'])):
                os.mkdir(d)
                for n in names:
                    open(os.path.join(d, n), 'wb').close()
            catalog = os.path.join(tmp, 'catalog.json')
            def entry(file, **more):
                return dict({'repo': 'owner/x', 'tag': 'v1.0', 'file': file, 'device': 'digitakt-mk1', 'sha256': '0' * 64,
                             'summary': file[0].upper() + '.'}, **more)
            def write(items):
                with open(catalog, 'w', encoding='utf-8') as fh:
                    fh.write(submit_mod.render({'about': '', 'items': items}))
            def read():
                with open(catalog, encoding='utf-8') as fh:
                    return {i['file']: i for i in json.load(fh)['items']}
            def apply():
                import contextlib
                import io
                with contextlib.redirect_stdout(io.StringIO()):
                    code = shop_updates.main(['--apply', 'owner/x', '--catalog', catalog, '--files-dir', shop,
                                              '--cores-dir', cores, '--report', os.path.join(tmp, 'report.md'),
                                              '--result', os.path.join(tmp, 'result.json')])
                with open(os.path.join(tmp, 'result.json')) as fh:
                    return code, json.load(fh)
            # x opted out, y not: the repository is followed, but only y moves (v1.2 says off, so y stops at 1.1)
            write([entry('x-1.0.elemod', auto_update=False), entry('y-1.0.elemod')])
            assert list(shop_updates.shop_repos(shop_updates.read_items(catalog))) == ['owner/x']
            code, result = apply()
            got = read()
            assert code == 0 and result['tags'] == ['v1.1', 'v1.2'], result
            assert sorted(got) == ['x-1.0.elemod', 'y-1.1.elemod'] and got['x-1.0.elemod']['auto_update'] is False, sorted(got)
            assert got['y-1.1.elemod']['auto_update'] is False                 # the 'off' release set it, took no file
            assert shop_updates.shop_repos(shop_updates.read_items(catalog)) == {}  # now nothing to follow
            with open(os.path.join(tmp, 'report.md'), encoding='utf-8') as fh:
                report = fh.read()
            assert 'its author turned automatic updates off' in report and 'Shop auto-update: off' in report, report
            # the form: ticked box opts a new mod out; the same release again with it unticked opts back in
            write([entry('x-1.0.elemod')])
            box = '- [x] Don\'t update these mods automatically.'
            assert submit_mod.parse_issue('### Updates\n\n' + box + '\n') == {'updates': box}
            status, r = submit_mod.submit({'repo': 'owner/z', 'tag': 'z1', 'summary': 'Z.', 'updates': box}, catalog, shop, cores)
            assert status == 0 and read()['z-1.elemod']['auto_update'] is False and 'Automatic updates are off' in submit_mod.report_md(status, r)
            status, r = submit_mod.submit({'repo': 'owner/z', 'tag': 'z1', 'summary': 'Z.', 'updates': '- [ ] Don\'t'}, catalog, shop, cores)
            assert status == 0 and r['same_repo_update'] and r['rows'][0]['result'] == 'auto-update turned on (already in the shop)', r['rows']
            assert 'auto_update' not in read()['z-1.elemod'] and 'Automatic updates are on' in submit_mod.report_md(status, r)
            assert submit_mod.submit({'repo': 'owner/z', 'tag': 'z1', 'summary': 'Z.'}, catalog, shop, cores)[0] == 2   # nothing changes
            # the release's own line opts out, and 'on' wins over the box
            status, r = submit_mod.submit({'repo': 'owner/z', 'tag': 'z1-off', 'summary': 'Z.'}, catalog, shop, cores)
            assert status == 0 and read()['z-1.elemod']['auto_update'] is False
            status, r = submit_mod.submit({'repo': 'owner/z', 'tag': 'z1-on', 'summary': 'Z.', 'updates': box}, catalog, shop, cores)
            assert status == 0 and 'auto_update' not in read()['z-1.elemod']
    finally:
        submit_mod.get, submit_mod.facts = real
    assert submit_mod.release_text('Shop auto-update: Off\n* Shop auto-update: yes') == {'auto_update': True}
    assert submit_mod.release_text('Shop auto-update: off') == {'auto_update': False}
    assert submit_mod.release_text('Shop auto-update: later') == {}
    from elekloader.sdk import publish
    assert publish.release_notes('A.', '', auto_update=False) == 'Shop summary: A.\nShop auto-update: off\n'
    assert submit_mod.release_text(publish.release_notes('A.', '', False)) == {'summary': 'A.', 'auto_update': False}


def build_web_module():
    """packaging/build_web.py, or None where the repository isn't (under Pyodide, as submit_mod_module)."""
    folder = os.path.join(os.path.dirname(HERE), 'packaging')
    if not os.path.isdir(folder):
        return None
    sys.path.insert(0, folder)
    import build_web
    return build_web


def shop_list_or_skip(path):
    """The shop's real list (a clone of elekloader/shop: $ELEKLOADER_CATALOG, else build/shop-list/), or None,
    said, where there is none (tests.yml clones it; docs/WEB.md, "Where the list is")."""
    if os.path.exists(path):
        return path
    print('  skipped: no shop list at %s (git clone https://github.com/elekloader/shop build/shop-list)' % path)
    return None


def test_the_shops_list_can_come_from_another_repository():
    """$ELEKLOADER_CATALOG: the shop's list from a clone of its own repository (SHOP_REPO), sections beside it."""
    build_web = build_web_module()
    if build_web is None:
        return
    old = os.environ.pop('ELEKLOADER_CATALOG', None)
    try:
        assert build_web.catalog_path() == os.path.join(build_web.ROOT, 'build', 'shop-list', 'catalog.json')
        assert build_web.sections_path() == os.path.join(build_web.ROOT, 'build', 'shop-list', 'sections.json')
        with tempfile.TemporaryDirectory() as d:
            os.environ['ELEKLOADER_CATALOG'] = os.path.join(d, 'catalog.json')
            assert build_web.catalog_path() == os.path.join(d, 'catalog.json')
            assert build_web.catalog_path('x.json') == 'x.json'
            assert build_web.sections_path() == os.path.join(os.path.abspath(d), 'sections.json')
            with open(os.path.join(d, 'sections.json'), 'w') as fh:
                json.dump({'sections': [{'id': 'pics', 'title': 'Pictures', 'packs': [{'repo': 'a/b'}]}]}, fh)
            assert [s['id'] for s in build_web.sections([{'repo': 'a/b'}])] == ['pics']
    finally:
        os.environ.pop('ELEKLOADER_CATALOG', None)
        if old is not None:
            os.environ['ELEKLOADER_CATALOG'] = old


def test_the_builder_page_names_the_shops_site():
    """build_web.page_mode: the shop page as it is; the builder stamped <html data-page="builder" data-shop-url>."""
    build_web = build_web_module()
    if build_web is None:
        return
    html = '<!doctype html>\n<html lang="en">\n<head>'
    assert build_web.page_mode(html, 'shop') == html
    assert build_web.page_mode(html, 'builder') == html.replace(
        '<html lang="en">', '<html lang="en" data-page="builder" data-shop-url="https://elekloader.github.io/shop/">')
    for bad in (('builder', 'http://x.example/'), ('builder', 'https://x.example'), ('builder', 'https://x/"><script>/'),
                ('other', build_web.SHOP_URL)):
        try:
            build_web.page_mode(html, bad[0], bad[1])
            raise AssertionError('not refused: %r' % (bad,))
        except SystemExit:
            pass
    # the page's parts: the shop's hidden on the builder, its links to the shop's site shown only there
    web = os.path.join(build_web.ROOT, 'web')
    with open(os.path.join(web, 'style.css'), encoding='utf-8') as fh:
        css = fh.read()
    assert 'html[data-page="builder"] .shop-only { display: none !important; }' in css
    assert 'html:not([data-page="builder"]) .builder-only { display: none !important; }' in css
    with open(os.path.join(web, 'index.html'), encoding='utf-8') as fh:
        page = fh.read()
    for shop_part in ('id="nav-library"', 'id="nav-sections"', 'id="nav-submit"', 'id="tab-library"', 'id="tab-submit"'):
        tag = page[page.rindex('<', 0, page.index(shop_part)):page.index('>', page.index(shop_part))]
        assert 'shop-only' in tag, shop_part
    assert page.count('data-shop-link') >= 4 and 'shop-banner builder-only' in page


def test_the_shops_sections_are_checked():
    """sections.json: whole repositories on a page of their own (build_web.sections)."""
    build_web = build_web_module()
    if build_web is None or shop_list_or_skip(build_web.catalog_path()) is None:
        return
    with open(build_web.sections_path(), encoding='utf-8') as fh:
        real = json.load(fh)
    with open(build_web.catalog_path(), encoding='utf-8') as fh:
        repos = {i['repo'] for i in json.load(fh)['items']}
    # every pack names a repository the shop carries
    assert all(p['repo'] in repos for s in real['sections'] for p in s['packs'])
    out = build_web.sections([{'repo': r} for r in repos])
    assert [s['id'] for s in out] == [s['id'] for s in real['sections']] and all(s['nav'] for s in out)
    # the Splash screen maker is a tool of Splash & screens
    assert {'href': '#splash', 'title': 'Splash screen maker'} in next(s for s in out if s['id'] == 'screens')['tools']
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, 's.json')
        for bad in ({'sections': [{'id': 'A b', 'title': 'x', 'packs': [{'repo': 'a/b'}]}]},
                    {'sections': [{'id': 'build', 'title': 'x', 'packs': [{'repo': 'a/b'}]}]},
                    {'sections': [{'id': 'x', 'title': 'x', 'packs': [{'repo': 'a/b'}]},
                                  {'id': 'y', 'title': 'y', 'packs': [{'repo': 'a/b'}]}]},
                    {'sections': [{'id': 'x', 'title': 'x', 'packs': []}]},
                    {'sections': [{'id': 'x', 'title': 'x', 'packs': [{'repo': 'a/b'}],
                                   'tools': [{'href': 'https://x', 'title': 't'}]}]},
                    {'sections': [{'id': 'x', 'title': 'x', 'packs': [{'repo': 'a/b'}],
                                   'tools': [{'href': '#splash', 'title': 't'}]},
                                  {'id': 'y', 'title': 'y', 'packs': [{'repo': 'c/d'}],
                                   'tools': [{'href': '#splash', 'title': 't'}]}]}):
            with open(p, 'w') as fh:
                json.dump(bad, fh)
            try:
                build_web.sections([], p)
                raise AssertionError('not refused: %r' % bad)
            except SystemExit:
                pass
        assert build_web.sections([], os.path.join(d, 'none.json')) == []

def test_the_splash_maker_writes_a_mod_the_loader_takes():
    from elekloader import splash
    rows = [[(x + y) % 3 == 0 for x in range(128)] for y in range(64)]
    f0 = splash.pack(rows)
    assert len(f0) == 1024 and splash.unpack(f0) == [[int(v) for v in r] for r in rows]
    # the panel's layout: (0, 0) is page 7's bit 0, (127, 63) page 0's bit 7
    one = [[0] * 128 for _ in range(64)]
    one[0][0] = one[63][127] = 1
    assert splash.pack(one)[7] == 1 and splash.pack(one)[8 * 127] == 0x80
    f1 = bytes(1024)
    for t in splash.targets():
        doc = splash.make([f0, f1], [0, 0, 1], device=t['device'], os=t['os'], title='  My  logo ',
                          loop=False, source='logo.gif')
        raw = splash.dumps(doc)
        m = link.Mod2(json.loads(raw), 'splash')
        assert m.id == 'splash' and (m.dev.key, m.rel.version) == (t['device'], t['os'])
        assert doc['title'] == 'My logo' and doc['exports'] == ['splash_diff', 'splash_flush', 'splash_tick']
        body = bytes.fromhex(doc['sections']['.run']['parts'][0][1])
        code = splash.code(t['device'], t['os'])
        assert body.startswith(code) and len(code) % 4 == 0
        tab = splash.TABLES[(t['device'], t['os'])]
        for k, at in splash.FIELDS.items():
            assert int.from_bytes(body[at:at + 4], 'big') == tab[k]
        d = body[len(code):]
        n, pos, start, loop = struct.unpack('>4I', d[:16])
        assert (n, pos, loop) == (3, 0, 0) and d[16:22] == bytes.fromhex('000000000001')
        assert d[start:start + 1024] == f0 and d[start + 1024:] == f1
        # every intro call to a present goes through the splash, and the timer's post
        assert len(doc['sites']) == len(tab['diff_sites']) + len(tab['flush_sites']) + 1
        assert [s['relocs'][0][2] for s in doc['sites']].count('sym:splash_tick') == 1
        assert doc['sites'][-1]['addr'] == '0x%08x' % (tab['isr'] + splash.TICK_SITE)
    for bad in (lambda: splash.make([], device='digitakt-mk1', os='1.54'),
                lambda: splash.make([f0], [1], device='digitakt-mk1', os='1.54'),
                lambda: splash.make([f0] * (splash.MAX_FRAMES + 1), device='digitakt-mk1', os='1.54'),
                lambda: splash.make([f0], device='digitakt-mk1', os='1.52'),
                lambda: splash.make([f0[:10]], device='digitone-mk1', os='1.44')):
        try:
            bad()
            raise AssertionError('not refused')
        except splash.SplashError:
            pass
    assert splash.file_name('digitone-mk1', '1.44') == 'splash-dn-os1.44.elemod'


def test_the_splash_code_is_its_source_assembled():
    """CODE is examples/splash/splash.s, when the cross assembler is there."""
    import shutil
    import subprocess
    from elekloader import splash
    asm = shutil.which('m68k-linux-gnu-as')
    if not asm:
        print('  skipped: no m68k-linux-gnu-as')
        return
    src = os.path.join(os.path.dirname(HERE), 'examples', 'splash', 'splash.s')
    for t in splash.targets():
        tab = splash.TABLES[(t['device'], t['os'])]
        with tempfile.TemporaryDirectory() as d:
            o = os.path.join(d, 's.o')
            subprocess.run([asm, '-mcpu=54455', '-o', o, src] + [
                a for k in ('ISR', 'FB', 'DIFF', 'FLUSH', 'POST')
                for a in ('--defsym', '%s=0x%08x' % (k, tab[k.lower()]))], check=True)
            b = os.path.join(d, 's.bin')
            subprocess.run([asm.replace('-as', '-objcopy'), '-O', 'binary', '--only-section=.run', o, b],
                           check=True)
            with open(b, 'rb') as fh:
                assert fh.read() == splash.code(t['device'], t['os'])


# Folders that had no README, and examples no page named, when the test below was written. The
# lists only get shorter: write the README (or name the example) and take the folder out.
MODS_WITHOUT_README = {'core-mc', 'core-ms', 'machines-dn2', 'sdk-alpha', 'seq-events'}
EXAMPLES_NOWHERE = {'boot-marker-mc', 'crush-machine-ot', 'hello-marker-model', 'hooks-check-dn1', 'machine-ar2',
                    'master-mono', 'sdk-glyphs', 'sdk-hello-world', 'sdk-mono', 'sdk-sine', 'track-marker-mc'}


def test_every_mod_has_a_readme_and_every_example_is_listed():
    """A mod's sites, build and checks live in mods/<mod>/README.md (the doc-hygiene skill, "One
    home per fact"). An example has a README of its own, or docs/README.md or examples/README.md
    names it."""
    root = os.path.dirname(HERE)
    folders = lambda top: sorted(d for d in os.listdir(os.path.join(root, top))
                                 if os.path.isdir(os.path.join(root, top, d)))
    has = lambda *p: os.path.exists(os.path.join(root, *p))
    if not has('packaging', 'build_web.py'):
        return      # under Pyodide (tests/test_web.mjs): a few of the repository's files, not its folders
    bare = {m for m in folders('mods') if not has('mods', m, 'README.md')}
    assert bare == MODS_WITHOUT_README, (
        'mods/<mod>/README.md is missing: %s; has one now, take it out of MODS_WITHOUT_README: %s'
        % (sorted(bare - MODS_WITHOUT_README), sorted(MODS_WITHOUT_README - bare)))
    index = ''
    for page in (('docs', 'README.md'), ('examples', 'README.md')):
        if has(*page):
            with open(os.path.join(root, *page), encoding='utf-8') as f:
                index += f.read()
    listed = lambda e: '`%s`' % e in index or 'examples/%s)' % e in index or '(%s)' % e in index
    lost = {e for e in folders('examples') if not has('examples', e, 'README.md') and not listed(e)}
    assert lost == EXAMPLES_NOWHERE, (
        'an example with no README that docs/README.md does not name: %s; named now, take it out of '
        'EXAMPLES_NOWHERE: %s' % (sorted(lost - EXAMPLES_NOWHERE), sorted(EXAMPLES_NOWHERE - lost)))


def test_core_ar2_0_12_room_for_drive_mods():
    """core-ar2 0.12's tables have room for +Drive mods (mods/core-ar2/README.md, "Room for +Drive
    mods"): its events for as many handlers as the Drive Loader links (256), not the default 16
    (found 2026-11-07: a 17th ev_tick handler was refused `full`), the machines table for its 10
    hidden slots less the flashed mods' machines, the page tables by the views' room (10 views of VIEW_MAX 16 pages, 11 of them
    stock's), the id-range tables by the loader's mods, and core_step_owners stopping at its 8
    owners (steps.c OWNERS)."""
    path = os.path.join(HERE, '..', 'mods', 'core-ar2', 'mod.json')
    if not os.path.exists(path):
        return      # under Pyodide (tests/test_web.mjs): a few of the repository's files, not its folders
    with open(path, encoding='utf-8') as fh:
        doc = json.load(fh)
    cols = doc['collections']
    assert tuple(map(int, doc['version'].split('.'))) >= (0, 12)
    for name, v in cols.items():
        if name.startswith('ev_'):
            assert isinstance(v, dict) and v.get('spare') == 256, (name, v)
    assert cols['core_machines'] == {'entry': 4, 'spare': 10, 'max': 10}   # less the flashed mods' machines
    for name in ('core_pages', 'core_page_fns', 'core_view_pages'):
        assert cols[name] == {'entry': 4, 'spare': 10 * 16 - 11}, name
    for name in ('core_params', 'core_param_locks', 'core_param_scales'):
        assert cols[name] == {'entry': 4, 'spare': 256}, name
    assert cols['core_step_owners'] == {'entry': 4, 'spare': 8, 'max': 8}
    assert cols['core_lock_params'] == 4      # its 32 spare are the linker's (link.DRIVE_SPARE)



def test_the_rytm_pre_link_ranges_are_ordered_and_inside_the_main_os():
    """The Analog Rytm MKII 1.74's pre-link ranges (the boot-order trace, docs/DRIVE.md): sorted, apart,
    inside the main OS, and holding main() up to the +Drive hook at 0x401009e0 but not the hook itself."""
    from elekloader import devices
    dev = [d for d in devices.devices() if d.key == 'analog-rytm-mk2'][0]
    rel = dev.releases['1.74']
    pre = rel.prelink
    assert pre == tuple(sorted(pre)) and all(lo < hi for lo, hi, _k, _w in pre)
    assert all(a[1] <= b[0] for a, b in zip(pre, pre[1:]))
    assert all(dev.main_load <= lo and hi <= dev.image_end(rel) and k in (1, 2) for lo, hi, k, _w in pre)
    assert any(lo <= 0x401006ee and hi == 0x401009e0 for lo, hi, _k, _w in pre)
    assert not any(lo <= 0x401009e0 < hi for lo, hi, _k, _w in pre)

if __name__ == '__main__':
    import traceback
    ok = failed = 0
    for n in sorted(k for k in globals() if k.startswith('test_')):
        try:
            globals()[n]()
            print('ok      %s' % n)
            ok += 1
        except Exception:
            print('FAIL    %s' % n)
            traceback.print_exc()
            failed += 1
    print('%d passed, %d failed' % (ok, failed))
    sys.exit(1 if failed else 0)
