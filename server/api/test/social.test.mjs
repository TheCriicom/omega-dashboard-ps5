// Social: bacheca, stato, blocchi, privacy, segnalazioni, suggerimenti,
// gruppi, inviti, tempo di gioco, voce nel party, proxy e moderazione.
import { execFileSync, spawn } from 'node:child_process';
import { createRequire } from 'node:module';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { BASE as B, API_DIR as API, PROXY_DIR as PROXY, DATABASE_URL as DBURL, IP, checker, j, sleep, suffix } from './helpers.mjs';

const require = createRequire(path.join(API, 'package.json'));
const { Pool } = require('pg');
const pool = new Pool({ connectionString: DBURL });
const q = async (sql, args) => (await pool.query(sql, args)).rows;
const { ok, done } = checker();

const sfx = suffix();
// id di gioco unici per esecuzione: i dati dei giri precedenti non falsano i suggerimenti
const GID = (n) => `G${n}-${sfx}`;
const N = Object.fromEntries(['a', 'b', 'c', 'd', 'e', 'adm', 'spam'].map((k) => [k, `${k}${sfx}`]));
const tok = {};
for (const n of Object.values(N)) {
  await j('POST', '/api/v1/auth/register', { online_id: n, password: 'Password123!', accept_terms: true });
  const r = await j('POST', '/api/v1/auth/login', { online_id: n, password: 'Password123!' });
  tok[n] = r.b.token || r.b.access_token;
  ok(!!tok[n], `login ${n}`, r);
}
const H = (k) => ({ authorization: `Bearer ${tok[N[k]]}` });
const api = (k, method, p, body) => j(method, p, body, H(k));
const accId = async (k) => (await q('SELECT account_id FROM lab_account WHERE online_id=$1', [N[k]]))[0].account_id;
const notes = async (k, type) => q(`SELECT n.*, a.online_id AS actor FROM lab_notification n LEFT JOIN lab_account a ON a.account_id=n.actor_id
  WHERE n.account_id=$1 AND ($2::text IS NULL OR type=$2) ORDER BY notification_id`, [await accId(k), type || null]);

async function befriend(x, y) {
  await api(x, 'POST', '/api/v1/friends/request', { online_id: N[y] });
  const r = await api(y, 'POST', '/api/v1/friends/accept', { online_id: N[x] });
  ok(r.s === 200, `amicizia ${x}-${y}`, r);
}
// amicizie: A-B, A-C, B-C, B-D. E estraneo.
await befriend('a', 'b'); await befriend('a', 'c'); await befriend('b', 'c'); await befriend('b', 'd');

// ============================================================ 1. BACHECA ===
console.log('\n--- bacheca');
let r = await api('a', 'POST', '/api/v1/posts', { text: '   ' });
ok(r.s === 400 && r.b.error === 'empty_text', 'post vuoto → 400 empty_text', r);
r = await api('a', 'POST', '/api/v1/posts', { text: 'x'.repeat(501) });
ok(r.s === 400 && r.b.error === 'text_too_long', 'post 501 caratteri → 400', r);
r = await api('a', 'POST', '/api/v1/posts', { text: '  Ciao bacheca!  ', game_id: 'PPSA01', game_name: 'Astro Bot' });
const p1 = r.b.post;
ok(r.s === 201 && p1 && p1.text === 'Ciao bacheca!' && p1.author.online_id === N.a && p1.game_name === 'Astro Bot'
  && p1.likes === 0 && p1.liked === false && p1.comments === 0 && p1.mine === true && 'avatar_media' in p1.author && 'avatar_frames' in p1.author,
'crea post → 201 {post} con tutti i campi', r);
r = await api('b', 'GET', '/api/v1/feed');
ok(r.s === 200 && r.b.posts.some((p) => p.post_id === p1.post_id && p.mine === false) && 'next_before' in r.b, 'feed dell\'amico contiene il post', r);
r = await api('d', 'GET', '/api/v1/feed');
ok(r.s === 200 && !r.b.posts.some((p) => p.post_id === p1.post_id), 'feed di un non amico non lo contiene', r.b);
r = await api('d', 'GET', `/api/v1/users/${N.a}/posts`);
ok(r.s === 403 && r.b.error === 'not_friends', 'posts utente da non amico → 403 not_friends', r);
r = await api('a', 'GET', `/api/v1/users/${N.a}/posts`);
ok(r.s === 200 && r.b.posts.length === 1, 'i miei post', r);
r = await api('b', 'GET', `/api/v1/users/${N.a}/posts`);
ok(r.s === 200 && r.b.posts[0].post_id === p1.post_id, 'post di un amico', r);
r = await api('d', 'POST', `/api/v1/posts/${p1.post_id}/like`, { like: true });
ok(r.s === 403 && r.b.error === 'not_friends', 'like da non amico → 403', r);
r = await api('b', 'POST', `/api/v1/posts/${p1.post_id}/like`, { like: 'si' });
ok(r.s === 400, 'like non booleano → 400', r);
r = await api('b', 'POST', `/api/v1/posts/${p1.post_id}/like`, { like: true });
ok(r.s === 200 && r.b.likes === 1 && r.b.liked === true, 'like → {likes:1, liked:true}', r);
await api('b', 'POST', `/api/v1/posts/${p1.post_id}/like`, { like: false });
r = await api('b', 'POST', `/api/v1/posts/${p1.post_id}/like`, { like: true });
ok(r.b.likes === 1, 'togli e rimetti like', r);
r = await api('a', 'POST', `/api/v1/posts/${p1.post_id}/like`, { like: true });
ok(r.b.likes === 2, 'like al proprio post', r);
let n = await notes('a', 'post_like');
ok(n.length === 1 && n[0].ref === p1.post_id && n[0].actor === N.b, 'una sola notifica post_like (nessuna per sé)', n);
r = await api('b', 'GET', `/api/v1/posts/${p1.post_id}`);
ok(r.s === 200 && r.b.post.liked === true && r.b.post.likes === 2, 'GET post: liked per me', r);
r = await api('b', 'POST', `/api/v1/posts/${p1.post_id}/comments`, { text: 'y'.repeat(301) });
ok(r.s === 400 && r.b.error === 'text_too_long', 'commento 301 → 400', r);
r = await api('b', 'POST', `/api/v1/posts/${p1.post_id}/comments`, { text: 'Bello!' });
const c1 = r.b.comment;
ok(r.s === 201 && c1 && c1.text === 'Bello!' && c1.author.online_id === N.b && c1.mine === true, 'commento → 201 {comment}', r);
r = await api('c', 'POST', `/api/v1/posts/${p1.post_id}/comments`, { text: 'Secondo' });
const c2 = r.b.comment;
r = await api('d', 'POST', `/api/v1/posts/${p1.post_id}/comments`, { text: 'intruso' });
ok(r.s === 403, 'commento da non amico → 403', r);
n = await notes('a', 'post_comment');
ok(n.length === 2 && n[0].ref === p1.post_id && n[0].body === 'Bello!', 'notifica post_comment all\'autore', n);
r = await api('a', 'GET', `/api/v1/posts/${p1.post_id}/comments`);
ok(r.s === 200 && r.b.comments.length === 2 && r.b.comments[0].comment_id === c1.comment_id && r.b.comments[0].mine === false, 'commenti dal più vecchio', r);
r = await api('c', 'DELETE', `/api/v1/posts/${p1.post_id}/comments/${c1.comment_id}`);
ok(r.s === 403, 'cancellare il commento di un altro → 403', r);
r = await api('a', 'DELETE', `/api/v1/posts/${p1.post_id}/comments/${c2.comment_id}`);
ok(r.s === 200, 'l\'autore del post cancella un commento', r);
r = await api('b', 'GET', `/api/v1/feed`);
ok(r.b.posts.find((p) => p.post_id === p1.post_id).comments === 1, 'conteggio commenti nel feed', r.b);
for (let i = 0; i < 3; i++) await api('a', 'POST', '/api/v1/posts', { text: `post ${i}` });
r = await api('b', 'GET', '/api/v1/feed?limit=2');
const nb = r.b.next_before;
ok(r.b.posts.length === 2 && nb === r.b.posts[1].post_id, 'paginazione: next_before', r.b);
r = await api('b', 'GET', `/api/v1/feed?limit=2&before=${nb}`);
ok(r.b.posts.length === 2 && Number(r.b.posts[0].post_id) < Number(nb), 'pagina successiva', r.b);
r = await api('b', 'GET', `/api/v1/feed?limit=50`);
ok(r.b.next_before === null, 'ultima pagina: next_before null', r.b.next_before);
const pDel = (await api('a', 'POST', '/api/v1/posts', { text: 'da cancellare' })).b.post;
r = await api('b', 'DELETE', `/api/v1/posts/${pDel.post_id}`);
ok(r.s === 403 && r.b.error === 'not_author', 'cancellare il post di un altro → 403', r);
r = await api('a', 'DELETE', `/api/v1/posts/${pDel.post_id}`);
ok(r.s === 200, 'l\'autore cancella il post', r);
r = await api('b', 'GET', `/api/v1/posts/${pDel.post_id}`);
ok(r.s === 404, 'post cancellato → 404', r);
for (let i = 0; i < 20; i++) await api('spam', 'POST', '/api/v1/posts', { text: `spam ${i}` });
r = await api('spam', 'POST', '/api/v1/posts', { text: 'troppi' });
ok(r.s === 429, 'limite 20 post/ora → 429', r);

