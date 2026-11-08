// SPDX-License-Identifier: GPL-2.0-or-later
// A GIF decoder for the Splash screen maker (splash.js): every frame of an
// animated GIF, composited as a browser shows it (disposal, transparency,
// interlacing), with its delay. The page loads nothing from other sites, and
// a canvas only ever draws a GIF's first frame, so the frames are decoded here.
//
//   const { width, height, frames } = decodeGif(bytes);   // frames: [{ rgba, delay }] (delay in ms)

const MAX_PIXELS = 4096 * 4096;
const MAX_FRAMES = 2000;

export function isGif(b) {
  return b.length > 6 && b[0] === 0x47 && b[1] === 0x49 && b[2] === 0x46 && b[3] === 0x38;
}

export function decodeGif(b) {
  if (!isGif(b)) throw new Error('not a GIF file');
  let p = 6;
  const u8 = () => { if (p >= b.length) throw new Error('the GIF ends early'); return b[p++]; };
  const u16 = () => { const v = u8(); return v | (u8() << 8); };
  const width = u16(), height = u16();
  if (!width || !height || width * height > MAX_PIXELS) throw new Error(`a ${width}x${height} GIF is too large`);
  const flags = u8();
  const bgIndex = u8();
  u8();                                                  // pixel aspect
  let global = null;
  if (flags & 0x80) global = table(1 << ((flags & 7) + 1));

  function table(n) {
    if (p + 3 * n > b.length) throw new Error('the GIF ends early');
    const t = b.subarray(p, p + 3 * n);
    p += 3 * n;
    return t;
  }
  function blocks() {                                    // data sub-blocks, joined
    const parts = [];
    let n = 0;
    for (let len = u8(); len; len = u8()) {
      if (p + len > b.length) throw new Error('the GIF ends early');
      parts.push(b.subarray(p, p + len));
      p += len;
      n += len;
    }
    const out = new Uint8Array(n);
    let o = 0;
    for (const x of parts) { out.set(x, o); o += x.length; }
    return out;
  }

  const canvas = new Uint8ClampedArray(width * height * 4);   // transparent to start
  const frames = [];
  let gce = { disposal: 0, delay: 0, transparent: -1 };
  for (;;) {
    if (p >= b.length) break;                            // no trailer: take what came
    const kind = u8();
    if (kind === 0x3b) break;                            // trailer
    if (kind === 0x21) {                                 // an extension
      const label = u8();
      const data = blocks();
      if (label === 0xf9 && data.length >= 4) {
        gce = { disposal: (data[0] >> 2) & 7, delay: (data[1] | (data[2] << 8)) * 10,
          transparent: data[0] & 1 ? data[3] : -1 };
      }
      continue;
    }
    if (kind !== 0x2c) throw new Error('the GIF has an unknown block');
    const x0 = u16(), y0 = u16(), w = u16(), h = u16();
    const f = u8();
    const colours = f & 0x80 ? table(1 << ((f & 7) + 1)) : global;
    if (!colours) throw new Error('the GIF has no colour table');
    const interlaced = !!(f & 0x40);
    const minCode = u8();
    const idx = lzw(blocks(), minCode, w * h);
    const before = gce.disposal === 3 ? canvas.slice() : null;
    // the frame's pixels onto the canvas, its transparent ones leaving what is there
    const rows = interlaced ? interlaceOrder(h) : null;
    for (let r = 0; r < h; r++) {
      const y = y0 + (rows ? rows[r] : r);
      if (y >= height) continue;
      for (let c = 0; c < w; c++) {
        const x = x0 + c;
        if (x >= width) continue;
        const k = idx[r * w + c];
        if (k === gce.transparent || 3 * k + 2 >= colours.length) continue;
        const o = 4 * (y * width + x);
        canvas[o] = colours[3 * k]; canvas[o + 1] = colours[3 * k + 1]; canvas[o + 2] = colours[3 * k + 2];
        canvas[o + 3] = 255;
      }
    }
    // a delay of 0 or 10 ms plays at 100 ms in browsers: do the same
    frames.push({ rgba: canvas.slice(), delay: gce.delay <= 10 ? 100 : gce.delay });
    if (frames.length >= MAX_FRAMES) break;
    if (gce.disposal === 2) {                            // back to the background: transparent
      for (let r = y0; r < Math.min(height, y0 + h); r++) {
        canvas.fill(0, 4 * (r * width + x0), 4 * (r * width + Math.min(width, x0 + w)));
      }
    } else if (before) {
      canvas.set(before);
    }
    gce = { disposal: 0, delay: 0, transparent: -1 };
  }
  if (!frames.length) throw new Error('the GIF has no frames');
  void bgIndex;
  return { width, height, frames };
}

function interlaceOrder(h) {
  const order = [];
  for (const [start, step] of [[0, 8], [4, 8], [2, 4], [1, 2]]) {
    for (let y = start; y < h; y += step) order.push(y);
  }
  return order;
}

// GIF's LZW: variable-width codes, LSB first, a clear code and an end code
function lzw(data, minCode, n) {
  const out = new Uint8Array(n);
  if (minCode < 2 || minCode > 11) throw new Error('the GIF is damaged');
  const clear = 1 << minCode, end = clear + 1;
  const prefix = new Int16Array(4096), suffix = new Uint8Array(4096), first = new Uint8Array(4096);
  const lens = new Uint16Array(4096);
  for (let i = 0; i < clear; i++) { prefix[i] = -1; suffix[i] = i; first[i] = i; lens[i] = 1; }
  let size = minCode + 1, next = end + 1, prev = -1;
  let bits = 0, acc = 0, o = 0, i = 0;
  const stack = new Uint8Array(4097);
  while (o < n) {
    while (bits < size) {
      if (i >= data.length) return out;                  // short data: the rest stays index 0
      acc |= data[i++] << bits;
      bits += 8;
    }
    let code = acc & ((1 << size) - 1);
    acc >>>= size;
    bits -= size;
    if (code === clear) { size = minCode + 1; next = end + 1; prev = -1; continue; }
    if (code === end) break;
    let cur = code;
    let extra = -1;
    if (code >= next) {                                  // the KwKwK case
      if (prev < 0 || code > next) break;
      cur = prev;
      extra = first[prev];
    }
    let sp = 0;
    for (let c = cur; c >= 0 && sp < 4096; c = prefix[c]) stack[sp++] = suffix[c];
    while (sp && o < n) out[o++] = stack[--sp];
    if (extra >= 0 && o < n) out[o++] = extra;
    if (prev >= 0 && next < 4096) {
      prefix[next] = prev;
      suffix[next] = extra >= 0 ? extra : first[cur];
      first[next] = first[prev];
      lens[next] = lens[prev] + 1;
      next++;
      if (next === 1 << size && size < 12) size++;
    }
    prev = code;
  }
  return out;
}
