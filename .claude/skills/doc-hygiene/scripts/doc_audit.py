# SPDX-License-Identifier: GPL-2.0-or-later
"""Where elekloader's markdown is growing, and what a move would break.

    python .claude/skills/doc-hygiene/scripts/doc_audit.py [--since REF] [--detail FILE] [--strict]

Run from the repository's root. It reports:

- **over budget**: files past their size budget (BUDGETS: a prompt to look,
  not a gate);
- **long sections**: headings whose text, with their subsections, is past
  SECTION_KB;
- **history phrasing**: "From 3.2 ...", "0.2 took ...", "no longer", PR
  numbers and dates, which belong in release notes and PRs, not in text
  that says what is true now;
- **repeated paragraphs**: the same explanation in more than one place;
- **broken links**: relative links to a missing file or anchor;
- **broken section references**: `ADAPTING.md, "Exclusive audio"` style
  references, in the docs and in the code (headers, messages), whose title
  is in no heading of that file. Moving a section breaks these silently.

`--since REF` adds what changed since a git ref (a branch's own growth:
`--since origin/main`). `--detail FILE` lists that file's sections with
their sizes and history phrasing, line by line. `--strict` exits 1 when a
link or a section reference is broken.
"""
import argparse
import collections
import fnmatch
import os
import re
import subprocess
import sys

BUDGETS = [  # (path pattern, KB); the first match counts
    ('AGENTS.md', 8),            # read into every coding agent's context
    ('CLAUDE.md', 1),
    ('README.md', 8),            # the front page
    ('CONTRIBUTING.md', 4),
    ('docs/README.md', 8),       # the wiki's home: an index
    ('docs/*.md', 40),
    ('mods/*/README.md', 16),
    ('examples/*/README.md', 6),
    ('.claude/skills/*', 20),
    ('*', 16),
]
SECTION_KB = 8
SKIP_DIRS = {'.git', 'node_modules', 'build', 'dist', 'out', '__pycache__', '.venv'}
CODE_EXT = ('.py', '.ts', '.js', '.mjs', '.c', '.h', '.s', '.inc', '.json', '.yml')

HISTORY = [
    (r'\b[Ff]rom (?:core[\w-]* )?\d+\.\d+(?:\.\d+)?\b', 'from X.Y'),
    (r'\b\d+\.\d+(?:\.\d+)?(?: to \d+\.\d+(?:\.\d+)?)? (?:took|had|gave|moved|did|was|were|used)\b', 'X.Y did'),
    (r'\b[Cc]ore[\w-]* \d+\.\d+(?:\.\d+)? (?:adds|added|includes|brings|gains|gained)\b', 'core X.Y adds'),
    (r'\b(?:[Cc]hecked|[Vv]erified|[Tt]ested) (?:in|on|with|by|against|the same way)\b', 'check log'),
    (r'\bpass(?:es|ed)\b.{0,60}\b(?:emulator|on a unit|on hardware|on the unit)\b', 'check log'),
    (r'\b(?:no longer|used to|any more|anymore)\b', 'no longer'),
    (r'\((?:it |they )?was [^)]{1,40}\)', '(was ...)'),
    (r'(?<![\w/&])#\d{2,4}\b', 'PR #'),
    (r'\b20\d\d-\d\d-\d\d\b', 'date'),
]
HISTORY_RE = [(re.compile(p), name) for p, name in HISTORY]

LINK_RE = re.compile(r'!?\[[^\]]*\]\(([^)\s]+)(?:\s+"[^"]*")?\)')
# `ADAPTING.md, "Exclusive audio"`, and `"Mod storage" in ADAPTING.md`; matched over whole
# paragraphs, so a reference may wrap
SECREF_RES = [
    (re.compile(r'([\w./-]*\b[\w-]+\.md)\)?,?\s+"([^"]{2,80})"'), 1, 2),
    (re.compile(r'"([^"]{2,80})"\s+(?:in|of)\s+(?:\[[^\]]*\]\()?([\w./-]*\b[\w-]+\.md)'), 2, 1),
]
COMMENT_RE = re.compile(r'^\s*(?:/\*+|\*+/?|//+|#+|;+)?\s?')


def blocks(lines):
    """Paragraphs of (line number, text): (first line, joined text, each line's offset in it)."""
    out, text, offs, start = [], '', [], None
    for n, line in list(lines) + [(None, '')]:
        if line.strip():
            if start is None:
                start, text, offs = n, '', []
            offs.append(len(text))
            text += line.strip() + ' '
        elif start is not None:
            out.append((start, text, offs))
            start = None
    return out


