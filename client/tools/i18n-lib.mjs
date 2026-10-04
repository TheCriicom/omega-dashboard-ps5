// Omega — funzioni comuni agli script delle traduzioni (estrazione e generatore).
// Nessuna dipendenza: solo Node.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

export const TOOLS = path.dirname(fileURLToPath(import.meta.url));
export const UI = path.resolve(TOOLS, '..');
export const ROOT = path.resolve(UI, '..');
export const I18N = path.join(UI, 'i18n');

// Le 27 lingue: stessi codici per file, impostazioni e Accept-Language.
export const LANGS = ['it', 'en', 'ja', 'fr', 'es', 'de', 'nl', 'pt-PT', 'pt-BR', 'ru', 'ko', 'zh-Hans', 'zh-Hant',
  'fi', 'sv', 'da', 'nb', 'pl', 'tr', 'cs', 'hu', 'el', 'ro', 'th', 'vi', 'id', 'uk'];

// Sorgenti con testi da tradurre: la UI e i due programmi che mostrano notifiche di sistema.
// nomi delle cartelle nei sorgenti di lavoro e nella repository pubblica
const pick = (...names) => path.join(ROOT, names.find((n) => fs.existsSync(path.join(ROOT, n))) || names[0]);
const REDIRECT = pick('omega-redirect-src', 'daemon'), INSTALLER = pick('omega-installer-src', 'installer'), ONION = pick('omega-onion-plugin-src', 'onionhen-plugin');
export const COMPONENTS = {
  ui: { dir: path.join(UI, 'source'), files: () => fs.readdirSync(path.join(UI, 'source')).filter(f => f.endsWith('.c') && f !== 'i18n_data.c').sort().map(f => path.join(UI, 'source', f)) },
  redirect: { dir: path.join(REDIRECT, 'source'), files: () => ['main.c', 'ctl.c'].map(f => path.join(REDIRECT, 'source', f)) },
  installer: { dir: path.join(INSTALLER, 'source'), files: () => [path.join(INSTALLER, 'source', 'main.c')] },
  onion: { dir: path.join(ONION, 'source'), files: () => [path.join(ONION, 'source', 'main.c')] },
};

export const die = (msg) => { console.error(`i18n: ERRORE: ${msg}`); process.exit(1); };
export const rel = (p) => path.relative(ROOT, p);

// Decodifica il contenuto di un letterale stringa C (senza virgolette) in byte.
function cBytes(s, where) {
  const out = [];
  for (let i = 0; i < s.length; i++) {
    const c = s[i];
    if (c !== '\\') { for (const b of Buffer.from(c === undefined ? '' : String.fromCodePoint(s.codePointAt(i)), 'utf8')) out.push(b); if (s.codePointAt(i) > 0xFFFF) i++; continue; }
    const n = s[++i];
    const simple = { n: 10, t: 9, r: 13, a: 7, b: 8, f: 12, v: 11, '\\': 92, '"': 34, "'": 39, '?': 63 };
    if (n in simple) out.push(simple[n]);
    else if (n === 'x') {                              // come in C: tutte le cifre esadecimali che seguono
      let j = i + 1; while (j < s.length && /[0-9a-fA-F]/.test(s[j])) j++;
      const v = parseInt(s.slice(i + 1, j), 16);
      if (j === i + 1 || v > 255) die(`escape \\x non valido in ${where}`);
      out.push(v); i = j - 1;
    } else if (/[0-7]/.test(n)) {
      let j = i; while (j < s.length && j < i + 3 && /[0-7]/.test(s[j])) j++;
      out.push(parseInt(s.slice(i, j), 8) & 255); i = j - 1;
    } else die(`escape \\${n} non gestito in ${where}`);
  }
  return out;
}

// Token minimi di un sorgente C: identificatori, letterali stringa, punteggiatura.
// Commenti e letterali carattere si saltano.
function tokens(src) {
  const t = []; let i = 0, line = 1;
  while (i < src.length) {
    const c = src[i];
    if (c === '\n') { line++; i++; continue; }
    if (/\s/.test(c)) { i++; continue; }
    if (c === '/' && src[i + 1] === '/') { while (i < src.length && src[i] !== '\n') i++; continue; }
    if (c === '/' && src[i + 1] === '*') { const e = src.indexOf('*/', i + 2); const body = src.slice(i, e < 0 ? src.length : e + 2); line += (body.match(/\n/g) || []).length; i += body.length; continue; }
    if (c === '"') {
      let j = i + 1; while (j < src.length && src[j] !== '"') { if (src[j] === '\\') j++; j++; }
      t.push({ k: 'str', v: src.slice(i + 1, j), line }); i = j + 1; continue;
    }
    if (c === "'") { let j = i + 1; while (j < src.length && src[j] !== "'") { if (src[j] === '\\') j++; j++; } i = j + 1; t.push({ k: 'chr', line }); continue; }
    if (/[A-Za-z_]/.test(c)) { let j = i; while (j < src.length && /[A-Za-z0-9_]/.test(src[j])) j++; t.push({ k: 'id', v: src.slice(i, j), line }); i = j; continue; }
    if (c === '#') {                                   // direttive: si saltano (con le continuazioni di riga)
      const prev = t.length ? t[t.length - 1] : null;
      if (!prev || prev.line < line) { while (i < src.length && src[i] !== '\n') { if (src[i] === '\\' && src[i + 1] === '\n') { line++; i++; } i++; } continue; }
    }
    t.push({ k: 'p', v: c, line }); i++;
  }
  return t;
}

