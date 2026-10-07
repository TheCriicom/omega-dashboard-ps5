'use strict';
// La web app di Omega (play.omegasuite.it/app): file statici di webapp/, una
// pagina sola installabile sul telefono (manifest + service worker).
const fs = require('node:fs');
const fsp = require('node:fs/promises');
const path = require('node:path');
const { HttpError } = require('../http');

const ROOT = path.resolve(__dirname, '..', '..', 'webapp');
const TYPES = {
  '.html': 'text/html; charset=utf-8', '.css': 'text/css; charset=utf-8', '.js': 'text/javascript; charset=utf-8',
  '.svg': 'image/svg+xml', '.png': 'image/png', '.webp': 'image/webp', '.ico': 'image/x-icon', '.json': 'application/json',
  '.webmanifest': 'application/manifest+json',
};
const SEGMENT = /^[A-Za-z0-9][A-Za-z0-9._-]{0,120}$/;
const CSP = [
  "default-src 'self'", "img-src 'self' data: blob:", "style-src 'self' 'unsafe-inline'", "script-src 'self'",
  "connect-src 'self'", "manifest-src 'self'", "worker-src 'self'", "frame-ancestors 'none'", "base-uri 'none'", "form-action 'self'",
].join('; ');

async function send(req, res, rel) {
  const file = path.join(ROOT, rel);
  if (!file.startsWith(ROOT + path.sep)) throw new HttpError(404, 'not_found');
  let st; try { st = await fsp.stat(file); } catch { throw new HttpError(404, 'not_found'); }
  if (!st.isFile()) throw new HttpError(404, 'not_found');
  const ext = path.extname(file);
  const headers = {
    'content-type': TYPES[ext] || 'application/octet-stream', 'content-length': st.size, 'x-content-type-options': 'nosniff',
    // la pagina e il service worker sempre freschi, il resto lo versiona il service worker
    'cache-control': ext === '.html' || rel === 'sw.js' ? 'no-cache' : 'public, max-age=300',
  };
  if (ext === '.html') { headers['content-security-policy'] = CSP; headers['referrer-policy'] = 'same-origin'; }
  if (rel === 'sw.js') headers['service-worker-allowed'] = '/app/';
  res.writeHead(200, headers);
  if (req.method === 'HEAD') { res.end(); return { sent: true }; }
  fs.createReadStream(file).on('error', () => res.destroy()).pipe(res);
  return { sent: true };
}

// GET /app → index.html; /app/<file>; /app/<dir>/<file>
async function index({ req, res }) { return send(req, res, 'index.html'); }
async function file({ req, res, params }) {
  const parts = [params.dir, params.file].filter(Boolean);
  if (!parts.every((p) => SEGMENT.test(p))) throw new HttpError(404, 'not_found');
  return send(req, res, parts.join('/'));
}

module.exports = { index, file };
