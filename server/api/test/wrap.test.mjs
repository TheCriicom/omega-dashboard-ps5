// Il tuo riepilogo: periodi nel fuso di chi guarda, totali, ritmo, giorni di
// fila, sessione più lunga, trofei, «giocato insieme» solo con gli amici che
// mostrano la loro attività, confronto anonimo con la community, settimane
// prima del registro delle sessioni e richieste sbagliate.
import { createRequire } from 'node:module';
import path from 'node:path';
import { API_DIR as API, DATABASE_URL as DBURL, checker, j, suffix } from './helpers.mjs';

const require = createRequire(path.join(API, 'package.json'));
const { Pool } = require('pg');
const pool = new Pool({ connectionString: DBURL });
const q = async (sql, args) => (await pool.query(sql, args)).rows;
const { ok, done } = checker();
const { bounds, persona } = require(path.join(API, 'src/endpoints/wrap.js'));

const sfx = suffix();
const N = Object.fromEntries(['a', 'b', 'c', 'd', 'e', 'f', 'g', 'h'].map((k) => [k, `w${k}${sfx}`]));
const tok = {}; const id = {};
for (const [k, n] of Object.entries(N)) {
  await j('POST', '/api/v1/auth/register', { online_id: n, password: 'Password123!', accept_terms: true });
  tok[k] = (await j('POST', '/api/v1/auth/login', { online_id: n, password: 'Password123!' })).b.token;
  id[k] = (await q('SELECT account_id FROM lab_account WHERE online_id=$1', [n]))[0].account_id;
}
const api = (k, method, p, body) => j(method, p, body, { authorization: `Bearer ${tok[k]}` });
const G1 = `PPSA9${sfx}`.toUpperCase().slice(0, 9); const G2 = `CUSA9${sfx}`.toUpperCase().slice(0, 9);
const NP = `NPWR9${sfx}_00`.toUpperCase();
const TZ = 120;

console.log('\n--- periodi');
const now = Date.UTC(2026, 9, 8, 0, 30);                 // giovedì 8 ottobre, 02:30 in Italia
let b = bounds('week', 0, 120, now);
ok(b.from.toISOString() === '2026-10-04T22:00:00.000Z' && b.to.toISOString() === '2026-10-11T22:00:00.000Z', 'settimana da lunedì 00:00 ora locale', b);
b = bounds('week', 1, 120, now);
ok(b.from.toISOString() === '2026-09-27T22:00:00.000Z', 'settimana scorsa', b);
b = bounds('week', 0, -300, now);
ok(b.from.toISOString() === '2026-10-05T04:00:00.000Z' || b.from.toISOString() === '2026-10-05T05:00:00.000Z', 'a New York è ancora mercoledì 7: stessa settimana, lunedì 00:00 di New York', b);
b = bounds('month', 1, 120, now);
ok(b.from.toISOString() === '2026-08-31T22:00:00.000Z' && b.to.toISOString() === '2026-09-30T22:00:00.000Z', 'mese scorso', b);
b = bounds('year', 0, 0, now);
ok(b.from.toISOString() === '2026-01-01T00:00:00.000Z' && b.to.toISOString() === '2027-01-01T00:00:00.000Z', 'anno', b);
b = bounds('month', 10, 0, now);
ok(b.from.toISOString() === '2025-12-01T00:00:00.000Z', 'mesi indietro oltre l\'anno', b);

// ------------------------------------------------- dati della settimana scorsa
// a: lun 23:00 → mar 02:00 G1, mer 22:30 → 23:30 G1, gio 10:00 → 10:45 G2 (ora locale)
const W = bounds('week', 1, TZ).from.getTime();
const at = (day, h, m = 0) => new Date(W + ((day * 24 + h) * 60 + m) * 60e3);
const sess = (k, g, s, e) => q(
  'INSERT INTO lab_play_session (account_id, game_id, started_at, ended_at, seconds) VALUES ($1,$2,$3,$4,$5)',
  [id[k], g, s, e, Math.round((e - s) / 1000)]);
