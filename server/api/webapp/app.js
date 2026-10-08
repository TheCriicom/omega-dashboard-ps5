// Omega web app: guscio (barra laterale / barra delle schede), router, sync.
import { t, LANG } from './js/i18n.js';
import { auth, get, onUnauthorized } from './js/api.js';
import { installSprite } from './js/icons.js';
import { h, icon, iconBtn, mark, replace, clear, toast, avatar, closeTopSheet, topSheet, errorState, vibrate } from './js/ui.js';
import { state, emit, on, pref, setPref } from './js/state.js';
import * as V from './js/views.js';
import { authView, termsView } from './js/auth.js';
import { onboarding } from './js/onboarding.js';
import { installBanner } from './js/install.js';
import { notifTarget } from './js/nav.js';

// --------------------------------------------------------- avvio dev --
// #token=<jwt>&view=/store : solo per test/screenshot, letto una volta e tolto dall'URL
(function devToken() {
  const m = /^#token=([^&]+)(?:&view=(.*))?$/.exec(location.hash);
  if (!m) return;
  auth.set({ token: decodeURIComponent(m[1]) });
  setPref('onboarded', true);
  setPref('installDismissed', Date.now());
  history.replaceState(null, '', `${location.pathname}#${m[2] ? decodeURIComponent(m[2]) : '/home'}`);
}());

document.documentElement.lang = LANG;
applyTheme();
function applyTheme() {
  const th = pref('theme', 'dark');
  const dark = th === 'dark' || (th === 'auto' && matchMedia('(prefers-color-scheme: dark)').matches);
  document.documentElement.dataset.theme = dark ? 'dark' : 'light';
  const meta = document.querySelector('meta[name="theme-color"]');
  if (meta) meta.content = dark ? '#0c0e14' : '#f4f5fa';
}
matchMedia('(prefers-color-scheme: dark)').addEventListener('change', applyTheme);
on('theme', applyTheme);

// ------------------------------------------------------------- rotte --
const ROUTES = [
  ['/home', V.home, { tab: 'home', root: true, title: 'nav.home' }],
  ['/friends', V.friends, { tab: 'friends', root: true, title: 'nav.friends' }],
  ['/user/:id', V.user, { tab: 'friends' }],
  ['/messages', V.messages, { tab: 'messages', title: 'nav.messages' }],
  ['/messages/:id', V.chat, { tab: 'messages', chat: true }],
  ['/party', V.party, { tab: 'party', title: 'nav.party' }],
  ['/community', V.community, { tab: 'community', root: true, title: 'nav.community' }],
  ['/post/:id', V.post, { tab: 'community', title: 'feed.post' }],
  ['/group/:id', V.group, { tab: 'community', chat: true }],
  ['/records/game/:id', V.gameRecords, { tab: 'community', title: 'records.title' }],
  ['/game/:id', V.game, { tab: 'community' }],
  ['/trophies/:user', V.trophies, { tab: 'community', title: 'trophies.title' }],
  ['/trophies/:user/:np', V.trophySet, { tab: 'community', title: 'trophies.title' }],
  ['/store', V.store, { tab: 'store', root: true, title: 'nav.store' }],
  ['/store/browse', V.storeBrowse, { tab: 'store', title: 'store.browse' }],
  ['/store/app/:id', V.storeApp, { tab: 'store', title: 'nav.store', transparent: true }],
  ['/store/creators', V.storeCreators, { tab: 'store', title: 'store.creators' }],
  ['/notifications', V.notifications, { tab: 'notifications', title: 'nav.notifications' }],
  ['/settings/notifications', V.notifPrefs, { tab: 'me', title: 'nprefs.title' }],
  ['/console', V.consoleView, { tab: 'console', title: 'console.title' }],
  ['/me', V.me, { tab: 'me', root: true, title: 'nav.me' }],
  ['/settings/saves', V.saves, { tab: 'me', title: 'saves.title' }],
  ['/settings/privacy', V.privacy, { tab: 'me', title: 'privacy.title' }],
  ['/settings/account', V.account, { tab: 'me', title: 'account.title' }],
  ['/settings/blocked', V.blocked, { tab: 'me', title: 'blocked.title' }],
  ['/settings/app', V.appSettings, { tab: 'me', title: 'appset.title' }],
  ['/stats', V.stats, { tab: 'community', title: 'stats.title' }],
  ['/wrap', V.wrap, { tab: 'community', title: 'wrap.title' }],
  ['/about', V.about, { tab: 'me', title: 'about.title' }],
].map(([pattern, view, meta]) => {
  const keys = [];
  const re = new RegExp(`^${pattern.replace(/:(\w+)/g, (_, k) => { keys.push(k); return '([^/]+)'; })}$`);
  return { pattern, re, keys, view, meta };
});

