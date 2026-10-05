'use strict';
// Record pubblici: il tempo di gioco di tutti gli iscritti, non solo degli
// amici. I totali dei giochi contano chiunque (sono numeri senza nomi); negli
// elenchi con il nome compare solo chi lo accetta (privacy: show_in_records) e
// non nasconde la propria attività. Io mi vedo sempre, anche se nascosto.
const db = require('../db');
const { HttpError } = require('../http');
const rel = require('../relations');
const playtime = require('../playtime');
const games = require('../games');

const TOP_PLAYERS = 50;
const TOP_GAMES = 30;
const TOP_PAIRS = 20;

// Righe (account_id, game_id, game_name, seconds) del periodo scelto.
const SRC = (period) => (period === 'week'
  ? `(SELECT w.account_id, w.game_id, p.game_name, w.seconds
        FROM lab_playtime_week w LEFT JOIN lab_playtime p USING (account_id, game_id)
       WHERE w.week_start = date_trunc('week', now())::date)`
  : '(SELECT account_id, game_id, game_name, seconds FROM lab_playtime)');

// Chi compare con il nome, visto da $1.
const LISTED = `(a.account_id=$1 OR (a.show_in_records AND a.show_activity AND ${rel.LISTABLE_SQL('a', '$1')}))`;
const CARD = 'a.account_id, a.online_id, a.avatar, a.avatar_media, a.avatar_frames';

const periodOf = (url) => ((url.searchParams.get('period') || 'all') === 'week' ? 'week' : 'all');

// A pari tempo, pari posizione. Ritorna le righe con rank.
function ranked(list) {
  let rank = 0;
  let prev = null;
  return list.map((x, i) => {
    if (x.seconds !== prev) { rank = i + 1; prev = x.seconds; }
    return { ...x, rank };
  });
}

// Tutte le coppie (giocatore elencabile, gioco) con il tempo in corso sommato.
async function listedPairs(me, period, gameId) {
  const args = [me];
  if (gameId) args.push(gameId);
  const r = await db.query(
    `SELECT ${CARD}, s.game_id, s.game_name, s.seconds::bigint AS seconds
       FROM ${SRC(period)} s JOIN lab_account a ON a.account_id = s.account_id
      WHERE ${LISTED} ${gameId ? 'AND s.game_id=$2' : ''}`, args);
  const pairs = r.rows.map((x) => ({ ...x, seconds: Number(x.seconds) }));
  const open = await playtime.openSegments(null);
  if (open.size) {
    const known = new Map(pairs.map((x) => [`${x.account_id}|${x.game_id}`, x]));
    const ids = [...open.keys()];
    const acc = await db.query(`SELECT ${CARD} FROM lab_account a WHERE a.account_id = ANY($2::bigint[]) AND ${LISTED}`, [me, ids]);
    for (const a of acc.rows) {
      const seg = open.get(String(a.account_id));
      if (gameId && seg.game_id !== gameId) continue;
      const hit = known.get(`${a.account_id}|${seg.game_id}`);
      if (hit) hit.seconds += seg.seconds;
      else pairs.push({ ...a, game_id: seg.game_id, game_name: seg.game_name, seconds: seg.seconds });
    }
  }
  return pairs;
}

