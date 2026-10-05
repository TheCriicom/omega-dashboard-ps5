'use strict';
// Regole tra account condivise dagli endpoint social: blocchi (valgono nei due
// sensi), amicizie, privacy, stato personalizzato e la scheda utente che
// compare ovunque: { online_id, avatar, avatar_media, avatar_frames }.
const db = require('./db');

const STATUS_MODES = ['online', 'away', 'dnd', 'invisible'];

// Frammenti SQL: $n è il parametro con l'account di riferimento.
const BLOCKED_SQL = (p) => `(SELECT blocked_id FROM lab_block WHERE blocker_id=${p}
                             UNION SELECT blocker_id FROM lab_block WHERE blocked_id=${p})`;
const FRIENDS_SQL = (p) => `(SELECT CASE WHEN requester_id=${p} THEN addressee_id ELSE requester_id END
                               FROM lab_friendship WHERE status='accepted' AND (requester_id=${p} OR addressee_id=${p}))`;

// Account attivo per online_id (senza distinzione di maiuscole), oppure null.
async function accountByOnlineId(onlineId) {
  const r = await db.query(
    'SELECT account_id, online_id FROM lab_account WHERE lower(online_id) = lower($1) AND NOT disabled',
    [String(onlineId || '')],
  );
  return r.rows[0] || null;
}

function userCard(row) {
  return { online_id: row.online_id, avatar: row.avatar, avatar_media: row.avatar_media, avatar_frames: row.avatar_frames };
}

// { iBlocked, theyBlocked } tra me e un altro account.
async function blockState(me, other) {
  const r = await db.query(
    `SELECT blocker_id FROM lab_block
      WHERE (blocker_id=$1 AND blocked_id=$2) OR (blocker_id=$2 AND blocked_id=$1)`, [me, other]);
  const out = { iBlocked: false, theyBlocked: false };
  for (const row of r.rows) {
    if (String(row.blocker_id) === String(me)) out.iBlocked = true; else out.theyBlocked = true;
  }
  return out;
}

async function isBlocked(a, b) {
  const s = await blockState(a, b);
  return s.iBlocked || s.theyBlocked;
}

async function areFriends(a, b) {
  const r = await db.query(
    `SELECT 1 FROM lab_friendship WHERE status='accepted'
       AND ((requester_id=$1 AND addressee_id=$2) OR (requester_id=$2 AND addressee_id=$1))`, [a, b]);
  return r.rowCount > 0;
}

async function mutualCount(a, b) {
  const r = await db.query(
    `SELECT count(*)::int AS n FROM ${FRIENDS_SQL('$1')} x(id) WHERE id IN ${FRIENDS_SQL('$2')}`, [a, b]);
  return r.rows[0].n;
}

async function mutualFriends(a, b, limit = 5) {
  const r = await db.query(
    `SELECT acc.online_id, acc.avatar, acc.avatar_media, acc.avatar_frames
       FROM lab_account acc
      WHERE acc.account_id IN ${FRIENDS_SQL('$1')} AND acc.account_id IN ${FRIENDS_SQL('$2')} AND NOT acc.disabled
      ORDER BY acc.online_id LIMIT $3`, [a, b, limit]);
  return r.rows.map(userCard);
}

async function privacyOf(accountId) {
  const r = await db.query(
    `SELECT privacy_messages, privacy_friend_requests, show_activity, show_in_records, privacy_trophies, import_trophies
       FROM lab_account WHERE account_id=$1`, [accountId]);
  const p = r.rows[0] || {};
  return {
    messages: p.privacy_messages || 'everyone',
    friend_requests: p.privacy_friend_requests || 'everyone',
    show_activity: p.show_activity !== false,
    show_in_records: p.show_in_records !== false,
    trophies: p.privacy_trophies || 'everyone',
    import_trophies: p.import_trophies !== false,
  };
}

// Chi compare negli elenchi pubblici (record, classifica dei trofei) visti da
// $n: account attivi che non sono in un blocco con chi guarda. La scelta di
// comparire o no la aggiunge chi chiama (show_in_records, privacy_trophies).
const LISTABLE_SQL = (alias, p) => `(NOT ${alias}.disabled AND ${alias}.banned_at IS NULL
                                     AND ${alias}.account_id NOT IN ${BLOCKED_SQL(p)})`;

// Presenza vista dagli altri, secondo lo stato scelto dall'utente:
// invisibile → offline senza gioco; assente/non disturbare → quello stato.
function maskPresence(eff, mode) {
  if (!eff || eff.status === 'offline') return eff;
  if (mode === 'invisible') return { status: 'offline', game_id: null, game_name: null, started_at: null, last_seen: null };
  if (mode === 'away' || mode === 'dnd') return { ...eff, status: mode };
  return eff;
}

module.exports = {
  STATUS_MODES, BLOCKED_SQL, FRIENDS_SQL, LISTABLE_SQL, accountByOnlineId, userCard,
  blockState, isBlocked, areFriends,
  mutualCount, mutualFriends, privacyOf, maskPresence,
};
