'use strict';
// L'account dell'utente e i suoi diritti sui dati (GDPR): profilo di base,
// esportazione completa, cancellazione, accettazione dei termini aggiornati.
// La cancellazione si propaga a tutto ciò che è legato all'account
// (FK ON DELETE CASCADE) e rimuove i file di avatar e copertina.
const db = require('../db');
const passwords = require('../passwords');
const limiter = require('../ratelimit');
const legal = require('../legal');
const { HttpError, readJson } = require('../http');
const { removeMedia } = require('./media');

async function rows(sql, args) { return (await db.query(sql, args)).rows; }

// GET /api/v1/me
async function me({ auth }) {
  const a = (await rows(
    'SELECT account_id, online_id, email, created_at, last_login_at, role, terms_version FROM lab_account WHERE account_id = $1',
    [auth.accountId],
  ))[0];
  return {
    status: 200,
    body: {
      account_id: String(a.account_id), online_id: a.online_id, email: a.email,
      created_at: a.created_at, last_login_at: a.last_login_at, role: a.role,
      terms: { accepted: a.terms_version, current: legal.TERMS_VERSION, needs_accept: a.terms_version !== legal.TERMS_VERSION },
    },
  };
}

// GET /api/v1/account/export
async function exportData({ auth }) {
  if (limiter.hit(`export|${auth.accountId}`, 5, 3600)) throw new HttpError(429, 'too_many_requests');
  const id = auth.accountId;
  const account = (await rows(
    `SELECT online_id, email, about_me, avatar, avatar_media, cover_media, role, created_at, last_login_at,
            terms_version, terms_accepted_at, status_mode, status_message,
            privacy_messages, privacy_friend_requests, show_activity FROM lab_account WHERE account_id=$1`, [id]))[0];
  const data = {
    exported_at: new Date().toISOString(),
    account,
    friends: await rows(
      `SELECT a.online_id, f.status, f.created_at FROM lab_friendship f
         JOIN lab_account a ON a.account_id = CASE WHEN f.requester_id=$1 THEN f.addressee_id ELSE f.requester_id END
        WHERE f.requester_id=$1 OR f.addressee_id=$1`, [id]),
    presence: (await rows('SELECT status, game_id, game_name, started_at, last_seen FROM lab_presence WHERE account_id=$1', [id]))[0] || null,
    activity: await rows('SELECT type, game_id, game_name, detail, created_at FROM lab_activity WHERE account_id=$1 ORDER BY created_at', [id]),
    messages_sent: await rows(
      `SELECT coalesce(a.online_id, '(party)') AS to, m.body, m.created_at FROM lab_message m
         LEFT JOIN lab_account a ON a.account_id=m.to_id WHERE m.from_id=$1 ORDER BY m.message_id`, [id]),
    messages_received: await rows(
      `SELECT a.online_id AS from, m.body, m.created_at, m.read_at FROM lab_message m
         JOIN lab_account a ON a.account_id=m.from_id WHERE m.to_id=$1 ORDER BY m.message_id`, [id]),
    notifications: await rows('SELECT type, title, body, created_at, read FROM lab_notification WHERE account_id=$1 ORDER BY notification_id', [id]),
    store_apps: await rows(
      `SELECT title, tagline, description, category, version, download_url, file_kind, published, created_at
         FROM lab_store_app WHERE author_id=$1 ORDER BY app_id`, [id]),
    store_comments: await rows(
      `SELECT s.title AS app, c.body, c.created_at FROM lab_store_comment c JOIN lab_store_app s USING (app_id)
        WHERE c.account_id=$1 ORDER BY c.comment_id`, [id]),
    store_votes: await rows('SELECT s.title AS app, v.value, v.created_at FROM lab_store_vote v JOIN lab_store_app s USING (app_id) WHERE v.account_id=$1', [id]),
    store_ratings: await rows('SELECT s.title AS app, r.stars, r.created_at FROM lab_store_rating r JOIN lab_store_app s USING (app_id) WHERE r.account_id=$1', [id]),
    store_reports: await rows('SELECT s.title AS app, r.reason, r.note, r.status, r.created_at FROM lab_store_report r JOIN lab_store_app s USING (app_id) WHERE r.reporter_id=$1', [id]),
    library_source: (await rows('SELECT url, name, last_sync FROM lab_library_source WHERE account_id=$1', [id]))[0] || null,
    library_items: await rows('SELECT title, platform, version, title_id, download_url, file_kind FROM lab_library_item WHERE account_id=$1 ORDER BY title', [id]),
    // tabelle dello schema iniziale non più alimentate: restano finché contengono dati dell'utente
    posts: await rows('SELECT body, game_id, game_name, hidden, created_at FROM lab_post WHERE author_id=$1 ORDER BY post_id', [id]),
    post_comments: await rows(
      `SELECT a.online_id AS post_author, c.body, c.created_at FROM lab_post_comment c
         JOIN lab_post p USING (post_id) JOIN lab_account a ON a.account_id=p.author_id WHERE c.account_id=$1 ORDER BY c.comment_id`, [id]),
    post_likes: await rows('SELECT post_id::text, created_at FROM lab_post_like WHERE account_id=$1', [id]),
    blocks: await rows('SELECT a.online_id, b.created_at FROM lab_block b JOIN lab_account a ON a.account_id=b.blocked_id WHERE b.blocker_id=$1', [id]),
    social_reports: await rows('SELECT target_type, reason, note, status, created_at FROM lab_social_report WHERE reporter_id=$1', [id]),
    groups: await rows('SELECT g.name, m.joined_at FROM lab_group_member m JOIN lab_group g USING (group_id) WHERE m.account_id=$1', [id]),
    group_messages_sent: await rows(
      `SELECT g.name AS group, m.body, m.created_at FROM lab_group_message m JOIN lab_group g USING (group_id)
        WHERE m.from_id=$1 AND NOT m.system ORDER BY m.message_id`, [id]),
    playtime: await rows('SELECT game_id, game_name, seconds::bigint AS seconds, sessions, last_played FROM lab_playtime WHERE account_id=$1 ORDER BY seconds DESC', [id]),
  };
  return { status: 200, body: data };
}

