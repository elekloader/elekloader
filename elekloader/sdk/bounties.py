# SPDX-License-Identifier: GPL-2.0-or-later
"""The community's to-do list for the core (docs/CORE-CONTRIBUTING.md).

    python -m elekloader.sdk.bounties MODS... [--stock STOCK.syx ...] [--upstream LIST.json ...] [--json]

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
- a site is "covered" when the core or a companion (machine-pages,
  machines, seq-events, trig-events, dspbus, intercepts) hooks the same bytes: the mod can move onto
  what exists. An entry is "migration" when all its sites are covered,
  "core" when none is (the core needs an event there) and "partly" when
  some are;
- an entry's `near` lists the core's and companions' sites in its routine
  (by the rule above) that overlap none of its sites, each with the
  symbol it jumps to: the core hooks that routine beside the mods' bytes
  (core-ot does so on purpose, so that a shop file holding those bytes
  still links), and its event there may already serve them. It changes
  no kind;
- an entry's id is the device and its lowest site on the oldest OS that has
  one. It stays the same while the mods there do not change.

An upstream list (--upstream: web/upstream/octabam.json, which
`sdk.octabam --sites` writes from an octabam checkout) adds the stock code
the modules of a newer upstream hook, as no file here has them yet:

- a module the files have from that commit (its version is the commit, or
  starts with it and a dash) is left out: the file says what it hooks;
- a site a file of the same mod has (address, length and label) is left
  out: it is that hook, and the file lists it;
- every other site joins the entry of the files' sites it is in the same
  routine with (by the rule above), so no entry of the files changes its
  id; the rest are entries of their own, `upstream_only`, named by their
  lowest site;
- upstream sites carry `from` (the project and commit), entries the ids of
  their mods with upstream sites (`upstream`); the score counts the files
  only.

A score per device says how many mods hook no stock code of their own.
Nothing is changed or refused. There is no reward but credit: the list is
what the community could do next.
"""
import argparse
import datetime
import json
import os
import sys

from .. import devices, elemod, formats, link
from ..lint import COMPANIONS, is_core  # noqa: F401  (COMPANIONS: what counts as core)
from .coresites import routine_test


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


def load_upstream(paths):
    """-> the upstream lists in `paths` (JSON: {'from', 'commit', 'device', 'os', 'mods': [{'id',
    'version', 'sites': [[addr, len, label], ...], ...}, ...]})."""
    out = []
    for p in paths:
        with open(p, encoding='utf-8') as fh:
            u = json.load(fh)
        for k in ('from', 'commit', 'device', 'os', 'mods'):
            if k not in u:
                raise OSError('%s: an upstream list needs "%s"' % (p, k))
        out.append(u)
    return out


def upstream_sites(lists, mods):
    """-> {(device key, OS): [site, ...]}: the upstream lists' sites the loaded `mods` do not
    have (see the module's doc), in routines()'s form, each with 'from'."""
    have = {}
    for m in mods:
        have.setdefault((m.dev.key, m.rel.version, m.id), []).append(m)
    out = {}
    for u in lists:
        src = '%s@%s' % (u['from'], u['commit'])
        for e in u['mods']:
            ms = have.get((u['device'], u['os'], e['id']), [])
            if is_core(e['id']) or any(m.version == u['commit'] or m.version.startswith(u['commit'] + '-')
                                       for m in ms):
                continue
            taken = {(a, n, lb) for m in ms for a, n, lb in code_sites(m)}
            seen = {}
            for a, n, lb in e['sites']:
                a = int(a, 16) if isinstance(a, str) else a
                if (a, n, lb) in taken:
                    continue
                i = seen[lb] = seen.get(lb, -1) + 1
                out.setdefault((u['device'], u['os']), []).append(
                    {'mod': e['id'], 'version': e.get('version', u['commit']), 'addr': a, 'len': n,
                     'label': lb, 'os': u['os'], 'key': ('upstream', src, e['id'], lb, i), 'from': src})
    return out


def _near(a, b, same, window):
    """Are sites a and b in one routine, as routines() joins them?"""
    lo, hi = sorted((a, b), key=lambda s: s['addr'])
    if hi['addr'] < lo['addr'] + lo['len']:
        return True
    return same((lo['addr'], lo['len']), (hi['addr'], hi['len'])) if same \
        else hi['addr'] - (lo['addr'] + lo['len']) <= window


