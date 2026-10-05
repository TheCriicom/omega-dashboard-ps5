// Localizzazione: risoluzione della lingua, documenti legali nelle 27 lingue,
// crediti, Store tradotto, notifiche nella lingua del destinatario, dettagli
// degli errori in inglese e instradamento del sito per lingua.
import { spawn } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { createRequire } from 'node:module';
import { BASE as B, API_DIR as API, DATABASE_URL as DBURL, IP, checker, j, sleep, suffix } from './helpers.mjs';

const require = createRequire(import.meta.url);
const { Pool } = require('pg');
const lang = require(path.join(API, 'src', 'lang.js'));
const pool = new Pool({ connectionString: DBURL });
const { ok, done } = checker();

const CODES = ['it', 'en', 'ja', 'fr', 'es', 'de', 'nl', 'pt-PT', 'pt-BR', 'ru', 'ko', 'zh-Hans', 'zh-Hant', 'fi', 'sv',
  'da', 'nb', 'pl', 'tr', 'cs', 'hu', 'el', 'ro', 'th', 'vi', 'id', 'uk'];
const DEV = { name: 'TheCriicom', url: 'https://outlinedigital.it' };

// ============================================================ 1. lingua ===
ok(JSON.stringify(lang.CODES) === JSON.stringify(CODES), '27 codici nell\'ordine dell\'app');
const R = (header, query) => lang.resolve({ header, query }).lang;
const cases = [
  [null, null, 'en'], ['', null, 'en'], ['*', null, 'en'], ['xx-YY', null, 'en'],
  ['it', null, 'it'], ['it-IT,it;q=0.9,en;q=0.8', null, 'it'], ['de-AT', null, 'de'],
  ['fr;q=0.4, de;q=0.9, en;q=0.7', null, 'de'], ['en;q=0, it;q=0.1', null, 'it'], ['xx, sv;q=0.5', null, 'sv'],
  ['pt-BR', null, 'pt-BR'], ['pt', null, 'pt-PT'], ['pt-PT', null, 'pt-PT'], ['pt-AO', null, 'pt-PT'], ['pt-br', null, 'pt-BR'],
  ['zh-TW', null, 'zh-Hant'], ['zh-HK', null, 'zh-Hant'], ['zh-Hant', null, 'zh-Hant'], ['zh-Hant-TW', null, 'zh-Hant'],
  ['zh', null, 'zh-Hans'], ['zh-CN', null, 'zh-Hans'], ['zh-Hans', null, 'zh-Hans'], ['zh-SG', null, 'zh-Hans'],
  ['no', null, 'nb'], ['nn-NO', null, 'nb'], ['nb', null, 'nb'], ['ja-JP', null, 'ja'], ['UK', null, 'uk'],
  ['de', 'fr', 'fr'], ['de', 'zh-tw', 'zh-Hant'], ['de', 'bogus', 'de'], [null, 'it', 'it'],
];
for (const [h, q, want] of cases) {
  const got = R(h, q);
  ok(got === want, `lingua: Accept-Language=${JSON.stringify(h)} ?lang=${JSON.stringify(q)} → ${want}`, got);
}
ok(lang.resolve({}).explicit === false && lang.resolve({ header: 'fr' }).explicit === true, 'ripiego "en" non esplicito');
ok(JSON.stringify(lang.parseAcceptLanguage('en;q=0.2,fr,de;q=0.5,fr-CA')) === '["fr","de","en"]', 'q-value e duplicati');

