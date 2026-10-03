'use strict';
// Sito pubblico (play.omegasuite.it): pagine e file statici di site/.
// Le richieste Range servono ai video: Safari e iOS non li riproducono senza.
const fs = require('node:fs');
const fsp = require('node:fs/promises');
const path = require('node:path');
const config = require('../config');
const { HttpError } = require('../http');
const info = require('./info');

const ROOT = path.resolve(config.siteDir);
const TYPES = {
  '.html': 'text/html; charset=utf-8', '.css': 'text/css; charset=utf-8', '.js': 'text/javascript; charset=utf-8',
  '.svg': 'image/svg+xml', '.png': 'image/png', '.jpg': 'image/jpeg', '.webp': 'image/webp', '.ico': 'image/x-icon',
  '.mp4': 'video/mp4', '.webm': 'video/webm', '.woff2': 'font/woff2', '.txt': 'text/plain; charset=utf-8', '.json': 'application/json',
};
const SEGMENT = /^[A-Za-z0-9][A-Za-z0-9._-]{0,120}$/;
const CSP = [
  "default-src 'self'", "img-src 'self' data:", "media-src 'self'", "style-src 'self' 'unsafe-inline'", "script-src 'self'",
  // l'installazione da console manda l'installer a websrv sulla console stessa
  "connect-src 'self' http://127.0.0.1:8080", "frame-ancestors 'none'", "base-uri 'none'", "form-action 'self'",
].join('; ');

async function serve(req, res, rel) {
  const file = path.join(ROOT, rel);
  if (!file.startsWith(ROOT + path.sep)) throw new HttpError(404, 'not_found');
  let st;
  try { st = await fsp.stat(file); } catch { throw new HttpError(404, 'not_found'); }
  if (!st.isFile()) throw new HttpError(404, 'not_found');
  const ext = path.extname(file);
  const headers = {
    'content-type': TYPES[ext] || 'application/octet-stream',
    'cache-control': ext === '.html' ? 'no-cache' : 'public, max-age=3600',
    'x-content-type-options': 'nosniff', 'accept-ranges': 'bytes',
  };
  if (ext === '.html') { headers['content-security-policy'] = CSP; headers['referrer-policy'] = 'strict-origin-when-cross-origin'; }
  const range = /^bytes=(\d*)-(\d*)$/.exec(req.headers.range || '');
  if (range && (range[1] || range[2])) {
    let start = range[1] ? Number(range[1]) : st.size - Number(range[2]);
    let end = range[1] && range[2] ? Number(range[2]) : st.size - 1;
    if (start < 0) start = 0;
    if (start >= st.size || end < start) {
      res.writeHead(416, { 'content-range': `bytes */${st.size}` });
      res.end();
      return { sent: true };
    }
    end = Math.min(end, st.size - 1);
    res.writeHead(206, { ...headers, 'content-range': `bytes ${start}-${end}/${st.size}`, 'content-length': end - start + 1 });
    fs.createReadStream(file, { start, end }).on('error', () => res.destroy()).pipe(res);
    return { sent: true };
  }
  res.writeHead(200, { ...headers, 'content-length': st.size });
  if (req.method === 'HEAD') { res.end(); return { sent: true }; }
  fs.createReadStream(file).on('error', () => res.destroy()).pipe(res);
  return { sent: true };
}

// GET / — chi ospita un server senza sito vede la descrizione dell'api
async function home(ctx) {
  if (!fs.existsSync(path.join(ROOT, 'index.html'))) return info.index(ctx);
  return serve(ctx.req, ctx.res, 'index.html');
}

// GET /installa
async function install({ req, res }) { return serve(req, res, 'installa.html'); }

// GET /assets/:dir/:file  e  /assets/:file
async function asset({ req, res, params }) {
  const parts = [params.dir, params.file].filter(Boolean);
  if (!parts.every((p) => SEGMENT.test(p))) throw new HttpError(404, 'not_found');
  return serve(req, res, path.join('assets', ...parts));
}

module.exports = { home, install, asset };