def beside(sites, cores, same=None, window=64):
    """`sites`: an entry's sites on one OS; `cores`: the core's and companions' there, as
    [(addr, len, mod, label), ...]. -> those of `cores` in one routine with a site of `sites` (as
    _near decides) that overlap none of them."""
    reach = window if same is None else getattr(same, 'reach', None)
    out = []
    for a, n, mod, label in cores:
        if any(a < s['addr'] + s['len'] and s['addr'] < a + n for s in sites):
            continue
        # a site farther than the test follows a routine (and an instruction more) is in another one
        c = {'addr': a, 'len': n}
        if any(_near(c, s, same, window) for s in sites
               if reach is None or max(s['addr'] - (a + n), a - (s['addr'] + s['len'])) <= reach + 32):
            out.append((a, n, mod, label))
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


def bounties(mods, tests=None, window=64, upstream=()):
    """`mods`: loaded mods (see newest). `tests`: {(device name, OS): routine_test}.
    `upstream`: upstream lists (load_upstream).
    -> [{'device', 'oses', 'score': {...}, 'entries': [...]}, ...], one per device."""
    tests = tests or {}
    by_dev = {}
    for m in mods:
        by_dev.setdefault(m.dev.key, []).append(m)
    ups = upstream_sites(upstream, mods)
    names = {d.key: d.name for d in devices.devices()}
    report = []
    for key in sorted(set(by_dev) | {k for k, _o in ups}):
        ms = by_dev.get(key, [])
        name = ms[0].dev.name if ms else names.get(key, key)
        oses = sorted({m.rel.version for m in ms} | {o for k, o in ups if k == key}, key=elemod.version_key)
        # every mod's sites, and each later OS's site paired with the oldest OS's
        own, covered = {o: [] for o in oses}, {o: [] for o in oses}
        theirs = {o: ups.get((key, o), []) for o in oses}
        nth = {s['key']: {s['os']: s} for o in oses for s in theirs[o]}
        for m in ms:
            seen = {}
            for addr, n, label in code_sites(m):
                i = seen[label] = seen.get(label, -1) + 1
                if is_core(m.id):
                    covered[m.rel.version].append((addr, n, m.id, label))
                else:
                    s = {'mod': m.id, 'version': m.version, 'addr': addr, 'len': n, 'label': label,
                         'os': m.rel.version, 'key': (m.id, label, i)}
                    own[m.rel.version].append(s)
                    nth.setdefault(s['key'], {})[m.rel.version] = s
        entries, placed = [], set()
        for o in oses:
            same = tests.get((name, o))
            groups = routines([s for s in own[o] if id(s) not in placed], same, window)
            # an upstream site joins the files' routine it is in, so their entries keep their ids
            left = []
            for u in sorted(theirs[o], key=lambda s: (s['addr'], s['mod'])):
                g = next((g for g in groups if any(_near(u, s, same, window) for s in g)), None)
                (g if g is not None else left).append(u)
            for g in groups + routines(left, same, window):
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
                where, cov, near = {}, set(), []
                for oo in oses:
                    ss = sorted(at.get(oo, []), key=lambda s: s['addr'])
                    if not ss:
                        continue
                    sites = []
                    for s in ss:
                        c = any(a < s['addr'] + s['len'] and s['addr'] < a + n for a, n, _m, _l in covered[oo])
                        cov.add(c)
                        sites.append({'mod': s['mod'], 'version': s['version'], 'site': '0x%08x' % s['addr'],
                                      'len': s['len'], 'label': s['label'], 'covered': c})
                        if 'from' in s:
                            sites[-1]['from'] = s['from']
                    where[oo] = {'lo': '0x%08x' % ss[0]['addr'],
                                 'hi': '0x%08x' % max(s['addr'] + s['len'] for s in ss), 'sites': sites}
                    # the core's sites in this routine on other bytes: what it may offer here already
                    near += [{'os': oo, 'site': '0x%08x' % a, 'len': n, 'mod': mod, 'target': label}
                             for a, n, mod, label in sorted(beside(ss, covered[oo], tests.get((name, oo)), window))]
                # an OS a mod here has no address for
                missing = sorted({oo for s in g for oo in oses
                                  if oo not in nth[s['key']] and any(
                                      m.id == s['mod'] for m in ms if m.rel.version == oo)}
                                 | {oo for oo in oses if oo not in where},
                                 key=elemod.version_key)
                # named by the files' lowest site; by the upstream's where no file hooks it
                mine = {oo: [s for s in at.get(oo, []) if 'from' not in s] for oo in oses}
                named = mine if any(mine.values()) else at
                first = next(oo for oo in oses if named.get(oo))
                e = {'id': '%s@%s:0x%08x' % (key, first, min(s['addr'] for s in named[first])),
                     'kind': 'partly' if len(cov) > 1 else 'migration' if True in cov else 'core',
                     'mods': mods_here, 'sites': len(g), 'at': where,
                     'missing_os': missing, 'near': near}
                up = sorted({s['mod'] for s in g if 'from' in s})
                if up:
                    e['upstream'] = up
                    if named is at:
                        e['upstream_only'] = True
                entries.append(e)
        entries.sort(key=lambda e: (-len(e['mods']), e['kind'] == 'migration', e['id']))
        ids, hooks = set(), set()
        for m in ms:
            if not is_core(m.id):
                ids.add(m.id)
                # a whole build (format 1) is all its own code
                if not isinstance(m, link.Mod2) or code_sites(m):
                    hooks.add(m.id)
        pure = sorted(ids - hooks)
        d = {'device': name, 'oses': oses,
             'score': {'mods': len(ids), 'no_own_code': len(pure), 'pure': pure},
             'routines': len(entries),
             'shared': sum(1 for e in entries if len(e['mods']) > 1),
             'entries': entries}
        lists = [u for u in upstream if u['device'] == key]
        if lists:
            got = {s['mod'] for o in oses for s in theirs[o]}
            d['upstream'] = {'from': sorted({'%s@%s' % (u['from'], u['commit']) for u in lists}),
                             'mods': len({e['id'] for u in lists for e in u['mods']}),
                             'hooking': len(got),
                             'not_here': len(got - ids),
                             'entries': sum(1 for e in entries if e.get('upstream')),
                             'only': sum(1 for e in entries if e.get('upstream_only'))}
        report.append(d)
    return report


