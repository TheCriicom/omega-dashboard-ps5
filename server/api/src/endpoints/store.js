'use strict';
// Store, in due sezioni.
//
// Homebrew: gli utenti pubblicano app e giochi indicando il link diretto al
// file (.pkg, .zip o .elf); gli altri votano, danno stelle e commentano.
// "Installa" risolve il link e dice alla console che tipo di file è.
//
// Libreria personale: ogni account indica l'URL di un proprio JSON e la
// libreria si riempie con i titoli descritti lì.
//
// Le immagini stanno su siti terzi: le scarica e le serve il server, la
// console non li contatta. Il file da installare invece lo scarica la console.
const fsp = require('node:fs/promises');
const path = require('node:path');
const config = require('../config');
const db = require('../db');
const { HttpError, readJson } = require('../http');
const { notify, notifyAdmins } = require('../notify');
const messages = require('../messages');
const { safeFetch, readLimited } = require('../safefetch');
const limiter = require('../ratelimit');
const { userCard } = require('../relations');
const { multiline, likePattern } = require('../text');

const MAX_SHOTS = 8;
const ASSET_DIR = config.storeAssetDir;
const MAX_IMG = 5 * 1024 * 1024;
const MAX_JSON = 4 * 1024 * 1024;
const CATEGORIES = new Set(['app', 'gioco', 'utility', 'emulatore', 'tema', 'altro']);
// categoria dello Store → categoria di Payload Manager (file .elf.json accanto al payload)
const PLD_CATEGORY = { app: 'Apps', gioco: 'Games', utility: 'Utilities', emulatore: 'Emulators', tema: 'Themes', altro: 'Utilities' };

function cleanText(v, max, { required = false, field = 'text' } = {}) {
  const s = multiline(v);
  if (!s) { if (required) throw new HttpError(400, `${field}_required`); return null; }
  return s.slice(0, max);
}

