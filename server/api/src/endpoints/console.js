'use strict';
// La console vista dal telefono (web app).
//  · POST /api/v1/console/announce  dal servizio della console, ogni 30 s:
//    {lan_ip, port, token, version}. Indirizzo e token del telecomando restano
//    visibili solo allo stesso account.
//  · GET  /api/v1/console  accesa o no (annuncio negli ultimi 90 s), e il link
//    diretto alla console nella rete di casa: i file non passano mai dal server.
//  · POST /api/v1/console/queue {app_id}  "Installa sulla PS5" dallo Store;
//    GET /api/v1/console/queue  la console prende i comandi (e li segna presi).
const db = require('../db');
const { HttpError, readJson } = require('../http');
const limiter = require('../ratelimit');

const ONLINE_S = 90;
const IPV4 = /^(10|172|192|169)\.\d{1,3}\.\d{1,3}\.\d{1,3}$/;   // solo indirizzi della rete di casa

async function announce({ req, auth }) {
  if (limiter.hit(`console_ann|${auth.accountId}`, 20, 60)) throw new HttpError(429, 'too_many_requests');
  const b = await readJson(req);
  const ip = String(b.lan_ip || '').trim();
  const port = Number(b.port) || 9095;
  const token = String(b.token || '').replace(/[^A-Za-z0-9]/g, '').slice(0, 64) || null;
  await db.query(
    `INSERT INTO lab_console (account_id, lan_ip, port, remote_token, version, updated_at) VALUES ($1,$2,$3,$4,$5,now())
     ON CONFLICT (account_id) DO UPDATE SET lan_ip=EXCLUDED.lan_ip, port=EXCLUDED.port, remote_token=EXCLUDED.remote_token, version=EXCLUDED.version, updated_at=now()`,
    [auth.accountId, IPV4.test(ip) ? ip : null, port > 0 && port < 65536 ? port : 9095, token, String(b.version || '').slice(0, 24) || null]);
  return { status: 204 };
}

async function status({ auth }) {
  const r = (await db.query(
    `SELECT c.lan_ip, c.port, c.remote_token, c.version, c.updated_at, extract(epoch FROM now()-c.updated_at)::int AS age,
            p.game_id, p.game_name
       FROM lab_console c LEFT JOIN lab_presence p ON p.account_id=c.account_id WHERE c.account_id=$1`, [auth.accountId])).rows[0];
  if (!r) return { status: 200, body: { online: false, paired: false, last_seen: null, lan_ip: null, port: 9095, link: null, version: null, game: null } };
  const online = r.age <= ONLINE_S;
  const link = online && r.lan_ip && r.remote_token ? `http://${r.lan_ip}:${r.port}/#k=${r.remote_token}` : null;
  return { status: 200, body: {
    online, paired: true, last_seen: r.updated_at, lan_ip: online ? r.lan_ip : null, port: r.port, link, version: r.version,
    game: online && r.game_id ? { id: r.game_id, name: r.game_name } : null,
  } };
}

async function queueAdd({ req, auth }) {
  if (limiter.hit(`console_q|${auth.accountId}`, 30, 3600)) throw new HttpError(429, 'too_many_requests');
  const b = await readJson(req);
  const app = (await db.query('SELECT app_id FROM lab_store_app WHERE app_id=$1 AND published', [Number(b.app_id) || 0])).rows[0];
  if (!app) throw new HttpError(404, 'app_not_found');
  const pend = (await db.query('SELECT count(*)::int AS n FROM lab_console_queue WHERE account_id=$1 AND taken_at IS NULL', [auth.accountId])).rows[0].n;
  if (pend >= 20) throw new HttpError(429, 'queue_full');
  await db.query('INSERT INTO lab_console_queue (account_id, app_id) VALUES ($1,$2)', [auth.accountId, app.app_id]);
  const c = (await db.query('SELECT extract(epoch FROM now()-updated_at)::int AS age FROM lab_console WHERE account_id=$1', [auth.accountId])).rows[0];
  return { status: 202, body: { queued: true, online: !!c && c.age <= ONLINE_S } };
}

async function queueTake({ auth }) {
  const r = await db.query(
    `UPDATE lab_console_queue SET taken_at=now() WHERE queue_id IN
       (SELECT queue_id FROM lab_console_queue WHERE account_id=$1 AND taken_at IS NULL ORDER BY queue_id LIMIT 10)
     RETURNING queue_id::text, app_id::text`, [auth.accountId]);
  return { status: 200, body: { items: r.rows } };
}

module.exports = { announce, status, queueAdd, queueTake };
