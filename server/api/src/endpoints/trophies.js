'use strict';
// Trofei della console. L'app manda per ogni set (NPWRxxxxx_00):
//  · la definizione (tropconf.json + tropmeta) e l'icona, una volta sola per
//    tutti: vale la prima console che la manda;
//  · il file di stato dell'utente (TRPTITLE.DAT) quando cambia.
// Chi vede i trofei di un utente lo decide lui (privacy: trophies =
// everyone | friends | nobody); nella classifica pubblica compare solo con
// «everyone». Spegnere l'importazione cancella tutto (people.setPrivacy).
const fs = require('node:fs');
const path = require('node:path');
const config = require('../config');
const db = require('../db');
const { HttpError, retryLater, readBody, readJson } = require('../http');
const limiter = require('../ratelimit');
const rel = require('../relations');
const tp = require('../trophyparse');
const { storeImage } = require('./media');

const NP_ID = /^NPWR\d{5}_\d{2}$/;
const MAX_STATE = 512 * 1024;
const MAX_ICON = 4 * 1024 * 1024;
const MAX_SETS = 600;
const TOP = 50;

const npOf = (params) => {
  const id = String(params.npId || '');
  if (!NP_ID.test(id)) throw new HttpError(400, 'invalid_np_id');
  return id;
};

async function importOn(accountId) {
  return (await db.query('SELECT import_trophies FROM lab_account WHERE account_id=$1', [accountId])).rows[0].import_trophies;
}

const counts = (row, prefix) => ({ p: row[`${prefix}_p`], g: row[`${prefix}_g`], s: row[`${prefix}_s`], b: row[`${prefix}_b`] });

// Rilegge lo stato salvato con la definizione del set e aggiorna i conteggi.
async function applyState(accountId, npId, raw, def) {
  const st = def ? tp.state(raw, def.map((t) => t.id)) : { parsed: false, earned: {} };
  const t = tp.tally(def || [], st.earned);
  await db.query(
    `UPDATE lab_tset_user SET parsed=$3, earned=$4, earned_p=$5, earned_g=$6, earned_s=$7, earned_b=$8, points=$9, last_earned=$10
      WHERE account_id=$1 AND np_id=$2`,
    [accountId, npId, st.parsed, JSON.stringify(st.earned), t.P, t.G, t.S, t.B, t.points, t.last]);
  return { parsed: st.parsed, earned: t.P + t.G + t.S + t.B };
}

// POST /api/v1/trophies/check { sets: [{ np_id, tag }], layout? }
// → { import, need_def: [...], need_icon: [...], need_state: [...] }
// layout: l'elenco dei file dei trofei sulla console (solo nomi e dimensioni),
// tenuto per capire le console su cui l'importazione non legge niente.
async function check({ req, auth }) {
  const wait = limiter.hit(`trophy_check|${auth.accountId}`, 30, 3600);
  if (wait) throw retryLater('too_many_requests', wait);
  const body = await readJson(req);
  if (!(await importOn(auth.accountId))) return { status: 200, body: { import: false, need_def: [], need_icon: [], need_state: [] } };
  const sets = (Array.isArray(body.sets) ? body.sets : []).slice(0, MAX_SETS)
    .map((s) => ({ np: String(s.np_id || ''), tag: String(s.tag || '').slice(0, 40) })).filter((s) => NP_ID.test(s.np));
  if (typeof body.layout === 'string' && body.layout) {
    try {
      const dir = path.join(config.mediaDir, 'diag');
      fs.mkdirSync(dir, { recursive: true });
      fs.writeFileSync(path.join(dir, `trophy-layout-${auth.accountId}.txt`), body.layout.slice(0, 64 * 1024));
    } catch { /* la diagnostica non blocca l'importazione */ }
  }
  const ids = sets.map((s) => s.np);
  const defs = new Map((await db.query('SELECT np_id, icon_media FROM lab_tset WHERE np_id = ANY($1::text[])', [ids])).rows.map((r) => [r.np_id, r]));
  const mine = new Map((await db.query('SELECT np_id, raw_tag FROM lab_tset_user WHERE account_id=$1 AND np_id = ANY($2::text[])', [auth.accountId, ids])).rows.map((r) => [r.np_id, r.raw_tag]));
  return {
    status: 200,
    body: {
      import: true,
      need_def: ids.filter((id) => !defs.has(id)),
      need_icon: ids.filter((id) => !(defs.get(id) || {}).icon_media),
      need_state: sets.filter((s) => mine.get(s.np) !== s.tag).map((s) => s.np),
    },
  };
}

