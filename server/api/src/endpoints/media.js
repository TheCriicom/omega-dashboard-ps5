'use strict';
// Avatar e copertina personalizzati. La console carica il file così com'è
// (cattura PS5 in JPEG XR HDR, video WebM, oppure JPG/PNG/MP4 da USB) e qui
// diventa una serie di JPEG: una foto è un fotogramma, un video 3 s a 10 fps.
// Avatar 256×256, copertina 1170×195.
//
// Le catture PS5 sono HDR (PQ/BT.2020, 10 bit impacchettati): JxrDecApp le
// decodifica in TIFF e qui si fa il tone mapping verso SDR.
const fs = require('node:fs');
const fsp = require('node:fs/promises');
const path = require('node:path');
const crypto = require('node:crypto');
const { execFile } = require('node:child_process');
const config = require('../config');
const db = require('../db');
const { HttpError } = require('../http');
const limiter = require('../ratelimit');

const ROOT = config.mediaDir;
const MEDIA_ID = /^[a-f0-9]{16}$/;
const MAX_UPLOAD = 96 * 1024 * 1024;
const TARGET = { avatar: [256, 256], cover: [1170, 195] };
const EXT = new Set(['jxr', 'jpg', 'jpeg', 'png', 'webp', 'webm', 'mp4', 'mov', 'mkv']);
const VIDEO = new Set(['webm', 'mp4', 'mov', 'mkv']);

function run(cmd, args, timeout = 120000) {
  return new Promise((resolve, reject) => {
    execFile(cmd, args, { timeout, maxBuffer: 8 * 1024 * 1024 }, (err, stdout, stderr) => {
      if (err) { err.stderr = String(stderr || '').slice(-800); reject(err); } else resolve(String(stdout));
    });
  });
}

// Elimina la cartella di un avatar/copertina; id non validi si ignorano.
async function removeMedia(id) {
  if (id && MEDIA_ID.test(id)) await fsp.rm(path.join(ROOT, id), { recursive: true, force: true });
}

function saveBody(req, file) {
  return new Promise((resolve, reject) => {
    let size = 0;
    const out = fs.createWriteStream(file);
    req.on('data', (c) => {
      size += c.length;
      if (size > MAX_UPLOAD) { req.destroy(); out.destroy(); reject(new HttpError(413, 'file_too_large', 'massimo 96 MB')); }
    });
    req.pipe(out);
    out.on('finish', () => resolve(size));
    out.on('error', reject);
    req.on('error', reject);
  });
}

// JPEG XR HDR (via TIFF) → PPM SDR
function readTiff(buf) {
  const le = buf.toString('latin1', 0, 2) === 'II';
  const u16 = (o) => (le ? buf.readUInt16LE(o) : buf.readUInt16BE(o));
  const u32 = (o) => (le ? buf.readUInt32LE(o) : buf.readUInt32BE(o));
  const ifd = u32(4), n = u16(ifd);
  const t = {};
  for (let i = 0; i < n; i++) {
    const e = ifd + 2 + i * 12, tag = u16(e), type = u16(e + 2), count = u32(e + 4);
    const size = type === 3 ? 2 : 4;
    const valOff = count * size <= 4 ? e + 8 : u32(e + 8);
    t[tag] = type === 3 ? u16(valOff) : u32(valOff);
  }
  return { le, w: t[256], h: t[257], bits: t[258], spp: t[277] || 3, offset: t[273], compression: t[259] };
}

const PQ = (() => {
  const m1 = 0.1593017578125, m2 = 78.84375, c1 = 0.8359375, c2 = 18.8515625, c3 = 18.6875;
  const lut = new Float32Array(1024);
  for (let i = 0; i < 1024; i++) {
    const p = Math.pow(i / 1023, 1 / m2);
    lut[i] = (10000 * Math.pow(Math.max(p - c1, 0) / (c2 - c3 * p), 1 / m1)) / 203;   // 203 nit = bianco di riferimento
  }
  return lut;
})();

