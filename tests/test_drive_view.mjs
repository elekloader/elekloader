// SPDX-License-Identifier: GPL-2.0-or-later
// The patcher page's +Drive mods view (web/index.html#drive, web/drive.js) in a browser, against a mocked Web MIDI port, needing no firmware and no unit.
// The mock is a fake unit (tests/test_drive.py's FakeUnit, in JavaScript) holding the made-up build of
// js/test/drive-vectors.json, whose replies are the ones js/tools/drive_vectors.py recorded from `elekloader mods`.
// The patcher's own engine (Pyodide) is not on this test's site: only the view is tested. It connects, reads the build, lists /mods, refuses an upload the checker refuses (nothing is sent), uploads
// the two example mods with the very requests the CLI sent, saves the set, reads it back, and deletes a file.
//
//   node tests/test_drive_view.mjs      (Playwright with its Chromium; it skips without them)
//   DRIVE_VIEW_SHOT=page.png node tests/test_drive_view.mjs    (and a screenshot, once the set is saved)
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
  const files = { '.build': [...hexb(buildHex)], '.status': [...new TextEncoder().encode('drive-hello.elemod loaded 0 loaded\n')] };
  const jobs = {};
  let nj = 1;
  const sent = [], listeners = [];
  window.__unit = { files, sent };
  function answer(raw) {
    const msg = unpack7(raw.slice(6, -1)), seq = (msg[0] << 8) | msg[1], cmd = msg[4], a = msg.slice(5);
    sent.push(Array.from(msg));
    let ok = 1, extra = [];
    const name = () => cstr(a, cmd === 0x57 ? 4 : 0).split('/').pop();
    if (cmd === 0x53) {
      extra = new Array(12).fill(0);
      for (const n of Object.keys(files).sort()) extra.push(...new TextEncoder().encode(n), 0, 0, 2, ...be([0, 4], [files[n].length, 4], [0, 2]), 1, 0);
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
      if (ok) files[n] = body;
      extra = be([j, 4], [d.length, 4]);
    } else if (cmd === 0x5c) {
      const n = name();
      ok = n in files ? 1 : 0;
      delete files[n];
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
  await page.click('#dm-connect-btn');
  await page.waitForSelector('#dm-build:not([hidden])');
  await page.waitForSelector('#dm-files:not([hidden])');
  check('the build reads as the Python labels it', (await page.textContent('#dm-build-label')) === V.label, await page.textContent('#dm-build-label'));
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
  await page.waitForFunction(() => document.querySelectorAll('#dm-file-list input[type=checkbox]').length === 2);
  const list = await page.textContent('#dm-file-list');
  check('/mods lists both, each checked alone', /drive-hello\.elemod.*loads/s.test(list) && /requires drive-hello/.test(list), list.replace(/\s+/g, ' '));

  // the set: only the user ticked is refused; both are saved
  await page.check(`#dm-file-list input[aria-label="Load ${user.name}"]`);
  check('a set that would not load cannot be saved', await page.isDisabled('#dm-save-set'), await page.textContent('#dm-set-check'));
  await page.check(`#dm-file-list input[aria-label="Load ${hello.name}"]`);
  check('a set that loads can', !(await page.isDisabled('#dm-save-set')), await page.textContent('#dm-set-check'));
  await page.click('#dm-save-set');
  await page.waitForFunction(() => /^Saved/.test(document.getElementById('dm-set-msg').textContent));
  const active = await page.evaluate(() => new TextDecoder().decode(Uint8Array.from(window.__unit.files.active)));
  const want2 = V.exchanges.find(s => s.op === 'put' && s.args[0] === 'active').args[1];
  check('/mods/active is the CLI\'s for the same set', Buffer.from(active).toString('hex') === want2, JSON.stringify(active));
  await page.waitForFunction(() => document.querySelectorAll('#dm-file-list input:checked').length === 2);
  check('the set reads back from the unit', true);
  if (process.env.DRIVE_VIEW_SHOT) await page.screenshot({ path: process.env.DRIVE_VIEW_SHOT, fullPage: true });

  // delete: two presses
  const del = page.locator('#dm-file-list tr', { hasText: user.name }).locator('button.dm-danger');
  await del.click();
  check('delete asks once more', /delete\?/i.test(await del.textContent()));
  await del.click();
  await page.waitForFunction(n => !(n in window.__unit.files), user.name);
  check('the file is deleted on the unit', true);
  await page.waitForFunction(() => /drop/.test(document.getElementById('dm-set-check').textContent));
  check('the set says the unit will drop it', true);

  const writes = (await page.evaluate(() => window.__unit.sent)).filter(m => [0x57, 0x5c].includes(m[4]));
  const paths = writes.map(m => { const a = m.slice(m[4] === 0x57 ? 9 : 5); return Buffer.from(a.slice(0, a.indexOf(0))).toString(); });
  check('every write is inside /mods', paths.length > 0 && paths.every(p => p.startsWith('/mods/')), paths.join(' '));
  check('no errors from the view', !errors.length, errors.join(' | '));
} finally {
  await browser.close();
  server.close();
  rmSync(site, { recursive: true, force: true });
}
console.log(failed ? `${failed} failed` : 'all passed');
process.exit(failed ? 1 : 0);