// ======================================================= 2. documenti ===
const legalLib = require(path.join(API, 'src', 'legal.js'));
const itDoc = await j('GET', '/api/v1/legal?lang=it');
ok(itDoc.s === 200 && itDoc.b.lang === 'it' && itDoc.b.notice === null, 'legal it: nessun avviso di traduzione', itDoc.b.lang);
ok(JSON.stringify(itDoc.b.available) === JSON.stringify(CODES), 'legal: available = 27 lingue', itDoc.b.available);
ok(itDoc.b.version === legalLib.TERMS_VERSION && legalLib.TERMS_VERSION === '2026-10-05', 'TERMS_VERSION attesa (05/10: diagnostica nell\'informativa)');
ok(JSON.stringify(itDoc.b.developer) === JSON.stringify(DEV), 'legal: developer', itDoc.b.developer);
const ids = (doc) => doc.sections.map((s) => s.id).join();
for (const code of CODES) {
  const r = await j('GET', `/api/v1/legal?lang=${encodeURIComponent(code)}`);
  const d = r.b.documents || {};
  ok(r.s === 200 && r.b.lang === code && r.b.version === legalLib.TERMS_VERSION, `legal ${code}: lingua e versione`, { lang: r.b.lang, v: r.b.version });
  const docs = ['privacy', 'terms', 'licenses'].map((k) => d[k]);
  ok(docs.every((x) => x && x.lang === code && x.title), `legal ${code}: tre documenti`, Object.keys(d));
  const left = JSON.stringify(d).match(/\{[a-z_]+\}/g);
  ok(!left, `legal ${code}: segnaposto tutti sostituiti`, left);
  for (const k of ['privacy', 'terms', 'licenses']) {
    const mine = code === 'it' ? ids(d[k]) : ids(d[k]).replace(/^translation_notice,/, '');
    ok(mine === ids(itDoc.b.documents[k]), `legal ${code}/${k}: stesse sezioni dell'italiano`, mine);
    if (code !== 'it') ok(d[k].sections[0].id === 'translation_notice' && d[k].sections[0].body.length > 20, `legal ${code}/${k}: si apre con l'avviso`, d[k].sections[0]);
  }
  const dev = d.licenses.sections.find((s) => s.id === 'developer');
  ok(dev && dev.body.includes('TheCriicom') && dev.body.includes('https://outlinedigital.it'), `legal ${code}: sezione sviluppatore`, dev);
  ok(d.terms.sections.some((s) => s.body.includes(legalLib.TERMS_VERSION)), `legal ${code}: versione nei termini`);
  if (code !== 'it') ok(r.b.notice && r.b.notice === d.privacy.sections[0].body, `legal ${code}: notice`, r.b.notice);
}
let r = await j('GET', '/api/v1/legal', null, { 'accept-language': 'pt-BR,pt;q=0.8' });
ok(r.b.lang === 'pt-BR', 'legal: lingua da Accept-Language', r.b.lang);
r = await j('GET', '/api/v1/legal?lang=zh-TW', null, { 'accept-language': 'de' });
ok(r.b.lang === 'zh-Hant', 'legal: ?lang= prevale su Accept-Language', r.b.lang);
r = await j('GET', '/api/v1/legal');
ok(r.b.lang === 'en', 'legal: senza lingua → en', r.b.lang);

// pagine HTML
const html = async (p, headers = {}) => {
  const x = await fetch(B + p, { headers: { ...IP, ...headers } });
  return { s: x.status, t: await x.text(), h: x.headers };
};
for (const [p, h, want] of [
  ['/legal/privacy?lang=ja', {}, 'ja'], ['/legal/terms', { 'accept-language': 'de-CH,de;q=0.9' }, 'de'],
  ['/legal/licenses?lang=zh-HK', {}, 'zh-Hant'], ['/legal', { 'accept-language': 'it' }, 'it'], ['/legal/privacy', {}, 'en'],
  ['/source?lang=fr', {}, 'fr'], ['/source', { 'accept-language': 'nn' }, 'nb'],
]) {
  const x = await html(p, h);
  ok(x.s === 200 && x.t.includes(`<html lang="${want}">`), `${p} ${JSON.stringify(h)} → <html lang="${want}">`, x.t.slice(0, 120));
  ok(x.h.get('vary') === 'Accept-Language' && x.h.get('content-language') === want, `${p}: Vary e Content-Language`, x.h.get('vary'));
  ok(/<nav class="langs"/.test(x.t) && x.t.includes(`?lang=pt-BR"`) && x.t.includes('hreflang="uk"'), `${p}: selettore della lingua`);
  ok(x.t.includes('href="https://outlinedigital.it"') && x.t.includes('>TheCriicom</a>'), `${p}: credito nel footer`);
  ok(x.t.includes('rel="icon" type="image/svg+xml" href="data:image/svg+xml;base64,'), `${p}: favicon`);
  ok(/img-src data:/.test(x.h.get('content-security-policy') || ''), `${p}: CSP consente la favicon data:`);
}
r = await html('/legal/terms?lang=it');
ok(!r.t.includes('class="notice"') && (await html('/legal/terms?lang=sv')).t.includes('class="notice"'), 'avviso di traduzione solo fuori dall\'italiano');
ok(r.t.includes('href="/legal/privacy?lang=it"'), 'i link interni conservano la lingua');

