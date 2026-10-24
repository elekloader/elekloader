# SPDX-License-Identifier: GPL-2.0-or-later
"""Your stock OS file + .elemod files -> a custom firmware (the command line).

    python -m elekloader.patch --stock Digitakt_OS1.53.syx \
        --mod core-2.1.elemod --mod sysinfo-2.0a.elemod ... --out CUSTOM.syx \
        [--version 2.0a] [--check] [--lockfile FILE] [--lockfile-from OLD.syx]

In this order, stopping at the first failure:
  1. the stock file must be a release elekloader knows (by its sha256), and
     its main OS the known image;
  2. every mod must be made for that release, and every site's stock bytes
     must hash to what the mod expects;
  3. the static conflict check (elemod.check, link.check), including the
     device's protected ranges;
  4. apply the sites, or link the mods;
  5. repack the main OS and rebuild the OS file: only the main OS, optional
     profiled SHARC section 7, and the version field change;
  6. verify every output with independent code (formats.verify): the rest
     of the file is stock, every checksum, the main OS depacks to the
     patched image (in place, where the device's bootloader staging is
     known), the protected ranges are stock, the flash budget;
  7. write the .syx and, for a device with a card file (the Octatrack), the
     .bin beside it; OUT.json (a manifest with every hash); and, for linked
     mods, OUT.map.json (every symbol's address).

A core with mod storage (core-ar2 0.2) places the mods' fields from a
lockfile (elekloader/store.py): --lockfile FILE, by default DEVICE.lock.json
in the output's folder (analog-rytm-mk2.lock.json), so every build written
there shares it. Keep it, and give it to every later build: a build without
it packs the fields afresh, and when the mods have changed since, the unit
loads every project's mod data at defaults. It is written back after each
build, and copies go in the manifest and in the build itself: --lockfile-from
OLD.syx takes it out of an earlier build when the lockfile is lost.

--check stops after step 4 and writes nothing. Nothing here talks to a
device: you flash the file yourself, as with any OS update, and the stock
file recovers the unit because the bootloader is never changed.
"""
import argparse
import json
import os
import sys
import time

from . import devices, elemod, formats, link, store
from .elemod import sha


class PatchError(Exception):
    pass


