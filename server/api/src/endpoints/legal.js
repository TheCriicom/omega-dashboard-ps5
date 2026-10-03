'use strict';
// Documenti pubblici: privacy, termini e licenze (JSON per l'app, HTML per il
// browser) e il codice sorgente dell'app (GPL-3.0-or-later), impacchettato al
// momento dalla cartella montata in sola lettura.
const { spawn } = require('node:child_process');
const fs = require('node:fs');
const config = require('../config');
const legal = require('../legal');
const { HttpError } = require('../http');

const SRC_DIR = config.sourceDir;

const esc = (s) => String(s == null ? '' : s).replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));

function page(title, inner) {
  return `<!doctype html><html lang="it"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>${esc(title)} · Omega</title><style>
:root{--bg:#0c0e16;--panel:#161a26;--txt:#e8ecf5;--dim:#8b93a7;--acc:#38a0ff}
body{margin:0;background:var(--bg);color:var(--txt);font:16px/1.6 -apple-system,Segoe UI,Roboto,Helvetica,Arial,sans-serif}
main{max-width:820px;margin:0 auto;padding:32px 16px 64px}a{color:var(--acc)}h1{font-weight:300;font-size:34px;margin:0 0 8px}
h2{font-size:19px;margin:28px 0 6px}p{color:#cfd5e3;margin:0}nav{margin:0 0 28px;display:flex;gap:16px;flex-wrap:wrap}
.card{background:var(--panel);border-radius:14px;padding:18px 20px;margin-top:20px}.dim{color:var(--dim)}</style></head>
<body><main><nav><a href="/legal/privacy">Privacy</a><a href="/legal/terms">Termini d'uso</a><a href="/legal/licenses">Licenze</a><a href="/source">Codice sorgente</a></nav>${inner}</main></body></html>`;
}

function docHtml(doc) {
  return `<h1>${esc(doc.title)}</h1>${doc.sections.map((s) => `<h2>${esc(s.title)}</h2><p>${esc(s.body)}</p>`).join('')}`;
}

function sendHtml(res, html) {
  res.writeHead(200, {
    'content-type': 'text/html; charset=utf-8', 'content-length': Buffer.byteLength(html),
    'content-security-policy': "default-src 'none'; style-src 'unsafe-inline'; frame-ancestors 'none'", 'x-content-type-options': 'nosniff',
  });
  res.end(html);
  return { sent: true };
}

// GET /api/v1/legal
async function json() { return { status: 200, body: legal.all() }; }

// GET /legal  /legal/privacy  /legal/terms  /legal/licenses
async function index({ res }) { return sendHtml(res, page('Documenti', docHtml(legal.privacy()))); }
async function privacyPage({ res }) { return sendHtml(res, page('Privacy', docHtml(legal.privacy()))); }
async function termsPage({ res }) { return sendHtml(res, page('Termini d\'uso', docHtml(legal.terms()))); }
async function licensesPage({ res }) { return sendHtml(res, page('Licenze', docHtml(legal.licenses()))); }

// GET /source
async function sourcePage({ res }) {
  const ok = fs.existsSync(`${SRC_DIR}/omega-ui-src`);
  return sendHtml(res, page('Codice sorgente', `<h1>Codice sorgente</h1>
<p>L'app Omega per console è software libero: GNU General Public License versione 3 o successive. Qui trovi il sorgente completo dell'app e del demone che la accompagna, con le istruzioni di compilazione (build.sh).</p>
<div class="card">${ok ? '<p><a href="/source/omega-src.tar.gz">Scarica omega-src.tar.gz</a></p><p class="dim">Archivio generato al momento dal codice in uso sul server.</p>'
    : '<p class="dim">Archivio momentaneamente non disponibile.</p>'}</div>`));
}

// GET /source/omega-src.tar.gz
async function sourceTar({ res }) {
  if (!fs.existsSync(`${SRC_DIR}/omega-ui-src`)) throw new HttpError(503, 'source_unavailable');
  const parts = ['omega-ui-src', 'omega-redirect-src'].filter((p) => fs.existsSync(`${SRC_DIR}/${p}`));
  const tar = spawn('tar', ['-czf', '-', '-C', SRC_DIR,
    '--exclude=build', '--exclude=*.o', '--exclude=omega-ui-desktop', '--exclude=data', '--exclude=.DS_Store', ...parts]);
  res.writeHead(200, { 'content-type': 'application/gzip', 'content-disposition': 'attachment; filename="omega-src.tar.gz"', 'cache-control': 'no-store' });
  tar.stdout.pipe(res);
  tar.stderr.on('data', () => {});
  tar.on('error', () => res.destroy());
  return { sent: true };
}

module.exports = { json, index, privacyPage, termsPage, licensesPage, sourcePage, sourceTar };
