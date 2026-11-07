# SPDX-License-Identifier: GPL-2.0-or-later
"""The Drive Loader's boot linker (mods/drive-loader-ar2/dlink.c), built for the computer and run
on a made-up Analog Rytm MKII build with a build descriptor: what it links, where, the tables it
rewrites, and each refusal.

    python tests/test_drive_link.py

Needs a C compiler (cc). The byte-for-byte comparison with `elekloader link --device-layout`
is in the emulator gate; this checks the loader's own rules on known inputs."""
import json
import os
import struct
import subprocess
import sys
import tempfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
sys.path.insert(0, HERE)

from elekloader import desc, link                                # noqa: E402
from test_link_refusals import Bench                             # noqa: E402

SRC = os.path.join(os.path.dirname(HERE), 'mods', 'drive-loader-ar2')
AREA = 0x47D00000


def build_host(work):
    exe = os.path.join(work, 'drive_link_host')
    subprocess.run(['cc', '-O1', '-Wall', '-DDL_HOST', '-o', exe, os.path.join(HERE, 'drive_link_host.c'),
                    os.path.join(SRC, 'dlink.c'), os.path.join(SRC, 'json.c')], check=True)
    return exe


class Flashed:
    """A flashed build: core (tables ev_x, ev_y with one spare, ev_z with none and closed),
    the loader stub (imports core_desc) and a flashed mod `flm` on ev_x."""

    def __init__(self, B):
        self.B = B
        core = B.core(version='0.11',
                      collections={'ev_x': {'entry': 4}, 'ev_y': {'entry': 4, 'spare': 1},
                                   'core_lock_codes': {'entry': 8}},
                      symbols={'core_fn': ['.boot', 0], 'core_zero': ['.boot', 0]},
                      exports=['core_fn', 'core_zero'])
        loader = {'elemod': 2, 'id': 'drive-loader', 'version': '0.1', 'target': B.target,
                  'requires': ['core'],
                  'sections': {'.run': {'align': 4, 'len': 4, 'parts': [['hex', '4e754e75']]}},
                  'symbols': {'dl_fn': ['.run', 0]}, 'exports': ['dl_fn'], 'imports': ['core_desc'],
                  'relocs': [[".run", 0, "abs32", "sym:core_desc", 0]],
                  'resources': {'names': ['settings:MODS']}}
        loader['sections']['.run'] = {'align': 4, 'len': 8, 'parts': [['hex', '0000000000000000']]}
        flm = B.mod('flm', exports=['fn_flm'],
                    contribute=[{'to': 'ev_x', 'order': 50, 'data': '00000000',
                                 'relocs': [[0, 'abs32', 'sym:fn_flm', 0]]},
                                {'to': 'ev_y', 'order': 50, 'data': '00000000',
                                 'relocs': [[0, 'abs32', 'sym:fn_flm', 0]]}])
        self.docs = [core, loader, flm]
        out = link.link([link.Mod2(d, d['id']) for d in self.docs], B.img)
        self.out = out
        dev = B.dev
        ddr_base, run_end = out.layout['ddr']
        run_load = out.layout['run_load']
        o = run_load - dev.main_load
        self.ram_base = ddr_base
        self.ram = bytearray(out.image[o:o + run_end - ddr_base])
        self.desc_at = out.map['core_desc']
        self.d = desc.decode(self.ram, self.desc_at - ddr_base)
        self.tables = {t['name']: t for t in self.d['tables']}

    def table_words(self, ram, name, n=None):
        t = self.tables[name]
        at = t['address'] - self.ram_base
        n = n if n is not None else t['capacity'] + 1
        return list(struct.unpack('>%dI' % n, bytes(ram[at:at + 4 * n])))