// ============================================================== 2. STATO ===
console.log('\n--- stato personalizzato');
await api('a', 'POST', '/api/v1/presence', { status: 'online', game_id: 'PPSA01', game_name: 'Astro Bot' });
r = await api('b', 'GET', '/api/v1/sync');
let fa = r.b.friends.find((f) => f.online_id === N.a);
ok(fa.presence.status === 'online' && fa.presence.game_id === 'PPSA01' && 'status_message' in fa, 'amico visibile mentre gioca (+status_message)', fa);
r = await api('a', 'POST', '/api/v1/status', { mode: 'boh' });
ok(r.s === 400 && r.b.error === 'invalid_mode', 'mode non valido → 400', r);
r = await api('a', 'POST', '/api/v1/status', { mode: 'dnd', message: 'z'.repeat(61) });
ok(r.s === 400, 'messaggio > 60 → 400', r);
r = await api('a', 'POST', '/api/v1/status', { mode: 'invisible', message: 'Zitto zitto' });
ok(r.s === 200 && r.b.status_mode === 'invisible' && r.b.status_message === 'Zitto zitto', 'stato invisibile', r);
r = await api('b', 'GET', '/api/v1/sync');
fa = r.b.friends.find((f) => f.online_id === N.a);
ok(fa.presence.status === 'offline' && fa.presence.game_id === null && fa.presence.game_name === null, 'invisibile: sync amici → offline senza gioco', fa);
r = await api('b', 'GET', '/api/v1/friends');
fa = r.b.friends.find((f) => f.online_id === N.a);
ok(fa.presence.status === 'offline' && fa.presence.game_id === null && fa.status_message === 'Zitto zitto', 'invisibile: GET /friends → offline (+status_message)', fa);
r = await api('b', 'GET', `/api/v1/users/${N.a}`);
ok(r.b.presence.status === 'offline' && !r.b.presence.game_id && r.b.status_message === 'Zitto zitto', 'invisibile: profilo → offline', r.b);
r = await api('b', 'GET', '/api/v1/presence');
ok(r.b.friends.find((f) => f.online_id === N.a).presence.status === 'offline', 'invisibile: GET /presence → offline', r.b);
r = await api('a', 'GET', '/api/v1/sync');
ok(r.b.me.status_mode === 'invisible' && r.b.me.status_message === 'Zitto zitto' && r.b.me.dnd === false, 'sync me: status_mode/message', r.b.me);
r = await api('a', 'POST', '/api/v1/status', { mode: 'dnd', message: 'Non disturbare' });
r = await api('b', 'GET', '/api/v1/sync');
fa = r.b.friends.find((f) => f.online_id === N.a);
ok(fa.presence.status === 'dnd' && fa.presence.game_id === 'PPSA01' && fa.status_message === 'Non disturbare', 'dnd: amici vedono status dnd + gioco', fa);
r = await api('a', 'GET', '/api/v1/sync');
ok(r.b.me.dnd === true && r.b.me.status_mode === 'dnd', 'sync me.dnd = true', r.b.me);
await api('b', 'POST', `/api/v1/posts/${p1.post_id}/comments`, { text: 'mentre sei in dnd' });
ok((await notes('a', 'post_comment')).length === 3, 'dnd: le notifiche si salvano comunque', null);
r = await api('a', 'POST', '/api/v1/status', { mode: 'away' });
ok(r.b.status_message === 'Non disturbare', 'senza message il messaggio resta', r.b);
r = await api('b', 'GET', '/api/v1/friends');
ok(r.b.friends.find((f) => f.online_id === N.a).presence.status === 'away', 'away riportato come presenza', r.b);
r = await api('a', 'POST', '/api/v1/status', { mode: 'online', message: null });
ok(r.b.status_mode === 'online' && r.b.status_message === null, 'torna online, messaggio cancellato', r.b);

