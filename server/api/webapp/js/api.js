// Client dell'API di Omega: token di sessione, errori, immagini protette.
import { LANG } from './i18n.js';

const BASE = '/api/v1';
const KEY = 'omega.session';

let session = null;
try { session = JSON.parse(localStorage.getItem(KEY) || 'null'); } catch { session = null; }
// niente controllo locale di expires_at: il server rinnova la sessione a ogni uso, decide lui (401)

const listeners = new Set();
export function onUnauthorized(fn) { listeners.add(fn); }

export const auth = {
  get token() { return session && session.token; },
  get onlineId() { return session && session.online_id; },
  get loggedIn() { return !!(session && session.token); },
  set(s) {
    session = { token: s.token, expires_at: s.expires_at || null, online_id: s.online_id || null };
    try { localStorage.setItem(KEY, JSON.stringify(session)); } catch { /* privato */ }
  },
  setOnlineId(id) { if (session) { session.online_id = id; this.set(session); } },
  clear() {
    session = null;
    try { localStorage.removeItem(KEY); } catch { /* privato */ }
    imageCache.forEach((u) => { if (u && u.startsWith('blob:')) URL.revokeObjectURL(u); });
    imageCache.clear();
  },
};

export class ApiError extends Error {
  constructor(status, code, detail) {
    super(detail || code || `HTTP ${status}`);
    this.status = status; this.code = code || `http_${status}`; this.detail = detail || null;
  }
}

function headers(extra) {
  const h = { Accept: 'application/json', 'Accept-Language': LANG, ...extra };
  if (session && session.token) h.Authorization = `Bearer ${session.token}`;
  return h;
}

export async function api(path, opts = {}) {
  const { method = 'GET', body, signal, raw } = opts;
  const init = { method, headers: headers(body !== undefined && !raw ? { 'Content-Type': 'application/json' } : {}), signal, cache: 'no-store' };
  if (body !== undefined) init.body = raw ? body : JSON.stringify(body);
  let res;
  try { res = await fetch(BASE + path, init); }
  catch (e) {
    if (e.name === 'AbortError') throw e;
    throw new ApiError(0, 'network', e.message);
  }
  if (res.status === 204) return {};
  let data = null;
  const ct = res.headers.get('content-type') || '';
  if (ct.includes('json')) { try { data = await res.json(); } catch { data = null; } }
  if (!res.ok) {
    const err = new ApiError(res.status, data && data.error, data && data.detail);
    if (res.status === 401 && session && !path.startsWith('/auth/')) {
      auth.clear();
      listeners.forEach((fn) => fn(err));
    }
    if (res.status === 429) err.retryAfter = Number(res.headers.get('retry-after')) || null;
    throw err;
  }
  return data || {};
}

export const get = (p, o) => api(p, o);
export const post = (p, body, o) => api(p, { ...o, method: 'POST', body: body === undefined ? {} : body });
export const del = (p, o) => api(p, { ...o, method: 'DELETE' });
export const enc = encodeURIComponent;
export const E = encodeURIComponent;

// --- immagini protette dal token: fetch() + blob: URL, con cache in memoria ---
const imageCache = new Map();          // url -> blob url | null (errore)
const pending = new Map();
const MAX_CACHE = 400;

export function imageUrl(path) {
  if (imageCache.has(path)) return Promise.resolve(imageCache.get(path));
  if (pending.has(path)) return pending.get(path);
  const p = (async () => {
    try {
      const res = await fetch(BASE + path, { headers: headers({ Accept: 'image/*' }) });
      if (!res.ok) throw new Error(String(res.status));
      const blob = await res.blob();
      if (!blob.type.startsWith('image/')) throw new Error('not an image');
      const u = URL.createObjectURL(blob);
      if (imageCache.size >= MAX_CACHE) {
        const first = imageCache.keys().next().value;
        const old = imageCache.get(first);
        imageCache.delete(first);
        if (old) setTimeout(() => URL.revokeObjectURL(old), 30000);
      }
      imageCache.set(path, u);
      return u;
    } catch {
      imageCache.set(path, null);
      return null;
    } finally {
      pending.delete(path);
    }
  })();
  pending.set(path, p);
  return p;
}

export function cachedImage(path) { return imageCache.get(path); }
