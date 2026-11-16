# SPDX-License-Identifier: GPL-2.0-or-later
"""Writes js/test/drive-vectors.json: what elekloader's build-checker (elekloader/drive.py) says about
tests/test_drive.py's fixtures, on its made-up Analog Rytm MKII build (a release of nops, as
tests/test_link_refusals.py's Bench makes it), so test/drive.test.ts can check drive.ts refuses the
same files with the same codes, details and notes. Also the data container, /mods/active and the
7-bit packing. Run from the repository root:

    python js/tools/drive_vectors.py
"""
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, ROOT)
sys.path.insert(0, os.path.join(ROOT, 'tests'))

from elekloader import container, datarpc, desc, drive    # noqa: E402
from elekloader.elemod import ModError                     # noqa: E402
import test_drive as T                                     # noqa: E402
from test_link_refusals import Bench                       # noqa: E402


def sets(B):
    """(name, files, area) for every row's refused and loading set, and the tests' other sets; area:
    the +Drive area the row's build has (T.built), None for the usual one."""
    out = []
    for code, (f, _line, _detail, _arg, deep, area) in sorted(T.ROWS.items()):
        if deep:                            # pass 2: the host twin's, not the checker's
            continue
        bad, good = f(B)
        out += [(code + ': refused', bad, area), (code + ': loads', good, area)]
    h, u = T.hello(B), T.user(B)
    out = out + [(n, f, None) for n, f in _more(B, h, u)]
    return out + _top(B) + _skip(B) + _abs16(B) + _std(B) + _collections(B) + _claims(B)


def _collections(B):
    """Sets on a build whose boot linker links +Drive mods' own tables (F_COLLECTIONS, the A4's), and the same
    files on one that doesn't (the Rytm's): the tables laid out after every .bss there, a feature or table
    refusal here."""
    from test_drive_collections import adder, decl
    two = decl(B, 'sc-b')
    out = []
    for area in ('a4', 'skip'):
        out += [('%s: a table and two adders' % area, _files(adder(B, 'sc-zz', 40), adder(B, 'sc-aa', 60), decl(B)), area),
                ('%s: an adder without its table' % area, _files(adder(B, 'sc-a')), area)]
    out += [('a4: an entry of another size', _files(decl(B), adder(B, 'sc-a', data='00000000')), 'a4'),
            ('a4: the declarer goes, then its adder',
             _files(adder(B, 'sc-a'), decl(B, imports=['core_fn', 'nothing_has_this', 'sc_cmds', 'sc_cmds_n'])), 'a4'),
            ('a4: two declare one name', _files(two, decl(B, 'sc-a'), adder(B, 'sc-x')), 'a4'),
            ('a4: a table named as a flashed export', _files(decl(B, tables={'core_fn': {'entry': 4}})), 'a4'),
            ('a4: a DSP table', _files(decl(B, tables={'dsp_t': {'space': 'dsp', 'entry': 3}})), 'a4'),
            ('a4: an entry of 6', _files(decl(B, tables={'t6': {'entry': 6}})), 'a4'),
            ('a4: a name of 60 bytes', _files(decl(B, tables={'x' * 60: {'entry': 4}})), 'a4')]
    b = T.built(B, 'a4')[1]
    got = [[(c.code, c.detail) for c in drive.check(b, f).choices] for _n, f, a in out if a == 'a4']
    assert got[0] == [(0, '')] * 3 and got[1] == [(15, 'sc_cmds')] and got[2] == [(0, ''), (15, 'sc_cmds')] \
        and got[3] == [(15, 'sc_cmds'), (14, 'nothing_has_this')] and got[4][0] == (12, 'sc_cmds') \
        and got[6] == [(7, 'collections')] and got[7][0][0] == got[8][0][0] == 5, got
    return out


def _claims(B):
    """Sets on a build whose boot linker takes contributions' claims (F_CLAIMS, the A4's; a flashed claim in its
    site map), test_drive_claims's, and a claim on one that doesn't."""
    import test_drive_claims as TC
    out = [('claims: ' + n, _files(*mods), 'claims') for n, mods, _want in TC.sets(B)]
    return out + [('claims: no F_CLAIMS on this build', _files(TC.cm(B, 'a-mod', [[B.at(0x1000), B.at(0x1010)]])), None)]


