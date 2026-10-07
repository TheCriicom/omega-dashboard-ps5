// Mattoni dell'interfaccia: DOM sicuro (mai innerHTML con testi degli utenti),
// icone, avatar, immagini protette, toast, fogli dal basso, dialoghi, stati.
import { t, LANG } from './i18n.js';
import { imageUrl, cachedImage } from './api.js';

// ---------------------------------------------------------------- DOM --
export function h(tag, props, ...children) {
  const el = document.createElement(tag);
  if (props) {
    for (const [k, v] of Object.entries(props)) {
      if (v === undefined || v === null || v === false) continue;
      if (k === 'class') el.className = v;
      else if (k === 'text') el.textContent = v;
      else if (k === 'style' && typeof v === 'object') Object.assign(el.style, v);
      else if (k === 'dataset') Object.assign(el.dataset, v);
      else if (k.startsWith('on') && typeof v === 'function') el.addEventListener(k.slice(2), v);
      else if (k === 'value') el.value = v;
      else if (k === 'checked' || k === 'disabled' || k === 'selected' || k === 'hidden') el[k] = !!v;
      else el.setAttribute(k, v === true ? '' : String(v));
    }
  }
  append(el, children);
  return el;
}
function append(el, children) {
  for (const c of children) {
    if (c === null || c === undefined || c === false) continue;
    if (Array.isArray(c)) append(el, c);
    else if (c instanceof Node) el.appendChild(c);
    else el.appendChild(document.createTextNode(String(c)));
  }
}
export function add(el, ...children) { append(el, children); return el; }
export function clear(el) { while (el.firstChild) el.removeChild(el.firstChild); return el; }
export function replace(el, ...children) { clear(el); append(el, children); return el; }

const SVGNS = 'http://www.w3.org/2000/svg';
export function icon(name, size = 22, cls = '') {
  const s = document.createElementNS(SVGNS, 'svg');
  s.setAttribute('class', `ic ${cls}`.trim());
  s.setAttribute('width', size); s.setAttribute('height', size);
  s.setAttribute('aria-hidden', 'true'); s.setAttribute('focusable', 'false');
  const u = document.createElementNS(SVGNS, 'use');
  u.setAttribute('href', `#i-${name}`);
  s.appendChild(u);
  return s;
}
export function mark(size = 28) { return icon('mark', size, 'mark'); }

// Bottone con icona, etichetta accessibile
export function iconBtn(name, label, onclick, cls = '') {
  return h('button', { class: `icon-btn ${cls}`, type: 'button', 'aria-label': label, title: label, onclick }, icon(name, 22));
}
export function btn(label, opts = {}) {
  const { icon: ic, kind = 'primary', onclick, type = 'button', size, block, disabled, cls = '' } = opts;
  return h('button', { class: `btn btn-${kind} ${size ? `btn-${size}` : ''} ${block ? 'btn-block' : ''} ${cls}`, type, onclick, disabled },
    ic ? icon(ic, size === 'sm' ? 18 : 20) : null, h('span', { text: label }));
}
// esegue un'azione asincrona con il bottone in attesa
export async function busy(button, fn) {
  if (!button || button.disabled) return;
  button.disabled = true; button.classList.add('is-busy');
  try { return await fn(); } finally { button.disabled = false; button.classList.remove('is-busy'); }
}

// ------------------------------------------------------------- avatar --
const AV = [
  ['#0070f3', '#00c8ff'], ['#8c3cff', '#ff50c8'], ['#ff5a3c', '#ffbe3c'], ['#14b478', '#8cf05a'],
  ['#e6285a', '#ff826e'], ['#283cc8', '#785aff'], ['#0096a0', '#50e6d2'], ['#ff8c00', '#ffdc5a'],
  ['#5a5a6e', '#aab4c8'], ['#c81ea0', '#7828ff'], ['#0a785a', '#00c8a0'], ['#b43228', '#fa6e3c'],
  ['#3c82ff', '#96d2ff'], ['#6e28c8', '#28aaff'], ['#dcaa00', '#ff6e3c'], ['#1e1e28', '#5a6482'],
];
export function avatarColors(n) { return AV[(Number(n) || 0) & 15]; }