function validUrl(v, { required = false, field = 'url' } = {}) {
  const s = String(v == null ? '' : v).trim();
  if (!s) { if (required) throw new HttpError(400, `${field}_required`); return null; }
  if (!/^https?:\/\//i.test(s)) throw new HttpError(400, `${field}_invalid`, 'serve un URL http(s)');
  if (s.length > 1000) throw new HttpError(400, `${field}_too_long`);
  return s;
}

// hashtag: minuscoli, [a-z0-9_], senza '#', al massimo 12
function cleanHashtags(v) {
  let list = [];
  if (Array.isArray(v)) list = v;
  else if (typeof v === 'string') list = v.split(/[\s,#]+/);
  const out = [];
  for (const raw of list) {
    const t = String(raw || '').toLowerCase().replace(/[^a-z0-9_]/g, '').slice(0, 24);
    if (t && !out.includes(t)) out.push(t);
    if (out.length >= 12) break;
  }
  return out;
}

function cleanScreens(v) {
  const list = Array.isArray(v) ? v : [];
  const out = [];
  for (const raw of list) {
    const u = validUrl(raw, { field: 'screenshot' });
    if (u) out.push(u);
    if (out.length >= MAX_SHOTS) break;
  }
  return out;
}

function normKind(v, url) {
  const k = String(v || '').toLowerCase();
  if (k === 'pkg' || k === 'zip' || k === 'elf') return k;
  return kindFromUrl(url) || 'auto';
}

function kindFromUrl(url) {
  try {
    const p = new URL(url).pathname.toLowerCase();
    if (p.endsWith('.pkg')) return 'pkg';
    if (p.endsWith('.zip')) return 'zip';
    if (p.endsWith('.elf')) return 'elf';
  } catch { /* */ }
  return null;
}

// Immagini servite dal server, con cache in memoria: chiave → { type, buf }.
const imgCache = new Map();
async function serveImage(res, key, url) {
  if (!url) throw new HttpError(404, 'no_image');
  // immagini del catalogo curato: file locali in store-assets/ ("asset:nome.jpg")
  if (url.startsWith('asset:')) {
    const name = url.slice(6);
    if (!/^[a-z0-9][a-z0-9._-]*\.(jpg|png|webp)$/i.test(name)) throw new HttpError(404, 'no_image');
    let buf;
    try { buf = await fsp.readFile(path.join(ASSET_DIR, name)); } catch { throw new HttpError(404, 'no_image'); }
    res.writeHead(200, { 'content-type': name.endsWith('.png') ? 'image/png' : name.endsWith('.webp') ? 'image/webp' : 'image/jpeg',
      'content-length': buf.length, 'cache-control': 'max-age=604800' });
    res.end(buf);
    return { sent: true };
  }
  let hit = imgCache.get(key);
  if (!hit) {
    const { res: r } = await safeFetch(url, 'image/webp,image/jpeg,image/png,image/*;q=0.8');
    const type = r.headers.get('content-type') || '';
    if (!/^image\/(jpeg|png|webp|gif)/.test(type)) { try { r.body && r.body.cancel(); } catch { /* */ } throw new HttpError(415, 'not_an_image'); }
    const buf = await readLimited(r, MAX_IMG + 1);
    if (buf.length > MAX_IMG) throw new HttpError(413, 'image_too_large');
    if (!buf.length) throw new HttpError(502, 'empty_image');
    hit = { type, buf };
    imgCache.set(key, hit);
    if (imgCache.size > 300) imgCache.delete(imgCache.keys().next().value);
  }
  res.writeHead(200, { 'content-type': hit.type, 'content-length': hit.buf.length, 'cache-control': 'max-age=86400' });
  res.end(hit.buf);
  return { sent: true };
}

function dropCachedImages(prefix) {
  for (const k of [...imgCache.keys()]) if (k.startsWith(prefix)) imgCache.delete(k);
}

// Testi della scheda nella lingua della richiesta: le voci del catalogo curato
// hanno le traduzioni in i18n, gli homebrew degli utenti restano come scritti.
function localized(row, lang) {
  const tr = row.i18n && lang && row.i18n[lang];
  return {
    tagline: (tr && tr.tagline) || row.tagline,
    description: (tr && tr.description) || row.description,
  };
}

// Scheda per l'elenco: campi brevi, senza descrizione.
function appCard(row, me, lang) {
  return {
    app_id: String(row.app_id), title: row.title, tagline: localized(row, lang).tagline, category: row.category,
    version: row.version, title_id: row.title_id, platform: row.platform, file_kind: row.file_kind, size_bytes: row.size_bytes ? Number(row.size_bytes) : null,
    has_icon: !!row.icon_url, has_cover: !!row.cover_url, hashtags: row.hashtags || [],
    downloads: Number(row.downloads || 0),
    likes: Number(row.likes || 0), dislikes: Number(row.dislikes || 0),
    rating: row.rating ? Number(Number(row.rating).toFixed(2)) : 0, ratings: Number(row.ratings || 0),
    comments: Number(row.comments || 0),
    author: userCard(row), mine: String(row.author_id) === String(me),
    created_at: row.created_at,
  };
}

const AGG = `
  (SELECT count(*)::int FROM lab_store_vote   v WHERE v.app_id=a.app_id AND v.value=1)  AS likes,
  (SELECT count(*)::int FROM lab_store_vote   v WHERE v.app_id=a.app_id AND v.value=-1) AS dislikes,
  (SELECT coalesce(avg(stars),0)  FROM lab_store_rating r WHERE r.app_id=a.app_id)      AS rating,
  (SELECT count(*)::int FROM lab_store_rating r WHERE r.app_id=a.app_id)                AS ratings,
  (SELECT count(*)::int FROM lab_store_comment c WHERE c.app_id=a.app_id AND NOT c.hidden) AS comments`;

// ------------------------------------------------------------- homebrew --
// GET /api/v1/store/apps?sort=recent|top|downloads&q=&tag=&mine=1
async function listApps({ auth, url, lang }) {
  const sort = url.searchParams.get('sort') || 'recent';
  const q = String(url.searchParams.get('q') || '').trim().slice(0, 48);
  const tag = String(url.searchParams.get('tag') || '').toLowerCase().replace(/[^a-z0-9_]/g, '').slice(0, 24);
  const mine = url.searchParams.get('mine') === '1';
  const where = ['a.published'];
  const args = [];
  if (mine) { args.push(auth.accountId); where.push(`a.author_id=$${args.length}`); }
  if (q) {
    // si cerca anche nel sottotitolo tradotto mostrato all'utente
    args.push(likePattern(q), lang);
    const p = `$${args.length - 1}`;
    where.push(`(a.title ILIKE ${p} OR a.tagline ILIKE ${p} OR a.i18n->$${args.length}->>'tagline' ILIKE ${p})`);
  }
  if (tag) { args.push(tag); where.push(`$${args.length} = ANY(a.hashtags)`); }
  const order = sort === 'downloads' ? 'a.downloads DESC, a.created_at DESC'
    : sort === 'top' ? 'likes DESC, rating DESC, a.created_at DESC'
    : 'a.created_at DESC';
  const r = await db.query(
    `SELECT a.*, acc.online_id, acc.avatar, acc.avatar_media, acc.avatar_frames, ${AGG}
       FROM lab_store_app a JOIN lab_account acc ON acc.account_id=a.author_id
      WHERE ${where.join(' AND ')}
      ORDER BY ${order} LIMIT 80`, args);
  return { status: 200, body: { apps: r.rows.map((row) => appCard(row, auth.accountId, lang)) } };
}

async function appById(appId) {
  const r = await db.query(
    `SELECT a.*, acc.online_id, acc.avatar, acc.avatar_media, acc.avatar_frames, ${AGG}
       FROM lab_store_app a JOIN lab_account acc ON acc.account_id=a.author_id
      WHERE a.app_id=$1`, [Number(appId) || 0]);
  return r.rows[0] || null;
}

// GET /api/v1/store/apps/:id
async function getApp({ auth, params, lang }) {
  const row = await appById(params.id);
  if (!row || (!row.published && String(row.author_id) !== String(auth.accountId))) throw new HttpError(404, 'app_not_found');
  const myVote = (await db.query('SELECT value FROM lab_store_vote WHERE app_id=$1 AND account_id=$2', [row.app_id, auth.accountId])).rows[0];
  const myRating = (await db.query('SELECT stars FROM lab_store_rating WHERE app_id=$1 AND account_id=$2', [row.app_id, auth.accountId])).rows[0];
  const comments = (await db.query(
    `SELECT c.comment_id::text, c.body, c.created_at, acc.online_id, acc.avatar, acc.avatar_media, acc.avatar_frames,
            (acc.account_id=$2) AS mine
       FROM lab_store_comment c JOIN lab_account acc ON acc.account_id=c.account_id
      WHERE c.app_id=$1 AND NOT c.hidden ORDER BY c.comment_id DESC LIMIT 50`, [row.app_id, auth.accountId])).rows;
  const screens = Array.isArray(row.screenshots) ? row.screenshots : [];
  const mine = String(row.author_id) === String(auth.accountId);
  const card = appCard(row, auth.accountId, lang);
  const body = {
    ...card,
    comment_count: card.comments, // qui "comments" è l'elenco, il conteggio passa in comment_count
    description: localized(row, lang).description,
    title_id: row.title_id,
    homepage_url: row.homepage_url,
    license: row.license,
    nscreens: screens.length,
    my_vote: myVote ? myVote.value : 0,
    my_rating: myRating ? myRating.stars : 0,
    comments: comments.map((c) => ({ comment_id: c.comment_id, body: c.body, created_at: c.created_at, mine: c.mine, author: userCard(c) })),
  };
  // all'autore servono i valori originali per la modifica
  if (mine) body.edit = { download_url: row.download_url, icon_url: row.icon_url, cover_url: row.cover_url, screenshots: screens };
  return { status: 200, body };
}

function appFromBody(body) {
  const download_url = validUrl(body.download_url || body.url, { required: true, field: 'download_url' });
  const file_kind = normKind(body.file_kind, download_url);
  const cat = String(body.category || 'app').toLowerCase();
  return {
    title: cleanText(body.title, 80, { required: true, field: 'title' }),
    tagline: cleanText(body.tagline, 120),
    description: cleanText(body.description, 4000),
    category: CATEGORIES.has(cat) ? cat : 'altro',
    version: cleanText(body.version, 24),
    title_id: (() => { const t = cleanText(body.title_id, 16); return t && /^[A-Z]{4}[0-9]{5}$/.test(t) ? t : null; })(),
    icon_url: validUrl(body.icon_url, { field: 'icon_url' }),
    cover_url: validUrl(body.cover_url, { field: 'cover_url' }),
    screenshots: JSON.stringify(cleanScreens(body.screenshots)),
    hashtags: cleanHashtags(body.hashtags),
    download_url, file_kind,
    size_bytes: Number.isFinite(body.size_bytes) ? Math.max(0, Math.floor(body.size_bytes)) : null,
    platform: (() => { const p = String(body.platform || '').toUpperCase(); return p === 'PS4' || p === 'PS5' ? p : null; })(),
    homepage_url: validUrl(body.homepage_url, { field: 'homepage_url' }),
    license: cleanText(body.license, 40),
  };
}

// POST /api/v1/store/apps
async function publishApp({ req, auth }) {
  if (limiter.hit(`store_pub|${auth.accountId}`, 20, 3600)) throw new HttpError(429, 'too_many_publishes');
  const body = await readJson(req);
  // chi pubblica dichiara di avere i diritti di redistribuzione (termini d'uso)
  if (body.rights_confirmed !== true) throw new HttpError(400, 'rights_not_confirmed', 'conferma di essere l\'autore o che la licenza permette la redistribuzione');
  const a = appFromBody(body);
  const r = await db.query(
    `INSERT INTO lab_store_app
       (author_id, title, tagline, description, category, version, title_id, icon_url, cover_url, screenshots, hashtags, download_url, file_kind, size_bytes, platform, homepage_url, license, rights_confirmed)
     VALUES ($1,$2,$3,$4,$5,$6,$7,$8,$9,$10::jsonb,$11,$12,$13,$14,$15,$16,$17,true)
     RETURNING app_id`,
    [auth.accountId, a.title, a.tagline, a.description, a.category, a.version, a.title_id, a.icon_url, a.cover_url, a.screenshots, a.hashtags, a.download_url, a.file_kind, a.size_bytes, a.platform, a.homepage_url, a.license]);
  return { status: 201, body: { app_id: String(r.rows[0].app_id) } };
}

// POST /api/v1/store/apps/:id  — modifica (solo l'autore)
async function updateApp({ req, auth, params }) {
  const row = await appById(params.id);
  if (!row) throw new HttpError(404, 'app_not_found');
  if (String(row.author_id) !== String(auth.accountId)) throw new HttpError(403, 'not_author');
  const body = await readJson(req);
  const a = appFromBody(body);
  await db.query(
    `UPDATE lab_store_app SET title=$2, tagline=$3, description=$4, category=$5, version=$6, title_id=$7,
       icon_url=$8, cover_url=$9, screenshots=$10::jsonb, hashtags=$11, download_url=$12, file_kind=$13, size_bytes=$14,
       platform=$15, homepage_url=$16, license=$17, i18n=NULL, updated_at=now()   -- le traduzioni non valgono più
     WHERE app_id=$1`,
    [row.app_id, a.title, a.tagline, a.description, a.category, a.version, a.title_id, a.icon_url, a.cover_url, a.screenshots, a.hashtags, a.download_url, a.file_kind, a.size_bytes, a.platform, a.homepage_url, a.license]);
  dropCachedImages(`app:${row.app_id}:`);
  return { status: 200, body: { result: 'ok' } };
}

// DELETE /api/v1/store/apps/:id
async function deleteApp({ auth, params }) {
  const row = await appById(params.id);
  if (!row) throw new HttpError(404, 'app_not_found');
  if (String(row.author_id) !== String(auth.accountId)) throw new HttpError(403, 'not_author');
  await db.query('DELETE FROM lab_store_app WHERE app_id=$1', [row.app_id]);
  dropCachedImages(`app:${row.app_id}:`);
  return { status: 200, body: { result: 'ok' } };
}

// GET /api/v1/store/apps/:id/cover  e  /icon
function appImage(kind) {
  return async function ({ params, res }) {
    const r = await db.query(`SELECT ${kind}_url AS u FROM lab_store_app WHERE app_id=$1`, [Number(params.id) || 0]);
    return serveImage(res, `app:${Number(params.id)}:${kind}`, r.rows[0] && r.rows[0].u);
  };
}

// GET /api/v1/store/apps/:id/shot/:n
async function appShot({ params, res }) {
  const n = Number(params.n) || 0;
  const r = await db.query('SELECT screenshots FROM lab_store_app WHERE app_id=$1', [Number(params.id) || 0]);
  const arr = r.rows[0] && Array.isArray(r.rows[0].screenshots) ? r.rows[0].screenshots : [];
  return serveImage(res, `app:${Number(params.id)}:shot:${n}`, arr[n]);
}

// ---------------------------------------------------- voti e valutazioni --
// POST /api/v1/store/apps/:id/vote  { value: 1 | -1 | 0 }
async function vote({ req, auth, params }) {
  const row = await appById(params.id);
  if (!row) throw new HttpError(404, 'app_not_found');
  const body = await readJson(req);
  const v = Number(body.value);
  if (v === 0) await db.query('DELETE FROM lab_store_vote WHERE app_id=$1 AND account_id=$2', [row.app_id, auth.accountId]);
  else if (v === 1 || v === -1) await db.query(
    `INSERT INTO lab_store_vote (app_id, account_id, value) VALUES ($1,$2,$3)
     ON CONFLICT (app_id, account_id) DO UPDATE SET value=EXCLUDED.value, created_at=now()`, [row.app_id, auth.accountId, v]);
  else throw new HttpError(400, 'invalid_value');
  const c = (await db.query(
    `SELECT count(*) FILTER (WHERE value=1) AS likes, count(*) FILTER (WHERE value=-1) AS dislikes
       FROM lab_store_vote WHERE app_id=$1`, [row.app_id])).rows[0];
  return { status: 200, body: { likes: Number(c.likes), dislikes: Number(c.dislikes), my_vote: v === 1 || v === -1 ? v : 0 } };
}

// POST /api/v1/store/apps/:id/rate  { stars: 1..5 }
async function rate({ req, auth, params }) {
  const row = await appById(params.id);
  if (!row) throw new HttpError(404, 'app_not_found');
  const body = await readJson(req);
  const s = Math.round(Number(body.stars));
  if (!(s >= 1 && s <= 5)) throw new HttpError(400, 'invalid_stars');
  await db.query(
    `INSERT INTO lab_store_rating (app_id, account_id, stars) VALUES ($1,$2,$3)
     ON CONFLICT (app_id, account_id) DO UPDATE SET stars=EXCLUDED.stars, created_at=now()`, [row.app_id, auth.accountId, s]);
  const c = (await db.query('SELECT coalesce(avg(stars),0) AS rating, count(*)::int AS ratings FROM lab_store_rating WHERE app_id=$1', [row.app_id])).rows[0];
  return { status: 200, body: { rating: Number(Number(c.rating).toFixed(2)), ratings: c.ratings, my_rating: s } };
}

// ------------------------------------------------------------- commenti --
// GET /api/v1/store/apps/:id/comments
async function comments({ auth, params }) {
  const r = await db.query(
    `SELECT c.comment_id::text, c.body, c.created_at, acc.online_id, acc.avatar, acc.avatar_media, acc.avatar_frames,
            (acc.account_id=$2) AS mine
       FROM lab_store_comment c JOIN lab_account acc ON acc.account_id=c.account_id
      WHERE c.app_id=$1 AND NOT c.hidden ORDER BY c.comment_id DESC LIMIT 100`, [Number(params.id) || 0, auth.accountId]);
  return { status: 200, body: { comments: r.rows.map((c) => ({ comment_id: c.comment_id, body: c.body, created_at: c.created_at, mine: c.mine, author: userCard(c) })) } };
}

// POST /api/v1/store/apps/:id/comments  { text }
async function addComment({ req, auth, params }) {
  if (limiter.hit(`store_comment|${auth.accountId}`, 30, 600)) throw new HttpError(429, 'too_many_comments');
  const row = await appById(params.id);
  if (!row) throw new HttpError(404, 'app_not_found');
  const body = await readJson(req);
  const text = cleanText(body.text, 500, { required: true, field: 'text' });
  const r = await db.query(
    'INSERT INTO lab_store_comment (app_id, account_id, body) VALUES ($1,$2,$3) RETURNING comment_id::text, created_at',
    [row.app_id, auth.accountId, text]);
  if (String(row.author_id) !== String(auth.accountId)) {
    await notify(row.author_id, 'store_comment', { actorId: auth.accountId, title: (l) => messages.t(l, 'notify.store_comment', { actor: auth.onlineId, app: row.title }), body: text, ref: String(row.app_id) });
  }
  return { status: 201, body: { comment_id: r.rows[0].comment_id, created_at: r.rows[0].created_at } };
}

// DELETE /api/v1/store/apps/:id/comments/:commentId  (autore del commento o dell'app)
async function deleteComment({ auth, params }) {
  const row = await appById(params.id);
  if (!row) throw new HttpError(404, 'app_not_found');
  const c = (await db.query('SELECT account_id FROM lab_store_comment WHERE comment_id=$1 AND app_id=$2', [Number(params.commentId) || 0, row.app_id])).rows[0];
  if (!c) throw new HttpError(404, 'comment_not_found');
  if (String(c.account_id) !== String(auth.accountId) && String(row.author_id) !== String(auth.accountId)) throw new HttpError(403, 'not_allowed');
  await db.query('DELETE FROM lab_store_comment WHERE comment_id=$1', [Number(params.commentId) || 0]);
  return { status: 200, body: { result: 'ok' } };
}

// ------------------------------------------------------------- download --
// HEAD sul file per tipo e dimensione: la console riceve il link diretto e
// sa come installarlo.
async function resolveDownload(download_url, declaredKind) {
  let kind = declaredKind && declaredKind !== 'auto' ? declaredKind : kindFromUrl(download_url);
  let size = null, ctype = null, finalUrl = download_url;
  try {
    const { res, url: resolved } = await safeFetch(download_url, '*/*', 'HEAD', 12000);
    // si restituisce il link dopo i redirect: github.com risponde con header
    // oltre i 5 KB che la console non accetta, l'host finale no
    if (res.ok && resolved) finalUrl = resolved;
    try { res.body && res.body.cancel && res.body.cancel(); } catch { /* */ }
    ctype = res.headers.get('content-type');
    const len = res.headers.get('content-length');
    if (len) size = Number(len);
    if (!kind) {
      if (/application\/zip|x-zip/.test(ctype || '')) kind = 'zip';
      else if (/application\/octet-stream/.test(ctype || '') && /\.pkg(\?|$)/i.test(download_url)) kind = 'pkg';
    }
  } catch { /* HEAD non sempre permesso: si usa l'estensione */ }
  let filename = null;
  try { filename = decodeURIComponent(new URL(download_url).pathname.split('/').pop() || '') || null; } catch { /* */ }
  return { url: finalUrl, filename, kind: kind || 'auto', size, content_type: ctype };
}

// GET /api/v1/store/apps/:id/download
async function appDownload({ auth, params, lang }) {
  const row = await appById(params.id);
  if (!row || (!row.published && String(row.author_id) !== String(auth.accountId))) throw new HttpError(404, 'app_not_found');
  await db.query('UPDATE lab_store_app SET downloads=downloads+1 WHERE app_id=$1', [row.app_id]);
  const info = await resolveDownload(row.download_url, row.file_kind);
  return {
    status: 200,
    body: {
      ...info, title: row.title, title_id: row.title_id, version: row.version, category: PLD_CATEGORY[row.category] || 'Utilities',
      summary: localized(row, lang).tagline || String(localized(row, lang).description || '').slice(0, 180), homepage_url: row.homepage_url,
      size: info.size || (row.size_bytes ? Number(row.size_bytes) : null),
    },
  };
}

// --------------------------------------------------- libreria personale --
// GET /api/v1/library/source
async function getSource({ auth }) {
  const r = await db.query('SELECT url, name, last_sync, last_status, item_count FROM lab_library_source WHERE account_id=$1', [auth.accountId]);
  return { status: 200, body: { source: r.rows[0] || null } };
}

// POST /api/v1/library/source  { url, name? }
async function setSource({ req, auth }) {
  const body = await readJson(req);
  const url = validUrl(body.url, { required: true, field: 'url' });
  const name = cleanText(body.name, 60);
  await db.query(
    `INSERT INTO lab_library_source (account_id, url, name, updated_at) VALUES ($1,$2,$3,now())
     ON CONFLICT (account_id) DO UPDATE SET url=EXCLUDED.url, name=EXCLUDED.name, updated_at=now()`,
    [auth.accountId, url, name]);
  return { status: 200, body: { result: 'ok' } };
}

// Un elemento del JSON della libreria; si accettano diversi nomi di campo.
function libItem(it, idx) {
  if (!it || typeof it !== 'object') return null;
  const title = cleanText(it.title || it.name, 120);
  const download_url = (() => { try { return validUrl(it.url || it.download_url || it.download || it.file || it.pkg, { field: 'url' }); } catch { return null; } })();
  if (!title || !download_url) return null;
  const cover = (() => { try { return validUrl(it.cover || it.cover_url || it.image || it.icon, { field: 'cover' }); } catch { return null; } })();
  const screensRaw = Array.isArray(it.screenshots) ? it.screenshots : Array.isArray(it.images) ? it.images : [];
  const plat = String(it.platform || it.plat || '').toUpperCase();
  const tid = cleanText(it.title_id || it.titleId || it.tid, 16);
  const ext = cleanText(it.id || it.ext_id, 120) || `i${idx}:${download_url}`.slice(0, 120);
  return {
    ext_id: ext, title,
    description: cleanText(it.description || it.desc || it.text, 4000),
    platform: plat === 'PS4' || plat === 'PS5' ? plat : null,
    version: cleanText(it.version || it.ver, 24),
    title_id: tid && /^[A-Z]{4}[0-9]{5}$/.test(tid) ? tid : null,
    cover_url: cover,
    screenshots: JSON.stringify(cleanScreens(screensRaw)),
    download_url, file_kind: normKind(it.type || it.kind || it.file_kind, download_url),
    size_bytes: Number.isFinite(it.size) ? Math.max(0, Math.floor(it.size)) : (Number.isFinite(it.size_bytes) ? Math.max(0, Math.floor(it.size_bytes)) : null),
  };
}

// POST /api/v1/library/sync — rilegge il JSON e riempie la libreria.
async function syncLibrary({ auth }) {
  if (limiter.hit(`lib_sync|${auth.accountId}`, 20, 600)) throw new HttpError(429, 'too_many_syncs');
  const src = (await db.query('SELECT url FROM lab_library_source WHERE account_id=$1', [auth.accountId])).rows[0];
  if (!src) throw new HttpError(409, 'no_source', 'imposta prima l\'URL della tua libreria');
  let data;
  try {
    const { res } = await safeFetch(src.url, 'application/json,text/plain,*/*', 'GET', 20000);
    const buf = await readLimited(res, MAX_JSON + 1);
    if (buf.length > MAX_JSON) throw new HttpError(413, 'json_too_large');
    data = JSON.parse(buf.toString('utf8'));
  } catch (err) {
    if (err instanceof HttpError) { await db.query('UPDATE lab_library_source SET last_status=$2, last_sync=now() WHERE account_id=$1', [auth.accountId, err.code]); throw err; }
    await db.query('UPDATE lab_library_source SET last_status=$2, last_sync=now() WHERE account_id=$1', [auth.accountId, 'invalid_json']);
    throw new HttpError(422, 'invalid_json', 'il JSON non è valido o non è raggiungibile');
  }
  const rawList = Array.isArray(data) ? data : Array.isArray(data.games) ? data.games
    : Array.isArray(data.items) ? data.items : Array.isArray(data.library) ? data.library : [];
  const libName = cleanText(data && data.name, 60);
  const items = [];
  const seen = new Set();
  for (let i = 0; i < rawList.length && items.length < 500; i++) {
    const it = libItem(rawList[i], i);
    if (it && !seen.has(it.ext_id)) { seen.add(it.ext_id); items.push(it); }
  }
  // via gli elementi non più presenti, gli altri inseriti o aggiornati
  const client = await db.pool.connect();
  try {
    await client.query('BEGIN');
    if (items.length) {
      await client.query('DELETE FROM lab_library_item WHERE account_id=$1 AND NOT (ext_id = ANY($2::text[]))', [auth.accountId, items.map((x) => x.ext_id)]);
    } else {
      await client.query('DELETE FROM lab_library_item WHERE account_id=$1', [auth.accountId]);
    }
    for (const it of items) {
      await client.query(
        `INSERT INTO lab_library_item (account_id, ext_id, title, description, platform, version, title_id, cover_url, screenshots, download_url, file_kind, size_bytes)
         VALUES ($1,$2,$3,$4,$5,$6,$7,$8,$9::jsonb,$10,$11,$12)
         ON CONFLICT (account_id, ext_id) DO UPDATE SET
           title=EXCLUDED.title, description=EXCLUDED.description, platform=EXCLUDED.platform, version=EXCLUDED.version,
           title_id=EXCLUDED.title_id, cover_url=EXCLUDED.cover_url, screenshots=EXCLUDED.screenshots,
           download_url=EXCLUDED.download_url, file_kind=EXCLUDED.file_kind, size_bytes=EXCLUDED.size_bytes`,
        [auth.accountId, it.ext_id, it.title, it.description, it.platform, it.version, it.title_id, it.cover_url, it.screenshots, it.download_url, it.file_kind, it.size_bytes]);
    }
    await client.query('UPDATE lab_library_source SET name=coalesce($2,name), last_sync=now(), last_status=$3, item_count=$4 WHERE account_id=$1',
      [auth.accountId, libName, 'ok', items.length]);
    await client.query('COMMIT');
  } catch (err) { await client.query('ROLLBACK'); throw err; } finally { client.release(); }
  dropCachedImages(`lib:${auth.accountId}:`);
  return { status: 200, body: { result: 'ok', count: items.length, name: libName } };
}

function itemCard(row) {
  const screens = Array.isArray(row.screenshots) ? row.screenshots : [];
  return {
    item_id: String(row.item_id), title: row.title, platform: row.platform, version: row.version,
    title_id: row.title_id, file_kind: row.file_kind, size_bytes: row.size_bytes ? Number(row.size_bytes) : null,
    has_cover: !!row.cover_url, nscreens: screens.length,
  };
}

// GET /api/v1/library/items?q=&platform=PS4|PS5
async function listItems({ auth, url }) {
  const q = String(url.searchParams.get('q') || '').trim().slice(0, 48);
  const plat = String(url.searchParams.get('platform') || '').toUpperCase();
  const where = ['account_id=$1']; const args = [auth.accountId];
  if (q) { args.push(likePattern(q)); where.push(`title ILIKE $${args.length}`); }
  if (plat === 'PS4' || plat === 'PS5') { args.push(plat); where.push(`platform=$${args.length}`); }
  const r = await db.query(
    `SELECT item_id, title, platform, version, title_id, file_kind, size_bytes, cover_url, screenshots
       FROM lab_library_item WHERE ${where.join(' AND ')} ORDER BY title LIMIT 500`, args);
  return { status: 200, body: { items: r.rows.map(itemCard) } };
}

async function itemByIdFor(itemId, accountId) {
  const r = await db.query('SELECT * FROM lab_library_item WHERE item_id=$1 AND account_id=$2', [Number(itemId) || 0, accountId]);
  return r.rows[0] || null;
}

// GET /api/v1/library/items/:id
async function getItem({ auth, params }) {
  const row = await itemByIdFor(params.id, auth.accountId);
  if (!row) throw new HttpError(404, 'item_not_found');
  const screens = Array.isArray(row.screenshots) ? row.screenshots : [];
  return { status: 200, body: { ...itemCard(row), description: row.description, nscreens: screens.length } };
}

// GET /api/v1/library/items/:id/cover
async function itemCover({ auth, params, res }) {
  const row = await itemByIdFor(params.id, auth.accountId);
  if (!row) throw new HttpError(404, 'item_not_found');
  return serveImage(res, `lib:${auth.accountId}:${row.item_id}:cover`, row.cover_url);
}

// GET /api/v1/library/items/:id/shot/:n
async function itemShot({ auth, params, res }) {
  const row = await itemByIdFor(params.id, auth.accountId);
  if (!row) throw new HttpError(404, 'item_not_found');
  const arr = Array.isArray(row.screenshots) ? row.screenshots : [];
  return serveImage(res, `lib:${auth.accountId}:${row.item_id}:shot:${Number(params.n) || 0}`, arr[Number(params.n) || 0]);
}

// GET /api/v1/library/items/:id/download
async function itemDownload({ auth, params }) {
  const row = await itemByIdFor(params.id, auth.accountId);
  if (!row) throw new HttpError(404, 'item_not_found');
  const info = await resolveDownload(row.download_url, row.file_kind);
  return { status: 200, body: { ...info, title: row.title, title_id: row.title_id, size: info.size || (row.size_bytes ? Number(row.size_bytes) : null) } };
}

// --------------------------------------------------------- segnalazioni --
const REPORT_REASONS = new Set(['pirateria', 'malware', 'contenuto_offensivo', 'spam', 'link_rotto', 'altro']);

// POST /api/v1/store/apps/:id/report  { reason, note?, comment_id? }
async function report({ req, auth, params }) {
  if (limiter.hit(`report|${auth.accountId}`, 20, 3600)) throw new HttpError(429, 'too_many_reports');
  const row = await appById(params.id);
  if (!row) throw new HttpError(404, 'app_not_found');
  const body = await readJson(req);
  const reason = String(body.reason || '');
  if (!REPORT_REASONS.has(reason)) throw new HttpError(400, 'invalid_reason');
  const note = cleanText(body.note, 500);
  let commentId = null;
  if (body.comment_id) {
    const c = (await db.query('SELECT comment_id, account_id FROM lab_store_comment WHERE comment_id=$1 AND app_id=$2', [Number(body.comment_id) || 0, row.app_id])).rows[0];
    if (!c) throw new HttpError(404, 'comment_not_found');
    if (String(c.account_id) === String(auth.accountId)) throw new HttpError(400, 'cannot_report_own');
    commentId = c.comment_id;
  } else if (String(row.author_id) === String(auth.accountId)) throw new HttpError(400, 'cannot_report_own');
  await db.query(
    `INSERT INTO lab_store_report (app_id, comment_id, reporter_id, reason, note) VALUES ($1,$2,$3,$4,$5) ON CONFLICT DO NOTHING`,
    [row.app_id, commentId, auth.accountId, reason, note]);
  // raggiunta la soglia il contenuto si oscura in attesa di verifica
  const n = (await db.query(
    `SELECT count(DISTINCT reporter_id)::int AS n FROM lab_store_report
      WHERE app_id=$1 AND coalesce(comment_id,0)=coalesce($2::bigint,0) AND status='open'`, [row.app_id, commentId])).rows[0].n;
  let hidden = false;
  if (n >= config.autoHideReports) {
    if (commentId) await db.query('UPDATE lab_store_comment SET hidden=true WHERE comment_id=$1', [commentId]);
    else await db.query(`UPDATE lab_store_app SET published=false, hidden_reason='Oscurato in attesa di verifica (segnalazioni)' WHERE app_id=$1`, [row.app_id]);
    hidden = true;
  }
  await notifyAdmins('admin_report', {
    actorId: auth.accountId,
    title: (l) => messages.t(l, 'notify.admin_report', { title: row.title }),
    body: (l) => messages.tOr(l, `report_reason.${reason}`, reason) + (commentId ? messages.t(l, 'notify.report_on_comment') : '') + (hidden ? messages.t(l, 'notify.report_hidden') : ''),
    ref: String(row.app_id),
  });
  return { status: 201, body: { result: 'reported', hidden } };
}

module.exports = {
  listApps, getApp, publishApp, updateApp, deleteApp, report,
  appCover: appImage('cover'), appIcon: appImage('icon'), appShot, appDownload,
  vote, rate, comments, addComment, deleteComment,
  getSource, setSource, syncLibrary, listItems, getItem, itemCover, itemShot, itemDownload,
};
