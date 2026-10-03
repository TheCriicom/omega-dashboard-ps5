'use strict';
// Chat di gruppo persistenti, fino a 32 persone. Chi crea il gruppo ne è il
// proprietario (rinomina, toglie membri); tutti possono aggiungere i propri
// amici. Se il proprietario esce passa al membro più vecchio; quando esce
// l'ultimo il gruppo si cancella.
//
// I messaggi di gruppo non generano notifiche: contano solo in unread_groups
// del sync. Quelli di chi è in un blocco con me non li vedo e non li conto.
const db = require('../db');
const { HttpError, retryLater, readJson } = require('../http');
const limiter = require('../ratelimit');
const rel = require('../relations');
const { multiline, oneLine, charLength } = require('../text');

const MAX_MEMBERS = 32;
const { accountByOnlineId } = rel;

function cleanName(v) {
  const s = oneLine(v);
  if (!s || charLength(s) > 40) throw new HttpError(400, 'invalid_name', 'nome da 1 a 40 caratteri');
  return s;
}

function cleanText(v) {
  const s = multiline(v);
  if (!s) throw new HttpError(400, 'empty_text');
  if (charLength(s) > 500) throw new HttpError(400, 'text_too_long', 'massimo 500 caratteri');
  return s;
}

async function systemMessage(groupId, actorId, text) {
  await db.query('INSERT INTO lab_group_message (group_id, from_id, body, system) VALUES ($1,$2,$3,true)', [groupId, actorId, text]);
  await db.query('UPDATE lab_group SET last_activity=now() WHERE group_id=$1', [groupId]);
}

// Il gruppo, se ne faccio parte (altrimenti 404/403). Se il proprietario non
// esiste più (account cancellato) subentra il membro più vecchio.
async function groupFor(accountId, groupId) {
  const g = (await db.query('SELECT group_id, name, owner_id FROM lab_group WHERE group_id=$1', [Number(groupId) || 0])).rows[0];
  if (!g) throw new HttpError(404, 'group_not_found');
  const m = await db.query('SELECT 1 FROM lab_group_member WHERE group_id=$1 AND account_id=$2', [g.group_id, accountId]);
  if (!m.rowCount) throw new HttpError(403, 'not_member');
  if (!g.owner_id) {
    const first = (await db.query('SELECT account_id FROM lab_group_member WHERE group_id=$1 ORDER BY joined_at, account_id LIMIT 1', [g.group_id])).rows[0];
    if (first) { g.owner_id = first.account_id; await db.query('UPDATE lab_group SET owner_id=$2 WHERE group_id=$1', [g.group_id, first.account_id]); }
  }
  return g;
}

// Gli account da aggiungere: devono essere miei amici e senza blocchi.
async function resolveFriends(me, list) {
  const out = [];
  const seen = new Set();
  for (const raw of list) {
    const oid = String(raw || '').trim();
    if (!oid) continue;
    const u = await accountByOnlineId(oid);
    if (!u) throw new HttpError(404, 'account_not_found', oid);
    if (String(u.account_id) === String(me) || seen.has(String(u.account_id))) continue;
    if (await rel.isBlocked(me, u.account_id)) throw new HttpError(403, 'blocked', u.online_id);
    if (!(await rel.areFriends(me, u.account_id))) throw new HttpError(403, 'not_friends', u.online_id);
    seen.add(String(u.account_id));
    out.push(u);
  }
  return out;
}

// Scheda del gruppo vista da me: stessa forma in elenco e nelle risposte.
async function groupCard(me, groupId) {
  const g = (await db.query(
    `SELECT g.group_id, g.name, o.online_id AS owner,
            (SELECT last_read_id FROM lab_group_member WHERE group_id=g.group_id AND account_id=$1) AS last_read
       FROM lab_group g LEFT JOIN lab_account o ON o.account_id=g.owner_id WHERE g.group_id=$2`, [me, groupId])).rows[0];
  if (!g) return null;
  const members = (await db.query(
    `SELECT a.online_id, a.avatar, a.avatar_media, a.avatar_frames FROM lab_group_member m
       JOIN lab_account a USING (account_id) WHERE m.group_id=$1 ORDER BY m.joined_at, m.account_id`, [groupId])).rows;
  const last = (await db.query(
    `SELECT a.online_id AS from, m.body AS text, m.created_at FROM lab_group_message m
       LEFT JOIN lab_account a ON a.account_id=m.from_id
      WHERE m.group_id=$2 AND (m.from_id IS NULL OR m.from_id NOT IN ${rel.BLOCKED_SQL('$1')})
      ORDER BY m.message_id DESC LIMIT 1`, [me, groupId])).rows[0] || null;
  const unread = (await db.query(
    `SELECT count(*)::int AS n FROM lab_group_message m
      WHERE m.group_id=$2 AND m.message_id > $3 AND m.from_id IS DISTINCT FROM $1
        AND (m.from_id IS NULL OR m.from_id NOT IN ${rel.BLOCKED_SQL('$1')})`, [me, groupId, g.last_read || 0])).rows[0].n;
  return {
    group_id: String(g.group_id), name: g.name, owner: g.owner || (members[0] && members[0].online_id) || null,
    members: members.map(rel.userCard), member_count: members.length,
    last_message: last ? { from: last.from, text: last.text, created_at: last.created_at } : null,
    unread,
  };
}