// ======================================================== 3. crediti ===
for (const [p, h, want] of [['/api/v1', { 'accept-language': 'ja' }, 'ja'], ['/api/v1?lang=it', {}, 'it']]) {
  r = await j('GET', p, null, h);
  ok(r.s === 200 && JSON.stringify(r.b.developer) === JSON.stringify(DEV) && r.b.lang === want, `${p}: developer e lingua`, r.b);
}
const pkg = JSON.parse(fs.readFileSync(path.join(API, 'package.json'), 'utf8'));
ok(pkg.author === 'TheCriicom (https://outlinedigital.it)', 'package.json author', pkg.author);
const adm = await html('/admin');
ok(adm.t.includes('TheCriicom') && adm.t.includes('https://outlinedigital.it'), 'pannello admin: credito');
ok(!adm.t.includes('>Ω<') && adm.t.includes('class="brand-mark" src="data:image/svg+xml;base64,'), 'pannello admin: logo SVG');
ok(adm.t.includes('data-lang="en"') && adm.t.includes('omega-admin-lang') && !/innerHTML/.test(adm.t), 'pannello admin: selettore IT/EN senza innerHTML');

// ===================================================== 4. catalogo ===
const cat = JSON.parse(fs.readFileSync(path.join(API, 'config', 'store-catalog.json'), 'utf8'));
const others = CODES.filter((c) => c !== 'it');
const incomplete = cat.apps.filter((a) => !a.i18n || others.some((c) => !a.i18n[c] || !a.i18n[c].tagline || !a.i18n[c].description));
ok(cat.apps.length >= 49 && incomplete.length === 0, `catalogo: ${cat.apps.length} voci tradotte nelle 26 lingue`, incomplete.map((a) => a.catalog_key));
const extra = cat.apps.filter((a) => a.i18n && Object.keys(a.i18n).some((c) => !others.includes(c)));
ok(extra.length === 0, 'catalogo: solo codici noti in i18n');
const diab = cat.apps.find((a) => a.catalog_key === 'ghcat:devilutionx');
ok(diab && others.every((c) => diab.i18n[c].description.includes('DIABDAT.MPQ')), 'catalogo: i nomi di file restano (DIABDAT.MPQ)');

// ================================== 5. Store, notifiche ed errori (api) ===
const sfx = suffix();
const users = { de: `lde${sfx}`, none: `lno${sfx}`, actor: `lac${sfx}` };
const tok = {};
for (const [k, n] of Object.entries(users)) {
  const h = k === 'de' ? { 'accept-language': 'de-DE,de;q=0.9,en;q=0.5' } : {};
  r = await j('POST', '/api/v1/auth/register', { online_id: n, password: 'Password123!', accept_terms: true }, h);
  ok(r.s === 201, `register ${n}`, r);
  r = await j('POST', '/api/v1/auth/login', { online_id: n, password: 'Password123!' }, h);
  tok[k] = r.b.token;
  ok(!!tok[k], `login ${n}`, r);
}
const A = (k, h = {}) => ({ authorization: `Bearer ${tok[k]}`, ...h });
const langOf = async (n) => (await pool.query('SELECT lang FROM lab_account WHERE online_id=$1', [n])).rows[0].lang;
ok(await langOf(users.de) === 'de', 'lab_account.lang dalla Accept-Language del login');
ok(await langOf(users.none) === null, 'senza Accept-Language la lingua resta NULL (italiano)');
r = await j('GET', '/api/v1/sync', null, A('none', { 'accept-language': 'ja' }));
ok(r.s === 200 && await langOf(users.none) === 'ja', '/sync aggiorna la lingua se cambia');
r = await j('GET', '/api/v1/sync', null, A('none'));
ok(await langOf(users.none) === 'ja', '/sync senza lingua non la cancella');
await pool.query('UPDATE lab_account SET lang=NULL WHERE online_id=$1', [users.none]);

