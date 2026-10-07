// Salvataggi online: chiave cifrata, caricamento a pezzi, versioni, e tutto
// quello che un client ostile potrebbe provare (account altrui, pezzi falsi,
// dimensioni sbagliate, quota, id inventati).
import crypto from 'node:crypto';
import { BASE, IP, checker, j, suffix } from './helpers.mjs';

const { ok, done } = checker();
const sfx = suffix();
const N = { a: `sa${sfx}`, b: `sb${sfx}` };
const tok = {};
for (const n of Object.values(N)) {
  await j('POST', '/api/v1/auth/register', { online_id: n, password: 'Password123!', accept_terms: true });
  tok[n] = (await j('POST', '/api/v1/auth/login', { online_id: n, password: 'Password123!' })).b.token;
}
const H = (k) => ({ authorization: `Bearer ${tok[N[k]]}` });
const api = (k, method, p, body) => j(method, p, body, H(k));
const raw = async (k, p, buf, extra = {}) => {
  const r = await fetch(BASE + p, { method: 'POST', headers: { 'content-type': 'application/octet-stream', ...IP, ...H(k), ...extra }, body: buf });
  const t = await r.text(); let b; try { b = JSON.parse(t); } catch { b = t; }
  return { s: r.status, b };
};
const get = async (k, p) => {
  const r = await fetch(BASE + p, { headers: { ...IP, ...H(k) } });
  return { s: r.status, buf: Buffer.from(await r.arrayBuffer()), h: r.headers };
};
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const b64 = (n) => crypto.randomBytes(n).toString('base64');
const blob = (size) => { const b = crypto.randomBytes(size); Buffer.from('OMSAVE1\0', 'latin1').copy(b, 0); return b; };
const TITLE = 'PPSA01234';

console.log('\n--- chiave');
let r = await api('a', 'GET', '/api/v1/saves/key');
ok(r.s === 200 && r.b.exists === false, 'nessuna chiave all\'inizio', r.b);
const key = { key_id: '0123456789abcdef', mem: 65536, iter: 3, salt: b64(16), nonce: b64(24), wrapped: b64(48) };
r = await api('a', 'POST', '/api/v1/saves/begin', { title_id: TITLE, size: 1000, sha256: 'a'.repeat(64), key_id: key.key_id });
ok(r.s === 409 && r.b.error === 'no_key', 'senza chiave non si carica', r.b);
r = await api('a', 'POST', '/api/v1/saves/key', { ...key, wrapped: b64(47) });
ok(r.s === 400, 'chiave cifrata della lunghezza sbagliata rifiutata', r.b);
r = await api('a', 'POST', '/api/v1/saves/key', { ...key, mem: 10_000_000 });
ok(r.s === 400, 'parametri Argon2 assurdi rifiutati', r.b);
r = await api('a', 'POST', '/api/v1/saves/key', { ...key, salt: '!!!!' });
ok(r.s === 400, 'base64 non valido rifiutato', r.b);
r = await api('a', 'POST', '/api/v1/saves/key', key);
ok(r.s === 200 && r.b.key_id === key.key_id, 'chiave salvata', r.b);
r = await api('a', 'GET', '/api/v1/saves/key');
ok(r.b.exists && r.b.wrapped === key.wrapped && r.b.salt === key.salt && r.b.mem === 65536, 'la chiave torna uguale', r.b);
r = await api('a', 'POST', '/api/v1/saves/key', { ...key, key_id: 'fedcba9876543210' });
ok(r.s === 409 && r.b.error === 'key_exists', 'una chiave diversa non sostituisce quella esistente senza reset', r.b);
r = await api('a', 'POST', '/api/v1/saves/key', { ...key, salt: b64(16), wrapped: b64(48) });
ok(r.s === 200, 'nuova parola d\'ordine per la stessa chiave', r.b);
r = await api('b', 'GET', '/api/v1/saves/key');
ok(r.b.exists === false, 'b non vede la chiave di a', r.b);