function parseHash() {
  const raw = location.hash.replace(/^#/, '') || '/home';
  const [path, qs] = raw.split('?');
  const query = Object.fromEntries(new URLSearchParams(qs || ''));
  for (const r of ROUTES) {
    const m = r.re.exec(path);
    if (m) {
      const params = {};
      r.keys.forEach((k, i) => { params[k] = decodeURIComponent(m[i + 1]); });
      return { route: r, params, query, path, full: raw };
    }
  }
  return null;
}

export function nav(path, { replace: rep = false } = {}) {
  const target = `#${path}`;
  if (location.hash === target) { render(); return; }
  if (rep) { history.replaceState(null, '', target); render(); } else location.hash = target;
}
window.omegaNav = nav;

// --------------------------------------------------------------- guscio --
const TABS = [
  { id: 'home', icon: 'home', label: 'nav.home', path: '/home' },
  { id: 'friends', icon: 'friends', label: 'nav.friends', path: '/friends', badge: () => state.counts.incoming_requests },
  { id: 'community', icon: 'community', label: 'nav.community', path: '/community', badge: () => state.counts.unread_groups },
  { id: 'store', icon: 'store', label: 'nav.store', path: '/store' },
  { id: 'me', icon: 'user', label: 'nav.me', path: '/me' },
];
const SIDE = [
  { id: 'home', icon: 'home', label: 'nav.home', path: '/home' },
  { id: 'friends', icon: 'friends', label: 'nav.friends', path: '/friends', badge: () => state.counts.incoming_requests },
  { id: 'messages', icon: 'chat', label: 'nav.messages', path: '/messages', badge: () => state.counts.unread_messages },
  { id: 'party', icon: 'party', label: 'nav.party', path: '/party', badge: () => state.partyInvites.length, dot: () => !!state.party },
  { id: 'community', icon: 'community', label: 'nav.community', path: '/community', badge: () => state.counts.unread_groups },
  { id: 'store', icon: 'store', label: 'nav.store', path: '/store' },
  { id: 'notifications', icon: 'bell', label: 'nav.notifications', path: '/notifications', badge: () => state.counts.unread_notifications },
  { id: 'console', icon: 'console', label: 'nav.console', path: '/console' },
  { id: 'me', icon: 'user', label: 'nav.me', path: '/me' },
];

const root = document.getElementById('root');
let shell = null;

function buildShell() {
  const sideNav = h('nav', { class: 'side-nav', 'aria-label': t('nav.main') },
    SIDE.map((it) => h('a', { class: 'side-link', href: `#${it.path}`, dataset: { tab: it.id } },
      icon(it.icon, 22), h('span', { class: 'side-label', text: t(it.label) }), h('span', { class: 'side-badge' }))));
  const sideMe = h('a', { class: 'side-me', href: '#/me' });
  const sidebar = h('aside', { class: 'sidebar' },
    h('a', { class: 'brand', href: '#/home', 'aria-label': 'Omega' }, mark(30), h('span', { class: 'brand-name', text: 'Omega' })),
    sideNav, h('div', { class: 'side-foot' }, sideMe));

  const back = h('button', { class: 'icon-btn hdr-back', type: 'button', 'aria-label': t('common.back'), onclick: goBack }, icon('back', 24));
  const titleEl = h('h1', { class: 'hdr-title' });
  const brand = h('a', { class: 'hdr-brand', href: '#/home', 'aria-label': 'Omega' }, mark(26), h('span', { text: 'Omega' }));
  const actions = h('div', { class: 'hdr-actions' });
  const globalActs = h('div', { class: 'hdr-global' },
    h('a', { class: 'icon-btn hdr-msg', href: '#/messages', 'aria-label': t('nav.messages'), title: t('nav.messages') }, icon('chat', 22), h('span', { class: 'dot-badge' })),
    h('a', { class: 'icon-btn hdr-bell', href: '#/notifications', 'aria-label': t('nav.notifications'), title: t('nav.notifications') }, icon('bell', 22), h('span', { class: 'dot-badge' })));
  const header = h('header', { class: 'hdr' }, h('div', { class: 'hdr-in' }, back, brand, titleEl, h('div', { class: 'hdr-spacer' }), actions, globalActs));

  const ptr = h('div', { class: 'ptr', 'aria-hidden': 'true' }, h('div', { class: 'ptr-spin' }, icon('refresh', 20)));
  const main = h('main', { class: 'main', id: 'main', tabindex: '-1' });
  const content = h('div', { class: 'content' }, header, ptr, main);

  const tabbar = h('nav', { class: 'tabbar', 'aria-label': t('nav.main') },
    TABS.map((it) => h('a', { class: 'tab-link', href: `#${it.path}`, dataset: { tab: it.id } },
      h('span', { class: 'tab-ic' }, icon(it.icon, 24), h('span', { class: 'tab-badge' })), h('span', { class: 'tab-label', text: t(it.label) }))));
  const partyPill = h('a', { class: 'party-pill', href: '#/party', hidden: true }, icon('party', 18), h('span', { class: 'pp-text' }));
  const skip = h('a', { class: 'skip', href: '#main', text: t('nav.skip'), onclick: (e) => { e.preventDefault(); main.focus(); } });

  replace(root, h('div', { class: 'app' }, skip, sidebar, content, tabbar, partyPill));
  shell = { sidebar, sideMe, header, back, titleEl, brand, actions, main, tabbar, ptr, partyPill, content };
  setupPullToRefresh();
  updateBadges();
}

function setBadge(el, n) {
  if (!el) return;
  el.textContent = n > 0 ? (n > 99 ? '99+' : String(n)) : '';
  el.classList.toggle('on', n > 0);
}
function updateBadges() {
  if (!shell) return;
  for (const it of SIDE) {
    const a = shell.sidebar.querySelector(`[data-tab="${it.id}"] .side-badge`);
    const n = it.badge ? it.badge() : 0;
    setBadge(a, n);
    if (a && !n && it.dot && it.dot()) { a.classList.add('on', 'is-dot'); } else if (a) a.classList.remove('is-dot');
  }
  for (const it of TABS) setBadge(shell.tabbar.querySelector(`[data-tab="${it.id}"] .tab-badge`), it.badge ? it.badge() : 0);
  const meBadge = shell.tabbar.querySelector('[data-tab="me"] .tab-badge');
  if (meBadge && state.partyInvites.length) setBadge(meBadge, state.partyInvites.length);
  setBadge(shell.header.querySelector('.hdr-msg .dot-badge'), state.counts.unread_messages);
  setBadge(shell.header.querySelector('.hdr-bell .dot-badge'), state.counts.unread_notifications);
  // io nella barra laterale
  if (state.me) {
    replace(shell.sideMe, avatar(state.me, 36), h('span', { class: 'side-me-txt' },
      h('strong', { text: state.me.online_id }), h('small', { text: t(`status.${state.me.status_mode || 'online'}`) })));
  }
  // party attivo: pillola sopra la barra delle schede
  const p = state.party;
  shell.partyPill.hidden = !p || (current && current.route.pattern === '/party');
  document.body.classList.toggle('has-pill', !shell.partyPill.hidden);
  if (p) shell.partyPill.querySelector('.pp-text').textContent = t('party.pill', { name: p.name, n: p.members.length });
  const unread = state.counts.unread_messages + state.counts.unread_notifications;
  document.title = unread > 0 ? `(${unread}) Omega` : 'Omega';
  if ('setAppBadge' in navigator) { try { unread ? navigator.setAppBadge(unread) : navigator.clearAppBadge(); } catch { /* niente */ } }
}
on('badges', updateBadges);

// ------------------------------------------------------------ render --
let current = null;        // { route, params, query, ctx }
let navDepth = 0;
const scrollMemo = new Map();
let lastHash = null;

window.addEventListener('hashchange', () => render());
window.addEventListener('popstate', () => { navDepth = Math.max(0, navDepth - 1); });

function goBack() {
  if (topSheet()) { closeTopSheet(); return; }
  if (navDepth > 0 && history.length > 1) { history.back(); return; }
  const r = current && current.route;
  const parent = r && r.meta.tab ? (TABS.concat(SIDE).find((x) => x.id === r.meta.tab) || {}).path : '/home';
  nav(parent || '/home', { replace: true });
}

async function render() {
  if (!auth.loggedIn) { showAuth(); return; }
  if (!shell) { buildShell(); }
  const m = parseHash();
  if (!m) { nav('/home', { replace: true }); return; }
  if (lastHash) scrollMemo.set(lastHash, window.scrollY);
  const isBack = scrollMemo.has(m.full) && current;
  if (current && current.route !== m.route || current && current.full !== m.full) navDepth++;
  if (current && current.ctx) current.ctx._destroy();
  lastHash = m.full;

  const { route } = m;
  const meta = route.meta;
  // guscio: scheda attiva, intestazione
  for (const a of root.querySelectorAll('[data-tab]')) a.classList.toggle('active', a.dataset.tab === meta.tab);
  for (const a of root.querySelectorAll('.side-link')) a.toggleAttribute('aria-current', a.dataset.tab === meta.tab);
  document.body.classList.toggle('is-root', !!meta.root);
  document.body.classList.toggle('is-chat', !!meta.chat);
  document.body.classList.toggle('hdr-transparent', !!meta.transparent);
  document.body.classList.toggle('is-home', route.pattern === '/home');
  shell.back.hidden = !!meta.root;
  shell.titleEl.textContent = meta.title ? t(meta.title) : '';
  clear(shell.actions);
  closeAllSheets();

  const page = h('div', { class: 'page' });
  replace(shell.main, page);
  const ctx = makeCtx(page, m);
  current = { ...m, ctx };
  updateBadges();
  window.scrollTo(0, 0);
  const pending = Promise.resolve().then(() => route.view(ctx));
  requestAnimationFrame(() => page.classList.add('in'));
  try {
    await pending;
  } catch (e) {
    if (ctx.dead) return;
    console.error(e);
    replace(page, errorState(e, () => render()));
  }
  if (ctx.dead) return;
  if (isBack) window.scrollTo(0, scrollMemo.get(m.full) || 0);
}

function closeAllSheets() { while (closeTopSheet()) { /* chiude tutto */ } }

function makeCtx(page, m) {
  const cleanups = [];
  const refreshers = [];
  const ctx = {
    page, params: m.params, query: m.query, path: m.path, dead: false, nav,
    setTitle(s) { if (!ctx.dead) replace(shell.titleEl, s || ''); },
    setActions(...els) { if (!ctx.dead) replace(shell.actions, ...els); },
    onCleanup(fn) { cleanups.push(fn); },
    onRefresh(fn) { refreshers.push(fn); },
    async refresh() { for (const f of refreshers) await f(); },
    get hasRefresh() { return refreshers.length > 0; },
    // ripete fn ogni ms mentre la pagina è visibile e aperta
    every(ms, fn) {
      let tm = null;
      const tick = async () => {
        if (ctx.dead) return;
        if (document.visibilityState === 'visible') { try { await fn(); } catch { /* il prossimo giro riprova */ } }
        if (!ctx.dead) tm = setTimeout(tick, ms);
      };
      tm = setTimeout(tick, ms);
      const vis = () => { if (document.visibilityState === 'visible' && !ctx.dead) { clearTimeout(tm); tick(); } };
      document.addEventListener('visibilitychange', vis);
      cleanups.push(() => { clearTimeout(tm); document.removeEventListener('visibilitychange', vis); });
    },
    on(name, fn) { cleanups.push(on(name, fn)); },
    _destroy() { ctx.dead = true; cleanups.forEach((f) => { try { f(); } catch { /* niente */ } }); },
  };
  return ctx;
}

// ----------------------------------------------------- tirare per aggiornare --
function setupPullToRefresh() {
  let y0 = null, dy = 0, active = false;
  const max = 90;
  window.addEventListener('touchstart', (e) => {
    if (window.scrollY > 0 || topSheet() || !current || !current.ctx.hasRefresh || document.body.classList.contains('is-chat')) { y0 = null; return; }
    y0 = e.touches[0].clientY; dy = 0; active = false;
  }, { passive: true });
  window.addEventListener('touchmove', (e) => {
    if (y0 == null) return;
    dy = e.touches[0].clientY - y0;
    if (dy <= 0 || window.scrollY > 0) { shell.ptr.style.transform = ''; shell.ptr.classList.remove('show'); return; }
    active = true;
    const d = Math.min(max, dy * 0.5);
    shell.ptr.classList.add('show');
    shell.ptr.style.transform = `translate(-50%, ${d}px) rotate(${d * 4}deg)`;
    shell.ptr.classList.toggle('ready', d >= max * 0.8);
  }, { passive: true });
  window.addEventListener('touchend', async () => {
    if (y0 == null || !active) { y0 = null; return; }
    y0 = null;
    if (shell.ptr.classList.contains('ready')) {
      vibrate(10);
      shell.ptr.classList.add('spinning');
      shell.ptr.style.transform = 'translate(-50%, 56px)';
      try { await Promise.all([current.ctx.refresh(), syncNow()]); } catch { /* lo mostra la vista */ }
    }
    shell.ptr.classList.remove('spinning', 'ready', 'show');
    shell.ptr.style.transform = '';
  });
}

// ----------------------------------------------------------------- sync --
let syncTimer = null;
let syncing = false;
const SYNC_MS = 12000;

export async function syncNow() {
  if (syncing || !auth.loggedIn) return;
  syncing = true;
  try {
    const first = !state.synced;
    const d = await get(`/sync${state.lastNotificationId ? `?since=${state.lastNotificationId}` : ''}`);
    state.me = { ...d.me };
    if (!auth.onlineId && d.me) auth.setOnlineId(d.me.online_id);
    state.counts = {
      unread_notifications: d.unread_notifications, unread_messages: d.unread_messages,
      incoming_requests: d.incoming_requests, outgoing_requests: d.outgoing_requests, unread_groups: d.unread_groups || 0,
    };
    state.friends = d.friends || [];
    state.party = d.party || null;
    state.partyInvites = d.party_invites || [];
    if (!first) for (const n of d.notifications || []) if (!n.silent) notifyToast(n);
    state.lastNotificationId = Number(d.last_notification_id) || state.lastNotificationId;
    state.synced = true;
    updateBadges();
    emit('sync', d);
  } catch (e) {
    if (e.status === 0) emit('offline', e);
  } finally { syncing = false; }
}
function notifyToast(n) {
  const where = notifTarget(n);
  toast(n.body ? `${n.title} · ${n.body}` : n.title, { user: n.actor ? { online_id: n.actor, avatar: n.avatar } : null, icon: 'bell', onClick: where ? () => nav(where) : null, duration: 5000 });
}


function startSync() {
  stopSync();
  const loop = async () => {
    if (document.visibilityState === 'visible') await syncNow();
    syncTimer = setTimeout(loop, SYNC_MS);
  };
  loop();
}
function stopSync() { clearTimeout(syncTimer); syncTimer = null; }
document.addEventListener('visibilitychange', () => {
  if (document.visibilityState === 'visible' && auth.loggedIn) { clearTimeout(syncTimer); startSync(); }
});
on('resync', () => syncNow());

// --------------------------------------------------------- accesso --
function showAuth() {
  stopSync();
  shell = null; current = null;
  Object.assign(state, { me: null, account: null, friends: [], party: null, partyInvites: [], synced: false, lastNotificationId: 0 });
  document.body.className = 'is-auth';
  replace(root, authView(onLoggedIn));
  hideBoot();
}
async function onLoggedIn() {
  document.body.className = '';
  await boot();
}
onUnauthorized(() => { toast(t('auth.expired'), { kind: 'error' }); showAuth(); });
on('logout', () => showAuth());

async function boot() {
  if (!auth.loggedIn) { showAuth(); return; }
  try {
    state.account = await get('/me');
    auth.setOnlineId(state.account.online_id);
    if (!state.me) state.me = { online_id: state.account.online_id, avatar: 0, status_mode: 'online' };
  } catch (e) {
    if (!auth.loggedIn) return;      // 401 → già al login
    if (e.status === 0) { /* offline: si prova comunque a mostrare l'app */ } else { console.warn(e); }
  }
  if (state.account && state.account.terms && state.account.terms.needs_accept) {
    document.body.className = 'is-auth';
    replace(root, termsView(state.account.terms, () => { document.body.className = ''; state.account.terms.needs_accept = false; boot(); }));
    hideBoot();
    return;
  }
  buildShell();
  hideBoot();
  startSync();
  if (!pref('onboarded', false)) onboarding(() => setPref('onboarded', true));
  else setTimeout(() => installBanner(), 2500);
  render();
}

function hideBoot() {
  const b = document.getElementById('boot');
  if (b) { b.classList.add('out'); setTimeout(() => b.remove(), 400); }
}

// ----------------------------------------------------------- tastiera --
document.addEventListener('keydown', (e) => {
  if (e.key === 'Escape' && closeTopSheet()) { e.preventDefault(); return; }
  const tag = (e.target && e.target.tagName) || '';
  if (/INPUT|TEXTAREA|SELECT/.test(tag) || e.metaKey || e.ctrlKey || e.altKey) return;
  if (e.key === '/' && current) {
    const s = document.querySelector('.page input[type="search"]');
    if (s) { e.preventDefault(); s.focus(); }
  }
  if (e.key === 'g') { awaitingG = true; setTimeout(() => { awaitingG = false; }, 900); return; }
  if (awaitingG) {
    const map = { h: '/home', a: '/friends', m: '/messages', c: '/community', s: '/store', n: '/notifications', p: '/me' };
    if (map[e.key]) { e.preventDefault(); nav(map[e.key]); }
    awaitingG = false;
  }
});
let awaitingG = false;

// online/offline
window.addEventListener('offline', () => toast(t('err.offlineNow'), { kind: 'error', icon: 'wifi' }));
window.addEventListener('online', () => { toast(t('common.backOnline'), { kind: 'ok', icon: 'wifi' }); syncNow(); });

// --------------------------------------------------- service worker --
if ('serviceWorker' in navigator && location.protocol !== 'file:') {
  window.addEventListener('load', () => {
    navigator.serviceWorker.register('/app/sw.js', { scope: '/app/' }).then((reg) => {
      reg.addEventListener('updatefound', () => {
        const w = reg.installing;
        if (!w) return;
        w.addEventListener('statechange', () => {
          if (w.state === 'installed' && navigator.serviceWorker.controller) {
            toast(t('pwa.updateReady'), { action: t('pwa.reload'), onAction: () => location.reload(), duration: 12000, icon: 'sparkle' });
          }
        });
      });
    }).catch(() => { /* senza SW l'app funziona lo stesso */ });
  });
}

window.addEventListener('scroll', () => { document.body.classList.toggle('scrolled', window.scrollY > 4); }, { passive: true });

installSprite();
boot().catch((e) => { console.error(e); replace(root, errorState(e, () => location.reload())); hideBoot(); });

export { render };