function hdrTiffToPpm(buf, info, step) {
  const w = Math.floor(info.w / step), h = Math.floor(info.h / step);
  const out = Buffer.alloc(w * h * 3);
  const tm = (c) => { c = c > 0 ? c : 0; return c / (1 + c / 4); };          // Reinhard esteso (bianco a 4)
  const enc = (c) => Math.round(255 * Math.pow(c > 1 ? 1 : c < 0 ? 0 : c, 1 / 2.2));
  const n = step * step;
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      let r = 0, g = 0, b = 0;
      for (let dy = 0; dy < step; dy++) {
        let o = info.offset + ((y * step + dy) * info.w + x * step) * 4;
        for (let dx = 0; dx < step; dx++, o += 4) {
          const v = info.le ? buf.readUInt32LE(o) : buf.readUInt32BE(o);
          r += PQ[v & 1023]; g += PQ[(v >>> 10) & 1023]; b += PQ[(v >>> 20) & 1023];
        }
      }
      r /= n; g /= n; b /= n;
      // BT.2020 → BT.709
      const R = 1.6605 * r - 0.5876 * g - 0.0728 * b;
      const G = -0.1246 * r + 1.1329 * g - 0.0083 * b;
      const B = -0.0182 * r - 0.1006 * g + 1.1187 * b;
      const k = (y * w + x) * 3;
      out[k] = enc(tm(R)); out[k + 1] = enc(tm(G)); out[k + 2] = enc(tm(B));
    }
  }
  return Buffer.concat([Buffer.from(`P6\n${w} ${h}\n255\n`), out]);
}

function cropFilter([w, h]) {
  return `scale=${w}:${h}:force_original_aspect_ratio=increase:flags=lanczos,crop=${w}:${h}`;
}

async function isHdrVideo(file) {
  try {
    const out = await run('ffprobe', ['-v', 'error', '-select_streams', 'v:0', '-show_entries', 'stream=color_transfer', '-of', 'csv=p=0', file], 20000);
    return /smpte2084|arib-std-b67/.test(out);
  } catch { return false; }
}

async function videoFrames(file, dir, target) {
  const base = `fps=10,${cropFilter(target)}`;
  const tonemap = 'zscale=t=linear:npl=203,format=gbrpf32le,zscale=p=bt709,tonemap=tonemap=hable:desat=0,zscale=t=bt709:m=bt709:r=tv,format=yuv420p';
  const hdr = await isHdrVideo(file);
  const attempt = (vf) => run('ffmpeg', ['-v', 'error', '-y', '-t', '3', '-i', file, '-vf', vf, '-frames:v', '30', '-q:v', '4', path.join(dir, '%03d.jpg')]);
  try { await attempt(hdr ? `${tonemap},${base}` : base); } catch (e) {
    if (!hdr) throw e;
    await attempt(base); // ffmpeg senza zscale: colori meno fedeli, ma converte
  }
}

async function imageFrame(file, ext, dir, target, tmpBase) {
  let src = file;
  if (ext === 'jxr') {
    const tif = `${tmpBase}.tif`;
    await run('JxrDecApp', ['-i', file, '-o', tif], 60000);
    const buf = await fsp.readFile(tif);
    const info = readTiff(buf);
    await fsp.unlink(tif).catch(() => {});
    if (info.bits === 10 && info.compression === 1) {
      const step = Math.max(1, Math.floor(Math.min(info.w / target[0], info.h / target[1]) / 1.5));
      src = `${tmpBase}.ppm`;
      await fsp.writeFile(src, hdrTiffToPpm(buf, info, step));
    } else {
      src = `${tmpBase}.8.tif`;
      await fsp.writeFile(src, buf);
    }
  }
  await run('ffmpeg', ['-v', 'error', '-y', '-i', src, '-vf', cropFilter(target), '-frames:v', '1', '-q:v', '3', path.join(dir, '001.jpg')]);
  if (src !== file) await fsp.unlink(src).catch(() => {});
}

