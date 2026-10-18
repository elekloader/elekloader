#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Assemble the web patcher (web/) into a static site, for GitHub Pages.

    python packaging/build_web.py --pyodide-url          # the pinned Pyodide's URL
    python packaging/build_web.py --pyodide pyodide-core-314.0.7.tar.bz2 \\
        --core core-2.1.elemod [core-dn1-2.0a.elemod ...] \\
        [--catalog web/catalog.json --catalog-dir build/shop] --out build/site
    python packaging/build_web.py --catalog-list web/catalog.json   # what to download

The site holds:
  - web/'s files (the page, its worker and the Python bridge);
  - elekloader.zip: the elekloader package exactly as the commit has it
    (`git archive HEAD elekloader`: committed files only, nothing changed),
    zipped the same way every time, so its sha256 follows from the commit:
    the same code a release of that commit carries;
  - core/: the core mods given (the release's), and the cores the catalog
    lists, with core/index.json;
  - shop/: the mod shop: each mod web/catalog.json lists, taken from its
    author's release or from their repository at a commit (the workflow
    downloads them into --catalog-dir), checked against the sha256 the
    catalog pins, and listed in shop/index.json with what its file says
    about it. A mod goes in only under a licence that allows passing it on
    (SHOP_LICENCES); one whose file is not there (a draft release) is listed
    as not available. shop/bounties.json is the community's to-do list for
    the core, made from those files and the cores (elekloader.sdk.bounties,
    grouped by distance: the site has no stock file);
  - devices.json: the devices the engine knows, for the page before it loads;
  - shop/claims.json: the leaderboard, the to-do list's entries claimed by
    merged pull requests (--prs: gh pr list --state merged --json ...);
  - pyodide/: the pinned Pyodide's runtime, five files from its core
    tarball, which is checked against its sha256 first;
  - LICENSE.txt, NOTICE.txt, pyodide/NOTICE.txt, and build.json (what went in).

index.html names app.js and style.css with the site's version (?v=, a hash of
every other file), and the page and its worker pass it on to every file they
load, so a browser's cache never mixes two deploys.

Everything the page loads is in the site: it fetches nothing from anywhere
else. No firmware goes in. The stock OS file is only ever read in the
user's browser, each core is checked to be a core mod, and each of the
shop's files to be the one the catalog names.
"""
import argparse
import datetime
import hashlib
import io
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)

from elekloader import devices, elemod, link  # noqa: E402
from elekloader.sdk import bounties  # noqa: E402

PYODIDE_VERSION = '314.0.7'
PYODIDE_SHA256 = '2abdcc2e35208af406e07724cffa85bc582ced97e9028383ecf5462541393f95'
PYODIDE_URL = ('https://github.com/pyodide/pyodide/releases/download/%s/pyodide-core-%s.tar.bz2'
               % (PYODIDE_VERSION, PYODIDE_VERSION))
# what the page needs: the loader, the runtime, the standard library, the lock file
PYODIDE_FILES = ('pyodide.mjs', 'pyodide.asm.mjs', 'pyodide.asm.wasm', 'python_stdlib.zip',
                 'pyodide-lock.json')
WEB_FILES = ('index.html', 'style.css', 'app.js', 'worker.js', 'bridge.py')
# licences under which the site may pass a mod on (SPDX identifiers)
SHOP_LICENCES = {'GPL-2.0', 'GPL-2.0-only', 'GPL-2.0-or-later', 'GPL-3.0', 'GPL-3.0-only',
                 'GPL-3.0-or-later', 'LGPL-2.1-or-later', 'LGPL-3.0-or-later', 'MIT',
                 'BSD-2-Clause', 'BSD-3-Clause', 'Apache-2.0', 'MPL-2.0', 'ISC', '0BSD',
                 'CC0-1.0', 'Unlicense'}
PAGES_FILE_MAX = 100 << 20           # GitHub Pages: 100 MB a file, 1 GB a site
PAGES_SITE_MAX = 1 << 30

PYODIDE_NOTICE = """\
The build engine of this page is Pyodide %(v)s: CPython compiled to
WebAssembly, served from this site (these files are unchanged from
pyodide-core-%(v)s.tar.bz2, sha256 %(sha)s).

Pyodide: Mozilla Public License 2.0.
  Source: https://github.com/pyodide/pyodide/tree/%(v)s
  Licence: https://github.com/pyodide/pyodide/blob/%(v)s/LICENSE
