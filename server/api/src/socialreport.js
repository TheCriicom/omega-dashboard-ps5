'use strict';
// Segnalazioni social (utenti, post, commenti ai post), sul modello di quelle
// dello Store: una aperta per utente e contenuto. Raggiunta la soglia di
// segnalatori distinti, post e commenti si oscurano in attesa di verifica;
// gli utenti no (il ban si decide dal pannello). Gli admin ricevono una notifica.
const db = require('./db');
const { HttpError } = require('./http');
const config = require('./config');
const { notifyAdmins } = require('./notify');
const messages = require('./messages');
const limiter = require('./ratelimit');
const { multiline } = require('./text');

const USER_REASONS = new Set(['spam', 'molestie', 'contenuto_offensivo', 'impersonificazione', 'altro']);
const CONTENT_REASONS = new Set(['spam', 'contenuto_offensivo', 'molestie', 'altro']);

function cleanNote(v) {
  const s = multiline(v);
  return s ? s.slice(0, 500) : null;
}

// target: { type, id, ownerId, title, snapshot }
async function fileReport({ auth, body, target }) {
  if (limiter.hit(`sreport|${auth.accountId}`, 20, 3600)) throw new HttpError(429, 'too_many_reports');
  const reason = String(body.reason || '');
  const reasons = target.type === 'user' ? USER_REASONS : CONTENT_REASONS;
  if (!reasons.has(reason)) throw new HttpError(400, 'invalid_reason');
  if (String(target.ownerId) === String(auth.accountId)) throw new HttpError(400, 'cannot_report_own');
  await db.query(
    `INSERT INTO lab_social_report (target_type, target_id, target_owner_id, reporter_id, reason, note, snapshot)
     VALUES ($1,$2,$3,$4,$5,$6,$7) ON CONFLICT DO NOTHING`,
    [target.type, target.id, target.ownerId, auth.accountId, reason, cleanNote(body.note),
      target.snapshot ? String(target.snapshot).slice(0, 1000) : null]);
  const n = (await db.query(
    `SELECT count(DISTINCT reporter_id)::int AS n FROM lab_social_report
      WHERE target_type=$1 AND target_id=$2 AND status='open'`, [target.type, target.id])).rows[0].n;
  let hidden = false;
  if (n >= config.autoHideReports && target.type !== 'user') {
    if (target.type === 'post') {
      await db.query(`UPDATE lab_post SET hidden=true, hidden_reason='Oscurato in attesa di verifica (segnalazioni)' WHERE post_id=$1`, [target.id]);
    } else {
      await db.query('UPDATE lab_post_comment SET hidden=true WHERE comment_id=$1', [target.id]);
    }
    hidden = true;
  }
  await notifyAdmins('admin_social_report', {
    actorId: auth.accountId,
    title: (l) => messages.t(l, 'notify.admin_social_report', { kind: messages.tOr(l, `report_kind.${target.type}`, target.type), title: target.title }),
    body: (l) => messages.tOr(l, `report_reason.${reason}`, reason) + (hidden ? messages.t(l, 'notify.report_hidden') : ''),
    ref: `${target.type}:${target.id}`,
  });
  return { status: 201, body: { result: 'reported', hidden } };
}

module.exports = { fileReport };