// notizie nella lingua della richiesta, poi l'inglese; le altre lingue mai
{
  const ins = (l, t, min) => pool.query(
    `INSERT INTO lab_news (title, body, tag, created_at, source, ext_id, lang) VALUES ($1,$1,'t', now() + make_interval(mins => $2), 't', $3, $4)`,
    [t, min, `test:${sfx}:${t}`, l]);
  await ins('es', `ES-${sfx}`, 1); await ins('en', `EN-${sfx}`, 2); await ins('it', `IT-${sfx}`, 3); await ins('pt-BR', `BR-${sfx}`, 4);
  // b dopo a, oppure assente (con abbastanza notizie nella lingua l'inglese non serve)
  const after = (t, a, b) => !t.includes(b) || t.indexOf(a) < t.indexOf(b);
  const titles = async (h) => (await j('GET', '/api/v1/news', null, A('de', { 'accept-language': h }))).b.news.map((x) => x.title);
  let t = await titles('es');
  ok(t.indexOf(`ES-${sfx}`) >= 0 && after(t, `ES-${sfx}`, `EN-${sfx}`) && !t.includes(`IT-${sfx}`) && !t.includes(`BR-${sfx}`),
    'news: prima lo spagnolo, poi l\'inglese, niente italiano', t.slice(0, 5));
  t = await titles('pt-PT');
  ok(t.includes(`BR-${sfx}`) && !t.includes(`ES-${sfx}`), 'news: pt-PT riceve anche pt-BR', t.slice(0, 5));
  t = await titles('tr');
  ok(t.includes(`EN-${sfx}`) && !t.includes(`IT-${sfx}`), 'news: lingua senza feed → inglese', t.slice(0, 5));
  t = await titles('it');
  ok(t.indexOf(`IT-${sfx}`) >= 0 && after(t, `IT-${sfx}`, `EN-${sfx}`), 'news: italiano per gli italiani', t.slice(0, 5));
  await pool.query('DELETE FROM lab_news WHERE ext_id LIKE $1', [`test:${sfx}:%`]);
}

// notifiche nella lingua del destinatario
const catDe = JSON.parse(fs.readFileSync(path.join(API, 'src', 'i18n', 'de.json'), 'utf8'));
const catIt = JSON.parse(fs.readFileSync(path.join(API, 'src', 'i18n', 'it.json'), 'utf8'));
const fill = (t, v) => t.replace(/\{(\w+)\}/g, (m, k) => v[k]);
for (const k of ['de', 'none']) {
  r = await j('POST', '/api/v1/friends/request', { online_id: users[k] }, A('actor', { 'accept-language': 'fr' }));
  ok(r.s === 201, `richiesta di amicizia a ${users[k]}`, r);
}
const lastNotif = async (n) => (await pool.query(
  `SELECT n.type, n.title, n.ref FROM lab_notification n JOIN lab_account a ON a.account_id=n.account_id
    WHERE a.online_id=$1 ORDER BY n.notification_id DESC LIMIT 1`, [n])).rows[0];
let nt = await lastNotif(users.de);
ok(nt && nt.title === fill(catDe.notify.friend_request, { actor: users.actor }) && nt.type === 'friend_request' && nt.ref === users.actor,
  'notifica in tedesco per chi usa il tedesco (tipo e ref invariati)', nt);
nt = await lastNotif(users.none);
ok(nt && nt.title === fill(catIt.notify.friend_request, { actor: users.actor }), 'notifica in italiano senza lingua salvata', nt);
r = await j('POST', '/api/v1/friends/accept', { online_id: users.actor }, A('de'));
nt = await lastNotif(users.actor);
ok(nt && nt.type === 'friend_accept' && nt.title === fill(catIt.notify.friend_accept, { actor: users.de }),
  'la lingua è del destinatario, non di chi agisce', nt);

