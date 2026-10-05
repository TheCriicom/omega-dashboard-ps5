'use strict';
// Tempo di gioco e classifiche tra amici. I totali vengono da lab_playtime e
// lab_playtime_week (src/playtime.js) più il tratto in corso, così chi sta
// giocando vede il tempo crescere. Chi nasconde la propria attività non
// compare nelle classifiche degli altri.
const db = require('../db');
const rel = require('../relations');
const playtime = require('../playtime');
const gameCards = require('../games');

// GET /api/v1/stats/me
async function me({ auth }) {
  const r = await db.query(
    `SELECT t.game_id, t.game_name, t.seconds::bigint AS seconds, t.sessions, t.last_played,
            coalesce(w.seconds, 0)::bigint AS week_seconds
       FROM lab_playtime t
       LEFT JOIN lab_playtime_week w ON w.account_id=t.account_id AND w.game_id=t.game_id
                                    AND w.week_start = date_trunc('week', now())::date
      WHERE t.account_id=$1`, [auth.accountId]);
  const games = r.rows.map((g) => ({
    game_id: g.game_id, game_name: g.game_name, seconds: Number(g.seconds), sessions: g.sessions,
    last_played: g.last_played, week_seconds: Number(g.week_seconds),
  }));
  const open = (await playtime.openSegments([auth.accountId])).get(String(auth.accountId));
  if (open) {
    let g = games.find((x) => x.game_id === open.game_id);
    if (!g) { g = { game_id: open.game_id, game_name: open.game_name, seconds: 0, sessions: 0, last_played: null, week_seconds: 0 }; games.push(g); }
    g.seconds += open.seconds;
    g.week_seconds += open.seconds;
    g.last_played = new Date().toISOString();
    if (!g.game_name) g.game_name = open.game_name;
  }
  games.sort((a, b) => b.seconds - a.seconds || String(a.game_name || '').localeCompare(String(b.game_name || '')));
  const total = games.reduce((s, g) => s + g.seconds, 0);
  const week = games.reduce((s, g) => s + g.week_seconds, 0);
  return { status: 200, body: { total_seconds: total, week_seconds: week, games: await gameCards.decorate(games.slice(0, 30)) } };
}

// Totali per account, eventualmente di un solo gioco o della settimana in corso.
async function totalsFor(ids, { gameId, period }) {
  const args = [ids];
  let sql;
  if (period === 'week') {
    sql = `SELECT account_id, sum(seconds)::bigint AS s FROM lab_playtime_week
            WHERE account_id = ANY($1::bigint[]) AND week_start = date_trunc('week', now())::date`;
  } else {
    sql = 'SELECT account_id, sum(seconds)::bigint AS s FROM lab_playtime WHERE account_id = ANY($1::bigint[])';
  }
  if (gameId) { args.push(gameId); sql += ' AND game_id=$2'; }
  sql += ' GROUP BY account_id';
  const out = new Map();
  for (const row of (await db.query(sql, args)).rows) out.set(String(row.account_id), Number(row.s));
  const open = await playtime.openSegments(ids);
  for (const [id, seg] of open) {
    if (gameId && seg.game_id !== gameId) continue;
    out.set(id, (out.get(id) || 0) + seg.seconds);
  }
  return out;
}

// GET /api/v1/stats/friends?game_id=&period=week|all
async function friends({ auth, url }) {
  const period = (url.searchParams.get('period') || 'all') === 'week' ? 'week' : 'all';
  const gameId = String(url.searchParams.get('game_id') || '').trim().slice(0, 64) || null;
  const people = (await db.query(
    `SELECT a.account_id, a.online_id, a.avatar, a.avatar_media, a.avatar_frames
       FROM lab_account a
      WHERE a.account_id=$1
         OR (a.account_id IN ${rel.FRIENDS_SQL('$1')} AND a.show_activity AND NOT a.disabled
             AND a.account_id NOT IN ${rel.BLOCKED_SQL('$1')})`, [auth.accountId])).rows;
  const totals = await totalsFor(people.map((p) => p.account_id), { gameId, period });
  const list = people
    .map((p) => ({ p, seconds: totals.get(String(p.account_id)) || 0, me: String(p.account_id) === String(auth.accountId) }))
    .filter((x) => x.me || x.seconds > 0)
    .sort((a, b) => b.seconds - a.seconds || a.p.online_id.localeCompare(b.p.online_id));
  // a pari tempo, pari posizione
  let rank = 0;
  let prev = null;
  const ranking = list.map((x, i) => {
    if (x.seconds !== prev) { rank = i + 1; prev = x.seconds; }
    return { user: rel.userCard(x.p), seconds: x.seconds, rank, me: x.me };
  });
  return { status: 200, body: { period, game_id: gameId, ranking } };
}

// Riepilogo per il profilo di un utente: { total_seconds, top_games (≤ 3) }.
async function summaryOf(accountId) {
  const r = await db.query(
    `SELECT game_id, game_name, seconds::bigint AS seconds FROM lab_playtime WHERE account_id=$1`, [accountId]);
  const games = r.rows.map((g) => ({ game_id: g.game_id, game_name: g.game_name, seconds: Number(g.seconds) }));
  const open = (await playtime.openSegments([accountId])).get(String(accountId));
  if (open) {
    const g = games.find((x) => x.game_id === open.game_id);
    if (g) g.seconds += open.seconds; else games.push({ game_id: open.game_id, game_name: open.game_name, seconds: open.seconds });
  }
  games.sort((a, b) => b.seconds - a.seconds);
  return { total_seconds: games.reduce((s, g) => s + g.seconds, 0), top_games: await gameCards.decorate(games.slice(0, 3)) };
}

module.exports = { me, friends, summaryOf };