await sess('a', G1, at(0, 23), at(1, 2));
await sess('a', G1, at(2, 22, 30), at(2, 23, 30));
await sess('a', G2, at(3, 10), at(3, 10, 45));
await sess('b', G1, at(0, 23, 30), at(1, 1));            // amico, insieme ad a per 90 minuti
await sess('c', G1, at(0, 23), at(1, 2));                 // amico che nasconde la sua attività
await sess('d', G1, at(0, 23), at(1, 2));                 // non amico
for (const k of ['e', 'f', 'g']) await sess(k, G2, at(4, 18), at(4, 18, 10));
// il registro delle sessioni «parte» due giorni prima della settimana scorsa
await sess('h', G2, new Date(W - 2 * 864e5), new Date(W - 2 * 864e5 + 60e3));

for (const k of ['b', 'c']) {
  await api('a', 'POST', '/api/v1/friends/request', { online_id: N[k] });
  await api(k, 'POST', '/api/v1/friends/accept', { online_id: N.a });
}
await q('UPDATE lab_account SET show_activity=false WHERE account_id=$1', [id.c]);

// trofei: a ottiene l'oro 1 (solo lui) e il bronzo 2 (tutti e tre)
await q(`INSERT INTO lab_tset (np_id, title, trophies) VALUES ($1, 'Gioco di prova', $2)`,
  [NP, JSON.stringify([{ id: 1, grade: 'G', name: 'Raro' }, { id: 2, grade: 'B', name: 'Comune' }, { id: 3, grade: 'S', name: 'Fuori periodo' }])]);
const tAt = at(1, 12).toISOString();
const old = new Date(W - 30 * 864e5).toISOString();
await q(`INSERT INTO lab_tset_user (account_id, np_id, parsed, earned, last_earned) VALUES ($1,$2,true,$3,$4)`,
  [id.a, NP, JSON.stringify({ 1: tAt, 2: tAt, 3: old }), tAt]);
for (const k of ['b', 'c']) {
  await q(`INSERT INTO lab_tset_user (account_id, np_id, parsed, earned, last_earned) VALUES ($1,$2,true,$3,$4)`,
    [id[k], NP, JSON.stringify({ 2: tAt }), tAt]);
}

console.log('\n--- richieste sbagliate');
let r = await j('GET', '/api/v1/wrap');
ok(r.s === 401, 'serve il login', r.b);
r = await api('a', 'GET', '/api/v1/wrap?back=-1');
ok(r.s === 400, 'back negativo', r.b);
r = await api('a', 'GET', '/api/v1/wrap?period=week&back=999');
ok(r.s === 400, 'back troppo lontano', r.b);
r = await api('a', 'GET', '/api/v1/wrap?tz=99999');
ok(r.s === 400, 'fuso assurdo', r.b);
r = await api('a', 'GET', '/api/v1/wrap?tz=1.5');
ok(r.s === 400, 'fuso non intero', r.b);
r = await api('a', 'GET', '/api/v1/wrap?period=decade');
ok(r.s === 200 && r.b.period === 'week', 'periodo sconosciuto → settimana', r.b.period);