// GET /api/v1/records?period=week|all
async function overview({ auth, url }) {
  const period = periodOf(url);
  const me = String(auth.accountId);
  const pairs = await listedPairs(auth.accountId, period, null);

  // giocatori: totale, numero di giochi, gioco più giocato
  const byPlayer = new Map();
  for (const x of pairs) {
    const k = String(x.account_id);
    let p = byPlayer.get(k);
    if (!p) { p = { user: rel.userCard(x), seconds: 0, games: 0, top: x, me: k === me }; byPlayer.set(k, p); }
    p.seconds += x.seconds; p.games += 1;
    if (x.seconds > p.top.seconds) p.top = x;
  }
  const allPlayers = ranked([...byPlayer.values()].filter((p) => p.seconds > 0)
    .sort((a, b) => b.seconds - a.seconds || a.user.online_id.localeCompare(b.user.online_id)));
  const mine = allPlayers.find((p) => p.me) || null;
  const players = allPlayers.slice(0, TOP_PLAYERS);
  if (mine && !players.includes(mine)) players.push(mine);

  // giochi: i totali contano tutti, il primatista è tra chi compare
  const totals = (await db.query(
    `SELECT game_id, max(game_name) AS game_name, sum(seconds)::bigint AS seconds, count(DISTINCT account_id)::int AS players
       FROM ${SRC(period)} s GROUP BY game_id`)).rows.map((g) => ({ ...g, seconds: Number(g.seconds) }));
  const tmap = new Map(totals.map((g) => [g.game_id, g]));
  const open = await playtime.openSegments(null);
  for (const seg of open.values()) {
    const g = tmap.get(seg.game_id);
    if (g) g.seconds += seg.seconds;
    else { const n = { game_id: seg.game_id, game_name: seg.game_name, seconds: seg.seconds, players: 1 }; tmap.set(seg.game_id, n); totals.push(n); }
  }
  const now = (await db.query(
    `SELECT game_id, count(*)::int AS n FROM lab_presence
      WHERE status<>'offline' AND last_seen > now() - interval '120 seconds' GROUP BY game_id`)).rows;
  const nowBy = new Map(now.map((r) => [r.game_id, r.n]));
  const best = new Map();
  for (const x of pairs) { const b = best.get(x.game_id); if (!b || x.seconds > b.seconds) best.set(x.game_id, x); }
  const topGames = totals.filter((g) => g.seconds > 0)
    .sort((a, b) => b.seconds - a.seconds || a.game_id.localeCompare(b.game_id)).slice(0, TOP_GAMES)
    .map((g) => {
      const b = best.get(g.game_id);
      return {
        game_id: g.game_id, game_name: g.game_name, seconds: g.seconds, players: g.players,
        playing_now: nowBy.get(g.game_id) || 0,
        top: b ? { user: rel.userCard(b), seconds: b.seconds } : null,
      };
    });

  // maratone: il tempo più alto di un solo giocatore su un solo gioco
  const marathons = ranked(pairs.filter((x) => x.seconds > 0)
    .sort((a, b) => b.seconds - a.seconds || a.online_id.localeCompare(b.online_id)).slice(0, TOP_PAIRS)
    .map((x) => ({ user: rel.userCard(x), game_id: x.game_id, game_name: x.game_name, seconds: x.seconds, me: String(x.account_id) === me })));

  const playerRows = players.map((p) => ({
    user: p.user, seconds: p.seconds, rank: p.rank, me: p.me, games: p.games,
    game_id: p.top.game_id, game_name: p.top.game_name,
  }));
  await games.decorate([...topGames, ...marathons, ...playerRows]);

  const listed = (await db.query('SELECT show_in_records AND show_activity AS ok FROM lab_account WHERE account_id=$1', [auth.accountId])).rows[0].ok;
  return {
    status: 200,
    body: {
      period,
      players: playerRows,
      games: topGames,
      marathons,
      now: { online: now.reduce((s, r) => s + r.n, 0), playing: now.filter((r) => r.game_id).reduce((s, r) => s + r.n, 0) },
      me: { listed, rank: mine ? mine.rank : null, seconds: mine ? mine.seconds : 0, of: allPlayers.length },
    },
  };
}

// GET /api/v1/records/games/:gameId?period=week|all
async function game({ auth, url, params }) {
  const period = periodOf(url);
  const gameId = String(params.gameId || '');
  if (!games.GAME_ID.test(gameId)) throw new HttpError(404, 'game_not_found');
  const me = String(auth.accountId);
  const pairs = await listedPairs(auth.accountId, period, gameId);
  const all = ranked(pairs.filter((x) => x.seconds > 0)
    .sort((a, b) => b.seconds - a.seconds || a.online_id.localeCompare(b.online_id))
    .map((x) => ({ user: rel.userCard(x), seconds: x.seconds, me: String(x.account_id) === me })));
  const ranking = all.slice(0, TOP_PLAYERS);
  const mine = all.find((x) => x.me);
  if (mine && !ranking.includes(mine)) ranking.push(mine);
  const tot = (await db.query(
    `SELECT max(game_name) AS game_name, coalesce(sum(seconds),0)::bigint AS seconds, count(DISTINCT account_id)::int AS players
       FROM ${SRC(period)} s WHERE game_id=$1`, [gameId])).rows[0];
  let seconds = Number(tot.seconds);
  let name = tot.game_name;
  for (const seg of (await playtime.openSegments(null)).values()) {
    if (seg.game_id === gameId) { seconds += seg.seconds; name = name || seg.game_name; }
  }
  const [card] = await games.decorate([{ game_id: gameId, game_name: name }]);
  return {
    status: 200,
    body: { period, game: { ...card, seconds, players: tot.players }, ranking },
  };
}

module.exports = { overview, game };
