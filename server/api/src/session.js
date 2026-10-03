'use strict';
// Sessioni dell'app console. Il token è  lab1.<session_id>.<firma>, con firma
// HMAC-SHA256 del session_id: il prefisso "lab1" resta perché le sessioni già
// emesse devono continuare a valere. Qualsiasi altro token è un 401.
const crypto = require('node:crypto');
const config = require('./config');
const db = require('./db');

const SECRET = config.sessionSecret;
const TTL = config.sessionTtlSeconds;
if (!SECRET || SECRET.length < 32) {
  throw new Error('OMEGA_SESSION_SECRET mancante o troppo corto (servono almeno 32 caratteri)');
}

const TOKEN = /^Bearer\s+lab1\.([0-9a-f-]{36})\.([A-Za-z0-9_-]+)$/;

// Firma HMAC con il segreto di sessione; `scope` separa gli usi (es. il cookie admin).
const sign = (value, scope = '') => crypto.createHmac('sha256', SECRET).update(scope + value).digest('base64url');

function signatureMatches(value, signature, scope) {
  const expected = Buffer.from(sign(value, scope));
  const given = Buffer.from(signature);
  return expected.length === given.length && crypto.timingSafeEqual(expected, given);
}

// Apre una sessione per un account già verificato.
async function issue(account) {
  const ses = await db.query(
    `INSERT INTO lab_session (account_id, expires_at)
     VALUES ($1, now() + make_interval(secs => $2))
     RETURNING session_id, expires_at`,
    [account.account_id, TTL],
  );
  const { session_id: sessionId, expires_at: expiresAt } = ses.rows[0];
  return {
    token: `lab1.${sessionId}.${sign(sessionId)}`,
    token_type: 'Bearer',
    expires_in: TTL,
    expires_at: expiresAt,
    account_id: String(account.account_id),
    online_id: account.online_id,
  };
}

// L'account della richiesta, oppure null: un token malformato è solo un 401.
async function resolve(req) {
  const m = TOKEN.exec(req.headers.authorization || '');
  if (!m) return null;
  const [, sessionId, signature] = m;
  if (!signatureMatches(sessionId, signature)) return null;
  const r = await db.query(
    `SELECT s.account_id, a.online_id
       FROM lab_session s JOIN lab_account a USING (account_id)
      WHERE s.session_id = $1 AND NOT s.revoked AND s.expires_at > now() AND NOT a.disabled`,
    [sessionId],
  );
  if (r.rowCount === 0) return null;
  return { accountId: String(r.rows[0].account_id), onlineId: r.rows[0].online_id, sessionId };
}

async function revoke(sessionId) {
  await db.query('UPDATE lab_session SET revoked = true WHERE session_id = $1', [sessionId]);
}

// Chiude tutte le sessioni di un account tranne, eventualmente, quella indicata.
async function revokeAll(accountId, exceptSessionId = null) {
  const r = await db.query(
    `UPDATE lab_session SET revoked = true
      WHERE account_id = $1 AND NOT revoked AND ($2::uuid IS NULL OR session_id <> $2)`,
    [accountId, exceptSessionId],
  );
  return r.rowCount;
}

module.exports = { issue, resolve, revoke, revokeAll, sign, signatureMatches };
