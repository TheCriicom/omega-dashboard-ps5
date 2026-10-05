'use strict';
// La home dell'app: sync periodico (fa anche da heartbeat della presenza),
// notifiche, ricerca e profili utente, messaggi diretti, party con chat,
// notizie e la scheda di un gioco.
const db = require('../db');
const { HttpError, readJson } = require('../http');
const { notify, rememberLang } = require('../notify');
const messages = require('../messages');
const { effective } = require('./social');
const feeds = require('../feeds');
const sysmsg = require('../sysmsg');
const limiter = require('../ratelimit');
const rel = require('../relations');
const { multiline, oneLine, likePattern } = require('../text');
const playtime = require('../playtime');
const gameCards = require('../games');
const trophies = require('./trophies');
const voice = require('./voice');
const groups = require('./groups');
const stats = require('./stats');

const MAX_TEXT = 500;
const { accountByOnlineId } = rel;

function cleanText(v) {
  const s = multiline(v);
  if (!s) throw new HttpError(400, 'empty_text');
  return s.slice(0, MAX_TEXT);
}

// Amici accettati con la presenza come la vedono gli altri (stato
// personalizzato già applicato), in una sola query.
async function friendsWithPresence(accountId) {
  const r = await db.query(
    `SELECT a.account_id, a.online_id, a.avatar, a.about_me, a.avatar_media, a.avatar_frames,
            a.status_mode, a.status_message,
            p.status, p.game_id, p.game_name, p.started_at, p.last_seen
       FROM lab_friendship f
       JOIN lab_account a
         ON a.account_id = CASE WHEN f.requester_id=$1 THEN f.addressee_id ELSE f.requester_id END
       LEFT JOIN lab_presence p ON p.account_id = a.account_id
      WHERE f.status='accepted' AND (f.requester_id=$1 OR f.addressee_id=$1)`, [accountId]);
  const list = r.rows.map((row) => ({
    account_id: String(row.account_id), online_id: row.online_id, avatar: row.avatar,
    avatar_media: row.avatar_media, avatar_frames: row.avatar_frames,
    about_me: row.about_me, presence: rel.maskPresence(effective(row.last_seen ? row : null), row.status_mode),
    status_message: row.status_message || null,
  }));
  // chi gioca, poi chi è online, poi gli offline; in ordine alfabetico dentro ogni gruppo
  const rank = (f) => (f.presence.game_id ? 0 : f.presence.status !== 'offline' ? 1 : 2);
  list.sort((a, b) => rank(a) - rank(b) || a.online_id.localeCompare(b.online_id));
  return list;
}

async function relation(me, other) {
  if (String(me) === String(other)) return 'self';
  const r = await db.query(
    `SELECT requester_id, status FROM lab_friendship
      WHERE (requester_id=$1 AND addressee_id=$2) OR (requester_id=$2 AND addressee_id=$1)`, [me, other]);
  if (!r.rowCount) return 'none';
  const row = r.rows[0];
  if (row.status === 'accepted') return 'friend';
  if (row.status === 'pending') return String(row.requester_id) === String(me) ? 'outgoing' : 'incoming';
  return row.status;
}

// ------------------------------------------------------------------ party --
async function partyOf(accountId) {
  const r = await db.query(
    `SELECT p.party_id, p.name, p.owner_id, p.created_at
       FROM lab_party_member m JOIN lab_party p USING (party_id)
      WHERE m.account_id=$1 AND NOT p.closed`, [accountId]);
  return r.rows[0] || null;
}

