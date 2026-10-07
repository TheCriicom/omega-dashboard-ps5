'use strict';
// Notifiche: una riga in lab_notification per destinatario, scaricate dall'app
// con GET /api/v1/sync. Un errore qui non fa fallire l'azione che le genera.
//
// Titolo e corpo si scrivono nella lingua del destinatario (lab_account.lang,
// NULL = italiano) al momento della creazione: title e body possono essere
// testi fissi oppure funzioni (lang) → testo, di solito messages.t(...).
const db = require('./db');
const lang = require('./lang');
const { FRIENDS_SQL } = require('./relations');
const { decide } = require('./notifyprefs');

const langOf = (v) => (lang.isCode(v) ? v : lang.SOURCE);
const render = (v, code) => (typeof v === 'function' ? v(code) : v);

async function insert(accountId, recipientLang, type, { actorId = null, title, body = null, ref = null }) {
  try {
    // preferenze del destinatario: non la vuole (tipo spento, solo preferiti,
    // amico silenziato) oppure la vuole senza avviso (orari di silenzio, in gioco)
    const d = await decide(accountId, type, actorId);
    if (d.skip) return;
    const code = langOf(recipientLang);
    const t = render(title, code);
    const b = render(body, code);
    await db.query(
      `INSERT INTO lab_notification (account_id, type, actor_id, title, body, ref, silent)
       VALUES ($1,$2,$3,$4,$5,$6,$7)`,
      [accountId, type, actorId, String(t).slice(0, 160), b == null ? null : String(b).slice(0, 400), ref, !!d.silent]);
  } catch (err) {
    console.error(JSON.stringify({ ts: new Date().toISOString(), event: 'notify_error', type, error: err.message }));
  }
}

async function notify(accountId, type, payload) {
  let code = null;
  try {
    const r = await db.query('SELECT lang FROM lab_account WHERE account_id=$1', [accountId]);
    code = r.rows[0] ? r.rows[0].lang : null;
  } catch { /* si scrive in italiano */ }
  await insert(accountId, code, type, payload);
}

async function notifyFriends(accountId, type, payload) {
  const r = await db.query(
    `SELECT f.id, a.lang FROM ${FRIENDS_SQL('$1')} f(id) JOIN lab_account a ON a.account_id=f.id`, [accountId]);
  for (const { id, lang: l } of r.rows) await insert(id, l, type, { actorId: accountId, ...payload });
}

// Tutti gli amministratori attivi (segnalazioni da moderare).
async function notifyAdmins(type, payload) {
  const r = await db.query(`SELECT account_id, lang FROM lab_account WHERE role='admin' AND banned_at IS NULL`);
  for (const { account_id: id, lang: l } of r.rows) await insert(id, l, type, payload);
}

// Salva la lingua preferita dell'account se la richiesta ne indica una diversa
// (al login e a ogni /sync). `current` evita la query quando è già nota.
async function rememberLang(accountId, ctx, current) {
  if (!ctx || !ctx.langExplicit || !lang.isCode(ctx.lang) || current === ctx.lang) return;
  await db.query('UPDATE lab_account SET lang=$2 WHERE account_id=$1 AND lang IS DISTINCT FROM $2', [accountId, ctx.lang]);
}

module.exports = { notify, notifyFriends, notifyAdmins, rememberLang };
