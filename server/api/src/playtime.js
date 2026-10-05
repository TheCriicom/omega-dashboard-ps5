'use strict';
// Tempo di gioco ricavato dalla presenza. lab_presence.play_since segna
// l'inizio del tratto ancora da contare: quando il gioco cambia o la console
// va offline o sparisce, il tratto si chiude e finisce in lab_playtime (totale)
// e lab_playtime_week (settimana in cui termina).
//
// Tratti sotto i 30 s si scartano; uno solo vale al massimo 12 h. Una console
// sparita senza avvisare chiude il tratto all'ultimo last_seen, al sync
// successivo o nel giro periodico. Ogni chiusura passa da un UPDATE
// condizionato su play_since: due richieste concorrenti non contano lo stesso
// tratto due volte.
const db = require('./db');

const MIN_SECONDS = 30;
const MAX_SECONDS = 12 * 3600;
const STALE_MS = 120 * 1000; // come STALE_SECONDS in endpoints/social.js
const SWEEP_AFTER = '3 minutes';
const SWEEP_EVERY_MS = 60 * 1000;

function segmentSeconds(start, end) {
  const s = Math.floor((new Date(end).getTime() - new Date(start).getTime()) / 1000);
  if (!(s >= MIN_SECONDS)) return 0;
  return Math.min(s, MAX_SECONDS);
}

async function addSegment(accountId, gameId, gameName, start, end) {
  const secs = segmentSeconds(start, end);
  if (!secs || !gameId) return 0;
  await db.query(
    `INSERT INTO lab_playtime (account_id, game_id, game_name, seconds, sessions, last_played)
     VALUES ($1,$2,$3,$4,1,$5)
     ON CONFLICT (account_id, game_id) DO UPDATE SET
       seconds = lab_playtime.seconds + EXCLUDED.seconds,
       sessions = lab_playtime.sessions + 1,
       game_name = coalesce(EXCLUDED.game_name, lab_playtime.game_name),
       last_played = greatest(lab_playtime.last_played, EXCLUDED.last_played)`,
    [accountId, gameId, gameName, secs, new Date(end)]);
  await db.query(
    `INSERT INTO lab_playtime_week (account_id, game_id, week_start, seconds)
     VALUES ($1,$2, date_trunc('week', $4::timestamptz)::date, $3)
     ON CONFLICT (account_id, game_id, week_start) DO UPDATE SET seconds = lab_playtime_week.seconds + EXCLUDED.seconds`,
    [accountId, gameId, secs, new Date(end)]);
  return secs;
}

// Sposta play_since da `from` a `to` solo se nessun altro l'ha già fatto.
async function swap(accountId, from, to) {
  const r = await db.query(
    'UPDATE lab_presence SET play_since=$3 WHERE account_id=$1 AND play_since IS NOT DISTINCT FROM $2',
    [accountId, from, to]);
  return r.rowCount > 0;
}

const isStale = (row, now) => !row.last_seen || now - new Date(row.last_seen).getTime() > STALE_MS;

// Dopo POST /api/v1/presence; prev è la riga di lab_presence prima dell'aggiornamento.
async function onPresence(accountId, prev, { status, gameId }) {
  const now = Date.now();
  const newGame = status !== 'offline' ? gameId : null;
  if (prev && prev.play_since) {
    const stale = isStale(prev, now) || prev.status === 'offline';
    // stesso gioco, nessun buco: il tratto continua
    if (!stale && newGame && newGame === prev.game_id) return;
    const end = stale ? prev.last_seen : new Date(now);
    if (await swap(accountId, prev.play_since, newGame ? new Date(now) : null)) {
      await addSegment(accountId, prev.game_id, prev.game_name, prev.play_since, end);
    }
    return;
  }
  if (newGame) await swap(accountId, null, new Date(now));
}

// Dal sync (heartbeat), prima di rinfrescare last_seen.
async function onHeartbeat(accountId) {
  const row = (await db.query(
    'SELECT status, game_id, game_name, last_seen, play_since FROM lab_presence WHERE account_id=$1', [accountId])).rows[0];
  if (!row || row.status === 'offline') return;
  const now = Date.now();
  const stale = isStale(row, now);
  if (row.play_since && stale) {
    // la console era sparita: si chiude all'ultimo segno di vita e si riparte da ora
    if (await swap(accountId, row.play_since, row.game_id ? new Date(now) : null)) {
      await addSegment(accountId, row.game_id, row.game_name, row.play_since, row.last_seen);
    }
  } else if (!row.play_since && row.game_id) {
    await swap(accountId, null, new Date(now));
  }
}

// Chiude i tratti rimasti aperti di console sparite da più di 3 minuti e ne
// spegne la presenza: senza questo la riga resterebbe «online» per sempre, e
// chi torna dopo ore senza gioco erediterebbe quello di prima. swept la
// distingue dall'offline scelto dall'utente (vedi sync in endpoints/hub.js).
async function sweep() {
  const r = await db.query(
    `SELECT account_id, game_id, game_name, last_seen, play_since FROM lab_presence
      WHERE play_since IS NOT NULL AND last_seen < now() - interval '${SWEEP_AFTER}' LIMIT 500`);
  let closed = 0;
  for (const row of r.rows) {
    if (await swap(row.account_id, row.play_since, null)) {
      if (await addSegment(row.account_id, row.game_id, row.game_name, row.play_since, row.last_seen)) closed++;
    }
  }
  await db.query(
    `UPDATE lab_presence SET status='offline', swept=true, game_id=NULL, game_name=NULL, started_at=NULL
      WHERE status<>'offline' AND play_since IS NULL AND last_seen < now() - interval '${SWEEP_AFTER}'`);
  return closed;
}

// Tratti aperti e ancora vivi, per mostrare il tempo in corso:
// Map(account_id → { game_id, game_name, seconds }). Senza elenco: di tutti.
async function openSegments(accountIds) {
  const out = new Map();
  if (accountIds && !accountIds.length) return out;
  const r = await db.query(
    `SELECT account_id, game_id, game_name, play_since, last_seen, status FROM lab_presence
      WHERE ($1::bigint[] IS NULL OR account_id = ANY($1::bigint[]))
        AND play_since IS NOT NULL AND game_id IS NOT NULL AND status <> 'offline'`,
    [accountIds || null]);
  const now = Date.now();
  for (const row of r.rows) {
    if (isStale(row, now)) continue;
    const secs = segmentSeconds(row.play_since, now);
    if (secs) out.set(String(row.account_id), { game_id: row.game_id, game_name: row.game_name, seconds: secs });
  }
  return out;
}

function start() {
  const run = () => sweep().catch((e) => console.error(JSON.stringify({ ts: new Date().toISOString(), event: 'playtime_sweep_failed', error: e.message })));
  setInterval(run, SWEEP_EVERY_MS).unref();
}

module.exports = { onPresence, onHeartbeat, sweep, openSegments, start };
