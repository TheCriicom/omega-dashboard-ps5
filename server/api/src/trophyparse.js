'use strict';
// Lettura dei trofei della PS5.
//  · definizione(conf, meta): dai due JSON dell'archivio TROPHY.UCP
//    (tropconf.json, tropmeta_<lingua>.json) l'elenco dei trofei del set;
//  · stato(buf, n): dal file per utente TRPTITLE.DAT quali trofei sono stati
//    ottenuti e quando.
//
// TRPTITLE.DAT della PS5 (formato T2PD, ricavato il 06/10/2026 dai file di
// 21 giochi arrivati da una console vera, tutti letti senza scarti):
//   · intestazione di 0x40 byte: "T2PD", a 0x0c la misura del blocco (0x420);
//   · poi blocchi di 0x400 byte di dati seguiti da 0x20 byte di firma: tolte
//     le firme resta un flusso continuo, a cui si riferiscono gli indirizzi;
//   · a 0x40 "T2TD", a 0x48 il numero di sezioni, a 0x70 il loro indice
//     (0x20 byte l'una: tipo, misura del record, ?, numero, indirizzo + 0x40);
//   · la sezione 0x800 ha un record per trofeo, ogni 0x60 byte (0x50 + 0x10):
//     +0x10 id, +0x18 stato (bit 0x2000 = ottenuto), +0x20 data dello sblocco
//     in microsecondi dall'anno 1 (big endian).
// Un file in un altro formato passa al riconoscimento generico (tabella di
// record numerati con un campo data); se nemmeno quello lo legge resta «non
// letto» (parsed=false) e si conserva com'è per rileggerlo più avanti.
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

// Formato T2PD (vedi sopra): { parsed, earned } oppure null se il file non lo è.
const T2_EARNED = 0x2000;
function t2pd(buf, ids, tMax) {
  if (buf.length < 0x100 || buf.toString('latin1', 0, 4) !== 'T2PD') return null;
  const block = buf.readUInt32BE(0x0c);
  if (block <= 0x20 || block > 0x100000) return null;
  const parts = [buf.subarray(0, 0x40)];
  for (let o = 0x40; o < buf.length; o += block) parts.push(buf.subarray(o, Math.min(o + block - 0x20, buf.length)));
  const s = Buffer.concat(parts);
  if (s.length < 0x70 || s.toString('latin1', 0x40, 0x44) !== 'T2TD') return null;
  const nsec = s.readUInt32BE(0x48);
  if (nsec < 1 || nsec > 64 || 0x70 + nsec * 0x20 > s.length) return null;
  for (let i = 0; i < nsec; i++) {
    const e = 0x70 + i * 0x20;
    if (s.readUInt32BE(e) !== 0x800) continue;
    const size = s.readUInt32BE(e + 4), count = s.readUInt32BE(e + 12);
    const off = Number(s.readBigUInt64BE(e + 0x10)) + 0x40, stride = size + 0x10;
    if (size < 0x30 || !count || off + count * stride > s.length + 0x10) return null;
    const known = new Set(ids);
    const earned = {};
    for (let k = 0; k < count; k++) {
      const r = off + k * stride;
      if (r + 0x28 > s.length || s.readUInt32BE(r) !== 0x800 || s.readUInt32BE(r + 4) !== size) return null;
      const id = s.readUInt32BE(r + 0x10);
      if (!known.has(id) || !(s.readUInt32BE(r + 0x18) & T2_EARNED)) continue;
      const t = Number(s.readBigUInt64BE(r + 0x20)) / 1e6 - EPOCH_0001;
      earned[String(id)] = t >= T_MIN && t <= tMax ? new Date(Math.round(t * 1000)).toISOString() : true;
    }
    return { parsed: true, earned };
  }
  return null;
}

// { parsed, earned: { "<id>": "<ISO>" | true } } — ids = id dei trofei del set, in ordine.
function state(buf, ids, now = Date.now()) {
  const none = { parsed: false, earned: {} };
  if (!Buffer.isBuffer(buf) || buf.length < 32 || !Array.isArray(ids) || !ids.length) return none;
  const t2 = t2pd(buf, ids, now / 1000 + 2 * 86400);
  if (t2) return t2;
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
