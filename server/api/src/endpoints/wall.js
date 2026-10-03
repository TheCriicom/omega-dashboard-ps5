'use strict';
// Bacheca: post brevi (con il gioco del momento), mi piace e commenti.
// Si vedono i propri post e quelli degli amici; un blocco, in un senso o
// nell'altro, nasconde post e commenti a entrambi. I contenuti oscurati dalla
// moderazione non compaiono.
const db = require('../db');
const { HttpError, readJson } = require('../http');
const { notify } = require('../notify');
const limiter = require('../ratelimit');
const rel = require('../relations');
const { fileReport } = require('../socialreport');
const { multiline, oneLine, charLength } = require('../text');

const POST_MAX = 500;
const COMMENT_MAX = 300;

const { accountByOnlineId } = rel;

// Da 1 a max caratteri: un testo troppo lungo si rifiuta, non si tronca.
function text(v, max) {
  const s = multiline(v);
  if (!s) throw new HttpError(400, 'empty_text');
  if (charLength(s) > max) throw new HttpError(400, 'text_too_long', `massimo ${max} caratteri`);
  return s;
}

function optional(v, max) {
  const s = oneLine(v, '');
  return s ? s.slice(0, max) : null;
}

function pageArgs(url, def = 20, max = 50) {
  const before = Number.parseInt(url.searchParams.get('before') || '0', 10) || 0;
  const limit = Math.min(max, Math.max(1, Number.parseInt(url.searchParams.get('limit') || String(def), 10) || def));
  return { before, limit };
}

// $1 = io. I conteggi dei commenti escludono oscurati e utenti bloccati.
const POST_COLS = `
  p.post_id, p.author_id, p.body, p.game_id, p.game_name, p.created_at,
  a.online_id, a.avatar, a.avatar_media, a.avatar_frames,
  (SELECT count(*)::int FROM lab_post_like l WHERE l.post_id=p.post_id) AS likes,
  EXISTS (SELECT 1 FROM lab_post_like l WHERE l.post_id=p.post_id AND l.account_id=$1) AS liked,
  (SELECT count(*)::int FROM lab_post_comment c WHERE c.post_id=p.post_id AND NOT c.hidden
      AND c.account_id NOT IN ${rel.BLOCKED_SQL('$1')}) AS comments`;

function postOut(row, me) {
  return {
    post_id: String(row.post_id), author: rel.userCard(row), text: row.body,
    game_id: row.game_id, game_name: row.game_name, created_at: row.created_at,
    likes: Number(row.likes || 0), liked: !!row.liked, comments: Number(row.comments || 0),
    mine: String(row.author_id) === String(me),
  };
}

function page(rows, me, limit) {
  const posts = rows.map((r) => postOut(r, me));
  return { posts, next_before: rows.length === limit ? posts[posts.length - 1].post_id : null };
}

// I contenuti di un autore li vedono lui e i suoi amici; altrimenti 403 blocked/not_friends.
async function assertCanSee(me, authorId) {
  if (String(me) === String(authorId)) return;
  if (await rel.isBlocked(me, authorId)) throw new HttpError(403, 'blocked');
  if (!(await rel.areFriends(me, authorId))) throw new HttpError(403, 'not_friends');
}

async function visiblePost(me, postId) {
  const r = await db.query(
    `SELECT ${POST_COLS} FROM lab_post p JOIN lab_account a ON a.account_id=p.author_id
      WHERE p.post_id=$2 AND NOT p.hidden AND NOT a.disabled`, [me, Number(postId) || 0]);
  const row = r.rows[0];
  if (!row) throw new HttpError(404, 'post_not_found');
  await assertCanSee(me, row.author_id);
  return row;
}

// ------------------------------------------------------------------ post --
// POST /api/v1/posts  { text, game_id?, game_name? }
async function create({ req, auth }) {
  if (limiter.hit(`post|${auth.accountId}`, 20, 3600)) throw new HttpError(429, 'too_many_posts');
  const body = await readJson(req);
  const t = text(body.text, POST_MAX);
  const r = await db.query(
    'INSERT INTO lab_post (author_id, body, game_id, game_name) VALUES ($1,$2,$3,$4) RETURNING post_id',
    [auth.accountId, t, optional(body.game_id, 64), optional(body.game_name, 128)]);
  const row = await visiblePost(auth.accountId, r.rows[0].post_id);
  return { status: 201, body: { post: postOut(row, auth.accountId) } };
}

