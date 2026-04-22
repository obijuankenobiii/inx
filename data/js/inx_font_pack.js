/**
 * Browser-side packer for Inx SD streaming fonts (.bin).
 * Layout must match ExternalFont::load / decodeGlyphRow (24-byte rows, 2-bit bitmaps).
 */
(function (global) {
  'use strict';

  var MAGIC = 0x45504446;
  var VERSION = 1;
  /** Reader font steps are treated as pt; use pt here too (px would look ~4/3 smaller). */
  var SIZES = [10, 12, 14, 16, 18];
  /** Codepoint ranges (BMP-focused; device uses uint32 CP search). */
  var CP_RANGES = [
    [0x0020, 0x007e],
    [0x00a0, 0x00ff],
    [0x0100, 0x017f],
    [0x0180, 0x024f],
    [0x2000, 0x206f],
    [0x2010, 0x203a],
    [0x2040, 0x205f],
    [0x20a0, 0x20cf],
    [0x0300, 0x036f],
    [0x0400, 0x04ff],
    [0x2200, 0x22ff],
    [0xfffd, 0xfffd],
  ];
  var MAX_SIDE = 384;
  var MAX_ADVANCE = 256;

  function collectCodepoints() {
    var s = new Set();
    for (var ri = 0; ri < CP_RANGES.length; ri++) {
      var a = CP_RANGES[ri][0];
      var b = CP_RANGES[ri][1];
      for (var cp = a; cp <= b; cp++) {
        if (cp >= 0xd800 && cp <= 0xdfff) continue;
        if (cp < 0x20 && cp !== 0x09) continue;
        s.add(cp);
      }
    }
    return Array.from(s).sort(function (x, y) {
      return x - y;
    });
  }

  function sanitizeFamilyName(raw) {
    var t = (raw || '').trim();
    if (!t) t = 'CustomFont';
    t = t.replace(/[^a-zA-Z0-9 _\-]/g, '_').replace(/\s+/g, '_');
    if (t.length > 48) t = t.slice(0, 48);
    if (t.startsWith('.')) t = '_' + t;
    return t;
  }

  function grayToStored(L) {
    if (L >= 252) return 0;
    if (L >= 200) return 1;
    if (L >= 140) return 2;
    return 3;
  }

  function pack2bitLinear(padW, h, getLum) {
    var totalPx = padW * h;
    var nbytes = Math.ceil(totalPx / 4);
    var out = new Uint8Array(nbytes);
    for (var i = 0; i < totalPx; i++) {
      var gy = Math.floor(i / padW);
      var gx = i % padW;
      var stored = grayToStored(getLum(gy, gx));
      var bi = i >> 2;
      var sh = (3 - (i & 3)) << 1;
      out[bi] |= stored << sh;
    }
    return out;
  }

  function fontSpec(family, sizePt) {
    return sizePt + 'pt "' + family + '"';
  }

  function measureRef(ctx, family, size) {
    ctx.textBaseline = 'alphabetic';
    ctx.font = fontSpec(family, size);
    var m = ctx.measureText('|');
    var asc = m.actualBoundingBoxAscent;
    var desc = m.actualBoundingBoxDescent;
    if (!isFinite(asc) || asc <= 0) asc = size * 0.72;
    if (!isFinite(desc) || desc <= 0) desc = size * 0.28;
    var lh = Math.round(asc + desc);
    return {
      lineHeight: lh,
      ascender: Math.round(asc),
      descender: Math.round(desc),
    };
  }

  function rasterizeChar(family, size, cp) {
    var W = 512;
    var H = 512;
    var ox = 200;
    var by = 300;
    var c = document.createElement('canvas');
    c.width = W;
    c.height = H;
    var ctx = c.getContext('2d');
    ctx.fillStyle = '#ffffff';
    ctx.fillRect(0, 0, W, H);
    ctx.textBaseline = 'alphabetic';
    ctx.fillStyle = '#000000';
    ctx.font = fontSpec(family, size);
    var ch = String.fromCodePoint(cp);
    var m = ctx.measureText(ch);
    var adv = Math.round(m.width);
    if (adv < 1) adv = 1;
    if (adv > MAX_ADVANCE) adv = MAX_ADVANCE;
    var topRef = Math.round(m.actualBoundingBoxAscent > 0 ? m.actualBoundingBoxAscent : size * 0.72);

    if (cp === 0x20 || cp === 0xa0 || ch === '\t') {
      if (cp === 0x20 || cp === 0xa0) adv = Math.max(adv, Math.round(size * 0.35));
      return { w: 0, h: 0, left: 0, top: topRef, adv: adv, bits: new Uint8Array(0) };
    }

    ctx.fillText(ch, ox, by);
    var id = ctx.getImageData(0, 0, W, H).data;
    var minX = W,
      minY = H,
      maxX = -1,
      maxY = -1;
    var thr = 248;
    for (var y = 0; y < H; y++) {
      for (var x = 0; x < W; x++) {
        var j = (y * W + x) * 4;
        var L = 0.299 * id[j] + 0.587 * id[j + 1] + 0.114 * id[j + 2];
        if (L < thr) {
          if (x < minX) minX = x;
          if (y < minY) minY = y;
          if (x > maxX) maxX = x;
          if (y > maxY) maxY = y;
        }
      }
    }
    if (maxX < minX) {
      return { w: 0, h: 0, left: 0, top: topRef, adv: adv, bits: new Uint8Array(0) };
    }
    var bw = maxX - minX + 1;
    var bh = maxY - minY + 1;
    if (bw > MAX_SIDE || bh > MAX_SIDE) {
      return null;
    }
    var padW = (bw + 3) & ~3;
    var getLum = function (gy, gx) {
      if (gx >= bw) return 255;
      var j = ((minY + gy) * W + (minX + gx)) * 4;
      return 0.299 * id[j] + 0.587 * id[j + 1] + 0.114 * id[j + 2];
    };
    var bits = pack2bitLinear(padW, bh, getLum);
    return {
      w: padW,
      h: bh,
      left: minX - ox,
      top: by - minY,
      adv: adv,
      bits: bits,
    };
  }

  function writeUint16(dv, o, v) {
    dv.setUint16(o, v, true);
  }
  function writeInt16(dv, o, v) {
    dv.setInt16(o, v, true);
  }
  function writeUint32(dv, o, v) {
    dv.setUint32(o, v, true);
  }

  /**
   * @param {string} styleName — "Regular" | "Bold" | "Italic" | "BoldItalic" (embedded name + filename stem)
   * @param {string} familyCss — loaded @font-face family string
   * @param {number} size
   * @param {number[]} codepoints sorted ascending
   */
  function buildBin(styleName, familyCss, size, codepoints) {
    var refC = document.createElement('canvas');
    refC.width = 256;
    refC.height = 128;
    var rctx = refC.getContext('2d');
    var ref = measureRef(rctx, familyCss, size);

    var rows = [];
    var bitmapChunks = [];
    var cum = 0;

    for (var i = 0; i < codepoints.length; i++) {
      var cp = codepoints[i];
      var g = rasterizeChar(familyCss, size, cp);
      if (g === null) continue;
      var dlen = g.bits.length;
      if (g.w > MAX_SIDE || g.h > MAX_SIDE) continue;
      var ax = g.adv;
      if (ax > MAX_ADVANCE) ax = MAX_ADVANCE;
      var row = new ArrayBuffer(24);
      var dv = new DataView(row);
      writeUint16(dv, 0, g.w);
      writeUint16(dv, 2, g.h);
      writeUint16(dv, 4, ax);
      writeInt16(dv, 6, g.left);
      writeInt16(dv, 8, g.top);
      writeUint32(dv, 10, dlen);
      writeUint32(dv, 14, cum);
      writeUint32(dv, 18, cp >>> 0);
      writeUint16(dv, 22, 0);
      rows.push(new Uint8Array(row));
      if (dlen) bitmapChunks.push(g.bits);
      cum += dlen;
    }

    if (!rows.length) {
      throw new Error('No glyphs generated for ' + styleName + ' at ' + size + 'pt');
    }

    var enc = new TextEncoder();
    var nameUtf8 = enc.encode(styleName);
    var nameLen = nameUtf8.length;
    if (nameLen > 255) throw new Error('Style name too long');

    var glyphCount = rows.length;
    var headerSize = 4 + 4 + 2 + nameLen + 2 + 2 + 2 + 1 + 2 + 4;
    var tableBytes = glyphCount * 24;
    var bitmapStart = headerSize + tableBytes;
    var totalSize = bitmapStart + cum;
    var out = new Uint8Array(totalSize);
    var dv = new DataView(out.buffer);
    var o = 0;
    writeUint32(dv, o, MAGIC);
    o += 4;
    writeUint32(dv, o, VERSION);
    o += 4;
    writeUint16(dv, o, nameLen);
    o += 2;
    out.set(nameUtf8, o);
    o += nameLen;
    writeInt16(dv, o, ref.lineHeight);
    o += 2;
    writeInt16(dv, o, ref.ascender);
    o += 2;
    writeInt16(dv, o, ref.descender);
    o += 2;
    out[o++] = 1;
    writeUint16(dv, o, 0);
    o += 2;
    writeUint32(dv, o, glyphCount);
    o += 4;
    for (var ri = 0; ri < rows.length; ri++) {
      out.set(rows[ri], o);
      o += 24;
    }
    for (var bi = 0; bi < bitmapChunks.length; bi++) {
      out.set(bitmapChunks[bi], o);
      o += bitmapChunks[bi].length;
    }
    return out;
  }

  async function registerFace(uniqueFamily, blob, descriptors) {
    var buf = await blob.arrayBuffer();
    var face = new FontFace(uniqueFamily, buf, descriptors || {});
    await face.load();
    document.fonts.add(face);
    return face;
  }

  /**
   * @returns {{ filename: string, blob: Blob }[]}
   */
  async function buildAllBins(opts) {
    var regular = opts.regular;
    var bold = opts.bold;
    var italic = opts.italic;
    var boldItalic = opts.boldItalic;
    var onLog = opts.onLog || function () {};
    var onProgress = opts.onProgress || function () {};

    if (!regular) throw new Error('Regular TTF/OTF is required');

    var token = Math.random().toString(36).slice(2, 11);
    var faces = [];
    var cps = collectCodepoints();

    try {
      var jobs = [];
      jobs.push({ key: 'Regular', blob: regular, desc: {} });
      if (bold) jobs.push({ key: 'Bold', blob: bold, desc: { weight: '700' } });
      if (italic) jobs.push({ key: 'Italic', blob: italic, desc: { style: 'italic' } });
      if (boldItalic) jobs.push({ key: 'BoldItalic', blob: boldItalic, desc: { weight: '700', style: 'italic' } });

      var outBins = [];
      var step = 0;
      var totalSteps = jobs.length * SIZES.length;

      for (var ji = 0; ji < jobs.length; ji++) {
        var job = jobs[ji];
        var fam = 'inxfp_' + token + '_' + job.key;
        var face = await registerFace(fam, job.blob, job.desc);
        faces.push(face);

        for (var si = 0; si < SIZES.length; si++) {
          var sz = SIZES[si];
          step++;
          onProgress(step, totalSteps, job.key, sz);
          await document.fonts.load(sz + 'pt "' + fam + '"');
          var bytes = buildBin(job.key, fam, sz, cps);
          var fn = job.key + '_' + sz + '.bin';
          outBins.push({ filename: fn, blob: new Blob([bytes], { type: 'application/octet-stream' }) });
          onLog('Packed ' + fn + ' (' + bytes.length + ' bytes)', 'success');
          await new Promise(function (r) {
            return setTimeout(r, 0);
          });
        }
      }
      return outBins;
    } finally {
      for (var fi = 0; fi < faces.length; fi++) {
        try {
          document.fonts.delete(faces[fi]);
        } catch (e) {}
      }
    }
  }

  global.InxFontPack = {
    MAGIC: MAGIC,
    SIZES: SIZES,
    sanitizeFamilyName: sanitizeFamilyName,
    buildAllBins: buildAllBins,
  };
})(typeof window !== 'undefined' ? window : globalThis);
