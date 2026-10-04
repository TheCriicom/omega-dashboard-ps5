'use strict';
// Radio per il lettore musicale: catalogo pubblico di radio-browser.info
// (stazioni inserite dalla comunità, dati CC0). Il server sceglie un nodo,
// tiene solo i campi che servono alla console, scarta i formati che il
// lettore non sa aprire e fa da proxy per i loghi (host qualsiasi, spesso http).
// La console poi apre il flusso della stazione direttamente.
const crypto = require('node:crypto');
const { HttpError } = require('../http');
const limiter = require('../ratelimit');
const { safeFetch, readLimited } = require('../safefetch');

const UA = 'Omega/1.0 (+https://play.omegasuite.it)';
const FALLBACK = ['de1.api.radio-browser.info', 'de2.api.radio-browser.info', 'fi1.api.radio-browser.info', 'nl1.api.radio-browser.info'];
const CODECS = new Set(['MP3', 'AAC', 'AAC+', 'OGG', 'OPUS', 'FLAC', 'UNKNOWN', '']);
const MAX_ICON = 512 * 1024;

let nodes = { list: FALLBACK, at: 0 };
const cache = new Map();   // chiave → { at, data }

async function hosts() {
  if (Date.now() - nodes.at < 6 * 3600e3) return nodes.list;
  try {
    const r = await fetch('https://all.api.radio-browser.info/json/servers', { headers: { 'user-agent': UA }, signal: AbortSignal.timeout(5000) });
    const list = [...new Set((await r.json()).map((s) => s.name).filter((n) => /^[a-z0-9.-]+\.radio-browser\.info$/.test(n)))];
    nodes = { list: list.length ? list : FALLBACK, at: Date.now() };
  } catch { nodes = { list: FALLBACK, at: Date.now() }; }
  return nodes.list;
}

// GET su un nodo a caso, poi sugli altri se non risponde
async function rb(pathAndQuery, ttl = 600e3) {
  const hit = cache.get(pathAndQuery);
  if (hit && Date.now() - hit.at < ttl) return hit.data;
  const list = [...await hosts()].sort(() => Math.random() - 0.5);
  let last;
  for (const h of list.slice(0, 3)) {
    try {
      const r = await fetch(`https://${h}${pathAndQuery}`, { headers: { 'user-agent': UA }, signal: AbortSignal.timeout(8000) });
      if (!r.ok) { last = new Error(`HTTP ${r.status}`); continue; }
      const data = await r.json();
      cache.set(pathAndQuery, { at: Date.now(), data });
      if (cache.size > 500) cache.delete(cache.keys().next().value);
      return data;
    } catch (e) { last = e; }
  }
  throw new HttpError(502, 'radio_unavailable', last && last.message);
}

// id breve e stabile per il logo, così la console non vede l'URL esterno
const icons = new Map();
function iconId(url) {
  const id = crypto.createHash('sha1').update(url).digest('base64url').slice(0, 16);
  if (!icons.has(id)) { icons.set(id, url); if (icons.size > 20000) icons.delete(icons.keys().next().value); }
  return id;
}

