# SPDX-License-Identifier: GPL-2.0-or-later
"""Publish a mod: build it for each stock OS file, put the files in a GitHub release, and send it to the shop.

    python -m elekloader.sdk.publish MODDIR --stock Digitakt_OS1.53.syx [--stock Digitakt_OS1.54.syx ...]
        [--repo owner/name] [--tag v1.0] [--prerelease] [--summary "..."] [--tested "..."]
        [--dry-run] [--no-browser]

1. It builds MODDIR (elekloader.sdk.build) against each stock file, into MODDIR/out. The stock files stay on your
   machine: only the built .elemod files are published.
2. It creates the release with GitHub's command line tool: `gh release create TAG FILES`, tag v<version> unless
   --tag says otherwise, in the repository --repo names or MODDIR's git remote "origin" points at. --summary and
   --tested go in the release's notes as "Shop summary:" and "Tested on a unit:" lines, which the shop takes for
   the mod's card (docs/SHOP.md).
3. If the shop lists the mod already, the shop's daily check takes the release by itself (unless you opted out:
   --no-auto-update writes "Shop auto-update: off" in the notes, and the form is how a release gets in then). If
   not, it opens the Submit a mod form in your browser, filled in with the repository, the tag and the summary.

--dry-run builds, then prints the gh command and the form's address, and publishes nothing.
"""
import argparse
import json
import os
import re
import shutil
import sys
import tempfile
import urllib.parse
import urllib.request

from . import build as sdk_build

SHOP = os.environ.get('ELEKLOADER_SHOP_REPO') or 'elekloader/shop'        # the repository with the Submit a mod form
INDEX_URL = 'https://elekloader.github.io/shop/shop/index.json'              # what each of the shop's files is


def repo_from_remote(url):
    """owner/name from a GitHub remote (https or ssh), or None."""
    m = re.match(r'^(?:https://github\.com/|git@github\.com:|ssh://git@github\.com/)([\w.-]+/[\w.-]+?)(?:\.git)?/?$',
                 (url or '').strip())
    return m.group(1) if m else None


def release_notes(summary='', tested='', auto_update=True):
    """The release's notes: the lines the shop reads for the card, and whether to follow the repository's newer
    releases (packaging/submit_mod.py, release_text)."""
    lines = ['Shop summary: ' + ' '.join(summary.split())] if summary.strip() else []
    lines += ['Tested on a unit: ' + ' '.join(tested.split())] if tested.strip() else []
    lines += [] if auto_update else ['Shop auto-update: off']
    return '\n'.join(lines) + '\n' if lines else 'Built with elekloader.\n'


def form_url(repo, tag, title, summary=''):
    """The shop's Submit a mod form, filled in (GitHub fills an issue form's fields from the address)."""
    q = {'template': 'submit-mod.yml', 'title': 'Submit a mod: ' + title, 'repo': repo, 'tag': tag}
    if summary:
        q['summary'] = ' '.join(summary.split())[:300]
    return 'https://github.com/%s/issues/new?%s' % (SHOP, urllib.parse.urlencode(q))


def in_shop(items, repo, mod_id):
    """Whether the shop takes a file of this mod from this repository (items: the site's shop/index.json)."""
    return any(it.get('repo', '').lower() == repo.lower() and it.get('id') == mod_id for it in items)


def shop_items():
    """The site's shop/index.json, or None when it can't be read."""
    try:
        with urllib.request.urlopen(urllib.request.Request(INDEX_URL, headers={'User-Agent': 'elekloader-publish'}),
                                    timeout=30) as r:
            items = json.load(r)
        return items if isinstance(items, list) else None
    except (OSError, ValueError):
        return None


