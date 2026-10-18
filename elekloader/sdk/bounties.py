# SPDX-License-Identifier: GPL-2.0-or-later
"""The community's to-do list for the core (docs/CORE-CONTRIBUTING.md).

    python -m elekloader.sdk.bounties MODS... [--stock STOCK.syx ...] [--json]

MODS are .elemod files or folders of them: the shop's mods, say, and the
cores and companions too when you have them. Of each mod (by id, for each
device and OS) only the newest version counts. Every code site a mod hooks
itself (one `lint` notes as a core candidate) is a piece of stock code no
other mod can hook; this lists them all, one entry per stock routine, so
that each can become a core event:

- sites are grouped by routine: with that OS's stock file given, sites the
  stock code runs between with no `rts`, `rte` or new `link` (as
  `coresites` decides); without it, sites at most --window bytes apart;
- each site is named by the symbols it jumps to, from the mod's relocs;
- the same routine on each OS of a device is one entry: a mod's site on a
  later OS is paired with its site on the oldest by the symbols it jumps
  to, and an entry says which OS it has no address for;
- an entry is "migration" when the core or a companion (machine-pages,
  machines, seq-events, dspbus) already hooks that routine: the mods there
  can move onto what exists. Otherwise it is "core": the core needs an
  event for it;
- an entry's id is the device and its lowest site on the oldest OS that has
  one. It stays the same while the mods there do not change.

A score per device says how many mods hook no stock code of their own.
Nothing is changed or refused. There is no reward but credit: the list is
what the community could do next.
"""
import argparse
import json
import os
import sys

from .. import devices, elemod, formats, link
from .coresites import routine_test

# Mods that own a group of sites once, for every mod: they count as core.
COMPANIONS = frozenset({'machine-pages', 'machines', 'seq-events', 'dspbus'})


def is_core(mod_id):
    return mod_id == 'core' or mod_id in COMPANIONS


def newest(paths):
    """-> the newest version of each mod in `paths`, by (device, OS, id)."""
    files = []
    for p in paths:
        files += elemod.mod_files(p) if os.path.isdir(p) else [p]
    best = {}
    for f in files:
        m = elemod.load_any(f)
        k = (m.dev.key, m.rel.version, m.id)
        if k not in best or elemod.version_key(m.version) > elemod.version_key(best[k].version):
            best[k] = m
    return [best[k] for k in sorted(best)]


def code_sites(m):
    """-> [(addr, len, label), ...]: a format-2 mod's code sites, each named by
    the symbols it jumps to ('?' when it names none)."""
    if not isinstance(m, link.Mod2):
        return []
    out = []
    for s in m.sites:
        if s['kind'] != 'code':
            continue
        syms = [r[2].split(':', 1)[1] for r in s.get('relocs', ()) if str(r[2]).startswith('sym:')]
        out.append((s['addr'], s['len'], ','.join(syms) or '?'))
    return out


def routines(sites, same=None, window=64):
    """`sites`: [{'addr', 'len', ...}, ...] of one OS. -> lists of them, one per
    routine: sorted by address, each site joined to the one before it when
    they overlap, or (with `same`, a routine_test) the stock code runs from
    one to the other, or (without) they are at most `window` bytes apart."""
    out = []
    for s in sorted(sites, key=lambda s: (s['addr'], s['mod'])):
        if out:
            prev = max(out[-1], key=lambda p: p['addr'] + p['len'])
            end = prev['addr'] + prev['len']
            if s['addr'] < end or (same((prev['addr'], prev['len']), (s['addr'], s['len'])) if same
                                   else s['addr'] - end <= window):
                out[-1].append(s)
                continue
        out.append([s])
    return out


def touches(group, addr, n, same=None, window=64):
    """Whether a site (addr, n) is in the routine of `group`."""
    for s in group:
        lo, hi = sorted([(s['addr'], s['len']), (addr, n)])
        if hi[0] < lo[0] + lo[1]:
            return True
        if same(lo, hi) if same else hi[0] - (lo[0] + lo[1]) <= window:
            return True
    return False