CPython and its standard library (python_stdlib.zip, and the interpreter
in pyodide.asm.wasm): Python Software Foundation License Version 2.
  https://docs.python.org/3/license.html
The other libraries built into pyodide.asm.wasm (such as zlib, libffi
and the Emscripten runtime) are under their own permissive licences,
listed with Pyodide's source.
"""


def sha(b):
    return hashlib.sha256(b).hexdigest()


def git(*args):
    return subprocess.run(['git', '-C', ROOT] + list(args), check=True,
                          stdout=subprocess.PIPE).stdout


def package_zip():
    """The elekloader package as HEAD has it, zipped reproducibly: the blobs as
    committed (no line-ending conversion), sorted, one fixed date, made "on
    Unix" whatever the OS, stored uncompressed (so no zlib version changes a
    byte). -> (bytes, names)."""
    tar = tarfile.open(fileobj=io.BytesIO(
        git('-c', 'core.autocrlf=false', 'archive', '--format=tar', 'HEAD', 'elekloader')))
    files = sorted((m.name, tar.extractfile(m).read()) for m in tar.getmembers() if m.isfile())
    out = io.BytesIO()
    with zipfile.ZipFile(out, 'w', zipfile.ZIP_STORED) as z:
        for name, data in files:
            info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            info.external_attr = 0o644 << 16
            info.create_system = 3          # zipfile writes the running OS's (0 on Windows)
            z.writestr(info, data)
    return out.getvalue(), [n for n, _ in files]


def pyodide_files(tarball):
    with open(tarball, 'rb') as fh:
        raw = fh.read()
    if sha(raw) != PYODIDE_SHA256:
        sys.exit('%s: sha256 %s, not the pinned pyodide-core-%s (%s)'
                 % (tarball, sha(raw), PYODIDE_VERSION, PYODIDE_SHA256))
    out = {}
    with tarfile.open(fileobj=io.BytesIO(raw), mode='r:bz2') as tar:
        for m in tar.getmembers():
            name = m.name.split('/', 1)[-1]
            if m.isfile() and name in PYODIDE_FILES:
                out[name] = tar.extractfile(m).read()
    missing = set(PYODIDE_FILES) - set(out)
    if missing:
        sys.exit('%s lacks %s' % (tarball, ', '.join(sorted(missing))))
    return out


def cores(paths):
    out = []
    for p in sorted(paths, key=os.path.basename):
        m = elemod.load_any(p)
        if m.id != 'core' or not isinstance(m, link.Mod2):
            sys.exit('%s is not a core mod (id %r)' % (p, m.id))
        with open(p, 'rb') as fh:
            raw = fh.read()
        out.append(({'file': os.path.basename(p), 'sha256': sha(raw), 'id': m.id,
                     'version': m.version, 'device': m.dev.key, 'os': m.rel.version}, raw))
    return out


def read_catalog(path):
    """The catalog's items. Each names its file one of two ways: an asset of a
    GitHub release (`tag`, `file`), or a file in the repository at a commit
    (`commit`, `path`), whose `file` is then the path's last part."""
    with open(path, encoding='utf-8') as fh:
        doc = json.load(fh)
    names = set()
    for it in doc['items']:
        at = 'commit' in it
        for k in ('repo', 'sha256', 'device') + (('commit', 'path') if at else ('tag', 'file')):
            if not it.get(k):
                sys.exit('%s: an item has no "%s"' % (path, k))
        if at:
            if 'file' in it or 'tag' in it:
                sys.exit("%s: %s: a commit's file is named by its path alone" % (path, it['path']))
            if (not re.fullmatch('[0-9a-f]{40}', it['commit'])
                    or not re.fullmatch(r'[\w.-]+(/[\w.-]+)*', it['path']) or '..' in it['path'].split('/')):
                sys.exit('%s: %s at %s: the whole commit id, and a plain path in the repository'
                         % (path, it['path'], it['commit']))
            it['file'] = it['path'].rsplit('/', 1)[-1]
        if it['file'] in names:              # they are all downloaded into one folder
            sys.exit('%s: two items named %s' % (path, it['file']))
        names.add(it['file'])
    return doc['items']