// Store: scheda del catalogo con traduzioni, scheda utente senza
const appBody = { title: `I18n ${sfx}`, tagline: 'Sottotitolo originale', description: 'Descrizione originale', category: 'app',
  version: '1.0', download_url: 'https://example.com/x/a.zip', file_kind: 'zip', rights_confirmed: true };
r = await j('POST', '/api/v1/store/apps', appBody, A('actor'));
const appId = r.b.app_id;
ok(r.s === 201 && appId, 'publish', r);
r = await j('POST', '/api/v1/store/apps', { ...appBody, title: `I18n user ${sfx}` }, A('actor'));
const userAppId = r.b.app_id;
await pool.query(`UPDATE lab_store_app SET i18n=$2::jsonb WHERE app_id=$1`, [appId, JSON.stringify({
  de: { tagline: `Deutscher Untertitel ${sfx}`, description: 'Deutsche Beschreibung' },
  en: { tagline: 'English tagline', description: 'English description' },
  fr: { tagline: 'Sous-titre' },
})]);
const getApp = async (id, h) => (await j('GET', `/api/v1/store/apps/${id}`, null, A('none', h))).b;
let a = await getApp(appId, { 'accept-language': 'de' });
ok(a.tagline === `Deutscher Untertitel ${sfx}` && a.description === 'Deutsche Beschreibung', 'getApp in tedesco', a);
a = await getApp(appId, { 'accept-language': 'it' });
ok(a.tagline === 'Sottotitolo originale' && a.description === 'Descrizione originale', 'getApp in italiano: originale', a);
a = await getApp(appId, { 'accept-language': 'sv' });
ok(a.tagline === 'Sottotitolo originale', 'lingua non tradotta → originale italiano', a.tagline);
a = await getApp(appId, {});
ok(a.tagline === 'English tagline', 'senza lingua → inglese', a.tagline);
a = await getApp(appId, { 'accept-language': 'fr' });
ok(a.tagline === 'Sous-titre' && a.description === 'Descrizione originale', 'campo mancante nella traduzione → originale', a);
ok(!('i18n' in a) && !('tagline_orig' in a), 'i18n non esposto al client');
a = await getApp(userAppId, { 'accept-language': 'de' });
ok(a.tagline === 'Sottotitolo originale', 'homebrew dell\'utente: come scritto', a.tagline);
r = await j('GET', `/api/v1/store/apps?q=${encodeURIComponent(`Untertitel ${sfx}`)}`, null, A('none', { 'accept-language': 'de' }));
ok(r.s === 200 && r.b.apps.length === 1 && r.b.apps[0].app_id === String(appId) && r.b.apps[0].tagline.startsWith('Deutscher'),
  'listApps: ricerca e sottotitolo tradotti', r.b);
r = await j('GET', `/api/v1/store/apps/${appId}/download`, null, A('none', { 'accept-language': 'de' }));
ok(r.b.summary === `Deutscher Untertitel ${sfx}` || r.s >= 400, 'download: summary tradotto', r.b);
r = await j('POST', `/api/v1/store/apps/${appId}`, { ...appBody, tagline: 'Nuovo sottotitolo' }, A('actor'));
a = await getApp(appId, { 'accept-language': 'de' });
ok(r.s === 200 && a.tagline === 'Nuovo sottotitolo', 'modificando la scheda le traduzioni decadono', a.tagline);
const curated = (await pool.query(`SELECT count(*)::int AS n FROM lab_store_app WHERE catalog_key IS NOT NULL AND i18n IS NULL`)).rows[0].n;
const seeded = (await pool.query(`SELECT count(*)::int AS n FROM lab_store_app WHERE catalog_key IS NOT NULL`)).rows[0].n;
ok(seeded === 0 || curated === 0, `catalogo caricato con le traduzioni (${seeded} voci; npm run seed:store)`, { seeded, curated });
for (const id of [appId, userAppId]) await j('DELETE', `/api/v1/store/apps/${id}`, null, A('actor'));

