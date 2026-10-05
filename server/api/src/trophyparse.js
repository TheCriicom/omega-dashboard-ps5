'use strict';
// Lettura dei trofei della PS5.
//  · definizione(conf, meta): dai due JSON dell'archivio TROPHY.UCP
//    (tropconf.json, tropmeta_<lingua>.json) l'elenco dei trofei del set;
//  · stato(buf, n): dal file per utente TRPTITLE.DAT quali trofei sono stati
//    ottenuti e quando.
//
// Il formato di TRPTITLE.DAT non è documentato: qui non si presume una
// disposizione fissa, la si riconosce. Si cerca una tabella di record a passo
// costante numerati 0..n-1 e, dentro i record, un campo di 8 byte che per
// alcuni record è una data plausibile e per gli altri è vuoto. Se non si trova
// niente di simile il file resta «non letto» (parsed=false) e si conserva
// com'è: si rilegge quando il lettore migliora, senza chiedere altro alle console.
const GRADES = new Set(['P', 'G', 'S', 'B']);
const POINTS = { P: 300, G: 90, S: 30, B: 15 };
const MAX_TROPHIES = 400;

const clean = (v, n) => String(v == null ? '' : v).replace(/[\u0000-\u0008\u000b-\u001f]+/g, ' ').replace(/[ \t]+/g, ' ').trim().slice(0, n);

// { title, trophies: [{ id, grade, hidden, name, detail }] } oppure null.
function definition(conf, meta) {
  if (!conf || !Array.isArray(conf.trophies)) return null;
  const md = (meta && meta.metadata) || {};
  const names = new Map();
  for (const t of Array.isArray(md.trophyMetadata) ? md.trophyMetadata : []) names.set(Number.parseInt(t.id, 10), t);
  const trophies = [];
  for (const t of conf.trophies.slice(0, MAX_TROPHIES)) {
    const id = Number.parseInt(t.id, 10);
    const grade = String(t.grade || '').toUpperCase().slice(0, 1);
    if (!Number.isInteger(id) || id < 0 || id >= 2000 || !GRADES.has(grade)) continue;
    const m = names.get(id) || {};
    trophies.push({ id, grade, hidden: t.hidden === true, name: clean(m.name, 160) || null, detail: clean(m.detail, 400) || null });
  }
  if (!trophies.length) return null;
  trophies.sort((a, b) => a.id - b.id);
  const title = clean(String((md.titleMetadata && md.titleMetadata.name) || '').replace(/\n/g, ' '), 160) || null;
  return { title, trophies };
}

// Secondi Unix se gli 8 byte sono una data plausibile in una delle codifiche
// note (microsecondi dall'anno 1, oppure Unix in µs, ms o s), altrimenti null.
const EPOCH_0001 = 62135596800;
const T_MIN = Date.UTC(2020, 9, 1) / 1000;     // la PS5 è uscita a novembre 2020
function timeAt(buf, off, be, tMax) {
  const v = Number(be ? buf.readBigUInt64BE(off) : buf.readBigUInt64LE(off));
  for (const t of [v / 1e6 - EPOCH_0001, v / 1e6, v / 1e3, v]) if (t >= T_MIN && t <= tMax) return t;
  return null;
}
function blank(buf, off) {
  let z = true, f = true;
  for (let i = 0; i < 8; i++) { if (buf[off + i] !== 0) z = false; if (buf[off + i] !== 0xff) f = false; }
  return z || f;
}

// Tabelle di n record numerati 0..n-1 a passo costante: [{ base, stride, be }].
function indexTables(buf, n) {
  const out = [];
  if (n < 3) return out;
  for (const be of [true, false]) {
    const u32 = (o) => (be ? buf.readUInt32BE(o) : buf.readUInt32LE(o));
    const ones = [], twos = [];
    for (let o = 0; o + 4 <= buf.length; o++) { const v = u32(o); if (v === 1) ones.push(o); else if (v === 2) twos.push(o); }
    let j0 = 0;
    for (const p1 of ones) {
      while (j0 < twos.length && twos[j0] <= p1) j0++;
      for (let j = j0; j < twos.length; j++) {
        const stride = twos[j] - p1;
        if (stride > 4096) break;
        if (stride < 12) continue;
        const base = p1 - stride;
        if (base < 0 || base + (n - 1) * stride + 4 > buf.length || u32(base) !== 0) continue;
        let ok = true;
        for (let i = 3; i < n && ok; i++) ok = u32(base + i * stride) === i;
        if (ok) out.push({ base, stride, be });
        if (out.length >= 64) return out;
      }
    }
  }
  return out;
}

// { parsed, earned: { "<id>": "<ISO>" } } — ids = id dei trofei del set, in ordine.
function state(buf, ids, now = Date.now()) {
  const none = { parsed: false, earned: {} };
  if (!Buffer.isBuffer(buf) || buf.length < 32 || !Array.isArray(ids) || !ids.length) return none;
  const n = Math.max(...ids) + 1;
  const tMax = now / 1000 + 2 * 86400;
  let best = null;
  for (const t of indexTables(buf, n)) {
    const lim = Math.min(t.stride, buf.length - (t.base + (n - 1) * t.stride)) - 8;
    for (const be of [true, false]) {
      for (let k = 0; k <= lim; k++) {
        const times = new Array(n).fill(null);
        let count = 0, valid = true;
        for (let i = 0; i < n && valid; i++) {
          const off = t.base + i * t.stride + k;
          const ts = timeAt(buf, off, be, tMax);
          if (ts != null) { times[i] = ts; count++; } else if (!blank(buf, off)) valid = false;
        }
        if (valid && count && (!best || count > best.count)) best = { count, times };
      }
    }
  }
  if (!best) return none;
  const earned = {};
  for (const id of ids) if (best.times[id] != null) earned[String(id)] = new Date(best.times[id] * 1000).toISOString();
  return { parsed: true, earned };
}

// Conteggi e punti di un insieme di trofei ottenuti.
function tally(trophies, earned) {
  const out = { P: 0, G: 0, S: 0, B: 0, points: 0, last: null };
  for (const t of trophies) {
    const e = earned[String(t.id)];
    if (!e) continue;
    out[t.grade] += 1; out.points += POINTS[t.grade];
    if (typeof e === 'string' && (!out.last || e > out.last)) out.last = e;
  }
  return out;
}

module.exports = { definition, state, tally, POINTS };