def md_files(root):
    out = []
    for d, dirs, files in os.walk(root):
        dirs[:] = sorted(x for x in dirs if x not in SKIP_DIRS and not x.endswith('-workspace'))
        for f in sorted(files):
            if f.endswith('.md'):
                out.append(os.path.relpath(os.path.join(d, f), root).replace('\\', '/'))
    return out


def code_files(root):
    out = []
    for top in ('elekloader', 'js/src', 'js/tools', 'web', 'mods', 'packaging', 'tests'):
        for d, dirs, files in os.walk(os.path.join(root, top)):
            dirs[:] = [x for x in dirs if x not in SKIP_DIRS]
            for f in files:
                if f.endswith(CODE_EXT):
                    out.append(os.path.relpath(os.path.join(d, f), root).replace('\\', '/'))
    return out


def budget(path):
    for pat, kb in BUDGETS:
        if fnmatch.fnmatch(path, pat):
            return kb
    return None


def slug(title):
    """GitHub's anchor for a heading."""
    t = re.sub(r'\[([^\]]*)\]\([^)]*\)', r'\1', title).strip().lower()
    t = re.sub(r'[^\w\- ]', '', t)
    return t.replace(' ', '-')


class Doc:
    def __init__(self, root, path):
        self.path = path
        with open(os.path.join(root, path), encoding='utf-8') as f:
            self.text = f.read()
        self.lines = self.text.splitlines()
        self.size = len(self.text.encode('utf-8'))
        self.prose = []      # (line number, text) outside code fences
        self.headings = []   # (line number, level, title)
        self.labels = []     # bold run-in labels that start a paragraph: "**DSP code (the Octatrack).**"
        fence = None
        for n, line in enumerate(self.lines, 1):
            m = re.match(r'\s*(```+|~~~+)', line)
            if m:
                mark = m.group(1)[:3]
                fence = None if fence == mark else (fence or mark)
                continue
            if fence:
                continue
            self.prose.append((n, line))
            h = re.match(r'(#{1,6})\s+(.*?)\s*#*\s*$', line)
            if h:
                self.headings.append((n, len(h.group(1)), h.group(2)))
            b = re.match(r'(?:[-*] )?\*\*([^*]+)\*\*', line)
            if b:
                self.labels.append(b.group(1))
        # blocks a tool writes between <!-- name: generated ... --> and <!-- /name -->
        self.generated = sum(len(m.group(0).encode('utf-8')) for m in re.finditer(
            r'<!-- ([\w-]+): generated.*?<!-- /\1 -->', self.text, re.S))
        self.anchors = set()
        seen = collections.Counter()
        for _, _, title in self.headings:
            s = slug(title)
            self.anchors.add(s if not seen[s] else f'{s}-{seen[s]}')
            seen[s] += 1
        for m in re.finditer(r'<a\s+(?:name|id)="([^"]+)"', self.text):
            self.anchors.add(m.group(1))

    def sections(self):
        """(line, level, title, end line, bytes, own bytes) for each heading: bytes with its
        subsections, own bytes up to its first subsection."""
        out = []
        for i, (n, level, title) in enumerate(self.headings):
            end = len(self.lines) + 1
            for m, lv, _ in self.headings[i + 1:]:
                if lv <= level:
                    end = m
                    break
            own = self.headings[i + 1][0] if i + 1 < len(self.headings) else end
            size = lambda a, b: len('\n'.join(self.lines[a - 1:b - 1]).encode('utf-8'))
            out.append((n, level, title, end, size(n, end), size(n, min(own, end))))
        return out

    def history(self, lo=1, hi=None):
        hits = []
        for n, line in self.prose:
            if n < lo or (hi and n >= hi):
                continue
            names = [name for rx, name in HISTORY_RE if rx.search(line)]
            if names:
                hits.append((n, names, line.strip()))
        return hits

    def paragraphs(self):
        out, cur, start = [], [], 0
        for n, line in self.prose + [(None, '')]:
            if line.strip() and not line.lstrip().startswith(('|', '#')):
                if not cur:
                    start = n
                cur.append(line.strip())
            elif cur:
                p = ' '.join(cur)
                if len(p) >= 160:
                    out.append((start, p))
                cur = []
        return out


def shingles(text, k=8):
    w = re.findall(r'\w+', text.lower())
    return {' '.join(w[i:i + k]) for i in range(max(0, len(w) - k + 1))}


