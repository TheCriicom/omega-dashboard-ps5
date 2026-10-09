// Sessioni che scadono per inattività: chi usa Omega resta collegato, chi non
// lo apre per TTL secondi esce, il logout chiude davvero.
import { createRequire } from 'node:module';
import path from 'node:path';
import { API_DIR as API, DATABASE_URL as DBURL, checker, j, suffix } from './helpers.mjs';

const require = createRequire(path.join(API, 'package.json'));
const { Pool } = require('pg');
const pool = new Pool({ connectionString: DBURL });
const q = async (sql, args) => (await pool.query(sql, args)).rows;
const { ok, done } = checker();

const n = `ses${suffix()}`;
await j('POST', '/api/v1/auth/register', { online_id: n, password: 'Password123!', accept_terms: true });
const login = (await j('POST', '/api/v1/auth/login', { online_id: n, password: 'Password123!' })).b;
ok(!!login.token, 'login');
ok(login.expires_in >= 30 * 86400, 'durata predefinita di almeno 30 giorni', login.expires_in);
const sid = login.token.split('.')[1];
const H = { authorization: `Bearer ${login.token}` };
const exp = async () => (await q('SELECT extract(epoch FROM expires_at - now()) AS s FROM lab_session WHERE session_id = $1', [sid]))[0].s;

// a un'ora dalla scadenza: la prossima richiesta la rinnova per intero
await q("UPDATE lab_session SET expires_at = now() + interval '1 hour' WHERE session_id = $1", [sid]);
ok((await j('GET', '/api/v1/me', null, H)).s === 200, 'sessione quasi scaduta ancora valida');
ok(await exp() > login.expires_in - 120, 'rinnovata a now + TTL', await exp());

// appena rinnovata: nessuna nuova scrittura (la scadenza non si sposta)
const before = await exp();
await j('GET', '/api/v1/me', null, H);
ok(Math.abs(await exp() - before) < 5, 'nessun rinnovo a ogni richiesta');

// scaduta davvero: 401 e niente rinnovo
await q("UPDATE lab_session SET expires_at = now() - interval '1 minute' WHERE session_id = $1", [sid]);
ok((await j('GET', '/api/v1/me', null, H)).s === 401, 'sessione scaduta → 401');
ok(await exp() < 0, 'una sessione scaduta non si rinnova');

// logout: chiusa anche se rinnovabile
await q("UPDATE lab_session SET expires_at = now() + interval '1 hour' WHERE session_id = $1", [sid]);
ok((await j('POST', '/api/v1/auth/logout', {}, H)).s < 300, 'logout');
ok((await j('GET', '/api/v1/me', null, H)).s === 401, 'dopo il logout → 401');

await pool.end();
done();