def _std(B):
    """Sets on a build whose loader maps std1 subscriptions through the core's std map (F_STD_MAP,
    the A4's), and the same files on one that doesn't."""
    s = T.std_mod
    return [('std: two events, each to its table', _files(s(B, 'ev_boot'), s(B, 'ev_tick', 'drive-std2')), 'std'),
            ('std: to a closed table', _files(s(B, 'ev_closed')), 'std'),
            ('std: a table\'s name is no event', _files(s(B, 'ev_x')), 'std'),
            ('std: without "events"', _files(s(B, 'ev_boot', events=None)), 'std'),
            ('std: "events" of another set', _files(s(B, 'ev_boot', events='std2')), 'std'),
            ('std: "std": false', _files(s(B, 'ev_boot', contribute=[{'to': 'ev_boot', 'std': False,
                                                                      'data': '00000000'}])), 'std'),
            ('std: "std": null is no subscription', _files(s(B, 'ev_x', contribute=[
                {'to': 'ev_x', 'std': None, 'order': 50, 'data': '00000000',
                 'relocs': [[0, 'abs32', 'sym:fn_drive-std', 0]]}])), 'std'),
            ('std: no std map on this build', _files(s(B, 'ev_boot')), None)]


def _files(*docs):
    """T.files without sorting the keys: a skippable .payload has "len" and "sha256" before "parts"."""
    return [d if isinstance(d, tuple) else (d['id'] + '.elemod', json.dumps(d).encode('utf-8')) for d in docs]


def _payload(m, data, **over):
    import hashlib
    m = json.loads(json.dumps(m))
    m['sections']['.payload'] = dict({'align': 4, 'len': len(data), 'sha256': hashlib.sha256(data).hexdigest(),
                                      'parts': [['hex', data.hex()]]}, **over)
    return m


def _skip(B):
    """Sets on a build whose loader skips payloads (F_PAYLOAD_SKIP, the A4's): the file's .payload
    staged without its hex (drive.stage), and what is refused."""
    data = bytes(range(256)) * 3
    h = T.hello(B)
    no_sha = _payload(h, data)
    del no_sha['sections']['.payload']['sha256']
    reloc = _payload(h, data)
    reloc['relocs'] = reloc['relocs'] + [['.run', 0, 'abs32', 'sec:.payload', 0]]
    contrib = _payload(T.hello(B, contribute=[{'to': 'ev_tick', 'order': 50, 'data': '00000000',
                                               'relocs': [[0, 'abs32', 'sec:.payload', 0]]}]), data)
    two = _payload(h, data, parts=[['hex', data[:4].hex()], ['hex', data[4:].hex()]])
    out = [('skip: a payload loads', _files(_payload(h, data), T.user(B)), 'skip'),
            ('skip: a payload without sha256', _files(no_sha), 'skip'),
            ('skip: a relocation into .payload', _files(reloc), 'skip'),
            ('skip: a contribution relocation into .payload', _files(contrib), 'skip'),
            ('skip: two parts', _files(two), 'skip'),
            ('skip: no payload skip on this build', _files(_payload(h, data)), 'top')]
    b = T.built(B, 'skip')[1]
    got = [[(c.code, c.detail) for c in drive.check(b, f).choices] for _n, f, a in out if a == 'skip']
    assert got == [[(0, ''), (0, '')], [(7, '.payload')], [(7, 'relocs into .payload')],
                   [(7, 'relocs into .payload')], [(7, '.payload')]], got
    return out


def _abs16(B):
    """Sets on a build whose boot linker takes abs16 relocations (F_ABS16, the A4's), and the same files on one
    that doesn't (the Rytm's): taken there, a format refusal here."""
    run = T.hello(B, relocs=T.hello(B)['relocs'] + [['.run', 2, 'abs16', 'abs', 0x1234]])
    end = T.hello(B, relocs=[['.run', 11, 'abs16', 'abs', 0]])
    jmp = B.jmp_site(B.at(0x1000), 'fn_a-mod')
    site = T.dmod(B, 'a-mod', sites=[dict(jmp, relocs=[[2, 'abs16', 'abs', 0]])])
    out = []
    for area in ('a4', 'skip'):
        out += [('%s: abs16 in .run' % area, _files(run), area),
                ('%s: abs16 past .run' % area, _files(end), area),
                ('%s: abs16 in a site' % area, _files(site), area)]
    b, s = T.built(B, 'a4')[1], T.built(B, 'skip')[1]
    got = [[(c.code, c.detail) for c in drive.check(bb, f).choices] for bb in (b, s) for _n, f, _a in out[:3]]
    assert got[0] == [(0, '')] and got[1][0][0] == got[3][0][0] == got[4][0][0] == drive.CODE['format'], got
    return out