// POST /api/v1/trophies/:npId/def { conf, meta }
async function define({ req, auth, params }) {
  const wait = limiter.hit(`trophy_def|${auth.accountId}`, 300, 3600);
  if (wait) throw retryLater('too_many_requests', wait);
  const npId = npOf(params);
  const body = await readJson(req);
  const declared = body.conf && body.conf.trophyNpCommId;
  if (declared && declared !== npId) throw new HttpError(400, 'np_id_mismatch');
  const def = tp.definition(body.conf, body.meta);
  if (!def) throw new HttpError(422, 'invalid_definition');
  const n = { P: 0, G: 0, S: 0, B: 0 };
  for (const t of def.trophies) n[t.grade] += 1;
  const r = await db.query(
    `INSERT INTO lab_tset (np_id, title, trophies, total_p, total_g, total_s, total_b, defined_by)
     VALUES ($1,$2,$3,$4,$5,$6,$7,$8) ON CONFLICT (np_id) DO NOTHING`,
    [npId, def.title, JSON.stringify(def.trophies), n.P, n.G, n.S, n.B, auth.accountId]);
  if (r.rowCount) {
    // stati arrivati prima della definizione: ora si possono leggere
    const waiting = await db.query('SELECT account_id, raw FROM lab_tset_user WHERE np_id=$1 AND raw IS NOT NULL', [npId]);
    for (const w of waiting.rows) await applyState(w.account_id, npId, w.raw, def.trophies);
  }
  return { status: r.rowCount ? 201 : 200, body: { result: r.rowCount ? 'saved' : 'kept', trophies: def.trophies.length } };
}

// POST /api/v1/trophies/:npId/icon  (corpo = PNG o JPEG)
async function icon({ req, auth, params }) {
  const wait = limiter.hit(`trophy_icon|${auth.accountId}`, 300, 3600);
  if (wait) throw retryLater('too_many_uploads', wait);
  const npId = npOf(params);
  const set = (await db.query('SELECT icon_media FROM lab_tset WHERE np_id=$1', [npId])).rows[0];
  if (!set) throw new HttpError(404, 'set_not_found');
  if (set.icon_media) return { status: 200, body: { media: set.icon_media, kept: true } };
  const media = await storeImage(await readBody(req, MAX_ICON));
  await db.query('UPDATE lab_tset SET icon_media=$2 WHERE np_id=$1 AND icon_media IS NULL', [npId, media]);
  return { status: 201, body: { media } };
}

// POST /api/v1/trophies/:npId/state?tag=<byte>:<mtime>  (corpo = TRPTITLE.DAT)
async function state({ req, auth, params, url }) {
  const wait = limiter.hit(`trophy_state|${auth.accountId}`, 600, 3600);
  if (wait) throw retryLater('too_many_uploads', wait);
  const npId = npOf(params);
  if (!(await importOn(auth.accountId))) throw new HttpError(403, 'import_disabled');
  const raw = await readBody(req, MAX_STATE);
  if (raw.length < 16) throw new HttpError(400, 'empty_file');
  const tag = String(url.searchParams.get('tag') || '').slice(0, 40);
  await db.query(
    `INSERT INTO lab_tset_user (account_id, np_id, raw, raw_tag) VALUES ($1,$2,$3,$4)
     ON CONFLICT (account_id, np_id) DO UPDATE SET raw=EXCLUDED.raw, raw_tag=EXCLUDED.raw_tag, updated_at=now()`,
    [auth.accountId, npId, raw, tag]);
  const set = (await db.query('SELECT trophies FROM lab_tset WHERE np_id=$1', [npId])).rows[0];
  const out = await applyState(auth.accountId, npId, raw, set ? set.trophies : null);
  if (set && !out.parsed) {
    console.log(JSON.stringify({ ts: new Date().toISOString(), event: 'trophy_state_unread', np_id: npId, account: String(auth.accountId), bytes: raw.length }));
  }
  return { status: 200, body: out };
}

