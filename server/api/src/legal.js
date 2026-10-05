'use strict';
// Testi legali del servizio (privacy, termini, licenze): un'unica fonte per
// l'app (JSON a sezioni) e per il web (HTML). Descrivono ciò che il servizio
// fa davvero: se cambiano i dati raccolti, si aggiornano legal/it.json e
// TERMS_VERSION. Fa fede l'italiano (legal/it.json); legal/<codice>.json sono
// traduzioni con le stesse sezioni e gli stessi segnaposto, e si aprono con
// l'avviso che in caso di differenze prevale il testo italiano. Tradurre non
// cambia i termini: TERMS_VERSION resta la stessa.
const fs = require('node:fs');
const path = require('node:path');
const config = require('./config');
const lang = require('./lang');

const TERMS_VERSION = '2026-10-05.2';
const DEVELOPER = { name: 'TheCriicom', url: 'https://outlinedigital.it' };
const DOCS = ['privacy', 'terms', 'licenses'];

const texts = {};
for (const code of lang.CODES) {
  try { texts[code] = JSON.parse(fs.readFileSync(path.join(__dirname, 'legal', `${code}.json`), 'utf8')); } catch { /* non tradotta */ }
}
if (!texts[lang.SOURCE]) throw new Error('legal/it.json mancante: è il testo che fa fede');

const available = () => lang.CODES.filter((c) => texts[c]);
// lingua servita: quella chiesta se tradotta, altrimenti l'inglese, altrimenti l'italiano
const pick = (code) => (texts[code] ? code : texts[lang.FALLBACK] ? lang.FALLBACK : lang.SOURCE);

// Dati del titolare dal .env (LEGAL_*): finché mancano si mostra "[da completare]" (tradotto).
function controller() {
  const l = config.legal;
  return {
    name: l.controllerName, vat: l.controllerVat, email: l.contactEmail, address: l.controllerAddress, hosting: l.hostingProvider,
    complete: !!(l.controllerName && l.contactEmail),
  };
}

// Valori dei segnaposto per una lingua.
function vars(T) {
  const c = controller();
  const missing = T.missing || texts[lang.SOURCE].missing;
  const name = c.name || missing;
  return {
    name,
    controller: name + (c.vat ? lang.format(T.fragments.vat, { vat: c.vat }) : '') + (c.address ? `, ${c.address}` : ''),
    email: c.email || missing,
    hosting_at: c.hosting ? lang.format(T.fragments.hosting_at, { hosting: c.hosting }) : '',
    days: config.logRetentionDays,
    version: TERMS_VERSION,
    url: config.publicUrl,
    developer: DEVELOPER.name,
    developer_url: DEVELOPER.url,
  };
}

// { title, lang, sections:[{id,title,body}] }; nelle traduzioni la prima
// sezione è l'avviso "prevale l'italiano".
function doc(which, code = lang.SOURCE) {
  const l = pick(code);
  const T = texts[l];
  const v = vars(T);
  const sections = T[which].sections.map((s) => ({ id: s.id, title: s.title, body: lang.format(s.body, v) }));
  if (T.notice) sections.unshift({ id: 'translation_notice', title: T.notice.title, body: T.notice.body });
  return { title: T[which].title, lang: l, sections };
}

const privacy = (code) => doc('privacy', code);
const terms = (code) => doc('terms', code);
const licenses = (code) => doc('licenses', code);

function all(code = lang.SOURCE) {
  const l = pick(code);
  const c = controller();
  const T = texts[l];
  return {
    version: TERMS_VERSION,
    lang: l,
    available: available(),
    notice: T.notice ? T.notice.body : null,
    controller: { name: c.name, vat: c.vat, email: c.email, complete: c.complete },
    developer: DEVELOPER,
    source_url: `${config.publicUrl}/source`,
    documents: Object.fromEntries(DOCS.map((d) => [d, doc(d, l)])),
  };
}

module.exports = { TERMS_VERSION, DEVELOPER, DOCS, all, privacy, terms, licenses, available, pick };
