'use strict';
// Profilo, amici, presenza e attività. La presenza la aggiorna l'app con
// POST /api/v1/presence: un account è online solo se il suo stato lo dice e
// l'ultimo segnale (last_seen) è più recente di STALE_SECONDS.
const db = require('../db');
const { HttpError, readJson } = require('../http');
const { notify, notifyFriends } = require('../notify');
const messages = require('../messages');
const rel = require('../relations');
const playtime = require('../playtime');

const STALE_SECONDS = 120;
const { accountByOnlineId } = rel;

// Presenza effettiva: offline se l'ultimo segnale è troppo vecchio.
function effective(row) {
  if (!row) return { status: 'offline', game_id: null, game_name: null, started_at: null, last_seen: null };
  const stale = row.last_seen && (Date.now() - new Date(row.last_seen).getTime()) / 1000 > STALE_SECONDS;
  const status = stale ? 'offline' : row.status;
  return {
    status,
    game_id: status === 'offline' ? null : row.game_id,
    game_name: status === 'offline' ? null : row.game_name,
    started_at: status === 'offline' ? null : row.started_at,
    last_seen: row.last_seen,
  };
}

async function presenceOf(accountId) {
  const r = await db.query('SELECT status, game_id, game_name, started_at, last_seen FROM lab_presence WHERE account_id = $1', [accountId]);
  return effective(r.rows[0]);
}

// Presenza come la vedono gli altri (stato personalizzato applicato) e messaggio di stato.
async function visiblePresence(accountId) {
  const r = await db.query(
    `SELECT a.status_mode, a.status_message, p.status, p.game_id, p.game_name, p.started_at, p.last_seen
       FROM lab_account a LEFT JOIN lab_presence p USING (account_id) WHERE a.account_id = $1`, [accountId]);
  const row = r.rows[0] || {};
  return { presence: rel.maskPresence(effective(row.last_seen ? row : null), row.status_mode), status_message: row.status_message || null };
}

// GET /api/v1/profile — il mio profilo + presenza + conteggi.
async function profile({ auth }) {
  const a = (await db.query(
    'SELECT account_id, online_id, email, created_at, last_login_at FROM lab_account WHERE account_id = $1',
    [auth.accountId],
  )).rows[0];
  const friends = (await db.query(
    `SELECT count(*)::int AS n FROM lab_friendship
      WHERE status='accepted' AND (requester_id=$1 OR addressee_id=$1)`, [auth.accountId])).rows[0].n;
  const pending = (await db.query(
    `SELECT count(*)::int AS n FROM lab_friendship WHERE status='pending' AND addressee_id=$1`, [auth.accountId])).rows[0].n;
  return {
    status: 200,
    body: {
      account_id: String(a.account_id), online_id: a.online_id, email: a.email,
      created_at: a.created_at, last_login_at: a.last_login_at,
      presence: await presenceOf(auth.accountId),
      friends_count: friends, pending_requests: pending,
    },
  };
}

// GET /api/v1/friends — amici accettati (con presenza) + richieste in/out.
async function friends({ auth }) {
  const acc = await db.query(
    `SELECT a.account_id, a.online_id, a.avatar, a.avatar_media, a.avatar_frames, a.status_message,
            CASE WHEN f.requester_id=$1 THEN 'outgoing' ELSE 'incoming' END AS dir,
            f.status, f.created_at
       FROM lab_friendship f
       JOIN lab_account a
         ON a.account_id = CASE WHEN f.requester_id=$1 THEN f.addressee_id ELSE f.requester_id END
      WHERE f.requester_id=$1 OR f.addressee_id=$1
      ORDER BY a.online_id`, [auth.accountId]);

  const list = [];
  const incoming = [];
  const outgoing = [];
  for (const row of acc.rows) {
    const base = { account_id: String(row.account_id), online_id: row.online_id, avatar: row.avatar, avatar_media: row.avatar_media, avatar_frames: row.avatar_frames, since: row.created_at };
    if (row.status === 'accepted') {
      const v = await visiblePresence(row.account_id);
      list.push({ ...base, presence: v.presence, status_message: v.status_message });
    }
    else if (row.status === 'pending' && row.dir === 'incoming') incoming.push(base);
    else if (row.status === 'pending' && row.dir === 'outgoing') outgoing.push(base);
  }
  return { status: 200, body: { friends: list, incoming_requests: incoming, outgoing_requests: outgoing } };
}

