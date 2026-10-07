// Preferenze delle notifiche (tipi, preferiti, silenziati, orari di silenzio)
// e la console vista dalla web app (annuncio, stato, coda "Installa sulla PS5").
import { createRequire } from 'node:module';
import path from 'node:path';
import { API_DIR as API, DATABASE_URL as DBURL, checker, j, suffix } from './helpers.mjs';

const require = createRequire(path.join(API, 'package.json'));
const { Pool } = require('pg');
const pool = new Pool({ connectionString: DBURL });
const q = async (sql, args) => (await pool.query(sql, args)).rows;
const { ok, done } = checker();

const sfx = suffix();
const N = Object.fromEntries(['a', 'b', 'c'].map((k) => [k, `n${k}${sfx}`]));
const tok = {};
for (const n of Object.values(N)) {
  await j('POST', '/api/v1/auth/register', { online_id: n, password: 'Password123!', accept_terms: true });
  tok[n] = (await j('POST', '/api/v1/auth/login', { online_id: n, password: 'Password123!' })).b.token;
}
const api = (k, method, p, body) => j(method, p, body, { authorization: `Bearer ${tok[N[k]]}` });
const id = {};
for (const k of Object.keys(N)) id[k] = (await q('SELECT account_id FROM lab_account WHERE online_id=$1', [N[k]]))[0].account_id;
// a è amico di b e di c
for (const k of ['b', 'c']) { await api(k, 'POST', '/api/v1/friends/request', { online_id: N.a }); await api('a', 'POST', '/api/v1/friends/accept', { online_id: N[k] }); }
const count = async (type, actor) => (await q('SELECT count(*)::int AS n FROM lab_notification WHERE account_id=$1 AND type=$2 AND actor_id=$3', [id.a, type, id[actor]]))[0].n;

console.log('\n--- preferenze di base');
let r = await api('a', 'GET', '/api/v1/notifications/prefs');
ok(r.s === 200 && r.b.types.online === 'favorites' && r.b.types.game_start === 'favorites' && r.b.types.message === 'all', 'online e gioco solo dai preferiti, il resto da tutti', r.b);
ok(r.b.friends.length === 2 && r.b.friends.every((f) => !f.favorite && !f.muted), 'amici elencati, nessun preferito', r.b.friends);

console.log('\n--- "sta giocando a" solo dai preferiti');
await api('b', 'POST', '/api/v1/presence', { status: 'online', game_id: `GA${sfx}`.toUpperCase(), game_name: 'Gioco A' });
ok(await count('game_start', 'b') === 0, 'b non è preferito: nessuna notifica');
r = await api('a', 'POST', '/api/v1/notifications/prefs/friend', { online_id: N.b, favorite: true });
ok(r.s === 200 && r.b.favorite === true, 'b preferito', r.b);
await api('b', 'POST', '/api/v1/presence', { status: 'online', game_id: `GB${sfx}`.toUpperCase(), game_name: 'Gioco B' });
ok(await count('game_start', 'b') === 1, 'b preferito: la notifica arriva');

console.log('\n--- tipo spento e amico silenziato');
await api('a', 'POST', '/api/v1/notifications/prefs', { types: { message: 'off' } });
await api('c', 'POST', `/api/v1/messages/${N.a}`, { text: 'ciao' });
ok(await count('message', 'c') === 0, 'messaggi spenti: nessuna notifica');
await api('a', 'POST', '/api/v1/notifications/prefs', { types: { message: 'all' } });
await api('a', 'POST', '/api/v1/notifications/prefs/friend', { online_id: N.c, muted: true });
await api('c', 'POST', `/api/v1/messages/${N.a}`, { text: 'ciao di nuovo' });
ok(await count('message', 'c') === 0, 'c silenziato: nessuna notifica');
await api('a', 'POST', '/api/v1/notifications/prefs/friend', { online_id: N.c, muted: false });
await api('c', 'POST', `/api/v1/messages/${N.a}`, { text: 'terzo' });
ok(await count('message', 'c') === 1, 'non più silenziato: arriva');

