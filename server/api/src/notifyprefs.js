'use strict';
// Preferenze delle notifiche di ogni account (lab_account.notify_prefs) e le
// regole che le applicano quando una notifica nasce (notify.js).
const db = require('./db');

// tipi che l'utente può regolare; valore: all | favorites | off
const TYPES = ['online', 'game_start', 'message', 'party_invite', 'game_invite', 'friend_request', 'friend_accept',
  'post_like', 'post_comment', 'store_comment', 'store_recommend', 'store_update'];
// prima tempestavano: "è online" e "sta giocando a" di base solo dai preferiti
const DEFAULTS = { online: 'favorites', game_start: 'favorites' };
// durante il gioco, con "solo importanti", arrivano questi
const IMPORTANT = new Set(['message', 'party_invite', 'game_invite', 'friend_request']);
const MODES = new Set(['all', 'favorites', 'off']);
const HHMM = /^([01]\d|2[0-3]):[0-5]\d$/;

function normalize(raw) {
  const p = raw && typeof raw === 'object' ? raw : {};
  const types = {};
  for (const t of TYPES) { const v = p.types && p.types[t]; types[t] = MODES.has(v) ? v : (DEFAULTS[t] || 'all'); }
  const q = p.quiet || {};
  return {
    types,
    in_game: ['all', 'important', 'off'].includes(p.in_game) ? p.in_game : 'important',
    quiet: { enabled: !!q.enabled, from: HHMM.test(q.from) ? q.from : '23:00', to: HHMM.test(q.to) ? q.to : '08:00' },
    tz_offset: Number.isFinite(p.tz_offset) ? Math.max(-840, Math.min(840, Math.round(p.tz_offset))) : 60,   // minuti da UTC
  };
}

function inQuiet(prefs, now = new Date()) {
  if (!prefs.quiet.enabled) return false;
  const m = ((now.getUTCHours() * 60 + now.getUTCMinutes() + prefs.tz_offset) % 1440 + 1440) % 1440;
  const [fh, fm] = prefs.quiet.from.split(':').map(Number), [th, tm] = prefs.quiet.to.split(':').map(Number);
  const a = fh * 60 + fm, b = th * 60 + tm;
  return a <= b ? m >= a && m < b : m >= a || m < b;   // anche a cavallo della mezzanotte
}

// Decide per un destinatario: { skip } (non crearla) o { silent } (crearla senza avviso).
async function decide(accountId, type, actorId) {
  if (!TYPES.includes(type)) return { skip: false, silent: false };   // notifiche di servizio (admin, feedback)
  const r = (await db.query(
    `SELECT a.notify_prefs, p.game_id, p.status, extract(epoch FROM now()-p.last_seen)::int AS age,
            f.favorite, f.muted
       FROM lab_account a LEFT JOIN lab_presence p ON p.account_id=a.account_id
       LEFT JOIN lab_notify_friend f ON f.account_id=a.account_id AND f.friend_id=$2
      WHERE a.account_id=$1`, [accountId, actorId || 0])).rows[0];
  if (!r) return { skip: true };
  const prefs = normalize(r.notify_prefs);
  const mode = prefs.types[type];
  if (mode === 'off') return { skip: true };
  if (actorId && r.muted) return { skip: true };
  if (mode === 'favorites' && !(actorId && r.favorite)) return { skip: true };
  let silent = inQuiet(prefs);
  const playing = r.game_id && r.status !== 'offline' && r.age != null && r.age < 180;
  if (playing && (prefs.in_game === 'off' || (prefs.in_game === 'important' && !IMPORTANT.has(type)))) silent = true;
  return { skip: false, silent };
}

module.exports = { TYPES, IMPORTANT, normalize, decide, inQuiet };
