'use strict';
// Documenti pubblici: privacy, termini e licenze (JSON per l'app, HTML per il
// browser) e il codice sorgente dell'app (GPL-3.0-or-later), impacchettato al
// momento dalla cartella montata in sola lettura.
const { spawn } = require('node:child_process');
const fs = require('node:fs');
const config = require('../config');
const legal = require('../legal');
const lang = require('../lang');
const messages = require('../messages');
const brand = require('../brand');
const { HttpError } = require('../http');

const SRC_DIR = config.sourceDir;

const esc = (s) => String(s == null ? '' : s).replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));

// Lingua delle pagine web: quella della richiesta se ha un catalogo, altrimenti l'inglese.
const webLang = (code) => (messages.available().includes(code) ? code : lang.FALLBACK);

// "Sviluppato da {developer}" con il nome come link.
function credit(code) {
  const link = `<a href="${esc(legal.DEVELOPER.url)}">${esc(legal.DEVELOPER.name)}</a>`;
  return messages.t(code, 'web.developed_by', { developer: '\u0000' }).split('\u0000').map(esc).join(link);
}

// Selettore della lingua: link alla stessa pagina con ?lang=.
function langSelector(pathname, current, codes) {
  const items = codes.map((c) => (c === current
    ? `<strong lang="${c}">${esc(lang.NAMES[c])}</strong>`
    : `<a href="${esc(pathname)}?lang=${encodeURIComponent(c)}" lang="${c}" hreflang="${c}">${esc(lang.NAMES[c])}</a>`));
  return `<nav class="langs" aria-label="${esc(messages.t(current, 'web.language'))}">${items.join(' ')}</nav>`;
}

function page({ code, title, inner, pathname, codes }) {
  const t = (k) => esc(messages.t(code, `web.${k}`));
  return `<!doctype html><html lang="${code}"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>${esc(title)} · Omega</title><link rel="icon" type="image/svg+xml" href="${brand.ICON_URI}"><style>
:root{--bg:#0c0e16;--panel:#161a26;--txt:#e8ecf5;--dim:#8b93a7;--acc:#38a0ff}
body{margin:0;background:var(--bg);color:var(--txt);font:16px/1.6 -apple-system,Segoe UI,Roboto,Helvetica,Arial,sans-serif}
main{max-width:820px;margin:0 auto;padding:32px 16px 64px}a{color:var(--acc)}h1{font-weight:300;font-size:34px;margin:0 0 8px}
h2{font-size:19px;margin:28px 0 6px}p{color:#cfd5e3;margin:0}nav{margin:0 0 28px;display:flex;gap:16px;flex-wrap:wrap}
.card{background:var(--panel);border-radius:14px;padding:18px 20px;margin-top:20px}.dim{color:var(--dim)}
.notice{background:var(--panel);border-left:3px solid var(--acc);border-radius:8px;padding:12px 16px;margin:16px 0 0}
.notice h2{font-size:15px;margin:0 0 4px}.notice p{font-size:14px}
nav.langs{gap:6px 12px;font-size:14px;margin:0 0 20px}nav.langs strong{color:var(--txt);font-weight:600}
footer{max-width:820px;margin:0 auto;padding:0 16px 40px;color:var(--dim);font-size:14px;border-top:1px solid var(--panel)}footer p{color:var(--dim);margin-top:16px}</style></head>
<body><main>${langSelector(pathname, code, codes)}<nav><a href="/legal/privacy${q(code)}">${t('privacy')}</a><a href="/legal/terms${q(code)}">${t('terms')}</a><a href="/legal/licenses${q(code)}">${t('licenses')}</a><a href="/source${q(code)}">${t('source')}</a></nav>${inner}</main>
<footer><p>${credit(code)} · <a href="${esc(legal.DEVELOPER.url)}">${esc(legal.DEVELOPER.url.replace(/^https?:\/\//, ''))}</a></p></footer></body></html>`;
}

// I link interni portano con sé la lingua scelta col selettore.
const q = (code) => `?lang=${encodeURIComponent(code)}`;

function docHtml(doc) {
  const [first, ...rest] = doc.sections;
  const notice = first && first.id === 'translation_notice'
    ? `<div class="notice" role="note"><h2>${esc(first.title)}</h2><p>${esc(first.body)}</p></div>` : '';
  const sections = notice ? rest : doc.sections;
  return `<h1>${esc(doc.title)}</h1>${notice}${sections.map((s) => `<h2 id="${esc(s.id)}">${esc(s.title)}</h2><p>${esc(s.body)}</p>`).join('')}`;
}

function sendHtml(res, html, code) {
  res.writeHead(200, {
    'content-type': 'text/html; charset=utf-8', 'content-length': Buffer.byteLength(html),
    'content-language': code, vary: 'Accept-Language',
    'content-security-policy': "default-src 'none'; style-src 'unsafe-inline'; img-src data:; frame-ancestors 'none'", 'x-content-type-options': 'nosniff',
  });
  res.end(html);
  return { sent: true };
}

// GET /api/v1/legal  (?lang= o Accept-Language)
async function json({ lang: code }) { return { status: 200, body: legal.all(code) }; }

// GET /legal  /legal/privacy  /legal/terms  /legal/licenses
function docPage(which) {
  return async function ({ res, url, lang: code }) {
    const doc = legal[which](code);
    return sendHtml(res, page({ code: doc.lang, title: doc.title, inner: docHtml(doc), pathname: url.pathname, codes: legal.available() }), doc.lang);
  };
}

// GET /source
async function sourcePage({ res, url, lang: code }) {
  const l = webLang(code);
  const t = (k) => esc(messages.t(l, `web.${k}`));
  const ok = fs.existsSync(`${SRC_DIR}/omega-ui-src`);
  return sendHtml(res, page({ code: l, title: messages.t(l, 'web.source'), pathname: url.pathname, codes: messages.available(), inner: `<h1>${t('source')}</h1>
<p>${t('source_intro')}</p>
<div class="card">${ok ? `<p><a href="/source/omega-src.tar.gz">${t('source_download')}</a></p><p class="dim">${t('source_generated')}</p>`
    : `<p class="dim">${t('source_unavailable')}</p>`}</div>` }), l);
}

// GET /source/omega-src.tar.gz
async function sourceTar({ res }) {
  if (!fs.existsSync(`${SRC_DIR}/omega-ui-src`)) throw new HttpError(503, 'source_unavailable');
  const parts = ['omega-ui-src', 'omega-redirect-src'].filter((p) => fs.existsSync(`${SRC_DIR}/${p}`));
  const tar = spawn('tar', ['-czf', '-', '-C', SRC_DIR,
    '--exclude=build', '--exclude=*.o', '--exclude=omega-ui-desktop', '--exclude=data', '--exclude=.DS_Store',
    // script di rilascio e segreti non vanno mai nell'archivio pubblico
    '--exclude=publish-update.sh', '--exclude=.env*', '--exclude=*.pem', '--exclude=*.key', ...parts]);
  res.writeHead(200, { 'content-type': 'application/gzip', 'content-disposition': 'attachment; filename="omega-src.tar.gz"', 'cache-control': 'no-store' });
  tar.stdout.pipe(res);
  tar.stderr.on('data', () => {});
  tar.on('error', () => res.destroy());
  return { sent: true };
}

module.exports = {
  json, index: docPage('privacy'), privacyPage: docPage('privacy'), termsPage: docPage('terms'), licensesPage: docPage('licenses'),
  sourcePage, sourceTar,
};