TODO = {'core': 'a core event', 'partly': 'some sites move onto the core, the rest need an event',
        'migration': 'move onto the core'}
START, END = '<!-- bounties: generated by elekloader.sdk.bounties -->', '<!-- /bounties -->'


def hooks(e, most=3):
    """An entry's sites in a few words: each mod with the symbols it jumps to (an upstream
    list's sites apart, marked "(upstream)")."""
    by = {}
    for w in e['at'].values():
        for s in w['sites']:
            k = s['mod'] + (' (upstream)' if 'from' in s else '')
            by.setdefault(k, [])
            for x in s['label'].split(','):
                if x not in by[k]:
                    by[k].append(x)
    return '; '.join('%s: %s%s' % (m, ', '.join(xs[:most]), ' +%d' % (len(xs) - most) if len(xs) > most else '')
                     for m, xs in sorted(by.items()))


def beside_text(e, most=3):
    """An entry's `near` in a few words, on the oldest OS it has one for: "core hooks this routine
    at 0x40010bd0 (core_midi_out)", each site with the symbols it jumps to ('' when there is none)."""
    if not e.get('near'):
        return ''
    os_ = e['near'][0]['os']
    by = {}
    for x in e['near']:
        if x['os'] == os_:
            by.setdefault(x['mod'], []).append(x['site'] + ('' if x['target'] == '?' else
                                                            ' (%s)' % x['target'].replace(',', ', ')))
    parts = ['%s %s %s%s' % (m, 'at' if i else 'hooks this routine at', ', '.join(xs[:most]),
                             ' +%d' % (len(xs) - most) if len(xs) > most else '')
             for i, (m, xs) in enumerate(by.items())]
    named = e['id'].split('@', 1)[1].rsplit(':', 1)[0]       # the OS the entry's id names
    return '; '.join(parts) + ('' if os_ == named else ' (on %s)' % os_)


