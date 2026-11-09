#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The shop's mods, kept up to date from their authors' newer releases (.github/workflows/shop-updates.yml).

    python packaging/shop_updates.py --list [--repo owner/name] [--catalog catalog.json]
    python packaging/shop_updates.py --apply owner/name --files-dir build/shop --cores-dir build/core
                                     [--catalog catalog.json] [--report report.md] [--result result.json]

Every mod is followed unless its author opted out (an entry with "auto_update": false: the Submit a mod form's
"Updates" box, or "Shop auto-update: off" in the release's notes).

--list prints each repository with such mods, taken by a release tag, that has newer releases with .elemod files,
one per line. A release is newer when GitHub published it after the earliest release the shop takes from that
repository; a draft is never one, nor a release whose .elemod files are all cores (core-*.elemod).

--apply runs packaging/submit_mod.py's update check (--update) on each newer release of one repository, oldest
first, on the same catalog, so a mod updated twice ends on its newest file. That check takes only newer files of
mods the repository already has in the shop, keeps their cards' text, and leaves out older files, new mods, cores,
held entries and a pre-release over a release. The result's same_repo_update is true when every change is such an
update (what the workflow merges by itself).

Exit 0: the catalog changed; 2: nothing to update; 1: a release could not be read.
"""
import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import submit_mod  # noqa: E402


def shop_repos(items):
    """-> {repository (as compared): (a name to ask GitHub with, {tags})} for the shop's mods pinned to a release whose
    author hasn't opted out of automatic updates ("auto_update": false in the entry)."""
    out = {}
    for it in items:
        if it.get('kind') == 'core' or 'tag' not in it or it.get('auto_update') is False:
            continue
        name, tags = out.setdefault(submit_mod.same_repo(it['repo']), (it['repo'], set()))
        tags.add(it['tag'])
    return out


def newer_releases(repo, tags):
    """-> the tags of repo's releases newer than the earliest of `tags` the shop takes, oldest first."""
    releases = json.loads(submit_mod.get('https://api.github.com/repos/%s/releases?per_page=100' % repo,
                                         'application/vnd.github+json', 8 << 20))
    pinned = [r['published_at'] for r in releases if r['tag_name'] in tags and r.get('published_at')]
    if not pinned:
        return []
    since, out = min(pinned), []
    for r in releases:
        if r.get('draft') or not r.get('published_at') or r['published_at'] <= since or r['tag_name'] in tags:
            continue
        files = [a['name'] for a in r.get('assets', []) if submit_mod.FILE.fullmatch(a['name'])]
        if files and not all(f.startswith('core-') for f in files):
            out.append((r['published_at'], r['tag_name']))
    return [t for _, t in sorted(out)]


def read_items(catalog):
    with open(catalog, encoding='utf-8') as fh:
        return json.load(fh)['items']


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--list', action='store_true', help='print the repositories with newer releases')
    ap.add_argument('--repo', help='with --list: only this repository')
    ap.add_argument('--apply', metavar='REPO', help='update the catalog from this repository\'s newer releases')
    ap.add_argument('--catalog', default=submit_mod.CATALOG)
    ap.add_argument('--files-dir', help='the shop\'s files, as pages.yml downloads them')
    ap.add_argument('--cores-dir', help='the latest release\'s cores, as pages.yml downloads them')
    ap.add_argument('--report', help='write the report (Markdown) here')
    ap.add_argument('--result', help='write the result (JSON) here: changed, same_repo_update, tags, titles')
    a = ap.parse_args(argv)
    repos = shop_repos(read_items(a.catalog))
    if a.list:
        for key, (name, tags) in sorted(repos.items()):
            if a.repo and submit_mod.same_repo(a.repo) != key:
                continue
            try:
                if newer_releases(name, tags):
                    print(name)
            except (submit_mod.urllib.error.URLError, ValueError) as e:
                print('%s: releases not listed (%s)' % (name, e), file=sys.stderr)
        return 0
    if not a.apply:
        ap.error('--list or --apply')
    name, tags = repos.get(submit_mod.same_repo(a.apply), (a.apply, set()))
    reports, changed, same, done, titles, failed = [], False, True, [], [], False
    for tag in newer_releases(name, tags):
        status, r = submit_mod.submit({'repo': name, 'tag': tag}, a.catalog, a.files_dir, a.cores_dir, update=True)
        reports.append(submit_mod.report_md(status, r))
        failed |= bool(r['errors'])
        if r['changed']:
            changed, same = True, same and r['same_repo_update']
            done.append(tag)
            titles += [t for t in r.get('titles', []) if t not in titles]
        if r.get('stopped'):                       # "Shop auto-update: off": the releases after it aren't followed
            break
    text = '\n'.join(reports) or 'No newer releases.\n'
    if a.report:
        with open(a.report, 'w', encoding='utf-8') as fh:
            fh.write(text)
    if a.result:
        with open(a.result, 'w', encoding='utf-8') as fh:
            json.dump({'repo': name, 'changed': changed, 'same_repo_update': changed and same, 'tags': done,
                       'titles': titles}, fh)
    sys.stdout.write(text)
    return 0 if changed else 1 if failed else 2


if __name__ == '__main__':
    sys.exit(main())
