'use strict';
// Notifiche: una riga in lab_notification per destinatario, scaricate dall'app
// con GET /api/v1/sync. Un errore qui non fa fallire l'azione che le genera.
const db = require('./db');
const { FRIENDS_SQL } = require('./relations');

async function notify(accountId, type, { actorId = null, title, body = null, ref = null }) {
  try {
    await db.query(
      `INSERT INTO lab_notification (account_id, type, actor_id, title, body, ref)
       VALUES ($1,$2,$3,$4,$5,$6)`,
      [accountId, type, actorId, String(title).slice(0, 160), body == null ? null : String(body).slice(0, 400), ref]);
  } catch (err) {
    console.error(JSON.stringify({ ts: new Date().toISOString(), event: 'notify_error', type, error: err.message }));
  }
}

async function notifyFriends(accountId, type, payload) {
  const r = await db.query(`SELECT id FROM ${FRIENDS_SQL('$1')} f(id)`, [accountId]);
  for (const { id } of r.rows) await notify(id, type, { actorId: accountId, ...payload });
}

// Tutti gli amministratori attivi (segnalazioni da moderare).
async function notifyAdmins(type, payload) {
  const r = await db.query(`SELECT account_id FROM lab_account WHERE role='admin' AND banned_at IS NULL`);
  for (const { account_id: id } of r.rows) await notify(id, type, payload);
}

module.exports = { notify, notifyFriends, notifyAdmins };
