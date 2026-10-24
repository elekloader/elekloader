# SPDX-License-Identifier: GPL-2.0-or-later
"""The Octatrack's boot gate: core-ot and mods linked into one image, booted in octabam's
emulator (pytest, or run with python).

What it checks, after the build says the image is right (docs/TESTS.md, "The boot gate"):
- .boot ran once, and the RTOS handoff was reached;
- the reserve holds the linked RAM image byte for byte, and .bss holds zeros, except the bytes
  a test declares mutable (the core's own boot record and the meter's counts-per-frame);
- the hash .boot took of its copy is the linker's `__run_hash`, which the test recomputes on its
  own, and core's record (`ot_boot_info`) says so;
- a copy that differs from what was linked, by one byte at the start, the middle or the end of the
  image, halts the boot in `core_boot_fatal` before any mod code runs, with the hashes recorded;
- the pages the reserve gave back hold nothing at the handoff (a write there is a claim no mod
  declared; this emulator build cannot say which code wrote, so it sees only what stays written).

Needs the stock files and the cross toolchain, like test_octatrack, and the emulator:
  ELEKLOADER_OT_SYX   OCTATRACK_OS1.40C.syx
  ELEKLOADER_OT_EMU   octabam's built ot_emu (or ELEKLOADER_OCTABAM, a checkout with out/emu/ot_emu)
A test whose inputs are missing is skipped, not passed.
"""
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT)

from elekloader import devices, elemod, formats, link      # noqa: E402

SYX = os.environ.get('ELEKLOADER_OT_SYX', '')
OCTABAM = os.environ.get('ELEKLOADER_OCTABAM', '')
EMU = os.environ.get('ELEKLOADER_OT_EMU') or (
    os.path.join(OCTABAM, 'out', 'emu', 'ot_emu') if OCTABAM else '')
DEV = [d for d in devices.devices() if d.key == 'octatrack'][0]
REL = DEV.releases['1.40C']
OT_BOOT_OK, OT_BOOT_FAIL = 0x424f4f54, 0x4641494c           # 'BOOT', 'FAIL' (core.h)
BUDGET = 40_000_000             # instructions: the boot reaches the handoff in about 10 million
HALT_BUDGET = 8_000_000         # a halted boot: past the boot site (4.3 million), spinning in the loop


class Skip(Exception):
    pass


def need_build():
    if not SYX or not os.path.exists(SYX):
        raise Skip('missing ELEKLOADER_OT_SYX')
    prefix = os.environ.get('ELEKLOADER_CROSS', DEV.toolchain['prefix'])
    for tool in ('as', 'gcc'):
        if not shutil.which(prefix + tool):
            raise Skip('no m68k cross %s to build mods/core-ot' % tool)


def need_emulator():
    need_build()
    if not EMU or not os.access(EMU, os.X_OK):
        raise Skip('missing ELEKLOADER_OT_EMU (octabam\'s ot_emu, built)')


_c = {}


def stock_image():
    if 'img' not in _c:
        st, dev, rel = formats.load(SYX)
        assert dev is DEV and rel == REL
        _c['img'] = formats.main_image(st, DEV)
    return _c['img']


def linked(*mod_dirs):
    """mods/core-ot and the given folders (relative to the repository), built and linked."""
    from elekloader.sdk import build
    key = mod_dirs
    if key not in _c:
        tmp = tempfile.mkdtemp(prefix='ot-boot-')
        _c.setdefault('tmps', []).append(tmp)
        paths = [build.build(os.path.join(ROOT, 'mods', 'core-ot'), SYX, tmp)[0]]
        for d in mod_dirs:
            paths.append(build.build(os.path.join(ROOT, d), SYX, tmp)[0])
        _c[key] = link.link([elemod.load_any(p) for p in paths], stock_image())
    return _c[key]


