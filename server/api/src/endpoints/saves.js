'use strict';
// Salvataggi online. La console cifra il salvataggio (XChaCha20-Poly1305) con
// una chiave che il server non riceve mai; qui arriva un blocco opaco che non
// si apre, non si decomprime e non si interpreta: si controllano solo la
// dimensione dichiarata, l'intestazione fissa ("OMSAVE1\0") e lo SHA-256.
//
// Caricamento a pezzi da 8 MB, perché le connessioni della console cadono:
//   POST /api/v1/saves/begin  {title_id, size, sha256, key_id, device}
//        prenota lo spazio (quota per account e spazio libero sul disco)
//   POST /api/v1/saves/:id/chunk/:n?sha=   corpo = il pezzo, lunghezza esatta
//   POST /api/v1/saves/:id/commit          unisce, verifica lo SHA-256, pubblica
// Scaricamento: GET /api/v1/saves/:id/chunk/:n (gli stessi pezzi).
// Per gioco restano le ultime KEEP versioni; i caricamenti lasciati a metà si
// cancellano dopo PENDING_H ore.
//
// La chiave per un'altra console: GET/POST /api/v1/saves/key conserva la chiave
// cifrata con la parola d'ordine dell'utente (Argon2id, sulla console).
//
// Nomi dei file scelti qui (esadecimali casuali), mai presi dalla richiesta.
const fs = require('node:fs');
const fsp = require('node:fs/promises');
const path = require('node:path');
const crypto = require('node:crypto');
const config = require('../config');
const db = require('../db');
const { HttpError, readJson } = require('../http');
const limiter = require('../ratelimit');

const ROOT = config.savesDir;
const UP = path.join(ROOT, 'up');
const CHUNK = 8 * 1024 * 1024;
const MAGIC = Buffer.from('OMSAVE1\0', 'latin1');
const KEEP = 3;
const PENDING_H = 2;
const MAX_TITLES = 300;
const SAVE_ID = /^[a-f0-9]{32}$/;
const KEY_ID = /^[a-f0-9]{16}$/;
const SHA = /^[a-f0-9]{64}$/;
const TITLE = /^[A-Z]{4}[0-9]{5}$/;
const B64 = /^[A-Za-z0-9+/]+={0,2}$/;

const final = (id) => path.join(ROOT, id.slice(0, 2), `${id}.bin`);
const part = (id, n) => path.join(UP, `${id}.${n}`);
const nchunks = (size) => Math.ceil(size / CHUNK);
const chunkLen = (size, n) => (n < nchunks(size) - 1 ? CHUNK : size - (nchunks(size) - 1) * CHUNK);
const busy = new Map();   // pezzi in arrivo per account, al massimo 2 insieme

function slow(key, limit, win) {
  const s = limiter.hit(key, limit, win);
  if (s) throw new HttpError(429, 'too_many_requests', undefined, { 'retry-after': String(s) });
}

async function freeBytes() {
  try { const s = await fsp.statfs(ROOT); return Number(s.bavail) * Number(s.bsize); } catch { return 0; }
}

async function removeFiles(id, chunks) {
  if (!SAVE_ID.test(id)) return;
  await fsp.rm(final(id), { force: true });
  for (let n = 0; n < (chunks || 0); n++) await fsp.rm(part(id, n), { force: true });
}

// ------------------------------------------------------------------ chiave --
async function keyGet({ auth }) {
  const r = (await db.query('SELECT key_id, kdf_mem, kdf_iter, salt, nonce, wrapped, updated_at FROM lab_save_key WHERE account_id=$1', [auth.accountId])).rows[0];
  if (!r) return { status: 200, body: { exists: false } };
  return { status: 200, body: {
    exists: true, key_id: r.key_id, kdf: 'argon2id', mem: r.kdf_mem, iter: r.kdf_iter,
    salt: r.salt.toString('base64'), nonce: r.nonce.toString('base64'), wrapped: r.wrapped.toString('base64'), updated_at: r.updated_at,
  } };
}

function b64(v, len, name) {
  const s = typeof v === 'string' ? v : '';
  if (!B64.test(s)) throw new HttpError(400, 'invalid_key', name);
  const b = Buffer.from(s, 'base64');
  if (b.length !== len) throw new HttpError(400, 'invalid_key', name);
  return b;
}