def source(it):
    """Where a catalog item's file comes from, in a few words."""
    return it['repo'] + ' ' + (it['commit'][:7] if it.get('commit') else it['tag'])


def shop(path, folder):
    """The catalog's items, from `folder`. -> (cores [(entry, bytes)],
    shop [(entry, bytes or None)])."""
    cs, out = [], []
    for it in read_catalog(path):
        f = it['file']
        base = {'file': f, 'sha256': it['sha256'], 'device': it['device'], 'repo': it['repo'],
                'tag': it.get('tag'), 'commit': it.get('commit'),
                'homepage': 'https://github.com/' + it['repo'],
                'release_url': ('https://github.com/%s/blob/%s/%s' % (it['repo'], it['commit'], it['path'])
                                if 'commit' in it else
                                'https://github.com/%s/releases/tag/%s' % (it['repo'], it['tag'])),
                'summary': it.get('summary', ''), 'needs_core': it.get('needs_core'),
                'on_unit': it.get('on_unit', '')}
        p = os.path.join(folder, f) if folder else ''
        if not p or not os.path.exists(p):
            print('WARNING: %s (%s) is not there: the shop lists it as not available'
                  % (f, source(it)))
            if it.get('kind') != 'core':
                out.append((dict(base, available=False, title=it.get('title', f),
                                 version=it.get('version', '')), None))
            continue
        with open(p, 'rb') as fh:
            raw = fh.read()
        if sha(raw) != it['sha256']:
            sys.exit('%s: sha256 %s, not the %s the catalog names' % (f, sha(raw), it['sha256']))
        m = elemod.load_any(p)
        if m.dev.key != it['device']:
            sys.exit("%s is made for %s, not the catalog's %s" % (f, m.dev.key, it['device']))
        lic = m.doc.get('license') or it.get('license')
        if lic not in SHOP_LICENCES:
            sys.exit('%s: licence %r is not one the site may pass a mod on under (%s)'
                     % (f, lic, ', '.join(sorted(SHOP_LICENCES))))
        if it.get('kind') == 'core':
            if m.id != 'core' or not isinstance(m, link.Mod2):
                sys.exit('%s is listed as a core but is %r' % (f, m.id))
            cs.append(({'file': f, 'sha256': it['sha256'], 'id': m.id, 'version': m.version,
                        'device': m.dev.key, 'os': m.rel.version,
                        'from': source(it)}, raw))
            continue
        v2 = isinstance(m, link.Mod2)
        out.append((dict(base, available=True, id=m.id, version=m.version,
                         title=m.doc.get('title', m.id), description=m.doc.get('description', ''),
                         category=m.doc.get('category', ''), author=m.doc.get('author', ''),
                         license=lic, license_from='mod' if m.doc.get('license') else 'catalog',
                         device_name=m.dev.name, os=m.rel.version, format=2 if v2 else 1,
                         requires=list(m.requires), conflicts=list(m.conflicts),
                         ram=(m.size('.run') + m.size('.bss')) if v2 else 0,
                         fast=m.size('.fast') if v2 else 0, sites=len(m.sites)), raw))
    return cs, out


def device_list():
    """-> devices.json: each device the engine knows, as bridge.info lists them (key, name, releases), so the
    page shows them all before the engine has loaded."""
    return [{'key': d.key, 'name': d.name, 'releases': sorted(d.releases)} for d in devices.devices()]


def stamp(html, token):
    """index.html with its script and stylesheet named as app.js?v=TOKEN and style.css?v=TOKEN: a deploy
    changes the names, so a browser never runs one deploy's page with another's files from its cache (app.js
    and worker.js pass the same ?v= on to every file they load)."""
    out = html
    for a in ('src="app.js"', 'href="style.css"'):
        if out.count(a) != 1:
            sys.exit('web/index.html: expected one %s' % a)
        out = out.replace(a, a[:-1] + '?v=%s"' % token)
    return out


def to_do(core_paths, folder, items, cs):
    """The community's to-do list for the core (elekloader.sdk.bounties) over
    the shop's files and the cores, grouped by distance."""
    paths = [os.path.join(folder, e['file']) for e, raw in items if raw is not None]
    paths += list(core_paths) + [os.path.join(folder, c['file']) for c, _ in cs
                                 if folder and os.path.exists(os.path.join(folder, c['file']))]
    paths = sorted({os.path.basename(p): p for p in paths}.values())     # a core given twice counts once
    mods = bounties.newest(paths)
    return {'made': datetime.datetime.now(datetime.timezone.utc).strftime('%Y-%m-%d'),
            'grouped': 'by distance', 'window': 64, 'files': len(paths),
            'devices': bounties.bounties(mods)}