// L'account di cui si chiedono i trofei, se chi guarda può vederli.
async function visibleTarget(auth, onlineId) {
  const u = await rel.accountByOnlineId(onlineId);
  if (!u) throw new HttpError(404, 'account_not_found');
  const self = String(u.account_id) === String(auth.accountId);
  if (!self) {
    if (await rel.isBlocked(auth.accountId, u.account_id)) throw new HttpError(403, 'blocked');
    const who = (await rel.privacyOf(u.account_id)).trophies;
    if (who === 'nobody' || (who === 'friends' && !(await rel.areFriends(auth.accountId, u.account_id)))) throw new HttpError(403, 'privacy');
  }
  return { ...u, self };
}

// Totali dei trofei di un account (per il profilo): null se non ne ha.
async function summaryOf(accountId) {
  const r = (await db.query(
    `SELECT count(*)::int AS sets, coalesce(sum(earned_p),0)::int AS p, coalesce(sum(earned_g),0)::int AS g,
            coalesce(sum(earned_s),0)::int AS s, coalesce(sum(earned_b),0)::int AS b, coalesce(sum(points),0)::int AS points
       FROM lab_tset_user WHERE account_id=$1`, [accountId])).rows[0];
  return r.sets ? r : null;
}

// Come summaryOf, ma rispettando la scelta di chi è guardato: { hidden: true } se non si può.
async function summaryFor(viewerId, targetId) {
  if (String(viewerId) !== String(targetId)) {
    const who = (await rel.privacyOf(targetId)).trophies;
    if (who === 'nobody' || (who === 'friends' && !(await rel.areFriends(viewerId, targetId)))) return { hidden: true };
  }
  return (await summaryOf(targetId)) || { sets: 0, p: 0, g: 0, s: 0, b: 0, points: 0 };
}

// GET /api/v1/users/:onlineId/trophies — i set dell'utente, dal trofeo più recente.
async function userSets({ auth, params }) {
  const u = await visibleTarget(auth, params.onlineId);
  const r = await db.query(
    `SELECT u.np_id, s.title, s.icon_media, u.parsed, u.earned_p, u.earned_g, u.earned_s, u.earned_b, u.points, u.last_earned,
            s.total_p, s.total_g, s.total_s, s.total_b
       FROM lab_tset_user u LEFT JOIN lab_tset s USING (np_id)
      WHERE u.account_id=$1
      ORDER BY u.last_earned DESC NULLS LAST, u.points DESC, s.title NULLS LAST LIMIT 300`, [u.account_id]);
  const sets = r.rows.map((x) => {
    const e = counts(x, 'earned');
    const t = x.total_p == null ? null : counts(x, 'total');
    const ne = e.p + e.g + e.s + e.b;
    const nt = t ? t.p + t.g + t.s + t.b : 0;
    return {
      np_id: x.np_id, title: x.title || x.np_id, icon_media: x.icon_media, parsed: x.parsed,
      earned: e, total: t, earned_count: ne, total_count: nt, progress: nt ? Math.round((ne * 100) / nt) : 0,
      points: x.points, last_earned: x.last_earned,
    };
  });
  return { status: 200, body: { online_id: u.online_id, summary: (await summaryOf(u.account_id)) || { sets: 0, p: 0, g: 0, s: 0, b: 0, points: 0 }, sets } };
}