async function partyState(accountId, withMessages = true, code = null) {
  const p = await partyOf(accountId);
  if (!p) return null;
  const members = (await db.query(
    `SELECT a.account_id, a.online_id, a.avatar, a.avatar_media, a.avatar_frames, a.status_mode, m.muted, m.joined_at,
            pr.status, pr.game_id, pr.game_name, pr.started_at, pr.last_seen
       FROM lab_party_member m
       JOIN lab_account a USING (account_id)
       LEFT JOIN lab_presence pr ON pr.account_id = a.account_id
      WHERE m.party_id=$1 ORDER BY m.joined_at`, [p.party_id])).rows.map((row) => ({
    online_id: row.online_id, avatar: row.avatar, avatar_media: row.avatar_media, avatar_frames: row.avatar_frames, muted: row.muted,
    owner: String(row.account_id) === String(p.owner_id),
    presence: String(row.account_id) === String(accountId)
      ? effective(row.last_seen ? row : null)
      : rel.maskPresence(effective(row.last_seen ? row : null), row.status_mode),
  }));
  const invited = (await db.query(
    `SELECT a.online_id, a.avatar FROM lab_party_invite i JOIN lab_account a ON a.account_id=i.to_id
      WHERE i.party_id=$1 ORDER BY i.created_at`, [p.party_id])).rows;
  const out = {
    party_id: String(p.party_id), name: p.name, created_at: p.created_at,
    owner: String(p.owner_id) === String(accountId), members, invited,
    talking: voice.talking(p.party_id),
  };
  if (withMessages) out.messages = await partyMessages(p.party_id, 0, code);
  return out;
}

async function partyMessages(partyId, after, code) {
  const r = await db.query(
    `SELECT * FROM (
       SELECT m.message_id, a.online_id AS sender, a.avatar, m.body, m.created_at
         FROM lab_message m JOIN lab_account a ON a.account_id=m.from_id
        WHERE m.party_id=$1 AND m.message_id > $2
        ORDER BY m.message_id DESC LIMIT 60) t
      ORDER BY message_id`, [partyId, Number(after) || 0]);
  // "· x si è unito al party": messaggio di sistema, nella lingua di chi legge
  return r.rows.map((m) => ({ ...m, body: m.body.startsWith('· ') ? sysmsg.translate(m.body, code) : m.body, message_id: String(m.message_id) }));
}

async function partyInvitesFor(accountId) {
  return (await db.query(
    `SELECT p.party_id::text, p.name, a.online_id AS from_online_id, a.avatar,
            (SELECT count(*)::int FROM lab_party_member WHERE party_id=p.party_id) AS members
       FROM lab_party_invite i
       JOIN lab_party p USING (party_id)
       JOIN lab_account a ON a.account_id=i.from_id
      WHERE i.to_id=$1 AND NOT p.closed ORDER BY i.created_at DESC`, [accountId])).rows;
}

async function systemPartyMessage(partyId, accountId, text) {
  await db.query(`INSERT INTO lab_message (from_id, party_id, body) VALUES ($1,$2,$3)`,
    [accountId, partyId, text]);
}

async function leaveParty(accountId, onlineId) {
  voice.forget(accountId);
  const p = await partyOf(accountId);
  if (!p) return false;
  await db.query('DELETE FROM lab_party_member WHERE party_id=$1 AND account_id=$2', [p.party_id, accountId]);
  const left = (await db.query('SELECT account_id FROM lab_party_member WHERE party_id=$1 ORDER BY joined_at', [p.party_id])).rows;
  if (!left.length) {
    await db.query('UPDATE lab_party SET closed=true WHERE party_id=$1', [p.party_id]);
    await db.query('DELETE FROM lab_party_invite WHERE party_id=$1', [p.party_id]);
  } else {
    if (String(p.owner_id) === String(accountId)) {
      await db.query('UPDATE lab_party SET owner_id=$2 WHERE party_id=$1', [p.party_id, left[0].account_id]);
    }
    await systemPartyMessage(p.party_id, accountId, `· ${onlineId} ha lasciato il party`);
  }
  return true;
}