export function avatar(user, size = 44, opts = {}) {
  const u = user || {};
  const oid = u.online_id || u.oid || '?';
  const [a, b] = avatarColors(u.avatar);
  const el = h('span', {
    class: `avatar ${opts.cls || ''}`, role: 'img', 'aria-label': oid,
    style: { width: `${size}px`, height: `${size}px`, fontSize: `${Math.round(size * 0.44)}px`, background: `linear-gradient(135deg, ${a}, ${b})` },
  }, h('span', { class: 'avatar-ini', text: oid.charAt(0).toUpperCase() }));
  if (u.avatar_media) lazyImage(el, `/media/${u.avatar_media}/1`, { cls: 'avatar-img', alt: '' });
  if (opts.presence) el.appendChild(h('span', { class: `pdot p-${presenceKind(opts.presence)}` }));
  return el;
}

// ------------------------------------------------- immagini protette --
const io = 'IntersectionObserver' in window ? new IntersectionObserver((entries) => {
  for (const e of entries) {
    if (!e.isIntersecting) continue;
    io.unobserve(e.target);
    loadInto(e.target);
  }
}, { rootMargin: '300px' }) : null;

function loadInto(holder) {
  const path = holder.dataset.src;
  imageUrl(path).then((u) => {
    if (!u || !holder.isConnected && !holder.dataset.keep) { if (!u) holder.classList.add('img-failed'); return; }
    const img = holder.querySelector('img.lz');
    if (!img) return;
    img.onload = () => holder.classList.add('img-ready');
    img.src = u;
    if (holder.classList.contains('fit')) holder.style.setProperty('--bg', `url("${u}")`);
  });
}

// Mette in `holder` un'immagine dell'API, caricata quando diventa visibile
export function lazyImage(holder, path, { cls = '', alt = '' } = {}) {
  const img = h('img', { class: `lz ${cls}`, alt, decoding: 'async', draggable: 'false' });
  holder.appendChild(img);
  holder.dataset.src = path;
  const hit = cachedImage(path);
  if (hit) { img.src = hit; holder.classList.add('img-ready'); if (holder.classList.contains('fit')) holder.style.setProperty('--bg', `url("${hit}")`); return holder; }
  if (hit === null) { holder.classList.add('img-failed'); return holder; }
  if (io) io.observe(holder); else loadInto(holder);
  return holder;
}
export function apiImg(path, cls = '', alt = '') {
  const holder = h('div', { class: `pimg ${cls}` });
  return lazyImage(holder, path, { alt });
}
export function gameIcon(row, size = 44) {
  const name = row.game_name || row.name || row.game_id || '?';
  const el = h('span', { class: 'game-ic', style: { width: `${size}px`, height: `${size}px` }, 'aria-hidden': 'true' },
    h('span', { class: 'game-ic-ph', text: initials(name) }));
  if (row.game_icon) lazyImage(el, `/media/${row.game_icon}/1`, { alt: '' });
  return el;
}
function initials(s) {
  const w = String(s).replace(/[^\p{L}\p{N} ]/gu, ' ').trim().split(/\s+/).filter(Boolean);
  return ((w[0] || '?').charAt(0) + (w[1] ? w[1].charAt(0) : '')).toUpperCase();
}

// ----------------------------------------------------------- presenza --
export function presenceKind(p) {
  if (!p) return 'offline';
  if (p.status === 'offline' || !p.status) return 'offline';
  if (p.game_id || p.game_name) return 'playing';
  if (p.status === 'away') return 'away';
  if (p.status === 'dnd') return 'dnd';
  return 'online';
}
export function presenceText(p, { long = false } = {}) {
  const k = presenceKind(p);
  if (k === 'playing') return long ? t('presence.playingGame', { game: p.game_name || p.game_id }) : (p.game_name || p.game_id);
  if (k === 'offline') return p && p.last_seen ? t('presence.lastSeen', { when: relTime(p.last_seen) }) : t('presence.offline');
  return t(`presence.${k}`);
}

