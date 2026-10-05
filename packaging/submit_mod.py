#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""A mod submitted with the "Submit a mod" issue form, checked and written into web/catalog.json.

    python packaging/submit_mod.py --issue-body body.md [--files-dir build/shop] [--report report.md]
    python packaging/submit_mod.py --repo owner/name --tag v1.0 [--summary ...] [--on-unit ...]
                                   [--license SPDX] [--files a.elemod,b.elemod] [--files-dir build/shop]

It reads the release from GitHub's API (GH_TOKEN or GITHUB_TOKEN, if set, raises the rate limit, and is sent to
api.github.com only), downloads each .elemod, checks it against the sha256 GitHub lists, and reads it with elekloader:
its id, version, device, OS, licence and requirements. With --files-dir (the shop's files, downloaded as pages.yml
does) it also knows what each listed file is, so it can tell:
- an update: the same mod, device and OS from the same repository, which replaces the old entry;
- a clash: another repository's file for a mod the shop lists (a release can bundle someone else's mod), which is
  flagged and left out unless the submitter names the file in "Only these files".

A core the release carries goes in with it when a mod of the release needs it and the shop doesn't list it.
web/catalog.json keeps its own layout (render() reproduces it byte for byte). Nothing in the release is run.

Exit 0: entries added or changed (the report says what); 1: nothing added, refused (the report says why);
2: nothing new.
"""
import argparse
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
            'title': m.doc.get('title', m.id), 'core': m.id == 'core'}


def version_key(v):
    return [(0, int(p)) if p.isdigit() else (1, p) for p in re.split(r'(\d+)', str(v)) if p]


def submit(fields, catalog=CATALOG, files_dir=None):
    """-> (status, report). Writes the catalog when entries are added or changed."""
    r = {'repo': '', 'tag': '', 'rows': [], 'errors': [], 'changed': False}
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
    if not summary or len(summary) > 300:
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
        r['errors'].append('GitHub has no release %s in %s.' % (tag, repo) if e.code == 404 else 'GitHub answered %d.' % e.code)
        return 1, r
    if release.get('draft') or release.get('prerelease'):
        r['errors'].append('%s %s is a draft or a pre-release: submit a published release.' % (repo, tag))
        return 1, r
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

    tmp = tempfile.mkdtemp()
    try:
        files = []
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
            if only and a['name'] not in only and not f['core']:   # cores follow the mods that need them
                listed = by_name.get(a['name'])
                row['result'] = ('already in the shop' if listed and listed['sha256'] == sha
                                 else 'left out: not in "Only these files"')
                continue
            if f['license'] not in SHOP_LICENCES:
                row['result'] = ("refused: licence '%s' does not allow the site to pass it on" % f['license']
                                 if f['license'] else 'refused: it names no licence; give the repository\'s in "Licence"')
                continue
            files.append(row)
        cores = {(f['device'], f['os']): f for f in files if f['core']}
        new, needed = [], set()
        for f in files:
            have = by_name.get(f['file'])
            if have:
                f['result'] = ('already in the shop' if have['sha256'] == f['sha256']
                               else 'refused: the shop lists another file by this name')
                continue
            if f['core']:
                continue                            # decided with the mods that need it, below
            same = [(it, known[name(it)]) for it in items if name(it) in known and not known[name(it)]['core']
                    and (known[name(it)]['id'], known[name(it)]['device'], known[name(it)]['os']) == (f['id'], f['device'], f['os'])]
            clash = [it for it, k in same if it['repo'].lower() != repo.lower()]
            older = [it for it, k in same if it['repo'].lower() == repo.lower()]
            if clash and f['file'] not in only:
                f['result'] = ('flagged, left out: the shop has %s from %s for %s %s; name this file in "Only these files" '
                               'to add a second build anyway' % (f['id'], clash[0]['repo'], f['device'], f['os']))
                continue
            entry = {'repo': repo, 'tag': tag, 'file': f['file'], 'device': f['device']}
            core = cores.get((f['device'], f['os']))
            if core:
                entry['needs_core'] = core['version']
                needed.add((f['device'], f['os']))
            if not f['named_license']:
                entry['license'] = f['license']
            entry.update({'sha256': f['sha256'], 'summary': summary})
            if on_unit:
                entry['on_unit'] = on_unit
            at = items.index(older[0]) if older else None       # an update takes the old entry's place
            for it in older:
                items.remove(it)
            f['result'] = ('replaces ' + ', '.join(it['file'] for it in older)) if older else ('added, beside %s from %s' % (f['id'], clash[0]['repo']) if clash else 'added')
            new.append((entry, at))
        # a core of the release goes in when a mod of it needs that core and the shop doesn't list it
        for (device, os_), c in cores.items():
            if c['file'] in by_name:
                if not c['result']:
                    c['result'] = 'already in the shop'
                continue
            if (device, os_) in needed:
                new.append(({'repo': repo, 'tag': tag, 'file': c['file'], 'kind': 'core', 'device': device, 'sha256': c['sha256']}, None))
                c['result'] = 'added: the core this release\'s mods need'
            elif not c['result']:
                c['result'] = 'left out: no mod of this submission needs it'
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
            r['titles'] = sorted({f.get('title', f['file']) for f in files if f['result'].startswith(('added', 'replaces'))})
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    if r['changed']:
        return 0, r
    if any(row['result'].startswith('refused') or row['result'].startswith('flagged') for row in r['rows']):
        return 1, r
    return 2, r


def report_md(status, r):
    lines = ['### %s %s' % (r['repo'] or '?', r['tag'] or '?'), '']
    lines += ['- %s' % e for e in r['errors']]
    if r['rows']:
        lines += ['| file | mod | for | licence | result |', '|---|---|---|---|---|']
        for row in r['rows']:
            lines.append('| `%s` | %s | %s | %s | %s |' % (row['file'], (row.get('title', '') + ' ' + row.get('version', '')).strip(),
                         ('%s %s' % (row.get('device', ''), row.get('os', ''))).strip(), row.get('license', ''), row['result']))
    lines.append('')
    lines.append({0: 'These go to the shop in a pull request, for a maintainer to review.',
                  1: 'Nothing was added. Edit the issue to fix it, and the check runs again.',
                  2: 'Nothing new: the shop has these files already.'}[status])
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
    ap.add_argument('--report', help='write the report (Markdown) here')
    ap.add_argument('--result', help='write the result (JSON) here: status, changed, titles')
    a = ap.parse_args(argv)
    if a.issue_body:
        with open(a.issue_body, encoding='utf-8') as fh:
            fields = parse_issue(fh.read())
    else:
        fields = {'repo': a.repo or '', 'tag': a.tag or '', 'summary': a.summary, 'on_unit': a.on_unit,
                  'license': a.license, 'files': a.files}
    status, r = submit(fields, a.catalog, a.files_dir)
    text = report_md(status, r)
    if a.report:
        with open(a.report, 'w', encoding='utf-8') as fh:
            fh.write(text)
    if a.result:
        with open(a.result, 'w', encoding='utf-8') as fh:
            json.dump({'status': status, 'changed': r['changed'], 'repo': r['repo'], 'tag': r['tag'],
                       'titles': r.get('titles', [])}, fh)
    sys.stdout.write(text)
    return status


if __name__ == '__main__':
    sys.exit(main())