// ------------------------------------------------------------------- sync --
// GET /api/v1/sync?since=<notification_id>
async function sync(ctx) {
  const { auth, url } = ctx;
  const since = Number(url.searchParams.get('since') || 0) || 0;
  // un tratto di gioco rimasto aperto da una console sparita si chiude qui
  await playtime.onHeartbeat(auth.accountId);
  // heartbeat: rinfresca la presenza senza cambiarne lo stato
  // (chi era stato spento dal giro periodico torna online; l'offline scelto resta)
  await db.query(
    `UPDATE lab_presence SET last_seen=now(), status=CASE WHEN swept THEN 'online' ELSE status END, swept=false
      WHERE account_id=$1 AND (status<>'offline' OR swept)`, [auth.accountId]);

  const me = (await db.query('SELECT online_id, avatar, about_me, avatar_media, avatar_frames, cover_media, cover_frames, status_mode, status_message, lang FROM lab_account WHERE account_id=$1', [auth.accountId])).rows[0];
  // lingua delle notifiche: si aggiorna solo se l'app ne chiede un'altra
  await rememberLang(auth.accountId, ctx, me.lang);
  const counts = (await db.query(
    `SELECT
       (SELECT count(*)::int FROM lab_notification WHERE account_id=$1 AND NOT read) AS unread_notifications,
       (SELECT count(*)::int FROM lab_message WHERE to_id=$1 AND read_at IS NULL) AS unread_messages,
       (SELECT count(*)::int FROM lab_friendship WHERE addressee_id=$1 AND status='pending') AS incoming_requests,
       (SELECT count(*)::int FROM lab_friendship WHERE requester_id=$1 AND status='pending') AS outgoing_requests,
       (SELECT coalesce(max(notification_id),0)::text FROM lab_notification WHERE account_id=$1) AS last_notification_id`,
    [auth.accountId])).rows[0];
  const fresh = since > 0
    ? (await db.query(
      `SELECT n.notification_id::text, n.type, n.title, n.body, n.ref, n.created_at, a.online_id AS actor, a.avatar
         FROM lab_notification n LEFT JOIN lab_account a ON a.account_id=n.actor_id
        WHERE n.account_id=$1 AND n.notification_id > $2
        ORDER BY n.notification_id LIMIT 10`, [auth.accountId, since])).rows
    : [];
  const party = await partyState(auth.accountId, false);
  if (party) {
    const last = (await db.query('SELECT coalesce(max(message_id),0)::text AS id FROM lab_message WHERE party_id=$1', [party.party_id])).rows[0];
    party.last_message_id = last.id;
  }
  return {
    status: 200,
    body: {
      me: {
        online_id: me.online_id, avatar: me.avatar, about_me: me.about_me, avatar_media: me.avatar_media, avatar_frames: me.avatar_frames, cover_media: me.cover_media, cover_frames: me.cover_frames,
        status_mode: me.status_mode, status_message: me.status_message, dnd: me.status_mode === 'dnd',
      },
      ...counts,
      unread_groups: await groups.unreadTotal(auth.accountId),
      notifications: fresh,
      friends: await friendsWithPresence(auth.accountId),
      party,
      party_invites: await partyInvitesFor(auth.accountId),
      server_time: new Date().toISOString(),
    },
  };
}

// ------------------------------------------------------------- notifiche --
// GET /api/v1/notifications
async function notifications({ auth }) {
  const r = await db.query(
    `SELECT n.notification_id::text, n.type, n.title, n.body, n.ref, n.read, n.created_at,
            a.online_id AS actor, a.avatar
       FROM lab_notification n LEFT JOIN lab_account a ON a.account_id=n.actor_id
      WHERE n.account_id=$1 ORDER BY n.notification_id DESC LIMIT 50`, [auth.accountId]);
  return { status: 200, body: { notifications: r.rows } };
}

// POST /api/v1/notifications/read  { id? }  (senza id = tutte)
async function notificationsRead({ req, auth }) {
  const body = await readJson(req);
  if (body.id) await db.query('UPDATE lab_notification SET read=true WHERE account_id=$1 AND notification_id=$2', [auth.accountId, Number(body.id) || 0]);
  else await db.query('UPDATE lab_notification SET read=true WHERE account_id=$1 AND NOT read', [auth.accountId]);
  return { status: 200, body: { result: 'ok' } };
}

// POST /api/v1/notifications/clear
async function notificationsClear({ auth }) {
  await db.query('DELETE FROM lab_notification WHERE account_id=$1', [auth.accountId]);
  return { status: 200, body: { result: 'ok' } };
}