// --------------------------------------------------------------- tempo --
const rtf = new Intl.RelativeTimeFormat(LANG, { numeric: 'auto', style: 'short' });
export function relTime(d) {
  if (!d) return '';
  const ms = Date.parse(d);
  if (!ms) return '';
  const s = Math.round((ms - Date.now()) / 1000);
  const a = Math.abs(s);
  if (a < 45) return t('time.now');
  if (a < 3600) return rtf.format(Math.round(s / 60), 'minute');
  if (a < 86400) return rtf.format(Math.round(s / 3600), 'hour');
  if (a < 86400 * 7) return rtf.format(Math.round(s / 86400), 'day');
  return new Date(ms).toLocaleDateString(LANG, { day: 'numeric', month: 'short', year: a > 86400 * 300 ? 'numeric' : undefined });
}
export function clockTime(d) { return new Date(d).toLocaleTimeString(LANG, { hour: '2-digit', minute: '2-digit' }); }
export function dayLabel(d) {
  const x = new Date(d), now = new Date();
  const same = (a, b) => a.toDateString() === b.toDateString();
  if (same(x, now)) return t('time.today');
  const y = new Date(now); y.setDate(now.getDate() - 1);
  if (same(x, y)) return t('time.yesterday');
  return x.toLocaleDateString(LANG, { weekday: 'long', day: 'numeric', month: 'long' });
}
export function duration(sec) {
  sec = Math.max(0, Math.round(Number(sec) || 0));
  const hrs = Math.floor(sec / 3600), min = Math.floor((sec % 3600) / 60);
  if (hrs >= 100) return t('time.h', { n: hrs.toLocaleString(LANG) });
  if (hrs > 0) return min ? t('time.hm', { h: hrs, m: min }) : t('time.h', { n: hrs });
  if (min > 0) return t('time.m', { n: min });
  return sec > 0 ? t('time.lt1m') : t('time.m', { n: 0 });
}
export function num(n) { return Number(n || 0).toLocaleString(LANG); }
export function compact(n) { return new Intl.NumberFormat(LANG, { notation: 'compact', maximumFractionDigits: 1 }).format(Number(n || 0)); }
export function bytes(n) {
  if (!n) return '';
  const u = ['B', 'KB', 'MB', 'GB']; let i = 0; let v = Number(n);
  while (v >= 1024 && i < u.length - 1) { v /= 1024; i++; }
  return `${v.toLocaleString(LANG, { maximumFractionDigits: v < 10 ? 1 : 0 })} ${u[i]}`;
}

// --------------------------------------------------------------- toast --
let toastHost = null;
export function toast(msg, opts = {}) {
  if (!toastHost) { toastHost = h('div', { class: 'toasts', role: 'status', 'aria-live': 'polite' }); document.body.appendChild(toastHost); }
  const { kind = 'info', action, onAction, icon: ic, duration: dur = 3600, user, onClick } = typeof opts === 'string' ? { kind: opts } : opts;
  const el = h('div', { class: `toast toast-${kind} ${onClick ? 'is-click' : ''}` },
    user ? avatar(user, 32) : icon(ic || (kind === 'error' ? 'alert' : kind === 'ok' ? 'check' : 'info'), 20),
    h('div', { class: 'toast-msg', text: msg }),
    action ? h('button', { class: 'toast-act', type: 'button', text: action, onclick: (e) => { e.stopPropagation(); onAction && onAction(); close(); } }) : null);
  if (onClick) el.addEventListener('click', () => { onClick(); close(); });
  toastHost.appendChild(el);
  requestAnimationFrame(() => el.classList.add('in'));
  let timer = setTimeout(close, dur);
  el.addEventListener('pointerenter', () => clearTimeout(timer));
  el.addEventListener('pointerleave', () => { timer = setTimeout(close, 1500); });
  function close() { el.classList.remove('in'); el.classList.add('out'); setTimeout(() => el.remove(), 300); }
  return close;
}

