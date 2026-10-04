#!/usr/bin/env node
// Omega — estrae i testi da tradurre: tutte le _("...") e N_("...") della UI,
// del demone (omega-redirect-src) e dell'installer (omega-installer-src).
//
//   node tools/i18n-extract.mjs [--prune]
//
// Scrive i18n/it.json (identità, ordinato), i18n/_context.json (dove compare
// ogni msgid e indicazioni per chi traduce) e crea {} per le lingue che non
// hanno ancora un file. Le traduzioni esistenti non si toccano; con --prune si
// tolgono le chiavi che nei sorgenti non esistono più.
import fs from 'node:fs';
import path from 'node:path';
import { COMPONENTS, I18N, LANGS, readCatalog, rel, scanComponent, sortKeys, specs, writeIfChanged } from './i18n-lib.mjs';

const prune = process.argv.includes('--prune');
fs.mkdirSync(I18N, { recursive: true });

const uses = new Map();       // msgid → [{ file, line, src, comp }]
for (const comp of Object.keys(COMPONENTS)) {
  const { found, dynamic } = scanComponent(comp);
  for (const f of found) {
    if (!uses.has(f.msgid)) uses.set(f.msgid, []);
    uses.get(f.msgid).push({ ...f, comp });
  }
  if (process.env.I18N_VERBOSE) for (const d of dynamic) console.log(`dinamica: ${rel(d.file)}:${d.line}`);
}
const ids = sortKeys(uses.keys());

// ------------------------------------------------------------- contesto --
// Che cosa è il testo, dedotto dalla riga di codice: chi traduce sa quanto
// spazio ha. maxlen è indicativo (caratteri latini; CJK circa la metà).
function hintFor(msgid, list) {
  const kinds = new Set();
  for (const u of list) {
    const s = u.src;
    if (u.comp !== 'ui') kinds.add('notification');
    if (/\bhints\s*\(|\blb\s*\[\]|\blbl?\[\]\s*=\s*\{/.test(s) && /\bhints|\blb\b/.test(s)) kinds.add('hint');
    if (/\bbutton\s*\(/.test(s)) kinds.add('button');
    if (/\bpill\s*\(|\bADD\s*\(/.test(s)) kinds.add('pill');
    if (/\bmenu_open\s*\(/.test(s) || /\bit\d?\s*\[\]\s*=|\bitems\s*\[\]\s*=|\bm\s*\[\]\s*=|\br\s*\[\]\s*=/.test(s)) kinds.add('menu');
    if (/\bconfirm_open\s*\(/.test(s)) kinds.add('confirm');
    if (/\bset_msg\s*\(/.test(s)) kinds.add('message');
    if (/\btoast\s*\(/.test(s)) kinds.add('toast');
    if (/\bedit_text\s*\(/.test(s)) kinds.add('keyboard-title');
    if (/\bdraw_text_wrap\s*\(/.test(s)) kinds.add('wrapped');
    if (/\btabs?\s*\[/.test(s) || /\bTABS?\b/.test(s)) kinds.add('tab');
    if (/\bcc_|\bCC_|lbl\[CC_N\]/.test(s)) kinds.add('tile');
  }
  const len = [...msgid].length;
  const notes = [];
  const k = [...kinds];
  if (kinds.has('hint')) notes.push('command bar label next to a button glyph: keep it short');
  if (kinds.has('button')) notes.push('button (700 px wide)');
  if (kinds.has('pill')) notes.push('pill button: one line, keep it short');
  if (kinds.has('tile')) notes.push('Control Center tile, 186 px wide: about 11 characters max');
  if (kinds.has('tab')) notes.push('tab label: keep it short');
  if (kinds.has('menu')) notes.push('menu entry or title: one line');
  if (kinds.has('confirm')) notes.push('confirmation dialog: up to 3 lines; the button text is separate');
  if (kinds.has('message')) notes.push('one-line message at the bottom of the screen');
  if (kinds.has('toast')) notes.push('notification popup: one line, truncated with …');
  if (kinds.has('keyboard-title')) notes.push('title of the system keyboard');
  if (kinds.has('notification')) notes.push('PS5 system notification');
  if (kinds.has('wrapped')) notes.push('wraps over several lines');
  const sp = specs(msgid);
  if (sp.length) notes.push(`printf format: keep exactly these specifiers in this order: ${sp.join(' ')}`);
  if (/\b(min|h|g|sett|KB|MB|GB)\b/.test(msgid) && sp.length) notes.push('unit or abbreviation: use the usual short form of your language');
  const short = kinds.has('hint') || kinds.has('pill') || kinds.has('tab') || kinds.has('button') || kinds.has('tile');
  const max = kinds.has('tile') ? 11 : short ? Math.max(len + 4, Math.ceil(len * 1.3)) : null;
  return { kinds: k, maxlen: max, notes };
}

const context = {};
for (const id of ids) {
  const list = uses.get(id);
  const h = hintFor(id, list);
  context[id] = {
    where: [...new Set(list.map(u => `${rel(u.file)}:${u.line}`))],
    ...(h.kinds.length ? { kind: h.kinds } : {}),
    ...(h.maxlen ? { maxlen: h.maxlen } : {}),
    ...(h.notes.length ? { hint: h.notes.join('; ') } : {}),
  };
}

const json = (o) => JSON.stringify(o, null, 2) + '\n';
const it = {}; for (const id of ids) it[id] = id;
writeIfChanged(path.join(I18N, 'it.json'), json(it));
writeIfChanged(path.join(I18N, '_context.json'), json({
  _readme: 'Context for translators. Keys are the Italian msgids of it.json. "where" = file:line, "kind" = what the text is, "maxlen" = suggested maximum length in Latin characters (CJK: about half), "hint" = notes. Format specifiers (%s %d %ld %.*s %%) must be kept exactly, in the same order.',
  ...context,
}));

let missing = 0;
for (const code of LANGS.filter(c => c !== 'it')) {
  const f = path.join(I18N, `${code}.json`);
  if (!fs.existsSync(f)) fs.writeFileSync(f, '{}\n');
  const cat = readCatalog(code);
  const stale = Object.keys(cat).filter(k => !uses.has(k));
  if (stale.length) {
    console.log(`${code}: ${stale.length} chiavi non più usate${prune ? ' (tolte)' : ' (usa --prune per toglierle)'}`);
    if (prune) { for (const k of stale) delete cat[k]; const o = {}; for (const k of sortKeys(Object.keys(cat))) o[k] = cat[k]; fs.writeFileSync(f, Object.keys(o).length ? json(o) : '{}\n'); }
  }
  if (code === 'en') missing = ids.filter(id => !(id in cat)).length;
}
console.log(`i18n: ${ids.length} msgid (ui ${scanComponent('ui').found.length} occorrenze); en mancanti: ${missing}`);