// ---------------------------------------------------------- utenti/profili --
// GET /api/v1/users/search?q=
async function usersSearch({ auth, url }) {
  const q = String(url.searchParams.get('q') || '').trim().slice(0, 32);
  const r = q
    ? await db.query(
      `SELECT account_id, online_id, avatar, avatar_media, avatar_frames FROM lab_account
        WHERE NOT disabled AND account_id<>$1 AND online_id ILIKE $2
          AND account_id NOT IN ${rel.BLOCKED_SQL('$1')}
        ORDER BY (lower(online_id)=lower($3)) DESC, length(online_id), online_id LIMIT 20`,
      [auth.accountId, likePattern(q), q])
    : await db.query(
      `SELECT account_id, online_id, avatar, avatar_media, avatar_frames FROM lab_account
        WHERE NOT disabled AND account_id<>$1 AND account_id NOT IN ${rel.BLOCKED_SQL('$1')}
        ORDER BY last_login_at DESC NULLS LAST LIMIT 20`, [auth.accountId]);
  const users = [];
  for (const u of r.rows) {
    users.push({ online_id: u.online_id, avatar: u.avatar, avatar_media: u.avatar_media, avatar_frames: u.avatar_frames, relation: await relation(auth.accountId, u.account_id) });
  }
  return { status: 200, body: { users } };
}

// GET /api/v1/users/:onlineId
// Chi mi ha bloccato → 403 blocked. Chi ho bloccato io → scheda ridotta con
// blocked:true (per poterlo sbloccare). Attività, giochi e tempo di gioco solo
// ad amici e a me, e non se l'utente nasconde la propria attività.
async function userProfile({ auth, params }) {
  const u = await accountByOnlineId(params.onlineId);
  if (!u) throw new HttpError(404, 'account_not_found');
  const self = String(u.account_id) === String(auth.accountId);
  const bs = self ? { iBlocked: false, theyBlocked: false } : await rel.blockState(auth.accountId, u.account_id);
  if (bs.theyBlocked && !bs.iBlocked) throw new HttpError(403, 'blocked');
  const a = (await db.query(
    `SELECT online_id, avatar, about_me, created_at, avatar_media, avatar_frames, cover_media, cover_frames,
            status_mode, status_message, show_activity
       FROM lab_account WHERE account_id=$1`, [u.account_id])).rows[0];
  const rel0 = bs.iBlocked ? 'blocked' : await relation(auth.accountId, u.account_id);
  const pres = (await db.query('SELECT status, game_id, game_name, started_at, last_seen FROM lab_presence WHERE account_id=$1', [u.account_id])).rows[0];
  const friendsCount = (await db.query(
    `SELECT count(*)::int AS n FROM lab_friendship WHERE status='accepted' AND (requester_id=$1 OR addressee_id=$1)`, [u.account_id])).rows[0].n;
  const visible = rel0 === 'friend' || rel0 === 'self';
  const activityOk = visible && (self || a.show_activity);
  const recent = activityOk ? (await db.query(
    `SELECT type, game_id, game_name, detail, created_at FROM lab_activity
      WHERE account_id=$1 ORDER BY created_at DESC LIMIT 8`, [u.account_id])).rows : [];
  const games = activityOk ? (await db.query(
    `SELECT game_id, max(game_name) AS game_name, max(created_at) AS last_played, count(*)::int AS sessions
       FROM lab_activity WHERE account_id=$1 AND game_id IS NOT NULL
      GROUP BY game_id ORDER BY max(created_at) DESC LIMIT 6`, [u.account_id])).rows : [];
  await gameCards.decorate(games);
  let presence = self ? effective(pres) : rel.maskPresence(effective(pres), a.status_mode);
  if (!visible) presence = { status: presence.status };
  if (bs.iBlocked) presence = { status: 'offline' };
  const blockedView = bs.iBlocked;
  return {
    status: 200,
    body: {
      online_id: a.online_id, avatar: a.avatar, about_me: a.about_me, created_at: a.created_at,
      avatar_media: a.avatar_media, avatar_frames: a.avatar_frames, cover_media: a.cover_media, cover_frames: a.cover_frames,
      relation: rel0, friends_count: friendsCount,
      presence, recent, games,
      status_message: blockedView ? null : (a.status_message || null),
      blocked: bs.iBlocked,
      mutual_friends: self || blockedView ? 0 : await rel.mutualCount(auth.accountId, u.account_id),
      mutual: self || blockedView ? [] : await rel.mutualFriends(auth.accountId, u.account_id, 5),
      stats: activityOk ? await stats.summaryOf(u.account_id) : { total_seconds: 0, top_games: [], hidden: true },
      trophies: blockedView ? { hidden: true } : await trophies.summaryFor(auth.accountId, u.account_id),
    },
  };
}

