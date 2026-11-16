// SPDX-License-Identifier: GPL-2.0-or-later
// The patcher page's +Drive mods view (web/index.html#drive, web/drive.js) in a browser, against a mocked Web MIDI port, needing no firmware and no unit.
// The mock is a fake unit (tests/test_drive.py's FakeUnit, in JavaScript) holding the made-up build of
// js/test/drive-vectors.json, whose replies are the ones js/tools/drive_vectors.py recorded from `elekloader mods`.
// The patcher's own engine (Pyodide) is not on this test's site: only the view is tested. It connects, reads the build, lists /mods, refuses an upload the checker refuses (nothing is sent), uploads
// the two example mods with the very requests the CLI sent, keeps a mod of the saved build (/mods/active, chosen on the
// unit) from being deleted, and deletes another. Then the folders: it makes one (an empty upload to FOLDER/.dir), uploads
// into it, goes 3 deep, drops
// onto folder rows and the path, finds a shop mod's copy in a folder,
// and deletes an empty folder (DELETE DIR), as the loader's route takes them (mods/drive-loader-ar2/README.md).
//
//   node tests/test_drive_view.mjs      (Playwright with its Chromium; it skips without them)
//   DRIVE_VIEW_SHOT=page.png node tests/test_drive_view.mjs    (and a screenshot, once both mods are uploaded)
import { createRequire } from 'node:module';
import { createServer } from 'node:http';
import { execFileSync } from 'node:child_process';
import { mkdtempSync, readFileSync, rmSync, writeFileSync, mkdirSync, copyFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, extname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = dirname(fileURLToPath(import.meta.url)), ROOT = dirname(HERE);
let chromium;
try { ({ chromium } = createRequire(import.meta.url)('playwright')); } catch {
  console.log('SKIP: Playwright is not installed');
  process.exit(0);
}
const V = JSON.parse(readFileSync(join(ROOT, 'js', 'test', 'drive-vectors.json'), 'utf8'));

let failed = 0;
function check(what, ok, detail = '') {
  console.log(`${ok ? 'ok    ' : 'FAIL  '} ${what}${detail ? ': ' + detail : ''}`);
  if (!ok) failed++;
}

// ---- the site: the page and its engine, as packaging/build_web.py lays them out --------------------------------
const site = mkdtempSync(join(tmpdir(), 'mods-page-'));
for (const n of ['index.html', 'style.css', 'app.js', 'worker.js', 'drive.js']) copyFileSync(join(ROOT, 'web', n), join(site, n));
execFileSync('node', [join(ROOT, 'js', 'tools', 'build.ts'), join(site, 'engine')], { stdio: 'ignore' });
// the vectors' build is a made-up release of the Rytm: the engine learns it as the JS tests do
writeFileSync(join(site, 'engine', 'devices.js'), readFileSync(join(site, 'engine', 'devices.js'), 'utf8')
  + `\nANALOG_RYTM_MK2.releases.push(${JSON.stringify({ ...V.release, binSha256: '' })});\n`);
// a shop with one mod that loads from +Drive and one that only a flashed build can carry
const put = V.exchanges.filter(s => s.op === 'put' && s.args[0] !== 'active');
const hello = { name: put[0].args[0], raw: Buffer.from(put[0].args[1], 'hex') };
const user = { name: put[1].args[0], raw: Buffer.from(put[1].args[1], 'hex') };
const sites = V.cases.find(c => c.name === 'feature: refused').files[0];
mkdirSync(join(site, 'shop'));
writeFileSync(join(site, 'shop', 'sites.elemod'), sites[1]);
writeFileSync(join(site, 'shop', hello.name), hello.raw);
const doc = JSON.parse(hello.raw.toString());
writeFileSync(join(site, 'shop', 'index.json'), JSON.stringify([
  { file: hello.name, available: true, device: doc.target.device, os: doc.target.os, id: doc.id, version: doc.version, title: 'Drive hello' },
  { file: 'sites.elemod', available: true, device: doc.target.device, os: doc.target.os, id: 'drive-hello', version: '1', title: 'Sites' },
]));

const TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css', '.json': 'application/json', '.elemod': 'application/json' };
const server = createServer((req, res) => {
  const p = join(site, decodeURIComponent(new URL(req.url, 'http://x').pathname));
  try {
    const b = readFileSync(p);
    res.writeHead(200, { 'content-type': TYPES[extname(p)] || 'application/octet-stream' });
    res.end(b);
  } catch { res.writeHead(404); res.end(); }
}).listen(0);
const url = `http://127.0.0.1:${server.address().port}/index.html#drive`;

// ---- the mocked unit, in the page ------------------------------------------------------------------------------
function mock(buildHex) {
  const HEAD = [0xf0, 0x00, 0x20, 0x3c, 0x10, 0x00], CHUNK = 0x2000;
  const hexb = h => Uint8Array.from(h.match(/../g) || [], x => parseInt(x, 16));
  const pack7 = d => { const o = []; for (let i = 0; i < d.length; i += 7) { const g = d.slice(i, i + 7); let hi = 0; g.forEach((b, k) => { if (b & 0x80) hi |= 0x40 >> k; }); o.push(hi, ...g.map(b => b & 0x7f)); } return o; };
  const unpack7 = d => { const o = []; for (let i = 0; i < d.length; i += 8) { const hi = d[i]; d.slice(i + 1, i + 8).forEach((b, k) => o.push(b | (hi & (0x40 >> k) ? 0x80 : 0))); } return Uint8Array.from(o); };
  let T = null;
  const crc = (d, start = 0) => {
    if (!T) { T = new Uint32Array(256); for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; T[n] = c >>> 0; } }
    let c = (start ^ 0xffffffff) >>> 0;
    for (const b of d) c = T[(c ^ b) & 0xff] ^ (c >>> 8);
    return (c ^ 0xffffffff) >>> 0;
  };
  const be = (...w) => w.flatMap(([v, n]) => Array.from({ length: n }, (_, k) => Math.floor((v >>> 0) / 2 ** (8 * (n - 1 - k))) & 0xff));
  const u32 = (d, o) => ((d[o] << 24) | (d[o + 1] << 16) | (d[o + 2] << 8) | d[o + 3]) >>> 0;
  const cstr = (d, o) => { let e = o; while (d[e]) e++; return new TextDecoder().decode(d.slice(o, e)); };
  const wrap = body => [...be([0xac11d303, 4]), 2, ...be([2, 2], [5, 2], [0x30313736, 4], [0x454d4f44, 4], [2, 4], [0, 4], [body.length, 4]), 0, 12,
    ...body, ...be([crc(body, 0xffffffff), 4], [body.length, 4], [0xaaa1daaa, 4])];
  // files by their path below /mods; folders too, as the route keeps them (an upload makes the ones it needs, and
  // an upload to FOLDER/.dir makes FOLDER and writes no file)
  const files = { '.build': [...hexb(buildHex)], '.status': [...new TextEncoder().encode('stage: 70000 bytes at 0x40c40000, free tail\ndrive-hello.elemod loaded 0 loaded\n')] };
  const dirs = new Set(), DEPTH = 3;
  const up = p => p.split('/').slice(0, -1).join('/');
  const jobs = {};
  let nj = 1;
  const sent = [], listeners = [];
  window.__unit = { files, dirs, sent };
  function answer(raw) {
    const msg = unpack7(raw.slice(6, -1)), seq = (msg[0] << 8) | msg[1], cmd = msg[4], a = msg.slice(5);
    sent.push(Array.from(msg));
    let ok = 1, extra = [];
    const name = () => cstr(a, cmd === 0x57 ? 4 : 0).replace(/^\/mods\/?/, '');
    const enc = s => [...new TextEncoder().encode(s)];
    if (cmd === 0x53) {
      const at = name();
      if (at && !dirs.has(at)) ok = 0;
      else {
        extra = new Array(12).fill(0);
        const here = p => up(p) === at, last = p => p.split('/').pop();
        if (at.split('/').length - (at ? 0 : 1) < DEPTH)        // folders above the route's depth only
          for (const d of [...dirs].filter(here).sort()) extra.push(...enc(last(d)), 0, 1, 2, ...be([0, 4], [0, 4], [0, 2]), 0, 0);
        for (const n of Object.keys(files).filter(here).sort()) extra.push(...enc(last(n)), 0, 0, 2, ...be([0, 4], [files[n].length, 4], [0, 2]), 1, 0);
      }
    } else if (cmd === 0x54) {
      const n = name();
      if (!(n in files)) ok = 0; else { const j = ++nj; jobs[j] = wrap(files[n]); extra = be([j, 4], [CHUNK, 4], [0, 1]); }
    } else if (cmd === 0x55) {
      const j = u32(a, 0), s = u32(a, 4), d = jobs[j].slice(s * CHUNK, (s + 1) * CHUNK);
      extra = [...be([j, 4], [s, 4], [0, 4]), (s + 1) * CHUNK >= jobs[j].length ? 1 : 0, ...be([0, 4], [d.length, 4]), ...d];
    } else if (cmd === 0x56) {
      extra = [...a.slice(0, 4), 0, 0, 0, 0];
    } else if (cmd === 0x57) {
      const j = ++nj; jobs[j] = [name(), []]; extra = be([j, 4]);
    } else if (cmd === 0x58) {
      const j = u32(a, 0), s = u32(a, 4), c = u32(a, 8), n = u32(a, 12), d = a.slice(16, 16 + n);
      if (crc(d, 0xffffffff) !== c) ok = 0; else { jobs[j][1].push(...d); extra = be([j, 4], [s, 4], [jobs[j][1].length, 4]); }
    } else if (cmd === 0x59) {
      const j = u32(a, 0), [n, d] = jobs[j];
      delete jobs[j];
      const body = d.slice(31, d.length - 12);
      ok = u32(d, 0) === 0xac11d303 && ((d[5] << 8) | d[6]) === 2 && u32(d, 13) === 0x454d4f44 && !d[29]
        && u32(d, d.length - 12) === crc(body, 0xffffffff) ? 1 : 0;
      if (ok && n === '.mv') {                           // "FROM\nTO": the route moves FROM, with what is in it, and its active lines
        const [from, to] = new TextDecoder().decode(Uint8Array.from(body)).split('\n');
        const re = p => (p === from ? to : p.startsWith(from + '/') ? to + p.slice(from.length) : p);
        if (dirs.has(from)) for (const x of [...dirs]) { dirs.delete(x); dirs.add(re(x)); }
        for (const k of Object.keys(files)) if (re(k) !== k) { files[re(k)] = files[k]; delete files[k]; }
        for (let x = up(to); x; x = up(x)) dirs.add(x);
        if (files.active) files.active = enc(new TextDecoder().decode(Uint8Array.from(files.active)).split('\n').map(l => { const i = l.indexOf(' '); return i < 0 ? re(l) : re(l.slice(0, i)) + l.slice(i); }).join('\n'));
      } else if (ok && n.endsWith('/.dir')) for (let d = up(n); d; d = up(d)) dirs.add(d);
      else if (ok) { files[n] = body; for (let d = up(n); d; d = up(d)) dirs.add(d); }
      extra = be([j, 4], [d.length, 4]);
    } else if (cmd === 0x5c) {
      const n = name();
      if (n in files) delete files[n];
      else if (dirs.has(n) && ![...dirs, ...Object.keys(files)].some(p => p.startsWith(n + '/'))) dirs.delete(n);
      else { ok = 0; if (dirs.has(n)) extra = [...enc('Folder not empty'), 0]; }
    }
    const reply = [...HEAD, ...pack7([0, 0, ...be([seq, 2]), cmd | 0x80, ok, ...extra]), 0xf7];
    setTimeout(() => listeners.forEach(f => f({ data: Uint8Array.from(reply) })), 0);
  }
  const input = { id: 'in', name: 'Elektron Analog Rytm MKII', manufacturer: 'Elektron', set onmidimessage(f) { listeners.length = 0; if (f) listeners.push(f); } };
  const output = { id: 'out', name: 'Elektron Analog Rytm MKII', manufacturer: 'Elektron', send: d => answer(Array.from(d)) };
  navigator.requestMIDIAccess = async opts => {
    window.__sysex = !!(opts && opts.sysex);
    return { inputs: new Map([['in', input]]), outputs: new Map([['out', output]]) };
  };
}

