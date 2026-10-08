// Il tuo riepilogo (stile Wrap-Up): le stesse schede della console, a storie,
// con l'immagine da condividere generata nel browser (canvas, niente server).
// I numeri arrivano da GET /api/v1/wrap nel fuso del browser.
import { t, LANG } from './i18n.js';
import { auth, get, post } from './api.js';
import { add, h, icon, avatar, gameIcon, replace, errorState, duration, num, toast, errorText, segmented, btn, confirmDialog } from './ui.js';
import { state, pref, setPref } from './state.js';

const PERIODS = [
  { value: 'w0', period: 'week', back: 0 }, { value: 'w1', period: 'week', back: 1 },
  { value: 'm0', period: 'month', back: 0 }, { value: 'm1', period: 'month', back: 1 }, { value: 'y0', period: 'year', back: 0 },
];
const PERSONA = {
  night_owl: { ic: 'moon', c: ['#4b3fd1', '#140f3d'] }, early_bird: { ic: 'sun', c: ['#f0a32b', '#5a2d05'] },
  marathoner: { ic: 'fire', c: ['#ff6a3d', '#4a1205'] }, hunter: { ic: 'trophy', c: ['#e8b52c', '#4a3304'] },
  explorer: { ic: 'globe', c: ['#21b893', '#063a2f'] }, loyal: { ic: 'heart', c: ['#ff4f86', '#4a0a20'] },
  weekend: { ic: 'party', c: ['#a35cff', '#2b0b52'] }, daily: { ic: 'clock', c: ['#3d8bff', '#0a2352'] },
  social: { ic: 'friends', c: ['#ff5fb7', '#4a0b33'] }, casual: { ic: 'console', c: ['#2fb8ff', '#06304a'] },
};
const SLIDE_COL = {
  intro: ['#2f6bff', '#0b1a4a'], empty: ['#2f6bff', '#0b1a4a'], total: ['#14a3ad', '#04343a'], top: ['#3c5adc', '#0c1546'],
  top5: ['#6e46c8', '#1c0d40'], rhythm: ['#2a338c', '#0a0d2c'], records: ['#dc6432', '#3d1406'], trophies: ['#c8961e', '#3a2a04'],
  together: ['#d2468c', '#3d0a24'], community: ['#28a05a', '#06301a'],
};
const SLIDE_MS = 6500;
const reduced = () => window.matchMedia && matchMedia('(prefers-reduced-motion: reduce)').matches;

function slidesOf(w) {
  const s = ['intro'];
  if (!w.total_seconds) return [...s, 'empty'];
  s.push('total');
  if (w.games.length) s.push('top');
  if (w.games.length >= 2) s.push('top5');
  if (w.hours) s.push('rhythm');
  if (w.longest || w.streak >= 2 || w.new_games) s.push('records');
  if (w.trophies.count) s.push('trophies');
  if (w.together.length) s.push('together');
  if (w.percentile !== null && w.percentile !== undefined) s.push('community');
  s.push('persona');
  return s;
}

function rangeText(w, period) {
  const a = new Date(w.from); const b = new Date(Math.min(Date.now(), new Date(w.to).getTime() - 1000));
  if (period === 'year') return String(a.getFullYear());
  const f = (d) => d.toLocaleDateString(LANG, { day: 'numeric', month: 'short' });
  return `${f(a)} – ${f(b)}`;
}

// numero che sale da 0 al valore
function countUp(el, to, fmt) {
  if (reduced()) { el.textContent = fmt(to); return; }
  const t0 = performance.now();
  const step = (now) => {
    const k = Math.min(1, (now - t0) / 1100); const e = 1 - (1 - k) ** 3;
    el.textContent = fmt(Math.round(to * e));
    if (k < 1 && el.isConnected) requestAnimationFrame(step);
  };
  requestAnimationFrame(step);
}
const up = (el, i = 0) => { el.classList.add('wr-up'); el.style.animationDelay = `${i * 110}ms`; return el; };