// GET /api/v1/feed?before=<post_id>&limit=20
async function feed({ auth, url }) {
  const { before, limit } = pageArgs(url);
  const r = await db.query(
    `SELECT ${POST_COLS} FROM lab_post p JOIN lab_account a ON a.account_id=p.author_id
      WHERE NOT p.hidden AND NOT a.disabled
        AND (p.author_id=$1 OR p.author_id IN ${rel.FRIENDS_SQL('$1')})
        AND p.author_id NOT IN ${rel.BLOCKED_SQL('$1')}
        AND ($2::bigint = 0 OR p.post_id < $2)
      ORDER BY p.post_id DESC LIMIT $3`, [auth.accountId, before, limit]);
  return { status: 200, body: page(r.rows, auth.accountId, limit) };
}

// GET /api/v1/users/:onlineId/posts?before=&limit=
async function userPosts({ auth, params, url }) {
  const u = await accountByOnlineId(params.onlineId);
  if (!u) throw new HttpError(404, 'account_not_found');
  await assertCanSee(auth.accountId, u.account_id);
  const { before, limit } = pageArgs(url);
  const r = await db.query(
    `SELECT ${POST_COLS} FROM lab_post p JOIN lab_account a ON a.account_id=p.author_id
      WHERE p.author_id=$2 AND NOT p.hidden AND ($3::bigint = 0 OR p.post_id < $3)
      ORDER BY p.post_id DESC LIMIT $4`, [auth.accountId, u.account_id, before, limit]);
  return { status: 200, body: page(r.rows, auth.accountId, limit) };
}

// GET /api/v1/posts/:id  (per aprire un post da una notifica)
async function get({ auth, params }) {
  const row = await visiblePost(auth.accountId, params.id);
  return { status: 200, body: { post: postOut(row, auth.accountId) } };
}

// DELETE /api/v1/posts/:id  (solo l'autore)
async function remove({ auth, params }) {
  const p = (await db.query('SELECT author_id FROM lab_post WHERE post_id=$1', [Number(params.id) || 0])).rows[0];
  if (!p) throw new HttpError(404, 'post_not_found');
  if (String(p.author_id) !== String(auth.accountId)) throw new HttpError(403, 'not_author');
  await db.query('DELETE FROM lab_post WHERE post_id=$1', [Number(params.id) || 0]);
  return { status: 200, body: { result: 'ok' } };
}

// POST /api/v1/posts/:id/like  { like: true|false }
async function like({ req, auth, params }) {
  const row = await visiblePost(auth.accountId, params.id);
  const body = await readJson(req);
  if (typeof body.like !== 'boolean') throw new HttpError(400, 'invalid_like', 'like: true|false');
  if (body.like) {
    const ins = await db.query(
      'INSERT INTO lab_post_like (post_id, account_id) VALUES ($1,$2) ON CONFLICT DO NOTHING', [row.post_id, auth.accountId]);
    if (ins.rowCount && String(row.author_id) !== String(auth.accountId)) {
      // una sola notifica per persona, anche togliendo e rimettendo il mi piace
      const seen = await db.query(
        `SELECT 1 FROM lab_notification WHERE account_id=$1 AND type='post_like' AND actor_id=$2 AND ref=$3 LIMIT 1`,
        [row.author_id, auth.accountId, String(row.post_id)]);
      if (!seen.rowCount) {
        await notify(row.author_id, 'post_like', {
          actorId: auth.accountId, title: `A ${auth.onlineId} piace il tuo post`, body: row.body.slice(0, 120), ref: String(row.post_id) });
      }
    }
  } else {
    await db.query('DELETE FROM lab_post_like WHERE post_id=$1 AND account_id=$2', [row.post_id, auth.accountId]);
  }
  const n = (await db.query('SELECT count(*)::int AS n FROM lab_post_like WHERE post_id=$1', [row.post_id])).rows[0].n;
  return { status: 200, body: { likes: n, liked: body.like } };
}

// -------------------------------------------------------------- commenti --
function commentOut(c, me) {
  return {
    comment_id: String(c.comment_id), author: rel.userCard(c), text: c.body,
    created_at: c.created_at, mine: String(c.account_id) === String(me),
  };
}