console.log('\n--- caricamento');
r = await api('a', 'GET', '/api/v1/saves');
const CH = r.b.chunk, Q = r.b.quota;
ok(r.s === 200 && CH === 8 * 1024 * 1024 && Q.used === 0 && r.b.titles.length === 0, 'elenco vuoto, pezzi da 8 MB', r.b);
const data = blob(CH + 4321);
const begin = (k, b) => api(k, 'POST', '/api/v1/saves/begin', { title_id: TITLE, size: data.length, sha256: sha(data), key_id: key.key_id, device: 'PS5 <b>test</b>', ...b });
r = await begin('a', { title_id: '../etc/passwd' });
ok(r.s === 400, 'title_id non valido rifiutato', r.b);
r = await begin('a', { key_id: 'ffffffffffffffff' });
ok(r.s === 409 && r.b.error === 'key_mismatch', 'chiave diversa da quella registrata rifiutata', r.b);
r = await begin('a', { size: Q.max_save + 1 });
ok(r.s === 413, 'oltre la dimensione massima rifiutato', r.b);
r = await begin('a', { size: -5 });
ok(r.s === 400, 'dimensione negativa rifiutata', r.b);
r = await begin('a');
ok(r.s === 201 && /^[a-f0-9]{32}$/.test(r.b.save_id) && r.b.chunks === 2, 'caricamento aperto, 2 pezzi', r.b);
const id = r.b.save_id;
const p0 = data.subarray(0, CH), p1 = data.subarray(CH);

r = await raw('b', `/api/v1/saves/${id}/chunk/0`, p0);
ok(r.s === 404, 'b non può scrivere nel caricamento di a', r.b);
r = await raw('a', `/api/v1/saves/${id}/chunk/1`, Buffer.concat([p1, Buffer.from('x')]));
ok(r.s === 400 && r.b.error === 'chunk_size_mismatch', 'pezzo più lungo del previsto rifiutato', r.b);
r = await raw('a', `/api/v1/saves/${id}/chunk/2`, p1);
ok(r.s === 400 && r.b.error === 'invalid_chunk', 'pezzo oltre la fine rifiutato', r.b);
r = await raw('a', `/api/v1/saves/${id}/chunk/-1`, p1);
ok(r.s === 400 || r.s === 404, 'pezzo negativo rifiutato', r.b);
const bad0 = Buffer.from(p0); bad0.write('NOTSAVE!', 0, 'latin1');
r = await raw('a', `/api/v1/saves/${id}/chunk/0`, bad0);
ok(r.s === 400 && r.b.error === 'invalid_format', 'intestazione sbagliata rifiutata', r.b);
r = await raw('a', `/api/v1/saves/${id}/chunk/1?sha=${'0'.repeat(64)}`, p1);
ok(r.s === 422, 'hash del pezzo non corrispondente rifiutato', r.b);
r = await api('a', 'POST', `/api/v1/saves/${id}/commit`);
ok(r.s === 409 && r.b.error === 'missing_chunks', 'commit senza tutti i pezzi rifiutato', r.b);
r = await raw('a', `/api/v1/saves/${id}/chunk/0?sha=${sha(p0)}`, p0);
ok(r.s === 204, 'pezzo 0 caricato', r.b);
r = await raw('a', `/api/v1/saves/${id}/chunk/1`, p1);
ok(r.s === 204, 'pezzo 1 caricato', r.b);
r = await raw('a', `/api/v1/saves/${id}/chunk/1`, p1);
ok(r.s === 204, 'un pezzo si può rimandare (connessione caduta)', r.b);
r = await api('b', 'POST', `/api/v1/saves/${id}/commit`);
ok(r.s === 404, 'b non può pubblicare il caricamento di a', r.b);
r = await api('a', 'POST', `/api/v1/saves/${id}/commit`);
ok(r.s === 200 && r.b.title_id === TITLE && r.b.quota.used === data.length, 'pubblicato, quota aggiornata', r.b);
r = await api('a', 'POST', `/api/v1/saves/${id}/commit`);
ok(r.s === 404, 'un secondo commit non fa nulla', r.b);
r = await raw('a', `/api/v1/saves/${id}/chunk/0`, p0);
ok(r.s === 404, 'non si scrive più su un salvataggio pubblicato', r.b);

console.log('\n--- scaricamento');
r = await api('a', 'GET', '/api/v1/saves');
const t = r.b.titles[0];
ok(t && t.title_id === TITLE && t.versions.length === 1 && t.versions[0].device === 'PS5 btest/b', 'elenco con una versione, nome console ripulito', r.b);
let g = await get('a', `/api/v1/saves/${id}/chunk/0`);
const g1 = await get('a', `/api/v1/saves/${id}/chunk/1`);
ok(g.s === 200 && g1.s === 200 && sha(Buffer.concat([g.buf, g1.buf])) === sha(data), 'i pezzi scaricati sono identici', g.s);
ok(g.h.get('content-type') === 'application/octet-stream' && g.h.get('x-content-type-options') === 'nosniff'
  && /sandbox/.test(g.h.get('content-security-policy') || '') && /attachment/.test(g.h.get('content-disposition') || ''), 'mai interpretato dal browser');