// POST /api/v1/friends/request { online_id }
async function friendRequest({ req, auth }) {
  const body = await readJson(req);
  const target = await accountByOnlineId(body.online_id);
  if (!target) throw new HttpError(404, 'account_not_found');
  if (String(target.account_id) === String(auth.accountId)) throw new HttpError(400, 'cannot_befriend_self');
  if (await rel.isBlocked(auth.accountId, target.account_id)) throw new HttpError(403, 'blocked');
  // una richiesta in senso inverso già in attesa vale come accettazione
  const inverse = await db.query(
    `SELECT 1 FROM lab_friendship WHERE requester_id=$1 AND addressee_id=$2 AND status='pending'`,
    [target.account_id, auth.accountId]);
  if (inverse.rowCount) {
    await db.query(`UPDATE lab_friendship SET status='accepted', responded_at=now()
                    WHERE requester_id=$1 AND addressee_id=$2`, [target.account_id, auth.accountId]);
    await notify(target.account_id, 'friend_accept', {
      actorId: auth.accountId, title: (l) => messages.t(l, 'notify.friend_accept', { actor: auth.onlineId }), ref: auth.onlineId });
    return { status: 200, body: { result: 'accepted', online_id: target.online_id } };
  }
  const existing = await db.query(
    `SELECT status FROM lab_friendship
      WHERE (requester_id=$1 AND addressee_id=$2) OR (requester_id=$2 AND addressee_id=$1)`,
    [auth.accountId, target.account_id]);
  if (existing.rowCount && existing.rows[0].status === 'accepted') {
    return { status: 200, body: { result: 'already_friends', online_id: target.online_id } };
  }
  // privacy di chi riceve: tutti | solo amici di amici | nessuno
  const priv = await rel.privacyOf(target.account_id);
  if (priv.friend_requests === 'nobody') throw new HttpError(403, 'privacy');
  if (priv.friend_requests === 'friends_of_friends' && !(await rel.mutualCount(auth.accountId, target.account_id))) {
    throw new HttpError(403, 'privacy');
  }
  const ins = await db.query(
    `INSERT INTO lab_friendship (requester_id, addressee_id, status)
     VALUES ($1,$2,'pending')
     ON CONFLICT (requester_id, addressee_id) DO NOTHING`, [auth.accountId, target.account_id]);
  if (ins.rowCount) {
    await notify(target.account_id, 'friend_request', {
      actorId: auth.accountId, title: (l) => messages.t(l, 'notify.friend_request', { actor: auth.onlineId }), ref: auth.onlineId });
  }
  return { status: 201, body: { result: 'pending', online_id: target.online_id } };
}

// POST /api/v1/friends/accept { online_id }  (accetta una richiesta in entrata)
async function friendAccept({ req, auth }) {
  const body = await readJson(req);
  const other = await accountByOnlineId(body.online_id);
  if (!other) throw new HttpError(404, 'account_not_found');
  const r = await db.query(
    `UPDATE lab_friendship SET status='accepted', responded_at=now()
      WHERE requester_id=$1 AND addressee_id=$2 AND status='pending'`,
    [other.account_id, auth.accountId]);
  if (!r.rowCount) throw new HttpError(404, 'no_pending_request');
  await notify(other.account_id, 'friend_accept', {
    actorId: auth.accountId, title: (l) => messages.t(l, 'notify.friend_accept', { actor: auth.onlineId }), ref: auth.onlineId });
  return { status: 200, body: { result: 'accepted', online_id: other.online_id } };
}

// POST /api/v1/friends/decline { online_id }
async function friendDecline({ req, auth }) {
  const body = await readJson(req);
  const other = await accountByOnlineId(body.online_id);
  if (!other) throw new HttpError(404, 'account_not_found');
  await db.query(
    `DELETE FROM lab_friendship
      WHERE requester_id=$1 AND addressee_id=$2 AND status='pending'`,
    [other.account_id, auth.accountId]);
  return { status: 200, body: { result: 'declined', online_id: other.online_id } };
}

// DELETE /api/v1/friends/:onlineId  (rimuove amicizia o richiesta, in entrambe le direzioni)
async function friendRemove({ params, auth }) {
  const other = await accountByOnlineId(params.onlineId);
  if (!other) throw new HttpError(404, 'account_not_found');
  await db.query(
    `DELETE FROM lab_friendship
      WHERE (requester_id=$1 AND addressee_id=$2) OR (requester_id=$2 AND addressee_id=$1)`,
    [auth.accountId, other.account_id]);
  return { status: 200, body: { result: 'removed', online_id: other.online_id } };
}