// ---- the run ---------------------------------------------------------------------------------------------------
const browser = await chromium.launch();
try {
  const page = await browser.newPage();
  const errors = [];
  // the patcher's engine is not here, so only the view's own errors count
  page.on('pageerror', e => { if (/drive|engine/.test(e.stack || '')) errors.push(e.message); });
  page.on('console', m => { if (m.type() === 'error' && /drive|engine/.test(m.location().url || '')) errors.push(m.text()); });
  await page.addInitScript(mock, V.build);
  await page.goto(url);
  await page.waitForFunction(() => document.getElementById('dm-port').options.length > 0);
  check('the page asks for MIDI with SysEx', await page.evaluate(() => window.__sysex));
  // the saved build names drive-hello (chosen on the unit): the view never deletes it, and never writes /mods/active
  const activeLines = Buffer.from(V.exchanges.find(s => s.op === 'put' && s.args[0] === 'active').args[1], 'hex').toString()
    .split('\n').filter(l => l.startsWith(hello.name + ' ')).map(l => l + '\n').join('');
  await page.evaluate(t => { window.__unit.files.active = [...new TextEncoder().encode(t)]; }, activeLines);
  await page.click('#dm-connect-btn');
  await page.waitForSelector('#dm-build:not([hidden])');
  await page.waitForSelector('#dm-files:not([hidden])');
  check('the build reads as the Python labels it', (await page.textContent('#dm-build-label')) === V.label, await page.textContent('#dm-build-label'));
  check('the +Drive area reads size first, area in brackets', /^\d+(\.\d)? K?B \(0x[0-9a-f]{8}\.\.0x[0-9a-f]{8}\)$/.test(await page.textContent('#dm-room dd')),
    await page.textContent('#dm-room dd'));
  check('the largest upload is the live stage of the unit\'s .status', (await page.textContent('#dm-room')).includes('68 KB'),
    await page.textContent('#dm-room'));
  check('the spare entries are collapsed', !(await page.evaluate(() => document.getElementById('dm-spares').open))
    && !(await page.isVisible('#dm-tables')));
  await page.click('#dm-spares summary');
  check('... and open on a click', await page.isVisible('#dm-tables'));
  const tables = await page.textContent('#dm-tables');
  check('the spare entries per event table', /ev_x.*0 of 1.*1/s.test(tables) && /ev_draw/.test(tables), tables.replace(/\s+/g, ' '));
  check('the last boot\'s /mods/.status', (await page.textContent('#dm-status-text')).includes('drive-hello.elemod loaded'));
  check('an empty /mods', (await page.textContent('#dm-file-list')).includes('No mods in /mods yet'));
  await page.waitForSelector('#dm-catalog:not([hidden])');
  const cat = await page.textContent('#dm-catalog-list');
  check('the shop marks each mod +Drive or flash', /\+Drive/.test(cat) && /flash\s*uses sites/.test(cat), cat.replace(/\s+/g, ' '));

  // an upload the checker refuses: nothing goes to the unit
  const before = await page.evaluate(() => window.__unit.sent.length);
  await page.setInputFiles('#dm-pick', [{ name: user.name, mimeType: 'application/json', buffer: user.raw }]);
  await page.waitForFunction(() => /REFUSED/.test(document.getElementById('dm-upload-msg').textContent));
  check('an upload that would not load is refused', (await page.textContent('#dm-upload-msg')).includes('requires drive-hello'),
    await page.textContent('#dm-upload-msg'));
  check('... and nothing was sent', (await page.evaluate(() => window.__unit.sent.length)) === before);

  // the two examples: the same requests as the CLI's, sequence numbers aside
  const start = await page.evaluate(() => window.__unit.sent.length);
  await page.setInputFiles('#dm-pick', [{ name: hello.name, mimeType: 'application/json', buffer: hello.raw },
    { name: user.name, mimeType: 'application/json', buffer: user.raw }]);
  await page.waitForFunction(() => /^Uploaded/.test(document.getElementById('dm-upload-msg').textContent), null, { timeout: 15000 });
  const msgs = (await page.evaluate(() => window.__unit.sent)).slice(start);
  const cli = put.flatMap(s => s.log.filter(([k]) => k === 'send').map(([, h]) => h));
  const unframe = h => {
    const d = Buffer.from(h, 'hex').subarray(6, -1), o = [];
    for (let i = 0; i < d.length; i += 8) for (let k = 1; k < 8 && i + k < d.length; k++) o.push(d[i + k] | (d[i] & (0x40 >> (k - 1)) ? 0x80 : 0));
    return o;
  };
  // sequence numbers aside, and the job id the unit gave (each fake unit counts its own)
  const strip = m => JSON.stringify([0x58, 0x59].includes(m[4]) ? [...m.slice(2, 5), ...m.slice(9)] : m.slice(2));
  const want = cli.map(h => strip(unframe(h))), got = msgs.slice(0, want.length).map(strip);
  check('the uploads are the CLI\'s requests, byte for byte (but the job id)', want.length > 0 && JSON.stringify(got) === JSON.stringify(want),
    `${got.length} of ${want.length} requests, first difference at ${got.findIndex((m, i) => m !== want[i])}`);
  check('the unit holds the files', await page.evaluate(([a, b]) => a in window.__unit.files && b in window.__unit.files, [hello.name, user.name]));
  await page.waitForFunction(() => document.querySelectorAll('#dm-file-list button[aria-label^="Delete "]').length === 2);
  const list = await page.textContent('#dm-file-list');
  check('/mods lists both, with their ids', /drive-hello-user\.elemod\s*drive-hello-user 1/.test(list) && /drive-hello\.elemod\s*drive-hello 1/.test(list)
    && !/Alone on this build/.test(list), list.replace(/\s+/g, ' '));

  check('nothing to tick: the build is chosen on the unit', !(await page.$('#dm-file-list input[type=checkbox]')) && !(await page.$('#dm-save-set')));
  check('a mod in the saved build can\'t be deleted', await page.isDisabled(`#dm-file-list button[aria-label="Delete ${hello.name}"]`));
  if (process.env.DRIVE_VIEW_SHOT) await page.screenshot({ path: process.env.DRIVE_VIEW_SHOT, fullPage: true });

  // delete: one press, no second ask (decided 2026-11-09)
  const del = page.locator('#dm-file-list tr', { hasText: user.name }).locator('button.dm-danger');
  await del.click();
  await page.waitForFunction(n => !(n in window.__unit.files), user.name);
  check('the file is deleted on the unit', true);

  // ---- folders ----
  const unit = () => page.evaluate(() => ({ files: Object.keys(window.__unit.files), dirs: [...window.__unit.dirs] }));
  const rowOf = text => page.locator('#dm-file-list tr', { hasText: text });
  const sentNow = () => page.evaluate(() => window.__unit.sent.length);
  const msg = () => page.textContent('#dm-upload-msg');
  // a new folder: made on the unit at once, empty, by an empty upload to fx/.dir
  let n0 = await sentNow();
  await page.fill('#dm-new-folder', 'fx');
  await page.click('#dm-new-folder-btn');
  await page.waitForFunction(() => /^Made/.test(document.getElementById('dm-upload-msg').textContent));
  check('a new folder opens, with the upload going into it', (await page.textContent('#dm-crumbs')).replace(/\s/g, '') === '/mods/fx'
    && (await page.textContent('#dm-into')) === '/mods/fx', await page.textContent('#dm-crumbs'));
  const mk = (await page.evaluate(() => window.__unit.sent)).slice(n0).filter(m => m[4] === 0x57)
    .map(m => { const a = m.slice(9); return Buffer.from(a.slice(0, a.indexOf(0))).toString(); });
  check('... made on the unit by an upload to /mods/fx/.dir', mk.join() === '/mods/fx/.dir', mk.join(' '));
  let u = await unit();
  check('... as an empty folder, with no file', u.dirs.includes('fx') && !u.files.some(f => f.startsWith('fx/')), JSON.stringify(u));
  check('... and it is empty', (await page.textContent('#dm-file-list')).includes('This folder is empty'));
  await page.fill('#dm-new-folder', '.hidden');
  await page.click('#dm-new-folder-btn');
  check('a folder name starting with . is refused', /REFUSED/.test(await msg()), await msg());
  await page.setInputFiles('#dm-pick', [{ name: user.name, mimeType: 'application/json', buffer: user.raw }]);
  await page.waitForFunction(() => /^Uploaded/.test(document.getElementById('dm-upload-msg').textContent), null, { timeout: 15000 });
  u = await unit();
  check('an upload lands in the open folder', u.files.includes(`fx/${user.name}`) && u.dirs.includes('fx') && !u.files.includes(user.name),
    JSON.stringify(u));
  check('... with its requirement found in another folder', (await msg()).includes(`/mods/fx/${user.name}`), await msg());
  await page.click('#dm-crumbs button:has-text("/mods")');
  await page.waitForSelector('#dm-file-list button[aria-label="Open fx"]');
  check('a folder row reads /name', (await page.textContent('#dm-file-list button[aria-label="Open fx"]')) === '/fx');
  check('a folder with a mod in it can\'t be deleted', await page.isDisabled('#dm-file-list button[aria-label="Delete folder fx"]'));
  // 3 deep: the note, and no deeper folder
  await page.click('#dm-file-list button[aria-label="Open fx"]');
  for (const d of ['a', 'b']) {
    await page.fill('#dm-new-folder', d); await page.click('#dm-new-folder-btn');
    await page.waitForFunction(p => document.getElementById('dm-upload-msg').textContent === `Made /mods/${p}.`, d === 'a' ? 'fx/a' : 'fx/a/b');
  }
  check('3 folders deep, a note says nothing deeper shows', (await page.textContent('#dm-into')) === '/mods/fx/a/b'
    && /3 deep/.test(await page.textContent('#dm-file-list')), await page.textContent('#dm-file-list'));
  check('... and no folder can be made there', await page.isDisabled('#dm-new-folder-btn'));
  // the shop's copy: one in a folder is found, and uploaded again where it is
  check('the shop finds the copy in /mods', /Upload again\s*replaces \/mods\/drive-hello\.elemod/.test(await page.textContent('#dm-catalog-list')),
    (await page.textContent('#dm-catalog-list')).replace(/\s+/g, ' '));
  // drops: files dropped on a folder row, or on a part of the path on top, go into that folder
  await page.click('#dm-crumbs button[data-dir="fx/a"]');
  check('a part of the path opens its folder', (await page.textContent('#dm-into')) === '/mods/fx/a');
  check('no .. row: the path goes up', !/\.\./.test(await page.textContent('#dm-file-list')) && /^Name/.test(await page.textContent('#dm-file-list tr')));
  const dropped = await page.evaluateHandle(([n, hex]) => {
    const dt = new DataTransfer();
    dt.items.add(new File([Uint8Array.from(hex.match(/../g), x => parseInt(x, 16))], n, { type: 'application/json' }));
    return dt;
  }, [hello.name, hello.raw.toString('hex')]);
  await page.evaluate(() => { delete window.__unit.files.active; });   // unloaded on the unit; read with the next listing
  await page.dispatchEvent('#dm-file-list tr[data-dir="fx/a/b"]', 'drop', { dataTransfer: dropped });
  await page.waitForFunction(n => `fx/a/b/${n}` in window.__unit.files, hello.name, { timeout: 15000 });
  await page.waitForFunction(() => /^Uploaded/.test(document.getElementById('dm-upload-msg').textContent));
  check('files dropped on a folder row go into it, and it opens', (await page.textContent('#dm-into')) === '/mods/fx/a/b');
  const dropped2 = await page.evaluateHandle(([n, hex]) => {
    const dt = new DataTransfer();
    dt.items.add(new File([Uint8Array.from(hex.match(/../g), x => parseInt(x, 16))], n, { type: 'application/json' }));
    return dt;
  }, ['row-drop.elemod', hello.raw.toString('hex')]);
  await page.waitForFunction(() => !document.getElementById('view-drive').dataset.busy);
  await page.dispatchEvent('#dm-crumbs button[data-dir="fx/a"]', 'drop', { dataTransfer: dropped2 });
  await page.waitForFunction(() => 'fx/a/row-drop.elemod' in window.__unit.files, null, { timeout: 15000 });
  await page.waitForFunction(() => /^Uploaded/.test(document.getElementById('dm-upload-msg').textContent));
  check('files dropped on a part of the path go into that folder', (await page.textContent('#dm-into')) === '/mods/fx/a');
  const rd = page.locator('#dm-file-list button[aria-label="Delete fx/a/row-drop.elemod"]');
  await rd.click();
  await page.waitForFunction(() => !('fx/a/row-drop.elemod' in window.__unit.files));
  await page.click('#dm-crumbs button:has-text("/mods")');
  const rootDel = rowOf(hello.name).filter({ hasNot: page.locator('button.dm-open') }).locator('button.dm-danger');
  await rootDel.click();
  await page.waitForFunction(n => !(n in window.__unit.files), hello.name);
  await page.waitForFunction(() => /replaces \/mods\/fx\/a\/b\//.test(document.getElementById('dm-catalog-list').textContent));
  check('the shop finds a copy inside a folder', /Upload again/.test(await page.textContent('#dm-catalog-list')),
    (await page.textContent('#dm-catalog-list')).replace(/\s+/g, ' '));
  n0 = await sentNow();
  await page.click('#dm-catalog-list button:has-text("Upload again")');
  await page.waitForFunction(() => /^Uploaded/.test(document.getElementById('dm-upload-msg').textContent), null, { timeout: 15000 });
  const opened = (await page.evaluate(() => window.__unit.sent)).slice(n0).filter(m => m[4] === 0x57)
    .map(m => { const a = m.slice(9); return Buffer.from(a.slice(0, a.indexOf(0))).toString(); });
  u = await unit();
  check('... and Upload again replaces it there, not at the root', opened.join() === `/mods/fx/a/b/${hello.name}` && !u.files.includes(hello.name),
    opened.join(' '));
  // DELETE DIR: an empty folder only
  await page.click('#dm-file-list button[aria-label="Open fx"]');
  await page.click('#dm-file-list button[aria-label="Open fx/a"]');
  await page.click('#dm-file-list button[aria-label="Open fx/a/b"]');
  const del2 = page.locator(`#dm-file-list button[aria-label="Delete fx/a/b/${hello.name}"]`);
  await del2.click();
  await page.waitForFunction(n => !(`fx/a/b/${n}` in window.__unit.files), hello.name);
  await page.waitForFunction(() => /This folder is empty/.test(document.getElementById('dm-file-list').textContent));
  check('an emptied folder stays, and shows', (await unit()).dirs.includes('fx/a/b'));
  await page.click('#dm-crumbs button[data-dir="fx"]');
  check('a folder that holds a folder can\'t be deleted', await page.isDisabled('#dm-file-list button[aria-label="Delete folder fx/a"]'));
  await page.click('#dm-file-list button[aria-label="Open fx/a"]');
  const dd = page.locator('#dm-file-list button[aria-label="Delete folder fx/a/b"]');
  await dd.click();
  await page.waitForFunction(() => !window.__unit.dirs.has('fx/a/b'));
  await page.waitForFunction(() => !document.querySelector('#dm-file-list button[aria-label="Open fx/a/b"]'));   // the listing read again
  check('the empty folder is deleted on the unit', /Deleted \/mods\/fx\/a\/b\./.test(await msg()), await msg());
  await page.fill('#dm-new-folder', 'tmp');
  await page.click('#dm-new-folder-btn');
  await page.waitForFunction(() => window.__unit.dirs.has('fx/a/tmp'));
  await page.waitForFunction(() => /^Made \/mods\/fx\/a\/tmp/.test(document.getElementById('dm-upload-msg').textContent));
  await page.click('#dm-crumbs button[data-dir="fx/a"]');
  const dt = page.locator('#dm-file-list button[aria-label="Delete folder fx/a/tmp"]');
  await dt.click();
  await page.waitForFunction(() => !window.__unit.dirs.has('fx/a/tmp'));
  check('a new empty folder is on the unit, and DELETE DIR removes it there', true);

  // move and rename: a row dragged onto a folder row or a part of the path moves there; Rename renames in place
  const rowDrag = path => page.evaluateHandle(p => { const dt = new DataTransfer(); dt.setData('application/x-elekloader-path', p); return dt; }, path);
  const moves = async from => (await page.evaluate(() => window.__unit.sent)).slice(from).filter(m => m[4] === 0x57)
    .map(m => { const a = m.slice(9); return Buffer.from(a.slice(0, a.indexOf(0))).toString(); }).filter(p => p === '/mods/.mv');
  const mLine = activeLines.slice(hello.name.length);    // drive-hello's line in /mods/active, for fx/a/m.elemod
  await page.evaluate(([raw, t]) => { window.__unit.files['fx/a/m.elemod'] = raw; window.__unit.files.active = [...new TextEncoder().encode(t)]; },
    [[...hello.raw], 'fx/a/m.elemod' + mLine]);
  await page.fill('#dm-new-folder', 'box');
  await page.click('#dm-new-folder-btn');
  await page.waitForFunction(() => /^Made \/mods\/fx\/a\/box/.test(document.getElementById('dm-upload-msg').textContent));
  await page.click('#dm-crumbs button[data-dir="fx/a"]');
  check('a mod in the build can be moved and renamed, not deleted', await page.isDisabled('#dm-file-list button[aria-label="Delete fx/a/m.elemod"]')
    && await page.isEnabled('#dm-file-list button[aria-label="Rename fx/a/m.elemod"]')
    && (await page.getAttribute('#dm-file-list tr[data-file="fx/a/m.elemod"]', 'draggable')) === 'true');
  n0 = await sentNow();
  await page.dispatchEvent('#dm-file-list tr[data-dir="fx/a/box"]', 'drop', { dataTransfer: await rowDrag('fx/a/m.elemod') });
  await page.waitForFunction(() => /^Moved/.test(document.getElementById('dm-upload-msg').textContent), null, { timeout: 15000 });
  u = await unit();
  check('a file row dropped on a folder row moves into it, by one upload to /mods/.mv', u.files.includes('fx/a/box/m.elemod')
    && !u.files.includes('fx/a/m.elemod') && (await moves(n0)).length === 1, `${await msg()} ${u.files.join(' ')}`);
  check('... and the route keeps /mods/active naming it', await page.evaluate(() => new TextDecoder().decode(Uint8Array.from(window.__unit.files.active))) === 'fx/a/box/m.elemod' + mLine);
  n0 = await sentNow();
  await page.dispatchEvent('#dm-file-list tr[data-dir="fx/a/box"]', 'drop', { dataTransfer: await rowDrag('fx/a/box') });
  check('a folder dropped on itself sends nothing', (await sentNow()) === n0);
  await page.click('#dm-file-list button[aria-label="Open fx/a/box"]');
  await page.click('#dm-file-list button[aria-label="Rename fx/a/box/m.elemod"]');
  check('Rename makes the name a text box', (await page.inputValue('#dm-file-list input.dm-rename')) === 'm.elemod');
  await page.fill('#dm-file-list input.dm-rename', 'n');
  await page.press('#dm-file-list input.dm-rename', 'Enter');
  check('a mod file keeps a .elemod name', /REFUSED: a mod file keeps a \.elemod name/.test(await msg()), await msg());
  await page.click('#dm-file-list button[aria-label="Rename fx/a/box/m.elemod"]');
  await page.fill('#dm-file-list input.dm-rename', 'n.elemod');
  await page.press('#dm-file-list input.dm-rename', 'Enter');
  await page.waitForFunction(() => /^Moved \/mods\/fx\/a\/box\/m\.elemod to \/mods\/fx\/a\/box\/n\.elemod/.test(document.getElementById('dm-upload-msg').textContent), null, { timeout: 15000 });
  check('Rename renames a file on the unit', (await unit()).files.includes('fx/a/box/n.elemod'));
  n0 = await sentNow();
  await page.dispatchEvent('#dm-crumbs button[data-dir=""]', 'drop', { dataTransfer: await rowDrag('fx/a/box') });
  await page.waitForFunction(() => /^Moved \/mods\/fx\/a\/box to \/mods\/box/.test(document.getElementById('dm-upload-msg').textContent), null, { timeout: 15000 });
  u = await unit();
  check('a folder dropped on a part of the path moves there, with what is in it', u.files.includes('box/n.elemod') && u.dirs.includes('box') && !u.dirs.includes('fx/a/box'));
  check('... and the open folder follows it', (await page.textContent('#dm-into')) === '/mods/box');
  await page.click('#dm-crumbs button[data-dir=""]');
  n0 = await sentNow();
  await page.click('#dm-file-list button[aria-label="Rename folder box"]');
  await page.fill('#dm-file-list input.dm-rename', 'fx');
  await page.press('#dm-file-list input.dm-rename', 'Enter');
  check('a name already there is refused before any request', /REFUSED: \/mods\/fx is already there/.test(await msg()) && (await sentNow()) === n0, await msg());
  await page.click('#dm-file-list button[aria-label="Rename folder box"]');
  await page.fill('#dm-file-list input.dm-rename', 'kept');
  await page.press('#dm-file-list input.dm-rename', 'Enter');
  await page.waitForFunction(() => /^Moved \/mods\/box to \/mods\/kept/.test(document.getElementById('dm-upload-msg').textContent), null, { timeout: 15000 });
  check('Rename renames a folder', (await unit()).files.includes('kept/n.elemod'));
  await page.click('#dm-file-list button[aria-label="Open fx"]');
  check('a folder\'s Rename and Delete line up with a file\'s', await page.evaluate(() => {
    const x = s => document.querySelector(`#dm-file-list button[aria-label="${s}"]`).getBoundingClientRect().left;
    const f = document.querySelector('#dm-file-list tr[data-file] button[aria-label^="Rename"]').getAttribute('aria-label').slice(7);
    return Math.abs(x('Rename folder fx/a') - x(`Rename ${f}`)) < 1 && Math.abs(x('Delete folder fx/a') - x(`Delete ${f}`)) < 1;
  }));
  check('the folder view spans the card', await page.evaluate(() => {
    const t = document.getElementById('dm-file-list'), p = document.getElementById('dm-pane');
    return Math.abs(t.getBoundingClientRect().width - p.getBoundingClientRect().width) < 2;
  }));

  const writes = (await page.evaluate(() => window.__unit.sent)).filter(m => [0x57, 0x5c].includes(m[4]));
  const paths = writes.map(m => { const a = m.slice(m[4] === 0x57 ? 9 : 5); return Buffer.from(a.slice(0, a.indexOf(0))).toString(); });
  check('the view never writes /mods/active', !paths.includes('/mods/active'), paths.join(' '));
  check('every write is inside /mods', paths.length > 0 && paths.every(p => p.startsWith('/mods/')), paths.join(' '));
  check('no errors from the view', !errors.length, errors.join(' | '));
} finally {
  await browser.close();
  server.close();
  rmSync(site, { recursive: true, force: true });
}
console.log(failed ? `${failed} failed` : 'all passed');
process.exit(failed ? 1 : 0);
