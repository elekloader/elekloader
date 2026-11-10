// SPDX-License-Identifier: GPL-2.0-or-later
// The Splash screen maker (index.html's #splash view): a PNG or a GIF ->
// 128x64 one-bit frames and a sequence, previewed as the screen shows them,
// then made into a .elemod by the engine (bridge.make_splash ->
// elekloader.splash). app.js loads this file when the view first opens.
//
// The picture never leaves the tab: it is drawn and converted here, and only
// the converted frames go to the worker.

// gif.js with this deploy's version (?v=, as app.js names this file), so a cache never mixes two deploys
const { decodeGif, isGif } = await import(new URL('./gif.js' + new URL(import.meta.url).search, import.meta.url).href);

const W = 128, H = 64, FRAME = 1024;
const SPEEDS = [0.25, 0.33, 0.5, 0.75, 1, 1.5, 2, 3, 4];
const BAYER = [0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5];
const MAX_SIDE = 1024;                    // a source frame is kept at most this big
const DEFAULTS = { fit: 'fit', zoom: 100, dx: 0, dy: 0, dither: 'threshold', threshold: 128, bright: 0,
  contrast: 0, invert: false, bgwhite: false, speed: 4, loop: true };

export function mount(ctx) {
  const $ = id => document.getElementById(id);
  const limits = () => (ctx.info() || {}).splash || { targets: [], max_frames: 96, max_sequence: 4096, rate: 15, intro: 50 };
  const s = {
    src: null,            // {name, frames: [{bitmap, delay}], width, height, animated}
    opt: { ...DEFAULTS },
    out: null,            // {frames: [Uint8Array 1024], bits: [Uint8Array 8192], seq: [n], dropped, cut}
    pos: 0, playing: true, last: 0, raf: 0, pending: 0, busy: false,
  };
  const work = document.createElement('canvas');
  work.width = W; work.height = H;
  const wctx = work.getContext('2d', { willReadFrequently: true });
  const screen = $('sp-screen').getContext('2d');
  const pixels = screen.createImageData(W, H);

  // ---- the picture in ----

  async function take(file) {
    error('');
    if (!file) return;
    if (file.size > 32 << 20) { error(`${file.name} is too large (over 32 MB).`); return; }
    try {
      const raw = new Uint8Array(await file.arrayBuffer());
      await load(file.name, raw, file.type);
    } catch (e) {
      error(`${file.name} could not be read as a picture: ${e.message || e}`);
    }
  }

  async function load(name, raw, type) {
    let frames = [], width, height;
    if (isGif(raw)) {
      const g = decodeGif(raw);
      width = g.width; height = g.height;
      for (const f of g.frames) {
        frames.push({ bitmap: await shrink(new ImageData(f.rgba, g.width, g.height)), delay: f.delay });
      }
    } else {
      const bmp = await createImageBitmap(new Blob([raw], { type: type || '' }));
      width = bmp.width; height = bmp.height;
      frames.push({ bitmap: await shrink(bmp), delay: 0 });
    }
    setSource({ name, frames, width, height, animated: frames.length > 1 });
  }

  // at most MAX_SIDE on a side: plenty for 128x64 at 400 %
  async function shrink(img) {
    const k = Math.min(1, MAX_SIDE / Math.max(img.width, img.height));
    if (k === 1 && img instanceof ImageBitmap) return img;
    return createImageBitmap(img, { resizeWidth: Math.max(1, Math.round(img.width * k)),
      resizeHeight: Math.max(1, Math.round(img.height * k)), resizeQuality: 'high' });
  }

  function setSource(src) {
    s.src = src;
    s.pos = 0;
    const base = src.name.replace(/\.[^.]*$/, '').replace(/[_-]+/g, ' ').trim();
    if (!$('sp-title').dataset.edited) $('sp-title').value = base.slice(0, 40);
    const secs = src.animated ? ` · ${src.frames.length} frames, ${(src.frames.reduce((a, f) => a + f.delay, 0) / 1000).toFixed(1)} s` : '';
    $('sp-source').textContent = `${src.name}: ${src.width} × ${src.height}${secs}`;
    $('sp-source').hidden = false;
    $('sp-anim').hidden = !src.animated;
    $('sp-empty').hidden = true;
    $('sp-player').hidden = !src.animated;
    update();
  }

  // ---- the conversion ----

  function rectFor(w, h) {
    const o = s.opt, z = o.zoom / 100;
    let sx, sy;
    if (o.fit === 'stretch') { sx = W / w; sy = H / h; }
    else if (o.fit === 'fill') { sx = sy = Math.max(W / w, H / h); }
    else { sx = sy = Math.min(W / w, H / h); }
    const dw = w * sx * z, dh = h * sy * z;
    return [(W - dw) / 2 + o.dx, (H - dh) / 2 + o.dy, dw, dh];
  }

  // one source frame -> 8192 bits (1 = lit), row by row
  function convert(bitmap) {
    const o = s.opt;
    wctx.save();
    wctx.fillStyle = o.bgwhite ? '#fff' : '#000';
    wctx.fillRect(0, 0, W, H);
    wctx.imageSmoothingEnabled = true;
    wctx.imageSmoothingQuality = 'high';
    wctx.drawImage(bitmap, ...rectFor(bitmap.width, bitmap.height));
    wctx.restore();
    const d = wctx.getImageData(0, 0, W, H).data;
    const cf = o.contrast >= 0 ? 1 + o.contrast / 40 : 1 + o.contrast / 100;
    const lum = new Float32Array(W * H);
    for (let i = 0; i < W * H; i++) {
      const v = 0.299 * d[4 * i] + 0.587 * d[4 * i + 1] + 0.114 * d[4 * i + 2];
      lum[i] = Math.max(0, Math.min(255, (v - 128) * cf + 128 + o.bright * 1.28));
    }
    const t = o.threshold;
    const bits = new Uint8Array(W * H);
    if (o.dither === 'bayer') {
      for (let y = 0; y < H; y++) {
        for (let x = 0; x < W; x++) {
          const b = (BAYER[(y & 3) * 4 + (x & 3)] + 0.5) / 16 - 0.5;
          bits[y * W + x] = lum[y * W + x] + b * 255 > t ? 1 : 0;
        }
      }
    } else if (o.dither === 'floyd' || o.dither === 'atkinson') {
      const at = o.dither === 'atkinson';
      for (let y = 0; y < H; y++) {
        for (let x = 0; x < W; x++) {
          const i = y * W + x, v = lum[i], on = v > t ? 1 : 0;
          bits[i] = on;
          const e = v - (on ? 255 : 0);
          const add = (dx, dy, k) => {
            const xx = x + dx, yy = y + dy;
            if (xx >= 0 && xx < W && yy < H) lum[yy * W + xx] += e * k;
          };
          if (at) { for (const [dx, dy] of [[1, 0], [2, 0], [-1, 1], [0, 1], [1, 1], [0, 2]]) add(dx, dy, 1 / 8); }
          else { add(1, 0, 7 / 16); add(-1, 1, 3 / 16); add(0, 1, 5 / 16); add(1, 1, 1 / 16); }
        }
      }
    } else {
      for (let i = 0; i < W * H; i++) bits[i] = lum[i] > t ? 1 : 0;
    }
    if (o.invert) for (let i = 0; i < W * H; i++) bits[i] ^= 1;
    return bits;
  }

  // the panel's format: byte = page + 8 * column, page = 7 - y / 8, bit y % 8 (elekloader.splash.pack)
  function pack(bits) {
    const out = new Uint8Array(FRAME);
    for (let y = 0; y < H; y++) {
      const page = 7 - (y >> 3), bit = 1 << (y & 7);
      for (let x = 0; x < W; x++) if (bits[y * W + x]) out[page + 8 * x] |= bit;
    }
    return out;
  }

  // which source frame shows at each of the intro's presents
  function sourceSequence() {
    const src = s.src, L = limits();
    if (!src.animated) return { seq: [0], cut: false };
    const speed = SPEEDS[s.opt.speed] || 1, tick = 1000 / L.rate;
    const ends = [];
    let t = 0;
    for (const f of src.frames) { t += f.delay / speed; ends.push(t); }
    let n = Math.max(1, Math.round(t / tick));
    const cut = n > L.max_sequence;
    n = Math.min(n, L.max_sequence);
    const seq = [];
    for (let k = 0, j = 0; k < n; k++) {
      const at = (k + 0.5) * tick;
      while (j < ends.length - 1 && ends[j] <= at) j++;
      seq.push(j);
    }
    return { seq, cut };
  }

  function update() {
    if (!s.src) return;
    clearTimeout(s.pending);                             // a timer, not a frame: it runs in a hidden tab too
    s.pending = setTimeout(compute, 0);
  }

  function compute() {
    const L = limits();
    const { seq: srcSeq, cut } = sourceSequence();
    const used = [...new Set(srcSeq)];
    const conv = new Map();                              // source frame -> its bits
    for (const i of used) conv.set(i, convert(s.src.frames[i].bitmap));
    // identical frames are stored once
    const index = new Map(), frames = [], bits = [], of = new Map();
    for (const i of used) {
      const p = pack(conv.get(i));
      const key = String.fromCharCode.apply(null, p);
      if (!index.has(key)) { index.set(key, frames.length); frames.push(p); bits.push(conv.get(i)); }
      of.set(i, index.get(key));
    }
    let seq = srcSeq.map(i => of.get(i)), dropped = 0;
    if (frames.length > L.max_frames) {                  // too many: keep evenly spaced ones
      const keep = [];
      for (let k = 0; k < L.max_frames; k++) keep.push(Math.floor(k * frames.length / L.max_frames));
      const remap = new Map(keep.map((f, k) => [f, k]));
      let lastKept = 0;
      const nearest = [];
      for (let f = 0; f < frames.length; f++) { if (remap.has(f)) lastKept = remap.get(f); nearest.push(lastKept); }
      seq = seq.map(f => nearest[f]);
      dropped = frames.length - keep.length;
      const kf = keep.map(f => frames[f]), kb = keep.map(f => bits[f]);
      frames.length = 0; bits.length = 0;
      frames.push(...kf); bits.push(...kb);
    }
    s.out = { frames, bits, seq, dropped, cut };
    if (s.pos >= seq.length) s.pos = 0;
    $('sp-scrub').max = String(seq.length - 1);
    facts();
    draw();
    buttons();
  }

  // ---- the preview ----

  function draw() {
    if (!s.out) return;
    const b = s.out.bits[s.out.seq[s.pos]];
    const px = pixels.data;
    for (let i = 0; i < W * H; i++) {
      const v = b[i] ? 236 : 0;
      px[4 * i] = v; px[4 * i + 1] = v; px[4 * i + 2] = b[i] ? 240 : 0; px[4 * i + 3] = 255;
    }
    screen.putImageData(pixels, 0, 0);
    $('sp-scrub').value = String(s.pos);
    const r = limits().rate;
    $('sp-time').textContent = `${(s.pos / r).toFixed(1)} / ${(s.out.seq.length / r).toFixed(1)} s`;
  }

  function tick(now) {
    s.raf = requestAnimationFrame(tick);
    if (!s.playing || !s.out || s.out.seq.length < 2 || $('view-splash').hidden) return;
    const step = 1000 / limits().rate;
    if (now - s.last < step) return;
    s.last = now - ((now - s.last) % step);
    if (s.pos + 1 >= s.out.seq.length) {
      if (!s.opt.loop) { s.playing = false; playButton(); return; }
      s.pos = 0;
    } else {
      s.pos++;
    }
    draw();
  }

  function playButton() {
    $('sp-play').textContent = s.playing ? 'Pause' : 'Play';
    $('sp-play').setAttribute('aria-pressed', String(s.playing));
  }

  function facts() {
    const o = s.out, L = limits();
    const n = o.frames.length, kb = Math.ceil((104 + 16 + 2 * o.seq.length + n * FRAME) / 1024);
    const secs = (o.seq.length / L.rate).toFixed(1);
    $('sp-tally').textContent = s.src.animated ? `${n} frame${n === 1 ? '' : 's'} · ${secs} s` : 'a still picture';
    $('sp-facts').textContent = (s.src.animated
      ? `${n} different frame${n === 1 ? '' : 's'} at about ${L.rate} a second, ${s.opt.loop ? 'looping' : 'then it stays on the last one'}. `
      : 'Shown for the whole intro. ') + `The mod takes about ${kb} KB of your unit's RAM.`;
    const warn = [];
    if (s.src.animated && o.seq.length > L.intro * 1.1) {
      warn.push(`The intro runs about ${(L.intro / L.rate).toFixed(1)} seconds, so only that much of it shows `
        + 'when the unit starts. A faster speed shows more of it.');
    }
    if (o.dropped) warn.push(`That is more than ${L.max_frames} different frames, so ${o.dropped} were left out. A faster speed or a shorter GIF keeps them all.`);
    if (o.cut) warn.push(`Only the first ${Math.round(L.max_sequence / L.rate)} seconds are kept.`);
    $('sp-warn').textContent = warn.join(' ');
    $('sp-warn').hidden = !warn.length;
  }

  // ---- making the mod ----

  function targets() {
    const sel = $('sp-target'), L = limits(), want = ctx.target();
    const had = sel.value;
    sel.replaceChildren(...L.targets.map(t => {
      const o = document.createElement('option');
      o.value = `${t.device} ${t.os}`;
      o.textContent = `${t.name} · OS ${t.os}`;
      return o;
    }));
    const pick = had && L.targets.some(t => `${t.device} ${t.os}` === had) ? had
      : want && L.targets.some(t => t.device === want.device && t.os === want.os) ? `${want.device} ${want.os}`
        : L.targets.length ? (t => `${t.device} ${t.os}`)(L.targets.filter(t => t.device === L.targets[0].device).pop()) : '';
    sel.value = pick;
    targetNote();
  }

  function targetNote() {
    const want = ctx.target(), v = $('sp-target').value;
    const L = limits();
    let text = '';
    if (!L.targets.length) text = 'The build engine is still loading.';
    else if (!want) text = 'Pick your device and its OS. Drop its stock OS file in on Build Firmware to build with it.';
    else if (v !== `${want.device} ${want.os}`) {
      const t = L.targets.find(x => `${x.device} ${x.os}` === `${want.device} ${want.os}`);
      text = t ? 'Your stock OS file is for another device or OS than this one: the mod goes in Your files, for that OS.'
        : `Your stock OS (${want.name || want.device} ${want.os}) has no splash yet. It is made for the devices listed here.`;
    }
    $('sp-target-note').textContent = text;
  }

  function buttons() {
    const ok = !!(s.out && ctx.info() && $('sp-target').value && !s.busy);
    $('sp-add').disabled = !ok;
    $('sp-download').disabled = !ok;
  }

  async function make() {
    const [device, os] = $('sp-target').value.split(' ');
    const o = s.out, data = new Uint8Array(o.frames.length * FRAME);
    o.frames.forEach((f, k) => data.set(f, k * FRAME));
    const r = await ctx.engine.call('make_splash', {
      device, os, sequence: o.seq, loop: s.src.animated ? s.opt.loop : true,
      title: $('sp-title').value.trim() || 'My splash', author: $('sp-author').value.trim(),
      source: s.src.name,
    }, data.buffer);
    if (!r.ok) throw new Error(r.error);
    return { name: r.name, data: new TextEncoder().encode(r.text).buffer };
  }

  async function run(fn) {
    if (s.busy) return;
    s.busy = true;
    buttons();
    status('Making the mod…');
    try {
      await fn();
    } catch (e) {
      status(String(e.message || e), true);
    } finally {
      s.busy = false;
      buttons();
    }
  }

  function status(text, bad = false) {
    $('sp-status').textContent = text;
    $('sp-status').className = 'small' + (bad ? ' bad' : '');
  }

  function error(text) {
    $('sp-error').textContent = text;
    $('sp-error').hidden = !text;
  }

  // ---- a sample: an animation drawn here, so there is something to try ----

  async function sample() {
    const frames = [];
    const c = document.createElement('canvas');
    c.width = 256; c.height = 128;
    const g = c.getContext('2d');
    for (let k = 0; k < 30; k++) {
      g.fillStyle = '#000'; g.fillRect(0, 0, 256, 128);
      g.fillStyle = '#fff';
      g.font = 'bold 34px system-ui, sans-serif';
      g.textAlign = 'center'; g.textBaseline = 'middle';
      g.fillText('HELLO', 128, 54);
      const a = (k / 30) * Math.PI * 2;
      for (let i = 0; i < 7; i++) {
        const x = 128 + Math.cos(a + i * 0.9) * (100 - i * 3), y = 64 + Math.sin(2 * (a + i * 0.9)) * 44;
        g.beginPath(); g.arc(x, y, 9 - i, 0, Math.PI * 2); g.fill();
      }
      g.fillRect(28, 82, 200 * (0.5 + 0.5 * Math.sin(a)), 6);
      frames.push({ bitmap: await createImageBitmap(c), delay: 70 });
    }
    setSource({ name: 'sample.gif', frames, width: 256, height: 128, animated: true });
  }

  // ---- wiring ----

  const ranges = { zoom: v => `${v}%`, dx: v => v, dy: v => v, threshold: v => v, bright: v => v, contrast: v => v,
    speed: v => `${SPEEDS[v]}×` };
  function syncControls() {
    const o = s.opt;
    for (const k of Object.keys(ranges)) {
      $('sp-' + k).value = String(o[k]);
      $(`sp-${k}-v`).textContent = String(ranges[k](o[k]));
    }
    for (const r of document.querySelectorAll('input[name=sp-fit]')) r.checked = r.value === o.fit;
    for (const r of document.querySelectorAll('input[name=sp-dither]')) r.checked = r.value === o.dither;
    $('sp-invert').checked = o.invert;
    $('sp-bgwhite').checked = o.bgwhite;
    $('sp-loop').checked = o.loop;
  }

  for (const k of Object.keys(ranges)) {
    $('sp-' + k).addEventListener('input', e => {
      s.opt[k] = Number(e.target.value);
      $(`sp-${k}-v`).textContent = String(ranges[k](s.opt[k]));
      update();
    });
  }
  for (const name of ['fit', 'dither']) {
    for (const r of document.querySelectorAll(`input[name=sp-${name}]`)) {
      r.addEventListener('change', () => { if (r.checked) { s.opt[name] = r.value; update(); } });
    }
  }
  for (const k of ['invert', 'bgwhite', 'loop']) {
    $('sp-' + k).addEventListener('change', e => {
      s.opt[k] = e.target.checked;
      if (k === 'loop' && s.opt.loop && !s.playing) { s.playing = true; playButton(); }
      update();
    });
  }
  $('sp-reset').addEventListener('click', () => { s.opt = { ...DEFAULTS }; syncControls(); update(); });
  $('sp-drop').addEventListener('click', () => $('sp-file').click());
  $('sp-drop').addEventListener('keydown', e => {
    if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); $('sp-file').click(); }
  });
  $('sp-file').addEventListener('change', e => { take(e.target.files[0]); e.target.value = ''; });
  $('sp-sample').addEventListener('click', () => { error(''); sample(); });
  $('sp-play').addEventListener('click', () => {
    s.playing = !s.playing;
    if (s.playing && s.out && s.pos + 1 >= s.out.seq.length) s.pos = 0;
    playButton();
  });
  $('sp-scrub').addEventListener('input', e => { s.pos = Number(e.target.value); s.playing = false; playButton(); draw(); });
  $('sp-title').addEventListener('input', e => { e.target.dataset.edited = e.target.value ? '1' : ''; });
  $('sp-target').addEventListener('change', targetNote);
  $('sp-add').addEventListener('click', () => run(async () => {
    const f = await make();
    const r = await ctx.add(f.name, f.data);
    if (!r.ok) throw new Error(r.error);
    status(r.ticked ? `Added to your build as ${f.name}. Build Firmware makes the .syx.`
      : `Added to Your files as ${f.name}. It goes in a build for its OS: drop that stock OS file in on Build Firmware.`);
    ctx.toast(`Your splash is in ${r.ticked ? 'your build' : 'Your files'}`);
  }));
  $('sp-download').addEventListener('click', () => run(async () => {
    const f = await make();
    const url = URL.createObjectURL(new Blob([f.data], { type: 'application/json' }));
    const a = document.createElement('a');
    a.href = url; a.download = f.name;
    document.body.append(a); a.click(); a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 10000);
    status(`Downloaded ${f.name}. Add it with "+ Add your own .elemod", here or in the desktop app.`);
  }));
  // a picture dropped on this view is the splash's, not a stock file
  $('view-splash').addEventListener('drop', e => {
    const f = e.dataTransfer && [...e.dataTransfer.files].find(x => /^image\//.test(x.type) || /\.(png|gif|jpe?g|webp)$/i.test(x.name));
    if (f) { e.preventDefault(); e.stopPropagation(); document.body.classList.remove('dragging'); take(f); }
  });

  syncControls();
  playButton();
  s.raf = requestAnimationFrame(tick);
  return {
    // the engine came up, or the stock file changed
    refresh() { targets(); buttons(); },
  };
}
