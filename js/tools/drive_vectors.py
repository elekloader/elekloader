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
import test_drive as T                                     # noqa: E402
from test_link_refusals import Bench                       # noqa: E402


def sets(B):
    """(name, files) for every row's refused and loading set, and the tests' other sets."""
    out = []
    for code, (f, _line, _detail, _arg, deep) in sorted(T.ROWS.items()):
        if deep:                            # pass 2: the host twin's, not the checker's
            continue
        bad, good = f(B)
        out += [(code + ': refused', bad), (code + ': loads', good)]
    h, u = T.hello(B), T.user(B)
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
    for field, value in (('collections', {'ev_mine': {'entry': 4}}), ('copied', [{'lo': 1, 'hi': 2, 'to': 3}]),
                         ('storage', [{'sym': 'st', 'scope': 'kit', 'bits': 1}])):
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
    big = ['n%05d' % i for i in range(drive.ARENA // 7)]
    out.append(('the arena', T.files(T.hello(B, resources={'names': big[:1000]}, exports=['not_a_symbol']),
                                     T.hello(B, resources={'names': big[1000:]}))))
    d = T.hello(B)
    d['symbols']['drive_init'] = ['.run', 2]
    d['exports'] = d['exports'] + ['drive_init']
    out.append(('drive_init', T.files(d, T.user(B))))
    return out


def main():
    rows = []
    with Bench('analog-rytm-mk2') as B:
        out, b = T.built(B)
        at = desc.find(out.image)[0]
        blk = out.image[at:at + desc.decode(out.image, at)['size']]
        for name, files in sets(B):
            r = drive.check(drive.load_build(blk), files)
            rows.append({'name': name, 'files': [[n, _enc(raw)] for n, raw in files],
                         'summary': drive.summary(r), 'status': drive.status_text(r),
                         'lines': _lines(r)})
        release = {'version': B.rel.version, 'syxSha256': B.rel.syx_sha256, 'mainSha256': B.rel.main_sha256,
                   'mainLen': B.rel.main_len}
        body = b'{"elemod": 2}'
        wrapped = container.wrap(body, 2, 5, 0x30313736)
        texts = ['# x\n\n  a.elemod   0x1F \r\nnocrc\nb.elemod zz\n', 'a.elemod -1\nb.elemod 1_f\nc.elemod 0x0x2\n',
                 ''.join('m%d.elemod 1\n' % i for i in range(33)), 'x 123456789ab\ny 0xffffffff\n']
        extra = {
            'build': blk.hex(), 'label': drive.load_build(blk).label(),
            'container': {'body': body.hex(), 'wrapped': wrapped.hex(),
                          'build': container.wrap(blk, 2, 5, 0x30313736).hex()},
            'active': {'text': drive.active_text([('a.elemod', b'x'), ('b c.elemod', b'')]),
                       'parsed': [[t, drive.parse_active(t)] for t in texts]},
            'pack7': [[bytes((i * 37 + 0x81) & 0xFF for i in range(n)).hex(),
                       datarpc.pack7(bytes((i * 37 + 0x81) & 0xFF for i in range(n))).hex()]
                      for n in (0, 1, 6, 7, 8, 13, 14, 15, 300)],
        }
        extra['exchanges'] = _exchanges(B, blk)
    path = os.path.join(ROOT, 'js', 'test', 'drive-vectors.json')
    with open(path, 'w') as fh:
        json.dump({'release': release, **extra, 'cases': rows}, fh, indent=None, separators=(',', ':'))
        fh.write('\n')
    print('wrote %s: %d cases' % (path, len(rows)))


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
        except datarpc.RpcError as e:
            out = {'error': str(e)}
        steps.append({'op': op, 'args': args, 'out': out, 'log': port.log})
    got = {}

    def build():
        b, got['head'] = mods.build()
        return {'crc': b.crc, 'head': got['head']}
    step('build', [], build)
    head = got['head']
    for name, raw in h + u:
        step('put', [name, raw.hex()], lambda: mods.put(name, raw, head))
    step('ls', [], lambda: [list(x) for x in mods.ls()])
    text = drive.active_text(u + h)
    step('put', ['active', text.encode().hex()], lambda: mods.put('active', text.encode(), head))
    step('get', [h[0][0]], lambda: mods.get(h[0][0])[1].hex())
    step('rm', [h[0][0]], lambda: mods.rm(h[0][0]))
    step('get', [h[0][0]], lambda: mods.get(h[0][0])[1].hex())
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