def bounties(mods, tests=None, window=64):
    """`mods`: loaded mods (see newest). `tests`: {(device name, OS): routine_test}.
    -> [{'device', 'oses', 'score': {...}, 'entries': [...]}, ...], one per device."""
    tests = tests or {}
    by_dev = {}
    for m in mods:
        by_dev.setdefault(m.dev.key, []).append(m)
    report = []
    for key, ms in sorted(by_dev.items()):
        name = ms[0].dev.name
        oses = sorted({m.rel.version for m in ms}, key=elemod.version_key)
        # every mod's sites, and each later OS's site paired with the oldest OS's
        own, covered = {o: [] for o in oses}, {o: [] for o in oses}
        nth = {}
        for m in ms:
            seen = {}
            for addr, n, label in code_sites(m):
                i = seen[label] = seen.get(label, -1) + 1
                if is_core(m.id):
                    covered[m.rel.version].append((addr, n))
                else:
                    s = {'mod': m.id, 'version': m.version, 'addr': addr, 'len': n, 'label': label,
                         'os': m.rel.version, 'key': (m.id, label, i)}
                    own[m.rel.version].append(s)
                    nth.setdefault(s['key'], {})[m.rel.version] = s
        entries, placed = [], set()
        for o in oses:
            same = tests.get((name, o))
            for g in routines([s for s in own[o] if id(s) not in placed], same, window):
                # this routine's sites on every OS: each site's twins, by mod and symbol
                at = {}
                for s in g:
                    for t in nth[s['key']].values():
                        if id(t) not in placed:
                            placed.add(id(t))
                            at.setdefault(t['os'], []).append(t)
                for t in g:
                    placed.add(id(t))
                mods_here = sorted({s['mod'] for s in g})
                where = {}
                migration = False
                for oo in oses:
                    ss = sorted(at.get(oo, []), key=lambda s: s['addr'])
                    if not ss:
                        continue
                    where[oo] = {'lo': '0x%08x' % ss[0]['addr'],
                                 'hi': '0x%08x' % max(s['addr'] + s['len'] for s in ss),
                                 'sites': [{'mod': s['mod'], 'version': s['version'],
                                            'site': '0x%08x' % s['addr'], 'len': s['len'],
                                            'label': s['label']} for s in ss]}
                    t = tests.get((name, oo))
                    if any(touches(ss, a, n, t, window) for a, n in covered[oo]):
                        migration = True
                # an OS a mod here has no address for
                missing = sorted({oo for s in g for oo in oses
                                  if oo not in nth[s['key']] and any(
                                      m.id == s['mod'] for m in ms if m.rel.version == oo)}
                                 | {oo for oo in oses if oo not in where},
                                 key=elemod.version_key)
                first = next(oo for oo in oses if oo in where)
                entries.append({'id': '%s@%s:%s' % (key, first, where[first]['lo']),
                                'kind': 'migration' if migration else 'core',
                                'mods': mods_here, 'sites': len(g), 'at': where,
                                'missing_os': missing})
        entries.sort(key=lambda e: (-len(e['mods']), e['kind'] != 'core', e['id']))
        ids, hooks = set(), set()
        for m in ms:
            if not is_core(m.id):
                ids.add(m.id)
                # a whole build (format 1) is all its own code
                if not isinstance(m, link.Mod2) or code_sites(m):
                    hooks.add(m.id)
        pure = sorted(ids - hooks)
        report.append({'device': name, 'oses': oses,
                       'score': {'mods': len(ids), 'no_own_code': len(pure), 'pure': pure},
                       'routines': len(entries),
                       'shared': sum(1 for e in entries if len(e['mods']) > 1),
                       'entries': entries})
    return report


def main(argv=None):
    ap = argparse.ArgumentParser(prog='elekloader.sdk.bounties', description=__doc__.split('\n')[0])
    ap.add_argument('mods', nargs='+', help='.elemod files, or folders of them')
    ap.add_argument('--stock', action='append', default=[],
                    help='a stock file (repeatable): its OS is grouped routine by routine')
    ap.add_argument('--window', type=int, default=64,
                    help='without a stock file: the most bytes between two sites in one entry')
    ap.add_argument('--reach', type=int, default=1024,
                    help='with one: the most bytes one routine is followed for')
    ap.add_argument('--json', action='store_true', help='machine-readable output')
    a = ap.parse_args(argv)
    tests = {}
    for p in a.stock:
        st, dev, rel = formats.load(p)
        tests[(dev.name, rel.version)] = routine_test(formats.main_image(st, dev), dev, a.reach)
    report = bounties(newest(a.mods), tests, a.window)
    if a.json:
        print(json.dumps(report, indent=1))
        return 0
    for d in report:
        sc = d['score']
        print('%s (%s): %d of %d mods hook no stock code of their own; %d routines to do, %d shared'
              % (d['device'], ', '.join(d['oses']), sc['no_own_code'], sc['mods'],
                 d['routines'], d['shared']))
        for e in d['entries']:
            labels = sorted({'%s:%s' % (s['mod'], s['label'])
                             for w in e['at'].values() for s in w['sites']})
            print('  %-34s %-9s %d mods, %2d sites%s  %s' % (
                e['id'], e['kind'], len(e['mods']), e['sites'],
                ' (no address on %s)' % ', '.join(e['missing_os']) if e['missing_os'] else '',
                ' '.join(labels)))
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, elemod.ModError, formats.FormatError, devices.UnknownFirmware) as e:
        sys.exit('bounties: %s' % e)