def build(stock_path, mod_paths, version=None, check_only=False, log=print, lock=None):
    """-> (outputs {'syx': bytes[, 'bin': bytes]} or None, manifest dict).
    lock: the mod-storage lockfile (a dict), or None for a new one; the new
    one is the manifest's 'store'. Raises PatchError."""
    try:
        stock, dev, rel = formats.load(stock_path)
    except OSError as e:
        raise PatchError('cannot read %s: %s' % (stock_path, e))
    except (devices.UnknownFirmware, formats.FormatError) as e:
        raise PatchError('%s: %s' % (stock_path, e))
    img0 = formats.main_image(stock, dev)
    if sha(img0) != rel.main_sha256:
        raise PatchError('its main OS is not the known %s %s image' % (dev.name, rel.version))
    log('stock: %s OS %s (%s)' % (dev.name, rel.version, stock.sha256))
    mods = []
    for p in mod_paths:
        try:
            m = elemod.load_any(p)
        except (OSError, elemod.ModError) as e:
            raise PatchError(str(e))
        if m.dev.key != dev.key or m.rel != rel:
            raise PatchError('%s is made for %s %s; your stock firmware is %s %s'
                             % (m.label(), m.dev.name, m.rel.version, dev.name, rel.version))
        log('mod %-24s %d sites%s  (file sha256 %s)'
            % (m.label(), len(m.sites), ', blob %d bytes' % m.blob['len'] if m.blob else '',
               m.sha256[:16]))
        mods.append(m)
    if not mods:
        raise PatchError('no mods given')
    v2 = [m for m in mods if isinstance(m, link.Mod2)]
    linked = None
    try:
        if v2:
            if len(v2) != len(mods):
                raise elemod.ModError('a whole-build bundle (format 1) cannot be combined '
                                      'with separate mods (format 2)')
            dsp = stock.section(rel.sharc['section']) if any(m.sharc for m in v2) else None
            linked = link.link(mods, img0, lock, sharc_stream=dsp)
            img = linked.image
        else:
            img = elemod.apply(mods, img0)
    except elemod.ModError as e:
        raise PatchError(str(e))
    log('the mods combine: no overlaps, stock bytes as expected, code sites whole instructions')
    if linked:
        L = linked.layout
        log('linked %s: RAM 0x%08x-0x%08x (%d bytes spare), .fast to 0x%08x (%d spare), '
            'blob %d bytes' % (', '.join(linked.order), L['ddr'][0], L['bss'][1],
                               L['ddr_spare'], L['fast'][1], L['fast_spare'], L['blob_len']))
    man = {
        'patcher': 'elekloader', 'device': dev.key,
        'stock': {'path': os.path.abspath(stock_path), 'sha256': stock.sha256,
                  'os': rel.version, 'main_sha256': sha(img0)},
        'mods': [{'id': m.id, 'version': m.version, 'file': m.name, 'sha256': m.sha256,
                  'format': 2 if isinstance(m, link.Mod2) else 1, 'sites': len(m.sites),
                  'regions': [{'name': g['name'], 'lo': '0x%08x' % g['lo'],
                               'hi': '0x%08x' % g['hi'], 'area': g['area']} for g in m.regions],
                  'names': m.names} for m in mods],
        'main_sha256': sha(img),
        'main_len': len(img),
    }
    if linked:
        man['link'] = {'order': linked.order, 'layout': linked.layout,
                       'tables': {c: {'at': '0x%08x' % a, 'entries': n, 'entry': e}
                                  for c, (a, n, e) in sorted(linked.tables.items())}}
        man['_map'] = linked.map
        if linked.store is not None:
            man['store'] = linked.store
            for n in linked.store_notes:
                log('storage: ' + n)
    if len(mods) == 1 and not v2 and (mods[0].doc.get('build') or {}).get('section3_sha256'):
        want = mods[0].doc['build'].get('section3_sha256')
        man['bundle_matches_build'] = want == sha(img)
        if want != sha(img):
            raise PatchError('the bundle does not reproduce its build\'s main OS')
        log('the main OS matches the build the bundle came from (sha256 %s)' % sha(img)[:16])
    if check_only:
        return None, man
    if version is None:
        vs = {m.doc.get('ele3_version') for m in mods} - {None}
        if not vs and dev.version_optional:
            vs = {stock.version}
        if len(vs) != 1:
            raise PatchError('give --version (%s): the mods name %s'
                             % ('%d characters' % dev.version_len if dev.container == 'ele3'
                                else 'up to %d characters' % dev.version_len,
                                sorted(vs) or 'no version'))
        version = vs.pop()
    try:
        formats.check_version(dev, version)
    except formats.FormatError as e:
        raise PatchError(str(e))
    t = time.time()
    stored = formats.pack_main(img, dev)
    log('packed the main OS: %d -> %d bytes (%.1f s)' % (len(img), len(stored), time.time() - t))
    try:
        outputs = formats.write(stock, stored, dev, version,
                                stored_sharc=linked.sharc_stored if linked else None)
        facts = formats.verify(outputs, stock, img, dev, version,
                               want_sharc=linked.sharc_stream if linked else None)
    except formats.FormatError as e:
        raise PatchError('the output failed verification: %s' % e)
    gap = facts['main'].get('inplace_min_gap')
    log('verified: %s; the main OS depacks to the patched image%s; flash ends %s (%d bytes spare)'
        % ('; '.join(facts['untouched']),
           ' in place (min gap %d bytes)' % gap if gap is not None else '',
           facts['flash_end'], facts['flash_headroom']))
    man['version'] = version
    man['output'] = facts
    return outputs, man


def companion(path, kind):
    """The path of a second output next to `path` (the .bin beside a .syx)."""
    root, ext = os.path.splitext(path)
    return (root if ext.lower() == '.syx' else path) + '.' + kind


def save(outputs, man, path):
    """Write the .syx at `path` (and the .bin beside it), and the manifest
    (and symbol map) next to it. -> the paths written."""
    written = []
    for kind, data in outputs.items():
        p = path if kind == 'syx' else companion(path, kind)
        tmp = p + '.part'
        with open(tmp, 'wb') as fh:
            fh.write(data)
        os.replace(tmp, p)
        written.append(p)
    man['output']['path'] = os.path.abspath(path)
    if 'bin' in outputs:
        man['output']['bin_path'] = os.path.abspath(companion(path, 'bin'))
    smap = man.pop('_map', None)
    if smap is not None:
        with open(path + '.map.json', 'w') as fh:
            json.dump({k: '0x%08x' % v for k, v in sorted(smap.items())}, fh, indent=0)
        man['map'] = os.path.basename(path) + '.map.json'
    with open(path + '.json', 'w') as fh:
        json.dump(man, fh, indent=1)
    return written


