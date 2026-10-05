// Presenza che si spegne da sola, record pubblici, schede dei giochi, trofei
// importati dalla console e le scelte di privacy che li governano.
import { createRequire } from 'node:module';
import fs from 'node:fs';
import path from 'node:path';
import { BASE as B, API_DIR as API, DATABASE_URL as DBURL, IP, checker, j, suffix } from './helpers.mjs';

const require = createRequire(path.join(API, 'package.json'));
const { Pool } = require('pg');
const pool = new Pool({ connectionString: DBURL });
const q = async (sql, args) => (await pool.query(sql, args)).rows;
const { ok, done } = checker();

const sfx = suffix();
const GID = (n) => `RG${n}${sfx}`.toUpperCase();
const N = Object.fromEntries(['a', 'b', 'c', 'd'].map((k) => [k, `r${k}${sfx}`]));
const tok = {};
for (const n of Object.values(N)) {
  await j('POST', '/api/v1/auth/register', { online_id: n, password: 'Password123!', accept_terms: true });
  tok[n] = (await j('POST', '/api/v1/auth/login', { online_id: n, password: 'Password123!' })).b.token;
  ok(!!tok[n], `login ${n}`);
}
const H = (k) => ({ authorization: `Bearer ${tok[N[k]]}` });
const api = (k, method, p, body) => j(method, p, body, H(k));
const raw = async (k, p, buf) => {
  const r = await fetch(B + p, { method: 'POST', headers: { 'content-type': 'application/octet-stream', ...IP, ...H(k) }, body: buf });
  const t = await r.text();
  let b; try { b = JSON.parse(t); } catch { b = t; }
  return { s: r.status, b };
};
const id = {};
for (const k of Object.keys(N)) id[k] = (await q('SELECT account_id FROM lab_account WHERE online_id=$1', [N[k]]))[0].account_id;
const playtime = require(path.join(API, 'src/playtime.js'));

// ========================================================== 1. PRESENZA ===
console.log('\n--- presenza che si spegne');
await api('a', 'POST', '/api/v1/presence', { status: 'online', game_id: GID(1), game_name: GID(1) });
await q(`UPDATE lab_presence SET last_seen = date_trunc('milliseconds', now() - interval '10 minutes'), play_since = date_trunc('milliseconds', now() - interval '70 minutes') WHERE account_id=$1`, [id.a]);
await playtime.sweep();
let p = (await q('SELECT status, swept, game_id, play_since FROM lab_presence WHERE account_id=$1', [id.a]))[0];
ok(p.status === 'offline' && p.swept === true && p.game_id === null && p.play_since === null, 'console sparita → riga offline, senza gioco', p);
let pt = (await q('SELECT seconds FROM lab_playtime WHERE account_id=$1 AND game_id=$2', [id.a, GID(1)]))[0];
ok(pt && Number(pt.seconds) === 3600, 'il tratto si chiude all\'ultimo segnale (60 min)', pt);
await api('a', 'GET', '/api/v1/sync?since=0');
p = (await q('SELECT status, swept, game_id FROM lab_presence WHERE account_id=$1', [id.a]))[0];
ok(p.status === 'online' && p.swept === false && p.game_id === null, 'al primo sync torna online, senza il gioco di prima', p);
await api('b', 'POST', '/api/v1/presence', { status: 'offline' });
await q(`UPDATE lab_presence SET last_seen = now() - interval '10 minutes' WHERE account_id=$1`, [id.b]);
await playtime.sweep();
await api('b', 'GET', '/api/v1/sync?since=0');
p = (await q('SELECT status, swept FROM lab_presence WHERE account_id=$1', [id.b]))[0];
ok(p.status === 'offline' && p.swept === false, 'l\'offline scelto dall\'utente resta offline dopo il sync', p);

// ============================================================ 2. RECORD ===
console.log('\n--- record pubblici');
const give = (k, g, secs) => q(
  `INSERT INTO lab_playtime (account_id, game_id, game_name, seconds, sessions, last_played) VALUES ($1,$2,$2,$3,1,now())
   ON CONFLICT (account_id, game_id) DO UPDATE SET seconds=EXCLUDED.seconds`, [id[k], GID(g), secs]);
