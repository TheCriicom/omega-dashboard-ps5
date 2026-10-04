'use strict';
// Stato personalizzato, blocchi, privacy, segnalazioni di utenti,
// suggerimenti di amicizia e inviti a giocare.
//
// Un blocco vale nei due sensi: niente richieste di amicizia, messaggi,
// inviti, post e commenti visibili, ricerca e suggerimenti. Bloccare chiude
// l'amicizia e le richieste in sospeso; nei gruppi in comune si resta membri,
// ma i messaggi dell'altro non si vedono più.
const db = require('../db');
const { HttpError, readJson } = require('../http');
const { notify } = require('../notify');
const messages = require('../messages');
const limiter = require('../ratelimit');
const rel = require('../relations');
const { fileReport } = require('../socialreport');
const { oneLine, charLength } = require('../text');

const { accountByOnlineId } = rel;

// --------------------------------------------------------------- stato --
// POST /api/v1/status  { mode: online|away|dnd|invisible, message? (≤ 60) }
async function setStatus({ req, auth }) {
  const body = await readJson(req);
  const mode = String(body.mode || '');
  if (!rel.STATUS_MODES.includes(mode)) throw new HttpError(400, 'invalid_mode', 'mode: online|away|dnd|invisible');
  let message;
  if (body.message !== undefined) {
    const s = oneLine(body.message);
    if (charLength(s) > 60) throw new HttpError(400, 'message_too_long', 'massimo 60 caratteri');
    message = s || null;
  }
  const r = await db.query(
    `UPDATE lab_account SET status_mode=$2, status_message = CASE WHEN $3::boolean THEN $4 ELSE status_message END
      WHERE account_id=$1 RETURNING status_mode, status_message`,
    [auth.accountId, mode, message !== undefined, message === undefined ? null : message]);
  return { status: 200, body: { status_mode: r.rows[0].status_mode, status_message: r.rows[0].status_message } };
}

// -------------------------------------------------------------- blocchi --
// POST /api/v1/users/:onlineId/block
async function block({ auth, params }) {
  const other = await accountByOnlineId(params.onlineId);
  if (!other) throw new HttpError(404, 'account_not_found');
  if (String(other.account_id) === String(auth.accountId)) throw new HttpError(400, 'cannot_block_self');
  await db.query('INSERT INTO lab_block (blocker_id, blocked_id) VALUES ($1,$2) ON CONFLICT DO NOTHING', [auth.accountId, other.account_id]);
  // amicizia e richieste in sospeso, nei due sensi
  await db.query(
    `DELETE FROM lab_friendship WHERE (requester_id=$1 AND addressee_id=$2) OR (requester_id=$2 AND addressee_id=$1)`,
    [auth.accountId, other.account_id]);
  // inviti a party in sospeso tra i due
  await db.query(
    `DELETE FROM lab_party_invite WHERE (from_id=$1 AND to_id=$2) OR (from_id=$2 AND to_id=$1)`, [auth.accountId, other.account_id]);
  return { status: 200, body: { result: 'blocked', online_id: other.online_id } };
}

// DELETE /api/v1/users/:onlineId/block
async function unblock({ auth, params }) {
  const r = await db.query('SELECT account_id, online_id FROM lab_account WHERE lower(online_id)=lower($1)', [String(params.onlineId || '')]);
  const other = r.rows[0];
  if (!other) throw new HttpError(404, 'account_not_found');
  await db.query('DELETE FROM lab_block WHERE blocker_id=$1 AND blocked_id=$2', [auth.accountId, other.account_id]);
  return { status: 200, body: { result: 'unblocked', online_id: other.online_id } };
}

// GET /api/v1/blocks
async function blocks({ auth }) {
  const r = await db.query(
    `SELECT a.online_id, a.avatar, a.avatar_media, a.avatar_frames FROM lab_block b
       JOIN lab_account a ON a.account_id=b.blocked_id
      WHERE b.blocker_id=$1 ORDER BY b.created_at DESC`, [auth.accountId]);
  return { status: 200, body: { users: r.rows.map(rel.userCard) } };
}

// -------------------------------------------------------------- privacy --
// GET /api/v1/privacy
async function getPrivacy({ auth }) {
  return { status: 200, body: await rel.privacyOf(auth.accountId) };
}

// POST /api/v1/privacy  { messages?, friend_requests?, show_activity? }
async function setPrivacy({ req, auth }) {
  const body = await readJson(req);
  const cur = await rel.privacyOf(auth.accountId);
  const next = { ...cur };
  if (body.messages !== undefined) {
    if (!['everyone', 'friends'].includes(body.messages)) throw new HttpError(400, 'invalid_messages', 'messages: everyone|friends');
    next.messages = body.messages;
  }
  if (body.friend_requests !== undefined) {
    if (!['everyone', 'friends_of_friends', 'nobody'].includes(body.friend_requests)) {
      throw new HttpError(400, 'invalid_friend_requests', 'friend_requests: everyone|friends_of_friends|nobody');
    }
    next.friend_requests = body.friend_requests;
  }
  if (body.show_activity !== undefined) {
    if (typeof body.show_activity !== 'boolean') throw new HttpError(400, 'invalid_show_activity', 'show_activity: true|false');
    next.show_activity = body.show_activity;
  }
  await db.query(
    'UPDATE lab_account SET privacy_messages=$2, privacy_friend_requests=$3, show_activity=$4 WHERE account_id=$1',
    [auth.accountId, next.messages, next.friend_requests, next.show_activity]);
  return { status: 200, body: next };
}