// POST /api/v1/profile  { about_me?, avatar? }
async function profileUpdate({ req, auth }) {
  const body = await readJson(req);
  if (body.about_me !== undefined) {
    const about = oneLine(body.about_me || '').slice(0, 160);
    await db.query('UPDATE lab_account SET about_me=$2 WHERE account_id=$1', [auth.accountId, about || null]);
  }
  if (body.avatar !== undefined) {
    const av = Math.max(0, Math.min(47, Number.parseInt(body.avatar, 10) || 0));
    await db.query('UPDATE lab_account SET avatar=$2 WHERE account_id=$1', [auth.accountId, av]);
  }
  return { status: 200, body: { result: 'ok' } };
}

// --------------------------------------------------------------- messaggi --
// GET /api/v1/messages — conversazioni (ultimo messaggio + non letti).
async function conversations({ auth }) {
  const r = await db.query(
    `SELECT DISTINCT ON (other) other, m.message_id, m.body, m.created_at, m.from_id
       FROM (SELECT CASE WHEN from_id=$1 THEN to_id ELSE from_id END AS other, *
               FROM lab_message WHERE to_id IS NOT NULL AND (from_id=$1 OR to_id=$1)) m
      ORDER BY other, m.message_id DESC`, [auth.accountId]);
  const out = [];
  for (const row of r.rows) {
    const a = (await db.query('SELECT online_id, avatar, avatar_media, avatar_frames FROM lab_account WHERE account_id=$1', [row.other])).rows[0];
    if (!a) continue;
    const unread = (await db.query(
      'SELECT count(*)::int AS n FROM lab_message WHERE from_id=$1 AND to_id=$2 AND read_at IS NULL', [row.other, auth.accountId])).rows[0].n;
    out.push({
      online_id: a.online_id, avatar: a.avatar, avatar_media: a.avatar_media, avatar_frames: a.avatar_frames, last_body: row.body, last_at: row.created_at,
      last_from_me: String(row.from_id) === String(auth.accountId), unread,
    });
  }
  out.sort((x, y) => new Date(y.last_at) - new Date(x.last_at));
  return { status: 200, body: { conversations: out } };
}

// GET /api/v1/messages/:onlineId?after=<id> — thread (e lo segna come letto).
async function thread({ auth, params, url }) {
  const other = await accountByOnlineId(params.onlineId);
  if (!other) throw new HttpError(404, 'account_not_found');
  const after = Number(url.searchParams.get('after') || 0) || 0;
  const r = await db.query(
    `SELECT * FROM (
       SELECT m.message_id, (m.from_id=$1) AS mine, m.body, m.created_at, m.read_at
         FROM lab_message m
        WHERE m.to_id IS NOT NULL AND m.message_id > $3
          AND ((m.from_id=$1 AND m.to_id=$2) OR (m.from_id=$2 AND m.to_id=$1))
        ORDER BY m.message_id DESC LIMIT 60) t ORDER BY message_id`,
    [auth.accountId, other.account_id, after]);
  await db.query('UPDATE lab_message SET read_at=now() WHERE from_id=$1 AND to_id=$2 AND read_at IS NULL', [other.account_id, auth.accountId]);
  await db.query(`UPDATE lab_notification SET read=true WHERE account_id=$1 AND type='message' AND ref=$2 AND NOT read`,
    [auth.accountId, other.online_id]);
  return {
    status: 200,
    body: { online_id: other.online_id, messages: r.rows.map((m) => ({ ...m, message_id: String(m.message_id) })) },
  };
}