console.log('\n--- orari di silenzio: la notifica resta ma senza avviso');
const now = new Date(), from = `${String(now.getUTCHours()).padStart(2, '0')}:00`, to = `${String((now.getUTCHours() + 1) % 24).padStart(2, '0')}:00`;
r = await api('a', 'POST', '/api/v1/notifications/prefs', { quiet: { enabled: true, from, to }, tz_offset: 0 });
ok(r.s === 200 && r.b.quiet.enabled && r.b.tz_offset === 0, 'orari salvati', r.b.quiet);
await api('c', 'POST', `/api/v1/messages/${N.a}`, { text: 'di notte' });
const last = (await q(`SELECT silent FROM lab_notification WHERE account_id=$1 AND type='message' ORDER BY notification_id DESC LIMIT 1`, [id.a]))[0];
ok(last && last.silent === true, 'notifica silenziosa', last);
r = await api('a', 'GET', '/api/v1/notifications');
ok(r.b.notifications.some((n) => n.silent === true), 'l\'elenco riporta silent', r.b.notifications.slice(0, 2));
r = await api('a', 'POST', '/api/v1/notifications/prefs', { types: { bogus: 'off', online: 'nope' } });
ok(r.b.types.online === 'favorites' && !('bogus' in r.b.types), 'valori sconosciuti ignorati', r.b.types);

console.log('\n--- console vista dalla web app');
r = await api('b', 'GET', '/api/v1/console');
ok(r.s === 200 && r.b.paired === false && r.b.online === false && r.b.link === null, 'mai annunciata', r.b);
r = await api('b', 'POST', '/api/v1/console/announce', { lan_ip: '8.8.8.8', port: 9095, token: 'abcdef0123456789abcdef0123456789', version: 't' });
ok(r.s === 204, 'annuncio accettato');
r = await api('b', 'GET', '/api/v1/console');
ok(r.b.online === true && r.b.lan_ip === null && r.b.link === null, 'indirizzo pubblico scartato: niente link', r.b);
await api('b', 'POST', '/api/v1/console/announce', { lan_ip: '192.168.1.40', port: 9095, token: 'abcdef0123456789abcdef0123456789', version: 't' });
r = await api('b', 'GET', '/api/v1/console');
ok(r.b.link === 'http://192.168.1.40:9095/#k=abcdef0123456789abcdef0123456789', 'link diretto nella rete di casa', r.b);
r = await api('c', 'GET', '/api/v1/console');
ok(r.b.paired === false && !r.b.link, 'un altro account non vede la console di b', r.b);
await q(`UPDATE lab_console SET updated_at = now() - interval '5 minutes' WHERE account_id=$1`, [id.b]);
r = await api('b', 'GET', '/api/v1/console');
ok(r.b.online === false && r.b.link === null && r.b.paired === true, 'annuncio vecchio: spenta, niente link', r.b);

console.log('\n--- installa sulla PS5 dal telefono');
const app = (await q('SELECT app_id FROM lab_store_app WHERE published LIMIT 1'))[0];
if (app) {
  r = await api('b', 'POST', '/api/v1/console/queue', { app_id: Number(app.app_id) });
  ok(r.s === 202 && r.b.queued === true && r.b.online === false, 'in coda (console spenta)', r.b);
  r = await api('b', 'GET', '/api/v1/console/queue');
  ok(r.b.items.length === 1 && r.b.items[0].app_id === String(app.app_id), 'la console prende il comando', r.b);
  r = await api('b', 'GET', '/api/v1/console/queue');
  ok(r.b.items.length === 0, 'preso una volta sola', r.b);
} else console.log('(nessun homebrew pubblicato: coda non provata)');
r = await api('b', 'POST', '/api/v1/console/queue', { app_id: 99999999 });
ok(r.s === 404, 'app inesistente → 404', r.b);

await pool.end();
done();
