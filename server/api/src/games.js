'use strict';
// Scheda dei giochi (lab_game): nome e icona da mostrare al posto del codice
// del titolo. La presenza manda spesso solo il codice (PPSA…): il nome vero
// arriva dalla console con POST /api/v1/games/meta.
const db = require('./db');

const GAME_ID = /^[A-Za-z0-9_-]{4,24}$/;

// Map(game_id → { name, icon_media }) per gli id dati.
async function cards(ids) {
  const out = new Map();
  const list = [...new Set(ids.filter(Boolean))];
  if (!list.length) return out;
  const r = await db.query('SELECT game_id, name, icon_media FROM lab_game WHERE game_id = ANY($1::text[])', [list]);
  for (const g of r.rows) out.set(g.game_id, { name: g.name, icon_media: g.icon_media });
  return out;
}

// Un nome uguale al codice non è un nome.
const realName = (name, id) => (name && name !== id ? name : null);

// Completa in posto righe { game_id, game_name } con il nome della scheda e
// game_icon. Ritorna le stesse righe.
async function decorate(rows) {
  const map = await cards(rows.map((r) => r.game_id));
  for (const r of rows) {
    const c = map.get(r.game_id);
    r.game_name = (c && c.name) || realName(r.game_name, r.game_id) || r.game_id;
    r.game_icon = (c && c.icon_media) || null;
  }
  return rows;
}

module.exports = { GAME_ID, cards, decorate, realName };