// ========================================================= 3. PRIVACY ===
console.log('\n--- privacy');
r = await api('e', 'GET', '/api/v1/privacy');
ok(r.s === 200 && r.b.messages === 'everyone' && r.b.friend_requests === 'everyone' && r.b.show_activity === true, 'privacy predefinita', r);
r = await api('e', 'POST', '/api/v1/privacy', { messages: 'qualcuno' });
ok(r.s === 400, 'valore non valido → 400', r);
r = await api('e', 'POST', `/api/v1/messages/${N.a}`, { text: 'ciao sconosciuto' });
ok(r.s === 201, 'DM da non amico con messages=everyone → 201', r);
await api('a', 'POST', '/api/v1/privacy', { messages: 'friends' });
r = await api('e', 'POST', `/api/v1/messages/${N.a}`, { text: 'ancora' });
ok(r.s === 403 && r.b.error === 'privacy', 'messages=friends: non amico → 403 privacy', r);
r = await api('b', 'POST', `/api/v1/messages/${N.a}`, { text: 'da amico' });
ok(r.s === 201, 'messages=friends: amico → 201', r);
r = await api('a', 'POST', '/api/v1/privacy', { friend_requests: 'nobody' });
ok(r.b.friend_requests === 'nobody' && r.b.messages === 'friends', 'privacy aggiornata parzialmente', r.b);
r = await api('e', 'POST', '/api/v1/friends/request', { online_id: N.a });
ok(r.s === 403 && r.b.error === 'privacy', 'friend_requests=nobody → 403 privacy', r);
await api('a', 'POST', '/api/v1/privacy', { friend_requests: 'friends_of_friends' });
r = await api('e', 'POST', '/api/v1/friends/request', { online_id: N.a });
ok(r.s === 403 && r.b.error === 'privacy', 'friends_of_friends: senza amici in comune → 403', r);
r = await api('d', 'POST', '/api/v1/friends/request', { online_id: N.a });
ok(r.s === 201 && r.b.result === 'pending', 'friends_of_friends: con amico in comune → pending', r);
await api('a', 'POST', '/api/v1/friends/decline', { online_id: N.d });
await api('a', 'POST', '/api/v1/privacy', { messages: 'everyone', friend_requests: 'everyone' });

// ====================================================== 4. TEMPO DI GIOCO ===
console.log('\n--- tempo di gioco');
const A = await accId('a');
const setPres = (k, game) => api(k, 'POST', '/api/v1/presence', game ? { status: 'online', game_id: game[0], game_name: game[1] } : { status: 'online' });
await setPres('a', [GID(1), 'Gioco Uno']);
let pr = (await q('SELECT play_since FROM lab_presence WHERE account_id=$1', [A]))[0];
ok(pr.play_since, 'presenza con gioco: tratto aperto', pr);
await q(`UPDATE lab_presence SET play_since = date_trunc('milliseconds', now() - interval '600 seconds') WHERE account_id=$1`, [A]);
await setPres('a', [GID(2), 'Gioco Due']);
let pt = await q('SELECT * FROM lab_playtime WHERE account_id=$1 ORDER BY game_id', [A]);
const g1 = pt.find((x) => x.game_id === GID(1));
ok(g1 && Number(g1.seconds) >= 599 && Number(g1.seconds) <= 602 && g1.sessions === 1 && g1.game_name === 'Gioco Uno', 'cambio gioco: chiuso G1 (~600 s)', pt);
await setPres('a', null);   // G2 durato < 30 s → ignorato
pt = await q('SELECT * FROM lab_playtime WHERE account_id=$1', [A]);
ok(!pt.some((x) => x.game_id === GID(2)), 'tratto < 30 s ignorato', pt);
await setPres('a', [GID(3), 'Gioco Tre']);
await q(`UPDATE lab_presence SET play_since = date_trunc('milliseconds', now() - interval '20 hours') WHERE account_id=$1`, [A]);
await api('a', 'POST', '/api/v1/presence', { status: 'offline' });
pt = await q(`SELECT seconds FROM lab_playtime WHERE account_id=$1 AND game_id='${GID(3)}'`, [A]);
ok(pt.length && Number(pt[0].seconds) === 43200, 'tratto limitato a 12 h (offline lo chiude)', pt);
// console sparita: il sync successivo chiude all'ultimo last_seen
await setPres('a', [GID(4), 'Gioco Quattro']);
await q(`UPDATE lab_presence SET play_since = date_trunc('milliseconds', now() - interval '1000 seconds'), last_seen = now() - interval '300 seconds' WHERE account_id=$1`, [A]);
await api('a', 'GET', '/api/v1/sync');
pt = await q(`SELECT seconds FROM lab_playtime WHERE account_id=$1 AND game_id='${GID(4)}'`, [A]);
pr = (await q('SELECT play_since, last_seen FROM lab_presence WHERE account_id=$1', [A]))[0];
ok(pt.length && Math.abs(Number(pt[0].seconds) - 700) <= 2 && pr.play_since && Date.now() - new Date(pr.play_since) < 5000, 'sync dopo un buco: chiude a last_seen (~700 s) e riapre', { pt, pr });
// giro periodico
await q(`UPDATE lab_presence SET play_since = date_trunc('milliseconds', now() - interval '500 seconds'), last_seen = now() - interval '200 seconds' WHERE account_id=$1`, [A]);
const playtime = require(path.join(API, 'src/playtime.js'));
const closed = await playtime.sweep();
pt = await q(`SELECT seconds, sessions FROM lab_playtime WHERE account_id=$1 AND game_id='${GID(4)}'`, [A]);
pr = (await q('SELECT play_since FROM lab_presence WHERE account_id=$1', [A]))[0];
ok(closed >= 1 && Math.abs(Number(pt[0].seconds) - 1000) <= 3 && pt[0].sessions === 2 && pr.play_since === null, 'sweep: chiude i tratti di console sparite da > 3 min', { closed, pt, pr });
await playtime.sweep();
pt = await q(`SELECT seconds FROM lab_playtime WHERE account_id=$1 AND game_id='${GID(4)}'`, [A]);
ok(Math.abs(Number(pt[0].seconds) - 1000) <= 3, 'sweep ripetuto non conta due volte', pt);
r = await api('a', 'GET', '/api/v1/stats/me');
ok(r.s === 200 && r.b.games[0].game_id === GID(3) && r.b.total_seconds >= 43200 + 600 + 1000 - 5 && r.b.week_seconds === r.b.total_seconds
  && r.b.games.every((g) => 'week_seconds' in g && 'sessions' in g && 'last_played' in g), 'stats/me', r.b);
// B gioca a G1 (tratto in corso conteggiato dal vivo) — D (amico di B) per i suggerimenti "stessi giochi"
await setPres('b', [GID(1), 'Gioco Uno']);
await q(`UPDATE lab_presence SET play_since = date_trunc('milliseconds', now() - interval '120 seconds') WHERE account_id=$1`, [await accId('b')]);
await setPres('e', [GID(1), 'Gioco Uno']);
await q(`UPDATE lab_presence SET play_since = date_trunc('milliseconds', now() - interval '90 seconds') WHERE account_id=$1`, [await accId('e')]);
await setPres('e', null);
r = await api('a', 'GET', `/api/v1/stats/friends?game_id=${GID(1)}&period=week`);
ok(r.s === 200 && r.b.period === 'week' && r.b.game_id === GID(1) && r.b.ranking[0].user.online_id === N.a && r.b.ranking[0].rank === 1 && r.b.ranking[0].me === true
  && r.b.ranking[1] && r.b.ranking[1].user.online_id === N.b && r.b.ranking[1].seconds >= 119 && !r.b.ranking.some((x) => x.user.online_id === N.e), 'stats/friends per gioco (B dal vivo, E escluso)', r.b);