// {key_id, mem, iter, salt, nonce, wrapped}. Stessa key_id: nuova parola
// d'ordine per la stessa chiave. key_id diversa: chiave nuova, solo con
// reset=true, e i salvataggi cifrati con la vecchia si cancellano.
async function keySet({ req, auth }) {
  slow(`savekey|${auth.accountId}`, 10, 3600);
  const b = await readJson(req);
  const keyId = String(b.key_id || '');
  if (!KEY_ID.test(keyId)) throw new HttpError(400, 'invalid_key', 'key_id');
  const mem = Number(b.mem), iter = Number(b.iter);
  if (!Number.isInteger(mem) || mem < 8192 || mem > 262144 || !Number.isInteger(iter) || iter < 1 || iter > 16) throw new HttpError(400, 'invalid_key', 'kdf');
  const salt = b64(b.salt, 16, 'salt'), nonce = b64(b.nonce, 24, 'nonce'), wrapped = b64(b.wrapped, 48, 'wrapped');
  const old = (await db.query('SELECT key_id FROM lab_save_key WHERE account_id=$1', [auth.accountId])).rows[0];
  let removed = 0;
  if (old && old.key_id !== keyId) {
    if (b.reset !== true) throw new HttpError(409, 'key_exists');
    const gone = (await db.query('DELETE FROM lab_save WHERE account_id=$1 RETURNING save_id, chunks', [auth.accountId])).rows;
    for (const g of gone) await removeFiles(g.save_id, g.chunks);
    removed = gone.length;
  }
  await db.query(
    `INSERT INTO lab_save_key (account_id, key_id, kdf_mem, kdf_iter, salt, nonce, wrapped) VALUES ($1,$2,$3,$4,$5,$6,$7)
     ON CONFLICT (account_id) DO UPDATE SET key_id=EXCLUDED.key_id, kdf_mem=EXCLUDED.kdf_mem, kdf_iter=EXCLUDED.kdf_iter,
       salt=EXCLUDED.salt, nonce=EXCLUDED.nonce, wrapped=EXCLUDED.wrapped, updated_at=now()`,
    [auth.accountId, keyId, mem, iter, salt, nonce, wrapped]);
  return { status: 200, body: { key_id: keyId, removed } };
}

// ------------------------------------------------------------------ elenco --
async function quota(accountId) {
  const r = (await db.query('SELECT coalesce(sum(size),0)::bigint AS used FROM lab_save WHERE account_id=$1', [accountId])).rows[0];
  return { used: Number(r.used), limit: config.savesQuota, max_save: config.savesMax };
}

async function list({ auth, url }) {
  const title = url.searchParams.get('title');
  const args = [auth.accountId];
  let where = `s.account_id=$1 AND s.status='ready'`;
  if (title) { if (!TITLE.test(title)) throw new HttpError(400, 'invalid_title'); args.push(title); where += ' AND s.title_id=$2'; }
  const r = await db.query(
    `SELECT s.save_id, s.title_id, s.key_id, s.size, s.chunks, s.device, s.committed_at, g.name, g.icon_media
       FROM lab_save s LEFT JOIN lab_game g ON g.game_id=s.title_id WHERE ${where}
      ORDER BY s.committed_at DESC LIMIT 1000`, args);
  const titles = new Map();
  for (const s of r.rows) {
    if (!titles.has(s.title_id)) titles.set(s.title_id, { title_id: s.title_id, name: s.name || null, icon_media: s.icon_media || null, versions: [] });
    titles.get(s.title_id).versions.push({ save_id: s.save_id, key_id: s.key_id, size: Number(s.size), chunks: s.chunks, device: s.device, at: s.committed_at });
  }
  const k = (await db.query('SELECT key_id FROM lab_save_key WHERE account_id=$1', [auth.accountId])).rows[0];
  return { status: 200, body: { titles: [...titles.values()], key_id: k ? k.key_id : null, chunk: CHUNK, quota: await quota(auth.accountId) } };
}