def run_image(ln, image, tmp, dumps=(), budget=BUDGET):
    """Boot `image` in the emulator, dumping [(name, address, length)] at the stop.
    -> {'stopped': the stop line's text, 'handoff': bool, 'spin': the address it spun at or None,
    'boot_hits': how often the watched boot site's code ran (only a run that ends at the handoff
    prints them), 'dumps': {name: bytes}}."""
    raw = os.path.join(tmp, 'boot.raw')
    with open(raw, 'wb') as fh:
        fh.write(image)
    args = [EMU, '--image', raw, '--max', str(budget), '--watch-pc', '0x%x' % ln.map['core:boot']]
    if dumps:
        args += ['--mem-dump', ';'.join('0x%x,%d=%s' % (a, n, os.path.join(tmp, nm))
                                        for nm, a, n in dumps)]
    r = subprocess.run(args, capture_output=True, text=True, timeout=600)
    out = r.stdout
    stop = re.search(r'^stopped\s*:\s*(.*)$', out, re.M)
    assert stop, 'the emulator did not say why it stopped:\n%s' % out[-1500:]
    spin = re.search(r'unrecognised spin at ([0-9a-f]+)', stop.group(1))
    hits = [x for x in re.findall(r'^\s*\[\s*\d+\]\s+at 0x([0-9a-f]+)', out, re.M)
            if int(x, 16) == ln.map['core:boot']]
    got = {}
    for nm, _a, n in dumps:
        with open(os.path.join(tmp, nm), 'rb') as fh:
            got[nm] = fh.read()
        assert len(got[nm]) == n, (nm, len(got[nm]), n)
    return {'stopped': stop.group(1), 'handoff': stop.group(1).startswith('HANDOFF'),
            'spin': int(spin.group(1), 16) if spin else None, 'boot_hits': len(hits), 'dumps': got}


def own_hash(data):
    """The x33 hash of 32-bit big-endian words from 5381, written out again here so the test
    does not trust link.run_hash (docs/FORMAT.md, "The linker")."""
    h = 5381
    for i in range(0, len(data), 4):
        h = (h * 33 + int.from_bytes(data[i:i + 4], 'big')) % 2 ** 32
    return h


def run_image_bytes(ln):
    """The RAM image .boot copies, from the blob the linker appended after .boot."""
    L = ln.layout
    o = L['run_load'] - DEV.main_load
    return ln.image[o:o + L['ddr'][1] - L['ddr'][0]]


def unexplained(base, want, got, mutable=()):
    """The addresses where `got` (read from `base`) differs from `want` and no (lo, hi) of
    `mutable` covers them."""
    assert len(got) == len(want), (len(got), len(want))
    return [base + i for i in range(len(want))
            if got[i] != want[i] and not any(lo <= base + i < hi for lo, hi in mutable)]


def core_mutable(ln):
    """The bytes core itself writes between the copy and the handoff: the boot record, and the
    meter's counts-per-frame guess."""
    m = ln.map
    return [(m['core:ot_boot_info'], m['core:ot_boot_info'] + 12),
            (m['core:ot_meter'] + 20, m['core:ot_meter'] + 24)]


# ---- the gate's own checks -----------------------------------------------------------------

def test_the_gate_sees_a_changed_byte_a_declared_one_and_a_write_to_a_given_back_page():
    """The comparison, with planted differences and no emulator: a changed byte is reported,
    the same byte declared mutable is not, a clean image reports nothing, and a nonzero byte in
    the pages the reserve gave back is a foreign write."""
    want = bytes(range(64)) * 4
    assert unexplained(0x1000, want, want) == []
    got = bytearray(want)
    got[100] ^= 1
    assert unexplained(0x1000, want, bytes(got)) == [0x1000 + 100]
    assert unexplained(0x1000, want, bytes(got), [(0x1000 + 100, 0x1000 + 101)]) == []
    assert unexplained(0x1000, want, bytes(got), [(0x1000 + 101, 0x1000 + 102)]) == [0x1000 + 100]
    given = bytearray(4096)
    assert unexplained(0x2000, bytes(4096), bytes(given)) == []
    given[4095] = 7
    assert unexplained(0x2000, bytes(4096), bytes(given)) == [0x2000 + 4095]


def test_the_hash_the_tests_use_is_the_linkers():
    assert own_hash(b'') == 5381 == link.run_hash(b'')
    for data in (b'\x00\x00\x00\x00', b'\x00\x00\x00\x01', bytes(range(256)) * 3, bytes(4096)):
        assert own_hash(data) == link.run_hash(data), len(data)
    assert own_hash(b'\x00\x00\x00\x00') == 5381 * 33 % 2 ** 32          # a zero word still counts
    assert own_hash(b'\x00\x00\x00\x01') == own_hash(b'\x00\x00\x00\x00') + 1
    assert own_hash(b'\x00\x00\x00\x01\x00\x00\x00\x02') != own_hash(b'\x00\x00\x00\x02\x00\x00\x00\x01')


# ---- the boot --------------------------------------------------------------------------------