// --------------------------------------------------------- segnalazioni --
// POST /api/v1/users/:onlineId/report  { reason, note? }
async function reportUser({ req, auth, params }) {
  const r = await db.query('SELECT account_id, online_id, about_me, status_message FROM lab_account WHERE lower(online_id)=lower($1)', [String(params.onlineId || '')]);
  const u = r.rows[0];
  if (!u) throw new HttpError(404, 'account_not_found');
  const body = await readJson(req);
  const snapshot = [u.about_me, u.status_message].filter(Boolean).join(' · ') || null;
  return fileReport({ auth, body, target: { type: 'user', id: u.account_id, ownerId: u.account_id, title: u.online_id, snapshot } });
}

// --------------------------------------------------------- suggerimenti --
// GET /api/v1/friends/suggestions — amici di amici (per numero di amici in
// comune), poi chi ha giocato agli stessi giochi. Esclusi: io, gli amici, le
// richieste in sospeso, i blocchi, gli account sospesi e chi non accetta richieste.
async function suggestions({ auth }) {
  const me = auth.accountId;
  const EXCL = `(SELECT $1::bigint
                 UNION ${rel.FRIENDS_SQL('$1')}
                 UNION SELECT addressee_id FROM lab_friendship WHERE requester_id=$1
                 UNION SELECT requester_id FROM lab_friendship WHERE addressee_id=$1
                 UNION ${rel.BLOCKED_SQL('$1')})`;
  const fof = await db.query(
    `WITH mine AS ${rel.FRIENDS_SQL('$1')},
          cand AS (
            SELECT CASE WHEN f.requester_id = m.id THEN f.addressee_id ELSE f.requester_id END AS id
              FROM mine m(id) JOIN lab_friendship f
                ON f.status='accepted' AND (f.requester_id=m.id OR f.addressee_id=m.id))
     SELECT a.account_id, a.online_id, a.avatar, a.avatar_media, a.avatar_frames, count(*)::int AS mutual
       FROM cand c JOIN lab_account a ON a.account_id=c.id
      WHERE c.id NOT IN ${EXCL}
        AND NOT a.disabled AND a.banned_at IS NULL AND a.password_hash IS NOT NULL
        AND a.privacy_friend_requests <> 'nobody'
      GROUP BY a.account_id ORDER BY mutual DESC, a.online_id LIMIT 15`, [me]);
  const users = fof.rows.map((u) => ({ ...rel.userCard(u), mutual: u.mutual, reason: 'amici_in_comune' }));
  if (users.length < 15) {
    const taken = fof.rows.map((u) => u.account_id);
    const games = await db.query(
      `SELECT a.account_id, a.online_id, a.avatar, a.avatar_media, a.avatar_frames,
              count(*)::int AS shared, sum(t.seconds)::bigint AS secs
         FROM lab_playtime t
         JOIN lab_account a ON a.account_id=t.account_id
        WHERE t.game_id IN (SELECT game_id FROM lab_playtime WHERE account_id=$1)
          AND t.account_id NOT IN ${EXCL}
          AND NOT (t.account_id = ANY($2::bigint[]))
          AND NOT a.disabled AND a.banned_at IS NULL AND a.password_hash IS NOT NULL
          AND a.privacy_friend_requests = 'everyone' AND a.show_activity
        GROUP BY a.account_id ORDER BY shared DESC, secs DESC, a.online_id LIMIT $3`, [me, taken, 15 - users.length]);
    for (const u of games.rows) users.push({ ...rel.userCard(u), mutual: 0, reason: 'stessi_giochi' });
  }
  return { status: 200, body: { users } };
}

// ------------------------------------------------------- inviti a giocare --
// POST /api/v1/invites  { to:[online_id] (amici, ≤ 16), game_id, game_name, text? (≤ 120) }
async function invite({ req, auth }) {
  if (limiter.hit(`invite|${auth.accountId}`, 20, 3600)) throw new HttpError(429, 'too_many_invites');
  const body = await readJson(req);
  const to = Array.isArray(body.to) ? [...new Set(body.to.map((x) => String(x || '').trim()).filter(Boolean))] : [];
  if (!to.length || to.length > 16) throw new HttpError(400, 'invalid_recipients', 'da 1 a 16 amici');
  const gameId = String(body.game_id == null ? '' : body.game_id).trim().slice(0, 64);
  const gameName = oneLine(body.game_name).slice(0, 128);
  if (!gameId) throw new HttpError(400, 'game_required');
  let note = null;
  if (body.text != null) {
    const s = oneLine(body.text);
    if (charLength(s) > 120) throw new HttpError(400, 'text_too_long', 'massimo 120 caratteri');
    note = s || null;
  }
  // prima si controllano tutti, poi si invia: o tutti o nessuno
  const targets = [];
  for (const oid of to) {
    const u = await accountByOnlineId(oid);
    if (!u) throw new HttpError(404, 'account_not_found', oid);
    if (String(u.account_id) === String(auth.accountId)) throw new HttpError(400, 'cannot_invite_self');
    if (await rel.isBlocked(auth.accountId, u.account_id)) throw new HttpError(403, 'blocked', u.online_id);
    if (!(await rel.areFriends(auth.accountId, u.account_id))) throw new HttpError(403, 'not_friends', u.online_id);
    targets.push(u);
  }
  const name = gameName || gameId;
  for (const u of targets) {
    await notify(u.account_id, 'game_invite', {
      actorId: auth.accountId, title: (l) => messages.t(l, 'notify.game_invite', { actor: auth.onlineId }),
      body: note ? `${name} — ${note}` : name, ref: gameId });
  }
  return { status: 201, body: { result: 'invited', to: targets.map((u) => u.online_id) } };
}

module.exports = { setStatus, block, unblock, blocks, getPrivacy, setPrivacy, reportUser, suggestions, invite };