// POST /api/v1/messages/:onlineId  { text }
async function sendMessage({ req, auth, params }) {
  const other = await accountByOnlineId(params.onlineId);
  if (!other) throw new HttpError(404, 'account_not_found');
  if (String(other.account_id) === String(auth.accountId)) throw new HttpError(400, 'cannot_message_self');
  if (await rel.isBlocked(auth.accountId, other.account_id)) throw new HttpError(403, 'blocked');
  // privacy di chi riceve: "everyone" accetta anche chi non è amico (con un limite), "friends" no
  if (!(await rel.areFriends(auth.accountId, other.account_id))) {
    if ((await rel.privacyOf(other.account_id)).messages !== 'everyone') throw new HttpError(403, 'privacy');
    if (limiter.hit(`dm_stranger|${auth.accountId}`, 30, 3600)) throw new HttpError(429, 'too_many_messages');
  }
  const body = await readJson(req);
  const text = cleanText(body.text);
  const r = await db.query(
    `INSERT INTO lab_message (from_id, to_id, body) VALUES ($1,$2,$3) RETURNING message_id::text, created_at`,
    [auth.accountId, other.account_id, text]);
  await notify(other.account_id, 'message', { actorId: auth.accountId, title: auth.onlineId, body: text, ref: auth.onlineId });
  return { status: 201, body: { message_id: r.rows[0].message_id, created_at: r.rows[0].created_at } };
}

// ---------------------------------------------------------------- party --
// GET /api/v1/party
async function party({ auth, lang, langExplicit }) {
  return { status: 200, body: { party: await partyState(auth.accountId, true, langExplicit ? lang : null), invites: await partyInvitesFor(auth.accountId) } };
}

// POST /api/v1/party  { name? }  — crea un party (lascia quello attuale).
async function partyCreate({ req, auth, lang, langExplicit }) {
  const body = await readJson(req);
  const name = String(body.name || '').trim().slice(0, 40) || `Party di ${auth.onlineId}`;
  await leaveParty(auth.accountId, auth.onlineId);
  const p = (await db.query('INSERT INTO lab_party (owner_id, name) VALUES ($1,$2) RETURNING party_id', [auth.accountId, name])).rows[0];
  await db.query('INSERT INTO lab_party_member (party_id, account_id) VALUES ($1,$2)', [p.party_id, auth.accountId]);
  voice.forget(auth.accountId);
  await systemPartyMessage(p.party_id, auth.accountId, `· ${auth.onlineId} ha creato il party`);
  return { status: 201, body: { party: await partyState(auth.accountId, true, langExplicit ? lang : null) } };
}

// POST /api/v1/party/invite  { online_id }
async function partyInvite({ req, auth }) {
  const body = await readJson(req);
  const p = await partyOf(auth.accountId);
  if (!p) throw new HttpError(409, 'not_in_party');
  const other = await accountByOnlineId(body.online_id);
  if (!other) throw new HttpError(404, 'account_not_found');
  if (await rel.isBlocked(auth.accountId, other.account_id)) throw new HttpError(403, 'blocked');
  if (!(await rel.areFriends(auth.accountId, other.account_id))) throw new HttpError(403, 'not_friends');
  const inside = await db.query('SELECT 1 FROM lab_party_member WHERE party_id=$1 AND account_id=$2', [p.party_id, other.account_id]);
  if (inside.rowCount) return { status: 200, body: { result: 'already_member' } };
  await db.query(
    `INSERT INTO lab_party_invite (party_id, from_id, to_id) VALUES ($1,$2,$3)
     ON CONFLICT (party_id, to_id) DO UPDATE SET created_at=now(), from_id=EXCLUDED.from_id`,
    [p.party_id, auth.accountId, other.account_id]);
  await notify(other.account_id, 'party_invite', {
    actorId: auth.accountId, title: (l) => messages.t(l, 'notify.party_invite', { actor: auth.onlineId }), body: p.name, ref: String(p.party_id) });
  return { status: 201, body: { result: 'invited', online_id: other.online_id } };
}