// POST /api/v1/media/upload?kind=avatar|cover&ext=jxr|jpg|png|webm|mp4  (corpo = file)
async function upload({ req, auth, url }) {
  const wait = limiter.hit(`media|${auth.accountId}`, 12, 600);
  if (wait) throw new HttpError(429, 'too_many_uploads');
  const kind = url.searchParams.get('kind');
  const ext = String(url.searchParams.get('ext') || '').toLowerCase().replace(/[^a-z0-9]/g, '');
  if (!TARGET[kind]) throw new HttpError(400, 'invalid_kind');
  if (!EXT.has(ext)) throw new HttpError(400, 'unsupported_type');

  const id = crypto.randomBytes(8).toString('hex');
  const dir = path.join(ROOT, id);
  const tmpBase = path.join(ROOT, `.up-${id}`);
  const file = `${tmpBase}.${ext}`;
  await fsp.mkdir(dir, { recursive: true });
  try {
    const size = await saveBody(req, file);
    if (size < 64) throw new HttpError(400, 'empty_file');
    if (VIDEO.has(ext)) await videoFrames(file, dir, TARGET[kind]);
    else await imageFrame(file, ext, dir, TARGET[kind], tmpBase);
    const frames = (await fsp.readdir(dir)).filter((f) => f.endsWith('.jpg')).length;
    if (!frames) throw new HttpError(422, 'conversion_failed');
    const col = kind === 'avatar' ? 'avatar' : 'cover';
    const old = (await db.query(`SELECT ${col}_media AS m FROM lab_account WHERE account_id=$1`, [auth.accountId])).rows[0];
    await db.query(`UPDATE lab_account SET ${col}_media=$2, ${col}_frames=$3 WHERE account_id=$1`, [auth.accountId, id, frames]);
    await removeMedia(old && old.m);
    return { status: 201, body: { kind, media: id, frames } };
  } catch (err) {
    await fsp.rm(dir, { recursive: true, force: true });
    if (err instanceof HttpError) throw err;
    console.error(JSON.stringify({ ts: new Date().toISOString(), event: 'media_error', error: err.message, stderr: err.stderr }));
    throw new HttpError(422, 'conversion_failed');
  } finally {
    await fsp.unlink(file).catch(() => {});
  }
}

// POST /api/v1/media/clear?kind=avatar|cover
async function clear({ auth, url }) {
  const kind = url.searchParams.get('kind');
  if (!TARGET[kind]) throw new HttpError(400, 'invalid_kind');
  const col = kind === 'avatar' ? 'avatar' : 'cover';
  const old = (await db.query(`SELECT ${col}_media AS m FROM lab_account WHERE account_id=$1`, [auth.accountId])).rows[0];
  await db.query(`UPDATE lab_account SET ${col}_media=NULL, ${col}_frames=0 WHERE account_id=$1`, [auth.accountId]);
  await removeMedia(old && old.m);
  return { status: 200, body: { result: 'ok' } };
}

// GET /api/v1/media/:id/:frame
async function frame({ params, res }) {
  const id = String(params.id), n = Number.parseInt(params.frame, 10);
  if (!MEDIA_ID.test(id) || !(n >= 1 && n <= 60)) throw new HttpError(404, 'not_found');
  const file = path.join(ROOT, id, `${String(n).padStart(3, '0')}.jpg`);
  let buf;
  try { buf = await fsp.readFile(file); } catch { throw new HttpError(404, 'not_found'); }
  res.writeHead(200, { 'content-type': 'image/jpeg', 'content-length': buf.length, 'cache-control': 'max-age=31536000, immutable' });
  res.end(buf);
  return { sent: true };
}

module.exports = { upload, clear, frame, removeMedia };