function renderSlide(kind, w, opt) {
  const g0 = w.games[0];
  const meName = auth.onlineId || (state.me && state.me.online_id) || '';
  switch (kind) {
    case 'intro': return h('div', { class: 'wr-c' },
      up(icon('mark', 64, 'mark'), 0), up(h('h2', { class: 'wr-xl', text: t(`wrap.title.${opt.value}`) }), 1),
      up(h('p', { class: 'wr-dim', text: rangeText(w, opt.period) }), 2),
      up(h('div', { class: 'wr-me' }, avatar(state.me || { online_id: meName }, 64), h('strong', { text: meName })), 3),
      w.total_seconds ? up(h('p', { class: 'wr-dim small', text: t('wrap.tap') }), 4) : null);
    case 'empty': return h('div', { class: 'wr-c' }, up(icon('console', 56), 0), up(h('h2', { class: 'wr-l', text: t('wrap.empty') }), 1), up(h('p', { class: 'wr-dim', text: t('wrap.emptyText') }), 2));
    case 'total': {
      const big = h('div', { class: 'wr-huge' });
      countUp(big, w.total_seconds, duration);
      const chips = [t('wrap.gamesN', { n: w.games_count }), w.sessions ? t('wrap.sessionsN', { n: w.sessions }) : null, w.days_active ? t('wrap.daysN', { n: w.days_active }) : null].filter(Boolean);
      let trend = null;
      if (w.prev_total_seconds > 0) {
        const pct = Math.round(((w.total_seconds - w.prev_total_seconds) * 100) / w.prev_total_seconds);
        trend = h('span', { class: `wr-chip ${pct >= 0 ? 'ok' : 'warn'}`, text: t('wrap.trend', { p: `${pct >= 0 ? '+' : ''}${pct}%` }) });
      }
      return h('div', { class: 'wr-c' }, up(h('p', { class: 'wr-dim wr-m', text: t('wrap.played') }), 0), up(big, 1), trend ? up(trend, 2) : null,
        up(h('div', { class: 'wr-chips' }, chips.map((c) => h('span', { class: 'wr-chip', text: c }))), 3));
    }
    case 'top': {
      const share = Math.round((g0.seconds * 100) / w.total_seconds);
      return h('div', { class: 'wr-c' }, up(h('p', { class: 'wr-dim wr-m', text: t('wrap.yourGame') }), 0),
        up(h('div', { class: 'wr-pop' }, gameIcon(g0, 168)), 1), up(h('h2', { class: 'wr-l', text: g0.game_name }), 2),
        up(h('p', { text: [duration(g0.seconds), g0.sessions ? t('wrap.sessionsN', { n: g0.sessions }) : null, t('wrap.share', { p: share })].filter(Boolean).join(' · ') }), 3));
    }
    case 'top5': {
      const max = Math.max(1, w.games[0].seconds);
      return h('div', { class: 'wr-c wr-left' }, up(h('h2', { class: 'wr-l', text: t('wrap.top5') }), 0),
        h('div', { class: 'wr-list' }, w.games.map((g, i) => up(h('div', { class: 'wr-row' },
          h('span', { class: `wr-rank ${i === 0 ? 'gold' : ''}`, text: String(i + 1) }), gameIcon(g, 48),
          h('div', { class: 'wr-row-main' }, h('div', { class: 'wr-row-top' }, h('strong', { text: g.game_name }), h('span', { text: duration(g.seconds) })),
            h('div', { class: 'wr-bar' }, h('span', { style: { width: `${Math.max(4, (g.seconds / max) * 100)}%`, animationDelay: `${300 + i * 110}ms` } })))), i + 1))),
        w.games_count > w.games.length ? h('p', { class: 'wr-dim small center', text: t('wrap.more', { n: w.games_count - w.games.length }) }) : null);
    }
    case 'rhythm': {
      const hmax = Math.max(1, ...w.hours);
      const peak = w.hours.indexOf(Math.max(...w.hours));
      const wpeak = w.weekdays.indexOf(Math.max(...w.weekdays));
      const part = peak >= 5 && peak < 12 ? 'morning' : peak >= 12 && peak < 18 ? 'afternoon' : peak >= 18 && peak < 22 ? 'evening' : 'night';
      const day = new Date(2024, 0, 1 + wpeak).toLocaleDateString(LANG, { weekday: 'long' });   // 1/1/2024 è lunedì
      return h('div', { class: 'wr-c' }, up(h('p', { class: 'wr-dim wr-m', text: t('wrap.rhythm') }), 0),
        up(h('h2', { class: 'wr-l', text: t(`wrap.part.${part}`) }), 1),
        up(h('p', { text: t('wrap.peak', { from: String(peak).padStart(2, '0'), to: String((peak + 1) % 24).padStart(2, '0'), day }) }), 2),
        up(h('div', { class: 'wr-hours' }, w.hours.map((s, i) => h('span', { class: i === peak ? 'peak' : '', style: { height: `${s ? Math.max(3, (s / hmax) * 100) : 0}%`, animationDelay: `${300 + i * 25}ms` }, title: `${i}:00` }))), 3),
        h('div', { class: 'wr-hlab' }, [0, 6, 12, 18, 23].map((x) => h('span', { text: String(x).padStart(2, '0') }))));
    }
    case 'records': {
      const cards = [];
      if (w.longest) cards.push([icon('fire', 26), duration(w.longest.seconds), t('wrap.longest', { g: w.longest.game_name })]);
      if (w.streak >= 2) cards.push([icon('clock', 26), String(w.streak), t('wrap.streak')]);
      if (w.new_games) cards.push([icon('sparkle', 26), String(w.new_games), t(w.new_games === 1 ? 'wrap.new1' : 'wrap.newN')]);
      return h('div', { class: 'wr-c' }, up(h('h2', { class: 'wr-l', text: t('wrap.records') }), 0),
        h('div', { class: 'wr-cards' }, cards.map(([ic, big, sub], i) => up(h('div', { class: 'wr-card' }, ic, h('div', { class: 'wr-big', text: big }), h('p', { text: sub })), i + 1))));
    }
    case 'trophies': {
      const tr = w.trophies; const big = h('div', { class: 'wr-huge' });
      countUp(big, tr.count, num);
      const G = [['plat', tr.p], ['gold', tr.g], ['silver', tr.s], ['bronze', tr.b]];
      const r = tr.rarest;
      return h('div', { class: 'wr-c' }, up(h('p', { class: 'wr-dim wr-m', text: t('wrap.trophies') }), 0), up(big, 1),
        up(h('div', { class: 'wr-cups' }, G.map(([c, n]) => h('div', {}, h('span', { class: `wr-cup ${c}` }, icon('trophy', 22)), h('strong', { text: String(n) })))), 2),
        r ? up(h('div', { class: 'wr-rare' }, gameIcon({ game_id: '', game_name: r.game, game_icon: r.icon_media }, 56),
          h('div', {}, h('small', { class: 'wr-dim', text: r.pct !== null && r.pct !== undefined ? t('wrap.rarest', { p: r.pct }) : t('wrap.precious') }),
            h('strong', { text: r.name || t('wrap.hidden') }), h('small', { text: r.game || '' }))), 3) : null);
    }
    case 'together': return h('div', { class: 'wr-c' }, up(h('h2', { class: 'wr-l', text: t('wrap.together') }), 0),
      h('div', { class: 'wr-cards' }, w.together.map((f, i) => up(h('div', { class: 'wr-card' }, avatar(f.user, 72), h('strong', { text: f.user.online_id }),
        h('div', { class: 'wr-big', text: duration(f.seconds) }), h('p', { class: 'wr-dim', text: f.game_name || '' })), i + 1))),
      up(h('p', { class: 'wr-dim small', text: t('wrap.togetherNote') }), 4));
    case 'community': {
      const big = h('div', { class: 'wr-huge' });
      countUp(big, w.percentile, (v) => `${v}%`);
      const tag = w.percentile >= 90 ? 'wrap.c90' : w.percentile >= 50 ? 'wrap.c50' : 'wrap.c0';
      return h('div', { class: 'wr-c' }, up(h('p', { class: 'wr-dim wr-m', text: t('wrap.community') }), 0), up(big, 1),
        up(h('p', { class: 'wr-m', text: t('wrap.more_than', { p: w.percentile, n: w.players }) }), 2), up(h('p', { text: t(tag) }), 3),
        up(h('p', { class: 'wr-dim small', text: t('wrap.anon') }), 4));
    }
    case 'persona': {
      const p = PERSONA[w.persona] || PERSONA.casual;
      const cells = [[duration(w.total_seconds), t('wrap.ofPlay')], [g0 ? g0.game_name : '—', t('wrap.mostPlayed')],
        w.days_active ? [String(w.days_active), t('wrap.days')] : [String(w.games_count), t('wrap.games')], [String(w.trophies.count), t('wrap.trophiesL')]];
      return h('div', { class: 'wr-c' }, up(h('p', { class: 'wr-dim wr-m', text: t('wrap.profile') }), 0),
        up(h('div', { class: 'wr-badge' }, icon(p.ic, 56)), 1), up(h('h2', { class: 'wr-xl', text: t(`wrap.p.${w.persona}`) }), 2),
        up(h('p', { text: t(`wrap.pd.${w.persona}`) }), 3),
        up(h('div', { class: 'wr-grid' }, cells.map(([v, k]) => h('div', {}, h('strong', { text: v }), h('small', { text: k })))), 4),
        up(h('p', { class: 'wr-dim small', text: `${meName} · ${rangeText(w, opt.period)} · Omega` }), 5));
    }
    default: return h('div');
  }
}

