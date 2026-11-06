# SPDX-License-Identifier: GPL-2.0-or-later
"""The +Drive tools (elekloader/drive.py, container.py, datarpc.py): the build-checker, the boot
linker's twin, the data container and `elekloader mods` (pytest, or run with python). Needs no
firmware, except the tests at the end, which take the owner's files:

    ELEKLOADER_AR2_SYX=<stock Rytm MKII 1.74 .syx> ELEKLOADER_CORE_AR2=<core-ar2 0.11 .elemod>
        [ELEKLOADER_DRIVE_MODS=<folder with drive-hello and drive-hello-user .elemod>]
        a real build of core-ar2 0.11 and a loader stub, and the example mods linked into it
    ELEKLOADER_AR2_DATA=<a stock project download, as the Data router sends it>
        the container round trip

The made-up build is test_link_refusals.Bench's (a release of nops of the real Rytm profile): a
core with ev_tick, ev_draw, ev_x (one spare entry) and the closed core_handles; a loader stub that
imports core_desc and exports a small staging buffer; and a flashed mod `flash-a` with an ev_draw
entry at order 60. Every refusal code the host can meet has a fixture that is refused and one that
loads (ROWS), and test_every_code_has_a_fixture fails for a code without them.
"""
import json
import os
import struct
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT)
sys.path.insert(0, HERE)

from elekloader import container, datarpc, desc, devices, drive, link   # noqa: E402
from elekloader.elemod import ModError                                    # noqa: E402
from test_link_refusals import Bench                                      # noqa: E402

STAGING = 0x10000


def flashed(B):
    core = B.core(collections={'ev_tick': {'entry': 4}, 'ev_draw': {'entry': 4},
                               'ev_x': {'entry': 4, 'spare': 1}, 'core_handles': {'entry': 4}})
    core['symbols']['core_zero'] = ['.boot', 0]
    core['exports'] = ['core_fn', 'core_zero']
    loader = B.other('drive-loader', imports=['core_desc'],
                     relocs=[['.run', 4, 'abs32', 'sym:core_desc', 0]])
    loader['sections']['.bss'] = {'size': STAGING}
    loader['symbols'].update({'drive_staging': ['.bss', 0], 'drive_staging_size': ['abs', STAGING]})
    loader['exports'] = ['drive_staging', 'drive_staging_size']
    fa = B.mod('flash-a', resources={'names': ['settings:A']})
    fa['contribute'] = [{'to': 'ev_draw', 'order': 60, 'data': '00000000',
                         'relocs': [[0, 'abs32', 'sym:fn_flash-a', 0]]}]
    fa['symbols']['flash_fn'] = ['.run', 4]
    fa['exports'] = ['flash_fn']
    return [core, loader, fa]


def built(B):
    out = link.link([link.Mod2(d, d['id']) for d in flashed(B)], B.img)
    return out, drive.load_build(out.image)


def dmod(B, ident, **extra):
    """A +Drive mod: 12 bytes of .run (code, then a word it relocates), 8 of .bss, an export."""
    d = {'elemod': 2, 'id': ident, 'version': '1', 'target': B.target, 'requires': ['core'],
         'sections': {'.run': {'align': 4, 'len': 12, 'parts': [['hex', '4e714e714e750000' + '00000000']]},
                      '.bss': {'size': 8}},
         'symbols': {'fn_' + ident: ['.run', 0], ident + '_count': ['.bss', 0]},
         'exports': [ident + '_count'], 'imports': ['core_fn'],
         'relocs': [['.run', 8, 'abs32', 'sym:core_fn', 0], ['.run', 6, 'pc16', 'sym:fn_' + ident, 0]]}
    d.update(extra)
    return d


def hello(B, **extra):
    d = dmod(B, 'drive-hello', **extra)
    d.setdefault('contribute', [{'to': 'ev_tick', 'order': 50, 'data': '00000000',
                                 'relocs': [[0, 'abs32', 'sym:fn_drive-hello', 0]]}])
    return d