await q('DELETE FROM lab_playtime_week WHERE account_id=$1', [id.a]);
await give('a', 1, 90000000); await give('b', 1, 80000000); await give('b', 2, 30000000); await give('c', 2, 70000000); await give('d', 1, 60000000);
let r = await api('a', 'GET', '/api/v1/records');
// i record sono di tutti gli iscritti: si guardano solo gli account di questo giro
const OURS = new Set(Object.values(N));
const names = (list) => list.map((x) => x.user.online_id).filter((n) => OURS.has(n));
const of = (list, k) => list.find((x) => x.user.online_id === N[k]);
ok(r.s === 200 && names(r.b.players).slice(0, 4).join() === [N.b, N.a, N.c, N.d].join(), 'giocatori in ordine di tempo totale', r.b.players?.slice(0, 5));
let pb = of(r.b.players, 'b');
ok(pb.seconds === 110000000 && pb.games === 2 && pb.game_id === GID(1) && pb.rank < of(r.b.players, 'a').rank, 'totale, numero di giochi e gioco più giocato', pb);
ok(r.b.players.find((x) => x.me)?.user.online_id === N.a && r.b.me.rank === of(r.b.players, 'a').rank && r.b.me.seconds === 90000000 && r.b.me.listed === true, 'la mia posizione', r.b.me);
let g1 = r.b.games.find((g) => g.game_id === GID(1));
ok(g1 && g1.seconds === 230000000 && g1.players === 3 && g1.top.user.online_id === N.a, 'giochi più giocati: totale, giocatori, primatista', g1);
ok(names(r.b.marathons)[0] === N.a && of(r.b.marathons, 'a').game_id === GID(1) && of(r.b.marathons, 'a').seconds === 90000000, 'maratone: record su un solo gioco', r.b.marathons[0]);
ok('avatar_media' in pb.user && 'game_icon' in pb, 'scheda utente e icona del gioco nei record', pb);

// chi non vuole comparire: sparisce il nome, restano i numeri
r = await api('b', 'POST', '/api/v1/privacy', { show_in_records: false });
ok(r.s === 200 && r.b.show_in_records === false && r.b.show_activity === true, 'privacy: show_in_records=false', r);
r = await api('a', 'GET', '/api/v1/records');
ok(!names(r.b.players).includes(N.b) && names(r.b.players)[0] === N.a, 'chi si toglie non compare tra i giocatori', names(r.b.players));
g1 = r.b.games.find((g) => g.game_id === GID(1));
ok(g1.seconds === 230000000 && g1.players === 3, 'ma il totale del gioco non cambia', g1);
ok(!r.b.marathons.some((m) => m.user.online_id === N.b), 'né compare nelle maratone', r.b.marathons.slice(0, 3));
r = await api('b', 'GET', '/api/v1/records');
ok(r.b.me.listed === false && r.b.players.some((x) => x.me && x.user.online_id === N.b), 'lui si vede comunque, e sa di essere nascosto', r.b.me);
r = await api('a', 'GET', `/api/v1/records/games/${GID(1)}`);
ok(r.s === 200 && names(r.b.ranking).join() === [N.a, N.d].join() && r.b.game.seconds === 230000000, 'classifica di un gioco', r.b);
await api('b', 'POST', '/api/v1/privacy', { show_in_records: true });
// chi nasconde l'attività agli amici non può finire in un elenco pubblico
await api('c', 'POST', '/api/v1/privacy', { show_activity: false });
r = await api('a', 'GET', '/api/v1/records');
ok(!names(r.b.players).includes(N.c), 'attività nascosta → fuori dai record', names(r.b.players).slice(0, 4));
await api('c', 'POST', '/api/v1/privacy', { show_activity: true });
// blocco
await api('d', 'POST', `/api/v1/users/${N.a}/block`);
r = await api('a', 'GET', '/api/v1/records');
ok(!names(r.b.players).includes(N.d), 'chi mi ha bloccato non compare', names(r.b.players).slice(0, 4));
await api('d', 'DELETE', `/api/v1/users/${N.a}/block`);
r = await api('a', 'GET', '/api/v1/records?period=week');
ok(r.s === 200 && r.b.period === 'week' && !names(r.b.players).includes(N.b), 'periodo settimana: solo il tempo della settimana', r.b.players?.slice(0, 3));
r = await api('a', 'GET', '/api/v1/records/games/x');
ok(r.s === 404, 'gioco con id non valido → 404', r);