// GET /api/v1/presence — la mia presenza + quella degli amici accettati.
async function presence({ auth }) {
  const me = await presenceOf(auth.accountId);
  const fr = await db.query(
    `SELECT a.account_id, a.online_id, a.status_message
       FROM lab_friendship f
       JOIN lab_account a
         ON a.account_id = CASE WHEN f.requester_id=$1 THEN f.addressee_id ELSE f.requester_id END
      WHERE f.status='accepted' AND (f.requester_id=$1 OR f.addressee_id=$1)`, [auth.accountId]);
  const friendsPresence = [];
  for (const row of fr.rows) {
    const v = await visiblePresence(row.account_id);
    friendsPresence.push({ account_id: String(row.account_id), online_id: row.online_id, presence: v.presence, status_message: v.status_message });
  }
  return { status: 200, body: { me, friends: friendsPresence } };
}

// POST /api/v1/presence { status, game_id?, game_name? }
// Fa anche da heartbeat; un cambio di gioco diventa attività e notifica agli amici.
async function presenceSet({ req, auth }) {
  const body = await readJson(req);
  const status = ['online', 'offline', 'away', 'dnd'].includes(body.status) ? body.status : 'online';
  const gameId = body.game_id != null ? String(body.game_id).slice(0, 64) : null;
  const gameName = body.game_name != null ? String(body.game_name).slice(0, 128) : null;

  const prevRow = (await db.query('SELECT status, game_id, game_name, started_at, last_seen, play_since FROM lab_presence WHERE account_id=$1', [auth.accountId])).rows[0];
  const prev = prevRow ? effective(prevRow) : null;
  await db.query(
    `INSERT INTO lab_presence (account_id, status, game_id, game_name, started_at, last_seen)
     VALUES ($1,$2,$3::text,$4::text, CASE WHEN $3::text IS NULL THEN NULL ELSE now() END, now())
     ON CONFLICT (account_id) DO UPDATE SET
       status=EXCLUDED.status,
       game_id=EXCLUDED.game_id,
       game_name=EXCLUDED.game_name,
       started_at = CASE WHEN EXCLUDED.game_id IS DISTINCT FROM lab_presence.game_id
                         THEN EXCLUDED.started_at ELSE lab_presence.started_at END,
       last_seen=now()`,
    [auth.accountId, status, gameId, gameName]);
  await playtime.onPresence(auth.accountId, prevRow, { status, gameId });

  // chi è invisibile non lascia attività e non avvisa gli amici
  const mode = (await db.query('SELECT status_mode FROM lab_account WHERE account_id=$1', [auth.accountId])).rows[0].status_mode;
  if (mode === 'invisible') return { status: 200, body: { result: 'ok', presence: await presenceOf(auth.accountId) } };
  if (gameId && (!prev || prev.game_id !== gameId)) {
    await db.query(
      `INSERT INTO lab_activity (account_id, type, game_id, game_name, detail)
       VALUES ($1,'game_start',$2,$3,$4)`, [auth.accountId, gameId, gameName, 'ha iniziato a giocare']);
    await notifyFriends(auth.accountId, 'game_start', {
      title: (l) => messages.t(l, 'notify.game_start', { actor: auth.onlineId, game: gameName || gameId }), body: gameName || gameId, ref: gameId });
  } else if (status !== 'offline' && (!prev || prev.status === 'offline')) {
    await db.query(`INSERT INTO lab_activity (account_id, type, detail) VALUES ($1,'online','è online')`, [auth.accountId]);
    await notifyFriends(auth.accountId, 'online', { title: (l) => messages.t(l, 'notify.online', { actor: auth.onlineId }), ref: auth.onlineId });
  }
  return { status: 200, body: { result: 'ok', presence: await presenceOf(auth.accountId) } };
}

// GET /api/v1/activity — attività recenti mie e degli amici, esclusi chi
// nasconde la propria attività e chi è in un blocco con me.
async function activity({ auth }) {
  const r = await db.query(
    `SELECT v.activity_id, ac.online_id, ac.avatar, ac.avatar_media, ac.avatar_frames, v.type, v.game_id, v.game_name, v.detail, v.created_at
       FROM lab_activity v
       JOIN lab_account ac ON ac.account_id = v.account_id
      WHERE v.account_id = $1
         OR (v.account_id IN ${rel.FRIENDS_SQL('$1')} AND ac.show_activity
             AND v.account_id NOT IN ${rel.BLOCKED_SQL('$1')})
      ORDER BY v.created_at DESC
      LIMIT 50`, [auth.accountId]);
  return { status: 200, body: { activity: r.rows } };
}

module.exports = {
  profile, friends, friendRequest, friendAccept, friendDecline, friendRemove,
  presence, presenceSet, activity, effective,
};
