#!/usr/bin/env node
// Omega — genera le tabelle delle traduzioni da i18n/<codice>.json:
//   source/i18n_data.c                          (UI: tutti i msgid)
//   ../omega-redirect-src/source/i18n_data.c    (demone: solo i suoi testi, con la sua mini libreria)
//   ../omega-installer-src/source/i18n_data.c   (installer: idem)
//
//   node tools/i18n-gen.mjs
//
// Si ferma con un errore se un JSON non è valido o se una traduzione non ha
// gli stessi specificatori printf del msgid (stesso ordine e tipo): sulla
// console un %s al posto di un %d manda in crash l'app.
import fs from 'node:fs';
import path from 'node:path';
import { COMPONENTS, I18N, LANGS, UI, cLit, die, readCatalog, rel, scanComponent, sortKeys, specs, writeIfChanged } from './i18n-lib.mjs';

const HEAD = '// GENERATO da omega-ui-src/tools/i18n-gen.mjs a partire da omega-ui-src/i18n/*.json:\n// non modificare a mano (rigenerare con `node tools/i18n-gen.mjs`).\n';

const it = readCatalog('it', { required: true });
const ids = sortKeys(Object.keys(it));
const idset = new Set(ids);
for (const k of ids) if (it[k] !== k) die(`i18n/it.json: il valore di ${JSON.stringify(k)} deve essere uguale al msgid`);

// ------------------------------------------------- lettura e controlli --
const files = fs.readdirSync(I18N).filter(f => f.endsWith('.json') && !f.startsWith('_'));
for (const f of files) if (!LANGS.includes(f.slice(0, -5))) die(`i18n/${f}: codice di lingua sconosciuto (ammessi: ${LANGS.join(' ')})`);

const cats = {};
let errors = 0, warnings = 0;
for (const code of LANGS) {
  if (code === 'it') continue;
  const cat = readCatalog(code);
  if (!cat) { console.warn(`i18n: avviso: manca i18n/${code}.json (si usa l'italiano)`); warnings++; continue; }
  const ok = {};
  for (const [k, v] of Object.entries(cat)) {
    if (!idset.has(k)) { warnings++; if (process.env.I18N_VERBOSE) console.warn(`i18n: ${code}: chiave non presente in it.json: ${JSON.stringify(k)}`); continue; }
    if (!v) continue;                                      // vuota = non tradotta
    const a = specs(k).join(' '), b = specs(v).join(' ');
    if (a !== b) { console.error(`i18n: ERRORE: i18n/${code}.json: specificatori diversi\n  msgid:      ${JSON.stringify(k)}  [${a}]\n  traduzione: ${JSON.stringify(v)}  [${b}]`); errors++; continue; }
    if (v.includes('\0')) { console.error(`i18n: ERRORE: i18n/${code}.json: carattere NUL in ${JSON.stringify(k)}`); errors++; continue; }
    if (v !== k) ok[k] = v;
  }
  cats[code] = ok;
}
if (errors) die(`${errors} traduzioni con specificatori sbagliati: correggere i file indicati`);

// Testi usati nei sorgenti ma assenti dal catalogo: restano in italiano.
for (const comp of Object.keys(COMPONENTS)) {
  const miss = [...new Set(scanComponent(comp).found.map(f => f.msgid).filter(m => !idset.has(m)))];
  if (miss.length) { console.warn(`i18n: avviso: ${miss.length} testi di ${comp} non sono in it.json: eseguire tools/i18n-extract.mjs`); warnings++; }
}

// --------------------------------------------------------------- UI --
function chunks(arr, per, fmt) {
  const out = [];
  for (let i = 0; i < arr.length; i += per) out.push('  ' + arr.slice(i, i + per).map(fmt).join(', ') + ',');
  return out.join('\n');
}
const ident = (code) => code.replace('-', '_');

function uiData() {
  const off = []; let pos = 0;
  const blob = ids.map(id => { off.push(pos); pos += Buffer.byteLength(id) + 1; return `  ${cLit(id)} "\\0"`; });
  let s = HEAD + '#include "i18n.h"\n\n';
  s += `const int i18n_nmsg = ${ids.length};\n\n`;
  s += `const char i18n_msgid_blob[] =\n${blob.join('\n')};\n\n`;
  s += `const uint32_t i18n_msgid_off[] = {\n${chunks(off, 12, String)}\n};\n`;
  const tables = [];
  for (const code of LANGS) {
    const cat = cats[code]; if (!cat) continue;
    const keys = ids.filter(k => k in cat); if (!keys.length) continue;
    let p = 0; const pairs = [];
    const tb = keys.map(k => { pairs.push([ids.indexOf(k), p]); p += Buffer.byteLength(cat[k]) + 1; return `  ${cLit(cat[k])} "\\0"`; });
    const n = ident(code);
    s += `\n// ${code}: ${keys.length}/${ids.length}\nstatic const char T_${n}[] =\n${tb.join('\n')};\n`;
    s += `static const uint32_t P_${n}[][2] = {\n${chunks(pairs, 8, x => `{ ${x[0]}, ${x[1]} }`)}\n};\n`;
    tables.push(`  { "${code}", T_${n}, P_${n}, ${keys.length} },`);
  }
  s += `\nconst I18nTable i18n_tables[] = {\n${tables.length ? tables.join('\n') : '  { "", "", 0, 0 },'}\n};\n`;
  s += `const int i18n_ntables = ${tables.length};\n`;
  return { text: s, tables: tables.length };
}

