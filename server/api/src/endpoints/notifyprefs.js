'use strict';
// GET/POST /api/v1/notifications/prefs e preferiti/silenziati per amico.
const db = require('../db');
const { HttpError, readJson } = require('../http');
const { normalize } = require('../notifyprefs');
const { userCard, accountByOnlineId, areFriends, FRIENDS_SQL } = require('../relations');

async function out(accountId) {
  const r = (await db.query('SELECT notify_prefs FROM lab_account WHERE account_id=$1', [accountId])).rows[0];
  const prefs = normalize(r && r.notify_prefs);
  const fr = (await db.query(
    `SELECT acc.online_id, acc.avatar, acc.avatar_media, acc.avatar_frames, coalesce(f.favorite,false) AS favorite, coalesce(f.muted,false) AS muted
       FROM lab_account acc LEFT JOIN lab_notify_friend f ON f.account_id=$1 AND f.friend_id=acc.account_id
      WHERE acc.account_id IN ${FRIENDS_SQL('$1')} AND NOT acc.disabled ORDER BY lower(acc.online_id)`, [accountId])).rows;
  return { ...prefs, friends: fr.map((x) => ({ ...userCard(x), favorite: x.favorite, muted: x.muted })) };
}

async function get({ auth }) { return { status: 200, body: await out(auth.accountId) }; }

// POST {types?:{tipo:modo}, in_game?, quiet?:{enabled,from,to}, tz_offset?}: si fondono con quelle salvate
async function set({ req, auth }) {
  const b = await readJson(req);
  const cur = (await db.query('SELECT notify_prefs FROM lab_account WHERE account_id=$1', [auth.accountId])).rows[0];
  const base = normalize(cur && cur.notify_prefs);
  const merged = normalize({
    types: { ...base.types, ...(b.types && typeof b.types === 'object' ? b.types : {}) },
    in_game: b.in_game || base.in_game,
    quiet: { ...base.quiet, ...(b.quiet && typeof b.quiet === 'object' ? b.quiet : {}) },
    tz_offset: Number.isFinite(b.tz_offset) ? b.tz_offset : base.tz_offset,
  });
  await db.query('UPDATE lab_account SET notify_prefs=$2 WHERE account_id=$1', [auth.accountId, JSON.stringify(merged)]);
  return { status: 200, body: await out(auth.accountId) };
}

// POST {online_id, favorite?, muted?}
async function friend({ req, auth }) {
  const b = await readJson(req);
  const f = await accountByOnlineId(b.online_id);
  if (!f) throw new HttpError(404, 'user_not_found');
  if (!(await areFriends(auth.accountId, f.account_id))) throw new HttpError(403, 'not_friends');
  const cur = (await db.query('SELECT favorite, muted FROM lab_notify_friend WHERE account_id=$1 AND friend_id=$2', [auth.accountId, f.account_id])).rows[0] || { favorite: false, muted: false };
  const fav = typeof b.favorite === 'boolean' ? b.favorite : cur.favorite;
  const mut = typeof b.muted === 'boolean' ? b.muted : cur.muted;
  await db.query(
    `INSERT INTO lab_notify_friend (account_id, friend_id, favorite, muted) VALUES ($1,$2,$3,$4)
     ON CONFLICT (account_id, friend_id) DO UPDATE SET favorite=EXCLUDED.favorite, muted=EXCLUDED.muted`, [auth.accountId, f.account_id, fav, mut]);
  return { status: 200, body: { online_id: f.online_id, favorite: fav, muted: mut } };
}

module.exports = { get, set, friend };