r = await api('c', 'GET', '/api/v1/stats/friends');
ok(r.b.period === 'all' && r.b.game_id === null && r.b.ranking.some((x) => x.me && x.seconds === 0) && r.b.ranking[0].user.online_id === N.a, 'stats/friends totale (io sempre presente)', r.b);
r = await api('b', 'GET', `/api/v1/users/${N.a}`);
ok(r.b.stats && r.b.stats.total_seconds >= 44800 && r.b.stats.top_games.length === 3 && r.b.stats.top_games[0].game_id === GID(3), 'profilo: stats con top_games', r.b.stats);

// ===================================================== 5. SUGGERIMENTI ===
console.log('\n--- suggerimenti e profilo');
r = await api('a', 'GET', '/api/v1/friends/suggestions');
const sd = r.b.users && r.b.users.find((u) => u.online_id === N.d);
const se = r.b.users && r.b.users.find((u) => u.online_id === N.e);
ok(r.s === 200 && sd && sd.mutual === 1 && sd.reason === 'amici_in_comune' && 'avatar_frames' in sd, 'suggerito D (amico di B) per amici in comune', r.b);
ok(se && se.reason === 'stessi_giochi' && se.mutual === 0, 'suggerito E per stessi giochi', r.b);
ok(!r.b.users.some((u) => [N.a, N.b, N.c].includes(u.online_id)) && r.b.users.length <= 15, 'esclusi io e gli amici', r.b);
await api('d', 'POST', '/api/v1/privacy', { friend_requests: 'nobody' });
r = await api('a', 'GET', '/api/v1/friends/suggestions');
ok(!r.b.users.some((u) => u.online_id === N.d), 'escluso chi non accetta richieste', r.b);
await api('d', 'POST', '/api/v1/privacy', { friend_requests: 'everyone' });
await api('e', 'POST', '/api/v1/friends/request', { online_id: N.a });
r = await api('a', 'GET', '/api/v1/friends/suggestions');
ok(!r.b.users.some((u) => u.online_id === N.e), 'esclusa la richiesta in sospeso', r.b);
await api('a', 'POST', '/api/v1/friends/decline', { online_id: N.e });
r = await api('a', 'GET', `/api/v1/users/${N.d}`);
ok(r.s === 200 && r.b.mutual_friends === 1 && r.b.mutual.length === 1 && r.b.mutual[0].online_id === N.b && r.b.blocked === false
  && 'status_message' in r.b && r.b.stats.hidden === true && r.b.stats.total_seconds === 0, 'profilo: amici in comune, blocked, stats nascoste ai non amici', r.b);
await api('b', 'POST', '/api/v1/privacy', { show_activity: false });
r = await api('a', 'GET', `/api/v1/users/${N.b}`);
ok(r.b.stats.hidden === true && r.b.recent.length === 0 && r.b.games.length === 0, 'show_activity=false: stats/attività nascoste agli amici', r.b);
r = await api('b', 'GET', `/api/v1/users/${N.b}`);
ok(!r.b.stats.hidden && r.b.recent.length > 0, '... ma non a sé stesso', r.b.stats);
r = await api('a', 'GET', '/api/v1/activity');
ok(!r.b.activity.some((x) => x.online_id === N.b) && r.b.activity.some((x) => x.online_id === N.a), 'show_activity=false: fuori da /activity degli altri', r.b.activity.map((x) => x.online_id));
r = await api('a', 'GET', `/api/v1/stats/friends?game_id=${GID(1)}`);
ok(!r.b.ranking.some((x) => x.user.online_id === N.b), 'show_activity=false: fuori dalle classifiche', r.b);
await api('b', 'POST', '/api/v1/privacy', { show_activity: true });

// ========================================================= 6. GRUPPI ===
console.log('\n--- gruppi');
r = await api('a', 'POST', '/api/v1/groups', { name: '', members: [N.b] });
ok(r.s === 400 && r.b.error === 'invalid_name', 'nome vuoto → 400', r);
r = await api('a', 'POST', '/api/v1/groups', { name: 'Squadra', members: [] });
ok(r.s === 400 && r.b.error === 'invalid_members', 'nessun membro → 400', r);
r = await api('a', 'POST', '/api/v1/groups', { name: 'Squadra', members: [N.e] });
ok(r.s === 403 && r.b.error === 'not_friends', 'membro non amico → 403', r);
r = await api('a', 'POST', '/api/v1/groups', { name: 'Squadra', members: [N.b, N.c] });
const G = r.b.group;
ok(r.s === 201 && G && G.name === 'Squadra' && G.owner === N.a && G.member_count === 3 && G.members[0].online_id === N.a && G.unread === 0
  && G.last_message && G.last_message.from === N.a, 'crea gruppo → 201 {group}', r);