// ---------------------------------------------- fogli dal basso / modali --
const stack = [];
export function sheet(opts) {
  const { title, subtitle, content, actions, onClose, wide, cls = '' } = opts;
  const prevFocus = document.activeElement;
  const backdrop = h('div', { class: 'sheet-backdrop' });
  const body = h('div', { class: 'sheet-body' });
  const panel = h('div', { class: `sheet ${wide ? 'sheet-wide' : ''} ${cls}`, role: 'dialog', 'aria-modal': 'true', tabindex: '-1' },
    h('div', { class: 'sheet-grip', 'aria-hidden': 'true' }),
    title ? h('div', { class: 'sheet-head' },
      h('div', { class: 'sheet-titles' }, h('h2', { class: 'sheet-title', text: title }), subtitle ? h('p', { class: 'sheet-sub', text: subtitle }) : null),
      iconBtn('close', t('common.close'), () => close())) : null,
    body);
  if (title) panel.setAttribute('aria-label', title);
  if (content) append(body, [typeof content === 'function' ? content(close) : content]);
  if (actions) {
    const list = h('div', { class: 'sheet-actions' });
    for (const a of actions) {
      if (!a) continue;
      if (a.sep) { list.appendChild(h('div', { class: 'sheet-sep' })); continue; }
      list.appendChild(h('button', {
        class: `sheet-action ${a.danger ? 'is-danger' : ''} ${a.active ? 'is-active' : ''}`, type: 'button',
        onclick: async () => { close(); if (a.onClick) await a.onClick(); },
      }, a.icon ? icon(a.icon, 22) : null, h('span', { class: 'sa-text' }, h('span', { text: a.label }), a.hint ? h('small', { text: a.hint }) : null),
      a.active ? icon('check', 20, 'sa-check') : null));
    }
    body.appendChild(list);
  }
  const wrap = h('div', { class: 'sheet-wrap' }, backdrop, panel);
  document.body.appendChild(wrap);
  document.body.classList.add('has-sheet');
  requestAnimationFrame(() => { wrap.classList.add('in'); panel.focus({ preventScroll: true }); });
  backdrop.addEventListener('click', () => close());
  // trascina giù per chiudere (telefono)
  let y0 = null, dy = 0;
  const grip = panel.querySelector('.sheet-grip');
  const head = panel.querySelector('.sheet-head');
  [grip, head].filter(Boolean).forEach((g) => {
    g.addEventListener('touchstart', (e) => { y0 = e.touches[0].clientY; dy = 0; panel.style.transition = 'none'; }, { passive: true });
    g.addEventListener('touchmove', (e) => { if (y0 == null) return; dy = Math.max(0, e.touches[0].clientY - y0); panel.style.transform = `translateY(${dy}px)`; }, { passive: true });
    g.addEventListener('touchend', () => { panel.style.transition = ''; panel.style.transform = ''; if (dy > 90) close(); y0 = null; });
  });
  let closed = false;
  function close(result) {
    if (closed) return; closed = true;
    const i = stack.indexOf(entry); if (i >= 0) stack.splice(i, 1);
    wrap.classList.remove('in'); wrap.classList.add('out');
    setTimeout(() => { wrap.remove(); if (!stack.length) document.body.classList.remove('has-sheet'); }, 260);
    if (prevFocus && prevFocus.focus) prevFocus.focus({ preventScroll: true });
    if (onClose) onClose(result);
  }
  const entry = { close, panel };
  stack.push(entry);
  return { close, body, panel };
}
export function closeTopSheet() { const s = stack[stack.length - 1]; if (s) { s.close(); return true; } return false; }
export function topSheet() { return stack[stack.length - 1]; }

export function confirmDialog({ title, text, ok, danger, cancel }) {
  return new Promise((resolve) => {
    let done = false;
    const s = sheet({
      title, cls: 'sheet-confirm',
      content: (close) => h('div', { class: 'confirm' },
        text ? h('p', { class: 'muted', text }) : null,
        h('div', { class: 'row-btns' },
          btn(cancel || t('common.cancel'), { kind: 'ghost', onclick: () => close() }),
          btn(ok || t('common.ok'), { kind: danger ? 'danger' : 'primary', onclick: () => { done = true; close(); } }))),
      onClose: () => resolve(done),
    });
    return s;
  });
}

// foglio con un modulo: fields = [{name,label,type,value,placeholder,max,options,required,hint}]
export function formSheet({ title, subtitle, fields, submit, onSubmit, danger }) {
  return new Promise((resolve) => {
    let result = null;
    sheet({
      title, subtitle,
      content: (close) => {
        const form = h('form', { class: 'form', novalidate: true });
        const inputs = {};
        for (const f of fields) {
          let input;
          if (f.type === 'select') {
            input = h('select', { class: 'input', name: f.name, id: `f-${f.name}` }, f.options.map((o) => h('option', { value: o.value, selected: o.value === f.value, text: o.label })));
          } else if (f.type === 'textarea') {
            input = h('textarea', { class: 'input', name: f.name, id: `f-${f.name}`, rows: f.rows || 3, maxlength: f.max, placeholder: f.placeholder || '' });
            input.value = f.value || '';
          } else {
            input = h('input', { class: 'input', name: f.name, id: `f-${f.name}`, type: f.type || 'text', maxlength: f.max, placeholder: f.placeholder || '', autocomplete: f.autocomplete || 'off', value: f.value || '' });
          }
          inputs[f.name] = input;
          form.appendChild(h('label', { class: 'field', for: `f-${f.name}` }, h('span', { class: 'field-label', text: f.label }), input, f.hint ? h('small', { class: 'field-hint', text: f.hint }) : null));
        }
        const err = h('p', { class: 'form-error', role: 'alert' });
        const go = btn(submit || t('common.save'), { kind: danger ? 'danger' : 'primary', type: 'submit', block: true });
        form.append(err, go);
        form.addEventListener('submit', async (e) => {
          e.preventDefault();
          const vals = {};
          for (const [k, el] of Object.entries(inputs)) vals[k] = el.value;
          for (const f of fields) if (f.required && !String(vals[f.name] || '').trim()) { err.textContent = t('form.required', { field: f.label }); inputs[f.name].focus(); return; }
          err.textContent = '';
          await busy(go, async () => {
            try { result = onSubmit ? await onSubmit(vals) : vals; if (result === false) return; close(); }
            catch (ex) { err.textContent = errorText(ex); }
          });
        });
        setTimeout(() => { const first = form.querySelector('input,textarea,select'); if (first && matchMedia('(pointer: fine)').matches) first.focus(); }, 280);
        return form;
      },
      onClose: () => resolve(result),
    });
  });
}