def _stage_files(B):
    """Files for drive.stage's vectors: skippable, not, and mutated ones."""
    import random
    data = bytes(range(200))
    h = T.hello(B)
    files = [_files(h)[0][1], _files(_payload(h, data))[0][1],
             json.dumps(_payload(h, data), separators=(',', ':')).encode()]
    for over in ({'sha256': 'ab' * 31}, {'parts': [['hex', data[:4].hex()], ['hex', data[4:].hex()]]}):
        files.append(_files(_payload(h, data, **over))[0][1])
    rnd = random.Random(1115)
    small = _files(_payload(h, data[:24]))[0][1]
    for _ in range(40):
        b = bytearray(small)
        for _k in range(rnd.randint(1, 3)):
            at = rnd.randrange(len(b))
            ch = rnd.choice(b'{}[]",:\\ 0123456789abx.')
            [b.__setitem__, b.insert, lambda a, _c: b.__delitem__(a)][rnd.randrange(3)](at, ch)
        files.append(bytes(b))
    return files


def _top(B):
    """Sets on a build whose loader stages files at the area's top (ABI 1.3, F_STAGE_TOP): the
    placement by file length, the room each file needs above the code before it, and too-big at the
    area's size (drive._layout, placement)."""
    from test_drive_top_stage import code_mod
    area = ('top', 0x8000)
    n = T.built(B, area)[1]
    room = n.hi - n.lo
    # z-big: the longest file (about 22 KB) with 4 KB of code; a-small: 8 KB of code in a 17 KB file.
    # In id order a-small's code would sit under z-big's file; longest first, both fit.
    big, small = code_mod(B, 'z-big', 4 * 1024, pad=13000), code_mod(B, 'a-small', 8 * 1024)
    bare = len(_files(T.hello(B, description=''))[0][1])
    out = [('top: a file over the staging buffer loads', _files(T.hello(B, description='x' * (T.STAGING + 1))), 'top'),
           ('top: the longest file first', _files(small, big), area),
           ('top: the longest file first, the other order', _files(big, small), area),
           ('top: one more mod is out of room', _files(small, big, code_mod(B, 'm-more', 8 * 1024)), area),
           ('top: a file that leaves no room for its own code',
            _files(T.hello(B, description='x' * (room - 8 - bare))), area),
           ('top: a file over the area', _files(T.hello(B, description='x' * (room + 1 - bare))), area)]
    b = T.built(B, area)[1]
    got = [[c.code for c in drive.check(b, f).choices] for _n, f, _a in out[1:]]
    room_, big_ = drive.CODE['room'], drive.CODE['too-big']
    assert got[:2] == [[0, 0], [0, 0]] and room_ in got[2] and got[3:] == [[room_], [big_]], got
    return out