function station(s) {
  const url = s.url_resolved || s.url;
  if (!url || !/^https?:\/\//i.test(url)) return null;
  const codec = String(s.codec || '').toUpperCase();
  if (!CODECS.has(codec) && !s.hls) return null;
  return {
    id: s.stationuuid,
    name: String(s.name || '').trim().slice(0, 120),
    url,
    icon: s.favicon && /^https?:\/\//i.test(s.favicon) ? `/api/v1/radio/icon/${iconId(s.favicon)}` : '',
    tags: String(s.tags || '').split(',').map((t) => t.trim()).filter(Boolean).slice(0, 4),
    country: s.countrycode || '',
    language: String(s.language || '').split(',')[0].trim(),
    codec: codec || (s.hls ? 'HLS' : ''),
    bitrate: Number(s.bitrate) || 0,
    votes: Number(s.votes) || 0,
  };
}
const stations = (arr) => (Array.isArray(arr) ? arr : []).map(station).filter(Boolean);
const clean = (v, n = 80) => String(v || '').replace(/[\u0000-\u001f]/g, '').trim().slice(0, n);
const int = (v, d, max) => Math.max(0, Math.min(max, Number.parseInt(v, 10) || d));

function throttle(auth) {
  if (limiter.hit(`radio|${auth.accountId}`, 120, 60)) throw new HttpError(429, 'too_many_requests');
}

// GET /api/v1/radio/search?q=&tag=&country=XX&limit=&offset=
async function search({ auth, url }) {
  throttle(auth);
  const p = url.searchParams;
  const q = new URLSearchParams({
    hidebroken: 'true', order: 'clickcount', reverse: 'true',
    limit: String(int(p.get('limit'), 40, 100)), offset: String(int(p.get('offset'), 0, 5000)),
  });
  const name = clean(p.get('q')), tag = clean(p.get('tag'), 40), cc = clean(p.get('country'), 2).toUpperCase();
  if (name) q.set('name', name);
  if (tag) q.set('tag', tag.toLowerCase());
  if (/^[A-Z]{2}$/.test(cc)) q.set('countrycode', cc);
  const data = await rb(`/json/stations/search?${q}`);
  return { status: 200, body: { stations: stations(data) } };
}

// GET /api/v1/radio/top?country=XX → le più ascoltate (del paese, se indicato)
async function top({ auth, url }) {
  throttle(auth);
  const cc = clean(url.searchParams.get('country'), 2).toUpperCase();
  const limit = int(url.searchParams.get('limit'), 40, 100);
  const data = /^[A-Z]{2}$/.test(cc)
    ? await rb(`/json/stations/search?${new URLSearchParams({ countrycode: cc, hidebroken: 'true', order: 'clickcount', reverse: 'true', limit: String(limit) })}`, 3600e3)
    : await rb(`/json/stations/topclick/${limit}?hidebroken=true`, 3600e3);
  return { status: 200, body: { stations: stations(data) } };
}

// GET /api/v1/radio/tags → generi più usati
async function tags({ auth }) {
  throttle(auth);
  const data = await rb('/json/tags?order=stationcount&reverse=true&limit=60&hidebroken=true', 24 * 3600e3);
  const list = (Array.isArray(data) ? data : [])
    .filter((t) => t.name && t.name.length <= 24 && t.stationcount >= 50)
    .map((t) => ({ name: t.name, count: t.stationcount }));
  return { status: 200, body: { tags: list } };
}

// POST /api/v1/radio/click/:id → conta l'ascolto come chiede radio-browser
async function click({ auth, params }) {
  throttle(auth);
  if (!/^[0-9a-f-]{36}$/i.test(params.id)) throw new HttpError(400, 'bad_id');
  rb(`/json/url/${params.id}`, 0).catch(() => {});
  return { status: 200, body: { ok: true } };
}

// GET /api/v1/radio/icon/:id
async function icon({ params, res }) {
  const src = icons.get(params.id);
  if (!src) throw new HttpError(404, 'unknown_icon');
  let r;
  try { ({ res: r } = await safeFetch(src, 'image/png,image/jpeg,image/webp,image/*;q=0.8', 'GET', 8000)); }
  catch (e) { throw new HttpError(502, 'icon_unavailable', e.message); }
  const type = r.headers.get('content-type') || '';
  if (!/^image\/(jpeg|png|webp|gif)/.test(type)) throw new HttpError(415, 'not_an_image');
  const buf = await readLimited(r, MAX_ICON + 1);
  if (buf.length > MAX_ICON) throw new HttpError(413, 'icon_too_large');
  res.writeHead(200, { 'content-type': type, 'content-length': buf.length, 'cache-control': 'max-age=86400' });
  res.end(buf);
  return { sent: true };
}

module.exports = { search, top, tags, click, icon };
