// Governance: termini, Store e segnalazioni, pannello admin, ban, diritti
// sull'account, documenti legali e sorgente.
import { execFileSync } from 'node:child_process';
import { BASE as B, API_DIR as API, IP, checker, j, suffix } from './helpers.mjs';

const { ok, done } = checker();

const sfx = suffix();
const names = ['adm', 'pub', 'rep1', 'rep2', 'rep3'].map((n) => `${n}${sfx}`);
const tok = {};
for (const n of names) {
  let r = await j('POST', '/api/v1/auth/register', { online_id: n, password: 'Password123!' });
  ok(r.s === 400 && r.b.error === 'terms_not_accepted', `register senza termini rifiutato (${n})`, r);
  r = await j('POST', '/api/v1/auth/register', { online_id: n, password: 'Password123!', accept_terms: true });
  ok(r.s === 200 || r.s === 201, `register ${n}`, r);
  r = await j('POST', '/api/v1/auth/login', { online_id: n, password: 'Password123!' });
  tok[n] = r.b.access_token || r.b.token;
  ok(!!tok[n], `login ${n}`, r);
}
const A = (n) => ({ authorization: `Bearer ${tok[n]}` });
const [adm, pub, rep1, rep2, rep3] = names;

let r = await j('GET', '/api/v1/me', null, A(pub));
ok(r.b.role === 'user' && r.b.terms && typeof r.b.terms.accepted === 'string' && r.b.terms.accepted === r.b.terms.current && r.b.terms.needs_accept === false, 'me: ruolo e termini accettati', r.b);
r = await j('GET', '/lab/v1/me', null, A(pub));
ok(r.s === 200 && r.b.online_id === pub, 'alias /lab/v1 (app meno recenti)', r);
r = await j('GET', '/api/v1/non-esiste', null, A(pub));
ok(r.s === 404 && r.b.error === 'not_found', 'rotta sconosciuta → 404 not_found', r);
r = await j('DELETE', '/api/v1/me', null, A(pub));
ok(r.s === 405 && r.h.get('allow') === 'GET', 'metodo sbagliato → 405 con Allow', r);

// publish
const app = { title: `Test ${sfx}`, tagline: 't', description: 'd', category: 'Giochi', version: '1.0', download_url: 'https://github.com/x/y/releases/download/v1/a.zip', file_kind: 'zip', hashtags: [] };
r = await j('POST', '/api/v1/store/apps', app, A(pub));
ok(r.s === 400 && r.b.error === 'rights_not_confirmed', 'publish senza rights_confirmed rifiutato', r);
r = await j('POST', '/api/v1/store/apps', { ...app, rights_confirmed: true }, A(pub));
const appId = r.b.app_id || (r.b.app && r.b.app.app_id);
ok(r.s < 300 && appId, 'publish', r);
r = await j('POST', `/api/v1/store/apps/${appId}/comments`, { text: 'commento brutto' }, A(pub));
const cid = r.b.comment_id || (r.b.comment && r.b.comment.comment_id);
ok(r.s < 300 && cid, 'commento', r);

// reports
r = await j('POST', `/api/v1/store/apps/${appId}/report`, { reason: 'boh' }, A(rep1));
ok(r.s === 400, 'motivo non valido', r);
r = await j('POST', `/api/v1/store/apps/${appId}/report`, { reason: 'pirateria', note: 'n' }, A(rep1));
ok(r.s < 300, 'segnala 1', r);
r = await j('POST', `/api/v1/store/apps/${appId}/report`, { reason: 'pirateria' }, A(rep1));
ok(r.s < 300, 'segnala ripetuta idempotente', r);
r = await j('POST', `/api/v1/store/apps/${appId}/report`, { reason: 'spam', comment_id: cid }, A(rep1));
ok(r.s < 300, 'segnala commento', r);
r = await j('POST', `/api/v1/store/apps/${appId}/report`, { reason: 'malware' }, A(rep2));
ok(r.s < 300 && r.b.hidden === false, 'segnala 2', r);
r = await j('POST', `/api/v1/store/apps/${appId}/report`, { reason: 'spam' }, A(rep3));
ok(r.s < 300 && r.b.hidden === true, 'segnala 3: raggiunta la soglia', r);
r = await j('GET', `/api/v1/store/apps?sort=recent&q=${encodeURIComponent(app.title)}`, null, A(rep1));
ok(!(r.b.apps || []).some((a) => String(a.app_id) === String(appId)), 'auto-oscurato dallo Store', r.b);