console.log('\n--- la settimana scorsa di a');
r = await api('a', 'GET', `/api/v1/wrap?period=week&back=1&tz=${TZ}`);
const w = r.b;
ok(r.s === 200 && w.source === 'sessions' && w.complete === true, 'dalle sessioni, periodo concluso', { s: r.s, source: w.source });
ok(w.total_seconds === 17100, '4 h 45 min in totale', w.total_seconds);
ok(w.games_count === 2 && w.games[0].game_id === G1 && w.games[0].seconds === 14400 && w.games[0].sessions === 2 && w.games[1].seconds === 2700, 'giochi in ordine con tempo e sessioni', w.games);
ok(w.sessions === 3, '3 sessioni', w.sessions);
ok(w.new_games === 2, 'due giochi nuovi', w.new_games);
ok(w.days_active === 4 && w.streak === 4, '4 giorni, 4 di fila (la notte conta anche martedì)', { d: w.days_active, s: w.streak });
ok(w.longest && w.longest.seconds === 10800 && w.longest.game_id === G1, 'sessione più lunga: 3 h', w.longest);
ok(w.hours && w.hours[23] === 5400 && w.hours[22] === 1800 && w.hours[0] === 3600 && w.hours[1] === 3600 && w.hours[10] === 2700 && w.hours.reduce((s, x) => s + x, 0) === 17100, 'ore del giorno nel fuso giusto', w.hours);
ok(w.weekdays && w.weekdays[0] === 3600 && w.weekdays[1] === 7200 && w.weekdays[2] === 3600 && w.weekdays[3] === 2700, 'giorni della settimana', w.weekdays);
ok(w.persona === 'night_owl', 'profilo: nottambulo', w.persona);
ok(w.together.length === 1 && w.together[0].user.online_id === N.b && w.together[0].seconds === 5400 && w.together[0].game_id === G1, 'giocato insieme: solo l\'amico che lo mostra', w.together);
ok(!JSON.stringify(w).includes(N.c) && !JSON.stringify(w).includes(N.d), 'né l\'amico nascosto né gli sconosciuti compaiono', null);
ok(w.players >= 7 && w.percentile >= 80, 'più di quasi tutti i giocatori della settimana', { p: w.percentile, n: w.players });
ok(w.trophies.count === 2 && w.trophies.g === 1 && w.trophies.b === 1 && w.trophies.s === 0, 'trofei del periodo per grado', w.trophies);
ok(w.trophies.rarest && w.trophies.rarest.name === 'Raro' && w.trophies.rarest.pct === 33 && w.trophies.rarest.grade === 'G', 'trofeo più raro con percentuale', w.trophies.rarest);
ok(w.prev_total_seconds === 0, 'settimana prima vuota', w.prev_total_seconds);

r = await api('a', 'GET', '/api/v1/wrap?period=week&back=1&tz=-300');
ok(r.s === 200 && r.b.total_seconds > 0 && r.b.hours && r.b.hours[16] === 5400 && !r.b.hours[23], 'un altro fuso sposta le ore', r.b.hours);

console.log('\n--- b vede a, c no');
r = await api('b', 'GET', `/api/v1/wrap?period=week&back=1&tz=${TZ}`);
ok(r.s === 200 && r.b.total_seconds === 5400 && r.b.together.length === 1 && r.b.together[0].user.online_id === N.a, 'b ha giocato con a', r.b.together);
r = await api('d', 'GET', `/api/v1/wrap?period=week&back=1&tz=${TZ}`);
ok(r.s === 200 && r.b.together.length === 0, 'd non è amico di nessuno', r.b.together);

console.log('\n--- prima del registro: settimane');
const W3 = bounds('week', 3, TZ);
await q('INSERT INTO lab_playtime_week (account_id, game_id, week_start, seconds) VALUES ($1,$2,$3::date,7200)',
  [id.h, G1, W3.localFrom.toISOString().slice(0, 10)]);
const minStart = (await q('SELECT min(started_at) AS t FROM lab_play_session'))[0].t;
r = await api('h', 'GET', `/api/v1/wrap?period=week&back=3&tz=${TZ}`);
if (new Date(minStart) > W3.from) {
  ok(r.s === 200 && r.b.source === 'weeks' && r.b.total_seconds === 7200 && r.b.hours === null && r.b.streak === null, 'totali dalle settimane, niente ritmo', r.b);
} else ok(r.s === 200, '(registro già più vecchio: controllo delle settimane saltato)', null);

console.log('\n--- profili');
const base = { total_seconds: 36000, hours: null, weekdays: null, longest: null, games: [], games_count: 1, new_games: 0, trophies: { count: 0 }, streak: 0, together: [] };
ok(persona({ ...base, longest: { seconds: 4 * 3600 } }) === 'marathoner', 'maratoneta', null);
ok(persona({ ...base, games_count: 6 }) === 'explorer', 'esploratore', null);
ok(persona({ ...base, games: [{ seconds: 30000 }] }) === 'loyal', 'fedele a un gioco', null);
ok(persona({ ...base, trophies: { count: 25 } }) === 'hunter', 'cacciatore di trofei', null);
ok(persona({ ...base, streak: 6 }) === 'daily', 'tutti i giorni', null);
ok(persona(base) === 'casual', 'altrimenti: tranquillo', null);

await pool.end();
done();