def user(B, **extra):
    d = dmod(B, 'drive-hello-user', requires=['core', 'drive-hello'],
             imports=['core_fn', 'drive-hello_count'],
             relocs=[['.run', 8, 'abs32', 'sym:drive-hello_count', 0]])
    d.update({'contribute': [{'to': 'ev_draw', 'order': 10, 'data': '00000000',
                              'relocs': [[0, 'abs32', 'sym:fn_drive-hello-user', 0]]}]})
    d.update(extra)
    return d


def files(*docs):
    out = []
    for d in docs:
        if isinstance(d, tuple):
            out.append(d)
        else:
            out.append((d['id'] + '.elemod', json.dumps(d, sort_keys=True).encode('utf-8')))
    return out


def test_the_two_example_mods_link_as_the_unit_links_them():
    with Bench('analog-rytm-mk2') as B:
        out, b = built(B)
        assert (b.lo, b.hi, b.staging_size) == (0x47D00000, 0x47FFF000, STAGING)
        r = drive.link(b, files(user(B), hello(B)))
        assert [c.state for c in r.choices] == [drive.LOADED] * 2, [c.why() for c in r.choices]
        assert [c.id for c in r.live] == ['drive-hello', 'drive-hello-user']
        # .run of each in id order from the area's start, then each .bss
        h, u = 0x47D00000, 0x47D0000C
        assert r.base[('drive-hello', '.run')] == h and r.base[('drive-hello-user', '.run')] == u
        assert r.base[('drive-hello', '.bss')] == 0x47D00018
        assert r.base[('drive-hello-user', '.bss')] == 0x47D00020 and r.end == 0x47D00028
        core_fn = out.map['core_fn']
        assert r.area[:12] == bytes.fromhex('4e714e714e75fffa') + struct.pack('>I', core_fn)
        assert r.area[12:24] == bytes.fromhex('4e714e714e750000') + struct.pack('>I', 0x47D00018)
        assert len(r.area) == r.end - b.lo and r.area[24:] == bytes(16)
        # ev_draw: the +Drive entry at order 10 before the flashed one at 60, then zeros
        t = r.tables['ev_draw']
        assert t['entries'] == [('drive-hello-user', 10), ('flash-a', 60)]
        a, n, e = out.tables['ev_draw']
        assert t['address'] == a and len(t['bytes']) == 4 * (t['capacity'] + 1)
        assert t['bytes'][:8] == struct.pack('>II', u, out.map['flash-a:fn_flash-a'])
        assert t['bytes'][8:] == bytes(len(t['bytes']) - 8)
        assert r.tables['ev_tick']['bytes'][:8] == struct.pack('>II', h, 0)
        assert sorted(r.tables) == ['ev_draw', 'ev_tick']          # untouched tables aren't listed
        st, dt = drive.records(r)
        assert st[:4] == bytes((1, 0, 0, 0)) and st[4:4 + 17] == b'drive-hello-user\0'
        assert st[32:36] == bytes((1, 0, 0, 0)) and len(st) == len(dt) == 32 * 32


def test_the_same_inputs_give_the_same_bytes():
    """Link twice, in fresh objects, and compare every file the twin writes."""
    with Bench('analog-rytm-mk2') as B:
        out, _b = built(B)
        got = []
        for _ in range(2):
            b = drive.load_build(bytes(out.image))
            r = drive.link(b, files(user(B), hello(B), ('bad.elemod', b'{')))
            with tempfile.TemporaryDirectory() as tmp:
                paths = drive.write_layout(r, tmp)
                got.append({os.path.relpath(p, tmp): open(p, 'rb').read() for p in paths})
        assert got[0] == got[1]
        assert sorted(got[0]) == ['area.bin', 'detail.bin', 'status.bin', 'status.txt', 'summary.json',
                                  os.path.join('tables', 'ev_draw.bin'), os.path.join('tables', 'ev_tick.bin')]