r = await api('b', 'GET', '/api/v1/sync');
ok(r.b.unread_groups === 1, 'sync unread_groups (messaggio di sistema)', r.b.unread_groups);
r = await api('b', 'GET', '/api/v1/groups');
ok(r.b.groups[0].group_id === G.group_id && r.b.groups[0].unread === 1, 'GET /groups con unread', r.b);
r = await api('b', 'GET', `/api/v1/groups/${G.group_id}/messages`);
ok(r.s === 200 && r.b.messages.length === 1 && r.b.messages[0].system === true && r.b.messages[0].from.online_id === N.a && r.b.messages[0].mine === false, 'messaggi: sistema', r.b);
r = await api('b', 'GET', '/api/v1/sync');
ok(r.b.unread_groups === 0, 'letti → unread_groups 0', r.b.unread_groups);
r = await api('b', 'POST', `/api/v1/groups/${G.group_id}/messages`, { text: '' });
ok(r.s === 400, 'messaggio vuoto → 400', r);
r = await api('b', 'POST', `/api/v1/groups/${G.group_id}/messages`, { text: 'Ciao gruppo' });
const gm1 = r.b.message;
ok(r.s === 201 && gm1 && gm1.text === 'Ciao gruppo' && gm1.mine === true && gm1.system === false && gm1.from.online_id === N.b, 'invia messaggio → 201', r);
r = await api('a', 'GET', '/api/v1/groups');
ok(r.b.groups[0].unread === 1 && r.b.groups[0].last_message.text === 'Ciao gruppo' && r.b.groups[0].last_message.from === N.b, 'A: unread 1 e last_message', r.b);
r = await api('a', 'GET', `/api/v1/groups/${G.group_id}/messages?after=${gm1.message_id}`);
ok(r.b.messages.length === 0, 'after=<ultimo> → vuoto', r.b);
ok(!(await notes('a')).some((x) => /group/.test(x.type)), 'nessuna notifica per i messaggi di gruppo', null);
r = await api('e', 'GET', `/api/v1/groups/${G.group_id}/messages`);
ok(r.s === 403 && r.b.error === 'not_member', 'non membro → 403 not_member', r);
r = await api('b', 'POST', `/api/v1/groups/${G.group_id}/members`, { add: [N.d] });
ok(r.s === 200 && r.b.added[0] === N.d && r.b.group.member_count === 4, 'B aggiunge un suo amico (D)', r);
r = await api('c', 'POST', `/api/v1/groups/${G.group_id}/members`, { add: [N.e] });
ok(r.s === 403, 'aggiungere un non amico → 403', r);
r = await api('c', 'POST', `/api/v1/groups/${G.group_id}`, { name: 'Hack' });
ok(r.s === 403 && r.b.error === 'not_owner', 'rinomina da non proprietario → 403', r);
r = await api('a', 'POST', `/api/v1/groups/${G.group_id}`, { name: 'Squadra 2' });
ok(r.s === 200 && r.b.group.name === 'Squadra 2', 'il proprietario rinomina', r);
r = await api('b', 'DELETE', `/api/v1/groups/${G.group_id}/members/${N.c}`);
ok(r.s === 403, 'rimuovere un membro da non proprietario → 403', r);
r = await api('a', 'DELETE', `/api/v1/groups/${G.group_id}/members/${N.d}`);
ok(r.s === 200 && r.b.group.member_count === 3, 'il proprietario rimuove D', r);
r = await api('c', 'GET', `/api/v1/groups/${G.group_id}/messages`);
const sys = r.b.messages.filter((m) => m.system).map((m) => m.text);
ok(sys.some((t) => t.includes('ha aggiunto ' + N.d)) && sys.some((t) => t.includes('rinominato')) && sys.some((t) => t.includes('ha rimosso ' + N.d)), 'messaggi di sistema', sys);

// ========================================================= 7. BLOCCHI ===
console.log('\n--- blocchi');
await api('c', 'POST', `/api/v1/groups/${G.group_id}/messages`, { text: 'messaggio di C' });
const pB = (await api('b', 'POST', '/api/v1/posts', { text: 'post di B' })).b.post;
await api('c', 'POST', `/api/v1/posts/${pB.post_id}/comments`, { text: 'commento di C' });
const pC = (await api('c', 'POST', '/api/v1/posts', { text: 'post di C' })).b.post;
r = await api('a', 'GET', '/api/v1/feed?limit=50');
ok(r.b.posts.some((p) => p.post_id === pC.post_id), 'prima del blocco A vede il post di C', null);
r = await api('a', 'POST', `/api/v1/users/${N.a}/block`);
ok(r.s === 400, 'bloccare sé stessi → 400', r);
r = await api('a', 'POST', `/api/v1/users/${N.c}/block`);
ok(r.s === 200 && r.b.result === 'blocked', 'A blocca C', r);
r = await api('a', 'GET', '/api/v1/blocks');
ok(r.b.users.length === 1 && r.b.users[0].online_id === N.c && 'avatar_media' in r.b.users[0], 'GET /blocks', r.b);
r = await api('a', 'GET', '/api/v1/friends');
ok(!r.b.friends.some((f) => f.online_id === N.c), 'il blocco chiude l\'amicizia', r.b);
r = await api('c', 'POST', '/api/v1/friends/request', { online_id: N.a });
ok(r.s === 403 && r.b.error === 'blocked', 'C → richiesta di amicizia: 403 blocked', r);
r = await api('a', 'POST', '/api/v1/friends/request', { online_id: N.c });
ok(r.s === 403 && r.b.error === 'blocked', 'A → richiesta di amicizia: 403 blocked', r);
r = await api('c', 'POST', `/api/v1/messages/${N.a}`, { text: 'ehi' });
ok(r.s === 403 && r.b.error === 'blocked', 'DM bloccato → 403 blocked', r);
await api('c', 'POST', '/api/v1/party', { name: 'Party di C' });
r = await api('c', 'POST', '/api/v1/party/invite', { online_id: N.a });
ok(r.s === 403 && r.b.error === 'blocked', 'invito al party bloccato → 403', r);
await api('c', 'POST', '/api/v1/party/leave');
r = await api('c', 'POST', '/api/v1/invites', { to: [N.a], game_id: GID(1), game_name: 'Gioco Uno' });
ok(r.s === 403 && r.b.error === 'blocked', 'invito a giocare bloccato → 403', r);
r = await api('c', 'GET', `/api/v1/users/search?q=${N.a}`);
ok(!r.b.users.some((u) => u.online_id === N.a), 'ricerca: C non trova A', r.b);
r = await api('a', 'GET', `/api/v1/users/search?q=${N.c}`);
ok(!r.b.users.some((u) => u.online_id === N.c), 'ricerca: A non trova C', r.b);
r = await api('c', 'GET', `/api/v1/users/${N.a}`);
ok(r.s === 403 && r.b.error === 'blocked', 'C apre il profilo di A → 403 blocked', r);
r = await api('a', 'GET', `/api/v1/users/${N.c}`);
ok(r.s === 200 && r.b.blocked === true && r.b.relation === 'blocked', 'A apre il profilo di C → blocked:true', r.b);
r = await api('c', 'GET', `/api/v1/users/${N.a}/posts`);
ok(r.s === 403 && r.b.error === 'blocked', 'post di chi mi ha bloccato → 403 blocked', r);
r = await api('c', 'GET', `/api/v1/posts/${p1.post_id}`);
ok(r.s === 403 && r.b.error === 'blocked', 'singolo post → 403 blocked', r);
r = await api('a', 'GET', `/api/v1/posts/${pB.post_id}/comments`);
ok(!r.b.comments.some((c) => c.author.online_id === N.c), 'commenti di C nascosti ad A sul post di B', r.b);
r = await api('a', 'GET', '/api/v1/feed?limit=50');
ok(!r.b.posts.some((p) => p.author.online_id === N.c) && r.b.posts.find((p) => p.post_id === pB.post_id).comments === 0, 'feed: niente da C, conteggio commenti senza C', null);
r = await api('b', 'GET', `/api/v1/posts/${pB.post_id}/comments`);
ok(r.b.comments.some((c) => c.author.online_id === N.c), '... B li vede ancora', r.b);
r = await api('a', 'GET', `/api/v1/groups/${G.group_id}/messages`);
ok(r.s === 200 && !r.b.messages.some((m) => m.from && m.from.online_id === N.c), 'gruppo: A non vede i messaggi di C (resta membro)', r.b.messages.map((m) => m.from && m.from.online_id));
r = await api('a', 'GET', '/api/v1/groups');
ok(r.b.groups[0].members.some((m) => m.online_id === N.c), 'C resta nel gruppo', null);
await api('c', 'POST', `/api/v1/groups/${G.group_id}/messages`, { text: 'altro di C' });
r = await api('a', 'GET', '/api/v1/sync');
ok(r.b.unread_groups === 0, 'messaggi di C non contano nei non letti di A', r.b.unread_groups);
r = await api('a', 'POST', `/api/v1/groups/${G.group_id}/members`, { add: [N.c] });
ok(r.s === 403 && r.b.error === 'blocked', 'aggiungere un bloccato → 403 blocked', r);
r = await api('a', 'DELETE', `/api/v1/users/${N.c}/block`);
ok(r.s === 200, 'sblocca', r);
r = await api('a', 'GET', '/api/v1/blocks');
ok(r.b.users.length === 0, 'lista blocchi vuota', r.b);
await befriend('a', 'c');