def repeated(docs, limit):
    items = [(d.path, n, p, shingles(p)) for d in docs for n, p in d.paragraphs()]
    index = collections.defaultdict(list)
    for i, it in enumerate(items):
        for s in it[3]:
            index[s].append(i)
    shared = collections.Counter()
    for ids in index.values():
        if 1 < len(ids) <= 12:
            for a in range(len(ids)):
                for b in range(a + 1, len(ids)):
                    shared[(ids[a], ids[b])] += 1
    pairs = []
    for (a, b), n in shared.items():
        overlap = n / max(1, min(len(items[a][3]), len(items[b][3])))
        if overlap >= 0.6:
            pairs.append((overlap, items[a], items[b]))
    pairs.sort(key=lambda x: (-x[0], x[1][0], x[1][1]))
    by_file = collections.Counter(tuple(sorted({a[0], b[0]})) for _, a, b in pairs)
    return by_file, pairs[:limit]


def resolve(root, src, target):
    """A link or reference's file, relative to the repository's root, or None."""
    for base in (os.path.dirname(src), ''):
        p = os.path.normpath(os.path.join(base, target)).replace('\\', '/')
        if os.path.exists(os.path.join(root, p)):
            return p
    return None


def check_links(root, docs, by_path):
    broken = []
    for d in docs:
        for n, line in d.prose:
            for m in LINK_RE.finditer(re.sub(r'`[^`]*`', '', line)):
                url = m.group(1)
                if re.match(r'[a-z]+:', url) or url.startswith('//'):
                    continue
                path, _, anchor = url.partition('#')
                target = resolve(root, d.path, path) if path else d.path
                if target is None:
                    broken.append((d.path, n, url, 'no such file'))
                elif anchor and target.endswith('.md'):
                    t = by_path.get(target)
                    if t and anchor.lower() not in t.anchors:
                        broken.append((d.path, n, url, 'no such heading'))
    return broken


def norm(title):
    """A title as a reference may shorten it: "Firmware locations (core-dn1 3.0)" names
    the heading "Firmware locations (core-dn1 3.0, Digitone mk1)"."""
    return re.sub(r'[`*)]', '', title).strip(' .:').lower()


def check_secrefs(root, docs, by_path, codes):
    by_name = collections.defaultdict(list)
    for d in docs:
        by_name[os.path.basename(d.path)].append(d.path)
    broken, count = [], 0

    def scan(src, lines):
        nonlocal count
        for start, text, offs in blocks(lines):
            for rx, fi, ti in SECREF_RES:
                for m in rx.finditer(text):
                    ref, title = m.group(fi), m.group(ti)
                    target = resolve(root, src, ref)
                    if target is None:
                        named = by_name.get(os.path.basename(ref), [])
                        target = named[0] if len(named) == 1 else None
                    t = by_path.get(target)
                    if t is None:
                        continue
                    count += 1
                    want = norm(title)
                    if not any(want in norm(h) for h in [h for _, _, h in t.headings] + t.labels):
                        n = start + sum(1 for o in offs if o <= m.start()) - 1
                        broken.append((src, n, f'{ref}, "{title}"'))

    for d in docs:
        scan(d.path, d.prose)
    for c in codes:
        try:
            with open(os.path.join(root, c), encoding='utf-8') as f:
                lines = f.read().splitlines()
        except (UnicodeDecodeError, OSError):
            continue
        scan(c, [(n, COMMENT_RE.sub('', line, count=1)) for n, line in enumerate(lines, 1)])
    return count, broken


def since(root, ref):
    try:
        out = subprocess.run(['git', 'diff', '--numstat', ref, '--', '*.md'], cwd=root,
                             capture_output=True, text=True, check=True).stdout
    except (subprocess.CalledProcessError, FileNotFoundError) as e:
        return None, str(e)
    rows = []
    for line in out.splitlines():
        a, r, p = line.split('\t', 2)
        if a != '-':
            rows.append((int(a) - int(r), int(a), int(r), p))
    new = subprocess.run(['git', 'ls-files', '--others', '--exclude-standard', '--', '*.md'],
                         cwd=root, capture_output=True, text=True).stdout
    for p in new.splitlines():
        with open(os.path.join(root, p), encoding='utf-8') as f:
            n = len(f.read().splitlines())
        rows.append((n, n, 0, p + ' (new)'))
    return sorted(rows, reverse=True), None


def kb(n):
    return f'{n / 1024:6.1f}'