def markdown(report, how=''):
    """The report as Markdown: per device a score and a table, core entries first."""
    out = [START, '']
    if how:
        out += [how, '']
    for d in report:
        sc = d['score']
        if not sc['mods'] and not d['entries']:
            continue                        # only a core here: nothing to do (the web page skips it too)
        up = d.get('upstream')
        out += ['### %s (OS %s)' % (d['device'], ', '.join(d['oses'])), '',
                '%d of %d mods hook no stock code of their own. %d routines to do, %d of them '
                'hooked by more than one mod.' % (sc['no_own_code'], sc['mods'], d['routines'], d['shared'])
                + (' Upstream (%s), %d mods hook stock code these files do not: %d of them are not '
                   'here at all. Their sites are in %d entries, %d of which only they hook ("upstream '
                   'only").' % (', '.join(up['from']), up['hooking'], up['not_here'], up['entries'], up['only'])
                   if up else ''),
                '']
        if not d['entries']:
            continue
        out += ['| entry | to do | mods | what they hook there |', '|---|---|---|---|']
        for e in sorted(d['entries'], key=lambda e: (e['kind'] == 'migration', -len(e['mods']), e['id'])):
            todo = TODO[e['kind']]
            if e['missing_os']:
                todo += ' (no address on %s)' % ', '.join(e['missing_os'])
            if e.get('upstream_only'):
                todo += ' (upstream only)'
            if e.get('near'):
                todo += '; ' + beside_text(e)
            out.append('| `%s` | %s | %d | %s |' % (e['id'], todo, len(e['mods']),
                                                   hooks(e).replace('|', '\\|')))
        out.append('')
    return '\n'.join(out + [END]) + '\n'


def update(path, text):
    """Put `text` between the markers in the file at `path`."""
    with open(path, encoding='utf-8') as fh:
        doc = fh.read()
    a, b = doc.find(START), doc.find(END)
    if a < 0 or b < a:
        raise OSError('%s has no %s ... %s to replace' % (path, START, END))
    with open(path, 'w', encoding='utf-8', newline='\n') as fh:
        fh.write(doc[:a] + text.rstrip('\n') + doc[b + len(END):])


def main(argv=None):
    ap = argparse.ArgumentParser(prog='elekloader.sdk.bounties', description=__doc__.split('\n')[0])
    ap.add_argument('mods', nargs='+', help='.elemod files, or folders of them')
    ap.add_argument('--stock', action='append', default=[],
                    help='a stock file (repeatable): its OS is grouped routine by routine')
    ap.add_argument('--window', type=int, default=64,
                    help='without a stock file: the most bytes between two sites in one entry')
    ap.add_argument('--reach', type=int, default=1024,
                    help='with one: the most bytes one routine is followed for')
    ap.add_argument('--upstream', action='append', default=[], metavar='LIST.json',
                    help='an upstream list (repeatable): the stock code a newer upstream\'s modules hook, '
                         'which these files do not (web/upstream/octabam.json, from sdk.octabam --sites)')
    ap.add_argument('--json', action='store_true', help='machine-readable output')
    ap.add_argument('--markdown', action='store_true', help='Markdown tables')
    ap.add_argument('--update', metavar='FILE.md',
                    help='write the Markdown between the bounties markers in FILE.md')
    a = ap.parse_args(argv)
    tests = {}
    for p in a.stock:
        st, dev, rel = formats.load(p)
        tests[(dev.name, rel.version)] = routine_test(formats.main_image(st, dev), dev, a.reach)
    report = bounties(newest(a.mods), tests, a.window, load_upstream(a.upstream))
    if a.json:
        print(json.dumps(report, indent=1))
        return 0
    if a.markdown or a.update:
        how = 'Made on %s; grouped %s.' % (datetime.date.today().isoformat(), 'by routine, with the stock OS files'
                                          if tests else 'by distance (at most %d bytes apart)' % a.window)
        md = markdown(report, how)
        if a.update:
            update(a.update, md)
        else:
            sys.stdout.write(md)
        return 0
    for d in report:
        sc = d['score']
        print('%s (%s): %d of %d mods hook no stock code of their own; %d routines to do, %d shared'
              % (d['device'], ', '.join(d['oses']), sc['no_own_code'], sc['mods'],
                 d['routines'], d['shared']))
        if d.get('upstream'):
            u = d['upstream']
            print('  upstream %s: %d mods hook stock code these files do not (%d not here); %d entries, '
                  '%d upstream only; their sites are marked ^' % (', '.join(u['from']), u['hooking'],
                                                                  u['not_here'], u['entries'], u['only']))
        for e in d['entries']:
            labels = sorted({'%s:%s%s' % (s['mod'], s['label'], '^' if 'from' in s else '')
                             for w in e['at'].values() for s in w['sites']})
            print('  %-34s %-9s %d mods, %2d sites%s  %s' % (
                e['id'], e['kind'], len(e['mods']), e['sites'],
                ' (no address on %s)' % ', '.join(e['missing_os']) if e['missing_os'] else '',
                ' '.join(labels)))
            if e['near']:
                print('  %-34s beside: %s' % ('', beside_text(e)))
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, elemod.ModError, formats.FormatError, devices.UnknownFirmware) as e:
        sys.exit('bounties: %s' % e)
