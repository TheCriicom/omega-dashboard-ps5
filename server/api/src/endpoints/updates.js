'use strict';
// Aggiornamenti dell'app console: manifest firmato ed ELF. Li prepara e firma
// omega-ui-src/publish-update.sh con una chiave Ed25519 che il server non ha:
// qui si servono e basta. Sono pubblici perché l'app li chiede senza sessione,
// anche quando l'utente usa un altro server.
const fs = require('node:fs');
const fsp = require('node:fs/promises');
const path = require('node:path');
const config = require('../config');
const { HttpError } = require('../http');

const DIR = config.updateDir;
const NAME = /^[A-Za-z0-9][A-Za-z0-9._-]{0,120}$/;
const TYPES = {
  '.json': 'application/json; charset=utf-8', '.sig': 'text/plain; charset=utf-8', '.elf': 'application/octet-stream',
  '.js': 'text/javascript; charset=utf-8', '.png': 'image/png', '.zip': 'application/zip',
};
const VERSIONED = new Set(['.elf', '.js', '.png', '.zip']);

// GET /updates/:file
async function file({ res, params }) {
  const name = String(params.file || '');
  const type = TYPES[path.extname(name)];
  if (!NAME.test(name) || !type) throw new HttpError(404, 'not_found');
  const p = path.join(DIR, name);
  let st;
  try { st = await fsp.stat(p); } catch { throw new HttpError(404, 'not_found'); }
  if (!st.isFile()) throw new HttpError(404, 'not_found');
  res.writeHead(200, {
    'content-type': type, 'content-length': st.size,
    // il manifest cambia a ogni pubblicazione; gli ELF hanno la versione nel nome
    'cache-control': VERSIONED.has(path.extname(name)) ? 'public, max-age=86400' : 'no-cache',
  });
  fs.createReadStream(p).on('error', () => res.destroy()).pipe(res);
  return { sent: true };
}

async function manifest() {
  try { return JSON.parse(await fsp.readFile(path.join(DIR, 'manifest.json'), 'utf8')); } catch { return null; }
}

async function send(res, file, downloadName) {
  const p = path.join(DIR, file);
  let st;
  try { st = await fsp.stat(p); } catch { throw new HttpError(404, 'not_published'); }
  res.writeHead(200, {
    'content-type': TYPES[path.extname(file)] || 'application/octet-stream', 'content-length': st.size,
    'content-disposition': `attachment; filename="${downloadName}"`, 'cache-control': 'no-cache',
  });
  fs.createReadStream(p).on('error', () => res.destroy()).pipe(res);
  return { sent: true };
}

// GET /download/latest — pacchetto completo dell'ultima versione pubblicata
async function latest({ res }) {
  const m = await manifest();
  if (!m || !m.package || !NAME.test(m.package.file)) throw new HttpError(404, 'not_published');
  return send(res, m.package.file, m.package.file);
}

// GET /download/omega-installer.elf
async function installer({ res }) {
  const m = await manifest();
  const c = m && (m.components || []).find((x) => x.id === 'installer');
  if (!c || !NAME.test(c.file)) throw new HttpError(404, 'not_published');
  return send(res, c.file, 'omega-installer.elf');
}

module.exports = { file, latest, installer };