// ------------------------------------------------ demone e installer --
// Mini libreria autonoma: pochi testi, ricerca lineare. La lingua è quella
// scelta nella UI (OMEGA_DIR/lang.txt) o, se manca, quella della console.
const PS5_LANG = ['ja', 'en', 'fr', 'es', 'de', 'it', 'nl', 'pt-PT', 'ru', 'ko', 'zh-Hant', 'zh-Hans', 'fi', 'sv', 'da', 'nb',
  'pl', 'pt-BR', 'en', 'tr', 'es', 'en', 'fr', 'cs', 'hu', 'el', 'ro', 'th', 'vi', 'id', 'uk'];

function miniData(comp, title) {
  const used = sortKeys(new Set(scanComponent(comp).found.map(f => f.msgid).filter(m => idset.has(m))));
  const langs = LANGS.filter(c => cats[c] && used.some(k => k in cats[c]));
  let s = HEAD;
  s += `// Testi di ${title} (notifiche di sistema) e scelta della lingua. Autonomo:\n`;
  s += '// main.c dichiara i18n_tr, i18n_init e i18n_code e passa a i18n_init il valore\n';
  s += '// di sceSystemServiceParamGetInt(1 /* lingua */) o -1 se non disponibile.\n';
  s += '#include <stdio.h>\n#include <string.h>\n\n';
  s += '#define I18N_LANG_FILE "/data/Omega/lang.txt"   // scelta fatta nella UI (Impostazioni → Lingua)\n';
  s += `#define NMSG ${Math.max(used.length, 1)}\n#define NTR ${Math.max(langs.length, 1)}\n\n`;
  s += `static const char *const MSGID[NMSG] = {\n${used.length ? used.map(k => `  ${cLit(k)},`).join('\n') : '  "",'}\n};\n`;
  s += `static const char *const CODE[NTR] = { ${langs.length ? langs.map(c => `"${c}"`).join(', ') : '""'} };\n`;
  s += 'static const char *const TR[NTR][NMSG] = {\n';
  if (!langs.length) s += '  { 0 },\n';
  for (const c of langs) s += `  { // ${c}\n${used.map(k => `    ${k in cats[c] ? cLit(cats[c][k]) : '0'},`).join('\n')}\n  },\n`;
  s += '};\n';
  s += `static const char *const LANGS[] = { ${LANGS.map(c => `"${c}"`).join(', ')} };\n`;
  s += `// valore di SCE_SYSTEM_SERVICE_PARAM_ID_LANG → codice (21 = arabo: inglese)\nstatic const char *const PS5_LANG[] = {\n${chunks(PS5_LANG, 16, c => `"${c}"`)}\n};\n\n`;
  s += `static int cur = -1;                 // riga di TR, -1 = msgid (italiano)
static const char *cur_code = "en";

const char *i18n_tr(const char *s) __attribute__((format_arg(1)));
const char *i18n_tr(const char *s) {
  if (cur < 0 || !s) return s;
  for (int i = 0; i < NMSG; i++) if (!strcmp(MSGID[i], s)) return TR[cur][i] ? TR[cur][i] : s;
  return s;
}

const char *i18n_code(void) { return cur_code; }

void i18n_init(int sys_lang) {
  char buf[16] = ""; const char *code = NULL;
  FILE *f = fopen(I18N_LANG_FILE, "r");
  if (f) { if (!fgets(buf, sizeof buf, f)) buf[0] = 0; fclose(f); buf[strcspn(buf, "\\r\\n \\t")] = 0; }
  for (unsigned i = 0; buf[0] && i < sizeof LANGS / sizeof LANGS[0]; i++) if (!strcmp(LANGS[i], buf)) code = LANGS[i];
  if (!code) code = sys_lang >= 0 && sys_lang < (int)(sizeof PS5_LANG / sizeof PS5_LANG[0]) ? PS5_LANG[sys_lang] : "en";
  cur_code = code; cur = -1;
  for (int i = 0; i < NTR; i++) if (!strcmp(CODE[i], code)) cur = i;
}
`;
  return { text: s, n: used.length, langs: langs.length };
}

const ui = uiData();
const changed = [];
if (writeIfChanged(path.join(UI, 'source', 'i18n_data.c'), ui.text)) changed.push('source/i18n_data.c');
for (const [comp, title] of [['redirect', 'omega_redirect'], ['installer', 'Omega Installer'], ['onion', 'Omega per OnionHEN']]) {
  const dir = COMPONENTS[comp].dir;
  if (!fs.existsSync(dir)) continue;
  const d = miniData(comp, title);
  const f = path.join(dir, 'i18n_data.c');
  if (writeIfChanged(f, d.text)) changed.push(rel(f));
}
console.log(`i18n: ${ids.length} msgid, ${ui.tables} lingue con traduzioni${changed.length ? `; aggiornati: ${changed.join(', ')}` : ''}${warnings ? ` (${warnings} avvisi)` : ''}`);