// POST /api/v1/account/delete  { password }
async function deleteAccount({ req, auth }) {
  if (limiter.hit(`delacc|${auth.accountId}`, 5, 900)) throw new HttpError(429, 'too_many_attempts');
  const body = await readJson(req);
  const a = (await rows('SELECT password_hash, avatar_media, cover_media FROM lab_account WHERE account_id=$1', [auth.accountId]))[0];
  const pw = typeof body.password === 'string' ? body.password : '';
  if (!a || !a.password_hash || !(await passwords.verify(pw, a.password_hash))) throw new HttpError(401, 'invalid_credentials', 'password non valida');
  await db.query('DELETE FROM lab_account WHERE account_id=$1', [auth.accountId]);
  await removeMedia(a.avatar_media);
  await removeMedia(a.cover_media);
  console.log(JSON.stringify({ ts: new Date().toISOString(), event: 'account_deleted', online_id: auth.onlineId }));
  return { status: 200, body: { result: 'deleted' } };
}

// POST /api/v1/account/terms  { version }
async function acceptTerms({ req, auth }) {
  const body = await readJson(req);
  if (String(body.version || '') !== legal.TERMS_VERSION) throw new HttpError(409, 'terms_outdated', `versione attuale: ${legal.TERMS_VERSION}`);
  await db.query('UPDATE lab_account SET terms_version=$2, terms_accepted_at=now() WHERE account_id=$1', [auth.accountId, legal.TERMS_VERSION]);
  return { status: 200, body: { result: 'ok', version: legal.TERMS_VERSION } };
}

module.exports = { me, exportData, deleteAccount, acceptTerms };