# an entry of the to-do list, as sdk.bounties names it: device@os:lowest site
ENTRY_ID = re.compile(r'\b([a-z0-9][a-z0-9-]*@[0-9][0-9A-Za-z.]*:0x[0-9a-fA-F]{8})\b')
CLAIMS_LINE = re.compile(r'^[\s>*_-]*claims?\s*:(.*)$', re.I | re.M)


def claims(prs):
    """The leaderboard (docs/CONTRIBUTE.md, "Claiming an entry"), from the repository's merged pull
    requests (`gh pr list --state merged --json number,title,body,author,mergedAt,url`): each entry id on
    a "Claims:" line of a pull request's description is that author's, one point an entry. An entry is
    the first merged claim's; a later claim on it scores nothing. -> {'claims': [...], 'board': [...]}"""
    got = {}
    for pr in sorted((p for p in prs if p.get('mergedAt')), key=lambda p: (p['mergedAt'], p['number'])):
        login = ((pr.get('author') or {}).get('login') or '').strip()
        if not login:
            continue
        for line in CLAIMS_LINE.findall(pr.get('body') or ''):
            for eid in ENTRY_ID.findall(line):
                head, _, addr = eid.rpartition(':')
                eid = '%s:0x%s' % (head, addr[2:].lower())      # the address as sdk.bounties writes it
                if eid not in got:
                    got[eid] = {'id': eid, 'pr': pr['number'], 'url': pr.get('url', ''), 'title': pr.get('title', ''),
                                'login': login, 'merged': pr['mergedAt']}
    board = {}
    for c in got.values():
        b = board.setdefault(c['login'], {'login': c['login'], 'points': 0, 'claims': [], 'first': c['merged']})
        b['points'] += 1
        b['claims'].append({'id': c['id'], 'pr': c['pr'], 'url': c['url']})
        b['first'] = min(b['first'], c['merged'])
    return {'claims': sorted(got.values(), key=lambda c: (c['merged'], c['id'])),
            'board': sorted(board.values(), key=lambda b: (-b['points'], b['first'], b['login']))}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--pyodide-url', action='store_true', help='print the pinned Pyodide\'s URL')
    ap.add_argument('--pyodide', help='pyodide-core-%s.tar.bz2' % PYODIDE_VERSION)
    ap.add_argument('--core', nargs='*', default=[], help='the core mods to list (the release\'s)')
    ap.add_argument('--release', help='the release the cores come from (its tag, fetched): the '
                                      'page says whether its package is that release\'s')
    ap.add_argument('--catalog', help="the mod shop's curated list (web/catalog.json)")
    ap.add_argument('--catalog-dir', help='where its files were downloaded')
    ap.add_argument('--catalog-list', metavar='CATALOG',
                    help='print each item as "release repo tag file" or "commit repo '
                         'commit path", for the download')
    ap.add_argument('--prs', help="the repository's merged pull requests (gh pr list --state merged --json "
                                  "number,title,body,author,mergedAt,url), for the leaderboard")
    ap.add_argument('--out', help='the site folder to write (emptied first)')
    a = ap.parse_args(argv)
    if a.pyodide_url:
        print(PYODIDE_URL)
        return 0
    if a.catalog_list:
        for it in read_catalog(a.catalog_list):
            print(*(('commit', it['repo'], it['commit'], it['path']) if 'commit' in it
                    else ('release', it['repo'], it['tag'], it['file'])))
        return 0
    if not a.pyodide or not a.out:
        ap.error('--pyodide and --out are required')
    py = pyodide_files(a.pyodide)
    zipped, names = package_zip()
    cs = cores(a.core)
    shop_cores, items = shop(a.catalog, a.catalog_dir) if a.catalog else ([], [])
    for c, raw in shop_cores:                # the catalog's cores join the release's
        same_name = [x for x, _ in cs if x['file'] == c['file']]
        if same_name and same_name[0]['sha256'] != c['sha256']:
            sys.exit("two different %s: the release's and %s's" % (c['file'], c['from']))
        if not same_name:
            cs.append((c, raw))
    cs.sort(key=lambda x: x[0]['file'])
    if os.path.exists(a.out):
        shutil.rmtree(a.out)
    site = {}
    for n in WEB_FILES:
        with open(os.path.join(ROOT, 'web', n), 'rb') as fh:
            site[n] = fh.read()
    for n in ('LICENSE', 'NOTICE'):
        with open(os.path.join(ROOT, n), 'rb') as fh:
            site[n + '.txt'] = fh.read()
    site['elekloader.zip'] = zipped
    for n, b in py.items():
        site['pyodide/' + n] = b
    site['pyodide/NOTICE.txt'] = (PYODIDE_NOTICE % {'v': PYODIDE_VERSION, 'sha': PYODIDE_SHA256}
                                  ).encode()
    for c, raw in cs:
        site['core/' + c['file']] = raw
    site['core/index.json'] = json.dumps([c for c, _ in cs], indent=1).encode()
    for e, raw in items:
        if raw is not None:
            site['shop/' + e['file']] = raw
    site['shop/index.json'] = json.dumps([e for e, _ in items], indent=1).encode()
    site['shop/bounties.json'] = json.dumps(to_do(a.core, a.catalog_dir, items, cs), indent=1).encode()
    site['devices.json'] = json.dumps(device_list(), indent=1).encode()
    prs = []
    if a.prs:
        with open(a.prs, encoding='utf-8') as fh:
            prs = json.load(fh)
    site['shop/claims.json'] = json.dumps(claims(prs), indent=1).encode()
    # the site's version: its files but the page, which names it
    token = sha(''.join('%s %s\n' % (n, sha(b)) for n, b in sorted(site.items()) if n != 'index.html')
                .encode())[:12]
    site['index.html'] = stamp(site['index.html'].decode('utf-8'), token).encode('utf-8')
    commit = git('rev-parse', 'HEAD').decode().strip()
    dirty = bool(git('status', '--porcelain', '--', 'elekloader').strip())
    tree = git('rev-parse', 'HEAD:elekloader').decode().strip()
    # the same tree is the same code, file for file
    same = (a.release and git('rev-parse', '%s^{commit}:elekloader' % a.release).decode().strip()
            == tree)
    from elekloader import __version__
    site['build.json'] = json.dumps({
        'elekloader': __version__, 'commit': commit, 'site_version': token,
        'commit_date': git('log', '-1', '--format=%cI').decode().strip(),
        # the zip is HEAD's package; a working tree with changes there is not in it
        'package_changes_not_in_zip': dirty,
        'package_tree': tree, 'release': a.release or None, 'same_as_release': bool(same),
        'zip_sha256': sha(zipped), 'zip_files': len(names),
        'pyodide': PYODIDE_VERSION, 'pyodide_tarball_sha256': PYODIDE_SHA256,
        'cores': [c for c, _ in cs],
        'shop': [{'file': e['file'], 'sha256': e['sha256'], 'available': e['available'],
                  'from': source(e)} for e, _ in items],
        'files': {n: sha(b) for n, b in sorted(site.items())},
    }, indent=1).encode()
    for n, b in site.items():
        p = os.path.join(a.out, *n.split('/'))
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, 'wb') as fh:
            fh.write(b)
    big = [n for n, b in site.items() if len(b) > PAGES_FILE_MAX]
    total = sum(len(b) for b in site.values())
    if big or total > PAGES_SITE_MAX:
        sys.exit('too large for GitHub Pages: %s' % (', '.join(big) or '%d bytes' % total))
    for n in sorted(site, key=lambda n: -len(site[n])):
        print('%10d  %s' % (len(site[n]), n))
    print('%10d  in %d files -> %s' % (total, len(site), a.out))
    print('elekloader %s (%s%s%s), zip sha256 %s; Pyodide %s; cores: %s; shop: %s'
          % (__version__, commit[:12], ', with package changes NOT in the zip' if dirty else '',
             (', the same package as %s' if same else ', not the package of %s') % a.release
             if a.release else '', sha(zipped), PYODIDE_VERSION,
             ', '.join(c['file'] for c, _ in cs) or 'none',
             ', '.join(e['file'] + ('' if e['available'] else ' (not available)')
                       for e, _ in items) or 'none'))
    return 0


if __name__ == '__main__':
    sys.exit(main())