// POST /api/v1/party/join  { party_id }
async function partyJoin({ req, auth, lang, langExplicit }) {
  const body = await readJson(req);
  const partyId = Number(body.party_id) || 0;
  const inv = await db.query(
    `SELECT p.party_id FROM lab_party_invite i JOIN lab_party p USING (party_id)
      WHERE i.party_id=$1 AND i.to_id=$2 AND NOT p.closed`, [partyId, auth.accountId]);
  if (!inv.rowCount) throw new HttpError(404, 'no_invite');
  await leaveParty(auth.accountId, auth.onlineId);
  await db.query('INSERT INTO lab_party_member (party_id, account_id) VALUES ($1,$2)', [partyId, auth.accountId]);
  await db.query('DELETE FROM lab_party_invite WHERE party_id=$1 AND to_id=$2', [partyId, auth.accountId]);
  voice.forget(auth.accountId);
  await systemPartyMessage(partyId, auth.accountId, `· ${auth.onlineId} si è unito al party`);
  return { status: 200, body: { party: await partyState(auth.accountId, true, langExplicit ? lang : null) } };
}

// POST /api/v1/party/decline  { party_id }
async function partyDecline({ req, auth }) {
  const body = await readJson(req);
  await db.query('DELETE FROM lab_party_invite WHERE party_id=$1 AND to_id=$2', [Number(body.party_id) || 0, auth.accountId]);
  return { status: 200, body: { result: 'declined' } };
}

// POST /api/v1/party/leave
async function partyLeave({ auth }) {
  const left = await leaveParty(auth.accountId, auth.onlineId);
  return { status: 200, body: { result: left ? 'left' : 'not_in_party' } };
}

// POST /api/v1/party/mute  { muted }
async function partyMute({ req, auth }) {
  const body = await readJson(req);
  await db.query('UPDATE lab_party_member SET muted=$2 WHERE account_id=$1', [auth.accountId, !!body.muted]);
  voice.forget(auth.accountId);
  return { status: 200, body: { result: 'ok', muted: !!body.muted } };
}

// GET /api/v1/party/messages?after=<id>
async function partyMessagesGet({ auth, url, lang, langExplicit }) {
  const p = await partyOf(auth.accountId);
  if (!p) throw new HttpError(409, 'not_in_party');
  return { status: 200, body: { messages: await partyMessages(p.party_id, url.searchParams.get('after'), langExplicit ? lang : null) } };
}

// POST /api/v1/party/messages  { text }
async function partyMessagesPost({ req, auth }) {
  const p = await partyOf(auth.accountId);
  if (!p) throw new HttpError(409, 'not_in_party');
  const body = await readJson(req);
  const r = await db.query(
    `INSERT INTO lab_message (from_id, party_id, body) VALUES ($1,$2,$3) RETURNING message_id::text`,
    [auth.accountId, p.party_id, cleanText(body.text)]);
  return { status: 201, body: { message_id: r.rows[0].message_id } };
}

// ------------------------------------------------------------ news/giochi --
// GET /api/v1/news?game_id=
// Prima le notizie nella lingua della richiesta (e in quelle sorelle), poi
// l'inglese; le notizie scritte a mano (lang NULL) valgono per tutti.
const NEWS_PREF = 'COALESCE(array_position($1::text[], lang), 1)';
async function news({ url, lang }) {
  const langs = feeds.langsFor(lang);
  const game = url.searchParams.get('game_id');
  const r = game
    ? await db.query(
      `SELECT news_id::text, title, body, tag, game_id, color, created_at, source, link, (image_url IS NOT NULL) AS has_image FROM lab_news WHERE (lang = ANY($1) OR lang IS NULL) AND (game_id=$2 OR game_id IS NULL)
        ORDER BY (game_id IS NULL), ${NEWS_PREF}, created_at DESC LIMIT 20`, [langs, game])
    : await db.query(
      `SELECT news_id::text, title, body, tag, game_id, color, created_at, source, link, (image_url IS NOT NULL) AS has_image FROM lab_news WHERE lang = ANY($1) OR lang IS NULL ORDER BY ${NEWS_PREF}, created_at DESC LIMIT 30`, [langs]);
  return { status: 200, body: { news: r.rows } };
}