// ============================================================ 3. GIOCHI ===
console.log('\n--- nome e icona dei giochi');
r = await api('a', 'POST', '/api/v1/games/wanted', { ids: [GID(1), GID(2), 'ZZZZ99999', 'no'] });
ok(r.s === 200 && r.b.names.sort().join() === [GID(1), GID(2)].sort().join() && r.b.icons.length === 2, 'mancano nome e icona solo dei giochi giocati', r);
r = await api('a', 'POST', '/api/v1/games/meta', { games: [{ game_id: GID(1), name: '  Astro  Bot ' }, { game_id: 'ZZZZ99999', name: 'Mai giocato' }, { game_id: GID(2), name: GID(2) }] });
ok(r.s === 200 && r.b.saved === 1, 'si salva solo il nome di un gioco giocato e diverso dal codice', r);
r = await api('b', 'POST', '/api/v1/games/meta', { games: [{ game_id: GID(1), name: 'Nome sbagliato' }] });
ok(r.b.saved === 0, 'vale il primo nome', r);
const ucp = fs.readFileSync(path.join(API, 'test', 'trophy00.ucp'));
const entry = (name) => {
  const n = ucp.readUInt32BE(0x10), toc = ucp.readUInt32BE(0x14);
  for (let i = 0; i < n; i++) {
    const b = toc + 0x10 + i * 0x40;
    if (ucp.toString('latin1', b + 0x10, ucp.indexOf(0, b + 0x10)) === name) return ucp.subarray(ucp.readUInt32BE(b + 0x34), ucp.readUInt32BE(b + 0x34) + ucp.readUInt32BE(b + 0x3c));
  }
  return null;
};
const png = entry('icon0_en-US.png');
r = await raw('a', `/api/v1/games/${GID(1)}/icon`, png);
ok(r.s === 201 && /^[a-f0-9]{16}$/.test(r.b.media), 'icona del gioco → 201 {media}', r);
const gameIcon = r.b.media;
r = await raw('b', `/api/v1/games/${GID(1)}/icon`, png);
ok(r.s === 200 && r.b.kept === true && r.b.media === gameIcon, 'vale la prima icona', r);
r = await raw('a', `/api/v1/games/${GID(2)}/icon`, Buffer.from('non è un\'immagine, proprio no'));
ok(r.s === 400 && r.b.error === 'unsupported_type', 'un file che non è un\'immagine → 400', r);
r = await fetch(`${B}/api/v1/media/${gameIcon}/1`, { headers: { ...IP, ...H('a') } });
ok(r.status === 200 && r.headers.get('content-type') === 'image/jpeg', 'l\'icona si scarica come JPEG');
r = await api('a', 'GET', '/api/v1/records');
g1 = r.b.games.find((g) => g.game_id === GID(1));
ok(g1.game_name === 'Astro Bot' && g1.game_icon === gameIcon, 'i record mostrano nome e icona veri', g1);
r = await api('a', 'GET', '/api/v1/stats/me');
ok(r.b.games.find((g) => g.game_id === GID(1))?.game_name === 'Astro Bot', 'anche il mio tempo di gioco', r.b.games?.[0]);
r = await api('a', 'POST', '/api/v1/games/wanted', { ids: [GID(1)] });
ok(r.b.names.length === 0 && r.b.icons.length === 0, 'a scheda completa non si chiede più niente', r);

