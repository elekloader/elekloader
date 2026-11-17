# SPDX-License-Identifier: GPL-2.0-or-later
"""Payload skip (docs/DRIVE.md, "Payloads on the +Drive"): both loaders stage each +Drive file
without its .payload's hex (skip.c, the A4's and from drive-loader 0.8 the Rytm's copy, which this
test builds), and the boot linker (dlink.c with DL_PAYLOAD_SKIP) takes the .payload left as one
empty hex part.

Built for the computer and run against the twin (elekloader/drive.py, stage() and
Build.payload_skip): the C staging and the twin's agree byte for byte on files that skip, files that
don't and mutated ones; the hex never reaches the stage past one small read; a set with payloads
links the same in C and twin, with no payload byte in RAM; a payload that can't be skipped and a
relocation into one are refused alike; and /mods/active's CRC is the staged file's.

    python tests/test_drive_payload_skip.py

Needs a C compiler (cc)."""
import hashlib
import json
import os
import random
import subprocess
import sys
import tempfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT)
sys.path.insert(0, HERE)

from elekloader import desc, drive                                 # noqa: E402
from test_link_refusals import Bench                               # noqa: E402
import test_drive as TD                                            # noqa: E402
from test_drive_top_stage import Built, code_mod, compare, raw    # noqa: E402

A4 = os.path.join(ROOT, 'mods', 'drive-loader-a4mk2')
AR2 = os.path.join(ROOT, 'mods', 'drive-loader-ar2')
KB = 1024
BREATHE = 0x8000                         # dl.h DL_BREATHE: a read's most; in .payload, DL_STAGE_STEP


def build_hosts(work):
    skip = os.path.join(work, 'drive_skip_host')
    subprocess.run(['cc', '-O1', '-Wall', '-DDL_HOST', '-o', skip, os.path.join(HERE, 'drive_skip_host.c'),
                    os.path.join(AR2, 'skip.c')], check=True)
    link = os.path.join(work, 'drive_link_host_skip')
    subprocess.run(['cc', '-O1', '-Wall', '-DDL_HOST', '-DDL_PAYLOAD_SKIP', '-o', link,
                    os.path.join(HERE, 'drive_link_host.c'), os.path.join(AR2, 'dlink.c'),
                    os.path.join(AR2, 'json.c'), os.path.join(AR2, 'dstore.c'), os.path.join(AR2, 'sha256.c'),
                    os.path.join(AR2, 'skip.c')], check=True)
    return skip, link


def c_stage(exe, data, work, max_=None, hex_range=None):
    """skip.c's dl_stage_file on data. -> (result, staged bytes, payload or None, hex bytes read)."""
    fd, src = tempfile.mkstemp(dir=work)
    with os.fdopen(fd, 'wb') as fh:
        fh.write(data)
    out = src + '.out'
    args = [exe, src, out, str(max_ if max_ is not None else len(data) + 1)]
    if hex_range:
        args += [str(hex_range[0]), str(hex_range[1])]
    r = subprocess.run(args, capture_output=True, text=True, check=True)
    j = json.loads(r.stdout)
    with open(out, 'rb') as fh:
        staged = fh.read()
    pay = {'off': j['off'], 'len': j['len'], 'sha256': j['sha256']} if j['len'] else None
    return j['r'], staged, pay, j['hex_read']


SKIPS = type('SkipBuild', (), {'payload_skip': True})()   # a build whose loader skips payloads


def c_step(exe, data, work, max_, hex_range=None):
    """skip.c's dl_stage_more on data, one call a tick as the MODS page reads (boot.c dl_read_more).
    -> (result, staged bytes when it fit, staged length, CRC-32, calls, hex bytes read, reads)."""
    fd, src = tempfile.mkstemp(dir=work)
    with os.fdopen(fd, 'wb') as fh:
        fh.write(data)
    out = src + '.out'
    args = [exe, '--step', src, out, str(max_)]
    if hex_range:
        args += [str(hex_range[0]), str(hex_range[1])]
    j = json.loads(subprocess.run(args, capture_output=True, text=True, check=True).stdout)
    with open(out, 'rb') as fh:
        staged = fh.read()
    return j['r'], staged, j['got'], j['crc'], j['calls'], j['hex_read'], j['reads']


def payload_section(data, **over):
    d = {'align': 4, 'len': len(data), 'sha256': hashlib.sha256(data).hexdigest(), 'parts': [['hex', data.hex()]]}
    d.update(over)
    return d


