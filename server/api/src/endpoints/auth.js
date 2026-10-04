'use strict';
// Registrazione e login con online_id + password (email facoltativa). La
// registrazione si può chiudere con una chiave (OMEGA_REGISTRATION_KEY).
// Login: stesso errore e stessi tempi per account inesistente e password
// sbagliata (verifyDecoy), così da fuori non si scopre quali online_id esistono.
const crypto = require('node:crypto');
const db = require('../db');
const session = require('../session');
const passwords = require('../passwords');
const limiter = require('../ratelimit');
const config = require('../config');
const { HttpError, retryLater, readJson } = require('../http');
const legal = require('../legal');

const ONLINE_ID = /^[A-Za-z][A-Za-z0-9_-]{2,15}$/;
const EMAIL = /^[^\s@]{1,64}@[^\s@]{1,190}\.[^\s@]{2,}$/;
const REGISTRATION_KEY = config.registrationKey;

const tooMany = (seconds) => retryLater('too_many_attempts', seconds, `riprova tra ${seconds}s`);

function checkPassword(password, onlineId) {
  if (typeof password !== 'string' || password.length < 8 || password.length > 128) {
    throw new HttpError(400, 'invalid_password', 'la password deve avere tra 8 e 128 caratteri');
  }
  if (onlineId && password.toLowerCase().includes(onlineId.toLowerCase())) {
    throw new HttpError(400, 'invalid_password', 'la password non può contenere l\'online_id');
  }
}

function keyMatches(given) {
  const a = Buffer.from(String(given || ''));
  const b = Buffer.from(REGISTRATION_KEY);
  return a.length === b.length && crypto.timingSafeEqual(a, b);
}

// POST /api/v1/auth/register  { online_id, password, email?, registration_key? }
async function register({ req, clientIp, lang, langExplicit }) {
  const wait = limiter.hit(`register|${clientIp}`, 10, 3600);
  if (wait) throw tooMany(wait);

  const body = await readJson(req);
  if (REGISTRATION_KEY && !keyMatches(body.registration_key)) {
    throw new HttpError(403, 'registration_closed', 'serve la chiave di registrazione');
  }
  const onlineId = String(body.online_id || '');
  if (!ONLINE_ID.test(onlineId)) {
    throw new HttpError(400, 'invalid_online_id', 'online_id: 3-16 caratteri, lettera iniziale, [A-Za-z0-9_-]');
  }
  checkPassword(body.password, onlineId);
  // consenso esplicito a termini e informativa: se ne registra la versione
  if (body.accept_terms !== true) throw new HttpError(400, 'terms_not_accepted', 'serve accettare termini d\'uso e informativa privacy');
  let email = null;
  if (body.email !== undefined && body.email !== null && body.email !== '') {
    email = String(body.email).trim().toLowerCase();
    if (!EMAIL.test(email)) throw new HttpError(400, 'invalid_email');
  }

  const hash = await passwords.hash(body.password);
  try {
    const r = await db.query(
      `INSERT INTO lab_account (online_id, password_hash, email, terms_version, terms_accepted_at, lang) VALUES ($1, $2, $3, $4, now(), $5)
       RETURNING account_id, online_id, created_at`,
      [onlineId, hash, email, legal.TERMS_VERSION, langExplicit ? lang : null],
    );
    const a = r.rows[0];
    return {
      status: 201,
      body: { account_id: String(a.account_id), online_id: a.online_id, created_at: a.created_at },
    };
  } catch (err) {
    if (err.code === '23505') {
      // Alla registrazione dire "esiste già" è inevitabile; al login no.
      const field = /email/.test(err.constraint || '') ? 'email' : 'online_id';
      throw new HttpError(409, `${field}_taken`, `${field} già registrato`);
    }
    throw err;
  }
}

// POST /api/v1/auth/login  { online_id, password }
async function login({ req, clientIp, lang, langExplicit }) {
  const body = await readJson(req);
  const onlineId = String(body.online_id || '');
  const password = typeof body.password === 'string' ? body.password : '';

  const waitIp = limiter.hit(`login-ip|${clientIp}`, 30, 900);
  const waitAcc = limiter.hit(`login-acc|${onlineId.toLowerCase()}`, 8, 900);
  if (waitIp || waitAcc) throw tooMany(Math.max(waitIp, waitAcc));

  const r = ONLINE_ID.test(onlineId) && password
    ? await db.query(
      `SELECT account_id, online_id, password_hash, disabled, banned_at, ban_reason FROM lab_account WHERE lower(online_id) = lower($1)`,
      [onlineId],
    )
    : { rowCount: 0, rows: [] };
  const account = r.rows[0];

  const ok = account && account.password_hash && !account.disabled
    ? await passwords.verify(password, account.password_hash)
    : await passwords.verifyDecoy(password);
  if (!ok) {
    // account sospeso: solo con la password giusta si dice perché
    if (account && account.banned_at && account.password_hash && await passwords.verify(password, account.password_hash)) {
      throw new HttpError(403, 'account_banned', account.ban_reason || 'account sospeso per violazione dei termini d\'uso');
    }
    throw new HttpError(401, 'invalid_credentials', 'online_id o password non validi');
  }

  limiter.reset(`login-acc|${onlineId.toLowerCase()}`);
  // la lingua dell'app (Accept-Language) diventa quella delle notifiche dell'account
  await db.query('UPDATE lab_account SET last_login_at = now(), lang = coalesce($2, lang) WHERE account_id = $1',
    [account.account_id, langExplicit ? lang : null]);
  return { status: 200, body: await session.issue(account) };
}

// POST /api/v1/auth/logout
async function logout({ auth }) {
  await session.revoke(auth.sessionId);
  return { status: 204 };
}

// POST /api/v1/auth/password  { current_password, new_password }
// Chiude tutte le altre sessioni dell'account.
async function changePassword({ req, auth }) {
  const body = await readJson(req);
  const r = await db.query('SELECT password_hash FROM lab_account WHERE account_id = $1', [auth.accountId]);
  const current = typeof body.current_password === 'string' ? body.current_password : '';
  if (!r.rows[0]?.password_hash || !(await passwords.verify(current, r.rows[0].password_hash))) {
    throw new HttpError(401, 'invalid_credentials', 'password attuale non valida');
  }
  checkPassword(body.new_password, auth.onlineId);
  await db.query('UPDATE lab_account SET password_hash = $1 WHERE account_id = $2',
    [await passwords.hash(body.new_password), auth.accountId]);
  const closed = await session.revokeAll(auth.accountId, auth.sessionId);
  return { status: 200, body: { password_changed: true, other_sessions_closed: closed } };
}

module.exports = { register, login, logout, changePassword };