// ---------------------------------------------- immagine da condividere --
async function shareImage(w, opt) {
  const W = 1080; const H = 1920;
  const cv = document.createElement('canvas'); cv.width = W; cv.height = H;
  const c = cv.getContext('2d');
  const p = PERSONA[w.persona] || PERSONA.casual;
  const g = c.createLinearGradient(0, 0, 0, H); g.addColorStop(0, p.c[0]); g.addColorStop(1, p.c[1]);
  c.fillStyle = g; c.fillRect(0, 0, W, H);
  const glow = (x, y, r, col) => { const rg = c.createRadialGradient(x, y, 0, x, y, r); rg.addColorStop(0, col); rg.addColorStop(1, 'rgba(255,255,255,0)'); c.fillStyle = rg; c.fillRect(0, 0, W, H); };
  glow(220, 380, 620, 'rgba(255,255,255,.18)'); glow(900, 1500, 700, 'rgba(255,120,200,.14)');
  const font = (wt, px) => `${wt} ${px}px -apple-system, "Segoe UI", Roboto, Arial, sans-serif`;
  const text = (s, x, y, px, wt = 400, col = '#fff', align = 'center', maxW = W - 160) => {
    c.font = font(wt, px); c.fillStyle = col; c.textAlign = align;
    let size = px; while (c.measureText(s).width > maxW && size > 18) { size -= 2; c.font = font(wt, size); }
    c.fillText(s, x, y);
  };
  text(t(`wrap.title.${opt.value}`), W / 2, 210, 64, 300, 'rgba(255,255,255,.85)');
  text(rangeText(w, opt.period), W / 2, 280, 40, 400, 'rgba(255,255,255,.6)');
  text(t('wrap.profile'), W / 2, 520, 42, 400, 'rgba(255,255,255,.7)');
  text(t(`wrap.p.${w.persona}`), W / 2, 650, 120, 700);
  text(t(`wrap.pd.${w.persona}`), W / 2, 740, 40, 400, 'rgba(255,255,255,.85)');
  const cells = [[duration(w.total_seconds), t('wrap.ofPlay')], [w.games[0] ? w.games[0].game_name : '—', t('wrap.mostPlayed')],
    w.days_active ? [String(w.days_active), t('wrap.days')] : [String(w.games_count), t('wrap.games')], [String(w.trophies.count), t('wrap.trophiesL')]];
  cells.forEach(([v, k], i) => {
    const x = i % 2 ? W * 0.73 : W * 0.27; const y = 980 + Math.floor(i / 2) * 300;
    c.fillStyle = 'rgba(255,255,255,.10)';
    c.beginPath(); c.roundRect ? c.roundRect(x - 230, y - 120, 460, 250, 40) : c.rect(x - 230, y - 120, 460, 250); c.fill();
    text(v, x, y + 10, i === 1 ? 52 : 84, 600, '#fff', 'center', 420);
    text(k, x, y + 80, 34, 400, 'rgba(255,255,255,.7)', 'center', 420);
  });
  text(auth.onlineId || '', W / 2, 1700, 44, 600);
  text('Omega', W / 2, 1780, 36, 400, 'rgba(255,255,255,.6)');
  const blob = await new Promise((r) => cv.toBlob(r, 'image/png'));
  const file = new File([blob], 'omega-riepilogo.png', { type: 'image/png' });
  try {
    if (navigator.canShare && navigator.canShare({ files: [file] })) { await navigator.share({ files: [file], title: t('wrap.title.' + opt.value) }); return; }
  } catch (e) { if (e && e.name === 'AbortError') return; }
  const a = h('a', { href: URL.createObjectURL(blob), download: 'omega-riepilogo.png' });
  document.body.appendChild(a); a.click(); a.remove();
  setTimeout(() => URL.revokeObjectURL(a.href), 10000);
}