// GET /api/v1/posts/:id/comments
async function comments({ auth, params }) {
  const row = await visiblePost(auth.accountId, params.id);
  const r = await db.query(
    `SELECT c.comment_id, c.account_id, c.body, c.created_at, a.online_id, a.avatar, a.avatar_media, a.avatar_frames
       FROM lab_post_comment c JOIN lab_account a ON a.account_id=c.account_id
      WHERE c.post_id=$2 AND NOT c.hidden AND NOT a.disabled AND c.account_id NOT IN ${rel.BLOCKED_SQL('$1')}
      ORDER BY c.comment_id LIMIT 300`, [auth.accountId, row.post_id]);
  return { status: 200, body: { comments: r.rows.map((c) => commentOut(c, auth.accountId)) } };
}

// POST /api/v1/posts/:id/comments  { text }
async function addComment({ req, auth, params }) {
  if (limiter.hit(`post_comment|${auth.accountId}`, 30, 600)) throw new HttpError(429, 'too_many_comments');
  const row = await visiblePost(auth.accountId, params.id);
  const body = await readJson(req);
  const t = text(body.text, COMMENT_MAX);
  const ins = (await db.query(
    'INSERT INTO lab_post_comment (post_id, account_id, body) VALUES ($1,$2,$3) RETURNING comment_id, created_at',
    [row.post_id, auth.accountId, t])).rows[0];
  if (String(row.author_id) !== String(auth.accountId)) {
    await notify(row.author_id, 'post_comment', {
      actorId: auth.accountId, title: `${auth.onlineId} ha commentato il tuo post`, body: t, ref: String(row.post_id) });
  }
  const me = (await db.query('SELECT online_id, avatar, avatar_media, avatar_frames FROM lab_account WHERE account_id=$1', [auth.accountId])).rows[0];
  return {
    status: 201,
    body: { comment: commentOut({ ...me, comment_id: ins.comment_id, account_id: auth.accountId, body: t, created_at: ins.created_at }, auth.accountId) },
  };
}

// DELETE /api/v1/posts/:id/comments/:commentId  (autore del commento o del post)
async function deleteComment({ auth, params }) {
  const c = (await db.query(
    `SELECT c.account_id, p.author_id FROM lab_post_comment c JOIN lab_post p USING (post_id)
      WHERE c.comment_id=$1 AND c.post_id=$2`, [Number(params.commentId) || 0, Number(params.id) || 0])).rows[0];
  if (!c) throw new HttpError(404, 'comment_not_found');
  if (String(c.account_id) !== String(auth.accountId) && String(c.author_id) !== String(auth.accountId)) throw new HttpError(403, 'not_allowed');
  await db.query('DELETE FROM lab_post_comment WHERE comment_id=$1', [Number(params.commentId) || 0]);
  return { status: 200, body: { result: 'ok' } };
}

// --------------------------------------------------------- segnalazioni --
// POST /api/v1/posts/:id/report  { reason, note? }
async function report({ req, auth, params }) {
  const row = await visiblePost(auth.accountId, params.id);
  const body = await readJson(req);
  return fileReport({ auth, body, target: { type: 'post', id: row.post_id, ownerId: row.author_id, title: `di ${row.online_id}`, snapshot: row.body } });
}

// POST /api/v1/posts/:id/comments/:commentId/report  { reason, note? }
async function reportComment({ req, auth, params }) {
  const row = await visiblePost(auth.accountId, params.id);
  const c = (await db.query(
    `SELECT c.comment_id, c.account_id, c.body, a.online_id FROM lab_post_comment c JOIN lab_account a ON a.account_id=c.account_id
      WHERE c.comment_id=$1 AND c.post_id=$2 AND NOT c.hidden`, [Number(params.commentId) || 0, row.post_id])).rows[0];
  if (!c) throw new HttpError(404, 'comment_not_found');
  if (await rel.isBlocked(auth.accountId, c.account_id)) throw new HttpError(404, 'comment_not_found');
  const body = await readJson(req);
  return fileReport({ auth, body, target: { type: 'post_comment', id: c.comment_id, ownerId: c.account_id, title: `di ${c.online_id}`, snapshot: c.body } });
}

module.exports = { create, feed, userPosts, get, remove, like, comments, addComment, deleteComment, report, reportComment };