// -------------------------------------------------------------- caricamento --
async function begin({ req, auth }) {
  slow(`savebeg|${auth.accountId}`, 30, 3600);
  const b = await readJson(req);
  const title = String(b.title_id || ''), sha = String(b.sha256 || '').toLowerCase(), keyId = String(b.key_id || '');
  const size = Number(b.size);
  if (!TITLE.test(title)) throw new HttpError(400, 'invalid_title');
  if (!SHA.test(sha) || !KEY_ID.test(keyId)) throw new HttpError(400, 'invalid_request');
  if (!Number.isSafeInteger(size) || size < 64 + 16) throw new HttpError(400, 'invalid_size');
  if (size > config.savesMax) throw new HttpError(413, 'save_too_large', `massimo ${config.savesMax} byte`);
  const device = String(b.device || '').replace(/[\u0000-\u001f<>]/g, '').slice(0, 40) || null;

  const client = await db.pool.connect();
  try {
    await client.query('BEGIN');
    // la riga della chiave fa da lucchetto per account: due begin insieme non superano la quota
    const k = (await client.query('SELECT key_id FROM lab_save_key WHERE account_id=$1 FOR UPDATE', [auth.accountId])).rows[0];
    if (!k) throw new HttpError(409, 'no_key');
    if (k.key_id !== keyId) throw new HttpError(409, 'key_mismatch');
    const pend = (await client.query(`SELECT count(*)::int AS n FROM lab_save WHERE account_id=$1 AND status='uploading'`, [auth.accountId])).rows[0].n;
    if (pend >= 3) throw new HttpError(429, 'too_many_uploads');
    const nt = (await client.query(`SELECT count(DISTINCT title_id)::int AS n FROM lab_save WHERE account_id=$1 AND title_id<>$2`, [auth.accountId, title])).rows[0].n;
    if (nt >= MAX_TITLES) throw new HttpError(409, 'too_many_titles');
    const used = Number((await client.query('SELECT coalesce(sum(size),0)::bigint AS u FROM lab_save WHERE account_id=$1', [auth.accountId])).rows[0].u);
    // la versione più vecchia che il commit toglierà non conta
    const old = (await client.query(
      `SELECT size FROM lab_save WHERE account_id=$1 AND title_id=$2 AND status='ready' ORDER BY committed_at DESC OFFSET $3`,
      [auth.accountId, title, KEEP - 1])).rows.reduce((a, r) => a + Number(r.size), 0);
    if (used - old + size > config.savesQuota) throw new HttpError(413, 'quota_exceeded', undefined);
    const total = Number((await client.query('SELECT coalesce(sum(size),0)::bigint AS t FROM lab_save')).rows[0].t);
    if (total + size > config.savesTotalMax || (await freeBytes()) - 2 * size < config.savesMinFree) throw new HttpError(507, 'server_full');
    const id = crypto.randomBytes(16).toString('hex');
    await client.query(
      'INSERT INTO lab_save (save_id, account_id, title_id, key_id, size, chunks, sha256, device) VALUES ($1,$2,$3,$4,$5,$6,$7,$8)',
      [id, auth.accountId, title, keyId, size, nchunks(size), sha, device]);
    await client.query('COMMIT');
    return { status: 201, body: { save_id: id, chunk: CHUNK, chunks: nchunks(size) } };
  } catch (e) {
    await client.query('ROLLBACK').catch(() => {});
    throw e;
  } finally {
    client.release();
  }
}

async function pending(id, accountId) {
  if (!SAVE_ID.test(id)) throw new HttpError(404, 'not_found');
  const r = (await db.query(
    `SELECT save_id, size, chunks, sha256 FROM lab_save WHERE save_id=$1 AND account_id=$2 AND status='uploading'
       AND created_at > now() - make_interval(hours => $3)`, [id, accountId, PENDING_H])).rows[0];
  if (!r) throw new HttpError(404, 'not_found');
  r.size = Number(r.size);
  return r;
}

function receive(req, file, expected, first) {
  return new Promise((resolve, reject) => {
    const hash = crypto.createHash('sha256');
    const out = fs.createWriteStream(file, { flags: 'w', mode: 0o600 });
    let size = 0, head = Buffer.alloc(0), failed = false;
    const fail = (e) => { if (failed) return; failed = true; req.unpipe(out); req.destroy(); out.destroy(); fsp.rm(file, { force: true }).finally(() => reject(e)); };
    req.setTimeout(60000, () => fail(new HttpError(408, 'timeout')));
    req.on('data', (c) => {
      size += c.length;
      if (size > expected) return fail(new HttpError(413, 'chunk_too_large'));
      if (first && head.length < MAGIC.length) head = Buffer.concat([head, c.subarray(0, MAGIC.length - head.length)]);
      hash.update(c);
      if (!out.write(c)) { req.pause(); out.once('drain', () => req.resume()); }
    });
    req.on('end', () => {
      if (failed) return;
      if (size !== expected) return fail(new HttpError(400, 'chunk_size_mismatch'));
      if (first && !head.equals(MAGIC)) return fail(new HttpError(400, 'invalid_format'));
      out.end(() => resolve(hash.digest('hex')));
    });
    req.on('error', (e) => fail(e));
    out.on('error', (e) => fail(e));
  });
}

