'use strict';
// Segnala un bug o chiedi una funzione (app › Impostazioni › Aiuto).
// Il messaggio resta nel database e gli amministratori ricevono una notifica;
// dal pannello /admin si legge, si segna come fatto o si archivia.
const db = require('../db');
const { HttpError, retryLater, readJson } = require('../http');
const limiter = require('../ratelimit');
const { notifyAdmins } = require('../notify');
const messages = require('../messages');
const { multiline } = require('../text');

// POST /api/v1/feedback {kind: bug|idea, text, app_version?, lang?, log?}
async function submit({ req, auth }) {
  const wait = limiter.hit(`feedback|${auth.accountId}`, 8, 3600);
  if (wait) throw retryLater('too_many_feedback', wait);
  const b = await readJson(req);
  const kind = b.kind === 'idea' ? 'idea' : b.kind === 'bug' ? 'bug' : null;
  if (!kind) throw new HttpError(400, 'invalid_kind');
  const text = multiline(b.text);
  if (!text || text.length < 4) throw new HttpError(400, 'text_required');
  const log = typeof b.log === 'string' ? b.log.slice(-80 * 1024) : null;
  const r = await db.query(
    `INSERT INTO lab_feedback (account_id, kind, body, app_version, lang, log) VALUES ($1,$2,$3,$4,$5,$6) RETURNING feedback_id`,
    [auth.accountId, kind, text.slice(0, 2000), String(b.app_version || '').slice(0, 24) || null, String(b.lang || '').slice(0, 8) || null, log]);
  await notifyAdmins('feedback', {
    actorId: auth.accountId,
    title: (l) => messages.t(l, kind === 'bug' ? 'notify.feedback_bug' : 'notify.feedback_idea', { actor: auth.onlineId }),
    body: text.slice(0, 300), ref: String(r.rows[0].feedback_id),
  });
  return { status: 201, body: { feedback_id: String(r.rows[0].feedback_id) } };
}

// GET /admin/api/feedback?status=open|done|dismissed|all&offset=&limit=
async function adminList({ url }) {
  const status = url.searchParams.get('status') || 'open';
  const limit = Math.min(100, Math.max(1, Number(url.searchParams.get('limit')) || 50));
  const offset = Math.max(0, Number(url.searchParams.get('offset')) || 0);
  const args = [limit, offset];
  const where = status === 'all' ? '' : (args.push(status), `WHERE f.status=$${args.length}`);
  const rows = (await db.query(
    `SELECT f.feedback_id::text, f.kind, f.body, f.app_version, f.lang, f.status, f.created_at, (f.log IS NOT NULL) AS has_log,
            left(f.log, 20000) AS log, a.online_id
       FROM lab_feedback f LEFT JOIN lab_account a ON a.account_id=f.account_id ${where}
      ORDER BY f.feedback_id DESC LIMIT $1 OFFSET $2`, args)).rows;
  const total = (await db.query(`SELECT count(*)::int AS n FROM lab_feedback f ${where.replace(/\$3/, '$1')}`, status === 'all' ? [] : [status])).rows[0].n;
  return { status: 200, body: { feedback: rows, total } };
}

// POST /admin/api/feedback/:id {status: open|done|dismissed}
async function adminSet({ req, params }) {
  const b = await readJson(req);
  if (!['open', 'done', 'dismissed'].includes(b.status)) throw new HttpError(400, 'invalid_status');
  const r = await db.query('UPDATE lab_feedback SET status=$2 WHERE feedback_id=$1', [Number(params.id) || 0, b.status]);
  if (!r.rowCount) throw new HttpError(404, 'not_found');
  return { status: 200, body: { result: 'ok' } };
}

module.exports = { submit, adminList, adminSet };
