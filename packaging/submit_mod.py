#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""A mod submitted with the "Submit a mod" issue form, checked and written into web/catalog.json.

    python packaging/submit_mod.py --issue-body body.md [--files-dir build/shop] [--cores-dir build/core]
                                   [--report report.md]
    python packaging/submit_mod.py --repo owner/name --tag v1.0 [--summary ...] [--on-unit ...]
                                   [--license SPDX] [--files a.elemod,b.elemod] [--files-dir build/shop]

It reads the release from GitHub's API (GH_TOKEN or GITHUB_TOKEN, if set, raises the rate limit, and is sent to
api.github.com only), downloads each .elemod, checks it against the sha256 GitHub lists, and reads it with elekloader:
its id, version, device, OS, licence and requirements. A published release or a pre-release; not a draft. With
--files-dir (the shop's files, downloaded as pages.yml does) it also knows what each listed file is, so it can tell:
- an update: the same mod, device and OS from the same repository, which replaces the old entry;
- a clash: another repository's file for a mod the shop lists (a release can bundle someone else's mod), which is
  flagged and left out unless the submitter names the file in "Only these files".

What a mod requires must be there for it to build:
- another mod (machine-pages, fxkit, ...): the shop's for its device and OS, or else the release's own, which comes
  along with it even when "Only these files" leaves it out;
