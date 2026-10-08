'use strict';
// Il tuo riepilogo, come il Wrap-Up di PlayStation: una settimana, un mese o
// un anno di gioco in numeri. Totali e giochi vengono dalle sessioni
// (lab_play_session) quando il registro copre tutto il periodo, altrimenti
// dalle settimane di lab_playtime_week; ritmo, sessione più lunga, giorni di
// fila e «giocato insieme» solo dalle sessioni. Le ore e i giorni sono quelli
// di chi guarda (?tz= in minuti, come lo manda la console o il browser).
// Degli altri si usano solo totali senza nomi, tranne gli amici che mostrano
// la propria attività.
const db = require('../db');
const { HttpError, retryLater } = require('../http');
const limiter = require('../ratelimit');
const rel = require('../relations');
const playtime = require('../playtime');
const games = require('../games');

const DAY = 86400e3;
const PERIODS = ['week', 'month', 'year'];
const MAX_BACK = { week: 52, month: 24, year: 5 };
const KEEP_DAYS = 400;
const CACHE_MS = 60e3;
const cache = new Map();

// Inizio e fine del periodo nel fuso di chi guarda, come istanti UTC.
function bounds(period, back, tz, now = Date.now()) {
  const loc = new Date(now + tz * 60e3);         // ora locale letta con i getter UTC
  const y = loc.getUTCFullYear(); const m = loc.getUTCMonth(); const d = loc.getUTCDate();
  let s; let e;
  if (period === 'week') {
    const dow = (loc.getUTCDay() + 6) % 7;      // lunedì = 0
    s = Date.UTC(y, m, d - dow - 7 * back); e = s + 7 * DAY;
  } else if (period === 'month') {
    s = Date.UTC(y, m - back, 1); e = Date.UTC(y, m - back + 1, 1);
  } else {
    s = Date.UTC(y - back, 0, 1); e = Date.UTC(y - back + 1, 0, 1);
  }
  return { from: new Date(s - tz * 60e3), to: new Date(e - tz * 60e3), localFrom: new Date(s), localTo: new Date(e) };
}

const isoDate = (d) => d.toISOString().slice(0, 10);

// Prima sessione registrata in assoluto: prima di lì il registro non c'era.
let logStart = { at: 0, t: null };
async function registryStart() {
  if (Date.now() - logStart.at < CACHE_MS) return logStart.t;
  const r = await db.query('SELECT min(started_at) AS t FROM lab_play_session');
  logStart = { at: Date.now(), t: r.rows[0].t ? new Date(r.rows[0].t) : null };
  return logStart.t;
}

// Sessioni di un account che toccano [from, to), tagliate ai bordi, più il
// tratto in corso.
async function sessionsOf(accountId, from, to) {
  const r = await db.query(
    `SELECT game_id, started_at, ended_at FROM lab_play_session
      WHERE account_id=$1 AND ended_at > $2 AND started_at < $3
      ORDER BY started_at LIMIT 20000`, [accountId, from, to]);
  const out = r.rows.map((x) => ({ game_id: x.game_id, s: new Date(x.started_at).getTime(), e: new Date(x.ended_at).getTime() }));
  const open = (await playtime.openSegments([accountId])).get(String(accountId));
  if (open) out.push({ game_id: open.game_id, game_name: open.game_name, open: true, s: Date.now() - open.seconds * 1000, e: Date.now() });
  const f = from.getTime(); const t = to.getTime();
  return out.map((x) => ({ ...x, s: Math.max(x.s, f), e: Math.min(x.e, t) })).filter((x) => x.e - x.s >= 1000);
}

// Totali per gioco dalle settimane (periodi in cui il registro non c'era ancora).
async function weeklyGames(accountId, b) {
  const r = await db.query(
    `SELECT w.game_id, p.game_name, sum(w.seconds)::bigint AS seconds
       FROM lab_playtime_week w LEFT JOIN lab_playtime p USING (account_id, game_id)
      WHERE w.account_id=$1 AND w.week_start >= $2::date AND w.week_start < $3::date
      GROUP BY w.game_id, p.game_name`, [accountId, isoDate(b.localFrom), isoDate(b.localTo)]);
  return r.rows.map((x) => ({ game_id: x.game_id, game_name: x.game_name, seconds: Number(x.seconds), sessions: null }));
}

