'use strict';
// Lingua della richiesta: ?lang=, poi Accept-Language, poi "en".
// L'app console manda uno dei codici di LANGS; i browser una lista con q-value
// (es. "pt-BR,pt;q=0.9,en;q=0.5"). L'italiano resta la lingua sorgente dei testi.

// Ordine = ordine del selettore nelle pagine web. Il nome è quello nella lingua stessa.
const LANGS = [
  ['it', 'Italiano'], ['en', 'English'], ['ja', '日本語'], ['fr', 'Français'], ['es', 'Español'],
  ['de', 'Deutsch'], ['nl', 'Nederlands'], ['pt-PT', 'Português (Portugal)'], ['pt-BR', 'Português (Brasil)'],
  ['ru', 'Русский'], ['ko', '한국어'], ['zh-Hans', '简体中文'], ['zh-Hant', '繁體中文'], ['fi', 'Suomi'],
  ['sv', 'Svenska'], ['da', 'Dansk'], ['nb', 'Norsk bokmål'], ['pl', 'Polski'], ['tr', 'Türkçe'],
  ['cs', 'Čeština'], ['hu', 'Magyar'], ['el', 'Ελληνικά'], ['ro', 'Română'], ['th', 'ไทย'],
  ['vi', 'Tiếng Việt'], ['id', 'Bahasa Indonesia'], ['uk', 'Українська'],
];
const CODES = LANGS.map(([c]) => c);
const NAMES = Object.fromEntries(LANGS);
const SOURCE = 'it';
const FALLBACK = 'en';
const BY_LOWER = new Map(CODES.map((c) => [c.toLowerCase(), c]));

// Un tag BCP 47 qualsiasi → uno dei codici supportati, oppure null.
function normalize(tag) {
  const t = String(tag || '').trim().replace(/_/g, '-').toLowerCase();
  if (!t || t === '*' || t.length > 35) return null;
  if (BY_LOWER.has(t)) return BY_LOWER.get(t);
  const parts = t.split('-');
  const base = parts[0];
  const rest = parts.slice(1);
  if (base === 'pt') return rest.includes('br') ? 'pt-BR' : 'pt-PT';
  if (base === 'zh') {
    if (rest.includes('hant') || rest.includes('tw') || rest.includes('hk') || rest.includes('mo')) {
      return rest.includes('hans') ? 'zh-Hans' : 'zh-Hant';
    }
    return 'zh-Hans';
  }
  if (base === 'no' || base === 'nn' || base === 'nb') return 'nb';
  return BY_LOWER.has(base) ? BY_LOWER.get(base) : null;
}

// Accept-Language → codici supportati in ordine di preferenza (q decrescente,
// a parità l'ordine dell'intestazione; q=0 esclude).
function parseAcceptLanguage(header) {
  const items = [];
  String(header || '').split(',').slice(0, 30).forEach((part, i) => {
    const [tag, ...params] = part.trim().split(';');
    let q = 1;
    for (const p of params) {
      const m = /^\s*q\s*=\s*([0-9.]+)\s*$/i.exec(p);
      if (m) q = Number(m[1]);
    }
    if (!(q > 0)) return;
    const code = normalize(tag);
    if (code) items.push({ code, q, i });
  });
  items.sort((a, b) => b.q - a.q || a.i - b.i);
  return [...new Set(items.map((x) => x.code))];
}

// { lang, explicit }: explicit = la richiesta ha indicato una lingua supportata
// (serve a non sovrascrivere la lingua salvata di un account con il ripiego).
function resolve({ query, header } = {}) {
  const q = normalize(query);
  if (q) return { lang: q, explicit: true };
  const list = parseAcceptLanguage(header);
  if (list.length) return { lang: list[0], explicit: true };
  return { lang: FALLBACK, explicit: false };
}

function fromRequest(req, url) {
  return resolve({ query: url ? url.searchParams.get('lang') : null, header: req.headers['accept-language'] });
}

const isCode = (c) => CODES.includes(c);

// "Ciao {name}" + { name: 'x' } → "Ciao x"; i segnaposto sconosciuti restano com'erano.
function format(template, vars = {}) {
  return String(template).replace(/\{([a-z_]+)\}/g, (m, k) => (vars[k] !== undefined && vars[k] !== null ? String(vars[k]) : m));
}

module.exports = { LANGS, CODES, NAMES, SOURCE, FALLBACK, normalize, parseAcceptLanguage, resolve, fromRequest, isCode, format };