def main(argv=None):
    import subprocess
    import webbrowser
    ap = argparse.ArgumentParser(prog='elekloader.sdk.publish', description=__doc__.split('\n')[0])
    ap.add_argument('moddir', help='the folder with mod.json')
    ap.add_argument('--stock', action='append', default=[], help='a stock .syx to build for; give one per OS version')
    ap.add_argument('--repo', help='the GitHub repository (default: MODDIR\'s git remote "origin")')
    ap.add_argument('--tag', help='the release tag (default: v<version>)')
    ap.add_argument('--prerelease', action='store_true', help='publish it as a pre-release (a beta)')
    ap.add_argument('--summary', default='', help='one sentence for the shop\'s card')
    ap.add_argument('--tested', default='', help='what ran on hardware, and what not yet')
    ap.add_argument('--no-auto-update', action='store_true', help='tell the shop not to take your later releases by '
                                                                   'itself ("Shop auto-update: off" in the notes)')
    ap.add_argument('--dry-run', action='store_true', help='build, then print what would happen; publish nothing')
    ap.add_argument('--no-browser', action='store_true', help='print the form\'s address instead of opening it')
    a = ap.parse_args(argv)
    if not a.stock:
        ap.error('give --stock at least once')
    with open(os.path.join(a.moddir, 'mod.json'), encoding='utf-8') as fh:
        mod = json.load(fh)
    mod_id, version = str(mod['id']), str(mod['version'])
    title = str(mod.get('title') or mod_id)
    summary = a.summary or ' '.join(str(mod.get('description', '')).split())[:300]
    repo = a.repo
    if not repo:
        try:
            url = subprocess.run(['git', '-C', a.moddir, 'remote', 'get-url', 'origin'], capture_output=True,
                                 text=True, check=True).stdout
            repo = repo_from_remote(url)
        except (OSError, subprocess.CalledProcessError):
            repo = None
    if not repo:
        ap.error('give --repo owner/name (MODDIR has no GitHub remote "origin")')
    tag = a.tag or 'v' + version

    files = []
    for stock in a.stock:
        try:
            path, m = sdk_build.build(a.moddir, stock, None)
        except (sdk_build.BuildError, OSError, KeyError, ValueError) as e:
            print('BUILD FAILED for %s: %s' % (os.path.basename(stock), e))
            return 1
        print('BUILT %s (%s %s)' % (path, m.dev.name, m.rel.version))
        files.append(path)

    notes = tempfile.NamedTemporaryFile('w', suffix='.md', delete=False, encoding='utf-8')
    notes.write(release_notes(a.summary, a.tested, not a.no_auto_update))
    notes.close()
    cmd = (['gh', 'release', 'create', tag] + files + ['--repo', repo, '--title', '%s %s' % (title, version),
                                                       '--notes-file', notes.name]
           + (['--prerelease'] if a.prerelease else []))
    try:
        if a.dry_run or not shutil.which('gh'):
            print('%s:\n  %s' % ('Would run' if a.dry_run else 'GitHub\'s command line tool (gh) is not installed; '
                                 'create the release with these files, or install gh and run', ' '.join(
                                     '"%s"' % c if ' ' in c else c for c in cmd)))
        else:
            subprocess.run(cmd, check=True)
            print('RELEASED %s %s' % (repo, tag))
    except subprocess.CalledProcessError:
        print('gh could not create the release (does the tag exist already?)')
        return 1
    finally:
        os.unlink(notes.name)

    items = shop_items()
    if items is not None and in_shop(items, repo, mod_id):
        print('The shop lists %s from %s: %s' % (title, repo, (
            'you opted out, so send this release through the Submit a mod form (docs/SHOP.md).' if a.no_auto_update else
            'its daily check takes this release by itself, unless you opted out earlier (docs/SHOP.md).')))
        if not a.no_auto_update:
            return 0
    url = form_url(repo, tag, title, summary)
    print('To put it in the shop, send the Submit a mod form:\n  ' + url)
    if not (a.dry_run or a.no_browser):
        webbrowser.open(url)
    return 0


if __name__ == '__main__':
    sys.exit(main())