- core: with --cores-dir (the latest release's cores, as pages.yml downloads them) and the shop's, a core for its
  device and OS at least as new as the version the file names (resources.core), or the release's core if it carries
  one. Cores come from elekloader's own releases only: the page builds with the newest core for a device and OS, so
  a core from a mod's release would become every build's. A mod's entry gets "needs_core" from its file, or else from
  the core its release carries.

An update keeps its card's text: the summary, and the hardware note marked "As of <the old version>", unless the
form or the release's notes give new ones ("Shop summary: ..." and "Tested on a unit: ..." lines). A file older than
the one the shop lists is left out. With --update (packaging/shop_updates.py) only newer files of mods this
repository already has go in, and a pre-release replaces only what came from a pre-release.

web/catalog.json keeps its own layout (render() reproduces it byte for byte). Nothing in the release is run.

Exit 0: entries added or changed (the report says what); 1: nothing added, refused (the report says why);
2: nothing new.
"""
import argparse
import glob
import hashlib
import json
import os
import re
import shutil
import sys
import tempfile
import urllib.error
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
sys.path.insert(0, os.path.join(ROOT, 'packaging'))

from elekloader import elemod  # noqa: E402
from build_web import SHOP_LICENCES  # noqa: E402

CATALOG = os.path.join(ROOT, 'web', 'catalog.json')
REPO = re.compile(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+')
TAG = re.compile(r'[A-Za-z0-9_.+-]{1,100}')
FILE = re.compile(r'[\w.-]+\.(elemod|dtmod)')
# the repositories whose releases may bring a core: elekloader's, under its old name too
CORE_REPOS = {(os.environ.get('GITHUB_REPOSITORY') or 'elekloader/elekloader').lower(), 'elekloader/elekloader'}
# repositories that moved, by their old names (the shop's list still names some by these)
MOVED = {'irpina/elekloader': 'elekloader/elekloader'}
# the issue form's fields (.github/ISSUE_TEMPLATE/submit-mod.yml), by their labels
FIELDS = {'Repository': 'repo', 'Release tag': 'tag', 'Summary': 'summary', 'Tested on a unit': 'on_unit',
          'Licence, for files that name none': 'license', 'Only these files': 'files'}
# web/catalog.json's layout: these keys share a line, in this order; any other key gets a line of its own
LINES = (('repo', 'tag', 'file', 'kind', 'commit'), ('path',), ('device', 'needs_core', 'title', 'version', 'license'),
         ('sha256',), ('summary',), ('on_unit',))


def render(doc):
    """web/catalog.json as it is laid out by hand: an item's short keys on shared lines."""
    def item(it):
        lines, used = [], set()
        for group in LINES:
            keys = [k for k in group if k in it]
            if keys:
                lines.append(', '.join('%s: %s' % (json.dumps(k), json.dumps(it[k], ensure_ascii=False)) for k in keys))
                used.update(keys)
        lines += ['%s: %s' % (json.dumps(k), json.dumps(v, ensure_ascii=False)) for k, v in it.items() if k not in used]
        return '    {\n' + ',\n'.join('      ' + line for line in lines) + '\n    }'
    return ('{\n  "about": %s,\n  "items": [\n' % json.dumps(doc['about'], ensure_ascii=False)
            + ',\n'.join(item(i) for i in doc['items']) + '\n  ]\n}\n')


def parse_issue(text):
    """The issue form's answers: GitHub writes each as '### <label>' and the answer, or '_No response_'."""
    out = {}
    for section in re.split(r'^### +', text.replace('\r\n', '\n'), flags=re.M)[1:]:
        head, _, body = section.partition('\n')
        key = FIELDS.get(head.strip())
        if key:
            value = body.strip()
            out[key] = '' if value == '_No response_' else value
    return out


def get(url, accept='application/octet-stream', limit=8 << 20):
    """A GET from GitHub; the token goes to api.github.com only (urllib would carry it across redirects)."""
    req = urllib.request.Request(url, headers={'Accept': accept, 'User-Agent': 'elekloader-submissions'})
    token = os.environ.get('GH_TOKEN') or os.environ.get('GITHUB_TOKEN')
    if token and url.startswith('https://api.github.com/'):
        req.add_header('Authorization', 'Bearer ' + token)
    with urllib.request.urlopen(req, timeout=60) as r:
        data = r.read(limit + 1)
    if len(data) > limit:
        raise ValueError('larger than %d MB' % (limit >> 20))
    return data


def facts(path, license=''):
    """What a file says about itself, read by elekloader."""
    m = elemod.load_any(path)
    return {'id': m.id, 'version': str(m.version), 'device': m.dev.key, 'os': m.rel.version,
            'license': m.doc.get('license') or license, 'named_license': bool(m.doc.get('license')),
            'title': m.doc.get('title', m.id), 'core': m.id == 'core', 'requires': list(m.requires),
            'needs_core': getattr(m, 'needs_core', None), 'description': ' '.join(str(m.doc.get('description', '')).split())}


def same_repo(name):
    """A repository's name as compared: lower case, under its new name if it moved."""
    return MOVED.get(name.lower(), name.lower())


def newest(versions):
    return max(versions, key=elemod.version_key) if versions else None


def release_text(body):
    """The shop's text a release's notes may give: 'Shop summary: ...' and 'Tested on a unit: ...' lines."""
    out = {}
    for line in (body or '').replace('\r\n', '\n').split('\n'):
        m = re.match(r'^[\s>*_-]*(shop summary|tested on a unit)[*_]*\s*:[*_]*\s*(.+)$', line, re.I)
        if m:
            out['summary' if m.group(1).lower() == 'shop summary' else 'on_unit'] = ' '.join(m.group(2).split())
    return out


def as_of(text, version):
    """An earlier version's hardware note, kept for a newer file: it says which version it was about."""
    return text if not text or text.startswith('As of ') else 'As of %s: %s' % (version, text)


def submit(fields, catalog=CATALOG, files_dir=None, cores_dir=None, update=False):
    """-> (status, report). Writes the catalog when entries are added or changed.

    update: only newer files of mods this repository already has in the shop go in (for any OS of the same mod and
    device); the summary and the hardware note come from the release's notes or else the entry they replace, so the
    fields may be empty; a pre-release replaces only what came from a pre-release. The report's same_repo_update says
    whether every change is such an update, in either mode."""
    r = {'repo': '', 'tag': '', 'rows': [], 'errors': [], 'changed': False, 'prerelease': False, 'same_repo_update': False}
    repo, tag = fields.get('repo', '').strip(), fields.get('tag', '').strip()
    repo = re.sub(r'^https://github\.com/|/$|\.git$', '', repo)
    summary = ' '.join(fields.get('summary', '').split())
    on_unit = ' '.join(fields.get('on_unit', '').split())
    license = fields.get('license', '').strip()
    only = [f.strip() for f in re.split(r'[\s,]+', fields.get('files', '')) if f.strip()]
    r['repo'], r['tag'] = repo, tag
    if not REPO.fullmatch(repo):
        r['errors'].append('The repository is owner/name, as on GitHub (for example irpina/digislicer).')
    if not TAG.fullmatch(tag):
        r['errors'].append('The release tag is a tag of that repository, such as v2.1.')
    if (not summary and not update) or len(summary) > 300:
        r['errors'].append('The summary is one sentence of up to 300 characters.')
    if len(on_unit) > 600:
        r['errors'].append('"Tested on a unit" is up to 600 characters.')
    if license and license not in SHOP_LICENCES:
        r['errors'].append('The licence is an SPDX id the site may pass a mod on under: %s.' % ', '.join(sorted(SHOP_LICENCES)))
    bad = [f for f in only if not FILE.fullmatch(f)]
    if bad:
        r['errors'].append('"Only these files" lists .elemod file names, one per line: not %s.' % ', '.join(bad))
    if r['errors']:
        return 1, r
    try:
        release = json.loads(get('https://api.github.com/repos/%s/releases/tags/%s' % (repo, tag), 'application/vnd.github+json', 2 << 20))
    except urllib.error.HTTPError as e:
        r['errors'].append('GitHub has no release %s in %s (a draft is not one until it is published).' % (tag, repo)
                           if e.code == 404 else 'GitHub answered %d.' % e.code)
        return 1, r
    if release.get('draft'):
        r['errors'].append('%s %s is a draft: publish it (a pre-release is fine).' % (repo, tag))
        return 1, r
    r['prerelease'] = bool(release.get('prerelease'))
    canonical = re.match(r'https://github\.com/([^/]+/[^/]+)/releases/', release.get('html_url', ''))
    if canonical:                                  # the name GitHub gives it now, if it moved
        repo = r['repo'] = canonical.group(1)
    notes = release_text(release.get('body'))      # the form's text first, then the release's notes
    summary = summary or notes.get('summary', '')[:300]
    on_unit = on_unit or notes.get('on_unit', '')[:600]
    assets = [a for a in release.get('assets', []) if FILE.fullmatch(a['name'])]
    if not assets:
        r['errors'].append('%s %s has no .elemod files attached.' % (repo, tag))
        return 1, r
    missing = [f for f in only if f not in {a['name'] for a in assets}]
    if missing:
        r['errors'].append('The release has no %s.' % ', '.join(missing))
        return 1, r

    with open(catalog, encoding='utf-8') as fh:
        text = fh.read()
    doc = json.loads(text)
    items = doc['items']
    known = {}                                    # file -> what it is, for the files the shop has
    for it in items:
        p = os.path.join(files_dir, it['file'] if 'file' in it else it['path'].rsplit('/', 1)[-1]) if files_dir else ''
        if p and os.path.exists(p):
            try:
                known[os.path.basename(p)] = facts(p)
            except (OSError, elemod.ModError):
                pass
    name = lambda it: it['file'] if 'file' in it else it['path'].rsplit('/', 1)[-1]
    by_name = {name(it): it for it in items}
    key = lambda f: (f['id'], f['device'], f['os'])
    shop_mods = {key(k) for k in known.values()}
    site_cores = {}                               # (device, os) -> the core versions the site has
    cores_known = cores_dir is not None
    for p in sorted(glob.glob(os.path.join(cores_dir, '*.elemod'))) if cores_known else []:
        try:
            f = facts(p)
        except (OSError, elemod.ModError):
            continue
        if f['core']:
            site_cores.setdefault((f['device'], f['os']), []).append(f['version'])
    for k in known.values():
        if k['core']:
            site_cores.setdefault((k['device'], k['os']), []).append(k['version'])
    own_entries = {}                              # (id, device) -> this repository's entries in the shop, any OS
    for it in items:
        k = known.get(name(it))
        if k and not k['core'] and same_repo(it['repo']) == same_repo(repo):
            own_entries.setdefault((k['id'], k['device']), []).append(it)
    pre_tags = {}

    def from_prerelease(it):
        """Whether a listed entry's release is a pre-release (a commit pin counts as a release)."""
        if 'tag' not in it:
            return False
        if (it['repo'], it['tag']) not in pre_tags:
            try:
                rel = json.loads(get('https://api.github.com/repos/%s/releases/tags/%s' % (it['repo'], it['tag']),
                                     'application/vnd.github+json', 2 << 20))
                pre_tags[(it['repo'], it['tag'])] = bool(rel.get('prerelease'))
            except (urllib.error.URLError, ValueError):
                pre_tags[(it['repo'], it['tag'])] = False
        return pre_tags[(it['repo'], it['tag'])]

    tmp = tempfile.mkdtemp()
    try:
        files, aside = [], []                     # the files to decide on; those "Only these files" left out
        for a in assets:
            row = {'file': a['name'], 'result': ''}
            r['rows'].append(row)
            try:
                data = get('https://github.com/%s/releases/download/%s/%s' % (repo, tag, a['name']))
            except (urllib.error.URLError, ValueError) as e:
                row['result'] = 'refused: could not be downloaded (%s)' % e
                continue
            sha = hashlib.sha256(data).hexdigest()
            if a.get('digest') and a['digest'] != 'sha256:' + sha:
                row['result'] = 'refused: not the file GitHub lists'
                continue
            path = os.path.join(tmp, a['name'])
            with open(path, 'wb') as fh:
                fh.write(data)
            try:
                f = facts(path, license)
            except (OSError, elemod.ModError, ValueError) as e:
                row['result'] = 'refused: elekloader cannot read it (%s)' % e
                continue
            row.update(f)
            row['sha256'] = sha
            licensed = f['license'] in SHOP_LICENCES
            if only and a['name'] not in only and not f['core']:   # cores and required mods may still come along
                listed = by_name.get(a['name'])
                row['result'] = ('already in the shop' if listed and listed['sha256'] == sha
                                 else 'left out: not in "Only these files"')
                if licensed:
                    aside.append(row)
                continue
            if not licensed:
                row['result'] = ("refused: licence '%s' does not allow the site to pass it on" % f['license']
                                 if f['license'] else 'refused: it names no licence; give the repository\'s in "Licence"')
                continue
            files.append(row)
        cores = {(f['device'], f['os']): f for f in files if f['core']}
        ours = same_repo(repo) in CORE_REPOS
        release_mods = {key(f): f for f in files + aside if not f['core']}

        for f in aside:
            if f['result'] == 'already in the shop':
                shop_mods.add(key(f))

        # the mods: each decided on its own, then what each requires (each file once)
        queue, taken = [f for f in files if not f['core']], []
        seen = {f['file'] for f in queue}
        while queue:
            f = queue.pop(0)
            listed = by_name.get(f['file'])
            if listed:
                f['result'] = ('already in the shop' if listed['sha256'] == f['sha256']
                               else 'refused: the shop lists another file by this name')
                if listed['sha256'] == f['sha256']:
                    shop_mods.add(key(f))
                continue
            same = [(it, known[name(it)]) for it in items if name(it) in known and not known[name(it)]['core']
                    and key(known[name(it)]) == key(f)]
            f['clash'] = [it for it, k in same if same_repo(it['repo']) != same_repo(repo)]
            f['older'] = [it for it, k in same if same_repo(it['repo']) == same_repo(repo)]
            if f['clash'] and update:
                f['result'] = 'left out: the shop takes %s from %s' % (f['id'], f['clash'][0]['repo'])
                continue
            if f['clash'] and f['file'] not in only:
                f['result'] = ('flagged, left out: the shop has %s from %s for %s %s; name this file in "Only these files" '
                               'to add a second build anyway' % (f['id'], f['clash'][0]['repo'], f['device'], f['os']))
                continue
            # what it updates: the entry for the same OS, or else this repository's entry for another OS
            mine = own_entries.get((f['id'], f['device']), [])
            f['update_of'] = (f['older'] or mine or [None])[0]
            if update and f['update_of'] is None:
                f['result'] = 'left out: not in the shop yet; submit it with the Submit a mod form'
                continue
            f['held'] = next((it['hold'] for it in f['older'] or mine if it.get('hold')), None)
            if update and f['held']:
                f['result'] = 'left out: the shop holds %s; a maintainer updates it' % f['id']
                continue
            if f['older']:
                was = known[name(f['older'][0])]['version']
                if elemod.version_key(f['version']) < elemod.version_key(was):
                    f['result'] = 'left out: the shop has a newer version (%s)' % was
                    continue
            if update and r['prerelease'] and not all(from_prerelease(it) for it in f['older'] or mine):
                f['result'] = 'left out: a pre-release replaces a release only through the Submit a mod form'
                continue
            # the core: the version the file names, or else the one its release carries
            f['need'] = None
            if 'core' in f['requires']:
                carried = cores.get((f['device'], f['os']))
                f['need'] = f['needs_core'] or (carried['version'] if carried else None)
                if cores_known:
                    avail = site_cores.get((f['device'], f['os']), []) + ([carried['version']] if carried and ours else [])
                    top = newest(avail)
                    if top is None or (f['need'] and elemod.version_key(top) < elemod.version_key(f['need'])):
                        f['result'] = ('refused: it needs core %sfor %s %s, and %s; cores come from elekloader\'s own '
                                       'releases' % (f['need'] + ' or newer ' if f['need'] else '', f['device'], f['os'],
                                                     'the newest the site has is ' + top if top else 'the site has none'))
                        continue
            # the other mods it requires: the shop's, or the release's own, which comes along (without the shop's
            # files, only the release's are known)
            lacking = []
            for req in f['requires']:
                if req == 'core' or (req, f['device'], f['os']) in shop_mods:
                    continue
                g = release_mods.get((req, f['device'], f['os']))
                if g is None:
                    if files_dir:
                        lacking.append(req)
                elif g['file'] not in seen:
                    seen.add(g['file'])
                    g['brought_by'] = f['title']
                    queue.append(g)
            if lacking:
                f['result'] = ('refused: it needs %s for %s %s, which neither the shop nor this release has'
                               % (', '.join(lacking), f['device'], f['os']))
                continue
            taken.append(f)
        # a mod whose required mod from the release was refused can't go in either
        while True:
            gone = {key(g) for g in release_mods.values() if g not in taken}
            drop = [f for f in taken if any((req, f['device'], f['os']) in gone and (req, f['device'], f['os']) not in shop_mods
                                            for req in f['requires'] if req != 'core')]
            if not drop:
                break
            for f in drop:
                taken.remove(f)
                f['result'] = 'refused: a mod it needs from this release can\'t go in'

        new, needed = [], set()
        for f in taken:
            entry = {'repo': repo, 'tag': tag, 'file': f['file'], 'device': f['device']}
            if f['held']:                              # a held entry's update (through the form) stays held
                entry['hold'] = f['held']
            if f['need']:
                entry['needs_core'] = f['need']
            if 'core' in f['requires']:
                needed.add((f['device'], f['os']))
            if not f['named_license']:
                entry['license'] = f['license']
            own = f.get('brought_by') and f['file'] not in only and only
            old = f['update_of'] or {}                 # an update keeps its card's text unless it is given anew
            was = ((known.get(name(old)) or {}).get('version') or old.get('tag', '')) if old else ''
            text = summary or old.get('summary') or f['description'][:300]
            entry.update({'sha256': f['sha256'], 'summary': (f['description'][:300] or text) if own else text})
            note = on_unit or as_of(old.get('on_unit', ''), was)
            if note:
                entry['on_unit'] = note
            at = items.index(f['older'][0]) if f['older'] else None       # an update takes the old entry's place
            for it in f['older']:
                items.remove(it)
            f['result'] = (('replaces ' + ', '.join(it['file'] for it in f['older'])) if f['older']
                           else 'added, beside %s from %s' % (f['id'], f['clash'][0]['repo']) if f['clash']
                           else 'added: %s needs it' % f['brought_by'] if f.get('brought_by') else 'added')
            new.append((entry, at))
        # a core of the release goes in when it is elekloader's and a mod of it needs that core
        for (device, os_), c in cores.items():
            if c['file'] in by_name:
                if not c['result']:
                    c['result'] = 'already in the shop'
                continue
            if (device, os_) not in needed:
                c['result'] = c['result'] or 'left out: no mod of this submission needs it'
            elif not ours:
                c['result'] = 'left out: cores come from elekloader\'s own releases'
            else:
                new.append(({'repo': repo, 'tag': tag, 'file': c['file'], 'kind': 'core', 'device': device, 'sha256': c['sha256']}, None))
                c['result'] = 'added: the core this release\'s mods need'
        if new:
            for e, at in new:                      # in the old entry's place, or after the shop's last item for its device
                if at is None:
                    at = max((i for i, it in enumerate(items) if it['device'] == e['device']), default=len(items) - 1) + 1
                items.insert(at, e)
            out = render(doc)
            if render(json.loads(out)) != out:
                raise RuntimeError('the catalog does not round-trip')
            with open(catalog, 'w', encoding='utf-8', newline='\n') as fh:
                fh.write(out)
            r['changed'] = True
            r['titles'] = sorted({f.get('title', f['file']) for f in r['rows'] if f['result'].startswith(('added', 'replaces'))})
            # every change a newer file of a mod this repository already has: no new mod, no clash, no core, none held
            r['same_repo_update'] = (all(f['update_of'] is not None and not f['clash'] and not f['held'] for f in taken)
                                     and len(new) == len(taken))
            if files_dir:                          # a later check in the same run knows these files
                for f in taken:
                    shutil.copyfile(os.path.join(tmp, f['file']), os.path.join(files_dir, f['file']))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    if r['changed']:
        return 0, r
    if any(row['result'].startswith('refused') or row['result'].startswith('flagged') for row in r['rows']):
        return 1, r
    return 2, r


def report_md(status, r):
    lines = ['### %s %s%s' % (r['repo'] or '?', r['tag'] or '?', ' (a pre-release)' if r.get('prerelease') else ''), '']
    lines += ['- %s' % e for e in r['errors']]
    if r['rows']:
        lines += ['| file | mod | for | needs | licence | result |', '|---|---|---|---|---|---|']
        for row in r['rows']:
            needs = ', '.join(('core %s+' % row['need'] if q == 'core' and row.get('need') else q)
                              for q in row.get('requires', []))
            lines.append('| `%s` | %s | %s | %s | %s | %s |' % (
                row['file'], (row.get('title', '') + ' ' + row.get('version', '')).strip(),
                ('%s %s' % (row.get('device', ''), row.get('os', ''))).strip(), needs, row.get('license', ''), row['result']))
    lines.append('')
    lines.append({0: 'These go to the shop in a pull request.' + (' Each is a newer file of a mod this repository already '
                                                                  'has in the shop.' if r.get('same_repo_update') else ''),
                  1: 'Nothing was added. Edit the issue to fix it, and the check runs again.',
                  2: 'Nothing new: the shop has these files already.' if all(row['result'] == 'already in the shop'
                                                                             for row in r['rows'])
                     else 'Nothing to add.'}[status])
    return '\n'.join(lines) + '\n'


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--issue-body', help='the issue\'s body, as the issue form wrote it')
    ap.add_argument('--repo')
    ap.add_argument('--tag')
    ap.add_argument('--summary', default='')
    ap.add_argument('--on-unit', default='')
    ap.add_argument('--license', default='')
    ap.add_argument('--files', default='')
    ap.add_argument('--catalog', default=CATALOG)
    ap.add_argument('--files-dir', help='the shop\'s files, as pages.yml downloads them, to compare with')
    ap.add_argument('--cores-dir', help='the latest release\'s cores, as pages.yml downloads them: a mod that needs a '
                                        'core the site doesn\'t have is refused')
    ap.add_argument('--update', action='store_true', help='only newer files of mods this repository already has in '
                                                           'the shop (packaging/shop_updates.py); the text may be empty')
    ap.add_argument('--report', help='write the report (Markdown) here')
    ap.add_argument('--result', help='write the result (JSON) here: status, changed, titles, same_repo_update')
    a = ap.parse_args(argv)
    if a.issue_body:
        with open(a.issue_body, encoding='utf-8') as fh:
            fields = parse_issue(fh.read())
    else:
        fields = {'repo': a.repo or '', 'tag': a.tag or '', 'summary': a.summary, 'on_unit': a.on_unit,
                  'license': a.license, 'files': a.files}
    status, r = submit(fields, a.catalog, a.files_dir, a.cores_dir, a.update)
    text = report_md(status, r)
    if a.report:
        with open(a.report, 'w', encoding='utf-8') as fh:
            fh.write(text)
    if a.result:
        with open(a.result, 'w', encoding='utf-8') as fh:
            json.dump({'status': status, 'changed': r['changed'], 'repo': r['repo'], 'tag': r['tag'],
                       'titles': r.get('titles', []), 'same_repo_update': r['same_repo_update']}, fh)
    sys.stdout.write(text)
    return status


if __name__ == '__main__':
    sys.exit(main())