def drive_mod(B, ident, **extra):
    d = {'elemod': 2, 'id': ident, 'version': '1', 'target': B.target,
         'sections': {'.run': {'align': 4, 'len': 8, 'parts': [['hex', '4e714e714e714e75']]},
                      '.bss': {'size': 16}},
         'symbols': {'fn_' + ident: ['.run', 0], 'b_' + ident: ['.bss', 4]},
         'exports': ['fn_' + ident], 'imports': [], 'relocs': [],
         'contribute': [{'to': 'ev_x', 'order': 60, 'data': '00000000',
                         'relocs': [[0, 'abs32', 'sym:fn_' + ident, 0]]}]}
    d.update(extra)
    return d


def run(exe, F, mods, work, active=None, raw=None, extra_files=None, check=False):
    """Link `mods` (docs, in this /mods/active order). -> (summary, RAM after).
    With check, run the MODS page's dl_check instead of dl_link."""
    d = tempfile.mkdtemp(dir=work)
    lines = []
    for m in mods:
        name = m['id'] + '.elemod'
        data = raw.get(m['id']) if raw and m['id'] in raw else (json.dumps(m, indent=1) + '\n').encode()
        with open(os.path.join(d, name), 'wb') as fh:
            fh.write(data)
        lines.append('%s %08x' % (name, zlib.crc32(data) & 0xFFFFFFFF))
    for name, data in (extra_files or {}).items():
        with open(os.path.join(d, name), 'wb') as fh:
            fh.write(data)
    with open(os.path.join(d, 'active'), 'w') as fh:
        fh.write(active if active is not None else '\n'.join(lines) + '\n')
    ramf, stockf, outf = (os.path.join(d, x) for x in ('ram.bin', 'stock.bin', 'out.bin'))
    with open(ramf, 'wb') as fh:
        fh.write(F.ram)
    with open(stockf, 'wb') as fh:
        fh.write(F.B.img)
    r = subprocess.run([exe, ramf, '0x%x' % F.ram_base, stockf, '0x%x' % F.B.dev.main_load,
                        '0x%x' % F.desc_at, d, outf], capture_output=True, text=True,
                       env=dict(os.environ, DL_HOST_CHECK='1') if check else None)
    if r.returncode:
        raise AssertionError('host linker failed: %s %s' % (r.stdout, r.stderr))
    with open(outf, 'rb') as fh:
        return json.loads(r.stdout), bytearray(fh.read())


def untouched(F, ram):
    """The host's RAM out is the flashed image followed by zeros."""
    return ram[:len(F.ram)] == F.ram and not any(ram[len(F.ram):])


def area(F, ram, n):
    at = AREA - F.ram_base
    return bytes(ram[at:at + n])


