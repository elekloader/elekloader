# SPDX-License-Identifier: GPL-2.0-or-later
"""The catalog against a new core (pytest, or run with python): every Digitakt
mk1 mod of a catalog, alone and with every other, linked against the
catalog's core 2.1 and against the core a builder takes for it
(gui.with_core) of the catalog's and mods/core built from this tree. The
core built here keeps all of 2.1, and a builder takes it for every
selection it takes, so every selection must link or be refused with it as
it is with 2.1, with the same messages. Three kinds of selection are the
exceptions:
- one with a mod that needs core 3.0 (machine-pages, say): 2.1 refuses it
  for that;
- one whose mods take more RAM than core 2.1's 128 KB: 2.1 refuses it for
  that, and the builder's core has 256 KB;
- one with a mod that patches a place the core built here patches (Digi EQ
  1.0c and Tone+FX 4.0c patch the master output's, which core 4.1 took):
  the builder takes the newest of the catalog's cores that leaves the place
  alone, and with that core the selection links or is refused as with 2.1.

Needs files named by environment variables; without them it skips:
  ELEKLOADER_CATALOG_DIR  a folder with the catalog's .elemod files, cores
                          included (js/tools/kit.ts sync, or the site's shop/
                          and core/ folders together)
  ELEKLOADER_STOCK        Digitakt_OS1.53.syx
  ELEKLOADER_STOCK_154    optional: Digitakt_OS1.54.syx
and the m68k cross toolchain, to build mods/core and mods/machine-pages.
"""
import glob
import itertools
import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT)

from elekloader import devices, elemod, formats, gui, link     # noqa: E402
from elekloader.sdk import build                          # noqa: E402

CATALOG = os.environ.get('ELEKLOADER_CATALOG_DIR', '')
STOCKS = {'1.53': os.environ.get('ELEKLOADER_STOCK', ''),
          '1.54': os.environ.get('ELEKLOADER_STOCK_154', '')}


class Skip(Exception):
    pass


def outcome(mods, image):
    try:
        link.link(mods, image)
        return 'links'
    except elemod.ModError as e:
        return str(e)


def describe(m):
    """What gui.with_core reads of a file, as gui.LoaderModel.describe lists it."""
    d = {'id': m.id, 'version': m.version, 'fits': True, 'needs_core': getattr(m, 'needs_core', None),
         'sites': [(s['addr'], s['len']) for s in m.sites]}
    if not isinstance(m, link.Mod2):
        return dict(d, format=1)
    d.update(format=2, ram_bound=m.ram_bound())
    if '.boot' in m.sections:
        lo, hi = m.dev.ddr_for(m.version)
        d['ram_area'] = hi - lo
    return d


def taken(cores, ms):
    """The core a builder takes for a selection (gui.with_core), of `cores`: the newest that is new enough for
    the mods and takes them."""
    descs = {'core/%02d' % i: describe(c) for i, c in enumerate(cores)}
    descs.update(('mods/%02d' % i, describe(m)) for i, m in enumerate(ms))
    on = gui.with_core(descs, {p for p in descs if p.startswith('mods/')})
    return cores[int(next(p for p in on if p.startswith('core/'))[5:])]


def matrix(os_version, tmp):
    """-> [(selection, 2.1's outcome, the outcome with the core built here, its mods, the core a builder takes)]
    for one OS, and the cores."""
    st = STOCKS[os_version]
    if not st or not os.path.exists(st):
        raise Skip('missing the stock file for %s' % os_version)
    stock, dev, rel = formats.load(st)
    image = formats.main_image(stock, dev)
    files = [elemod.load_any(p) for p in sorted(glob.glob(os.path.join(CATALOG, '*.elemod')))]
    files = [m for m in files if m.dev.key == 'digitakt-mk1' and m.rel.version == os_version]
    old = [m for m in files if m.id == 'core' and m.version == '2.1']
    if not old:
        raise Skip('no core 2.1 for %s in ELEKLOADER_CATALOG_DIR' % os_version)
    new = link.Mod2(*_load(build.build(os.path.join(ROOT, 'mods', 'core'), st, tmp)[0]))
    mods = [m for m in files if m.id != 'core']
    byid = {m.id: m for m in mods}
    cores = sorted((m for m in files if m.id == 'core'), key=lambda m: elemod.version_key(m.version)) + [new]

    def with_requirements(sel):
        out = list(sel)
        for m in sel:
            out += [byid[r] for r in m.requires if r in byid and byid[r] not in out]
        return out

    rows = []
    for sel in [(m,) for m in mods] + list(itertools.combinations(mods, 2)):
        ms = with_requirements(sel)
        a = outcome([old[0]] + ms, image).replace(old[0].label(), 'core')
        b = outcome([new] + ms, image).replace(new.label(), 'core')
        rows.append(([m.label() for m in sel], a, b, ms, taken(cores, ms)))
    return rows, old[0], new, image