// Totale di ogni account nel periodo (per il confronto con la community).
async function everyoneTotals(b, fromLog) {
  const r = fromLog
    ? await db.query(
      `SELECT account_id, sum(EXTRACT(EPOCH FROM (LEAST(ended_at,$2) - GREATEST(started_at,$1))))::bigint AS s
         FROM lab_play_session WHERE ended_at > $1 AND started_at < $2 GROUP BY account_id`, [b.from, b.to])
    : await db.query(
      `SELECT account_id, sum(seconds)::bigint AS s FROM lab_playtime_week
        WHERE week_start >= $1::date AND week_start < $2::date GROUP BY account_id`, [isoDate(b.localFrom), isoDate(b.localTo)]);
  const out = new Map(r.rows.map((x) => [String(x.account_id), Number(x.s)]));
  if (b.from.getTime() <= Date.now() && Date.now() < b.to.getTime()) {
    for (const [id, seg] of await playtime.openSegments(null)) out.set(id, (out.get(id) || 0) + seg.seconds);
  }
  return out;
}

// Secondi per ora del giorno e per giorno della settimana, nel fuso di chi guarda.
function rhythm(sessions, tz) {
  const hours = new Array(24).fill(0); const days = new Array(7).fill(0);
  const off = tz * 60e3;
  for (const x of sessions) {
    let t = x.s + off; const end = x.e + off;
    while (t < end) {
      const next = Math.min(end, (Math.floor(t / 3600e3) + 1) * 3600e3);
      const d = new Date(t);
      hours[d.getUTCHours()] += (next - t) / 1000;
      days[(d.getUTCDay() + 6) % 7] += (next - t) / 1000;
      t = next;
    }
  }
  return { hours: hours.map(Math.round), weekdays: days.map(Math.round) };
}

// Giorni con almeno un minuto di gioco e la serie più lunga di giorni di fila.
function dayStats(sessions, tz) {
  const off = tz * 60e3; const set = new Set();
  for (const x of sessions) {
    if (x.e - x.s < 60e3) continue;
    for (let d = Math.floor((x.s + off) / DAY); d <= Math.floor((x.e - 1 + off) / DAY); d++) set.add(d);
  }
  const list = [...set].sort((a, b) => a - b);
  let best = 0; let run = 0; let prev = null;
  for (const d of list) { run = prev !== null && d === prev + 1 ? run + 1 : 1; best = Math.max(best, run); prev = d; }
  return { days_active: list.length, streak: best };
}

// Giochi giocati per la prima volta nel periodo.
async function newGames(accountId, ids, b) {
  if (!ids.length) return 0;
  const r = await db.query(
    `SELECT count(*)::int AS n FROM unnest($2::text[]) g(id)
      WHERE NOT EXISTS (SELECT 1 FROM lab_playtime_week w WHERE w.account_id=$1 AND w.game_id=g.id AND w.week_start < $3::date)
        AND NOT EXISTS (SELECT 1 FROM lab_play_session s WHERE s.account_id=$1 AND s.game_id=g.id AND s.started_at < $4)`,
    [accountId, ids, isoDate(new Date(b.localFrom.getTime() - 6 * DAY)), b.from]);
  return r.rows[0].n;
}

// Trofei ottenuti nel periodo, per grado, e il più raro.
async function trophiesIn(accountId, b) {
  const r = await db.query(
    `SELECT u.np_id, u.earned, s.title, s.icon_media, s.trophies FROM lab_tset_user u JOIN lab_tset s USING (np_id)
      WHERE u.account_id=$1 AND u.parsed AND u.last_earned >= $2`, [accountId, b.from]);
  const f = b.from.getTime(); const t = b.to.getTime();
  const got = [];
  for (const row of r.rows) {
    const defs = new Map((Array.isArray(row.trophies) ? row.trophies : []).map((x) => [String(x.id), x]));
    for (const [id, at] of Object.entries(row.earned || {})) {
      const ms = typeof at === 'string' ? Date.parse(at) : NaN;
      if (!(ms >= f && ms < t)) continue;
      const d = defs.get(id) || {};
      got.push({ np_id: row.np_id, id, grade: ['P', 'G', 'S', 'B'].includes(d.grade) ? d.grade : 'B', name: d.name || null, game: row.title, icon_media: row.icon_media });
    }
  }
  const out = { count: got.length, p: 0, g: 0, s: 0, b: 0, rarest: null };
  for (const x of got) out[x.grade.toLowerCase()]++;
  if (!got.length) return out;
  const nps = [...new Set(got.map((x) => x.np_id))];
  const owners = new Map((await db.query(
    'SELECT np_id, count(*)::int AS n FROM lab_tset_user WHERE np_id = ANY($1::text[]) AND parsed GROUP BY np_id', [nps])).rows.map((x) => [x.np_id, x.n]));
  const holders = new Map((await db.query(
    `SELECT u.np_id, e.key, count(*)::int AS n FROM lab_tset_user u CROSS JOIN LATERAL jsonb_object_keys(u.earned) e(key)
      WHERE u.np_id = ANY($1::text[]) AND u.parsed GROUP BY u.np_id, e.key`, [nps])).rows.map((x) => [`${x.np_id}|${x.key}`, x.n]));
  const RANK = { P: 0, G: 1, S: 2, B: 3 };
  let best = null;
  for (const x of got) {
    const own = owners.get(x.np_id) || 1;
    const pct = own >= 3 ? Math.round((100 * (holders.get(`${x.np_id}|${x.id}`) || 1)) / own) : null;
    const score = (pct === null ? 100 : pct) * 10 + RANK[x.grade];
    if (!best || score < best.score) best = { ...x, pct, score };
  }
  delete best.score; delete best.np_id; delete best.id;
  out.rarest = best;
  return out;
}