def test_a_build_file_alone_checks_and_lays_out_but_has_no_table_bytes():
    with Bench('analog-rytm-mk2') as B:
        out, b = built(B)
        blk = out.image[desc.find(out.image)[0]:]
        only = drive.load_build(blk[:struct.unpack_from('>I', blk, 8)[0]])
        r = drive.link(only, files(hello(B), user(B)))
        full = drive.link(b, files(hello(B), user(B)))
        assert r.area == full.area and r.tables['ev_draw']['bytes'] is None
        assert r.tables['ev_draw']['entries'] == full.tables['ev_draw']['entries']
        wrapped = container.wrap(blk[:struct.unpack_from('>I', blk, 8)[0]], 2, 5, 0x30313736)
        assert drive.load_build(wrapped).crc == b.crc


def test_core_desc_and_table_counts_are_imports_a_mod_may_take():
    with Bench('analog-rytm-mk2') as B:
        out, b = built(B)
        d = hello(B, imports=['core_desc', 'ev_draw_n', 'ev_draw'],
                  relocs=[['.run', 8, 'abs32', 'sym:ev_draw_n', 0]])
        u = user(B)
        r = drive.link(b, files(d, u))
        assert all(c.state == drive.LOADED for c in r.choices), [c.why() for c in r.choices]
        assert r.area[8:12] == struct.pack('>I', 2)               # flash-a's and the user's entry
        assert b.desc_at == out.map['core_desc']


def test_drive_init_is_each_mods_own_and_never_an_export():
    with Bench('analog-rytm-mk2') as B:
        _out, b = built(B)
        h = hello(B)
        u = user(B)
        for d, at in ((h, 0), (u, 2)):
            d['symbols']['drive_init'] = ['.run', at]
            d['exports'] = d['exports'] + ['drive_init']
        r = drive.link(b, files(h, u))
        assert all(c.state == drive.LOADED for c in r.choices), [c.why() for c in r.choices]
        assert r.inits == [('drive-hello', 0x47D00000), ('drive-hello-user', 0x47D0000E)]
        assert 'drive_init' not in r.glob


def test_active_reads_as_the_boot_linker_reads_it():
    text = drive.active_text([('a.elemod', b'x'), ('b c.elemod', b'')])
    assert text == 'a.elemod 8cdc1683\nb c.elemod 00000000\n'
    assert drive.parse_active(text) == [('a.elemod', 0x8cdc1683), ('b c.elemod', 0)]
    assert drive.parse_active('# x\n\n  a.elemod   0x1F \r\nnocrc\nb.elemod zz\n') == [('a.elemod', 0x1f)]
    assert drive.parse_active(''.join('m%d.elemod 1\n' % i for i in range(33))) is None
    for bad in ('#x', ' a', 'a\nb', ''):
        try:
            drive.active_text([(bad, b'')])
            raise AssertionError(bad)
        except ModError:
            pass


# ---- every refusal: one fixture refused, one that loads ------------------------------------------

ROWS = {}


def row(code, line=0, detail=None, arg=None, deep=False):
    """A fixture: f(B) -> (bad files, good files); line `line` of the bad set gets `code`."""
    def deco(f):
        ROWS[code] = (f, line, detail, arg, deep)
        return f
    return deco


@row('too-big')
def _(B):
    return files(hello(B, description='x' * STAGING)), files(hello(B, description='x'))


@row('json')
def _(B):
    return files(('bad.elemod', b'{"elemod": 2,')), files(hello(B))


@row('format')
def _(B):
    return files(hello(B, version=1)), files(hello(B))


@row('target', 0)
def _(B):
    t = dict(B.target, os='1.73')
    return files(hello(B, target=t)), files(hello(B))


@row('feature', detail='sites')
def _(B):
    return files(hello(B, sites=[B.jmp_site(B.at(0x1000), 'fn_drive-hello')])), files(hello(B, sites=[]))


@row('duplicate', 1, detail='drive-hello')
def _(B):
    return files(hello(B), hello(B)), files(hello(B))