def detail(doc):
    print(f'{doc.path}: {kb(doc.size).strip()} KB, {len(doc.lines)} lines, '
          f'{len(doc.history())} lines with history phrasing\n')
    print('   with subsections / own text')
    for n, level, title, end, size, own in doc.sections():
        hist = len(doc.history(n, end))
        flag = '  <- long' if size > SECTION_KB * 1024 else ''
        print(f'  {kb(size)} / {kb(own)} KB  L{n:<5} {"  " * (level - 1)}{title[:70]}'
              f'{f"  [{hist} history]" if hist else ""}{flag}')
    hits = doc.history()
    if hits:
        print('\nHistory phrasing (say what is true now; the history is in the release notes):')
        for n, names, text in hits:
            print(f'  L{n:<5} {",".join(names):<14} {text[:110]}')


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--root', default='.')
    ap.add_argument('--since', help='a git ref: also show what each file gained since it')
    ap.add_argument('--detail', help='one markdown file: its sections and history phrasing')
    ap.add_argument('--strict', action='store_true', help='exit 1 on a broken link or section reference')
    ap.add_argument('--top', type=int, default=12, help='rows per list (default 12)')
    args = ap.parse_args()
    if hasattr(sys.stdout, 'reconfigure'):
        sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    root = args.root

    docs = [Doc(root, p) for p in md_files(root)]
    by_path = {d.path: d for d in docs}
    if args.detail:
        p = re.sub(r'^(\./)+', '', args.detail.replace('\\', '/'))
        if p not in by_path:
            sys.exit(f'{args.detail}: not a markdown file under {root}')
        detail(by_path[p])
        return

    total = sum(d.size for d in docs)
    print(f'{len(docs)} markdown files, {total / 1024:.0f} KB, '
          f'{sum(len(d.lines) for d in docs)} lines\n')

    if args.since:
        rows, err = since(root, args.since)
        print(f'Since {args.since}:' + (f' {err}' if err else ''))
        for net, a, r, p in (rows or [])[:args.top]:
            print(f'  {net:+6} lines  (+{a} -{r})  {p}')
        print()

    over = [(d.size / 1024 / budget(d.path), d) for d in docs if d.size > budget(d.path) * 1024]
    print('Over budget (KB / budget):')
    for ratio, d in sorted(over, key=lambda x: -x[0]):
        gen = f'  ({d.generated / 1024:.1f} KB of it generated)' if d.generated else ''
        print(f'  {kb(d.size)} / {budget(d.path):<3} x{ratio:4.1f}  {d.path}{gen}')
    if not over:
        print('  none')

    long = [(s[4], d.path, s) for d in docs for s in d.sections()
            if s[4] > SECTION_KB * 1024 and s[1] >= 2]
    long.sort(key=lambda x: -x[0])
    print(f'\nLong sections (over {SECTION_KB} KB, subsections included):')
    for size, path, (n, level, title, *_) in long[:args.top]:
        print(f'  {kb(size)} KB  {path}:{n}  {"#" * level} {title[:70]}')
    if not long:
        print('  none')

    hist = [(len(d.history()), d) for d in docs]
    hist = [(c, d) for c, d in hist if c >= 5]
    hist.sort(key=lambda x: -x[0])
    print('\nHistory phrasing (lines; per 10 KB):')
    for c, d in hist[:args.top]:
        print(f'  {c:4}  {c * 10240 / d.size:5.1f}  {d.path}')
    if not hist:
        print('  none')

    by_file, pairs = repeated(docs, args.top)
    print('\nRepeated paragraphs (60% or more shared), by file pair:')
    for (files, n) in by_file.most_common(args.top):
        print(f'  {n:3}  {"  =  ".join(files)}')
    if pairs:
        print('  e.g.')
        for overlap, a, b in pairs[:5]:
            print(f'    {overlap:.0%} {a[0]}:{a[1]} = {b[0]}:{b[1]}  "{a[2][:70]}..."')
    if not by_file:
        print('  none')

    broken = check_links(root, docs, by_path)
    print(f'\nBroken relative links: {len(broken)}')
    for path, n, url, why in broken[:50]:
        print(f'  {path}:{n}  {url}  ({why})')

    count, refs = check_secrefs(root, docs, by_path, code_files(root))
    print(f'\nSection references by title: {count}, broken: {len(refs)}')
    for path, n, ref in refs[:50]:
        print(f'  {path}:{n}  {ref}')

    if args.strict and (broken or refs):
        sys.exit(1)


if __name__ == '__main__':
    main()