// ===================================================== 8. SEGNALAZIONI ===
console.log('\n--- segnalazioni');
console.log(execFileSync('node', ['src/admin.js', 'grant', N.adm], { cwd: API, env: { ...process.env, DATABASE_URL: DBURL } }).toString().trim());
r = await api('a', 'POST', `/api/v1/users/${N.e}/report`, { reason: 'boh' });
ok(r.s === 400 && r.b.error === 'invalid_reason', 'motivo non valido → 400', r);
r = await api('a', 'POST', `/api/v1/users/${N.a}/report`, { reason: 'spam' });
ok(r.s === 400 && r.b.error === 'cannot_report_own', 'segnalare sé stessi → 400', r);
r = await api('a', 'POST', `/api/v1/users/${N.e}/report`, { reason: 'impersonificazione', note: 'finge di essere un altro' });
ok(r.s === 201 && r.b.result === 'reported' && r.b.hidden === false, 'segnala utente → 201', r);
r = await api('a', 'POST', `/api/v1/posts/${pB.post_id}/report`, { reason: 'impersonificazione' });
ok(r.s === 400, 'motivo utente non valido per un post → 400', r);
r = await api('e', 'POST', `/api/v1/posts/${pB.post_id}/report`, { reason: 'spam' });
ok(r.s === 403, 'segnalare un post non visibile → 403', r);
for (const k of ['a', 'c']) {
  r = await api(k, 'POST', `/api/v1/posts/${pB.post_id}/report`, { reason: 'spam' });
  ok(r.s === 201 && r.b.hidden === false, `segnalazione post da ${k}`, r);
}
r = await api('a', 'POST', `/api/v1/posts/${pB.post_id}/report`, { reason: 'spam' });
ok(r.s === 201 && r.b.hidden === false, 'segnalazione ripetuta non conta due volte', r);
r = await api('d', 'POST', `/api/v1/posts/${pB.post_id}/report`, { reason: 'molestie' });
ok(r.s === 201 && r.b.hidden === true, 'terza persona → post oscurato', r);
r = await api('a', 'GET', `/api/v1/posts/${pB.post_id}`);
ok(r.s === 404, 'post oscurato non visibile', r);
const cX = (await api('a', 'POST', `/api/v1/posts/${p1.post_id}/comments`, { text: 'commento cattivo' })).b.comment;
for (const k of ['b', 'c']) await api(k, 'POST', `/api/v1/posts/${p1.post_id}/comments/${cX.comment_id}/report`, { reason: 'contenuto_offensivo' });
r = await api('a', 'POST', `/api/v1/posts/${p1.post_id}/comments/${cX.comment_id}/report`, { reason: 'spam' });
ok(r.s === 400 && r.b.error === 'cannot_report_own', 'segnalare il proprio commento → 400', r);
let an = await notes('adm', 'admin_social_report');
ok(an.length >= 6 && an.some((x) => x.ref === `post:${pB.post_id}` && /oscurato/.test(x.body)), 'notifiche agli admin', an.map((x) => x.ref));

// ========================================================= 9. INVITI ===
console.log('\n--- inviti a giocare');
r = await api('a', 'POST', '/api/v1/invites', { to: [N.b, N.c], game_id: 'PPSA01', game_name: 'Astro Bot', text: 'Dai che vinciamo' });
ok(r.s === 201 && r.b.to.length === 2, 'invito → 201', r);
n = await notes('b', 'game_invite');
ok(n.length === 1 && n[0].title === `${N.a} ti invita a giocare` && n[0].body === 'Astro Bot — Dai che vinciamo' && n[0].ref === 'PPSA01' && n[0].actor === N.a, 'notifica game_invite', n);
r = await api('a', 'POST', '/api/v1/invites', { to: [N.b, N.e], game_id: 'PPSA01', game_name: 'Astro Bot' });
ok(r.s === 403 && r.b.error === 'not_friends', 'invito a un non amico → 403 (nessuno inviato)', r);
ok((await notes('b', 'game_invite')).length === 1, '... e B non riceve un secondo invito', null);
r = await api('a', 'POST', '/api/v1/invites', { to: Array.from({ length: 17 }, (_, i) => `x${i}`), game_id: 'PPSA01', game_name: 'x' });
ok(r.s === 400, 'più di 16 destinatari → 400', r);
r = await api('a', 'POST', '/api/v1/invites', { to: [N.b], game_id: 'PPSA01', game_name: 'x', text: 'w'.repeat(121) });
ok(r.s === 400, 'testo > 120 → 400', r);

