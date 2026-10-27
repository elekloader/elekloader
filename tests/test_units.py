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


def test_dn2_filter_g_is_refused_unless_valid():
    from elekloader.sdk.sharc import filter_artifact
    dn = next(d for d in devices.devices() if d.key == 'digitone-mk2')
    r = dn.releases['1.12']
    target, image = devices.target_of(dn, r), bytes(r.main_len)
    core = link.Mod2({'elemod': 2, 'id': 'core', 'version': '0.1', 'target': target,
                      'sections': {'.boot': {'align': 4, 'parts': [['hex', '00000000']]}},
                      'collections': {'mk2_voice_filters': {'entry': 4}}}, 'core')

    def mod(label, default, g=True):
        """The descriptor {8, name, label, curve, g} at 0, G {label, default} at 20 and its label at 28."""
        text = '00000008' + '00' * 20 + '%08x' % default + (label or '').encode('latin1').hex() + '00'
        return link.Mod2({'elemod': 2, 'id': 'g', 'version': '0.1', 'target': target,
                          'sections': {'.run': {'align': 4, 'parts': [['hex', text]]}},
                          'symbols': {'descriptor': ['.run', 0], 'g': ['.run', 20], 'label': ['.run', 28]},
                          'relocs': ([['.run', 16, 'abs32', 'sym:g', 0]] if g else [])
                          + ([['.run', 20, 'abs32', 'sym:label', 0]] if label is not None else []),
                          'contribute': [{'to': 'mk2_voice_filters', 'data': '00000000',
                                          'relocs': [[0, 'abs32', 'sym:descriptor', 0]]}],
                          'sharc': filter_artifact(bytes(4), bytes(4), 0, 8, 12)}, 'g')
    refusal = 'g 0.1: DN2 filter G needs a label of 1..5 printable ASCII characters and a default of 0..127'
    for label, default in (('MODE', 0), ('HIGH5', 127)):
        assert link.check([core, mod(label, default)], image) == []
    assert link.check([core, mod(None, 0, g=False)], image) == []
    for label, default in (('MODE', 128), ('TOOWIDE', 1), ('', 1), ('M\tX', 1), (None, 1)):
        assert link.check([core, mod(label, default)], image) == [refusal], (label, default)


def test_dn2_machine_ids_can_span_stored_parts():
    from elekloader.sdk.sharc import voice_artifact
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
                          'sharc': voice_artifact(bytes(4), b'', 0, 5, 0)}, 'voice')

    for split in (1, 2, 3):
        for left_stock in (False, True):
            for right_stock in (False, True):
                parts = [(['stock', dn.main_load + 0x100, split] if left_stock
                          else ['hex', machine_id[:split].hex()]),
                         (['stock', dn.main_load + 0x100 + split, 4 - split] if right_stock
                          else ['hex', machine_id[split:].hex()]), ['hex', '00' * 12]]
                voice = mod(parts)
                assert voice.implied_names(image) == ['mk2-synth:5'], parts
                assert link.check([core, voice], image) == [], parts
                relocated = mod(parts, [('.run', 0, 'abs32', 'abs', 5)])
                assert relocated.implied_names(image) == []
                assert 'voice 0.1: SHARC voice machine_id must match its single native voice registration' in link.check(
                    [core, relocated], image)


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


def test_dn2_machines_suite():
    import unittest
    suite = unittest.TestLoader().discover(HERE, pattern='test_dn2_machines.py')
    assert suite.countTestCases(), 'no DN2 machine registry tests discovered'
    result = unittest.TextTestRunner().run(suite)
    assert result.wasSuccessful(), 'DN2 machine registry suite failed'


def test_sharc_suite():
    import unittest
    suite = unittest.TestLoader().discover(HERE, pattern='test_sharc*.py')
    assert suite.countTestCases(), 'no SHARC tests discovered'
    result = unittest.TextTestRunner().run(suite)
    assert result.wasSuccessful(), 'SHARC suite failed'


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
