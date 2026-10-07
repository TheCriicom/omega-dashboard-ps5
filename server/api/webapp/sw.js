// Service worker di Omega: guscio dell'app in cache, API sempre dalla rete.
// Le risposte dell'API (con il token) non vengono mai messe in cache.
const VERSION = 'omega-web-1';
const SHELL = [
  '/app/', '/app/app.css', '/app/app.js', '/app/manifest.webmanifest',
  '/app/js/api.js', '/app/js/auth.js', '/app/js/common.js', '/app/js/i18n.js', '/app/js/icons.js', '/app/js/install.js',
  '/app/js/nav.js', '/app/js/onboarding.js', '/app/js/state.js', '/app/js/ui.js', '/app/js/views.js',
  '/app/js/v-chat.js', '/app/js/v-community.js', '/app/js/v-friends.js', '/app/js/v-home.js', '/app/js/v-me.js', '/app/js/v-store.js',
  '/app/icons/icon.svg', '/app/icons/icon-192.png', '/app/icons/apple-touch-icon.png',
];

self.addEventListener('install', (e) => {
  e.waitUntil(caches.open(VERSION).then((c) => c.addAll(SHELL)).then(() => self.skipWaiting()));
});

self.addEventListener('activate', (e) => {
  e.waitUntil(caches.keys()
    .then((keys) => Promise.all(keys.filter((k) => k.startsWith('omega-web-') && k !== VERSION).map((k) => caches.delete(k))))
    .then(() => self.clients.claim()));
});

self.addEventListener('fetch', (e) => {
  const req = e.request;
  if (req.method !== 'GET') return;
  const url = new URL(req.url);
  if (url.origin !== location.origin) return;
  // API: sempre dalla rete, mai in cache; offline → 503 JSON comprensibile dall'app
  if (url.pathname.startsWith('/api/')) {
    e.respondWith(fetch(req).catch(() => new Response(JSON.stringify({ error: 'network' }), { status: 503, headers: { 'content-type': 'application/json' } })));
    return;
  }
  if (!url.pathname.startsWith('/app')) return;
  // pagina: rete prima (sempre fresca), cache se offline
  if (req.mode === 'navigate') {
    e.respondWith(fetch(req).then((res) => {
      const copy = res.clone();
      caches.open(VERSION).then((c) => c.put('/app/', copy));
      return res;
    }).catch(() => caches.match('/app/')));
    return;
  }
  // file del guscio: cache subito, aggiornata in background
  e.respondWith(caches.open(VERSION).then(async (c) => {
    const hit = await c.match(req, { ignoreSearch: true });
    const net = fetch(req).then((res) => { if (res.ok) c.put(req, res.clone()); return res; }).catch(() => hit);
    return hit || net;
  }));
});