// =========================================================== 10. VOCE ===
console.log('\n--- voce nel party');
const vpost = async (k, buf, qs = 'codec=opus&seq=1', base = B) => {
  const res = await fetch(`${base}/api/v1/party/voice?${qs}`, { method: 'POST', headers: { ...IP, ...H(k), 'content-type': 'application/octet-stream' }, body: buf });
  const t = await res.text(); let b; try { b = JSON.parse(t); } catch { b = t; }
  return { s: res.status, b };
};
function decode(buf) {
  if (buf.subarray(0, 4).toString('ascii') !== 'OVC1') return null;
  const out = { next: buf.readUInt32LE(4), chunks: [] };
  const count = buf.readUInt16LE(8);
  let o = 10;
  for (let i = 0; i < count; i++) {
    const l = buf.readUInt8(o); o += 1;
    const from = buf.subarray(o, o + l).toString('utf8'); o += l;
    const codec = buf.readUInt8(o); o += 1;
    const seq = buf.readUInt32LE(o); o += 4;
    const len = buf.readUInt16LE(o); o += 2;
    out.chunks.push({ from, codec, seq, data: buf.subarray(o, o + len) }); o += len;
  }
  out.rest = buf.length - o;
  return out;
}
const vget = async (k, after, wait = 1, base = B, signal) => {
  const t0 = Date.now();
  const res = await fetch(`${base}/api/v1/party/voice?after=${after}&wait=${wait}`, { headers: { ...IP, ...H(k) }, signal });
  const buf = Buffer.from(await res.arrayBuffer());
  return { s: res.status, ct: res.headers.get('content-type'), d: res.status === 200 ? decode(buf) : null, ms: Date.now() - t0, raw: buf };
};
await api('a', 'POST', '/api/v1/party', { name: 'Party voce' });
await api('a', 'POST', '/api/v1/party/invite', { online_id: N.b });
const pid = (await api('b', 'GET', '/api/v1/party')).b.invites[0].party_id;
r = await api('b', 'POST', '/api/v1/party/join', { party_id: pid });
ok(r.s === 200, 'B entra nel party', r);
const audio = Buffer.from(Array.from({ length: 160 }, (_, i) => (i * 7) & 255));
r = await vpost('d', audio);
ok(r.s === 403 && r.b.error === 'not_in_party', 'POST voce senza party → 403', r);
let v = await vget('d', 0);
ok(v.s === 403, 'GET voce senza party → 403', v.s);
r = await vpost('a', audio, 'codec=mp3&seq=1');
ok(r.s === 400 && r.b.error === 'invalid_codec', 'codec non valido → 400', r);
r = await vpost('a', audio, 'codec=opus&seq=-1');
ok(r.s === 400 && r.b.error === 'invalid_seq', 'seq non valido → 400', r);
r = await vpost('a', Buffer.alloc(8193));
ok(r.s === 413, 'pezzo > 8 KB → 413', r);
r = await vpost('a', audio, 'codec=opus&seq=1');
ok(r.s === 204, 'POST pezzo → 204', r);
v = await vget('b', 0);
ok(v.s === 200 && v.ct === 'application/octet-stream' && v.d && v.d.next === 1 && v.d.chunks.length === 0 && v.d.rest === 0 && v.ms < 500, 'after=0 → solo il cursore, niente audio vecchio', v);
const cur = v.d.next;
const waiting = vget('b', cur, 1);
await sleep(300);
r = await vpost('a', audio, 'codec=adpcm&seq=4294967295');
v = await waiting;
ok(v.s === 200 && v.d.chunks.length === 1 && v.d.chunks[0].from === N.a && v.d.chunks[0].codec === 2 && v.d.chunks[0].seq === 4294967295
  && v.d.chunks[0].data.equals(audio) && v.d.next === cur + 1 && v.ms >= 250 && v.ms < 1200 && v.d.rest === 0, 'long-poll: si sveglia appena arriva un pezzo', { ms: v.ms, d: v.d && { ...v.d, chunks: v.d.chunks.map((c) => ({ ...c, data: c.data.length })) } });
r = await api('b', 'GET', '/api/v1/party');
ok(Array.isArray(r.b.party.talking) && r.b.party.talking.includes(N.a), 'GET /party: talking', r.b.party.talking);
r = await api('b', 'GET', '/api/v1/sync');
ok(r.b.party && Array.isArray(r.b.party.talking), 'sync party: talking', r.b.party);
v = await vget('a', cur, 1);
ok(v.s === 200 && v.d.chunks.length === 0 && v.ms >= 1400 && v.ms < 2500 && v.d.next === cur + 1, 'i propri pezzi esclusi; attesa massima ~1,5 s', { ms: v.ms, d: v.d });
v = await vget('b', 0, 0);
r = await vpost('b', audio, 'codec=opus&seq=9');
v = await vget('a', cur, 0);
ok(v.d.chunks.length === 1 && v.d.chunks[0].from === N.b && v.d.chunks[0].seq === 9, 'A riceve il pezzo di B (wait=0)', v.d);
v = await vget('a', 999999, 1);
ok(v.d.chunks.length === 0 && v.d.next <= cur + 3 && v.ms < 500, 'cursore dal futuro → riparte dal cursore attuale', v);
await api('a', 'POST', '/api/v1/party/mute', { muted: true });
r = await vpost('a', audio);
ok(r.s === 403 && r.b.error === 'muted', 'muto → 403 muted', r);
await api('a', 'POST', '/api/v1/party/mute', { muted: false });
// disconnessione del client a metà attesa
const ac = new AbortController();
const aborted = vget('b', v.d.next, 1, B, ac.signal).catch((e) => e.name);
await sleep(150); ac.abort();
ok((await aborted) === 'AbortError', 'client che si scollega durante il long-poll', null);
r = await j('GET', '/healthz');
ok(r.s === 200, 'api ancora in salute dopo la disconnessione', r);
let r429 = 0;
await Promise.all(Array.from({ length: 40 }, (_, i) => vpost('b', audio, `codec=opus&seq=${100 + i}`).then((x) => { if (x.s === 429) r429++; })));
ok(r429 > 0, `limite ~30 pezzi/s (${r429} rifiutati)`, r429);

// ========================================================= 11. PROXY ===
console.log('\n--- proxy');
const LOGDIR = fs.mkdtempSync(path.join(os.tmpdir(), 'omega-proxy-logs-'));
const proxy = spawn('node', ['src/server.js'], { cwd: PROXY, env: { ...process.env, PROXY_PORT: '19987', UPSTREAM_URL: process.env.OMEGA_TEST_URL || 'http://127.0.0.1:18080', LOG_DIR: LOGDIR }, stdio: 'ignore' });
await sleep(700);
const P = 'http://127.0.0.1:19987';
try {
  await sleep(1100); // nuova finestra del limite
  const big = Buffer.from(Array.from({ length: 8000 }, (_, i) => (i * 31 + 7) & 255));
  v = await vget('b', 0, 0, P);
  const pc = v.d.next;
  const w = vget('b', pc, 1, P);
  await sleep(200);
  r = await vpost('a', big, 'codec=opus&seq=77', P);
  ok(r.s === 204, 'POST binario attraverso il proxy', r);
  v = await w;
  ok(v.s === 200 && v.d.chunks.length === 1 && v.d.chunks[0].data.equals(big) && v.d.chunks[0].seq === 77, 'long-poll binario attraverso il proxy: dati intatti', { s: v.s, n: v.d && v.d.chunks.length });
  r = await j('GET', '/api/v1/feed', undefined, H('a'), P);
  ok(r.s === 200, 'altre richieste attraverso il proxy', r.s);
  const ac2 = new AbortController();
  const ab = vget('b', v.d.next, 1, P, ac2.signal).catch((e) => e.name);
  await sleep(100); ac2.abort(); await ab;
  await fetch(`${P}/lab/v1/party/voice?after=0&wait=0`, { headers: H('b') });
  r = await j('GET', '/lab/v1/feed', undefined, H('a'), P);
  ok(r.s === 200, 'alias /lab/v1 attraverso il proxy', r.s);
  await sleep(300);
  const day = new Date().toISOString().slice(0, 10);
  const log = fs.readFileSync(path.join(LOGDIR, `requests-${day}.jsonl`), 'utf8');
  const paths = log.trim().split('\n').map((l) => JSON.parse(l)).filter((e) => e.path).map((e) => e.path);
  ok(paths.includes('/api/v1/feed') && paths.includes('/lab/v1/feed') && !paths.some((x) => /^\/(api|lab)\/v1\/party\/voice/.test(x)), 'registro: voce esclusa, il resto registrato', paths);
  r = await j('GET', '/healthz', undefined, {}, P);
  ok(r.s === 200, 'proxy in salute', r);
} finally { proxy.kill(); fs.rmSync(LOGDIR, { recursive: true, force: true }); }

