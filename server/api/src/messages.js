'use strict';
// Catalogo dei testi generati dal server (notifiche, pagine web, descrizione
// dell'api): src/i18n/<codice>.json, una chiave "sezione.voce" per testo.
// Una voce mancante in una lingua ricade sull'inglese e poi sull'italiano.
const fs = require('node:fs');
const path = require('node:path');
const lang = require('./lang');

const DIR = path.join(__dirname, 'i18n');
const catalogs = {};
for (const code of lang.CODES) {
  try { catalogs[code] = JSON.parse(fs.readFileSync(path.join(DIR, `${code}.json`), 'utf8')); } catch { /* lingua senza catalogo */ }
}

function lookup(code, key) {
  const [sec, item] = key.split('.');
  for (const c of [code, lang.FALLBACK, lang.SOURCE]) {
    const v = catalogs[c] && catalogs[c][sec] && catalogs[c][sec][item];
    if (typeof v === 'string') return v;
  }
  return null;
}

// t('it', 'notify.online', { actor: 'x' }) → "x è online"
function t(code, key, vars) {
  const v = lookup(code, key);
  return v == null ? key : lang.format(v, vars);
}

// come t(), ma una voce assente dà `fallback` (es. un codice di motivo non tradotto)
function tOr(code, key, fallback, vars) {
  const v = lookup(code, key);
  return v == null ? fallback : lang.format(v, vars);
}

const available = () => lang.CODES.filter((c) => catalogs[c]);

module.exports = { t, tOr, available };