// GET /api/v1/news/:newsId/image
async function newsImage({ params, res }) {
  const img = await feeds.imageFor(Number(params.newsId) || 0);
  if (!img) throw new HttpError(404, 'no_image');
  res.writeHead(200, { 'content-type': img.type, 'content-length': img.buf.length, 'cache-control': 'max-age=86400' });
  res.end(img.buf);
  return { sent: true };
}

// GET /api/v1/games/:gameId — amici che ci giocano ora, chi ci ha giocato, news.
async function game({ auth, params, url, lang }) {
  const langs = feeds.langsFor(lang);
  const gameId = String(params.gameId).slice(0, 64);
  const friends = await friendsWithPresence(auth.accountId);
  const playingNow = friends.filter((f) => f.presence.game_id === gameId)
    .map((f) => ({ online_id: f.online_id, avatar: f.avatar, started_at: f.presence.started_at }));
  const ids = friends.map((f) => f.account_id);
  const played = ids.length ? (await db.query(
    `SELECT a.online_id, a.avatar, max(v.created_at) AS last_played
       FROM lab_activity v JOIN lab_account a ON a.account_id=v.account_id
      WHERE v.game_id=$1 AND v.account_id = ANY($2::bigint[]) AND a.show_activity
      GROUP BY a.online_id, a.avatar ORDER BY max(v.created_at) DESC LIMIT 12`, [gameId, ids])).rows : [];
  const total = (await db.query(
    `SELECT count(*)::int AS n FROM lab_presence
      WHERE game_id=$1 AND status<>'offline' AND last_seen > now() - interval '120 seconds'`, [gameId])).rows[0].n;
  // notizie con il nome del gioco (o le sue prime due parole) nel titolo
  const name = String(url.searchParams.get('name') || '').replace(/[™®©:]/g, ' ').replace(/\s+/g, ' ').trim();
  let n = [];
  if (name.length >= 3) {
    const words = name.split(' ').filter((w) => w.length > 1);
    const short = words.slice(0, Math.min(words.length, 2)).join(' ');
    n = (await db.query(
      `SELECT news_id::text, title, body, tag, game_id, color, created_at, source, link, (image_url IS NOT NULL) AS has_image FROM lab_news
        WHERE (lang = ANY($1) OR lang IS NULL) AND (game_id=$2 OR title ILIKE $3 OR title ILIKE $4)
        ORDER BY (title ILIKE $3) DESC, ${NEWS_PREF}, created_at DESC LIMIT 6`,
      [langs, gameId, `%${name.replace(/[%_\\]/g, '')}%`, `%${short.replace(/[%_\\]/g, '')}%`])).rows;
  }
  if (n.length < 3) {
    const more = (await db.query(
      `SELECT news_id::text, title, body, tag, game_id, color, created_at, source, link, (image_url IS NOT NULL) AS has_image FROM lab_news
        WHERE ext_id IS NOT NULL AND lang = ANY($1) ORDER BY ${NEWS_PREF}, created_at DESC LIMIT $2`, [langs, 6 - n.length])).rows;
    n = n.concat(more.filter((m) => !n.some((x) => x.news_id === m.news_id)));
  }
  return {
    status: 200,
    body: { game_id: gameId, playing_now: playingNow, played, players_now: total, news: n },
  };
}

module.exports = {
  sync, notifications, notificationsRead, notificationsClear,
  usersSearch, userProfile, profileUpdate,
  conversations, thread, sendMessage,
  party, partyCreate, partyInvite, partyJoin, partyDecline, partyLeave, partyMute,
  partyMessagesGet, partyMessagesPost,
  news, newsImage, game,
};