// ============================================================ 4. TROFEI ===
console.log('\n--- trofei');
const NP = 'NPWR18437_00';
await q('DELETE FROM lab_tset_user WHERE np_id=$1', [NP]); await q('DELETE FROM lab_tset WHERE np_id=$1', [NP]);
const conf = JSON.parse(entry('tropconf.json')), meta = JSON.parse(entry('tropmeta_en-US.json'));
// TRPTITLE.DAT di prova: record numerati a passo fisso, con la data dei trofei ottenuti
const stateFile = (earned) => {
  const b = Buffer.alloc(128 + 8 * 0x60);
  b.write('TRPT', 0);
  for (let i = 0; i < 8; i++) {
    const o = 128 + i * 0x60;
    b.writeUInt32BE(i, o + 0x10);
    if (earned.includes(i)) b.writeBigUInt64BE(BigInt(Math.floor(Date.now() / 1000) - 3600 * (i + 1) + 62135596800) * 1000000n, o + 0x20);
  }
  return b;
};
r = await api('a', 'POST', '/api/v1/trophies/check', { sets: [{ np_id: NP, tag: '896:1' }, { np_id: 'rotto', tag: 'x' }], layout: 'trophy2/nobackup/data/NPWR18437_00/TRPTITLE.DAT 896' });
ok(r.s === 200 && r.b.import === true && r.b.need_def.join() === NP && r.b.need_icon.join() === NP && r.b.need_state.join() === NP, 'check: manca tutto del set nuovo', r);
// lo stato arriva prima della definizione: si conserva e si legge dopo
r = await raw('a', `/api/v1/trophies/${NP}/state?tag=896:1`, stateFile([1, 3, 7]));
ok(r.s === 200 && r.b.parsed === false, 'stato senza definizione: conservato, non ancora letto', r);
r = await api('a', 'POST', `/api/v1/trophies/NPWR00001_00/def`, { conf, meta });
ok(r.s === 400 && r.b.error === 'np_id_mismatch', 'definizione di un altro set → 400', r);
r = await api('a', 'POST', `/api/v1/trophies/${NP}/def`, { conf: { trophies: 'no' } });
ok(r.s === 422, 'definizione non valida → 422', r);
r = await api('a', 'POST', `/api/v1/trophies/${NP}/def`, { conf, meta });
ok(r.s === 201 && r.b.trophies === 8, 'definizione → 201', r);
r = await api('b', 'POST', `/api/v1/trophies/${NP}/def`, { conf, meta: null });
ok(r.s === 200 && r.b.result === 'kept', 'vale la prima definizione', r);
r = await raw('a', `/api/v1/trophies/${NP}/icon`, png);
ok(r.s === 201, 'icona del set → 201', r);
r = await api('a', 'GET', `/api/v1/users/${N.a}/trophies`);
let set = r.b.sets?.[0];
ok(r.s === 200 && set && set.np_id === NP && set.parsed === true && set.earned_count === 3 && set.total_count === 8
  && set.earned.g === 1 && set.earned.b === 2 && set.points === 120 && set.progress === 38 && set.title === 'Unity PSN Sample PSN App' && !!set.icon_media,
'lo stato arrivato prima si legge con la definizione: 1 oro + 2 bronzi', r.b);
ok(r.b.summary.points === 120 && r.b.summary.sets === 1, 'riepilogo dei trofei', r.b.summary);
r = await api('a', 'POST', '/api/v1/trophies/check', { sets: [{ np_id: NP, tag: '896:1' }] });
ok(r.b.need_def.length === 0 && r.b.need_icon.length === 0 && r.b.need_state.length === 0, 'check: niente da mandare se nulla è cambiato', r);
r = await api('a', 'POST', '/api/v1/trophies/check', { sets: [{ np_id: NP, tag: '896:2' }] });
ok(r.b.need_state.join() === NP, 'check: file cambiato → va rimandato', r);
r = await raw('a', `/api/v1/trophies/${NP}/state?tag=896:2`, stateFile([0, 1, 2, 3, 4, 5, 6, 7]));
ok(r.s === 200 && r.b.parsed === true && r.b.earned === 8, 'stato aggiornato: tutti i trofei', r);
r = await raw('b', `/api/v1/trophies/${NP}/state?tag=896:1`, stateFile([3]));
ok(r.b.parsed === true && r.b.earned === 1, 'stato di un secondo utente', r);
r = await raw('c', `/api/v1/trophies/${NP}/state?tag=9:9`, Buffer.alloc(900, 7));
ok(r.s === 200 && r.b.parsed === false, 'file non riconosciuto: conservato, parsed=false', r);
r = await api('b', 'GET', `/api/v1/users/${N.a}/trophies/${NP}`);
ok(r.s === 200 && r.b.trophies.length === 8 && r.b.trophies.every((t) => t.earned && t.earned_at) && r.b.trophies[4].name === 'A Hidden Trophy', 'i trofei di un set, visti da un altro', r.b.trophies?.[4]);
// formato vero della PS5 (T2PD): blocchi da 0x400 + 0x20 di firma, sezione 0x800 a passo 0x60
const t2pd = (earned) => {
  const s = Buffer.alloc(0x400 * 2);
  s.write('T2PD', 0); s.writeUInt32BE(0x420, 0x0c); s.write('T2TD', 0x40); s.writeUInt32BE(1, 0x48);
  const off = 0x200;
  s.writeUInt32BE(0x800, 0x70); s.writeUInt32BE(0x50, 0x74); s.writeUInt32BE(8, 0x7c); s.writeBigUInt64BE(BigInt(off - 0x40), 0x80);
  for (let i = 0; i < 8; i++) {
    const r = off + i * 0x60;
    s.writeUInt32BE(0x800, r); s.writeUInt32BE(0x50, r + 4); s.writeUInt32BE(i, r + 0x10);
    if (earned.includes(i)) { s.writeUInt32BE(0x2000, r + 0x18); s.writeBigUInt64BE(BigInt(1790000000 + i + 62135596800) * 1000000n, r + 0x20); }
  }
  const out = [s.subarray(0, 0x40)];
  for (let o = 0x40; o < s.length; o += 0x400) out.push(s.subarray(o, o + 0x400), Buffer.alloc(0x20, 0xab));
  return Buffer.concat(out);
};
r = await raw('d', `/api/v1/trophies/${NP}/state?tag=t2:1`, t2pd([2, 5, 6]));
ok(r.s === 200 && r.b.parsed === true && r.b.earned === 3, 'formato T2PD della console: letto, 3 trofei', r);
r = await api('d', 'GET', `/api/v1/users/${N.d}/trophies/${NP}`);
ok(r.b.trophies.filter((t) => t.earned).map((t) => t.id).join() === '2,5,6' && r.b.trophies[2].earned_at === new Date(1790000002000).toISOString(), 'T2PD: id e data giusti', r.b.trophies?.[2]);
// un file non letto si rilegge da solo all'avvio dell'api
await q('UPDATE lab_tset_user SET parsed=false, earned=$2, points=0 WHERE account_id=$1 AND np_id=$3', [id.d, '{}', NP]);
const trophiesMod = require(path.join(API, 'src/endpoints/trophies.js'));
ok((await trophiesMod.rereadUnparsed()) >= 1 && (await q('SELECT parsed, points FROM lab_tset_user WHERE account_id=$1 AND np_id=$2', [id.d, NP]))[0].parsed === true, 'rilettura all\'avvio dei file non letti');
r = await api('a', 'GET', `/api/v1/users/${N.b}/trophies/${NP}`);
ok(r.b.trophies[4].hidden && r.b.trophies[4].name === null && r.b.trophies[3].earned && r.b.trophies[3].name === 'Basic Bronze Trophy', 'un trofeo nascosto non ottenuto resta senza nome per gli altri', r.b.trophies?.slice(3, 5));
r = await api('b', 'GET', `/api/v1/users/${N.b}/trophies/${NP}`);
ok(r.b.trophies[4].name === 'A Hidden Trophy', 'il proprietario lo vede', r.b.trophies?.[4]);
r = await api('b', 'GET', `/api/v1/users/${N.a}`);
ok(r.s === 200 && r.b.trophies && r.b.trophies.points === 600 && r.b.trophies.p === 1, 'il profilo porta il riepilogo dei trofei', r.b.trophies);
r = await api('a', 'GET', '/api/v1/trophies/ranking');
ok(r.s === 200 && names(r.b.ranking).join() === [N.a, N.d, N.b].join() && of(r.b.ranking, 'a').me && of(r.b.ranking, 'a').points === 600 && r.b.me.listed,
  'classifica pubblica dei trofei', r.b);