// GET /api/v1/users/:onlineId/trophies/:npId — i trofei di un set, con quelli ottenuti.
// Un trofeo nascosto non ancora ottenuto resta senza nome per chi non è il proprietario.
async function userSet({ auth, params }) {
  const u = await visibleTarget(auth, params.onlineId);
  const npId = npOf(params);
  const x = (await db.query(
    `SELECT u.parsed, u.earned, s.title, s.icon_media, s.trophies
       FROM lab_tset_user u LEFT JOIN lab_tset s USING (np_id) WHERE u.account_id=$1 AND u.np_id=$2`, [u.account_id, npId])).rows[0];
  if (!x) throw new HttpError(404, 'set_not_found');
  const trophies = (x.trophies || []).map((t) => {
    const e = x.earned[String(t.id)];
    const secret = t.hidden && !e && !u.self;
    return { id: t.id, grade: t.grade, hidden: t.hidden, name: secret ? null : t.name, detail: secret ? null : t.detail, earned: !!e, earned_at: typeof e === 'string' ? e : null };
  });
  return { status: 200, body: { online_id: u.online_id, np_id: npId, title: x.title || npId, icon_media: x.icon_media, parsed: x.parsed, trophies } };
}

// GET /api/v1/trophies/ranking — classifica pubblica per punti (platino 300,
// oro 90, argento 30, bronzo 15) fra chi mostra i trofei a tutti.
async function ranking({ auth }) {
  const me = String(auth.accountId);
  const r = await db.query(
    `SELECT a.account_id, a.online_id, a.avatar, a.avatar_media, a.avatar_frames,
            count(*)::int AS sets, sum(u.earned_p)::int AS p, sum(u.earned_g)::int AS g, sum(u.earned_s)::int AS s, sum(u.earned_b)::int AS b,
            sum(u.points)::int AS points
       FROM lab_tset_user u JOIN lab_account a USING (account_id)
      WHERE a.account_id=$1 OR (a.privacy_trophies='everyone' AND ${rel.LISTABLE_SQL('a', '$1')})
      GROUP BY a.account_id HAVING sum(u.points) > 0 OR a.account_id=$1
      ORDER BY sum(u.points) DESC, sum(u.earned_p) DESC, a.online_id`, [auth.accountId]);
  let rank = 0;
  let prev = null;
  const all = r.rows.map((x, i) => {
    if (x.points !== prev) { rank = i + 1; prev = x.points; }
    return { user: rel.userCard(x), sets: x.sets, p: x.p, g: x.g, s: x.s, b: x.b, points: x.points, rank, me: String(x.account_id) === me };
  });
  const list = all.slice(0, TOP);
  const mine = all.find((x) => x.me);
  if (mine && !list.includes(mine)) list.push(mine);
  const priv = await rel.privacyOf(auth.accountId);
  return { status: 200, body: { ranking: list, me: { listed: priv.trophies === 'everyone', importing: priv.import_trophies, rank: mine ? mine.rank : null, of: all.length } } };
}

// All'avvio dell'api: rilegge gli stati rimasti «non letti» (parsed=false)
// con il lettore di adesso. Quando trophyparse.js impara un formato nuovo, i
// file già arrivati si sistemano al rilascio, senza chiedere niente alle console.
async function rereadUnparsed() {
  const r = await db.query(
    `SELECT u.account_id, u.np_id, u.raw, s.trophies FROM lab_tset_user u JOIN lab_tset s USING (np_id)
      WHERE NOT u.parsed AND u.raw IS NOT NULL LIMIT 5000`);
  let read = 0;
  for (const row of r.rows) if ((await applyState(row.account_id, row.np_id, row.raw, row.trophies)).parsed) read++;
  if (r.rows.length) console.log(JSON.stringify({ ts: new Date().toISOString(), event: 'trophy_reread', pending: r.rows.length, read }));
  return read;
}

module.exports = { check, define, icon, state, userSets, userSet, ranking, summaryFor, rereadUnparsed };