// Amici che giocavano allo stesso gioco nello stesso momento.
async function together(accountId, b) {
  const r = await db.query(
    `SELECT a.account_id, a.online_id, a.avatar, a.avatar_media, a.avatar_frames, o.game_id,
            sum(EXTRACT(EPOCH FROM (LEAST(m.ended_at, o.ended_at, $3) - GREATEST(m.started_at, o.started_at, $2))))::bigint AS s
       FROM lab_play_session m
       JOIN lab_play_session o ON o.game_id = m.game_id AND o.account_id <> m.account_id
                              AND o.started_at < m.ended_at AND o.ended_at > m.started_at
       JOIN lab_account a ON a.account_id = o.account_id
      WHERE m.account_id=$1 AND m.ended_at > $2 AND m.started_at < $3
        AND o.account_id IN ${rel.FRIENDS_SQL('$1')} AND a.show_activity AND NOT a.disabled
        AND a.account_id NOT IN ${rel.BLOCKED_SQL('$1')}
      GROUP BY a.account_id, o.game_id`, [accountId, b.from, b.to]);
  const byFriend = new Map();
  for (const x of r.rows) {
    const s = Number(x.s);
    if (s < 60) continue;
    const k = String(x.account_id);
    const cur = byFriend.get(k) || { user: rel.userCard(x), seconds: 0, game_id: null, best: 0 };
    cur.seconds += s;
    if (s > cur.best) { cur.best = s; cur.game_id = x.game_id; }
    byFriend.set(k, cur);
  }
  const list = [...byFriend.values()].sort((a, c) => c.seconds - a.seconds).slice(0, 3)
    .map((x) => ({ user: x.user, seconds: x.seconds, game_id: x.game_id }));
  await games.decorate(list);
  return list;
}

// Un profilo in una parola, dalla prima regola che vale.
function persona(w) {
  const tot = w.total_seconds || 1;
  const h = w.hours; const sumH = (a, c) => { let s = 0; for (let i = a; i !== c; i = (i + 1) % 24) s += h ? h[i] : 0; return s; };
  if (h && sumH(22, 5) / tot >= 0.4) return 'night_owl';
  if (h && sumH(5, 10) / tot >= 0.35) return 'early_bird';
  if (w.longest && w.longest.seconds >= 3 * 3600) return 'marathoner';
  if (w.trophies.count >= 20) return 'hunter';
  if (w.games_count >= 5 || w.new_games >= 3) return 'explorer';
  if (w.games.length && w.games[0].seconds / tot >= 0.7 && tot >= 3 * 3600) return 'loyal';
  if (w.weekdays && (w.weekdays[5] + w.weekdays[6]) / tot >= 0.6) return 'weekend';
  if (w.streak >= 5) return 'daily';
  if (w.together.length) return 'social';
  return 'casual';
}