// ------------------------------------------------------------- stati --
export function skeletonList(n = 5, kind = 'row') {
  const wrap = h('div', { class: `skel-list skel-${kind}`, 'aria-busy': 'true', 'aria-label': t('common.loading') });
  for (let i = 0; i < n; i++) {
    if (kind === 'card') wrap.appendChild(h('div', { class: 'skel skel-card' }));
    else if (kind === 'tile') wrap.appendChild(h('div', { class: 'skel-tile' }, h('div', { class: 'skel skel-cover' }), h('div', { class: 'skel skel-line w70' }), h('div', { class: 'skel skel-line w40' })));
    else wrap.appendChild(h('div', { class: 'skel-row' }, h('div', { class: 'skel skel-circle' }), h('div', { class: 'skel-lines' }, h('div', { class: 'skel skel-line w60' }), h('div', { class: 'skel skel-line w35' }))));
  }
  return wrap;
}
export function empty(ic, title, text, action) {
  return h('div', { class: 'empty' },
    h('div', { class: 'empty-ic' }, icon(ic, 30)),
    h('h3', { text: title }), text ? h('p', { text }) : null,
    action || null);
}
export function errorText(err) {
  if (!err) return t('err.generic');
  const code = err.code || '';
  const k = `err.${code}`;
  const s = t(k);
  if (s !== k) return err.retryAfter ? `${s} ${t('err.retryIn', { s: err.retryAfter })}` : s;
  if (err.status === 0) return t('err.network');
  if (err.status >= 500) return t('err.server');
  return err.detail || t('err.generic');
}
export function errorState(err, retry) {
  return h('div', { class: 'empty is-error' },
    h('div', { class: 'empty-ic' }, icon(err && err.status === 0 ? 'wifi' : 'alert', 30)),
    h('h3', { text: err && err.status === 0 ? t('err.offlineTitle') : t('err.title') }),
    h('p', { text: errorText(err) }),
    retry ? btn(t('common.retry'), { kind: 'ghost', icon: 'refresh', onclick: retry }) : null);
}