// errori: dettaglio in inglese solo se la richiesta chiede una lingua diversa dall'italiano
r = await j('POST', '/api/v1/auth/login', { online_id: users.none, password: 'sbagliata1' }, { 'accept-language': 'fr' });
ok(r.s === 401 && r.b.error === 'invalid_credentials' && r.b.detail === 'invalid online_id or password', 'detail in inglese (fr)', r.b);
r = await j('POST', '/api/v1/auth/login', { online_id: users.none, password: 'sbagliata1' });
ok(r.b.detail === 'online_id o password non validi', 'detail in italiano senza lingua', r.b);
r = await j('POST', '/api/v1/auth/login', { online_id: users.none, password: 'sbagliata1' }, { 'accept-language': 'it' });
ok(r.b.detail === 'online_id o password non validi', 'detail in italiano (it)', r.b);
r = await j('GET', '/api/v1/me', null, { 'accept-language': 'ja' });
ok(r.s === 401 && /^a session token is required/.test(r.b.detail), '401 in inglese', r.b);

// ===================================== 6. sito per lingua (api temporanea) ===
const SITE = fs.mkdtempSync(path.join(os.tmpdir(), 'omega-site-'));
const PORT = 18000 + Math.floor(Math.random() * 900) + 100;
const S = `http://127.0.0.1:${PORT}`;
const api = spawn(process.execPath, ['src/server.js'], {
  cwd: API, stdio: 'ignore',
  env: { ...process.env, API_PORT: String(PORT), SITE_DIR: SITE, DATABASE_URL: DBURL,
    OMEGA_SESSION_SECRET: process.env.OMEGA_SESSION_SECRET || 'k'.repeat(40), UPDATE_DIR: path.join(API, 'updates') },
});
for (let i = 0; i < 50; i++) { try { if ((await fetch(`${S}/healthz`)).ok) break; } catch { /* avvio */ } await sleep(200); }
const get = async (p, al, cookie) => {
  const headers = {};
  if (al) headers['accept-language'] = al;
  if (cookie) headers.cookie = cookie;
  const x = await fetch(S + p, { redirect: 'manual', headers });
  return { s: x.status, loc: x.headers.get('location'), vary: x.headers.get('vary'), ct: x.headers.get('content-type') || '', t: await x.text() };
};
const put = (rel, text) => { fs.mkdirSync(path.dirname(path.join(SITE, rel)), { recursive: true }); fs.writeFileSync(path.join(SITE, rel), text); };
try {
  // nessun sito: GET / descrive l'api
  let x = await get('/', 'fr');
  ok(x.s === 200 && /json/.test(x.ct) && JSON.parse(x.t).developer.name === 'TheCriicom', 'sito assente: GET / → JSON dell\'api', x);
  x = await get('/installa');
  ok(x.s === 404, 'sito assente: /installa → 404', x.s);
  x = await get('/fr/');
  ok(x.s === 404, 'sito assente: /fr/ → 404', x.s);

  // solo le pagine inglesi in site/
  put('index.html', 'ROOT-INDEX'); put('installa.html', 'ROOT-INSTALL');
  x = await get('/', 'fr');
  ok(x.s === 200 && x.t === 'ROOT-INDEX' && x.vary === 'Accept-Language, Cookie', 'senza cartelle: / serve site/index.html con Vary', x);
  x = await get('/installa', 'fr');
  ok(x.s === 200 && x.t === 'ROOT-INSTALL', 'senza cartelle: /installa serve site/installa.html', x);
  x = await get('/fr/', 'fr');
  ok(x.s === 200 && x.t === 'ROOT-INDEX', 'senza cartelle: /fr/ ricade su site/index.html', x);
  x = await get('/fr/installa');
  ok(x.s === 200 && x.t === 'ROOT-INSTALL', 'senza cartelle: /fr/installa ricade su site/installa.html', x);

  // cartelle generate
  for (const c of ['fr', 'en', 'pt-BR', 'zh-Hant', 'it']) { put(`${c}/index.html`, `INDEX-${c}`); put(`${c}/installa.html`, `INSTALL-${c}`); }
  for (const [al, want] of [['fr-FR,fr;q=0.9', 'fr'], ['pt-BR', 'pt-BR'], ['zh-TW', 'zh-Hant'], ['it;q=0.5, xx', 'it'], [null, null], ['ko', null]]) {
    x = await get('/', al);
    if (want) ok(x.s === 302 && x.loc === `/${want}/` && x.vary === 'Accept-Language, Cookie', `GET / (${al}) → 302 /${want}/`, x);
    else ok(x.s === 200 && x.t === 'ROOT-INDEX' && x.vary === 'Accept-Language, Cookie', `GET / (${al}) senza ${al}/ → site/index.html`, x);
  }
  x = await get('/?lang=pt-BR', 'fr');
  ok(x.s === 302 && x.loc === '/pt-BR/', 'GET /?lang= prevale', x);
  x = await get('/?lang=fr&ref=forum', 'it');
  ok(x.s === 302 && x.loc === '/fr/?ref=forum', 'GET /?lang=: il resto della query resta', x);
  x = await get('/', 'fr', 'a=1; omega_lang=it');
  ok(x.s === 302 && x.loc === '/it/', 'cookie omega_lang prevale su Accept-Language', x);
  x = await get('/', 'fr', 'omega_lang=xx');
  ok(x.s === 302 && x.loc === '/fr/', 'cookie con codice sconosciuto ignorato', x);
  x = await get('/installa', null, 'omega_lang=zh-Hant');
  ok(x.s === 302 && x.loc === '/zh-Hant/installa', 'cookie anche su /installa', x);
  for (const [al, want] of [['fr', 'fr'], ['zh-HK', 'zh-Hant']]) {
    x = await get('/installa', al);
    ok(x.s === 302 && x.loc === `/${want}/installa` && x.vary === 'Accept-Language, Cookie', `GET /installa (${al}) → 302 /${want}/installa`, x);
  }
  x = await get('/installa', 'ko');
  ok(x.s === 200 && x.t === 'ROOT-INSTALL', 'GET /installa senza ko/installa.html → site/installa.html', x);
  for (const c of ['fr', 'pt-BR', 'zh-Hant']) {
    x = await get(`/${c}/`);
    ok(x.s === 200 && x.t === `INDEX-${c}` && /text\/html/.test(x.ct), `GET /${c}/`, x);
    x = await get(`/${c}/installa`);
    ok(x.s === 200 && x.t === `INSTALL-${c}`, `GET /${c}/installa`, x);
    x = await get(`/${c}`);
    ok(x.s === 302 && x.loc === `/${c}/`, `GET /${c} → 302 /${c}/`, x);
  }
  x = await get('/fr?x=1');
  ok(x.s === 302 && x.loc === '/fr/?x=1', 'il redirect conserva la query', x);
  x = await get('/ko/');
  ok(x.s === 200 && x.t === 'ROOT-INDEX', 'GET /ko/ senza cartella → site/index.html', x);
  for (const p of ['/xx/', '/FR/', '/pt/', '/zh/', '/xx/installa', '/fr/altro', '/de/../index.html']) {
    x = await get(p);
    ok(x.s === 404, `GET ${p} → 404`, x.s);
  }
  x = await get('/legal/privacy?lang=fr');
  ok(x.s === 200 && x.t.includes('<html lang="fr">'), 'le rotte esistenti non sono catturate da /:lang', x.s);
  x = await get('/healthz');
  ok(x.s === 200, '/healthz invariata');
  const post = await fetch(`${S}/fr/`, { method: 'POST' });
  ok(post.status === 405, 'POST /fr/ → 405', post.status);
  const unk = await fetch(`${S}/nonesiste`, { method: 'POST' });
  ok(unk.status === 404, 'percorsi di un segmento sconosciuti restano 404', unk.status);
} finally {
  api.kill();
  fs.rmSync(SITE, { recursive: true, force: true });
}

await pool.end();
done();