async function build(accountId, period, back, tz) {
  const b = bounds(period, back, tz);
  const start = await registryStart();
  const fromLog = !!start && start.getTime() <= b.from.getTime();
  const sessions = await sessionsOf(accountId, b.from, b.to);

  let list;
  if (fromLog) {
    const m = new Map();
    for (const x of sessions) {
      const g = m.get(x.game_id) || { game_id: x.game_id, game_name: x.game_name || null, seconds: 0, sessions: 0 };
      g.seconds += (x.e - x.s) / 1000; g.sessions++;
      m.set(x.game_id, g);
    }
    list = [...m.values()];
  } else {
    list = await weeklyGames(accountId, b);
    const open = sessions.find((x) => x.open);      // il tratto in corso non è ancora nelle settimane
    if (open) {
      const g = list.find((x) => x.game_id === open.game_id);
      if (g) g.seconds += (open.e - open.s) / 1000;
      else list.push({ game_id: open.game_id, game_name: open.game_name, seconds: (open.e - open.s) / 1000, sessions: null });
    }
  }
  for (const g of list) g.seconds = Math.round(g.seconds);
  list = list.filter((g) => g.seconds > 0).sort((a, c) => c.seconds - a.seconds);
  const total = list.reduce((s, g) => s + g.seconds, 0);

  const prevB = bounds(period, back + 1, tz);
  const prevLog = !!start && start.getTime() <= prevB.from.getTime();
  const prevTot = (await everyoneTotals(prevB, prevLog)).get(String(accountId)) || 0;
  const all = await everyoneTotals(b, fromLog);
  all.set(String(accountId), total);
  const players = [...all.values()].filter((s) => s > 0);
  const percentile = total > 0 && players.length >= 5
    ? Math.floor((100 * players.filter((s) => s < total).length) / players.length) : null;

  // ritmo e sessione più lunga anche da un registro che copre solo una parte
  // del periodo; giorni di fila solo se lo copre tutto
  const logged = sessions;
  const detail = logged.length > 0;
  let longest = null;
  for (const x of logged) if (!longest || x.e - x.s > longest.e - longest.s) longest = x;

  const w = {
    period, back, tz,
    from: b.from.toISOString(), to: b.to.toISOString(),
    complete: b.to.getTime() <= Date.now(),
    source: fromLog ? 'sessions' : 'weeks',
    total_seconds: total,
    prev_total_seconds: prevTot,
    games_count: list.length,
    sessions: fromLog ? sessions.length : null,
    games: await games.decorate(list.slice(0, 5)),
    new_games: await newGames(accountId, list.map((g) => g.game_id), b),
    hours: null, weekdays: null, longest: null, days_active: null, streak: null,
    percentile, players: players.length,
    trophies: await trophiesIn(accountId, b),
    together: detail ? await together(accountId, b) : [],
  };
  if (detail) {
    Object.assign(w, rhythm(logged, tz));
    w.longest = (await games.decorate([{ game_id: longest.game_id, game_name: longest.game_name || null, seconds: Math.round((longest.e - longest.s) / 1000), at: new Date(longest.s).toISOString() }]))[0];
    if (fromLog) Object.assign(w, dayStats(logged, tz));
  }
  w.persona = persona(w);
  return w;
}

// GET /api/v1/wrap?period=week|month|year&back=0..&tz=-720..840
async function get({ auth, url }) {
  const period = PERIODS.includes(url.searchParams.get('period')) ? url.searchParams.get('period') : 'week';
  const back = Number(url.searchParams.get('back') || 0);
  const tz = Number(url.searchParams.get('tz') || 0);
  if (!Number.isInteger(back) || back < 0 || back > MAX_BACK[period]) throw new HttpError(400, 'bad_back');
  if (!Number.isInteger(tz) || tz < -720 || tz > 840) throw new HttpError(400, 'bad_tz');
  const key = `${auth.accountId}|${period}|${back}|${tz}`;
  const hit = cache.get(key);
  if (hit && Date.now() - hit.at < CACHE_MS) return { status: 200, body: hit.body };
  const wait = limiter.hit(`wrap|${auth.accountId}`, 30, 60);
  if (wait) throw retryLater('too_many_requests', wait);
  const body = await build(auth.accountId, period, back, tz);
  if (cache.size > 2000) cache.clear();
  cache.set(key, { at: Date.now(), body });
  return { status: 200, body };
}

async function sweep() {
  await db.query(`DELETE FROM lab_play_session WHERE ended_at < now() - interval '${KEEP_DAYS} days'`);
}

function start() {
  const run = () => sweep().catch((e) => console.error(JSON.stringify({ ts: new Date().toISOString(), event: 'wrap_sweep_failed', error: e.message })));
  setInterval(run, 6 * 3600e3).unref();
}

module.exports = { get, start, bounds, persona };
