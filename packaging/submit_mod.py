#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""A mod submitted with the "Submit a mod" issue form, checked and written into catalog.json.

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

A thumbnail (packaging/thumbs.py: a 128x64 picture of the screen in two colours) is a release's thumb-<mod id>.png,
for that mod on every device, or its thumb.png, for the mods with none of their own (in a commit, each name once
anywhere in its tree). One that passes the check goes on the entries this submission adds or changes, and on this
repository's entries the form lists already; the report shows each new picture's pixels as text (with --previews, in
parts for the pull request's comments when there are many), and the change is never one that merges by itself
(same_repo_update is false), so a maintainer looks at every new picture. A thumbnail is pinned
on its own ("thumb": tag and file, or commit and path, and sha256), so it outlives the release its mod moves on from.
With --update a release's thumbnail goes in only when it is the very picture the entry has (the pin moves to it);
another one waits for the form, and the entry keeps its own.

Automatic updates are on for every entry unless its author opts out ("auto_update": false in catalog.json): the
form's "Updates" box ticked, or a "Shop auto-update: off" line in the release's notes, sets it on the files that go in
(and on this repository's files the form lists already, when only the setting changes); the box unticked, or "Shop
auto-update: on" in the notes, clears it. With --update an opted-out entry is left alone, and a newer release whose
notes say "off" is not taken: it opts the repository out instead.

catalog.json keeps its own layout (render() reproduces it byte for byte). Nothing in the release is run.

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
import urllib.parse
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
sys.path.insert(0, os.path.join(ROOT, 'packaging'))

from elekloader import elemod  # noqa: E402
from build_web import SHOP_LICENCES, clash, footprint  # noqa: E402
import thumbs  # noqa: E402
from elekloader import link  # noqa: E402

CATALOG = os.environ.get('ELEKLOADER_CATALOG') or os.path.join(ROOT, 'build', 'shop-list', 'catalog.json')   # build_web.catalog_path
REPO = re.compile(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+')
TAG = re.compile(r'[A-Za-z0-9_.+-]{1,100}')
FILE = re.compile(r'[\w.-]+\.(elemod|dtmod)')
PATH = re.compile(r'[\w.-]+(/[\w.-]+)*')       # a commit's file, as build_web.py takes it
COMMIT = re.compile(r'[0-9a-f]{40}')
# the repositories whose releases may bring a core: elekloader's, under its old name too
CORE_REPOS = {(os.environ.get('GITHUB_REPOSITORY') or 'elekloader/elekloader').lower(), 'elekloader/elekloader'}
# repositories that moved, by their old names (the shop's list still names some by these)
MOVED = {'irpina/elekloader': 'elekloader/elekloader'}
# the issue form's fields (elekloader/shop's .github/ISSUE_TEMPLATE/submit-mod.yml), by their labels
FIELDS = {'Repository': 'repo', 'Release tag': 'tag', 'Or a commit': 'commit', 'Summary': 'summary',
          'Tested on a unit': 'on_unit', 'Licence, for files that name none': 'license', 'Only these files': 'files',
          'Why': 'why', 'Updates': 'updates'}
# catalog.json's layout: these keys share a line, in this order; any other key gets a line of its own
LINES = (('repo', 'tag', 'file', 'kind', 'commit'), ('path',), ('device', 'needs_core', 'title', 'version', 'license'),
         ('sha256',), ('summary',), ('on_unit',))


def render(doc):
    """catalog.json as it is laid out by hand: an item's short keys on shared lines."""
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
    """What a file says about itself, read by elekloader: with what it holds that no other mod in a build may
    share (build_web.footprint) and the RAM it takes."""
    m = elemod.load_any(path)
    v2 = isinstance(m, link.Mod2)
    return {'id': m.id, 'version': str(m.version), 'device': m.dev.key, 'os': m.rel.version,
            'license': m.doc.get('license') or license, 'named_license': bool(m.doc.get('license')),
            'title': m.doc.get('title', m.id), 'core': m.id == 'core', 'requires': list(m.requires),
            'needs_core': getattr(m, 'needs_core', None), 'description': ' '.join(str(m.doc.get('description', '')).split()),
            'footprint': dict(footprint(m), id=m.id),
            'ram': (m.size('.run') + m.size('.bss')) if v2 else (m.blob['len'] if m.blob else 0)}


def heads_up(f, known, site_cores, core_prints=None):
    """What the author may want to know about a file that goes in: the core it builds with (as a builder takes it,
    gui.with_core: of the site's cores that are new enough for it, the newest it can share a build with, by
    `core_prints`, each core's footprint by (device, os, version)), its RAM, and the shop's mods for its device and
    OS it can't share a build with (the page greys them out beside it)."""
    out = []
    mine = sorted(site_cores.get((f['device'], f['os']), []), key=elemod.version_key)
    if 'core' in f['requires'] and mine:
        ok = [v for v in mine if not f.get('need') or elemod.version_key(v) >= elemod.version_key(f['need'])] or mine
        prints = {v: (core_prints or {}).get((f['device'], f['os'], v)) for v in ok}
        why = {v: clash(f['footprint'], prints[v]) if f.get('footprint') and prints[v] else '' for v in ok}
        takes = [v for v in ok if not why[v]]
        if not takes:
            out.append("can't be built on the site: it can't link with core %s (%s), nor with an older one the site has"
                       % (ok[-1], why[ok[-1]]))
        elif takes[-1] == ok[-1]:
            out.append('builds with core %s' % ok[-1])
        else:
            out.append("builds with core %s, not the newest: it can't link with core %s (%s), so it can't share a "
                       'build with a mod that needs a newer core than %s' % (takes[-1], ok[-1], why[ok[-1]], takes[-1]))
    if f.get('ram'):
        out.append('%.1f KB of RAM' % (f['ram'] / 1024.0))
    if f.get('footprint'):
        gone = {it.get('file') or it['path'].rsplit('/', 1)[-1] for it in f.get('older') or []}
        cant = {}
        for name, k in sorted(known.items()):
            if (k['core'] or name in gone or k['id'] == f['id'] or (k['device'], k['os']) != (f['device'], f['os'])
                    or not k.get('footprint')):
                continue
            why = clash(f['footprint'], k['footprint'])
            if why:
                cant.setdefault(why, []).append('%s %s' % (k['title'], k['version']))
        out += ["can't go in one build with %s (%s)" % (', '.join(sorted(set(v))), why) for why, v in sorted(cant.items())]
    return out


def same_repo(name):
    """A repository's name as compared: lower case, under its new name if it moved."""
    return MOVED.get(name.lower(), name.lower())


def newest(versions):
    return max(versions, key=elemod.version_key) if versions else None


def release_text(body):
    """What a release's notes may tell the shop: 'Shop summary: ...' and 'Tested on a unit: ...' lines (the card's
    text), and 'Shop auto-update: on' or 'off' (whether the shop takes this author's newer releases by itself)."""
    out = {}
    for line in (body or '').replace('\r\n', '\n').split('\n'):
        m = re.match(r'^[\s>*_-]*(shop summary|tested on a unit|shop auto-?updates?)[*_]*\s*:[*_]*\s*(.+)$', line, re.I)
        if not m:
            continue
        what, value = m.group(1).lower(), ' '.join(m.group(2).split())
        if what.startswith('shop auto'):
            word = value.strip('*_. ').lower()
            if word in ('on', 'yes', 'true'):
                out['auto_update'] = True
            elif word in ('off', 'no', 'false'):
                out['auto_update'] = False
        else:
            out['summary' if what == 'shop summary' else 'on_unit'] = value
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
    r = {'repo': '', 'tag': '', 'rows': [], 'errors': [], 'changed': False, 'prerelease': False, 'same_repo_update': False,
         'thumbs': {}, 'thumb_new': [], 'thumb_waits': [], 'thumb_on': {}}
    repo, tag = fields.get('repo', '').strip(), fields.get('tag', '').strip()
    commit = fields.get('commit', '').strip().lower()
    repo = re.sub(r'^https://github\.com/|/$|\.git$', '', repo)
    summary = ' '.join(fields.get('summary', '').split())
    on_unit = ' '.join(fields.get('on_unit', '').split())
    license = fields.get('license', '').strip()
    only = [f.strip() for f in re.split(r'[\s,]+', fields.get('files', '')) if f.strip()]
    form_off = fields.get('auto_update') is False or bool(re.search(r'\[x\]', fields.get('updates', ''), re.I))
    r['repo'], r['tag'] = repo, tag or commit[:7]
    r['auto_off'] = None
    if not REPO.fullmatch(repo):
        r['errors'].append('The repository is owner/name, as on GitHub (for example irpina/digislicer).')
    if commit and tag:
        r['errors'].append('Give a release tag or a commit, not both.')
    elif commit and not COMMIT.fullmatch(commit):
        r['errors'].append('The commit is its whole 40-character id.')
    elif not commit and not TAG.fullmatch(tag):
        r['errors'].append('The release tag is a tag of that repository, such as v2.1 (or give a commit instead).')
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
    api = 'https://api.github.com/repos/'
    if commit:                                     # files kept in the repository: those in the commit's tree
        try:
            found = json.loads(get(api + '%s/commits/%s' % (repo, commit), 'application/vnd.github+json', 4 << 20))
            tree = json.loads(get(api + '%s/git/trees/%s?recursive=1' % (repo, commit), 'application/vnd.github+json',
                                  16 << 20))
        except urllib.error.HTTPError as e:
            r['errors'].append('GitHub has no commit %s in %s.' % (commit, repo) if e.code in (404, 422)
                               else 'GitHub answered %d.' % e.code)
            return 1, r
        canonical = re.match(r'https://github\.com/([^/]+/[^/]+)/commit/', found.get('html_url', ''))
        release = {}
        assets = [{'name': e['path'].rsplit('/', 1)[-1], 'path': e['path']} for e in tree.get('tree', [])
                  if e.get('type') == 'blob' and FILE.fullmatch(e['path'].rsplit('/', 1)[-1])
                  and PATH.fullmatch(e['path']) and '..' not in e['path'].split('/')]
        pictures = {}
        for e in tree.get('tree', []):
            n = e['path'].rsplit('/', 1)[-1]
            if (e.get('type') == 'blob' and thumbs.PICTURE.fullmatch(n) and PATH.fullmatch(e['path'])
                    and '..' not in e['path'].split('/')):
                pictures.setdefault(n, []).append({'path': e['path']})
        for n, found in list(pictures.items()):
            if len(found) > 1:
                r['thumbs'][n] = {'ok': False, 'reason': 'the commit has more than one'}
                del pictures[n]
        pictures = {n: found[0] for n, found in pictures.items()}
        names = [a['name'] for a in assets]
        twice = sorted({n for n in names if names.count(n) > 1})
        if twice:
            r['errors'].append('The commit has more than one %s: the shop names a file by its name.' % ', '.join(twice))
            return 1, r
    else:
        try:
            release = json.loads(get(api + '%s/releases/tags/%s' % (repo, tag), 'application/vnd.github+json', 2 << 20))
        except urllib.error.HTTPError as e:
            r['errors'].append('GitHub has no release %s in %s (a draft is not one until it is published).' % (tag, repo)
                               if e.code == 404 else 'GitHub answered %d.' % e.code)
            return 1, r
        if release.get('draft'):
            r['errors'].append('%s %s is a draft: publish it (a pre-release is fine).' % (repo, tag))
            return 1, r
        canonical = re.match(r'https://github\.com/([^/]+/[^/]+)/releases/', release.get('html_url', ''))
        assets = [a for a in release.get('assets', []) if FILE.fullmatch(a['name'])]
        pictures = {a['name']: {'digest': a.get('digest')} for a in release.get('assets', [])
                    if thumbs.PICTURE.fullmatch(a['name'])}
    r['prerelease'] = bool(release.get('prerelease'))
    if canonical:                                  # the name GitHub gives it now, if it moved
        repo = r['repo'] = canonical.group(1)
    notes = release_text(release.get('body'))      # the form's text first, then the release's notes
    summary = summary or notes.get('summary', '')[:300]
    on_unit = on_unit or notes.get('on_unit', '')[:600]
    # automatic updates are on unless the author opts out: the form's "Updates" box ticked, or "Shop auto-update: off"
    # in the release's notes; "Shop auto-update: on" in the notes turns them back on
    off = notes.get('auto_update') is False or (form_off and notes.get('auto_update') is not True)
    if not assets:
        r['errors'].append('%s %s has no .elemod files %s.' % (repo, r['tag'], 'in it' if commit else 'attached'))
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
    if update and notes.get('auto_update') is False:
        # the author's release says to stop: its files stay out, and the shop stops following the repository
        stopping = [it for it in items if same_repo(it['repo']) == same_repo(repo) and it.get('auto_update') is not False
                    and it.get('kind') != 'core']
        if not stopping:
            return 2, r
        for it in stopping:
            it['auto_update'] = False
            r['rows'].append({'file': name(it), 'result': 'auto-update turned off by the release\'s notes',
                              'title': (known.get(name(it)) or {}).get('title', name(it))})
        with open(catalog, 'w', encoding='utf-8', newline='\n') as fh:
            fh.write(render(doc))
        r.update(changed=True, same_repo_update=True, stopped=True, auto_off=True,
                 titles=sorted({row['title'] for row in r['rows']}))
        return 0, r
    key = lambda f: (f['id'], f['device'], f['os'])
    shop_mods = {key(k) for k in known.values()}
    site_cores = {}                               # (device, os) -> the core versions the site has
    core_prints = {}                              # (device, os, version) -> that core's footprint (heads_up)
    cores_known = cores_dir is not None
    for p in sorted(glob.glob(os.path.join(cores_dir, '*.elemod'))) if cores_known else []:
        try:
            f = facts(p)
        except (OSError, elemod.ModError):
            continue
        if f['core']:
            site_cores.setdefault((f['device'], f['os']), []).append(f['version'])
            core_prints[(f['device'], f['os'], f['version'])] = f.get('footprint')
    for k in known.values():
        if k['core']:
            site_cores.setdefault((k['device'], k['os']), []).append(k['version'])
            core_prints.setdefault((k['device'], k['os'], k['version']), k.get('footprint'))
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

    # the release's thumbnails, checked: the pins they go in with when they pass
    pins = {}
    for n in sorted(pictures)[:thumbs.MAX_PICTURES]:
        picture = pictures[n]
        try:
            data = (get(api + '%s/contents/%s?ref=%s' % (repo, urllib.parse.quote(picture['path']), commit),
                        'application/vnd.github.raw', thumbs.MAX_BYTES) if commit
                    else get('https://github.com/%s/releases/download/%s/%s' % (repo, tag, n), limit=thumbs.MAX_BYTES))
        except ValueError:
            r['thumbs'][n] = {'ok': False, 'reason': 'it is larger than %d bytes' % thumbs.MAX_BYTES}
            continue
        except urllib.error.URLError as e:
            r['thumbs'][n] = {'ok': False, 'reason': 'it could not be downloaded (%s)' % e}
            continue
        r['thumbs'][n] = thumbs.check(data)
        if picture.get('digest') and picture['digest'] != 'sha256:' + r['thumbs'][n]['sha256']:
            r['thumbs'][n] = {'ok': False, 'reason': 'it is not the file GitHub lists'}
        elif r['thumbs'][n]['ok']:
            pins[n] = dict(commit=commit, path=picture['path']) if commit else dict(tag=tag, file=n)
            pins[n]['sha256'] = r['thumbs'][n]['sha256']
    for n in sorted(pictures)[thumbs.MAX_PICTURES:]:
        r['thumbs'][n] = {'ok': False, 'reason': 'one submission takes at most %d pictures; send the rest in another'
                                                 % thumbs.MAX_PICTURES}

    def picture_for(mod_id):
        """-> (name, pin or None): the mod's own picture if the release has one (None when it failed), or else the
        release's thumb.png."""
        own = 'thumb-%s.png' % mod_id
        if own in r['thumbs']:
            return own, pins.get(own)
        return thumbs.NAME, pins.get(thumbs.NAME)

    def thumb_for(entry, old, title, mod_id):
        """The thumbnail an entry goes in with: the release's when it passes (through the form; with --update only
        when it is the picture the entry has already), or else the one the entry it replaces has."""
        n, th = picture_for(mod_id)
        had = (old or {}).get('thumb')
        if th and had and had['sha256'] == th['sha256']:
            entry['thumb'] = th
            r['thumb_on'].setdefault(n, {}).setdefault('kept', set()).add(title)
        elif th and not update:
            entry['thumb'] = th
            r['thumb_new'].append(title)
            r['thumb_on'].setdefault(n, {}).setdefault('new', set()).add(title)
        else:
            if th:
                r['thumb_waits'].append(title)
                r['thumb_on'].setdefault(n, {}).setdefault('waits', set()).add(title)
            if had:
                entry['thumb'] = had

    tmp = tempfile.mkdtemp()
    try:
        files, aside, listed_cores = [], [], []   # the files to decide on; those "Only these files" left out; listed cores
        for a in assets:
            row = {'file': a['name'], 'path': a.get('path'), 'result': ''}
            r['rows'].append(row)
            try:
                data = (get(api + '%s/contents/%s?ref=%s' % (repo, urllib.parse.quote(a['path']), commit),
                            'application/vnd.github.raw') if commit
                        else get('https://github.com/%s/releases/download/%s/%s' % (repo, tag, a['name'])))
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
            listed = by_name.get(a['name'])
            if listed and listed['sha256'] == sha:     # the very file (whatever licence it names, the shop has one)
                row['result'] = 'already in the shop'
                if f['core']:
                    listed_cores.append(row)           # still the core the release's mods were built with
                continue
            licensed = f['license'] in SHOP_LICENCES
            if only and a['name'] not in only and not f['core']:   # cores and required mods may still come along
                row['result'] = 'left out: not in "Only these files"'
                if licensed:
                    aside.append(row)
                continue
            if not licensed:
                row['result'] = ("refused: licence '%s' does not allow the site to pass it on" % f['license']
                                 if f['license'] else 'refused: it names no licence; give the repository\'s in "Licence"')
                continue
            files.append(row)
        cores = {(f['device'], f['os']): f for f in files + listed_cores if f['core']}
        ours = same_repo(repo) in CORE_REPOS
        release_mods = {key(f): f for f in files + aside if not f['core']}

        # the mods: each decided on its own, then what each requires (each file once)
        queue, taken = [f for f in files if not f['core']], []
        seen = {f['file'] for f in queue}
        while queue:
            f = queue.pop(0)
            if f['file'] in by_name:
                f['result'] = 'refused: the shop lists another file by this name'
                continue
            # the shop's entries of this mod for this device: another repository's (any OS: the mod is theirs on this
            # device) clash; this repository's for this OS are what the file replaces
            same = [(it, known[name(it)]) for it in items if name(it) in known and not known[name(it)]['core']
                    and (known[name(it)]['id'], known[name(it)]['device']) == (f['id'], f['device'])]
            f['clash'] = [it for it, k in same if same_repo(it['repo']) != same_repo(repo)]
            f['older'] = [it for it, k in same if same_repo(it['repo']) == same_repo(repo) and k['os'] == f['os']]
            if f['clash'] and update:
                f['result'] = 'left out: the shop takes %s from %s' % (f['id'], f['clash'][0]['repo'])
                continue
            if f['clash'] and f['file'] not in only:
                f['result'] = ('flagged, left out: the shop has %s from %s for %s; name this file in "Only these files" '
                               'to add a second build anyway' % (f['id'], f['clash'][0]['repo'], f['device']))
                continue
            # what it updates: the entry for the same OS, or else this repository's entry for another OS
            mine = own_entries.get((f['id'], f['device']), [])
            f['update_of'] = (f['older'] or mine or [None])[0]
            if update and f['update_of'] is None:
                f['result'] = 'left out: not in the shop yet; submit it with the Submit a mod form'
                continue
            if update and f['update_of'].get('auto_update') is False:
                f['result'] = 'left out: its author turned automatic updates off; send it through the Submit a mod form'
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
            entry = dict(repo=repo, commit=commit, path=f['path']) if commit else dict(repo=repo, tag=tag, file=f['file'])
            entry['device'] = f['device']
            if f['held']:                              # a held entry's update (through the form) stays held
                entry['hold'] = f['held']
            if off and not update and not commit:      # opted out (an automatic update never is: it would be left out)
                entry['auto_update'] = False
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
            thumb_for(entry, old, f['title'], f['id'])
            at = items.index(f['older'][0]) if f['older'] else None       # an update takes the old entry's place
            for it in f['older']:
                items.remove(it)
            f['result'] = (('replaces ' + ', '.join(name(it) for it in f['older'])) if f['older']
                           else 'added, beside %s from %s' % (f['id'], f['clash'][0]['repo']) if f['clash']
                           else 'added: %s needs it' % f['brought_by'] if f.get('brought_by') else 'added')
            f['notes'] = heads_up(f, known, site_cores, core_prints)
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
                pin = dict(repo=repo, commit=commit, path=c['path']) if commit else dict(repo=repo, tag=tag, file=c['file'])
                new.append((dict(pin, kind='core', device=device, sha256=c['sha256']), None))
                c['result'] = 'added: the core this release\'s mods need'
        flips = []                                 # this repository's files the form lists already: the setting alone changes
        for row in r['rows'] if not update else []:
            it = by_name.get(row['file'])
            if (row['result'] == 'already in the shop' and not row.get('core') and it
                    and same_repo(it['repo']) == same_repo(repo) and (it.get('auto_update') is False) != off):
                if off:
                    it['auto_update'] = False
                else:
                    del it['auto_update']
                flips.append(it)
                row['result'] = 'auto-update turned %s (already in the shop)' % ('off' if off else 'on')
        for row in r['rows'] if pins and not update else []:   # a new picture for files the shop has already
            it = by_name.get(row['file'])
            n, th = picture_for(row.get('id'))
            if (th and (row['result'].endswith('already in the shop)') or row['result'] == 'already in the shop')
                    and not row.get('core') and it and same_repo(it['repo']) == same_repo(repo)
                    and (it.get('thumb') or {}).get('sha256') != th['sha256']):
                it['thumb'] = th
                r['thumb_new'].append(row.get('title', row['file']))
                r['thumb_on'].setdefault(n, {}).setdefault('new', set()).add(row.get('title', row['file']))
                if it not in flips:
                    flips.append(it)
                row['result'] = ('thumbnail changed (already in the shop)' if row['result'] == 'already in the shop'
                                 else 'thumbnail changed; ' + row['result'])
        if new or flips:
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
            r['auto_off'] = None if update else off
            r['titles'] = sorted({f.get('title', f['file']) for f in r['rows']
                                  if f['result'].startswith(('added', 'replaces', 'auto-update', 'thumbnail'))})
            # every change a newer file of a mod this repository already has: no new mod, no clash, no core, none held,
            # and no new picture (a maintainer looks at each)
            r['same_repo_update'] = (all(f['update_of'] is not None and not f['clash'] and not f['held'] for f in taken)
                                     and len(new) == len(taken) and not r['thumb_new'])
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


def remove(fields, catalog=CATALOG, files_dir=None):
    """The "Remove a mod" form: the shop's entries from one repository (all, or those "Only these files" names) leave
    catalog.json. Cores stay (a maintainer removes those). The report's own_only says whether nothing else is
    touched: no held entry, and no other shop mod for the same device and OS needs a mod that goes.
    -> (status, report): 0 entries removed, 1 refused, 2 nothing to remove."""
    r = {'repo': '', 'tag': '', 'rows': [], 'errors': [], 'changed': False, 'removal': True, 'own_only': False}
    repo = re.sub(r'^https://github\.com/|/$|\.git$', '', fields.get('repo', '').strip())
    only = [f.strip() for f in re.split(r'[\s,]+', fields.get('files', '')) if f.strip()]
    r['repo'], r['why'] = repo, ' '.join(fields.get('why', '').split())[:600]
    if not REPO.fullmatch(repo):
        r['errors'].append('The repository is owner/name, as on GitHub (for example irpina/digislicer).')
        return 1, r
    try:                                           # its name now, if it moved (a deleted one keeps the name given)
        repo = r['repo'] = json.loads(get('https://api.github.com/repos/' + repo, 'application/vnd.github+json',
                                          2 << 20)).get('full_name') or repo
    except (urllib.error.URLError, ValueError):
        pass
    with open(catalog, encoding='utf-8') as fh:
        doc = json.load(fh)
    items = doc['items']
    name = lambda it: it['file'] if 'file' in it else it['path'].rsplit('/', 1)[-1]
    mine = [it for it in items if same_repo(it['repo']) in (same_repo(repo), same_repo(fields.get('repo', '').strip()))]
    missing = [f for f in only if f not in {name(it) for it in mine}]
    if not mine or missing:
        r['errors'].append('The shop lists no %s from %s.' % (', '.join(missing) if missing else 'files', repo))
        return 1, r
    known = {}
    for it in items:
        p = os.path.join(files_dir, name(it)) if files_dir else ''
        if p and os.path.exists(p):
            try:
                known[name(it)] = facts(p)
            except (OSError, elemod.ModError):
                pass
    going = []
    for it in mine:
        row = dict(known.get(name(it), {}), file=name(it), result='')
        row.pop('notes', None)
        r['rows'].append(row)
        if only and name(it) not in only:
            row['result'] = 'stays: not in "Only these files"'
        elif it.get('kind') == 'core':
            row['result'] = 'stays: a maintainer removes cores'
        else:
            going.append(it)
            row['result'] = 'removed' + (' (held: %s)' % it['hold'] if isinstance(it.get('hold'), str) else
                                         ' (held)' if it.get('hold') else '')
    if not going:
        return 2, r
    left = [it for it in items if it not in going]
    gone = {(known[name(it)]['id'], known[name(it)]['device'], known[name(it)]['os']) for it in going if name(it) in known}
    still = {(k['id'], k['device'], k['os']) for n, k in known.items() if n in {name(it) for it in left}}
    needed = []
    for it in left:
        k = known.get(name(it))
        for req in (k or {}).get('requires', []):
            if (req, k['device'], k['os']) in gone - still:
                needed.append('%s %s from %s needs %s' % (k['title'], k['version'], it['repo'], req))
    for row in r['rows']:
        if row['result'].startswith('removed') and row.get('id'):
            who = [n for n in needed if n.endswith(' needs ' + row['id'])]
            if who:
                row['result'] += '; ' + ', '.join(sorted(set(who)))
    doc['items'] = left
    out = render(doc)
    with open(catalog, 'w', encoding='utf-8', newline='\n') as fh:
        fh.write(out)
    r['changed'] = True
    r['titles'] = sorted({row.get('title') or row['file'] for row in r['rows'] if row['result'].startswith('removed')})
    r['own_only'] = not needed and not any(it.get('hold') for it in going)
    return 0, r


INLINE = 24000          # the most picture text a report carries itself (GitHub takes 65536 characters a comment)
PART = 60000            # the most a comment of pictures carries


def previews(r):
    """The new pictures' pixels as text, in parts of at most PART characters, each part marked with the set's
    hash (so a workflow posts a set once)."""
    blocks = []
    for n, t in sorted(r.get('thumbs', {}).items()):
        if t['ok'] and r['thumb_on'].get(n, {}).get('new'):
            blocks.append('`%s`, for %s (%d bytes, sha256 `%s`):\n\n```text\n%s\n```\n' % (
                n, ', '.join(sorted(r['thumb_on'][n]['new'])), t['bytes'], t['sha256'][:12], thumbs.braille(t['bits'])))
    parts, cur = [], ''
    for b in blocks:
        if cur and len(cur) + len(b) > PART - 200:
            parts.append(cur)
            cur = ''
        cur += b + '\n'
    if cur:
        parts.append(cur)
    mark = hashlib.sha256(''.join(parts).encode()).hexdigest()[:16]
    if len(parts) == 1 and len(parts[0]) <= INLINE:
        return parts
    return ['<!-- shop-pictures %s -->\n**New thumbnails, part %d of %d**\n\n%s' % (mark, i + 1, len(parts), p)
            for i, p in enumerate(parts)]


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
    if r.get('thumbs'):
        lines += ['', '**Thumbnails.**', '']
        for n, t in sorted(r['thumbs'].items()):
            on = r['thumb_on'].get(n, {})
            uses = [('new on ', 'new'), ('the same picture on ', 'kept'), ('waits, for ', 'waits')]
            said = '; '.join(w + ', '.join(sorted(on[k])) for w, k in uses if on.get(k))
            if not t['ok']:
                said = 'not taken: ' + t['reason']
            elif not said:
                said = ('not used: no mod this submission adds or changes has the id %s' % n[len('thumb-'):-len('.png')]
                        if n != thumbs.NAME else 'not used: no mod this submission adds or changes is without its own')
            lines.append('- `%s`: %s' % (n, said))
        if r.get('thumb_new'):
            parts = previews(r)
            lines += ['', 'A maintainer looks at each new picture before it goes in. %s' % (
                'Their pixels, as text (no image is shown here):' if len(parts) == 1 and len(parts[0]) <= INLINE
                else 'They are many: their pixels, as text, are in %d comments on the pull request.' % len(parts)), '']
            if len(parts) == 1 and len(parts[0]) <= INLINE:
                lines.append(parts[0])
        if r.get('thumb_waits'):
            lines += ['', 'An automatic update keeps the shop\'s thumbnail for %s: send this release through the Submit a '
                      'mod form to change it.' % ', '.join(sorted(set(r['thumb_waits']))), '']
    noted = [row for row in r['rows'] if row.get('notes')]
    if noted:
        lines += ['', 'Good to know (not a refusal):']
        lines += ['- `%s`: %s' % (row['file'], '; '.join(row['notes'])) for row in noted]
    lines.append('')
    if r.get('removal'):
        if r.get('why'):
            lines += ['Why, from the issue: ' + r['why'], '']
        lines.append({0: 'These leave the shop in a pull request.' + ('' if r['own_only'] else
                                                                       ' Other shop mods need some of them, or they are '
                                                                       'held: a maintainer decides.'),
                      1: 'Nothing was removed. Edit the issue to fix it, and the check runs again.',
                      2: 'Nothing to remove.'}[status])
        return '\n'.join(lines) + '\n'
    if r.get('stopped'):
        lines += ['This release says `Shop auto-update: off`: its files were not taken, and the shop stops following %s. '
                  'To turn it back on, submit the form again with the "Updates" box unticked.' % r['repo'], '']
    elif status == 0 and r.get('auto_off') is False:
        lines += ['Automatic updates are on: when you publish a newer release, the shop takes its files within a day '
                  '(docs/SHOP.md). To stop, put `Shop auto-update: off` in a release\'s notes, or submit the form again '
                  'with "Updates" ticked.', '']
    elif status == 0 and r.get('auto_off') is True:
        lines += ['Automatic updates are off for these: send each newer release through this form. Submit it again '
                  'with "Updates" unticked to turn them on.', '']
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
    ap.add_argument('--no-auto-update', action='store_true', help='opt these mods out of automatic updates (the '
                                                                   'form\'s "Updates" box)')
    ap.add_argument('--remove', action='store_true', help='the "Remove a mod" form: take the repository\'s files out')
    ap.add_argument('--commit', default='', help='instead of --tag: the commit whose .elemod files to take')
    ap.add_argument('--report', help='write the report (Markdown) here')
    ap.add_argument('--result', help='write the result (JSON) here: status, changed, titles, removal, auto_ok (the '
                                     'change touches only the repository\'s own entries: an update, or a removal)')
    ap.add_argument('--previews', help='a folder: the new pictures\' pixels, when too many for the report, as '
                                       'previews-<n>.md, one pull request comment each')
    a = ap.parse_args(argv)
    if a.issue_body:
        with open(a.issue_body, encoding='utf-8') as fh:
            fields = parse_issue(fh.read())
    else:
        fields = {'repo': a.repo or '', 'tag': a.tag or '', 'commit': a.commit, 'summary': a.summary,
                  'on_unit': a.on_unit, 'license': a.license, 'files': a.files, 'auto_update': False if a.no_auto_update else None}
    if a.remove:
        status, r = remove(fields, a.catalog, a.files_dir)
    else:
        status, r = submit(fields, a.catalog, a.files_dir, a.cores_dir, a.update)
    text = report_md(status, r)
    parts = previews(r) if r.get('thumb_new') else []
    if a.previews and parts and parts[0].startswith('<!-- shop-pictures '):
        os.makedirs(a.previews, exist_ok=True)
        for i, part in enumerate(parts):
            with open(os.path.join(a.previews, 'previews-%d.md' % (i + 1)), 'w', encoding='utf-8') as fh:
                fh.write(part)
    if a.report:
        with open(a.report, 'w', encoding='utf-8') as fh:
            fh.write(text)
    if a.result:
        with open(a.result, 'w', encoding='utf-8') as fh:
            json.dump({'status': status, 'changed': r['changed'], 'repo': r['repo'], 'tag': r['tag'],
                       'titles': r.get('titles', []), 'removal': bool(r.get('removal')),
                       'auto_ok': bool(r['own_only'] if r.get('removal') else r['same_repo_update'])}, fh)
    sys.stdout.write(text)
    return status


if __name__ == '__main__':
    sys.exit(main())
