'use strict';
// Voce nel party: relay audio in memoria, senza database per ogni pezzo.
// La console codifica (Opus 16 kHz mono; IMA ADPCM nella build desktop) e
// invia pezzi con POST /api/v1/party/voice; gli altri membri li ricevono in
// long-poll con GET /api/v1/party/voice?after=<cursore>&wait=1. Il server non
// decodifica mai l'audio.
//
// Per party: un anello con gli ultimi 8 s di pezzi, un cursore crescente e un
// EventEmitter che sveglia chi aspetta. L'appartenenza al party è in cache per
// 2 s: un pezzo ogni ~60 ms non deve costare una query.
//
// Risposta binaria (little-endian):
//   "OVC1" | u32 next_cursor | u16 count |
//   count × ( u8 oid_len | oid | u8 codec (1=opus, 2=adpcm) | u32 seq | u16 len | data )
const { EventEmitter } = require('node:events');
const db = require('../db');
const { HttpError, retryLater } = require('../http');
const limiter = require('../ratelimit');

const MAX_CHUNK = 8 * 1024;
const KEEP_MS = 8000;
const WAIT_MS = 1500;
const TALKING_MS = 600;
const MEMBER_TTL_MS = 2000;
const CODECS = { opus: 1, adpcm: 2 };
const MAGIC = Buffer.from('OVC1', 'ascii');

// party_id → { cursor, chunks: [{cursor, from, fromId, codec, seq, data, ts}], ee, last: Map(online_id→ts), touched }
const parties = new Map();
// account_id → { partyId, muted, exp }
const members = new Map();

function partyBuf(partyId) {
  let p = parties.get(partyId);
  if (!p) {
    p = { cursor: 0, chunks: [], ee: new EventEmitter(), last: new Map(), touched: Date.now() };
    p.ee.setMaxListeners(0);
    parties.set(partyId, p);
  }
  return p;
}

function trim(p, now) {
  let i = 0;
  while (i < p.chunks.length && now - p.chunks[i].ts > KEEP_MS) i++;
  if (i) p.chunks.splice(0, i);
}

// Il party attivo dell'account (con cache breve), oppure null.
async function membership(accountId) {
  const now = Date.now();
  const hit = members.get(accountId);
  if (hit && hit.exp > now) return hit.partyId ? hit : null;
  const r = await db.query(
    `SELECT m.party_id, m.muted FROM lab_party_member m JOIN lab_party p USING (party_id)
      WHERE m.account_id=$1 AND NOT p.closed`, [accountId]);
  const row = r.rows[0];
  const v = { partyId: row ? String(row.party_id) : null, muted: row ? row.muted : false, exp: now + MEMBER_TTL_MS };
  members.set(accountId, v);
  return v.partyId ? v : null;
}

// Da chiamare quando cambia l'appartenenza: ingresso, uscita, muto.
function forget(accountId) { members.delete(String(accountId)); }

// Chi ha parlato negli ultimi 600 ms, per lo stato del party.
function talking(partyId) {
  const p = parties.get(String(partyId));
  if (!p) return [];
  const now = Date.now();
  const out = [];
  for (const [oid, ts] of p.last) if (now - ts <= TALKING_MS) out.push(oid);
  return out;
}

function readChunk(req) {
  return new Promise((resolve, reject) => {
    const parts = [];
    let size = 0;
    let done = false;
    req.on('data', (c) => {
      if (done) return;
      size += c.length;
      if (size > MAX_CHUNK) {
        done = true;
        reject(new HttpError(413, 'chunk_too_large', `massimo ${MAX_CHUNK} byte`));
        req.resume();
        return;
      }
      parts.push(c);
    });
    req.on('end', () => { if (!done) { done = true; resolve(Buffer.concat(parts)); } });
    req.on('error', (e) => { if (!done) { done = true; reject(e); } });
  });
}