// ------------------------------------------------------------------ vista --
export async function wrap(ctx) {
  const { page } = ctx;
  let opt = PERIODS.find((p) => p.value === pref('wrapPeriod', 'w1')) || PERIODS[1];
  let w = null; let slides = []; let cur = 0; let timer = null; let paused = false;
  const stage = h('div', { class: 'wr-stage' });
  const actions = h('div', { class: 'wr-actions' });
  const seg = segmented(PERIODS.map((p) => ({ value: p.value, label: t(`wrap.opt.${p.value}`) })), opt.value, (v) => {
    opt = PERIODS.find((p) => p.value === v); setPref('wrapPeriod', v); load();
  }, { small: true, label: t('wrap.period') });
  add(page, h('div', { class: 'wr-wrap' }, h('div', { class: 'wr-seg' }, seg), stage, actions));

  function stop() { if (timer) { clearTimeout(timer); timer = null; } }
  function schedule() { stop(); if (!paused && cur < slides.length - 1 && !reduced()) timer = setTimeout(() => show(cur + 1), SLIDE_MS); }
  function show(i) {
    if (!stage.isConnected && stage.childNodes.length) { stop(); return; }
    if (!w || i < 0 || i >= slides.length) return;
    cur = i;
    const kind = slides[cur];
    const col = kind === 'persona' ? (PERSONA[w.persona] || PERSONA.casual).c : SLIDE_COL[kind];
    stage.style.setProperty('--c1', col[0]); stage.style.setProperty('--c2', col[1]);
    const bars = h('div', { class: 'wr-bars' }, slides.map((_, k) => h('span', { class: k < cur ? 'done' : k === cur ? 'now' : '' }, h('i', { style: k === cur && cur < slides.length - 1 && !reduced() ? { animationDuration: `${SLIDE_MS}ms`, animationPlayState: paused ? 'paused' : 'running' } : {} }))));
    replace(stage, bars, h('div', { class: 'wr-label', text: t(`wrap.opt.${opt.value}`) }), renderSlide(kind, w, opt),
      h('button', { class: 'wr-tap l', type: 'button', 'aria-label': t('common.back'), onclick: () => show(cur - 1) }),
      h('button', { class: 'wr-tap r', type: 'button', 'aria-label': t('wrap.next'), onclick: () => show(cur + 1) }));
    schedule();
  }
  async function load() {
    stop(); w = null;
    replace(stage, h('div', { class: 'wr-c' }, h('div', { class: 'spinner' })));
    replace(actions);
    try {
      w = await get(`/wrap?period=${opt.period}&back=${opt.back}&tz=${-new Date().getTimezoneOffset()}`);
    } catch (e) { replace(stage, errorState(e, load)); return; }
    slides = slidesOf(w);
    show(0);
    if (w.total_seconds) {
      replace(actions,
        btn(t('wrap.shareImg'), { icon: 'upload', kind: 'grad', onclick: () => shareImage(w, opt).catch((e) => toast(errorText(e), { kind: 'error' })) }),
        btn(t('wrap.post'), { icon: 'edit', kind: 'soft', onclick: postWall }));
    }
  }
  async function postWall() {
    if (!(await confirmDialog({ title: t('wrap.postQ'), text: t('wrap.postText'), ok: t('wrap.postOk') }))) return;
    const text = t('wrap.postBody', { period: t(`wrap.opt.${opt.value}`), d: duration(w.total_seconds), g: w.games[0] ? w.games[0].game_name : '—', p: t(`wrap.p.${w.persona}`), pd: t(`wrap.pd.${w.persona}`) });
    try { await post('/posts', { text }); toast(t('wrap.posted'), { kind: 'ok' }); } catch (e) { toast(errorText(e), { kind: 'error' }); }
  }
  // tieni premuto per fermare, frecce da tastiera
  stage.addEventListener('pointerdown', () => { paused = true; stop(); stage.classList.add('paused'); });
  const resume = () => { if (paused) { paused = false; stage.classList.remove('paused'); schedule(); } };
  stage.addEventListener('pointerup', resume); stage.addEventListener('pointerleave', resume);
  const onKey = (e) => {
    if (!stage.isConnected) { stop(); document.removeEventListener('keydown', onKey); return; }   // pagina lasciata
    if (e.key === 'ArrowRight') show(cur + 1); else if (e.key === 'ArrowLeft') show(cur - 1);
  };
  document.addEventListener('keydown', onKey);
  ctx.onRefresh(load);
  await load();
}