def _more(B, h, u):
    out = []
    out.append(('the two examples', T.files(u, h)))
    out.append(('a missing file', [('gone.elemod', None)] + T.files(h)))
    out.append(('bad UTF-8', [('bad.elemod', b'{"id": "\xff"}')] + T.files(h)))
    out.append(('a list, not an object', [('list.elemod', b'[1]')]))
    out.append(('format 1', T.files(dict(h, elemod=1))))
    out.append(('a float for 2', T.files(dict(h, elemod=2.0))))
    out.append(('an order of 2.0', T.files(T.hello(B, contribute=[{'to': 'ev_tick', 'order': 2.0, 'data': '00000000'}]))))
    out.append(('data with spaces', T.files(T.hello(B, contribute=[{'to': 'ev_tick', 'data': '00 00\n0000'}]))))
    out.append(('data not hex', T.files(T.hello(B, contribute=[{'to': 'ev_tick', 'data': '0g000000'}]))))
    out.append(('a table it may not add to', T.files(T.hello(B, contribute=[{'to': 'nope', 'data': '00000000'}]))))
    out.append(('a relocation past .run', T.files(T.hello(B, relocs=[['.run', 10, 'abs32', 'sym:core_fn', 0]]))))
    out.append(('a long id', T.files(T.hello(B, id='x' * 28))))
    out.append(('no target', T.files(T.hello(B, target=None))))
    out.append(('Mod2 refuses', T.files(T.hello(B, symbols={'fn_drive-hello': ['.nope', 0], 'drive-hello_count': ['.bss', 0]}))))
    for field, value in (('collections', {'ev_mine': {'entry': 4}}), ('storage', [{'sym': 'st', 'scope': 'kit', 'bits': 1}])):
        out.append(('feature ' + field, T.files(T.hello(B, **{field: value}))))
    for sec in ('.boot', '.fast', '.payload'):
        d = T.hello(B)
        d['sections'][sec] = {'align': 4, 'len': 2, 'parts': [['hex', '4e75']]}
        out.append(('section ' + sec, T.files(d)))
    out.append(('regions', T.files(T.hello(B, resources={'regions': [{'name': 'r', 'lo': 1, 'hi': 2}]}))))
    out.append(('entry claims', T.files(T.hello(B, contribute=[{'to': 'ev_tick', 'order': 50, 'data': '00000000',
                                                                  'relocs': [], 'claims': [[1, 2]]}]))))
    a, z = T.hello(B, id='a-mod', conflicts=['b-mod']), T.hello(B, id='b-mod')
    out += [('a conflicting pair', T.files(a, z)), ('a conflicting pair, the other way', T.files(z, a))]
    big = ['n%05d' % i for i in range(T.built(B)[1].pools['arena'] // 7)]
    out.append(('the arena', T.files(T.hello(B, resources={'names': big[:1000]}, exports=['not_a_symbol']),
                                     T.hello(B, resources={'names': big[1000:]}))))
    # +Drive sites (the descriptor alone: no stock bytes to check)
    jmp, wide = B.jmp_site(B.at(0x1000), 'fn_a-mod'), B.site(B.at(0x1004), 4)
    block = {'lo': B.at(0x1000), 'hi': B.at(0x1100), 'to': 0}
    out.append(('sites: two mods on one byte', T.files(T.dmod(B, 'z-mod', sites=[wide]), T.dmod(B, 'a-mod', sites=[jmp]))))
    out.append(('sites: a site in a copied block', T.files(T.dmod(B, 'a-mod', copied=[block]), T.dmod(B, 'z-mod', sites=[wide]))))
    out.append(('sites: two copied blocks', T.files(T.dmod(B, 'a-mod', copied=[block]), T.dmod(B, 'z-mod', copied=[block]))))
    out.append(('sites: a copied block over a flashed site',
                T.files(T.dmod(B, 'a-mod', copied=[dict(block, lo=B.at(0x1f00), hi=B.at(0x2001))]))))
    for name, bad in (('outside the main OS', dict(jmp, addr=B.at(len(B.img) - 2))), ('a short new', dict(jmp, len=8)),
                      ('kind both', dict(jmp, kind='both')), ('an abs16', dict(jmp, relocs=[[2, 'abs16', 'abs', 0]])),
                      ('a sha not hex', dict(jmp, stock_sha256='x' * 64)), ('addr as text', dict(jmp, addr='0x40001400')),
                      ('addr not a number', dict(jmp, addr='here'))):
        out.append(('sites: ' + name, T.files(T.dmod(B, 'a-mod', sites=[bad]))))
    out.append(('sites: an empty copied block', T.files(T.dmod(B, 'a-mod', copied=[dict(block, hi=block['lo'])]))))
    d = T.hello(B)
    d['symbols']['drive_init'] = ['.run', 2]
    d['exports'] = d['exports'] + ['drive_init']
    out.append(('drive_init', T.files(d, T.user(B))))
    return out


def main():
    rows = []
    with Bench('analog-rytm-mk2') as B:
        blocks = {}

        def block(area):
            if area not in blocks:
                out = T.built(B, area)[0]
                at = desc.find(out.image)[0]
                blocks[area] = out.image[at:at + desc.decode(out.image, at)['size']]
            return blocks[area]
        blk = block(None)
        for name, files, area in sets(B):
            r = drive.check(drive.load_build(block(area)), files)
            row = {'name': name, 'files': [[n, _enc(raw)] for n, raw in files],
                   'summary': drive.summary(r), 'status': drive.status_text(r), 'lines': _lines(r)}
            if area is not None:            # its own build: a smaller area, smaller pools, or the stage at the top
                row['build'] = block(area).hex()
            rows.append(row)
        release = {'version': B.rel.version, 'syxSha256': B.rel.syx_sha256, 'mainSha256': B.rel.main_sha256,
                   'mainLen': B.rel.main_len}
        body = b'{"elemod": 2}'
        wrapped = container.wrap(body, 2, 5, 0x30313736)
        texts = ['# x\n\n  a.elemod   0x1F \r\nnocrc\nb.elemod zz\n', 'a.elemod -1\nb.elemod 1_f\nc.elemod 0x0x2\n',
                 ''.join('m%d.elemod 1\n' % i for i in range(257)), 'x 123456789ab\ny 0xffffffff\n',
                 # paths below /mods, to 127 bytes (decided 2026-11-08), and one byte past
                 'fx/a b/c.elemod 3\n%s 1\n%sx 2\n' % ('d/' + 'k' * 118 + '.elemod', 'd/' + 'k' * 118 + '.elemod')]
        extra = {
            'build': blk.hex(), 'label': drive.load_build(blk).label(),
            'container': {'body': body.hex(), 'wrapped': wrapped.hex(),
                          'build': container.wrap(blk, 2, 5, 0x30313736).hex()},
            'stage': [[f.hex(), drive.stage(f)[0].hex(), drive.stage(f)[1]] for f in _stage_files(B)],
            'active': {'text': drive.active_text([('a.elemod', b'x'), ('b c.elemod', b'')]),
                       'skip': {'build': block('skip').hex(),
                                'files': [[n, raw.hex()] for n, raw in _files(_payload(T.hello(B), bytes(64)))],
                                'text': drive.active_text(_files(_payload(T.hello(B), bytes(64))),
                                                          drive.load_build(block('skip')))},
                       'parsed': [[t, drive.parse_active(t)] for t in texts]},
            'pack7': [[bytes((i * 37 + 0x81) & 0xFF for i in range(n)).hex(),
                       datarpc.pack7(bytes((i * 37 + 0x81) & 0xFF for i in range(n))).hex()]
                      for n in (0, 1, 6, 7, 8, 13, 14, 15, 300)],
        }
        extra['exchanges'] = _exchanges(B, blk)
    extra['telos'] = _telos()
    path = os.path.join(ROOT, 'js', 'test', 'drive-vectors.json')
    with open(path, 'w') as fh:
        json.dump({'release': release, **extra, 'cases': rows}, fh, indent=None, separators=(',', ':'))
        fh.write('\n')
    print('wrote %s: %d cases' % (path, len(rows)))


def _telos():
    """TELOS's sites and regions (test_drive.test_telos_sites_and_regions) on a made-up Digitone mk1
    build whose descriptor carries the stock shadow, as the checker sees it: the descriptor alone."""
    from elekloader import link
    with Bench('digitone-mk1') as B:
        core = B.core(version='5.0', collections={'ev_tick': {'entry': 4}, 'ev_x': {'entry': 4}})
        loader = B.other('telos', imports=['core_desc'], relocs=[['.run', 4, 'abs32', 'sym:core_desc', 0]])
        fa = B.mod('flash-a', sites=[B.jmp_site(B.at(0x1000), 'fn_flash-a')],
                   resources={'regions': [{'name': 'flash-a buf', 'lo': '0x45000000', 'hi': '0x45100000'}]})
        out = link.link([link.Mod2(d, d['id']) for d in (core, loader, fa)], B.img)
        at = desc.find(out.image)[0]
        blk = out.image[at:at + desc.decode(out.image, at)['size']]
        site = lambda off: B.jmp_site(B.at(off), 'fn_drive-hello')
        reg = lambda lo, hi: {'regions': [{'name': 'r', 'lo': hex(lo), 'hi': hex(hi)}]}
        later = T.dmod(B, 'later', sites=[dict(site(0x2002), relocs=[[2, 'abs32', 'sym:fn_later', 0]])])
        sets = [
            ('a site', T.files(T.hello(B, sites=[site(0x2000)]))),
            ("a site over the flashed build's", T.files(T.hello(B, sites=[site(0x1000)]))),
            ('a site whose stock the unit checks', T.files(T.hello(B, sites=[dict(site(0x2000), stock_sha256='00' * 32)]))),
            ('a site with a short hash', T.files(T.hello(B, sites=[dict(site(0x2000), stock_sha256='00')]))),
            ('two sites that overlap', T.files(T.hello(B, sites=[site(0x2000)]), later)),
            ('a region', T.files(T.hello(B, resources=reg(0x45100000, 0x45200000)))),
            ("a region over a flashed mod's", T.files(T.hello(B, resources=reg(0x450f0000, 0x45110000)))),
            ('two regions that overlap', T.files(T.hello(B, resources=reg(0x45100000, 0x45200000)),
                                                 T.dmod(B, 'later', resources=reg(0x451f0000, 0x45300000)))),
            ('a region below the bulk area', T.files(T.hello(B, resources=reg(0x43f00000, 0x44100000)))),
            ('a region across two areas', T.files(T.hello(B, resources=reg(0x47b00000, 0x47bf0000)))),
            ('a region upside down', T.files(T.hello(B, resources=reg(0x45200000, 0x45100000)))),
            ('a region without hi', T.files(T.hello(B, resources={'regions': [{'name': 'r', 'lo': '0x45100000'}]}))),
        ]
        rows = []
        for name, files in sets:
            r = drive.check(drive.load_build(blk), files)
            rows.append({'name': name, 'files': [[n, _enc(raw)] for n, raw in files],
                         'summary': drive.summary(r), 'status': drive.status_text(r), 'lines': _lines(r)})
        d = desc.decode(blk)
        return {'release': {'version': B.rel.version, 'syxSha256': B.rel.syx_sha256, 'mainSha256': B.rel.main_sha256,
                            'mainLen': B.rel.main_len},
                'build': blk.hex(), 'abi': list(d['abi']), 'claims': [list(c) for c in d['claims']],
                'shadow': [[a, x.hex()] for a, x in d['shadow']], 'cases': rows}


class Recorder:
    """A port that records every message sent and every reply, around test_drive's FakeUnit."""

    def __init__(self, unit):
        self.unit, self.log = unit, []

    def send(self, raw):
        self.log.append(['send', bytes(raw).hex()])
        self.unit.send(raw)

    def receive(self, timeout):
        raw = self.unit.receive(timeout)
        if raw is not None:
            self.log.append(['reply', raw.hex()])
        return raw


def _exchanges(B, blk):
    """`elekloader mods`' requests and the fake unit's replies, step by step: the build, a list, two uploads (one
    over two chunks), /mods/active, a download, a delete and a refused download."""
    unit = T.FakeUnit(blk)
    port = Recorder(unit)
    mods = datarpc.Mods(datarpc.Client(port, timeout=0.5))
    h, u = T.files(T.hello(B, description='x' * 9000)), T.files(T.user(B))
    steps = []

    def step(op, args, f):
        port.log = []
        try:
            out = f()
        except ModError as e:                   # RpcError among them
            out = {'error': str(e)}
        steps.append({'op': op, 'args': args, 'out': out, 'log': port.log})
    got = {}

    def build():
        b, got['head'] = mods.build()
        return {'crc': b.crc, 'head': got['head']}
    step('build', [], build)
    head = got['head']
    for name, raw in h + u:                 # `elekloader mods put`: each upload, then the unit's note or listing
        step('upload', [name, raw.hex()], lambda: mods.upload(name, raw, head))
    step('ls', [], lambda: [list(x) for x in mods.ls()])
    text = drive.active_text(u + h)
    step('upload', ['active', text.encode().hex()], lambda: mods.upload('active', text.encode(), head))   # mods active
    step('get', [h[0][0]], lambda: mods.get(h[0][0])[1].hex())
    step('rm', [h[0][0]], lambda: mods.rm(h[0][0]))
    step('get', [h[0][0]], lambda: mods.get(h[0][0])[1].hex())
    # uploads checked against the route's `last:` note: one saved, one into a folder the unit didn't keep
    port.unit = T.NotingUnit(blk, folders={'fx'})
    for into in ('fx', 'gone'):
        n = '%s/%s' % (into, u[0][0])
        step('upload', [n, u[0][1].hex()], lambda: mods.upload(n, u[0][1], head))
    return steps


def _enc(raw):
    """A file as text when it is UTF-8 (most are JSON), else {"hex": ...}; a missing file is None."""
    if raw is None:
        return None
    try:
        return raw.decode('utf-8')
    except UnicodeDecodeError:
        return {'hex': raw.hex()}


def _lines(r):
    got = []
    drive.print_result(r, got.append)
    return got


if __name__ == '__main__':

    main()