def _load(path):
    doc, raw = elemod.read_json(path)
    return doc, os.path.basename(path), raw


def check_os(os_version):
    if not CATALOG or not os.path.isdir(CATALOG):
        raise Skip('missing ELEKLOADER_CATALOG_DIR')
    tc = devices.devices()[0].toolchain
    if not shutil.which(os.environ.get('ELEKLOADER_CROSS', tc['prefix']) + 'as'):
        raise Skip('no m68k cross assembler to build mods/core')
    with tempfile.TemporaryDirectory() as tmp:
        rows, old, new, _image = matrix(os_version, tmp)
        assert elemod.version_key(new.version) >= elemod.version_key('3.0') and rows
        same, on3, older, big = [], [], [], []
        for sel, a, b, ms, pick in rows:
            needs = [getattr(m, 'needs_core', None) for m in ms]
            need = max((v for v in needs if v), key=elemod.version_key, default=None)
            if pick is new:
                # the core a builder takes: the newest, the one built here
                assert elemod.pick_core([old.version, new.version], need) == 1, sel
            else:
                # a mod of the selection patches a place the core built here patches: the linker says so, and
                # the builder takes an older core of the catalog's, which leaves the place alone
                assert ' overlap ' in b and elemod.version_key(pick.version) < elemod.version_key(new.version), (sel, b)
                b = outcome([pick] + ms, _image).replace(pick.label(), 'core')
                older.append((sel, pick.version))
            if need and elemod.version_key(need) > elemod.version_key(old.version):
                # a mod that needs 3.0: refused with 2.1 for that
                assert 'needs core %s or newer' % need in a, (sel, a)
                on3.append((sel, b))
            elif a.startswith('the mods need RAM') and describe(pick)['ram_area'] > describe(old)['ram_area']:
                # more than 2.1's 128 KB: refused with 2.1 for that, and the builder's core has more
                big.append((sel, b))
            else:
                # with the same outcome as with 2.1
                assert a == b, (sel, a, b)
                same.append(a)
        linked = sum(a == 'links' for a in same)
        print('  %s: %d selections link (%d) or are refused (%d) with core %s as with 2.1'
              % (os_version, len(same), linked, len(same) - linked, new.version))
        if on3:
            print('  %s: %d need core 3.0: %d link, %d are refused'
                  % (os_version, len(on3), sum(b == 'links' for _s, b in on3),
                     sum(b != 'links' for _s, b in on3)))
        if big:
            print('  %s: %d take more RAM than core 2.1 has: %d link, %d are refused'
                  % (os_version, len(big), sum(b == 'links' for _s, b in big),
                     sum(b != 'links' for _s, b in big)))
        if older:
            print('  %s: %d take an older core than %s, for a place both patch: %s'
                  % (os_version, len(older), new.version,
                     ', '.join(sorted({'%s (core %s)' % (sel[0], v) for sel, v in older if len(sel) == 1}))))


def test_the_catalog_links_with_the_newest_core_as_with_core_2_1():
    check_os('1.53')


def test_the_catalog_links_with_the_newest_core_as_with_core_2_1_on_1_54():
    check_os('1.54')


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
    print('%d passed, %d failed, %d skipped' % (ok, failed, skipped))
    sys.exit(1 if failed else 0)