// Messaggi di gruppo non letti, per il sync.
async function unreadTotal(me) {
  const r = await db.query(
    `SELECT count(*)::int AS n FROM lab_group_member gm
       JOIN lab_group_message m ON m.group_id=gm.group_id AND m.message_id > gm.last_read_id
      WHERE gm.account_id=$1 AND m.from_id IS DISTINCT FROM $1
        AND (m.from_id IS NULL OR m.from_id NOT IN ${rel.BLOCKED_SQL('$1')})`, [me]);
  return r.rows[0].n;
}

// POST /api/v1/groups  { name, members:[online_id] }
async function create({ req, auth }) {
  if (limiter.hit(`group_create|${auth.accountId}`, 20, 3600)) throw new HttpError(429, 'too_many_groups');
  const body = await readJson(req);
  const name = cleanName(body.name);
  const list = Array.isArray(body.members) ? body.members : [];
  const others = await resolveFriends(auth.accountId, list);
  if (others.length < 1 || others.length > MAX_MEMBERS - 1) throw new HttpError(400, 'invalid_members', `da 1 a ${MAX_MEMBERS - 1} amici`);
  const g = (await db.query('INSERT INTO lab_group (name, owner_id) VALUES ($1,$2) RETURNING group_id', [name, auth.accountId])).rows[0];
  await db.query('INSERT INTO lab_group_member (group_id, account_id) VALUES ($1,$2)', [g.group_id, auth.accountId]);
  for (const u of others) await db.query('INSERT INTO lab_group_member (group_id, account_id) VALUES ($1,$2)', [g.group_id, u.account_id]);
  await systemMessage(g.group_id, auth.accountId, `${auth.onlineId} ha creato il gruppo «${name}»`);
  await markRead(auth.accountId, g.group_id);
  return { status: 201, body: { group: await groupCard(auth.accountId, g.group_id) } };
}

// GET /api/v1/groups
async function list({ auth }) {
  const r = await db.query(
    `SELECT g.group_id FROM lab_group_member m JOIN lab_group g USING (group_id)
      WHERE m.account_id=$1 ORDER BY g.last_activity DESC, g.group_id DESC LIMIT 100`, [auth.accountId]);
  const groups = [];
  for (const row of r.rows) {
    const c = await groupCard(auth.accountId, row.group_id);
    if (c) groups.push(c);
  }
  return { status: 200, body: { groups } };
}

// GET /api/v1/groups/:id
async function get({ auth, params }) {
  const g = await groupFor(auth.accountId, params.id);
  return { status: 200, body: { group: await groupCard(auth.accountId, g.group_id) } };
}

async function markRead(me, groupId) {
  await db.query(
    `UPDATE lab_group_member SET last_read_id = greatest(last_read_id,
       (SELECT coalesce(max(message_id),0) FROM lab_group_message WHERE group_id=$1))
      WHERE group_id=$1 AND account_id=$2`, [groupId, me]);
}

function messageOut(m, me) {
  return {
    message_id: String(m.message_id),
    from: m.online_id ? rel.userCard(m) : null,
    text: m.body, created_at: m.created_at,
    mine: String(m.from_id) === String(me), system: m.system,
  };
}

// GET /api/v1/groups/:id/messages?after=<message_id>&limit=50  (segna come letti)
async function messages({ auth, params, url }) {
  const g = await groupFor(auth.accountId, params.id);
  const after = Number.parseInt(url.searchParams.get('after') || '0', 10) || 0;
  const limit = Math.min(200, Math.max(1, Number.parseInt(url.searchParams.get('limit') || '50', 10) || 50));
  // dopo un cursore: i successivi in ordine; senza: gli ultimi `limit`
  const r = await db.query(
    `SELECT * FROM (
       SELECT m.message_id, m.from_id, m.body, m.system, m.created_at, a.online_id, a.avatar, a.avatar_media, a.avatar_frames
         FROM lab_group_message m LEFT JOIN lab_account a ON a.account_id=m.from_id
        WHERE m.group_id=$2 AND m.message_id > $3
          AND (m.from_id IS NULL OR m.from_id NOT IN ${rel.BLOCKED_SQL('$1')})
        ORDER BY CASE WHEN $3 > 0 THEN m.message_id END ASC, m.message_id DESC
        LIMIT $4) t ORDER BY message_id`, [auth.accountId, g.group_id, after, limit]);
  await markRead(auth.accountId, g.group_id);
  return { status: 200, body: { messages: r.rows.map((m) => messageOut(m, auth.accountId)) } };
}