@row('core', detail='2')
def _(B):
    return files(hello(B, resources={'core': '2'})), files(hello(B, resources={'core': '1'}))


@row('requires', detail='drive-hello')
def _(B):
    return files(user(B)), files(user(B), hello(B))


@row('conflicts', detail='flash-a')
def _(B):
    return files(hello(B, conflicts=['flash-a'])), files(hello(B, conflicts=['flash-b']))


@row('export', detail='flash_fn')
def _(B):
    d = hello(B)
    d['symbols']['flash_fn'] = ['.run', 0]
    d['exports'] = ['flash_fn']
    g = hello(B)
    g['symbols']['flash_fn2'] = ['.run', 0]
    g['exports'] = ['flash_fn2']
    return files(d), files(g)


@row('claim', detail='settings:A')
def _(B):
    return files(hello(B, resources={'names': ['settings:A']})), files(hello(B, resources={'names': ['settings:B']}))


@row('import', detail='nothing_here')
def _(B):
    return files(hello(B, imports=['core_fn', 'nothing_here'])), \
        files(hello(B, imports=['core_fn', 'nothing_here'], weak=['nothing_here']))


@row('table', detail='core_handles')
def _(B):
    c = [{'to': 'core_handles', 'order': 50, 'data': '00000000', 'relocs': []}]
    return files(hello(B, contribute=c)), files(hello(B, contribute=[dict(c[0], to='ev_tick')]))


@row('full', 1, detail='ev_x', arg=1)
def _(B):
    c = {'to': 'ev_x', 'order': 50, 'data': '00000000', 'relocs': []}
    return files(hello(B, contribute=[c]), user(B, contribute=[c])), \
        files(hello(B, contribute=[c]), user(B))


@row('room', arg=1)
def _(B):
    room = 0x47FFF000 - 0x47D00000 - 12
    return files(hello(B, sections={'.run': dmod(B, 'x')['sections']['.run'], '.bss': {'size': room + 1}})), \
        files(hello(B, sections={'.run': dmod(B, 'x')['sections']['.run'], '.bss': {'size': room}}))


@row('range', deep=True)
def _(B):
    far = [['.run', 6, 'pc16', 'sym:core_fn', 0]]
    return files(hello(B, relocs=far)), files(hello(B, relocs=[['.run', 6, 'pc32', 'sym:core_fn', 0]][:0]))


@row('limit')
def _(B):
    many, some = hello(B), hello(B)
    for i in range(drive.SYMS):
        many['symbols']['s%d' % i] = ['abs', i]
    for i in range(1000):
        some['symbols']['s%d' % i] = ['abs', i]
    return files(dict(many, exports=sorted(many['symbols']))), files(dict(some, exports=sorted(some['symbols'])))