// admin: not yet admin
r = await j('POST', '/admin/api/login', { online_id: adm, password: 'Password123!' }, { 'x-omega-admin': '1' });
ok(r.s === 403 && r.b.error === 'not_admin', 'login admin rifiutato senza ruolo', r);
console.log(execFileSync('node', ['src/admin.js', 'grant', adm], { cwd: API, env: process.env }).toString().trim());
r = await j('POST', '/admin/api/login', { online_id: adm, password: 'Password123!' });
ok(r.s === 403 && r.b.error === 'csrf', 'login senza X-Omega-Admin rifiutato', r);
r = await j('POST', '/admin/api/login', { online_id: adm, password: 'Password123!' }, { 'x-omega-admin': '1' });
const cookie = (r.h.get('set-cookie') || '').split(';')[0];
ok(r.s === 200 && cookie.startsWith('omega_admin='), 'login admin', r);
const C = { cookie, 'x-omega-admin': '1' };
r = await j('GET', '/admin/api/stats', null, { cookie: 'omega_admin=xx' });
ok(r.s === 401, 'cookie falso → 401', r);
r = await j('GET', '/admin/api/stats', null, { cookie: cookie.replace(/.$/, (c) => (c === 'A' ? 'B' : 'A')) });
ok(r.s === 401, 'firma manomessa → 401', r);
r = await j('GET', '/admin/api/me', null, C); ok(r.b.online_id === adm, 'me', r);
r = await j('GET', '/admin/api/stats', null, C); ok(r.s === 200 && r.b.open_reports >= 3, 'stats', r);
r = await j('GET', '/admin/api/reports?status=open', null, C);
const rep = (r.b.reports || []).find((x) => x.app.app_id === String(appId) && x.target_type === 'app');
const repC = (r.b.reports || []).find((x) => x.comment && x.comment.comment_id === String(cid));
ok(rep && rep.open_for_target === 3 && repC, 'lista segnalazioni', r.b);
r = await j('POST', `/admin/api/reports/${repC.report_id}`, { action: 'hide' }, C); ok(r.s === 200, 'oscura commento da segnalazione', r);
r = await j('GET', `/api/v1/store/apps/${appId}/comments`, null, A(rep1));
ok(!JSON.stringify(r.b).includes('commento brutto'), 'commento oscurato non visibile', r.b);
r = await j('POST', `/admin/api/reports/${rep.report_id}`, { action: 'dismiss', note: 'falso allarme' }, C); ok(r.s === 200, 'archivia', r);
r = await j('GET', '/admin/api/reports?status=open', null, C);
ok(!(r.b.reports || []).some((x) => x.app.app_id === String(appId)), 'chiuse tutte le segnalazioni del contenuto', r.b);
r = await j('POST', `/admin/api/apps/${appId}`, { action: 'show' }, C); ok(r.s === 200, 'ripristina app', r);
r = await j('GET', `/admin/api/apps?q=${encodeURIComponent(app.title)}`, null, C);
ok(r.b.total === 1 && r.b.apps[0].published === true, 'lista apps', r.b);
r = await j('GET', `/admin/api/comments?app_id=${appId}`, null, C); ok(r.b.total === 1 && r.b.comments[0].hidden, 'lista commenti', r.b);
r = await j('GET', `/admin/api/users?q=${sfx}`, null, C); ok(r.b.total === 5, 'lista utenti', r.b);
const pubId = r.b.users.find((u) => u.online_id === pub).account_id;
const admId = r.b.users.find((u) => u.online_id === adm).account_id;
r = await j('POST', `/admin/api/users/${admId}`, { action: 'ban' }, C); ok(r.s === 400, 'non ci si banna da soli', r);

