'use strict';
// Nome e icona dei giochi, mandati dalle console. Vale il primo che arriva:
// una scheda già completa non si sovrascrive (la corregge un amministratore
// dal database). Si accettano solo giochi che qualcuno ha davvero giocato.
const db = require('../db');
const { HttpError, retryLater, readBody, readJson } = require('../http');
const limiter = require('../ratelimit');
const { storeImage } = require('./media');
const { GAME_ID } = require('../games');

const MAX_ICON = 4 * 1024 * 1024;
const PLAYED_SQL = `(SELECT game_id FROM lab_playtime UNION SELECT game_id FROM lab_presence WHERE game_id IS NOT NULL)`;

// POST /api/v1/games/wanted { ids: [game_id] } → { names: [...], icons: [...] }
// Tra i giochi installati sulla console, quelli di cui manca nome o icona.
async function wanted({ req, auth }) {
  const wait = limiter.hit(`games_wanted|${auth.accountId}`, 30, 3600);
  if (wait) throw retryLater('too_many_requests', wait);
  const body = await readJson(req);
  const ids = [...new Set((Array.isArray(body.ids) ? body.ids : []).map(String).filter((s) => GAME_ID.test(s)))].slice(0, 400);
  if (!ids.length) return { status: 200, body: { names: [], icons: [] } };
  const r = await db.query(
    `SELECT p.game_id, g.name, g.icon_media
       FROM ${PLAYED_SQL} p LEFT JOIN lab_game g USING (game_id)
      WHERE p.game_id = ANY($1::text[])`, [ids]);
  return {
    status: 200,
    body: {
      names: r.rows.filter((g) => !g.name).map((g) => g.game_id),
      icons: r.rows.filter((g) => !g.icon_media).map((g) => g.game_id),
    },
  };
}

async function played(gameId) {
  return (await db.query(`SELECT 1 FROM ${PLAYED_SQL} p WHERE p.game_id=$1 LIMIT 1`, [gameId])).rowCount > 0;
}

// POST /api/v1/games/meta { games: [{ game_id, name }] }
async function meta({ req, auth }) {
  const wait = limiter.hit(`games_meta|${auth.accountId}`, 30, 3600);
  if (wait) throw retryLater('too_many_requests', wait);
  const body = await readJson(req);
  let saved = 0;
  for (const g of (Array.isArray(body.games) ? body.games : []).slice(0, 100)) {
    const id = String(g.game_id || '');
    const name = String(g.name || '').replace(/[\u0000-\u001f]+/g, ' ').replace(/\s+/g, ' ').trim().slice(0, 128);
    if (!GAME_ID.test(id) || !name || name === id || !(await played(id))) continue;
    const r = await db.query(
      `INSERT INTO lab_game (game_id, name, named_by) VALUES ($1,$2,$3)
       ON CONFLICT (game_id) DO UPDATE SET name=EXCLUDED.name, named_by=EXCLUDED.named_by, updated_at=now()
         WHERE lab_game.name IS NULL`, [id, name, auth.accountId]);
    saved += r.rowCount;
  }
  return { status: 200, body: { saved } };
}

// POST /api/v1/games/:gameId/icon  (corpo = PNG o JPEG)
async function icon({ req, auth, params }) {
  const wait = limiter.hit(`games_icon|${auth.accountId}`, 40, 3600);
  if (wait) throw retryLater('too_many_uploads', wait);
  const id = String(params.gameId || '');
  if (!GAME_ID.test(id) || !(await played(id))) throw new HttpError(404, 'game_not_found');
  const has = (await db.query('SELECT icon_media FROM lab_game WHERE game_id=$1', [id])).rows[0];
  if (has && has.icon_media) return { status: 200, body: { media: has.icon_media, kept: true } };
  const media = await storeImage(await readBody(req, MAX_ICON));
  await db.query(
    `INSERT INTO lab_game (game_id, icon_media, icon_by) VALUES ($1,$2,$3)
     ON CONFLICT (game_id) DO UPDATE SET icon_media=EXCLUDED.icon_media, icon_by=EXCLUDED.icon_by, updated_at=now()`,
    [id, media, auth.accountId]);
  return { status: 201, body: { media } };
}

module.exports = { wanted, meta, icon };