// Tutte le occorrenze di _("...") e N_("...") (letterali adiacenti concatenati).
// Ritorna [{ msgid, file, line, kind }] e l'elenco delle chiamate dinamiche _(espressione).
export function scanFile(file) {
  const src = fs.readFileSync(file, 'utf8');
  const lines = src.split('\n');
  const t = tokens(src), found = [], dynamic = [];
  for (let i = 0; i + 1 < t.length; i++) {
    if (t[i].k !== 'id' || (t[i].v !== '_' && t[i].v !== 'N_' && t[i].v !== 'P_') || t[i + 1].v !== '(') continue;
    if (i > 0 && (t[i - 1].v === 'define')) continue;
    let j = i + 2; const parts = [], ctx = [];
    if (t[i].v === 'P_') {                             // P_("contesto", "testo") → "contesto\u0004testo"
      while (j < t.length && t[j].k === 'str') ctx.push(t[j++]);
      if (!ctx.length || t[j].v !== ',') continue;
      j++;
    }
    while (j < t.length && t[j].k === 'str') parts.push(t[j++]);
    if (!parts.length || t[j].v !== ')') { if (t[i].v === '_') dynamic.push({ file, line: t[i].line }); continue; }
    const where = `${rel(file)}:${t[i].line}`;
    const bytes = [...(ctx.length ? [...ctx.flatMap(p => cBytes(p.v, where)), 4] : []), ...parts.flatMap(p => cBytes(p.v, where))];
    const msgid = Buffer.from(bytes).toString('utf8');
    if (Buffer.from(msgid, 'utf8').compare(Buffer.from(bytes)) !== 0) die(`${where}: la stringa non è UTF-8 valido`);
    found.push({ msgid, file, line: t[i].line, src: lines[t[i].line - 1] || '', noop: t[i].v === 'N_' });
  }
  return { found, dynamic };
}

export function scanComponent(name) {
  const c = COMPONENTS[name];
  const all = [], dyn = [];
  for (const f of c.files()) { if (!fs.existsSync(f)) continue; const r = scanFile(f); all.push(...r.found); dyn.push(...r.dynamic); }
  return { found: all, dynamic: dyn };
}

// ------------------------------------------------------ specificatori printf --
// Sequenza normalizzata degli specificatori: tipo (con lunghezza) e argomenti
// '*' contano; flag, larghezza e precisione numeriche no. '%' non valido = BAD.
export function specs(s) {
  const out = []; const re = /%([-+ #0]*)(\*|\d+)?(?:\.(\*|\d*))?(hh|h|ll|l|j|z|t|L)?([diouxXeEfFgGaAcsp%])/y;
  for (let i = 0; i < s.length; i++) {
    if (s[i] !== '%') continue;
    re.lastIndex = i; const m = re.exec(s);
    if (!m) { out.push('BAD'); continue; }
    if (m[5] === '%') { out.push(m[0] === '%%' ? '%%' : 'BAD'); i = re.lastIndex - 1; continue; }
    out.push(`${m[2] === '*' ? '*' : ''}${m[3] === '*' ? '.*' : ''}${m[4] || ''}${m[5]}`);
    i = re.lastIndex - 1;
  }
  return out;
}

export function readCatalog(code, { required = false } = {}) {
  const f = path.join(I18N, `${code}.json`);
  if (!fs.existsSync(f)) { if (required) die(`manca ${rel(f)}`); return null; }
  let j;
  try { j = JSON.parse(fs.readFileSync(f, 'utf8')); } catch (e) { die(`${rel(f)} non è JSON valido: ${e.message}`); }
  if (!j || typeof j !== 'object' || Array.isArray(j)) die(`${rel(f)}: serve un oggetto piatto { "msgid": "traduzione" }`);
  for (const [k, v] of Object.entries(j)) if (typeof v !== 'string') die(`${rel(f)}: il valore di ${JSON.stringify(k)} non è una stringa`);
  return j;
}

export const sortKeys = (keys) => [...keys].sort((a, b) => Buffer.compare(Buffer.from(a), Buffer.from(b)));

export function writeIfChanged(file, text) {
  if (fs.existsSync(file) && fs.readFileSync(file, 'utf8') === text) return false;
  fs.writeFileSync(file, text); return true;
}

// Letterale C per una stringa UTF-8 ('?' sempre con escape: niente trigrammi).
export function cLit(s) {
  let o = '"';
  for (const ch of s) {
    const cp = ch.codePointAt(0);
    if (ch === '"' || ch === '\\') o += '\\' + ch;
    else if (ch === '?') o += '\\?';
    else if (ch === '\n') o += '\\n';
    else if (ch === '\t') o += '\\t';
    else if (cp < 0x20 || cp === 0x7F) o += '\\' + cp.toString(8).padStart(3, '0');
    else o += ch;
  }
  return o + '"';
}
