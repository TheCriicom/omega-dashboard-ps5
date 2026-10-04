// Radio del lettore musicale: autenticazione, validazione, forma delle stazioni
// e proxy dei loghi. Le richieste vere a radio-browser.info servono la rete:
// senza, quei controlli si saltano (il server risponde 502 radio_unavailable).
import { BASE, checker, j, suffix } from './helpers.mjs';

const { ok, done } = checker();
const n = `rad${suffix()}`;
await j('POST', '/api/v1/auth/register', { online_id: n, password: 'Password123!', accept_terms: true });
const login = await j('POST', '/api/v1/auth/login', { online_id: n, password: 'Password123!' });
const tok = login.b.token || login.b.access_token;
ok(!!tok, 'login', login);
const H = { authorization: `Bearer ${tok}` };

let r = await j('GET', '/api/v1/radio/search?q=jazz');
ok(r.s === 401, 'ricerca senza sessione → 401', r);
r = await j('POST', '/api/v1/radio/click/non-un-id', null, H);
ok(r.s === 400, 'click con id non valido → 400', r);
r = await j('GET', '/api/v1/radio/icon/sconosciuto', null, H);
ok(r.s === 404, 'logo sconosciuto → 404', r);

r = await j('GET', '/api/v1/radio/search?q=paradise&limit=10', null, H);
if (r.s === 502) {
  console.log('SKIP radio-browser non raggiungibile: salto i controlli in rete');
} else {
  ok(r.s === 200 && Array.isArray(r.b.stations) && r.b.stations.length > 0, 'ricerca per nome', r);
  const s = r.b.stations[0] || {};
  ok(/^[0-9a-f-]{36}$/.test(s.id) && /^https?:\/\//.test(s.url) && typeof s.name === 'string', 'stazione: id, url, nome', s);
  ok(r.b.stations.every((x) => ['MP3', 'AAC', 'AAC+', 'OGG', 'OPUS', 'FLAC', 'UNKNOWN', '', 'HLS'].includes(x.codec)), 'solo formati che il lettore apre', r.b.stations.map((x) => x.codec));
  ok(r.b.stations.length <= 10, 'limite rispettato', r.b.stations.length);
  ok(!JSON.stringify(r.b).includes('favicon'), 'nessun URL di logo esterno nella risposta');

  r = await j('GET', '/api/v1/radio/top?country=IT&limit=5', null, H);
  ok(r.s === 200 && r.b.stations.length > 0 && r.b.stations.every((x) => x.country === 'IT'), 'più ascoltate in Italia', r.b);
  r = await j('GET', '/api/v1/radio/top?limit=5', null, H);
  ok(r.s === 200 && r.b.stations.length > 0, 'più ascoltate nel mondo', r.s);
  r = await j('GET', '/api/v1/radio/tags', null, H);
  ok(r.s === 200 && r.b.tags.length > 5 && r.b.tags.every((t) => t.name && t.count >= 50), 'generi', r.b);
  r = await j('GET', '/api/v1/radio/search?tag=jazz&country=DE&limit=5', null, H);
  ok(r.s === 200 && r.b.stations.every((x) => x.country === 'DE'), 'genere + paese', r.b);
  r = await j('POST', `/api/v1/radio/click/${s.id}`, null, H);
  ok(r.s === 200 && r.b.ok, 'click conteggiato', r);

  const withIcon = (await j('GET', '/api/v1/radio/top?limit=40', null, H)).b.stations.find((x) => x.icon);
  if (withIcon) {
    const ir = await fetch(BASE + withIcon.icon, { headers: H });
    ok([200, 415, 502, 413].includes(ir.status), `logo via proxy (${ir.status})`, withIcon.icon);
    if (ir.status === 200) ok(/^image\//.test(ir.headers.get('content-type')), 'logo è un\'immagine', ir.headers.get('content-type'));
  }
}
done();