ok(g.h.get('x-chunk-sha256') === sha(p0), 'hash del pezzo nell\'intestazione');
g = await get('b', `/api/v1/saves/${id}/chunk/0`);
ok(g.s === 404, 'b non scarica i salvataggi di a', g.s);
g = await get('a', `/api/v1/saves/${'f'.repeat(32)}/chunk/0`);
ok(g.s === 404, 'id inventato → 404', g.s);
g = await get('a', `/api/v1/saves/..%2F..%2Fetc%2Fpasswd/chunk/0`);
ok(g.s === 404, 'percorso nell\'id → 404', g.s);
r = await api('b', 'GET', '/api/v1/saves');
ok(r.b.titles.length === 0, 'b non vede i salvataggi di a', r.b);

console.log('\n--- versioni (ne restano 3)');
const small = blob(5000);
const ids = [];
for (let i = 0; i < 3; i++) {
  const s = small; s[100] = i;
  r = await api('a', 'POST', '/api/v1/saves/begin', { title_id: TITLE, size: s.length, sha256: sha(s), key_id: key.key_id });
  await raw('a', `/api/v1/saves/${r.b.save_id}/chunk/0`, s);
  r = await api('a', 'POST', `/api/v1/saves/${r.b.save_id}/commit`);
  ids.push(r.b.save_id);
}
r = await api('a', 'GET', `/api/v1/saves?title=${TITLE}`);
ok(r.b.titles[0].versions.length === 3 && !r.b.titles[0].versions.some((v) => v.save_id === id), 'la versione più vecchia è stata tolta', r.b.titles[0].versions);
g = await get('a', `/api/v1/saves/${id}/chunk/0`);
ok(g.s === 404, 'e il suo file non si scarica più', g.s);
r = await api('a', 'GET', '/api/v1/saves?title=abc');
ok(r.s === 400, 'filtro gioco non valido rifiutato', r.b);

console.log('\n--- hash finale sbagliato');
r = await api('a', 'POST', '/api/v1/saves/begin', { title_id: 'CUSA00001', size: small.length, sha256: 'b'.repeat(64), key_id: key.key_id });
await raw('a', `/api/v1/saves/${r.b.save_id}/chunk/0`, small);
const bid = r.b.save_id;
r = await api('a', 'POST', `/api/v1/saves/${bid}/commit`);
ok(r.s === 422 && r.b.error === 'hash_mismatch', 'commit con hash diverso rifiutato', r.b);
r = await api('a', 'POST', `/api/v1/saves/${bid}/commit`);
ok(r.s === 404, 'e il caricamento è stato cancellato', r.b);

console.log('\n--- cancellazione');
r = await api('b', 'POST', `/api/v1/saves/${ids[0]}/delete`);
ok(r.s === 404, 'b non cancella i salvataggi di a', r.b);
r = await api('a', 'POST', `/api/v1/saves/${ids[0]}/delete`);
ok(r.s === 200 && r.b.removed, 'cancellato', r.b);

console.log('\n--- quota');
r = await api('a', 'GET', '/api/v1/saves');
if (r.b.quota.limit <= 64 * 1024 * 1024) {
  const left = r.b.quota.limit - r.b.quota.used;
  r = await api('a', 'POST', '/api/v1/saves/begin', { title_id: 'CUSA00002', size: Math.min(left + 1, r.b.quota.max_save), sha256: 'c'.repeat(64), key_id: key.key_id });
  ok(r.s === 413 && (r.b.error === 'quota_exceeded' || r.b.error === 'save_too_large'), 'oltre la quota rifiutato', r.b);
} else console.log('(quota grande: prova saltata; avvia l\'api con SAVES_ACCOUNT_QUOTA piccola)');

console.log('\n--- reset della chiave (parola d\'ordine dimenticata)');
r = await api('a', 'POST', '/api/v1/saves/key', { ...key, key_id: 'fedcba9876543210', reset: true });
ok(r.s === 200 && r.b.removed === 2, 'chiave nuova: i salvataggi cifrati con la vecchia sono cancellati', r.b);
r = await api('a', 'GET', '/api/v1/saves');
ok(r.b.titles.length === 0 && r.b.quota.used === 0 && r.b.key_id === 'fedcba9876543210', 'niente più salvataggi', r.b);

done();