def main():
    work = tempfile.mkdtemp()
    exe = build_host(work)
    fails = []

    def check(name, cond, info=''):
        print('%s %s %s' % ('ok  ' if cond else 'FAIL', name, info if not cond else ''))
        if not cond:
            fails.append(name)

    with Bench('analog-rytm-mk2') as B:
        F = Flashed(B)
        flm_fn = F.out.map['flm:fn_flm']
        x0 = F.table_words(F.ram, 'ev_x')
        check('descriptor: ev_x has 1 entry and 16 spare', F.tables['ev_x']['count'] == 1
              and F.tables['ev_x']['capacity'] == 17, F.tables['ev_x'])

        # nothing chosen
        s, ram = run(exe, F, [], work)
        check('empty active: nothing loaded', s['loaded'] == 0 and not s['status'], s)
        check('empty active: tables as flashed', F.table_words(ram, 'ev_x') == x0)

        # the two test mods
        hello = drive_mod(B, 'drive-hello', contribute=[
            {'to': 'ev_x', 'order': 60, 'data': '00000000', 'relocs': [[0, 'abs32', 'sym:fn_drive-hello', 0]]},
            {'to': 'ev_y', 'order': 10, 'data': '00000000', 'relocs': [[0, 'abs32', 'sym:fn_drive-hello', 0]]}],
            symbols={'fn_drive-hello': ['.run', 0], 'drive_init': ['.run', 4], 'b': ['.bss', 0]})
        user = drive_mod(B, 'drive-hello-user', requires=['drive-hello'], imports=['fn_drive-hello', 'core_fn'],
                         sections={'.run': {'align': 16, 'len': 20, 'parts': [['hex', '00' * 16], ['stock', '0x40000500', 4]]},
                                   '.bss': {'size': 8, 'align': 8}},
                         relocs=[['.run', 0, 'abs32', 'sym:fn_drive-hello', 2],
                                 ['.run', 4, 'pc32', 'sym:fn_drive-hello-user', 0],
                                 ['.run', 8, 'pc16', 'sym:b_drive-hello-user', 0],
                                 ['.run', 10, 'abs32', 'sec:.bss', 4]],
                         contribute=[{'to': 'ev_x', 'order': 10, 'data': '0000000000000000',
                                      'relocs': [[0, 'abs32', 'sym:fn_drive-hello-user', 0],
                                                 [4, 'pc32', 'sym:fn_drive-hello', 0]]}])
        s, ram = run(exe, F, [user, hello], work)
        check('test mods: both load', s['loaded'] == 2 and [x['state'] for x in s['status']] == [1, 1], s)
        # id order: drive-hello first at the area start (.run 8 bytes), then -user aligned to 16
        h_run, u_run = AREA, AREA + 16
        h_bss = AREA + 36                      # after both .run, aligned 4
        u_bss = AREA + 56                      # after hello's 16, aligned 8
        a = area(F, ram, 64)
        check('hello .run at the area start', a[0:8] == bytes.fromhex('4e714e714e714e75'), a[:8].hex())
        w = struct.unpack('>I', a[16:20])[0]
        check('abs32 to an export of another +Drive mod (+2)', w == h_run + 2, hex(w))
        w = struct.unpack('>i', a[20:24])[0]
        check('pc32 to its own symbol', w == u_run - (u_run + 4), w)
        w = struct.unpack('>h', a[24:26])[0]
        check('pc16 to its own .bss symbol', w == (u_bss + 4) - (u_run + 8), w)
        w = struct.unpack('>I', a[26:30])[0]
        check('abs32 to sec:.bss + 4', w == u_bss + 4, hex(w))
        check('stock part copied', a[32:36] == bytes(B.img[0x100:0x104]))
        check('drive_init listed', s['inits'] == [h_run + 4], s['inits'])
        xs = F.table_words(ram, 'ev_x')
        check('ev_x merged by order: user(10, 2 words), flm(50), hello(60), 0',
              xs[:5] == [u_run, h_run - (F.tables['ev_x']['address'] + 4), flm_fn, h_run, 0], [hex(v) for v in xs[:6]])
        check('ev_x spare after the zero entry stays zero', not any(xs[5:]))
        ys = F.table_words(ram, 'ev_y')
        check('ev_y: hello(10) before flm(50)', ys == [h_run, flm_fn, 0], [hex(v) for v in ys])

        # equal order: ties by id string, flashed and +Drive mixed
        tie = drive_mod(B, 'flz', contribute=[{'to': 'ev_x', 'order': 50, 'data': '00000000',
                                               'relocs': [[0, 'abs32', 'sym:fn_flz', 0]]}])
        tia = drive_mod(B, 'fla', contribute=[{'to': 'ev_x', 'order': 50, 'data': '00000000',
                                               'relocs': [[0, 'abs32', 'sym:fn_fla', 0]]}])
        s, ram = run(exe, F, [tie, tia], work)
        xs = F.table_words(ram, 'ev_x', 4)
        check('ties: fla, flm (flashed), flz', xs[:3] == [AREA, flm_fn, AREA + 8], [hex(v) for v in xs])

        # refusals, one at a time, each next to a good mod
        good = drive_mod(B, 'good')

        def refused(name, bad, code, files=None, raw=None, active=None, detail=None, extra=()):
            mods = [good, bad] + list(extra)
            s, _ = run(exe, F, mods, work, raw=raw, active=active)
            st = {x['id']: x for x in s['status']}
            got = st.get(bad['id']) or st.get(bad['id'] + '.elemod', {})
            ok = got.get('state') in (2, 3) and got.get('code') == code and st['good']['state'] == 1
            if detail is not None:
                ok = ok and got.get('detail') == detail
            check('refuses ' + name, ok, s)
            # the MODS page's check: the same verdict without touching RAM, except a pass-2
            # refusal (pc16), which only the link sees
            c, cram = run(exe, F, mods, work, raw=raw, active=active, check=True)
            cst = {x['id']: x for x in c['status']}
            cgot = cst.get(bad['id']) or cst.get(bad['id'] + '.elemod', {})
            if code == 18:
                cok = cgot.get('state') == 1
            else:
                cok = (cgot.get('state') in (2, 3) and cgot.get('code') == code
                       and (detail is None or cgot.get('detail') == detail))
            cok = cok and cst['good']['state'] == 1 and untouched(F, cram) and c['boot_status_n'] == 0
            check('check agrees: ' + name, cok, c)
            return s

        refused('a flashed id', drive_mod(B, 'flm'), 8, detail='flm')
        refused('a missing require', drive_mod(B, 'needy', requires=['nope']), 10, detail='nope')
        refused('a conflict with a flashed mod', drive_mod(B, 'cf', conflicts=['flm']), 11, detail='flm')
        refused('a conflicting pair (the later)', drive_mod(B, 'zz', conflicts=['good']), 11, detail='good')
        refused('an export clash with flashed', drive_mod(B, 'ex', symbols={'fn_ex': ['.run', 0], 'core_fn': ['.run', 4]},
                                                          exports=['fn_ex', 'core_fn']), 12, detail='core_fn')
        refused('a claimed name', drive_mod(B, 'cl', resources={'names': ['settings:MODS']}), 13, detail='settings:MODS')
        refused('an unresolved import', drive_mod(B, 'im', imports=['nothing_here']), 14, detail='nothing_here')
        refused('another device', drive_mod(B, 'tg', target=dict(B.target, device='digitakt-mk1')), 6)
        refused('a site', drive_mod(B, 'st', sites=[B.site(B.at(0x1000))]), 7, detail='sites')
        refused('.fast', drive_mod(B, 'fa', sections={'.run': {'len': 2, 'parts': [['hex', '4e75']]},
                                                      '.fast': {'len': 2, 'parts': [['hex', '4e75']]}}), 7, detail='.fast')
        refused('storage', drive_mod(B, 'sg', storage=[{'sym': 'x', 'scope': 'kit', 'bits': 1}]), 7, detail='storage')
        refused('a closed table', drive_mod(B, 'ct', contribute=[{'to': 'core_lock_codes', 'data': '00' * 8}]), 15,
                detail='core_lock_codes')
        refused('a newer core', drive_mod(B, 'nc', resources={'core': '0.12'}), 9, detail='0.12')
        refused('format 1', drive_mod(B, 'f1', elemod=1), 5)
        refused('a full table (the later)', drive_mod(B, 'zfull', contribute=[
            {'to': 'ev_y', 'order': 1, 'data': '00000000'}]), 16, detail='ev_y',
            extra=[drive_mod(B, 'afull', contribute=[{'to': 'ev_y', 'order': 1, 'data': '00000000'}])])
        refused('no room', drive_mod(B, 'zroom', sections={'.bss': {'size': 3 * 1024 * 1024}}), 17)
        refused('a pc16 out of range', drive_mod(B, 'pc', imports=['core_fn'],
                                                 relocs=[['.run', 0, 'pc16', 'sym:core_fn', 0]]), 18)
        refused('a changed file', drive_mod(B, 'chg'), 2,
                active='good.elemod %08x\nchg.elemod 00000000\n' % zlib.crc32(
                    (json.dumps(good, indent=1) + '\n').encode()))
        gcrc = zlib.crc32((json.dumps(good, indent=1) + '\n').encode())
        s, _ = run(exe, F, [good], work, active='good.elemod %08x\ngone.elemod 12345678\n' % gcrc)
        check('drops a missing file', s['status'][1]['state'] == 3 and s['status'][1]['code'] == 1
              and s['status'][1]['id'] == 'gone.elemod' and s['loaded'] == 1, s)
        # two files with one id: the first line's loads, the second's is refused
        twin = dict(good, version='2')
        tdata = (json.dumps(twin, indent=1) + '\n').encode()
        s, _ = run(exe, F, [good], work, raw={}, active='good.elemod %08x\ngood2.elemod %08x\n'
                   % (gcrc, zlib.crc32(tdata)), extra_files={'good2.elemod': tdata})
        check('refuses a second file with the same id', [(x['state'], x['code']) for x in s['status']]
              == [(1, 0), (2, 8)], s)
        s, _ = run(exe, F, [good], work, active=''.join('m%d.elemod 0\n' % i for i in range(33)))
        check('more than 32 lines: nothing', s.get('skip') == 'count', s)
        big = drive_mod(B, 'big')
        refused('a file over 512 KB', big, 3, raw={'big': (json.dumps(big) + ' ' * (512 * 1024)).encode()})
        refused('broken JSON', drive_mod(B, 'bj'), 4, raw={'bj': b'{"elemod": 2, "id": "bj",'})

        # the check reports each file's version and loads nothing
        c, cram = run(exe, F, [user, hello], work, check=True)
        check('check: both would load, versions reported, RAM untouched',
              c['loaded'] == 2 and [x['version'] for x in c['status']] == [user.get('version', ''), hello.get('version', '')]
              and untouched(F, cram) and c['boot_status_n'] == 0, c)

        # a require of a refused mod goes too; its dependant of a pc16 refusal in pass 2 as well
        base = drive_mod(B, 'base', imports=['core_fn'], relocs=[['.run', 0, 'pc16', 'sym:core_fn', 0]])
        dep = drive_mod(B, 'dep', requires=['base'], imports=['fn_base'],
                        relocs=[['.run', 0, 'abs32', 'sym:fn_base', 0]])
        s, ram = run(exe, F, [good, dep, base], work)
        st = {x['id']: x for x in s['status']}
        check('pass-2 refusal drops its dependant', st['base']['code'] == 18 and st['dep']['code'] == 10
              and st['good']['state'] == 1, s)
        check('after a restart the survivor sits at the area start', area(F, ram, 8) == bytes.fromhex('4e714e714e714e75'))
        check('a restart leaves no bytes of the refused mod', not any(area(F, ram, 4096)[8:]))
        s, ram = run(exe, F, [base], work)
        check('a pass-2 refusal with nothing loaded leaves the area zero',
              s['loaded'] == 0 and not any(area(F, ram, 4096)), s)

        # the boot's time (dl.h, dl_bus_us): bus ticks at 132 MHz to ﾂｵs, across DTIM0's wrap
        src = os.path.join(work, 'bus_us.c')
        with open(src, 'w') as f:
            f.write('#include <stdio.h>\n#include "%s"\nint main(void) {\n'
                    '  printf("%%u %%u %%u %%u\\n", dl_bus_us(0, 131), dl_bus_us(1000, 1000 + 132 * 20000),\n'
                    '         dl_bus_us(0xFFFFFF00u, 0x00000184u), dl_bus_us(5, 5));\n  return 0;\n}\n'
                    % os.path.join(SRC, 'dl.h').replace('\\', '/'))
        exe2 = os.path.join(work, 'bus_us')
        subprocess.run(['cc', '-Wall', '-DDL_HOST', '-o', exe2, src], check=True)
        got = subprocess.run([exe2], capture_output=True, text=True, check=True).stdout.split()
        check('boot time: ticks to us, a wrap, zero', got == ['0', '20000', '4', '0'], got)

    if fails:
        print('%d failed' % len(fails))
        sys.exit(1)
    print('all passed')


if __name__ == '__main__':
    main()