// ------------------------------------------------ controlli di forma --
export function segmented(options, value, onChange, { label, small } = {}) {
  const wrap = h('div', { class: `seg ${small ? 'seg-sm' : ''}`, role: 'radiogroup', 'aria-label': label || '' });
  const buttons = options.map((o) => {
    const b = h('button', { type: 'button', role: 'radio', class: 'seg-btn', 'aria-checked': String(o.value === value), dataset: { v: o.value } },
      o.icon ? icon(o.icon, 16) : null, h('span', { text: o.label }));
    b.addEventListener('click', () => { if (wrap.dataset.value === o.value) return; set(o.value); onChange(o.value); });
    return b;
  });
  function set(v) { wrap.dataset.value = v; buttons.forEach((b) => b.setAttribute('aria-checked', String(b.dataset.v === v))); }
  wrap.append(...buttons);
  set(value);
  wrap.addEventListener('keydown', (e) => {
    if (e.key !== 'ArrowRight' && e.key !== 'ArrowLeft') return;
    const i = options.findIndex((o) => o.value === wrap.dataset.value);
    const n = options[(i + (e.key === 'ArrowRight' ? 1 : -1) + options.length) % options.length];
    set(n.value); onChange(n.value); buttons.find((b) => b.dataset.v === n.value).focus();
  });
  wrap.set = set;
  return wrap;
}
export function toggle(checked, onChange, label) {
  const input = h('input', { type: 'checkbox', class: 'switch-input', role: 'switch', checked, 'aria-label': label || '' });
  input.addEventListener('change', () => onChange(input.checked, input));
  return h('span', { class: 'switch' }, input, h('span', { class: 'switch-track', 'aria-hidden': 'true' }, h('span', { class: 'switch-thumb' })));
}
export function tabs(items, value, onChange) {
  const wrap = h('div', { class: 'tabs', role: 'tablist' });
  const btns = items.map((it) => {
    const b = h('button', { class: 'tab', role: 'tab', type: 'button', 'aria-selected': String(it.value === value), dataset: { v: it.value } },
      it.icon ? icon(it.icon, 18) : null, h('span', { text: it.label }), it.badge ? h('span', { class: 'badge', text: String(it.badge) }) : null);
    b.addEventListener('click', () => { btns.forEach((x) => x.setAttribute('aria-selected', String(x === b))); onChange(it.value); b.scrollIntoView({ block: 'nearest', inline: 'center', behavior: 'smooth' }); });
    return b;
  });
  wrap.append(...btns);
  return wrap;
}

// riga elenco (stile impostazioni)
export function listRow({ icon: ic, iconCls, lead, title, sub, trail, onClick, href, danger, chevron = !!(onClick || href), cls = '' }) {
  const inner = [
    lead || (ic ? h('span', { class: `lr-ic ${iconCls || ''}` }, icon(ic, 20)) : null),
    h('span', { class: 'lr-main' }, h('span', { class: 'lr-title', text: title }), sub ? (sub instanceof Node ? sub : h('span', { class: 'lr-sub', text: sub })) : null),
    trail || null,
    chevron ? icon('chev', 18, 'lr-chev') : null,
  ];
  if (href) return h('a', { class: `lrow ${danger ? 'is-danger' : ''} ${cls}`, href }, inner);
  if (onClick) return h('button', { class: `lrow ${danger ? 'is-danger' : ''} ${cls}`, type: 'button', onclick: onClick }, inner);
  return h('div', { class: `lrow ${cls}` }, inner);
}
export function section(title, ...children) {
  const opts = (children[0] && children[0].action !== undefined && !(children[0] instanceof Node)) ? children.shift() : {};
  return h('section', { class: 'sec' },
    title ? h('div', { class: 'sec-head' }, h('h2', { class: 'sec-title', text: title }), opts.action || null) : null,
    ...children);
}
export function card(...children) { return h('div', { class: 'card' }, ...children); }
export function badge(n) { return n > 0 ? h('span', { class: 'badge', text: n > 99 ? '99+' : String(n) }) : null; }

export function stars(value, size = 14) {
  const wrap = h('span', { class: 'stars', 'aria-label': t('store.ratingAria', { n: Number(value || 0).toFixed(1) }) });
  for (let i = 1; i <= 5; i++) wrap.appendChild(icon(value >= i - 0.25 ? 'starf' : 'star', size, value >= i - 0.25 ? 'on' : ''));
  return wrap;
}

// testo con link cliccabili (sicuro: solo nodi di testo e <a>)
export function richText(s, cls = 'rich') {
  const el = h('p', { class: cls });
  const re = /(https?:\/\/[^\s<>"']+)/g;
  let last = 0; let m;
  const str = String(s || '');
  while ((m = re.exec(str))) {
    if (m.index > last) el.appendChild(document.createTextNode(str.slice(last, m.index)));
    el.appendChild(h('a', { href: m[1], target: '_blank', rel: 'noopener noreferrer nofollow', text: m[1].length > 48 ? `${m[1].slice(0, 45)}…` : m[1] }));
    last = m.index + m[1].length;
  }
  if (last < str.length) el.appendChild(document.createTextNode(str.slice(last)));
  return el;
}

// textarea che cresce con il testo
export function autoGrow(ta, max = 160) {
  const fit = () => { ta.style.height = 'auto'; ta.style.height = `${Math.min(max, ta.scrollHeight)}px`; };
  ta.addEventListener('input', fit);
  requestAnimationFrame(fit);
  return fit;
}

export function vibrate(ms = 8) { try { if (navigator.vibrate) navigator.vibrate(ms); } catch { /* niente */ } }