def test_the_boot_linkers_other_rooms_refuse_as_limit():
    """The names' arena and lists, shared by every file and not given back by a refused one, and
    one file's own symbols in pass 2."""
    with Bench('analog-rytm-mk2') as B:
        _out, b = built(B)
        big = ['n%05d' % i for i in range(drive.ARENA // 7)]          # 7 bytes each: past the arena
        first = hello(B, resources={'names': big[:1000]}, exports=['not_a_symbol'])
        r = drive.check(b, files(first, hello(B, resources={'names': big[1000:]})))
        assert [(c.code, c.note) for c in r.choices] == [
            (drive.CODE['format'], 'exports not_a_symbol'), (drive.CODE['limit'], 'resources.names')], \
            [c.why() for c in r.choices]
        assert drive.check(b, files(hello(B, resources={'names': big[1000:]}))).choices[0].state == drive.LOADED
        locs = hello(B)
        for i in range(drive.LOCALS):
            locs['symbols']['s%d' % i] = ['abs', i]
        assert drive.check(b, files(locs)).choices[0].state == drive.LOADED     # pass 1 cannot tell
        r = drive.link(b, files(locs))
        assert (r.choices[0].code, r.skip) == (drive.CODE['limit'], 'none'), r.choices[0].why()


@row('count')
def _(B):
    good = [dmod(B, 'm%02d' % i) for i in range(32)]
    return files(*(good + [dmod(B, 'm32')])), files(*good)


def test_every_refusal_has_a_fixture_that_is_refused_and_one_that_loads():
    with Bench('analog-rytm-mk2') as B:
        _out, b = built(B)
        for code, (f, line, detail, arg, deep) in sorted(ROWS.items()):
            bad, good = f(B)
            run = drive.link if deep else drive.check
            rb, rg = run(b, bad), run(b, good)
            c = rb.choices[line]
            assert c.code == drive.CODE[code], (code, c.why(), [x.why() for x in rb.choices])
            if detail is not None:
                assert c.detail == detail, (code, c.detail)
            if arg is not None:
                assert c.arg == arg, (code, c.arg)
            assert all(x.state == drive.LOADED for x in rg.choices), (code, [x.why() for x in rg.choices])
            if deep:
                continue
            # the twin agrees with the checker on every row
            rl = drive.link(b, bad)
            assert [x.code for x in rl.choices] == [x.code for x in rb.choices], code


def test_every_code_has_a_fixture():
    host = set(drive.CODE) - set(drive.BOOT_ONLY) - {'ok'}
    assert host == set(ROWS), sorted(host ^ set(ROWS))


def test_each_feature_and_section_a_drive_mod_cannot_use_is_refused():
    with Bench('analog-rytm-mk2') as B:
        _out, b = built(B)
        for field, value in (('sites', [B.jmp_site(B.at(0x1000), 'fn_drive-hello')]),
                             ('collections', {'ev_mine': {'entry': 4}}), ('copied', [{'lo': 1, 'hi': 2, 'to': 3}]),
                             ('storage', [{'sym': 'st', 'scope': 'kit', 'bits': 1}]), ('sharc', {})):
            r = drive.check(b, files(hello(B, **{field: value})))
            want = 'sharc' if field == 'sharc' else field
            if field == 'sharc':                # an empty object is absent, as the loader reads it
                assert r.choices[0].code != drive.CODE['feature'] or r.choices[0].detail == want
                continue
            assert (r.choices[0].code, r.choices[0].detail) == (drive.CODE['feature'], field), r.choices[0].why()
        for sec in ('.boot', '.fast', '.payload'):
            d = hello(B)
            d['sections'][sec] = {'align': 4, 'len': 2, 'parts': [['hex', '4e75']]}
            r = drive.check(b, files(d))
            assert (r.choices[0].code, r.choices[0].detail) == (drive.CODE['feature'], sec)
        r = drive.check(b, files(hello(B, resources={'regions': [{'name': 'r', 'lo': 1, 'hi': 2}]})))
        assert (r.choices[0].code, r.choices[0].detail) == (drive.CODE['feature'], 'regions')
        c = [{'to': 'ev_tick', 'order': 50, 'data': '00000000', 'relocs': [], 'claims': [[1, 2]]}]
        r = drive.check(b, files(hello(B, contribute=c)))
        assert (r.choices[0].code, r.choices[0].detail) == (drive.CODE['feature'], 'claims')


def test_of_a_conflicting_pair_the_later_by_id_goes():
    with Bench('analog-rytm-mk2') as B:
        _out, b = built(B)
        a = hello(B, id='a-mod', conflicts=['b-mod'])
        z = hello(B, id='b-mod')
        for order in ((a, z), (z, a)):
            r = drive.check(b, files(*order))
            st = {c.id: (c.state, c.detail) for c in r.choices}
            assert st == {'a-mod': (drive.LOADED, ''), 'b-mod': (drive.REFUSED, 'a-mod')}


def test_a_refusal_in_pass_2_starts_the_check_again():
    """A mod refused for a pc16 out of range is dropped and the set laid out again: one that
    requires it goes too, and the rest move down."""
    with Bench('analog-rytm-mk2') as B:
        _out, b = built(B)
        a = dmod(B, 'a-far', relocs=[['.run', 6, 'pc16', 'sym:core_fn', 0]])
        r = drive.link(b, files(a, hello(B, requires=['a-far'])))
        st = [(c.id, c.code) for c in r.choices]
        assert st == [('a-far', drive.CODE['range']), ('drive-hello', drive.CODE['requires'])]
        r = drive.link(b, files(a, hello(B)))
        assert r.base[('drive-hello', '.run')] == 0x47D00000


def test_a_missing_file_is_dropped():
    with Bench('analog-rytm-mk2') as B:
        _out, b = built(B)
        r = drive.check(b, [('gone.elemod', None)] + files(hello(B)))
        assert (r.choices[0].state, r.choices[0].code) == (drive.DROPPED, drive.CODE['missing'])
        assert r.choices[1].state == drive.LOADED


def test_format_md_lists_every_code_and_state():
    with open(os.path.join(ROOT, 'docs', 'FORMAT.md'), encoding='utf-8') as fh:
        doc = fh.read()
    sec = doc[doc.index('### +Drive refusal codes'):]
    for code, (name, _m) in drive.CODES.items():
        assert '| %d | `%s` |' % (code, name) in sec, (code, name)
    for v, name in drive.SKIPS.items():
        assert '| %d | `%s`' % (v, name) in sec, (v, name)


# ---- the data container and the Data router --------------------------------------------------------

def test_the_container_wraps_and_reads_back():
    body = b'{"elemod": 2}'
    raw = container.wrap(body, 2, 5, 0x30313736)
    assert len(raw) == container.HEADER + len(body) + container.FOOTER
    assert raw[:31] == struct.pack('>IBHHIIiiIBB', 0xac11d303, 2, 2, 5, 0x30313736, 0x454d4f44, 2, 0,
                                   len(body), 0, 12)
    assert raw[-12:] == struct.pack('>III', container.crc(body), len(body), 0xaaa1daaa)
    head, got = container.parse(raw)
    assert got == body and (head['family'], head['device'], head['type']) == (2, 5, container.EMOD)
    assert container.rewrap(head, got) == raw
    for i, why in ((40, 'hash'), (len(raw) - 1, 'magic')):
        bad = bytearray(raw)
        bad[i] ^= 1
        try:
            container.parse(bytes(bad))
            raise AssertionError(why)
        except ModError as e:
            assert why in str(e)


def test_the_container_round_trips_a_stock_project_download():
    path = os.environ.get('ELEKLOADER_AR2_DATA')
    if not path:
        print('SKIP: ELEKLOADER_AR2_DATA')
        return
    raw = open(path, 'rb').read()
    head, body = container.parse(raw)
    assert (head['family'], head['device'], head['version']) == (2, 5, 2) and head['compressed']
    assert head['release'] == devices.for_target(devices.target_of(
        *[(d, d.releases['1.74']) for d in devices.devices() if d.key == 'analog-rytm-mk2'][0]))[1].data_release
    assert container.rewrap(head, body) == raw


class FakeUnit:
    """The Drive Loader's /mods route as the Data router answers it, in memory: lists, reads, writes
    and clears; checks each chunk's CRC and the container on close (family 2, type EMOD,
    uncompressed, the hash), as stock and the route do."""

    def __init__(self, build_blk):
        self.files = {'.build': build_blk, '.status': b''}
        self.jobs, self.out, self.nj = {}, [], 1

    def send(self, raw):
        msg = datarpc.unframe(raw)
        seq, cmd, a = struct.unpack_from('>H', msg)[0], msg[4], msg[5:]
        ok, extra = 1, b''
        if cmd == datarpc.LIST:
            for n in sorted(self.files):
                extra += n.encode() + b'\0' + bytes((0, 2)) + struct.pack('>iIHBB', 0, len(self.files[n]), 0, 1, 0)
            extra = bytes(12) + extra
        elif cmd == datarpc.ROPEN:
            name = a[:a.index(b'\0')].decode().rsplit('/', 1)[1]
            if name not in self.files:
                ok = 0
            else:
                j = self.nj = self.nj + 1
                body = self.files[name]
                self.jobs[j] = container.wrap(body, 2, 5, 0x30313736)
                extra = struct.pack('>IIB', j, datarpc.CHUNK, 0)
        elif cmd == datarpc.READ:
            j, s = struct.unpack_from('>II', a)
            d = self.jobs[j][s * datarpc.CHUNK:(s + 1) * datarpc.CHUNK]
            last = (s + 1) * datarpc.CHUNK >= len(self.jobs[j])
            extra = struct.pack('>IIIBII', j, s, 0, last, 0, len(d)) + d
        elif cmd in (datarpc.RCLOSE,):
            extra = a[:4] + bytes(4)
        elif cmd == datarpc.WOPEN:
            size = struct.unpack_from('>I', a)[0]
            j = self.nj = self.nj + 1
            self.jobs[j] = [a[4:a.index(b'\0', 4)].decode().rsplit('/', 1)[1], bytearray(), size]
            extra = struct.pack('>I', j)
        elif cmd == datarpc.WRITE:
            j, s, crc, n = struct.unpack_from('>IIII', a)
            d = a[16:16 + n]
            if container.crc(d) != crc:
                ok = 0
            else:
                self.jobs[j][1] += d
                extra = struct.pack('>III', j, s, len(self.jobs[j][1]))
        elif cmd == datarpc.WCLOSE:
            j, size = struct.unpack_from('>II', a)
            name, data, _want = self.jobs.pop(j)
            try:
                head, body = container.parse(bytes(data))
                ok = int(head['family'] == 2 and head['type'] == container.EMOD and not head['compressed'])
            except ModError:
                ok = 0
            if ok:
                self.files[name] = body
            extra = struct.pack('>II', j, len(data))
        elif cmd == datarpc.CLEAR:
            name = a[:a.index(b'\0')].decode().rsplit('/', 1)[1]
            ok = int(self.files.pop(name, None) is not None)
        self.out.append(datarpc.frame(struct.pack('>HH', 0, seq) + bytes((cmd | 0x80, ok)) + extra))

    def receive(self, timeout):
        return self.out.pop(0) if self.out else None


def test_mods_put_ls_get_active_and_rm_over_the_data_router():
    import argparse
    with Bench('analog-rytm-mk2') as B:
        out, b = built(B)
        blk = out.image[desc.find(out.image)[0]:]
        unit = FakeUnit(blk[:struct.unpack_from('>I', blk, 8)[0]])
        mods = datarpc.Mods(datarpc.Client(unit, timeout=0.5))
        log = []
        with tempfile.TemporaryDirectory() as tmp:
            paths = []
            for name, raw in files(hello(B), user(B)) + [('big.elemod', b'x' * (STAGING + 1))]:
                p = os.path.join(tmp, name)
                open(p, 'wb').write(raw)
                paths.append(p)
            a = argparse.Namespace(op='put', files=paths, port=None, folder='/mods')
            assert datarpc.run(a, mods, log.append) == 1 and 'big.elemod' not in unit.files
            assert datarpc.run(argparse.Namespace(op='put', files=paths[:2]), mods, log.append) == 0
            assert unit.files['drive-hello.elemod'] == open(paths[0], 'rb').read()
            names = [n for n, _k, _s in mods.ls()]
            assert {'drive-hello.elemod', 'drive-hello-user.elemod', '.build'} <= set(names)
            assert datarpc.run(argparse.Namespace(op='active', names=['drive-hello-user.elemod', paths[0]]),
                               mods, log.append) == 0
            act = drive.parse_active(unit.files['active'].decode())
            assert act == [('drive-hello-user.elemod', drive.file_crc(open(paths[1], 'rb').read())),
                           ('drive-hello.elemod', drive.file_crc(open(paths[0], 'rb').read()))]
            g = os.path.join(tmp, 'got')
            assert datarpc.run(argparse.Namespace(op='get', name='drive-hello.elemod', out=g), mods, log.append) == 0
            assert open(g, 'rb').read() == open(paths[0], 'rb').read()
            assert datarpc.run(argparse.Namespace(op='rm', name='drive-hello.elemod'), mods, log.append) == 0
            assert 'drive-hello.elemod' not in unit.files


def test_the_7_bit_packing_round_trips():
    for n in (0, 1, 6, 7, 8, 13, 14, 15, 300):
        data = bytes((i * 37 + 0x81) & 0xFF for i in range(n))
        packed = datarpc.pack7(data)
        assert all(b < 0x80 for b in packed) and datarpc.unpack7(packed) == data
    assert datarpc.pack7(b'\x80\x01') == b'\x40\x00\x01'


# ---- a real build (the owner's files) --------------------------------------------------------------

def test_a_real_core_ar2_build_with_a_loader_stub_and_the_example_mods():
    stock, core = os.environ.get('ELEKLOADER_AR2_SYX'), os.environ.get('ELEKLOADER_CORE_AR2')
    if not stock or not core:
        print('SKIP: ELEKLOADER_AR2_SYX, ELEKLOADER_CORE_AR2')
        return
    from elekloader import formats
    raw, dev, rel = formats.load(stock)
    image = formats.main_image(raw, dev)
    tgt = devices.target_of(dev, rel)
    stub = {'elemod': 2, 'id': 'drive-loader', 'version': '0.0', 'target': tgt, 'requires': ['core'],
            'sections': {'.run': {'align': 4, 'len': 8, 'parts': [['hex', '0000000000000000']]},
                         '.bss': {'size': 0x80000}},
            'symbols': {'drive_staging': ['.bss', 0], 'drive_staging_size': ['abs', 0x80000]},
            'exports': ['drive_staging', 'drive_staging_size'], 'imports': ['core_desc'],
            'relocs': [['.run', 0, 'abs32', 'sym:core_desc', 0]]}
    with open(core, 'rb') as fh:
        cdoc = json.load(fh)
    out = link.link([link.Mod2(cdoc, 'core'), link.Mod2(stub, 'stub')], image)
    b = drive.load_build(out.image)
    assert (b.lo, b.hi, b.staging_size) == (0x47D00000, 0x47FFF000, 0x80000)
    assert (b.d['family'], b.d['device'], b.d['release']) == (2, 5, 0x30313736)
    assert b.desc_at == out.map['core_desc']
    folder = os.environ.get('ELEKLOADER_DRIVE_MODS')
    if not folder:
        print('SKIP: ELEKLOADER_DRIVE_MODS (the example mods)')
        return
    mods = sorted(os.path.join(folder, f) for f in os.listdir(folder) if f.startswith('drive-hello') and f.endswith('.elemod'))
    r = drive.link(b, drive._files(mods))
    assert all(c.state == drive.LOADED for c in r.choices), [c.why() for c in r.choices]
    draw = r.tables['ev_draw']['entries']
    assert draw[0] == ('drive-hello-user', 5), draw
    assert ('drive-hello', 50) in r.tables['ev_tick']['entries']
    first = drive.write_layout(r, tempfile.mkdtemp())
    again = drive.write_layout(drive.link(drive.load_build(out.image), drive._files(mods)), tempfile.mkdtemp())
    assert [open(p, 'rb').read() for p in first] == [open(p, 'rb').read() for p in again]


if __name__ == '__main__':
    import traceback
    ok = failed = 0
    for n in sorted(k for k in globals() if k.startswith('test_')):
        try:
            globals()[n]()
            print('ok     ', n)
            ok += 1
        except Exception:
            print('FAIL   ', n)
            traceback.print_exc()
            failed += 1
    print('%d passed, %d failed' % (ok, failed))
    sys.exit(1 if failed else 0)