def with_payload(m, data, **over):
    m = json.loads(json.dumps(m))
    m['sections']['.payload'] = payload_section(data, **over)
    return m


def blob(n, seed=b'payload'):
    out = bytearray()
    while len(out) < n:
        seed = hashlib.sha256(seed).digest()
        out += seed
    return bytes(out[:n])


def main():
    work = tempfile.mkdtemp()
    skip_exe, link_exe = build_hosts(work)
    fails = []

    def check(name, cond, info=''):
        print('%s %s %s' % ('ok  ' if cond else 'FAIL', name, info if not cond else ''))
        if not cond:
            fails.append(name)

    def same(name, data):
        """C and twin stage data alike. -> the twin's (staged, payload)."""
        r, staged, pay, _ = c_stage(skip_exe, data, work)
        t_staged, t_pay = drive.stage(data)
        if t_pay is not None and t_pay['len'] == 0:
            t_pay = None                 # the C harness reports a payload of 0 bytes as none
        check(name + ': C and twin stage alike', r == len(staged) and staged == t_staged and pay == t_pay,
              (r, len(staged), len(t_staged), pay, t_pay))
        return t_staged, t_pay

    with Bench('analog-rytm-mk2') as B:
        base = code_mod(B, 'pay-a', 2 * KB)
        data = blob(3000)

        # ---- staging: C against the twin ----
        st, pay = same('no payload', raw(base))
        check('no payload: staged as it is', st == raw(base) and pay is None)
        f = raw(with_payload(base, data))
        st, pay = same('a payload', f)
        check('a payload: its hex left out, "" in its place', len(st) == len(f) - 2 * len(data)
              and data.hex().encode() not in st, len(st))
        check('a payload: offset, length and sha256 as the file gives them',
              pay == {'off': f.index(data.hex().encode()), 'len': len(data),
                      'sha256': hashlib.sha256(data).hexdigest()}, pay)
        doc = json.loads(st)
        check('a payload: the staged file is the same JSON with an empty hex part',
              doc['sections']['.payload']['parts'] == [['hex', '']]
              and doc['sections']['.run'] == base['sections']['.run'])
        compact = json.dumps(with_payload(base, data), separators=(',', ':')).encode()
        st, pay = same('compact JSON', compact)
        check('compact JSON: skipped too', pay and pay['len'] == len(data))
        upper = raw(with_payload(base, data, parts=[['hex', data.hex().upper()]]))
        same('upper-case hex', upper)

        _r, _s, _p, hex_read = c_stage(skip_exe, f, work, hex_range=(pay_off := f.index(data.hex().encode()),
                                                                     pay_off + 2 * len(data)))
        check('the hex reaches the stage only within one read (%d bytes)' % hex_read, hex_read <= BREATHE, hex_read)
        big = blob(400 * KB)
        fb = raw(with_payload(base, big))
        off = fb.index(big[:64].hex().encode())
        _r, sb, pb, hex_read = c_stage(skip_exe, fb, work, hex_range=(off, off + 2 * len(big)))
        check('a 400 KB payload: staged in %d bytes, %d hex bytes read' % (len(sb), hex_read),
              len(sb) == len(fb) - 2 * len(big) and hex_read <= BREATHE and pb['len'] == len(big))
        r, _s, _p, _h = c_stage(skip_exe, fb, work, max_=len(sb))
        check('a 400 KB payload: fits a stage of its staged size', r == len(sb), r)
        r, _s, _p, _h = c_stage(skip_exe, fb, work, max_=len(sb) - 1)
        check('a 400 KB payload: one byte less is too big (-2)', r == -2, r)

        # ---- the MODS page's stepped read (dl_stage_more): the boot's staging and /mods/active's CRC ----
        for name, data_ in (('no payload', raw(base)), ('a payload', f), ('compact JSON', compact),
                            ('a 400 KB payload', fb)):
            t_st = drive.stage(data_)[0]
            for mx in (len(t_st) + 1, len(t_st), 4 * KB + 1, 100):
                r, st, got, crc, calls, _h, _n = c_step(skip_exe, data_, work, mx)
                want = 1 if len(t_st) <= mx else -2
                check('stepped read, %s, buffer %d: staged as the boot stages it, CRC as /mods/active' % (name, mx),
                      r == want and got == len(t_st) and crc == zlib.crc32(t_st)
                      and (r != 1 or st == t_st) and crc == int(drive.active_text([('x', data_)], SKIPS)
                                                                    .split()[1], 16),
                      (r, want, got, len(t_st), crc, zlib.crc32(t_st), calls))
        r, _st, got, _crc, calls, hex_read, _n = c_step(skip_exe, fb, work, len(sb) + 1,
                                                        hex_range=(off, off + 2 * len(big)))
        check('stepped read, 400 KB payload: %d calls, %d hex bytes read' % (calls, hex_read),
              r == 1 and hex_read <= BREATHE and calls <= 2 + len(sb) // BREATHE + 8, (r, calls, hex_read))
        ns = raw(_no_sha(with_payload(base, blob(20 * KB))))     # not skipped: its hex read in small steps
        r, st, got, crc, calls, _h, reads = c_step(skip_exe, ns, work, len(ns) + 1)
        check('stepped read, a payload not skipped: whole, at most 64 short reads a call (%d reads, %d calls)'
              % (reads, calls), r == 1 and st == ns and crc == zlib.crc32(ns) and reads <= 65 * calls,
              (r, reads, calls))
        not_skipped = {
            'no sha256': _no_sha(with_payload(base, data)),
            'sha256 after parts': None,
            'a short sha256': with_payload(base, data, sha256='ab' * 31),
            'no len': None,
            '.payload outside sections': None,
            '.payload in a string': None,
        }
        m = with_payload(base, data)
        p = m['sections']['.payload']
        m['sections']['.payload'] = {'align': 4, 'len': p['len'], 'parts': p['parts'], 'sha256': p['sha256']}
        not_skipped['sha256 after parts'] = m
        m = with_payload(base, data)
        del m['sections']['.payload']['len']
        not_skipped['no len'] = m
        m = json.loads(json.dumps(base))
        m['.payload'] = payload_section(data)
        not_skipped['.payload outside sections'] = m
        m = json.loads(json.dumps(base))
        m['description'] = '"sections": {".payload": {"len": 4, "sha256": "%s", "parts": [["hex", "\\"' % ('0' * 64)
        not_skipped['.payload in a string'] = m
        for name, m in not_skipped.items():
            st, pay = same(name, raw(m))
            check(name + ': staged as it is', st == raw(m) and pay is None)

        # mutated files: whatever the bytes, C and twin stage alike
        rnd = random.Random(1115)
        small = raw(with_payload(code_mod(B, 'pay-m', 64), blob(48)))
        bad = 0
        for i in range(300):
            b = bytearray(small)
            for _ in range(rnd.randint(1, 4)):
                at = rnd.randrange(len(b))
                op = rnd.randrange(3)
                ch = rnd.choice(b'{}[]",:\\ 0123456789abx.')
                if op == 0:
                    b[at] = ch
                elif op == 1:
                    b.insert(at, ch)
                else:
                    del b[at]
            r, staged, pay, _ = c_stage(skip_exe, bytes(b), work)
            t_staged, t_pay = drive.stage(bytes(b))
            if t_pay is not None and t_pay['len'] == 0:
                t_pay = None
            if staged != t_staged or pay != t_pay:
                bad += 1
                if bad <= 3:
                    print('  differs:', bytes(b)[:400])
        check('300 mutated files: C and twin stage alike', bad == 0, bad)

        # ---- the link: C (dlink.c with DL_PAYLOAD_SKIP) against the twin ----
        F = Built(B, 1024 * KB)
        F.build.payload_skip = True

        def boot(mods, crc_of=lambda d: drive.file_crc(drive.stage(d)[0])):
            d = tempfile.mkdtemp(dir=work)
            lines = []
            for m in mods:
                name = m['id'] + '.elemod'
                data_ = raw(m)
                with open(os.path.join(d, name), 'wb') as fh:
                    fh.write(data_)
                lines.append('%s %08x' % (name, crc_of(data_)))
            with open(os.path.join(d, 'active'), 'w') as fh:
                fh.write('\n'.join(lines) + '\n')
            ramf, stockf, outf = (os.path.join(d, x) for x in ('ram.bin', 'stock.bin', 'out.bin'))
            with open(ramf, 'wb') as fh:
                fh.write(F.ram)
            with open(stockf, 'wb') as fh:
                fh.write(F.B.img)
            r = subprocess.run([link_exe, ramf, '0x%x' % F.ram_base, stockf, '0x%x' % F.B.dev.main_load,
                                '0x%x' % F.desc_at, d, outf], capture_output=True, text=True,
                               env=dict(os.environ, DL_HOST_TOP='1'))
            if r.returncode:
                raise AssertionError('host linker failed: %s %s' % (r.stdout, r.stderr))
            with open(outf, 'rb') as fh:
                return json.loads(r.stdout.splitlines()[0]), fh.read()

        def twin(mods):
            return drive.link(F.build, [(m['id'] + '.elemod', raw(m)) for m in mods])

        def states(sm):
            return [(x['state'], x['code'], x['arg']) for x in sm['status']]

        heavy = blob(700 * KB, b'heavy')          # with its hex, the file is 1.4 MB: past the 1 MB area
        mods = [with_payload(code_mod(B, 'pay-a', 8 * KB), heavy), code_mod(B, 'pay-b', 4 * KB)]
        check('the payload mod\'s file is larger than the area', len(raw(mods[0])) > F.build.hi - F.build.lo)
        sm, ram = boot(mods)
        r = twin(mods)
        check('a 700 KB payload in a 1 MB area: both load', states(sm) == [(1, 0, 0), (1, 0, 0)], sm['status'])
        check('a 700 KB payload: C and twin agree', not compare(F, sm, ram, r), compare(F, sm, ram, r))
        check('a 700 KB payload: none of its bytes in RAM', heavy[:256] not in ram and heavy[-256:] not in ram)
        sm, _ram = boot(mods, crc_of=drive.file_crc)
        check('/mods/active with the whole file\'s CRC: changed (2), the staged file\'s CRC is the one',
              states(sm)[0] == (3, 2, 0), sm['status'])
        check('active_text gives the staged CRC on a build that skips, the file\'s on one that doesn\'t',
              drive.active_text([('a', raw(mods[0]))], F.build).split()[1]
              == '%08x' % (zlib.crc32(drive.stage(raw(mods[0]))[0]) & 0xFFFFFFFF)
              and drive.active_text([('a', raw(mods[0]))]).split()[1]
              == '%08x' % (zlib.crc32(raw(mods[0])) & 0xFFFFFFFF))

        def refused(name, mods_, want):
            sm_, _ram = boot(mods_)
            r_ = twin(mods_)
            got = [(c.state, c.code, c.detail) for c in r_.choices]
            check(name + ': refused alike', states(sm_) == [(c.state, c.code, c.arg) for c in r_.choices]
                  and [x.get('detail', '') for x in sm_['status']] == [c.detail for c in r_.choices]
                  and got[0] == want, (sm_['status'], got))

        refused('a payload without sha256', [_no_sha(with_payload(code_mod(B, 'pay-a', 8 * KB), data))],
                (2, 7, '.payload'))
        two = with_payload(code_mod(B, 'pay-a', 8 * KB), data, parts=[['hex', data[:8].hex()], ['hex', data[8:].hex()]])
        same('two parts', raw(two))
        refused('two parts (the first one\'s hex skipped, the second kept)', [two], (2, 7, '.payload'))
        m = with_payload(code_mod(B, 'pay-a', 8 * KB), data)
        m['relocs'].append(['.run', 0, 'abs32', 'sec:.payload', 0])
        refused('an abs32 into .payload (as sidecar-codetest has)', [m], (2, 7, 'relocs into .payload'))
        m = with_payload(code_mod(B, 'pay-a', 8 * KB), data)
        m['contribute'][0]['relocs'].append([0, 'abs32', 'sec:.payload', 0])
        refused('a contribution relocation into .payload', [m], (2, 7, 'relocs into .payload'))

        # a loader without the skip (the Rytm's): any .payload stays a feature refusal
        F.build.payload_skip = False
        r = twin([with_payload(code_mod(B, 'pay-a', 8 * KB), data)])
        check('without F_PAYLOAD_SKIP: .payload refused as before',
              (r.choices[0].code, r.choices[0].detail) == (7, '.payload'))
        _out, b = TD.built(B, 'skip')
        check('a loader exporting drive_payload_skip sets the descriptor\'s F_PAYLOAD_SKIP (bit 1)',
              b.payload_skip and b.stage_top and b.d['flags'] == desc.F_STAGE_TOP | desc.F_PAYLOAD_SKIP == 3, b.d['flags'])
        check('a loader without it: no payload skip', not TD.built(B, 'top')[1].payload_skip)

    print('all passed' if not fails else '%d failed' % len(fails))
    return 1 if fails else 0


def _no_sha(m):
    del m['sections']['.payload']['sha256']
    return m


if __name__ == '__main__':
    sys.exit(main())
