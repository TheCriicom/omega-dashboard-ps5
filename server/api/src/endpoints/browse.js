'use strict';
// Browser dell'app: il server scarica la pagina e la riduce a blocchi
// semplici (titoli, paragrafi, link, immagini, elenchi) che la console disegna
// in modo nativo. Gli articoli passano da Readability (modalità lettura di
// Firefox), le altre pagine da una visita del DOM; la ricerca usa DuckDuckGo.
// Le immagini passano dal server con un id breve.
const crypto = require('node:crypto');
const { parseHTML } = require('linkedom');
const { Readability, isProbablyReaderable } = require('@mozilla/readability');
const { HttpError } = require('../http');
const limiter = require('../ratelimit');
const { safeFetch, readLimited } = require('../safefetch');

const MAX_HTML = 3 * 1024 * 1024;
const MAX_IMG = 4 * 1024 * 1024;
const MAX_BLOCKS = 700;
const MAX_TEXT = 160 * 1024;

function decodeHtml(buf, ctype) {
  let cs = (/charset=([^;]+)/i.exec(ctype || '') || [])[1];
  if (!cs) {
    const head = buf.subarray(0, 2048).toString('latin1');
    cs = (/<meta[^>]+charset=["']?([\w-]+)/i.exec(head) || [])[1];
  }
  try { return new TextDecoder((cs || 'utf-8').trim().toLowerCase()).decode(buf); } catch { return buf.toString('utf8'); }
}

// Id breve → URL assoluto delle immagini incontrate (le ultime 4000).
const images = new Map();
function imageId(url) {
  const id = crypto.createHash('sha1').update(url).digest('base64url').slice(0, 16);
  if (!images.has(id)) {
    images.set(id, url);
    if (images.size > 4000) images.delete(images.keys().next().value);
  }
  return id;
}

const SKIP = new Set(['SCRIPT', 'STYLE', 'NOSCRIPT', 'SVG', 'IFRAME', 'FORM', 'BUTTON', 'INPUT', 'SELECT', 'TEXTAREA', 'TEMPLATE', 'CANVAS', 'VIDEO', 'AUDIO', 'OBJECT', 'EMBED', 'DIALOG']);
const clean = (s) => String(s || '').replace(/\s+/g, ' ').trim();

function absUrl(href, base) {
  if (!href || /^(javascript|mailto|tel|data):/i.test(href) || href.startsWith('#')) return null;
  try { const u = new URL(href, base); return ['http:', 'https:'].includes(u.protocol) ? u.toString() : null; } catch { return null; }
}

function imgSrc(el, base) {
  const src = el.getAttribute('data-src') || el.getAttribute('data-lazy-src') || el.getAttribute('src')
    || (el.getAttribute('srcset') || '').split(',').pop().trim().split(' ')[0];
  const u = absUrl(src, base);
  if (!u || /\.svg(\?|$)/i.test(u)) return null;
  const w = Number(el.getAttribute('width')) || 0, h = Number(el.getAttribute('height')) || 0;
  if ((w && w < 80) || (h && h < 60)) return null; // icone e pixel di tracciamento
  return u;
}

function extract(root, base) {
  const blocks = [];
  let text = 0;
  const seenLinks = new Set();
  const push = (b) => {
    if (blocks.length >= MAX_BLOCKS || text > MAX_TEXT) return;
    const last = blocks[blocks.length - 1];
    if (b.x && last && last.x === b.x && last.t === b.t) return;
    text += (b.x || '').length + (b.s || '').length;
    blocks.push(b);
  };
  const linksOf = (el) => {
    const out = [];
    for (const a of el.querySelectorAll('a[href]')) {
      const u = absUrl(a.getAttribute('href'), base); const x = clean(a.textContent);
      if (u && x.length >= 3 && !seenLinks.has(u)) { seenLinks.add(u); out.push({ t: 'a', x: x.slice(0, 160), u }); }
      if (out.length >= 6) break;
    }
    return out;
  };

  const walk = (el) => {
    if (!el || blocks.length >= MAX_BLOCKS) return;
    for (const node of el.childNodes) {
      if (node.nodeType === 3) {
        const x = clean(node.textContent);
        if (x.length > 60) push({ t: 'p', x });
        continue;
      }
      if (node.nodeType !== 1) continue;
      const tag = node.tagName;
      if (SKIP.has(tag)) continue;
      if (node.getAttribute('aria-hidden') === 'true' || /display:\s*none/i.test(node.getAttribute('style') || '')) continue;
      if (/^H[1-6]$/.test(tag)) {
        const x = clean(node.textContent); if (!x) continue;
        const a = node.querySelector('a[href]'); const u = a && absUrl(a.getAttribute('href'), base);
        push(u ? { t: 'a', x, u, big: 1 } : { t: 'h', l: Math.min(3, Number(tag[1])), x });
      } else if (tag === 'P') {
        const x = clean(node.textContent); if (x) push({ t: 'p', x });
        for (const img of node.querySelectorAll('img')) { const s = imgSrc(img, base); if (s) push({ t: 'img', i: imageId(s), alt: clean(img.getAttribute('alt')).slice(0, 120) }); }
        for (const l of linksOf(node)) push(l);
      } else if (tag === 'LI') {
        const links = node.querySelectorAll('a[href]');
        const x = clean(node.textContent);
        if (links.length === 1 && x.length < 140) {
          const u = absUrl(links[0].getAttribute('href'), base);
          if (u && !seenLinks.has(u)) { seenLinks.add(u); push({ t: 'a', x: x || clean(links[0].textContent), u }); }
        } else if (node.querySelector('p, ul, ol, div, h2, h3')) walk(node);
        else if (x) { push({ t: 'li', x }); for (const l of linksOf(node)) push(l); }
      } else if (tag === 'A') {
        const u = absUrl(node.getAttribute('href'), base); const x = clean(node.textContent);
        const img = node.querySelector('img'); const s = img && imgSrc(img, base);
        if (s) push({ t: 'img', i: imageId(s), alt: clean(img.getAttribute('alt')).slice(0, 120) });
        if (u && x.length >= 2 && !seenLinks.has(u)) { seenLinks.add(u); push({ t: 'a', x: x.slice(0, 200), u }); }
      } else if (tag === 'IMG') {
        const s = imgSrc(node, base); if (s) push({ t: 'img', i: imageId(s), alt: clean(node.getAttribute('alt')).slice(0, 120) });
      } else if (tag === 'BLOCKQUOTE') {
        const x = clean(node.textContent); if (x) push({ t: 'q', x });
      } else if (tag === 'PRE') {
        const x = String(node.textContent || '').slice(0, 4000); if (x.trim()) push({ t: 'code', x });
      } else if (tag === 'TABLE') {
        for (const tr of node.querySelectorAll('tr')) {
          const x = [...tr.querySelectorAll('th,td')].map((c) => clean(c.textContent)).filter(Boolean).join('  ·  ');
          if (x) push({ t: 'li', x });
        }
      } else if (tag === 'BR' || tag === 'HR') {
        continue;
      } else {
        walk(node);
      }
    }
  };
  walk(root);
  return blocks;
}

async function search(q) {
  const { res } = await safeFetch(`https://html.duckduckgo.com/html/?q=${encodeURIComponent(q)}&kl=it-it`, 'text/html');
  const html = decodeHtml(await readLimited(res, MAX_HTML), res.headers.get('content-type'));
  const { document } = parseHTML(html);
  const blocks = [{ t: 'h', l: 1, x: `Risultati per "${q}"` }];
  for (const r of document.querySelectorAll('.result')) {
    const a = r.querySelector('a.result__a');
    if (!a) continue;
    let href = a.getAttribute('href') || '';
    const m = /[?&]uddg=([^&]+)/.exec(href);
    if (m) href = decodeURIComponent(m[1]);
    if (href.startsWith('//')) href = `https:${href}`;
    if (!/^https?:\/\//.test(href) || /duckduckgo\.com\/y\.js/.test(href)) continue; // annunci
    let disp = href; try { const u = new URL(href); disp = u.hostname.replace(/^www\./, '') + (u.pathname.length > 1 ? u.pathname : ''); } catch { /* */ }
    blocks.push({ t: 'res', x: clean(a.textContent).slice(0, 200), u: href, s: clean((r.querySelector('.result__snippet') || {}).textContent).slice(0, 400), d: disp.slice(0, 120) });
    if (blocks.length > 25) break;
  }
  if (blocks.length === 1) blocks.push({ t: 'p', x: 'Nessun risultato.' });
  return { url: `omega://cerca?q=${encodeURIComponent(q)}`, title: `${q} - Cerca`, site: 'Ricerca', mode: 'search', blocks };
}

async function page(rawUrl) {
  const { res, url } = await safeFetch(rawUrl, 'text/html,application/xhtml+xml;q=0.9,image/*;q=0.8,*/*;q=0.5');
  const ctype = res.headers.get('content-type') || '';
  if (/^image\//.test(ctype)) {
    res.body.cancel().catch(() => {});
    return { url, title: url.split('/').pop() || url, site: new URL(url).hostname, mode: 'image', blocks: [{ t: 'img', i: imageId(url), alt: '' }] };
  }
  if (!/html|xml|text\/plain/.test(ctype)) {
    res.body.cancel().catch(() => {});
    return { url, title: 'Contenuto non supportato', site: new URL(url).hostname, mode: 'page', status: res.status,
      blocks: [{ t: 'h', l: 2, x: 'Questo tipo di file non si può aprire' }, { t: 'p', x: `Tipo: ${ctype || 'sconosciuto'}` }] };
  }
  const html = decodeHtml(await readLimited(res, MAX_HTML), ctype);
  if (/text\/plain/.test(ctype)) {
    return { url, title: url, site: new URL(url).hostname, mode: 'page', blocks: html.split(/\n{2,}/).slice(0, 400).map((x) => ({ t: 'p', x: clean(x) })).filter((b) => b.x) };
  }
  const { document } = parseHTML(html);
  const site = new URL(url).hostname.replace(/^www\./, '');
  const title = clean((document.querySelector('meta[property="og:title"]') || {}).getAttribute?.('content'))
    || clean((document.querySelector('title') || {}).textContent) || site;

  // gli articoli in modalità lettura
  let mode = 'page', blocks = null;
  try {
    if (isProbablyReaderable(document, { minContentLength: 400 })) {
      const art = new Readability(parseHTML(html).document, { charThreshold: 400 }).parse();
      if (art && art.content && (art.textContent || '').length > 600) {
        const { document: d2 } = parseHTML(`<html><body>${art.content}</body></html>`);
        blocks = [];
        const hero = document.querySelector('meta[property="og:image"]');
        const heroUrl = hero && absUrl(hero.getAttribute('content'), url);
        blocks.push({ t: 'h', l: 1, x: clean(art.title) || title });
        if (art.byline || art.siteName) blocks.push({ t: 'meta', x: [clean(art.byline), clean(art.siteName)].filter(Boolean).join(' · ') });
        if (heroUrl) blocks.push({ t: 'img', i: imageId(heroUrl), alt: '' });
        blocks.push(...extract(d2.body, url));
        mode = 'reader';
      }
    }
  } catch { blocks = null; }
  if (!blocks) {
    // altrimenti in ordine di documento, partendo dal contenuto principale se c'è
    const main = document.querySelector('main, [role=main], article') || document.body;
    blocks = extract(main, url);
    if (main !== document.body && blocks.length < 15) blocks = extract(document.body, url);
  }
  if (!blocks.length) blocks = [{ t: 'p', x: 'Questa pagina non ha contenuti leggibili senza JavaScript.' }];
  return { url, title: title.slice(0, 200), site, mode, status: res.status, blocks };
}

// GET /api/v1/browse?url=...  oppure  ?q=...
async function browse({ auth, url }) {
  const wait = limiter.hit(`browse|${auth.accountId}`, 90, 60);
  if (wait) throw new HttpError(429, 'too_many_requests');
  const q = clean(url.searchParams.get('q'));
  const target = clean(url.searchParams.get('url'));
  try {
    if (q) return { status: 200, body: await search(q.slice(0, 200)) };
    if (!target) throw new HttpError(400, 'missing_url');
    return { status: 200, body: await page(target.slice(0, 2000)) };
  } catch (err) {
    if (err instanceof HttpError) throw err;
    const msg = err.name === 'TimeoutError' ? 'timeout' : 'fetch_failed';
    throw new HttpError(502, msg, err.message);
  }
}

// GET /api/v1/browse/img/:id
async function browseImage({ params, res }) {
  const src = images.get(params.id);
  if (!src) throw new HttpError(404, 'unknown_image');
  const { res: r } = await safeFetch(src, 'image/webp,image/jpeg,image/png,image/*;q=0.8');
  const type = r.headers.get('content-type') || '';
  if (!/^image\/(jpeg|png|webp|gif)/.test(type)) throw new HttpError(415, 'not_an_image');
  const buf = await readLimited(r, MAX_IMG + 1);
  if (buf.length > MAX_IMG) throw new HttpError(413, 'image_too_large');
  res.writeHead(200, { 'content-type': type, 'content-length': buf.length, 'cache-control': 'max-age=3600' });
  res.end(buf);
  return { sent: true };
}

module.exports = { browse, browseImage };