// ========================================================= 12. ADMIN ===
console.log('\n--- admin');
r = await fetch(`${B}/admin/api/login`, { method: 'POST', headers: { 'content-type': 'application/json', 'x-omega-admin': '1', ...IP }, body: JSON.stringify({ online_id: N.adm, password: 'Password123!' }) });
const cookie = (r.headers.get('set-cookie') || '').split(';')[0];
ok(r.status === 200 && cookie, 'login admin', r.status);
const AD = { cookie, 'x-omega-admin': '1' };
r = await j('GET', '/admin/api/stats', undefined, AD);
ok(r.s === 200 && r.b.posts > 0 && r.b.hidden_posts >= 1 && r.b.open_social_reports >= 1 && r.b.groups >= 1, 'stats: posts, hidden_posts, open_social_reports, groups', r.b);
r = await j('GET', '/admin/api/social-reports', undefined, AD);
const reps = r.b.reports || [];
const rPost = reps.find((x) => x.target_type === 'post' && x.target_id === pB.post_id);
const rUser = reps.find((x) => x.target_type === 'user' && x.owner === N.e);
const rCom = reps.find((x) => x.target_type === 'post_comment' && x.target_id === cX.comment_id);
ok(r.s === 200 && rPost && rPost.hidden === true && rPost.text === 'post di B' && rPost.open_for_target === 3 && rUser && rCom && rCom.post_id === p1.post_id, 'GET social-reports', reps.slice(0, 4));
r = await j('POST', `/admin/api/social-reports/${rUser.report_id}`, { action: 'hide' }, AD);
ok(r.s === 400, 'hide su un utente → 400', r);
r = await j('POST', `/admin/api/social-reports/${rUser.report_id}`, { action: 'resolve' }, { cookie });
ok(r.s === 403 && r.b.error === 'csrf', 'senza X-Omega-Admin → 403 csrf', r);
r = await j('POST', `/admin/api/social-reports/${rUser.report_id}`, { action: 'resolve', note: 'avvisato' }, AD);
ok(r.s === 200, 'resolve su un utente', r);
r = await j('POST', `/admin/api/social-reports/${rCom.report_id}`, { action: 'hide' }, AD);
ok(r.s === 200, 'hide su un commento', r);
r = await api('b', 'GET', `/api/v1/posts/${p1.post_id}/comments`);
ok(!r.b.comments.some((c) => c.comment_id === cX.comment_id), 'commento oscurato sparisce', r.b);
r = await j('POST', `/admin/api/social-reports/${rPost.report_id}`, { action: 'dismiss' }, AD);
ok(r.s === 200, 'dismiss su un post', r);
r = await j('GET', '/admin/api/social-reports?status=dismissed', undefined, AD);
ok(r.b.reports.filter((x) => x.target_id === pB.post_id && x.target_type === 'post').length === 3, 'tutte le segnalazioni del post archiviate', r.b);
r = await j('GET', `/admin/api/posts?status=hidden&q=${encodeURIComponent('post di B')}`, undefined, AD);
ok(r.s === 200 && r.b.total >= 1 && r.b.posts.some((p) => p.post_id === pB.post_id && p.author === N.b), 'GET /admin/api/posts?status=hidden', r.b);
r = await j('POST', `/admin/api/posts/${pB.post_id}`, { action: 'show' }, AD);
ok(r.s === 200, 'ripristina post', r);
r = await api('a', 'GET', `/api/v1/posts/${pB.post_id}`);
ok(r.s === 200, 'post ripristinato visibile', r);
r = await j('POST', `/admin/api/posts/${pB.post_id}`, { action: 'hide', reason: 'prova' }, AD);
ok(r.s === 200, 'oscura post', r);
r = await j('DELETE', `/admin/api/posts/${pB.post_id}`, undefined, AD);
ok(r.s === 200, 'elimina post', r);
r = await j('DELETE', `/admin/api/posts/${pB.post_id}`, undefined, AD);
ok(r.s === 404, 'elimina di nuovo → 404', r);
r = await j('GET', '/admin/api/log?limit=20', undefined, AD);
const acts = r.b.log.map((x) => x.action);
ok(['social_report_resolve', 'social_report_hide', 'social_report_dismiss', 'post_show', 'post_hide', 'post_delete'].every((a) => acts.includes(a)), 'registro admin', acts);
r = await j('GET', '/admin', undefined, {});
ok(r.s === 200 && typeof r.b === 'string' && r.b.includes('Bacheca') && r.b.includes('/admin/api/social-reports'), 'pagina admin con Bacheca e segnalazioni social', r.s);

// ============================================== 13. USCITA DAI GRUPPI ===
console.log('\n--- gruppi: uscita');
r = await api('a', 'POST', `/api/v1/groups/${G.group_id}/leave`);
ok(r.s === 200 && r.b.result === 'left', 'il proprietario esce', r);
r = await api('b', 'GET', `/api/v1/groups/${G.group_id}`);
ok(r.b.group.owner === N.b && r.b.group.member_count === 2, 'proprietà al membro più vecchio (B)', r.b);
r = await api('c', 'GET', `/api/v1/groups/${G.group_id}/messages`);
ok(r.b.messages.some((m) => m.system && m.text.includes(`${N.a} è uscito`)), 'messaggio di sistema "è uscito"', null);
await api('b', 'POST', `/api/v1/groups/${G.group_id}/leave`);
r = await api('c', 'POST', `/api/v1/groups/${G.group_id}/leave`);
ok(r.s === 200 && r.b.result === 'deleted', 'l\'ultimo che esce cancella il gruppo', r);
r = await api('c', 'GET', `/api/v1/groups/${G.group_id}`);
ok(r.s === 404, 'gruppo cancellato → 404', r);
const G2 = (await api('a', 'POST', '/api/v1/groups', { name: 'Flood', members: [N.b] })).b.group;
let gl = 0;
for (let i = 0; i < 31; i++) { const x = await api('a', 'POST', `/api/v1/groups/${G2.group_id}/messages`, { text: `m${i}` }); if (x.s === 429) gl++; }
ok(gl === 1, 'limite 30 messaggi/min → 429', gl);
r = await api('b', 'GET', `/api/v1/groups/${G2.group_id}/messages?limit=5`);
ok(r.b.messages.length === 5 && r.b.messages[4].text === 'm29' && Number(r.b.messages[0].message_id) < Number(r.b.messages[4].message_id), 'senza after: gli ultimi N in ordine crescente', r.b.messages.map((m) => m.text));

await pool.end();
done();