async function chunkPut({ req, auth, params, url }) {
  slow(`savechunk|${auth.accountId}`, 900, 3600);
  const s = await pending(String(params.id), auth.accountId);
  const n = Number(params.n);
  if (!Number.isInteger(n) || n < 0 || n >= s.chunks) throw new HttpError(400, 'invalid_chunk');
  const expected = chunkLen(s.size, n);
  const cl = Number(req.headers['content-length']);
  if (req.headers['transfer-encoding'] || !Number.isInteger(cl)) throw new HttpError(411, 'length_required');
  if (cl !== expected) throw new HttpError(400, 'chunk_size_mismatch', `attesi ${expected} byte`);
  const want = String(url.searchParams.get('sha') || '').toLowerCase();
  if (want && !SHA.test(want)) throw new HttpError(400, 'invalid_sha');
  const now = busy.get(auth.accountId) || 0;
  if (now >= 2) throw new HttpError(429, 'too_many_uploads');
  busy.set(auth.accountId, now + 1);
  const tmp = `${part(s.save_id, n)}.${crypto.randomBytes(4).toString('hex')}.tmp`;
  try {
    await fsp.mkdir(UP, { recursive: true });
    const got = await receive(req, tmp, expected, n === 0);
    if (want && got !== want) { await fsp.rm(tmp, { force: true }); throw new HttpError(422, 'chunk_hash_mismatch'); }
    await fsp.rename(tmp, part(s.save_id, n));
    return { status: 204 };
  } finally {
    const left = (busy.get(auth.accountId) || 1) - 1;
    if (left > 0) busy.set(auth.accountId, left); else busy.delete(auth.accountId);
  }
}

async function commit({ auth, params }) {
  slow(`savecommit|${auth.accountId}`, 60, 3600);
  const s = await pending(String(params.id), auth.accountId);
  for (let n = 0; n < s.chunks; n++) {
    const st = await fsp.stat(part(s.save_id, n)).catch(() => null);
    if (!st || st.size !== chunkLen(s.size, n)) throw new HttpError(409, 'missing_chunks', String(n));
  }
  const dest = final(s.save_id), tmp = `${dest}.tmp`;
  await fsp.mkdir(path.dirname(dest), { recursive: true });
  const hash = crypto.createHash('sha256');
  const out = await fsp.open(tmp, 'w', 0o600);
  try {
    for (let n = 0; n < s.chunks; n++) {
      const buf = await fsp.readFile(part(s.save_id, n));
      hash.update(buf);
      await out.write(buf);
    }
    await out.sync();
  } finally {
    await out.close();
  }
  if (hash.digest('hex') !== s.sha256) {
    await fsp.rm(tmp, { force: true });
    await removeFiles(s.save_id, s.chunks);
    await db.query('DELETE FROM lab_save WHERE save_id=$1', [s.save_id]);
    throw new HttpError(422, 'hash_mismatch');
  }
  await fsp.rename(tmp, dest);
  for (let n = 0; n < s.chunks; n++) await fsp.rm(part(s.save_id, n), { force: true });
  const done = (await db.query(
    `UPDATE lab_save SET status='ready', committed_at=now() WHERE save_id=$1 AND status='uploading' RETURNING title_id`, [s.save_id])).rows[0];
  if (!done) { await fsp.rm(dest, { force: true }); throw new HttpError(404, 'not_found'); }
  const old = (await db.query(
    `DELETE FROM lab_save WHERE save_id IN (SELECT save_id FROM lab_save WHERE account_id=$1 AND title_id=$2 AND status='ready'
       ORDER BY committed_at DESC OFFSET $3) RETURNING save_id, chunks`, [auth.accountId, done.title_id, KEEP])).rows;
  for (const o of old) await removeFiles(o.save_id, o.chunks);
  return { status: 200, body: { save_id: s.save_id, title_id: done.title_id, quota: await quota(auth.accountId) } };
}