def lock_from_build(path):
    """An earlier build's OS file -> the mod-storage lockfile it carries, or None."""
    with open(path, 'rb') as fh:
        raw = fh.read()
    for dev in devices.devices():
        if not store.supports(dev.key):
            continue
        try:
            image = formats.main_image(formats.parse(raw, dev), dev)
        except Exception:                      # not this family's file
            continue
        lock = store.from_image(image)
        if lock is not None:
            return lock
    return None


def default_lockfile(stock_path, out):
    """The lockfile a build keeps when --lockfile is not given: DEVICE.lock.json
    in the output's folder; None when the stock file can't be read (the build
    then says why)."""
    try:
        _, dev, _ = formats.load(stock_path)
    except (OSError, devices.UnknownFirmware, formats.FormatError):
        return None
    return os.path.join(os.path.dirname(os.path.abspath(out)), dev.key + '.lock.json')


def main(argv=None):
    ap = argparse.ArgumentParser(prog='elekloader.patch', description=__doc__.split('\n')[0])
    ap.add_argument('--stock', required=True, help='your stock OS file (.syx, or the '
                                                   'Octatrack\'s .bin)')
    ap.add_argument('--mod', action='append', default=[], help='a .elemod (repeat for more)')
    ap.add_argument('--out', help='the .syx to write (a .bin is written beside it for devices '
                                  'that have a card file)')
    ap.add_argument('--version', help='what the unit shows as its OS version: 4 characters on '
                                      'a Digitakt, up to 10 on an Octatrack (default: the mod\'s)')
    ap.add_argument('--check', action='store_true', help='check the mods combine; write nothing')
    ap.add_argument('--lockfile', help='the mod-storage lockfile, read and written back '
                                       '(default: DEVICE.lock.json in the output\'s folder)')
    ap.add_argument('--lockfile-from', metavar='OLD', help='when the lockfile does not exist: '
                    'take it from this earlier build (.syx)')
    a = ap.parse_args(argv)
    if not a.check and not a.out:
        ap.error('--out is required (or --check)')
    if a.out and os.path.exists(a.stock) and os.path.exists(a.out) \
            and os.path.samefile(a.stock, a.out):
        ap.error('--out is the stock file')
    lockpath = a.lockfile or (default_lockfile(a.stock, a.out) if a.out else None)
    readpath = lockpath
    if not a.lockfile and lockpath and not os.path.exists(lockpath) \
            and os.path.exists(a.out + '.lock.json'):
        readpath = a.out + '.lock.json'           # the default before DEVICE.lock.json
    lock = None
    if readpath and os.path.exists(readpath):
        if a.lockfile_from:
            print('IGNORED --lockfile-from %s: the lockfile %s exists (move it away to take the '
                  'build\'s)' % (a.lockfile_from, readpath))
        try:
            with open(readpath) as fh:
                lock = json.load(fh)
        except (OSError, ValueError) as e:
            print('REFUSED: cannot read the lockfile %s: %s' % (readpath, e))
            return 1
        if readpath != lockpath:
            print('READ the lockfile %s; it is kept as %s from now on' % (readpath, lockpath))
    elif a.lockfile_from:
        try:
            lock = lock_from_build(a.lockfile_from)
        except (OSError, elemod.ModError, formats.FormatError) as e:
            print('REFUSED: cannot take the lockfile from %s: %s' % (a.lockfile_from, e))
            return 1
        if lock is None:
            print('REFUSED: %s carries no lockfile (built without mod storage, or before '
                  'elekloader kept one in the build)' % a.lockfile_from)
            return 1
        print('READ the lockfile (layout ID %s) from %s' % (lock['id'], a.lockfile_from))
    try:
        outputs, man = build(a.stock, a.mod, a.version, a.check, lock=lock)
    except PatchError as e:
        print('REFUSED: %s' % e)
        return 1
    if a.check:
        print('OK: the mods combine (nothing written)')
        return 0
    for p in save(outputs, man, a.out):
        print('WROTE %s' % p)
    if man.get('store') is not None:
        with open(lockpath + '.part', 'w') as fh:
            fh.write(store.dumps(man['store']))
        os.replace(lockpath + '.part', lockpath)
        print('WROTE %s (keep it for your next build)' % lockpath)
    print('  sha256 %s, version %s, %d bytes' % (man['output']['sha256'], man['version'],
                                                 man['output']['bytes']))
    return 0


if __name__ == '__main__':
    sys.exit(main())