def check_clean_boot(ln, tmp, what):
    L, m = ln.layout, ln.map
    run = run_image_bytes(ln)
    run_lo, run_hi = L['ddr']
    bss_lo, bss_hi = L['bss']
    res_end = L['reserve']['end']
    ceiling = DEV.ddr[1]
    r = run_image(ln, ln.image, tmp, [('run', run_lo, run_hi - run_lo),
                                      ('bss', bss_lo, bss_hi - bss_lo),
                                      ('given', res_end, ceiling - res_end),
                                      ('info', m['core:ot_boot_info'], 12)])
    assert r['handoff'], '%s: %s' % (what, r['stopped'])
    assert r['boot_hits'] == 1, '%s: .boot ran %d times' % (what, r['boot_hits'])
    mutable = core_mutable(ln)
    assert unexplained(run_lo, run, r['dumps']['run']) == [], '%s: the reserve is not the linked run image' % what
    assert unexplained(bss_lo, bytes(bss_hi - bss_lo), r['dumps']['bss'], mutable) == [], what
    assert unexplained(res_end, bytes(ceiling - res_end), r['dumps']['given']) == [], \
        '%s: something wrote in the pages the reserve gave back' % what
    want = own_hash(run)
    assert m['__run_hash'] == want, (hex(m['__run_hash']), hex(want))
    assert struct.unpack('>3I', r['dumps']['info']) == (OT_BOOT_OK, want, want), what
    return r


def test_a_boot_runs_the_copy_once_and_reaches_the_handoff():
    """Core alone, and core with mods that have a RAM image: .boot ran once, the reserve is the
    linked image, the hash it took is the linker's, the handoff is reached."""
    need_emulator()
    for dirs in ((), ('examples/hello-marker-ot',), ('mods/intercepts-ot', 'examples/hello-marker-ot')):
        ln = linked(*dirs)
        with tempfile.TemporaryDirectory() as tmp:
            r = check_clean_boot(ln, tmp, '+'.join(['core'] + [os.path.basename(d) for d in dirs]))
        assert r['stopped'].startswith('HANDOFF')


def test_a_copy_that_differs_halts_the_boot_before_any_mod_code_runs():
    """One byte of the RAM image wrong where .boot reads it, at the first word, the middle and the
    last: the boot halts in core_boot_fatal (the emulator calls the loop a spin there), the record
    says FAIL with the linker's hash and the hash of the planted copy, and the handoff is never
    reached. The same image unplanted boots (test above)."""
    need_emulator()
    ln = linked('mods/intercepts-ot', 'examples/hello-marker-ot')
    L, m = ln.layout, ln.map
    run = run_image_bytes(ln)
    base = L['run_load'] - DEV.main_load
    fatal = m['core:core_boot_fatal']
    for at in (0, len(run) // 2, len(run) - 1):
        planted = bytearray(ln.image)
        planted[base + at] ^= 0x40
        bad_run = bytearray(run)
        bad_run[at] ^= 0x40
        with tempfile.TemporaryDirectory() as tmp:
            r = run_image(ln, bytes(planted), tmp, [('info', m['core:ot_boot_info'], 12)],
                          budget=HALT_BUDGET)
        assert not r['handoff'], (at, r['stopped'])
        assert r['spin'] is not None and fatal <= r['spin'] < fatal + 0x30, \
            (at, r['stopped'], hex(fatal))
        assert struct.unpack('>3I', r['dumps']['info']) == (
            OT_BOOT_FAIL, m['__run_hash'], own_hash(bytes(bad_run))), at
        assert own_hash(bytes(bad_run)) != m['__run_hash']


# ---- the linker's side of it --------------------------------------------------------------------

def test_the_boot_names_the_hash_the_linker_computes():
    """The .boot the linker places has the hash in it as the immediate of the compare's load,
    and it follows the image: another set of mods, another hash."""
    need_build()
    one, two = linked(), linked('examples/hello-marker-ot')
    assert one.map['__run_hash'] != two.map['__run_hash']
    for ln in (one, two):
        boot = ln.image[ln.layout['boot'][0] - DEV.main_load:ln.layout['boot'][1] - DEV.main_load]
        assert b'\x24\x3c' + struct.pack('>I', ln.map['__run_hash']) in boot   # move.l #hash,%d2
        assert ln.map['__run_hash'] == own_hash(run_image_bytes(ln))


if __name__ == '__main__':
    import traceback
    ok = skipped = failed = 0
    for n in sorted(k for k in globals() if k.startswith('test_')):
        try:
            globals()[n]()
            print('ok      %s' % n)
            ok += 1
        except Skip as e:
            print('SKIP    %s (%s)' % (n, e))
            skipped += 1
        except Exception:
            print('FAIL    %s' % n)
            traceback.print_exc()
            failed += 1
    for tmp in _c.get('tmps', []):
        shutil.rmtree(tmp, ignore_errors=True)
    print('%d passed, %d failed, %d skipped' % (ok, failed, skipped))
    sys.exit(1 if failed else 0)
