'use strict';
// Sito pubblico (play.omegasuite.it): pagine e file statici di site/.
// Le richieste Range servono ai video: Safari e iOS non li riproducono senza.
const fs = require('node:fs');
const fsp = require('node:fs/promises');
const path = require('node:path');
const config = require('../config');
const { HttpError } = require('../http');
const info = require('./info');
const lang = require('../lang');

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

// ------------------------------------------------------------- lingue --
// Il sito è generato in site/index.html (inglese) e site/<codice>/index.html,
// site/<codice>/installa.html per ognuna delle lingue di lang.CODES. Le
// cartelle possono mancare: allora si serve la pagina di site/.
const exists = (rel) => { try { return fs.statSync(path.join(ROOT, rel)).isFile(); } catch { return false; } };

function redirect(res, location, vary) {
  const headers = { location, 'cache-control': 'no-cache', 'content-length': 0 };
  if (vary) headers.vary = 'Accept-Language, Cookie';
  res.writeHead(302, headers);
  res.end();
  return { sent: true };
}

// Pagina della lingua scelta, oppure redirect a /<lang>/... se esiste.
// `page` è "index.html" o "installa.html", `suffix` il percorso dopo /<lang>/.
// La scelta fatta dal visitatore (cookie omega_lang, scritto dal selettore del
// sito) vale più della lingua del browser. Senza nessuna indicazione (crawler)
// si serve la pagina inglese, a cui punta x-default, senza redirect.
function chosenLang(ctx) {
  const m = /(?:^|;\s*)omega_lang=([A-Za-z-]+)/.exec(ctx.req.headers.cookie || '');
  if (m && lang.isCode(m[1])) return m[1];
  return ctx.langExplicit ? ctx.lang : null;
}

async function negotiate(ctx, page, suffix) {
  const { req, res, url } = ctx;
  const code = chosenLang(ctx);
  if (code && exists(path.join(code, page))) {
    const q = new URLSearchParams(url.search);
    q.delete('lang');                       // la lingua ora è nel percorso
    const rest = q.toString();
    return redirect(res, `/${code}/${suffix}${rest ? `?${rest}` : ''}`, true);
  }
  if (exists(page)) {
    res.setHeader('vary', 'Accept-Language, Cookie');
    return serve(req, res, page);
  }
  return null;
}

// GET / — 302 a /<lingua>/ (?lang=, Accept-Language, "en"); senza la cartella
// della lingua si serve site/index.html; senza sito la descrizione dell'api.
async function home(ctx) {
  const out = await negotiate(ctx, 'index.html', '');
  return out || info.index(ctx);
}

// GET /installa — 302 a /<lingua>/installa
async function install(ctx) {
  const out = await negotiate(ctx, 'installa.html', 'installa');
  if (!out) throw new HttpError(404, 'not_found');
  return out;
}

// GET /:lang  e  /:lang/  — solo i 27 codici (il router accetta solo quelli)
async function langHome({ req, res, url, params }) {
  const code = params.lang;
  if (!lang.isCode(code)) throw new HttpError(404, 'not_found');
  if (!url.pathname.endsWith('/')) return redirect(res, `/${code}/${url.search}`, false);
  const rel = exists(path.join(code, 'index.html')) ? path.join(code, 'index.html') : 'index.html';
  return serve(req, res, rel);
}

// GET /:lang/installa
async function langInstall({ req, res, params }) {
  const code = params.lang;
  if (!lang.isCode(code)) throw new HttpError(404, 'not_found');
  const rel = exists(path.join(code, 'installa.html')) ? path.join(code, 'installa.html') : 'installa.html';
  return serve(req, res, rel);
}

// GET /assets/:dir/:file  e  /assets/:file
async function asset({ req, res, params }) {
  const parts = [params.dir, params.file].filter(Boolean);
  if (!parts.every((p) => SEGMENT.test(p))) throw new HttpError(404, 'not_found');
  return serve(req, res, path.join('assets', ...parts));
}

module.exports = { home, install, langHome, langInstall, asset };