ok(!r.b.ranking.some((x) => x.user.online_id === N.c), 'chi ha zero punti non è in classifica', r.b.ranking);

console.log('\n--- privacy dei trofei');
r = await api('a', 'POST', '/api/v1/privacy', { trophies: 'tutti' });
ok(r.s === 400, 'valore non valido → 400', r);
await api('a', 'POST', '/api/v1/privacy', { trophies: 'friends' });
r = await api('b', 'GET', `/api/v1/users/${N.a}/trophies`);
ok(r.s === 403 && r.b.error === 'privacy', 'solo amici: un estraneo non li vede', r);
r = await api('b', 'GET', `/api/v1/users/${N.a}`);
ok(r.b.trophies.hidden === true, 'e il profilo non li riporta', r.b.trophies);
r = await api('b', 'GET', '/api/v1/trophies/ranking');
ok(!r.b.ranking.some((x) => x.user.online_id === N.a), 'né compare in classifica', r.b.ranking);
r = await api('a', 'GET', '/api/v1/trophies/ranking');
ok(r.b.ranking.some((x) => x.me) && r.b.me.listed === false, 'lui si vede, e sa di non essere pubblico', r.b.me);
await api('a', 'POST', '/api/v1/friends/request', { online_id: N.b }); await api('b', 'POST', '/api/v1/friends/accept', { online_id: N.a });
r = await api('b', 'GET', `/api/v1/users/${N.a}/trophies`);
ok(r.s === 200 && r.b.sets.length === 1, 'un amico sì', r);
await api('a', 'POST', '/api/v1/privacy', { trophies: 'nobody' });
r = await api('b', 'GET', `/api/v1/users/${N.a}/trophies`);
ok(r.s === 403, 'nessuno: nemmeno gli amici', r);
r = await api('a', 'GET', `/api/v1/users/${N.a}/trophies`);
ok(r.s === 200 && r.b.sets.length === 1, 'io li vedo sempre', r);
r = await api('a', 'GET', '/api/v1/account/export');
ok(r.s === 200 && r.b.trophies?.length === 1 && r.b.account.privacy_trophies === 'nobody', 'l\'esportazione dei dati contiene i trofei', r.b.trophies);
r = await api('a', 'POST', '/api/v1/privacy', { import_trophies: false });
ok(r.s === 200 && r.b.import_trophies === false && (await q('SELECT 1 FROM lab_tset_user WHERE account_id=$1', [id.a])).length === 0, 'spegnere l\'importazione cancella i trofei importati', r);
r = await api('a', 'POST', '/api/v1/trophies/check', { sets: [{ np_id: NP, tag: '1:1' }] });
ok(r.b.import === false && r.b.need_state.length === 0, 'check: importazione spenta', r);
r = await raw('a', `/api/v1/trophies/${NP}/state?tag=1:1`, stateFile([1]));
ok(r.s === 403 && r.b.error === 'import_disabled', 'e lo stato viene rifiutato', r);
r = await api('a', 'GET', '/api/v1/privacy');
ok(['messages', 'friend_requests', 'show_activity', 'show_in_records', 'trophies', 'import_trophies'].every((k) => k in r.b), 'GET /privacy riporta tutte le scelte', r.b);

await pool.end();
done();
