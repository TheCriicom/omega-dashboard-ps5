'use strict';
// Notizie per la home: legge ogni 30 minuti alcuni feed RSS pubblici di
// videogiochi e li salva in lab_news, tenendo gli ultimi 300 articoli.
// Parser minimo senza dipendenze (<item> con title/link/description/pubDate/
// guid; immagine da media:content, enclosure o prima <img> della descrizione).
const db = require('./db');

const FEEDS = [
  { name: 'PlayStation Blog', url: 'https://blog.playstation.com/feed/', color: 0 },
  { name: 'Multiplayer.it', url: 'https://multiplayer.it/feed/rss/homepage/', color: 3 },
  { name: 'Everyeye', url: 'https://www.everyeye.it/feed/feed_news_rss.asp', color: 4 },
  { name: 'IGN Italia', url: 'https://it.ign.com/feed.xml', color: 1 },
  { name: 'Push Square', url: 'https://www.pushsquare.com/feeds/latest', color: 2 },
];
const EVERY_MS = 30 * 60 * 1000;

const ENT = { amp: '&', lt: '<', gt: '>', quot: '"', apos: "'", nbsp: ' ', egrave: 'è', eacute: 'é', agrave: 'à', ograve: 'ò', ugrave: 'ù', igrave: 'ì', rsquo: '’', lsquo: '‘', ldquo: '“', rdquo: '”', hellip: '…', ndash: '–', mdash: '—' };
function decode(s) {
  return String(s || '')
    .replace(/<!\[CDATA\[([\s\S]*?)\]\]>/g, '$1')
    .replace(/&#(\d+);/g, (_, n) => String.fromCodePoint(Number(n)))
    .replace(/&#x([0-9a-f]+);/gi, (_, n) => String.fromCodePoint(parseInt(n, 16)))
    .replace(/&([a-z]+);/gi, (m, n) => (ENT[n.toLowerCase()] !== undefined ? ENT[n.toLowerCase()] : m));
}
const strip = (html) => decode(html).replace(/<[^>]+>/g, ' ').replace(/\s+/g, ' ').trim();
function tag(item, name) {
  const m = new RegExp(`<${name}(?:\\s[^>]*)?>([\\s\\S]*?)</${name}>`, 'i').exec(item);
  return m ? m[1] : '';
}
function image(item) {
  const m = /<media:(?:content|thumbnail)[^>]*url="([^"]+)"/i.exec(item)
    || /<enclosure[^>]*url="([^"]+)"[^>]*type="image/i.exec(item)
    || /<enclosure[^>]*type="image[^"]*"[^>]*url="([^"]+)"/i.exec(item)
    || /<img[^>]*src=["']([^"']+)["']/i.exec(decode(tag(item, 'content:encoded') || tag(item, 'description')));
  return m ? decode(m[1]) : null;
}

function parse(xml) {
  const items = [];
  const re = /<item[\s>][\s\S]*?<\/item>/gi;
  let m;
  while ((m = re.exec(xml)) && items.length < 40) {
    const it = m[0];
    const title = strip(tag(it, 'title'));
    const link = strip(tag(it, 'link'));
    if (!title || !link) continue;
    const date = new Date(strip(tag(it, 'pubDate')) || Date.now());
    items.push({
      title: title.slice(0, 200),
      body: strip(tag(it, 'description')).slice(0, 600) || title,
      link: link.slice(0, 500),
      guid: (strip(tag(it, 'guid')) || link).slice(0, 500),
      image: image(it),
      date: Number.isNaN(date.getTime()) ? new Date() : date,
    });
  }
  return items;
}

async function fetchFeed(feed) {
  const res = await fetch(feed.url, { headers: { 'user-agent': 'Mozilla/5.0 (OmegaNews)' }, signal: AbortSignal.timeout(15000) });
  if (!res.ok) throw new Error(`HTTP ${res.status}`);
  return parse(await res.text());
}

async function refresh() {
  let added = 0;
  for (const feed of FEEDS) {
    try {
      const items = await fetchFeed(feed);
      for (const it of items) {
        const r = await db.query(
          `INSERT INTO lab_news (title, body, tag, color, created_at, source, link, image_url, ext_id)
           VALUES ($1,$2,$3,$4,$5,$3,$6,$7,$8)
           ON CONFLICT (ext_id) WHERE ext_id IS NOT NULL DO NOTHING`,
          [it.title, it.body, feed.name, feed.color, it.date, it.link, it.image, it.guid]);
        added += r.rowCount;
      }
    } catch (err) {
      console.error(JSON.stringify({ ts: new Date().toISOString(), event: 'feed_error', feed: feed.name, error: err.message }));
    }
  }
  await db.query(
    `DELETE FROM lab_news WHERE ext_id IS NOT NULL AND news_id NOT IN (
       SELECT news_id FROM lab_news WHERE ext_id IS NOT NULL ORDER BY created_at DESC LIMIT 300)`);
  console.log(JSON.stringify({ ts: new Date().toISOString(), event: 'feeds_refreshed', added }));
  return added;
}

function start() {
  const run = () => refresh().catch((e) => console.error(JSON.stringify({ ts: new Date().toISOString(), event: 'feeds_failed', error: e.message })));
  setTimeout(run, 5000);
  setInterval(run, EVERY_MS).unref();
}

// Le immagini passano dal server (la console non contatta siti terzi), con un
// limite di dimensione e una piccola cache in memoria.
const cache = new Map();   // news_id → { type, buf }
const MAX_IMG = 3 * 1024 * 1024;
async function imageFor(newsId) {
  if (cache.has(newsId)) return cache.get(newsId);
  const r = await db.query('SELECT image_url FROM lab_news WHERE news_id=$1', [newsId]);
  const url = r.rows[0] && r.rows[0].image_url;
  if (!url || !/^https?:\/\//.test(url)) return null;
  const res = await fetch(url, { headers: { 'user-agent': 'Mozilla/5.0 (OmegaNews)' }, signal: AbortSignal.timeout(15000) });
  if (!res.ok) return null;
  const type = res.headers.get('content-type') || 'image/jpeg';
  if (!/^image\/(jpeg|png|webp)/.test(type)) return null;
  const buf = Buffer.from(await res.arrayBuffer());
  if (buf.length > MAX_IMG) return null;
  const out = { type, buf };
  cache.set(newsId, out);
  if (cache.size > 80) cache.delete(cache.keys().next().value);
  return out;
}

module.exports = { start, imageFor };