// POST /api/v1/groups/:id/messages  { text }
async function send({ req, auth, params }) {
  const wait = limiter.hit(`group_msg|${auth.accountId}`, 30, 60);
  if (wait) throw retryLater('too_many_messages', wait);
  const g = await groupFor(auth.accountId, params.id);
  const body = await readJson(req);
  const t = cleanText(body.text);
  const m = (await db.query(
    'INSERT INTO lab_group_message (group_id, from_id, body) VALUES ($1,$2,$3) RETURNING message_id, created_at',
    [g.group_id, auth.accountId, t])).rows[0];
  await db.query('UPDATE lab_group SET last_activity=now() WHERE group_id=$1', [g.group_id]);
  await markRead(auth.accountId, g.group_id);
  const me = (await db.query('SELECT online_id, avatar, avatar_media, avatar_frames FROM lab_account WHERE account_id=$1', [auth.accountId])).rows[0];
  const message = messageOut({ ...me, message_id: m.message_id, from_id: auth.accountId, body: t, system: false, created_at: m.created_at }, auth.accountId);
  return { status: 201, body: { message_id: message.message_id, created_at: m.created_at, message } };
}

// POST /api/v1/groups/:id/members  { add:[online_id] }
async function addMembers({ req, auth, params }) {
  const g = await groupFor(auth.accountId, params.id);
  const body = await readJson(req);
  const list = Array.isArray(body.add) ? body.add : [];
  if (!list.length) throw new HttpError(400, 'invalid_members');
  const users = await resolveFriends(auth.accountId, list);
  const current = new Set((await db.query('SELECT account_id FROM lab_group_member WHERE group_id=$1', [g.group_id])).rows.map((x) => String(x.account_id)));
  const fresh = users.filter((u) => !current.has(String(u.account_id)));
  if (current.size + fresh.length > MAX_MEMBERS) throw new HttpError(409, 'group_full', `massimo ${MAX_MEMBERS} membri`);
  for (const u of fresh) {
    await db.query('INSERT INTO lab_group_member (group_id, account_id) VALUES ($1,$2) ON CONFLICT DO NOTHING', [g.group_id, u.account_id]);
    await systemMessage(g.group_id, auth.accountId, `${auth.onlineId} ha aggiunto ${u.online_id}`);
  }
  return { status: 200, body: { result: 'ok', added: fresh.map((u) => u.online_id), group: await groupCard(auth.accountId, g.group_id) } };
}

async function leaveGroup(groupId, accountId, onlineId, ownerId) {
  await db.query('DELETE FROM lab_group_member WHERE group_id=$1 AND account_id=$2', [groupId, accountId]);
  const left = (await db.query('SELECT account_id FROM lab_group_member WHERE group_id=$1 ORDER BY joined_at, account_id', [groupId])).rows;
  if (!left.length) {
    await db.query('DELETE FROM lab_group WHERE group_id=$1', [groupId]);
    return 'deleted';
  }
  if (String(ownerId) === String(accountId)) await db.query('UPDATE lab_group SET owner_id=$2 WHERE group_id=$1', [groupId, left[0].account_id]);
  await systemMessage(groupId, accountId, `${onlineId} è uscito dal gruppo`);
  return 'left';
}

// POST /api/v1/groups/:id/leave
async function leave({ auth, params }) {
  const g = await groupFor(auth.accountId, params.id);
  const result = await leaveGroup(g.group_id, auth.accountId, auth.onlineId, g.owner_id);
  return { status: 200, body: { result } };
}

// POST /api/v1/groups/:id  { name }  (solo il proprietario)
async function rename({ req, auth, params }) {
  const g = await groupFor(auth.accountId, params.id);
  if (String(g.owner_id) !== String(auth.accountId)) throw new HttpError(403, 'not_owner');
  const body = await readJson(req);
  const name = cleanName(body.name);
  await db.query('UPDATE lab_group SET name=$2 WHERE group_id=$1', [g.group_id, name]);
  await systemMessage(g.group_id, auth.accountId, `${auth.onlineId} ha rinominato il gruppo in «${name}»`);
  return { status: 200, body: { group: await groupCard(auth.accountId, g.group_id) } };
}

// DELETE /api/v1/groups/:id/members/:onlineId  (solo il proprietario; se stesso = esce)
async function removeMember({ auth, params }) {
  const g = await groupFor(auth.accountId, params.id);
  const u = (await db.query('SELECT account_id, online_id FROM lab_account WHERE lower(online_id)=lower($1)', [String(params.onlineId || '')])).rows[0];
  if (!u) throw new HttpError(404, 'account_not_found');
  if (String(u.account_id) === String(auth.accountId)) {
    return { status: 200, body: { result: await leaveGroup(g.group_id, auth.accountId, auth.onlineId, g.owner_id) } };
  }
  if (String(g.owner_id) !== String(auth.accountId)) throw new HttpError(403, 'not_owner');
  const r = await db.query('DELETE FROM lab_group_member WHERE group_id=$1 AND account_id=$2', [g.group_id, u.account_id]);
  if (!r.rowCount) throw new HttpError(404, 'not_member');
  await systemMessage(g.group_id, auth.accountId, `${auth.onlineId} ha rimosso ${u.online_id}`);
  return { status: 200, body: { result: 'removed', group: await groupCard(auth.accountId, g.group_id) } };
}

module.exports = { create, list, get, messages, send, addMembers, leave, rename, removeMember, unreadTotal };