// ------------------------------------------------------------ scaricamento --
async function chunkGet({ auth, params, res }) {
  slow(`saveget|${auth.accountId}`, 1500, 3600);
  const id = String(params.id), n = Number(params.n);
  if (!SAVE_ID.test(id)) throw new HttpError(404, 'not_found');
  const s = (await db.query(`SELECT size, chunks FROM lab_save WHERE save_id=$1 AND account_id=$2 AND status='ready'`, [id, auth.accountId])).rows[0];
  if (!s || !Number.isInteger(n) || n < 0 || n >= s.chunks) throw new HttpError(404, 'not_found');
  const len = chunkLen(Number(s.size), n);
  const fh = await fsp.open(final(id), 'r').catch(() => null);
  if (!fh) throw new HttpError(404, 'not_found');
  try {
    const buf = Buffer.alloc(len);
    const { bytesRead } = await fh.read(buf, 0, len, n * CHUNK);
    if (bytesRead !== len) throw new HttpError(500, 'storage_error');
    // mai interpretato dal browser: è un blocco cifrato da scaricare
    res.writeHead(200, {
      'content-type': 'application/octet-stream', 'content-length': len,
      'content-disposition': `attachment; filename="${id}.${n}.bin"`,
      'x-content-type-options': 'nosniff', 'cache-control': 'private, no-store',
      'content-security-policy': "default-src 'none'; sandbox",
      'x-chunk-sha256': crypto.createHash('sha256').update(buf).digest('hex'),
    });
    res.end(buf);
    return { sent: true };
  } finally {
    await fh.close();
  }
}

async function remove({ auth, params }) {
  slow(`savedel|${auth.accountId}`, 120, 3600);
  const id = String(params.id);
  if (!SAVE_ID.test(id)) throw new HttpError(404, 'not_found');
  const r = (await db.query('DELETE FROM lab_save WHERE save_id=$1 AND account_id=$2 RETURNING chunks', [id, auth.accountId])).rows[0];
  if (!r) throw new HttpError(404, 'not_found');
  await removeFiles(id, r.chunks);
  return { status: 200, body: { removed: true, quota: await quota(auth.accountId) } };
}

// File di un account che si cancella (le righe vanno via con ON DELETE CASCADE).
async function removeAccountSaves(accountId) {
  const r = (await db.query('SELECT save_id, chunks FROM lab_save WHERE account_id=$1', [accountId])).rows;
  for (const s of r) await removeFiles(s.save_id, s.chunks);
}

// ---------------------------------------------------------------- pulizia --
// Caricamenti scaduti; file senza riga (processo interrotto a metà).
async function sweep() {
  const gone = (await db.query(
    `DELETE FROM lab_save WHERE status='uploading' AND created_at < now() - make_interval(hours => $1) RETURNING save_id, chunks`, [PENDING_H])).rows;
  for (const g of gone) await removeFiles(g.save_id, g.chunks);
  const live = new Set((await db.query('SELECT save_id FROM lab_save')).rows.map((r) => r.save_id));
  const cutoff = Date.now() - PENDING_H * 3600e3;
  const dirs = await fsp.readdir(ROOT).catch(() => []);
  for (const d of dirs) {
    const dir = path.join(ROOT, d);
    if (!/^([a-f0-9]{2}|up)$/.test(d)) continue;
    for (const f of await fsp.readdir(dir).catch(() => [])) {
      const id = f.slice(0, 32);
      const file = path.join(dir, f);
      if (live.has(id) && !f.endsWith('.tmp')) continue;
      const st = await fsp.stat(file).catch(() => null);
      if (st && st.mtimeMs < cutoff) await fsp.rm(file, { force: true });
    }
  }
}

function start() {
  const run = () => sweep().catch((e) => console.error(JSON.stringify({ ts: new Date().toISOString(), event: 'saves_sweep_failed', error: e.message })));
  setTimeout(run, 60_000).unref();
  setInterval(run, 15 * 60_000).unref();
}

module.exports = { keyGet, keySet, list, begin, chunkPut, commit, chunkGet, remove, removeAccountSaves, start, sweep, CHUNK };