// ban
r = await j('POST', `/admin/api/users/${pubId}`, { action: 'ban', reason: 'pirateria ripetuta' }, C); ok(r.s === 200, 'ban', r);
r = await j('GET', '/api/v1/me', null, A(pub)); ok(r.s === 401, 'sessione del bannato revocata', r);
r = await j('POST', '/api/v1/auth/login', { online_id: pub, password: 'Password123!' });
ok(r.s === 403 && r.b.error === 'account_banned' && /pirateria/.test(r.b.detail), 'login bannato → 403 con motivo', r);
r = await j('POST', '/api/v1/auth/login', { online_id: pub, password: 'sbagliata' }); ok(r.s === 401, 'bannato con password errata → 401 generico', r);
r = await j('GET', `/admin/api/apps?q=${encodeURIComponent(app.title)}`, null, C); ok(r.b.apps[0].published === false, 'app del bannato oscurate', r.b);
r = await j('POST', `/admin/api/users/${pubId}`, { action: 'unban' }, C); ok(r.s === 200, 'unban', r);
r = await j('GET', `/admin/api/apps?q=${encodeURIComponent(app.title)}`, null, C); ok(r.b.apps[0].published === true, 'app ripristinate', r.b);
r = await j('POST', '/api/v1/auth/login', { online_id: pub, password: 'Password123!' }); ok(r.s === 200, 'login dopo unban', r);
tok[pub] = r.b.access_token || r.b.token;

// account
r = await j('GET', '/api/v1/account/export', null, A(pub));
ok(r.s === 200 && r.b.store_apps.length === 1 && r.b.account.online_id === pub, 'export dati', r.b);
r = await j('POST', '/api/v1/account/terms', { version: 'vecchia' }, A(pub)); ok(r.s === 409, 'termini versione sbagliata', r);
r = await j('POST', '/api/v1/account/delete', { password: 'no' }, A(rep2)); ok(r.s === 401, 'delete con password errata', r);
r = await j('POST', '/api/v1/account/delete', { password: 'Password123!' }, A(rep2)); ok(r.s === 200, 'delete account', r);
r = await j('POST', '/api/v1/auth/login', { online_id: rep2, password: 'Password123!' }); ok(r.s === 401, 'account eliminato non entra', r);

// admin delete + log
r = await j('DELETE', `/admin/api/apps/${appId}`, null, { cookie }); ok(r.s === 403, 'DELETE senza header → csrf', r);
r = await j('DELETE', `/admin/api/apps/${appId}`, null, C); ok(r.s === 200, 'elimina app', r);
r = await j('GET', '/admin/api/log?limit=20', null, C);
ok(r.b.log.some((l) => l.action === 'user_ban') && r.b.log.some((l) => l.action === 'app_delete'), 'registro azioni', r.b);

// legal / source / admin page
r = await j('GET', '/api/v1/legal'); ok(r.s === 200 && r.b.documents.privacy.sections.length > 3, 'legal json', r);
for (const p of ['/legal', '/legal/privacy', '/legal/terms', '/legal/licenses', '/source', '/admin']) {
  const x = await fetch(B + p, { headers: IP }); ok(x.status === 200 && /text\/html/.test(x.headers.get('content-type')), `GET ${p}`, x.status);
}
const tz = await fetch(B + '/source/omega-src.tar.gz', { headers: IP }); const buf = Buffer.from(await tz.arrayBuffer());
ok(tz.status === 200 && buf[0] === 0x1f && buf.length > 10000, `tar sorgente ${buf.length} B`, tz.status);
r = await j('POST', '/admin/api/logout', null, C); ok(r.s === 204, 'logout admin', r);
r = await j('GET', '/admin/api/me', null, C); ok(r.s === 401, 'dopo logout → 401', r);
done();