// POST /api/v1/party/voice?codec=opus|adpcm&seq=<u32>   corpo = un pezzo (≤ 8 KB)
async function post({ req, url, auth }) {
  const wait = limiter.hit(`voice|${auth.accountId}`, 30, 1);
  if (wait) throw retryLater('too_many_chunks', wait);
  const codec = CODECS[String(url.searchParams.get('codec') || '').toLowerCase()];
  if (!codec) throw new HttpError(400, 'invalid_codec', 'codec=opus|adpcm');
  const seqRaw = url.searchParams.get('seq');
  const seq = Number(seqRaw);
  if (seqRaw == null || seqRaw === '' || !Number.isInteger(seq) || seq < 0 || seq > 0xffffffff) throw new HttpError(400, 'invalid_seq');
  const len = Number(req.headers['content-length'] || 0);
  if (len > MAX_CHUNK) throw new HttpError(413, 'chunk_too_large', `massimo ${MAX_CHUNK} byte`);
  const m = await membership(auth.accountId);
  if (!m) throw new HttpError(403, 'not_in_party');
  if (m.muted) throw new HttpError(403, 'muted');
  const data = await readChunk(req);
  if (!data.length) throw new HttpError(400, 'empty_chunk');
  const p = partyBuf(m.partyId);
  const now = Date.now();
  trim(p, now);
  p.cursor = (p.cursor + 1) >>> 0;
  p.chunks.push({ cursor: p.cursor, from: auth.onlineId, fromId: auth.accountId, codec, seq, data, ts: now });
  p.last.set(auth.onlineId, now);
  p.touched = now;
  p.ee.emit('chunk', auth.accountId);
  return { status: 204 };
}

function encode(nextCursor, chunks) {
  const parts = [];
  const head = Buffer.alloc(10);
  MAGIC.copy(head, 0);
  head.writeUInt32LE(nextCursor >>> 0, 4);
  head.writeUInt16LE(chunks.length, 8);
  parts.push(head);
  for (const c of chunks) {
    const oid = Buffer.from(c.from, 'utf8').subarray(0, 255);
    const h = Buffer.alloc(1 + oid.length + 1 + 4 + 2);
    let o = 0;
    h.writeUInt8(oid.length, o); o += 1;
    oid.copy(h, o); o += oid.length;
    h.writeUInt8(c.codec, o); o += 1;
    h.writeUInt32LE(c.seq >>> 0, o); o += 4;
    h.writeUInt16LE(c.data.length, o);
    parts.push(h, c.data);
  }
  return Buffer.concat(parts);
}

function sendBinary(res, buf) {
  if (res.writableEnded || res.destroyed) return;
  res.writeHead(200, { 'content-type': 'application/octet-stream', 'content-length': buf.length, 'cache-control': 'no-store' });
  res.end(buf);
}

// GET /api/v1/party/voice?after=<cursor>&wait=1
async function poll({ req, res, url, auth }) {
  const m = await membership(auth.accountId);
  if (!m) throw new HttpError(403, 'not_in_party');
  const p = partyBuf(m.partyId);
  p.touched = Date.now();
  const after = Number(url.searchParams.get('after') || 0) >>> 0;
  const wait = url.searchParams.get('wait') === '1';
  const pending = () => {
    trim(p, Date.now());
    return p.chunks.filter((c) => c.cursor > after && c.fromId !== auth.accountId).slice(-200);
  };
  // Al primo giro (after=0) o con un cursore "dal futuro" (server riavviato) si
  // restituisce solo il cursore attuale, così chi entra non sente audio vecchio.
  // Se il party non ha ancora audio si aspetta come sempre, o il client
  // girerebbe a vuoto.
  if ((after === 0 && p.cursor > 0) || after > p.cursor) { sendBinary(res, encode(p.cursor, [])); return { sent: true }; }
  let list = pending();
  if (list.length || !wait) { sendBinary(res, encode(p.cursor, list)); return { sent: true }; }
  await new Promise((resolve) => {
    let finished = false;
    const done = () => {
      if (finished) return;
      finished = true;
      clearTimeout(timer);
      p.ee.removeListener('chunk', onChunk);
      res.removeListener('close', done);
      resolve();
    };
    const onChunk = (fromId) => { if (fromId !== auth.accountId) done(); };
    const timer = setTimeout(done, WAIT_MS);
    p.ee.on('chunk', onChunk);
    res.on('close', done); // il client se n'è andato
  });
  if (res.destroyed || res.writableEnded) return { sent: true };
  list = pending();
  sendBinary(res, encode(p.cursor, list));
  return { sent: true };
}

// Pulizia dei party senza voce da un minuto e delle appartenenze scadute.
setInterval(() => {
  const now = Date.now();
  for (const [id, p] of parties) {
    trim(p, now);
    for (const [oid, ts] of p.last) if (now - ts > KEEP_MS) p.last.delete(oid);
    if (!p.chunks.length && now - p.touched > 60_000 && p.ee.listenerCount('chunk') === 0) parties.delete(id);
  }
  for (const [id, v] of members) if (v.exp <= now) members.delete(id);
}, 30_000).unref();

module.exports = { post, poll, talking, forget };
