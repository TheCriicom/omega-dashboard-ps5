// Componenti condivisi tra le viste: righe utente, post, app, notizie,
// console, fogli per stato / segnalazioni / scelta di un amico.
import { t } from './i18n.js';
import { get, post, del, E } from './api.js';
import { add,
  h, icon, avatar, apiImg, gameIcon, presenceKind, presenceText, relTime, sheet, toast, errorText,
  formSheet, confirmDialog, btn, busy, stars, compact, richText, autoGrow, empty, skeletonList, vibrate, iconBtn, replace,
} from './ui.js';
import { state, emit } from './state.js';
import { go, userPath, chatPath, appPath } from './nav.js';

// ------------------------------------------------------------- utenti --
export function presenceSub(p, statusMessage) {
  const k = presenceKind(p);
  const txt = presenceText(p, { long: true });
  if (k === 'playing') return h('span', { class: 'lr-sub p-playing-t' }, icon('gamepad', 14), h('span', { class: 'ell', text: txt }));
  return h('span', { class: 'lr-sub', text: statusMessage ? `${txt} · ${statusMessage}` : txt });
}

export function userRow(u, { sub, trail, onClick, presence = true } = {}) {
  const p = u.presence;
  return h('a', { class: 'lrow', href: `#${userPath(u.online_id)}`, onclick: onClick ? (e) => { e.preventDefault(); onClick(u); } : null },
    avatar(u, 46, { presence: presence && p ? p : null }),
    h('span', { class: 'lr-main' }, h('span', { class: 'lr-title', text: u.online_id }),
      sub !== undefined ? (sub instanceof Node ? sub : h('span', { class: 'lr-sub', text: sub })) : (p ? presenceSub(p, u.status_message) : null)),
    trail ? h('span', { class: 'lr-trail', onclick: (e) => { e.preventDefault(); e.stopPropagation(); } }, trail) : icon('chev', 18, 'lr-chev'));
}

export async function friendRequest(oid) {
  const r = await post('/friends/request', { online_id: oid });
  if (r.result === 'accepted') toast(t('friends.nowFriends', { name: oid }), { kind: 'ok' });
  else if (r.result === 'already_friends') toast(t('friends.already', { name: oid }));
  else toast(t('friends.requestSent', { name: oid }), { kind: 'ok' });
  emit('resync');
  return r;
}

const USER_REASONS = ['spam', 'molestie', 'contenuto_offensivo', 'impersonificazione', 'altro'];
const CONTENT_REASONS = ['spam', 'contenuto_offensivo', 'molestie', 'altro'];
const STORE_REASONS = ['pirateria', 'malware', 'contenuto_offensivo', 'spam', 'link_rotto', 'altro'];

// segnalazione: kind = user | post | comment | store | store_comment
export function reportSheet(kind, path, extra = {}) {
  const reasons = kind === 'user' ? USER_REASONS : kind.startsWith('store') ? STORE_REASONS : CONTENT_REASONS;
  return formSheet({
    title: t('report.title'), subtitle: t(`report.sub.${kind}`), submit: t('report.send'), danger: true,
    fields: [
      { name: 'reason', label: t('report.reason'), type: 'select', value: reasons[0], options: reasons.map((r) => ({ value: r, label: t(`reason.${r}`) })) },
      { name: 'note', label: t('report.note'), type: 'textarea', max: 500, placeholder: t('report.notePh') },
    ],
    onSubmit: async (v) => {
      await post(path, { reason: v.reason, note: v.note || undefined, ...extra });
      toast(t('report.thanks'), { kind: 'ok', icon: 'shield' });
    },
  });
}

// azioni su un utente (foglio): messaggio, party, amicizia, preferiti, blocco, segnalazione
export function userActionsSheet(u, relation, after) {
  const oid = u.online_id;
  const isFriend = relation === 'friend';
  sheet({
    title: oid,
    actions: [
      { icon: 'chat', label: t('user.message'), onClick: () => go(chatPath(oid)) },
      isFriend ? { icon: 'party', label: t('user.inviteParty'), onClick: () => inviteToParty(oid) } : null,
      isFriend ? { icon: 'gamepad', label: t('user.inviteGame'), onClick: () => inviteToGame([oid]) } : null,
      { sep: true },
      relation === 'none' ? { icon: 'useradd', label: t('user.add'), onClick: async () => { try { await friendRequest(oid); after && after(); } catch (e) { toast(errorText(e), { kind: 'error' }); } } } : null,
      relation === 'incoming' ? { icon: 'usercheck', label: t('friends.accept'), onClick: async () => { await acceptRequest(oid); after && after(); } } : null,
      relation === 'outgoing' ? { icon: 'userx', label: t('friends.cancelRequest'), onClick: async () => { await removeFriend(oid, true); after && after(); } } : null,
      isFriend ? { icon: 'userx', label: t('user.remove'), danger: true, onClick: async () => { if (await removeFriend(oid)) after && after(); } } : null,
      relation === 'blocked'
        ? { icon: 'ban', label: t('user.unblock'), onClick: async () => { try { await del(`/users/${E(oid)}/block`); toast(t('user.unblocked', { name: oid }), { kind: 'ok' }); after && after(); } catch (e) { toast(errorText(e), { kind: 'error' }); } } }
        : { icon: 'ban', label: t('user.block'), danger: true, onClick: async () => { if (await blockUser(oid)) after && after(); } },
      { icon: 'flag', label: t('user.report'), danger: true, onClick: () => reportSheet('user', `/users/${E(oid)}/report`) },
    ],
  });
}

export async function acceptRequest(oid) {
  try { await post('/friends/accept', { online_id: oid }); toast(t('friends.nowFriends', { name: oid }), { kind: 'ok' }); vibrate(12); emit('resync'); return true; }
  catch (e) { toast(errorText(e), { kind: 'error' }); return false; }
}
export async function declineRequest(oid) {
  try { await post('/friends/decline', { online_id: oid }); emit('resync'); return true; }
  catch (e) { toast(errorText(e), { kind: 'error' }); return false; }
}
export async function removeFriend(oid, isRequest = false) {
  if (!isRequest && !(await confirmDialog({ title: t('user.removeQ', { name: oid }), text: t('user.removeText'), ok: t('user.remove'), danger: true }))) return false;
  try { await del(`/friends/${E(oid)}`); toast(isRequest ? t('friends.requestCancelled') : t('user.removed', { name: oid })); emit('resync'); return true; }
  catch (e) { toast(errorText(e), { kind: 'error' }); return false; }
}
export async function blockUser(oid) {
  if (!(await confirmDialog({ title: t('user.blockQ', { name: oid }), text: t('user.blockText'), ok: t('user.block'), danger: true }))) return false;
  try { await post(`/users/${E(oid)}/block`); toast(t('user.blocked', { name: oid }), { kind: 'ok' }); emit('resync'); return true; }
  catch (e) { toast(errorText(e), { kind: 'error' }); return false; }
}

export async function inviteToParty(oid) {
  try {
    if (!state.party) {
      await post('/party', {});
      toast(t('party.created'), { kind: 'ok' });
    }
    await post('/party/invite', { online_id: oid });
    toast(t('party.invited', { name: oid }), { kind: 'ok', icon: 'party', action: t('party.open'), onAction: () => go('/party') });
    emit('resync');
  } catch (e) { toast(errorText(e), { kind: 'error' }); }
}

// invito a giocare: serve un gioco (quello che sto giocando o uno scritto a mano)
export function inviteToGame(to) {
  const me = state.friends && null;
  return formSheet({
    title: t('invite.title'), subtitle: t('invite.sub', { names: to.join(', ') }), submit: t('invite.send'),
    fields: [
      { name: 'game', label: t('invite.game'), required: true, max: 128, placeholder: t('invite.gamePh'), value: me || '' },
      { name: 'text', label: t('invite.note'), max: 120, placeholder: t('invite.notePh') },
    ],
    onSubmit: async (v) => {
      const name = v.game.trim();
      const id = name.toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '').slice(0, 64) || 'game';
      await post('/invites', { to, game_id: id, game_name: name, text: v.text || undefined });
      toast(t('invite.sent'), { kind: 'ok', icon: 'gamepad' });
    },
  });
}

// scegli uno o più amici
export function pickFriends({ title, subtitle, multi = false, exclude = [], submit, onPick }) {
  return new Promise((resolve) => {
    let picked = new Set();
    let resultSet = null;
    sheet({
      title, subtitle,
      content: (close) => {
        const listEl = h('div', { class: 'list' });
        const q = h('input', { class: 'input', type: 'search', placeholder: t('friends.filter'), 'aria-label': t('friends.filter') });
        const go2 = multi ? btn(submit || t('common.confirm'), { block: true, disabled: true }) : null;
        const draw = (friends) => {
          const f = q.value.trim().toLowerCase();
          const rows = friends.filter((x) => !exclude.includes(x.online_id) && (!f || x.online_id.toLowerCase().includes(f)));
          if (!rows.length) { replace(listEl, empty('friends', friends.length ? t('friends.noMatch') : t('friends.none'), null)); listEl.classList.remove('list'); return; }
          listEl.classList.add('list');
          replace(listEl, rows.map((u) => {
            const on = picked.has(u.online_id);
            const row = h('button', { class: 'lrow', type: 'button', 'aria-pressed': String(on) },
              avatar(u, 40, { presence: u.presence }), h('span', { class: 'lr-main' }, h('span', { class: 'lr-title', text: u.online_id }), u.presence ? presenceSub(u.presence) : null),
              multi ? h('span', { class: 'lr-trail' }, icon(on ? 'check' : 'plus', 20)) : icon('chev', 18, 'lr-chev'));
            row.addEventListener('click', async () => {
              if (!multi) { close(); resultSet = [u.online_id]; if (onPick) await onPick(u.online_id); return; }
              if (picked.has(u.online_id)) picked.delete(u.online_id); else picked.add(u.online_id);
              go2.disabled = !picked.size;
              draw(friends);
            });
            return row;
          }));
        };
        const box = h('div', { class: 'stack' }, h('div', { class: 'search' }, icon('search', 18), q), listEl, go2);
        listEl.appendChild(skeletonList(3));
        const load = state.friends && state.friends.length ? Promise.resolve(state.friends) : get('/friends').then((r) => r.friends);
        load.then((friends) => { q.addEventListener('input', () => draw(friends)); draw(friends); }).catch((e) => replace(listEl, h('p', { class: 'form-error', text: errorText(e) })));
        if (go2) go2.addEventListener('click', async () => { resultSet = [...picked]; await busy(go2, async () => { try { if (onPick) await onPick(resultSet); close(); } catch (e) { toast(errorText(e), { kind: 'error' }); } }); });
        return box;
      },
      onClose: () => resolve(resultSet),
    });
  });
}

// ------------------------------------------------------------- stato --
export const STATUS = [
  { mode: 'online', icon: 'check', cls: 'p-online' },
  { mode: 'away', icon: 'clock', cls: 'p-away' },
  { mode: 'dnd', icon: 'bellmute', cls: 'p-dnd' },
  { mode: 'invisible', icon: 'eyeoff', cls: 'p-offline' },
];
export function statusSheet(after) {
  const cur = (state.me && state.me.status_mode) || 'online';
  sheet({
    title: t('status.title'),
    content: (close) => {
      const msg = h('input', { class: 'input', maxlength: 60, placeholder: t('status.msgPh'), value: (state.me && state.me.status_message) || '' });
      let mode = cur;
      const opts = h('div', { class: 'sheet-actions' }, STATUS.map((s) => {
        const b = h('button', { class: `sheet-action ${s.mode === cur ? 'is-active' : ''}`, type: 'button', dataset: { m: s.mode } },
          h('span', { class: `pdot-i ${s.cls}`, style: { width: '12px', height: '12px', borderRadius: '50%', display: 'block' } }),
          h('span', { class: 'sa-text' }, h('span', { text: t(`status.${s.mode}`) }), h('small', { text: t(`status.${s.mode}Hint`) })));
        b.addEventListener('click', () => { mode = s.mode; opts.querySelectorAll('.sheet-action').forEach((x) => x.classList.toggle('is-active', x.dataset.m === mode)); });
        return b;
      }));
      const save = btn(t('common.save'), { block: true });
      save.addEventListener('click', () => busy(save, async () => {
        try {
          const r = await post('/status', { mode, message: msg.value });
          state.me = { ...state.me, status_mode: r.status_mode, status_message: r.status_message };
          emit('badges'); emit('resync');
          toast(t('status.saved'), { kind: 'ok' });
          close();
          after && after();
        } catch (e) { toast(errorText(e), { kind: 'error' }); }
      }));
      return h('div', { class: 'stack' }, opts,
        h('label', { class: 'field' }, h('span', { class: 'field-label', text: t('status.msg') }), msg, h('small', { class: 'field-hint', text: t('status.msgHint') })), save);
    },
  });
}

// --------------------------------------------------------------- post --
export function postCard(p, { onDeleted, expanded = false } = {}) {
  const el = h('article', { class: 'card card-flush post' });
  const likeBtn = h('button', { class: `pa-btn ${p.liked ? 'liked' : ''}`, type: 'button', 'aria-pressed': String(p.liked), 'aria-label': t('feed.like') },
    icon(p.liked ? 'heartf' : 'heart', 20), h('span', { text: p.likes ? compact(p.likes) : '' }));
  const cBtn = h('button', { class: 'pa-btn', type: 'button', 'aria-label': t('feed.comments'), 'aria-expanded': 'false' }, icon('comment', 20), h('span', { text: p.comments ? compact(p.comments) : '' }));
  const commentsEl = h('div', { class: 'comments', hidden: true });
  const more = iconBtn('more', t('common.more'), () => {
    sheet({
      actions: [
        { icon: 'user', label: t('feed.viewProfile', { name: p.author.online_id }), onClick: () => go(userPath(p.author.online_id)) },
        p.mine ? { icon: 'trash', label: t('feed.delete'), danger: true, onClick: async () => {
          if (!(await confirmDialog({ title: t('feed.deleteQ'), ok: t('feed.delete'), danger: true }))) return;
          try { await del(`/posts/${E(p.post_id)}`); el.remove(); toast(t('feed.deleted')); onDeleted && onDeleted(); } catch (e) { toast(errorText(e), { kind: 'error' }); }
        } } : { icon: 'flag', label: t('feed.report'), danger: true, onClick: () => reportSheet('post', `/posts/${E(p.post_id)}/report`) },
      ],
    });
  });
  likeBtn.addEventListener('click', async () => {
    const want = !p.liked;
    p.liked = want; p.likes += want ? 1 : -1;
    paintLike(); vibrate(8);
    try { const r = await post(`/posts/${E(p.post_id)}/like`, { like: want }); p.likes = r.likes; p.liked = r.liked; paintLike(); }
    catch (e) { p.liked = !want; p.likes += want ? -1 : 1; paintLike(); toast(errorText(e), { kind: 'error' }); }
  });
  function paintLike() {
    likeBtn.classList.toggle('liked', p.liked);
    likeBtn.setAttribute('aria-pressed', String(p.liked));
    likeBtn.querySelector('use').setAttribute('href', p.liked ? '#i-heartf' : '#i-heart');
    likeBtn.querySelector('span').textContent = p.likes > 0 ? compact(p.likes) : '';
  }
  let loaded = false;
  const toggleComments = async () => {
    const show = commentsEl.hidden;
    commentsEl.hidden = !show;
    cBtn.setAttribute('aria-expanded', String(show));
    if (show && !loaded) { loaded = true; await loadComments(); }
  };
  cBtn.addEventListener('click', toggleComments);
  async function loadComments() {
    replace(commentsEl, skeletonList(2));
    try {
      const r = await get(`/posts/${E(p.post_id)}/comments`);
      drawComments(r.comments);
    } catch (e) { replace(commentsEl, h('p', { class: 'form-error', text: errorText(e) })); loaded = false; }
  }
  function drawComments(list) {
    const ta = h('textarea', { class: 'input', rows: 1, maxlength: 300, placeholder: t('feed.commentPh'), 'aria-label': t('feed.commentPh') });
    const send = h('button', { class: 'send-btn', type: 'submit', 'aria-label': t('common.send'), disabled: true }, icon('send', 18));
    ta.addEventListener('input', () => { send.disabled = !ta.value.trim(); });
    autoGrow(ta, 120);
    const form = h('form', { class: 'cmt-form' }, avatar(state.me || {}, 32), ta, send);
    form.addEventListener('submit', async (e) => {
      e.preventDefault();
      const text = ta.value.trim(); if (!text) return;
      send.disabled = true;
      try {
        const r = await post(`/posts/${E(p.post_id)}/comments`, { text });
        list.push(r.comment); p.comments += 1;
        cBtn.querySelector('span').textContent = compact(p.comments);
        ta.value = ''; drawComments(list);
      } catch (ex) { toast(errorText(ex), { kind: 'error' }); send.disabled = false; }
    });
    ta.addEventListener('keydown', (e) => { if (e.key === 'Enter' && !e.shiftKey && matchMedia('(pointer: fine)').matches) { e.preventDefault(); form.requestSubmit(); } });
    replace(commentsEl, list.map((c) => commentEl(c)), form);
    function commentEl(c) {
      const node = h('div', { class: 'cmt' }, h('a', { href: `#${userPath(c.author.online_id)}` }, avatar(c.author, 32)),
        h('div', { style: { flex: 1, minWidth: 0 } },
          h('div', { class: 'cmt-b' }, h('a', { class: 'who', href: `#${userPath(c.author.online_id)}`, text: c.author.online_id }), document.createTextNode(c.text)),
          h('div', { class: 'cmt-meta' }, h('span', { text: relTime(c.created_at) }),
            (c.mine || p.mine) ? h('button', { type: 'button', text: t('common.delete'), onclick: async () => {
              try { await del(`/posts/${E(p.post_id)}/comments/${E(c.comment_id)}`); list.splice(list.indexOf(c), 1); p.comments -= 1; cBtn.querySelector('span').textContent = p.comments ? compact(p.comments) : ''; drawComments(list); }
              catch (e) { toast(errorText(e), { kind: 'error' }); }
            } }) : null,
            !c.mine ? h('button', { type: 'button', text: t('feed.report'), onclick: () => reportSheet('comment', `/posts/${E(p.post_id)}/comments/${E(c.comment_id)}/report`) }) : null)));
      return node;
    }
  }
  add(el, 
    h('div', { class: 'post-head' },
      h('a', { href: `#${userPath(p.author.online_id)}` }, avatar(p.author, 42)),
      h('div', { class: 'post-who' }, h('a', { href: `#${userPath(p.author.online_id)}`, text: p.author.online_id }), h('small', { text: relTime(p.created_at) })),
      more),
    p.game_name ? h('div', { class: 'post-game' }, h('span', { class: 'pill pill-play' }, icon('gamepad', 14), p.game_name)) : null,
    richText(p.text, 'post-body'),
    h('div', { class: 'post-actions' }, likeBtn, cBtn),
    commentsEl);
  if (expanded) toggleComments();
  return el;
}

// --------------------------------------------------------------- app --
export function appCover(a, cls = 'app-cover') {
  if (a.has_cover) return apiImg(`/store/apps/${E(a.app_id)}/cover`, `${cls} fit`, '');
  return h('div', { class: `pimg ${cls}`, style: { background: 'var(--grad)' } });
}
export function appIcon(a, size) {
  const el = a.has_icon ? apiImg(`/store/apps/${E(a.app_id)}/icon`, 'app-ic') : h('div', { class: 'pimg app-ic', style: { background: 'var(--grad)' } });
  if (size) { el.style.width = `${size}px`; el.style.height = `${size}px`; }
  return el;
}
export function catLabel(c) { const k = `cat.${c}`; const s = t(k); return s === k ? c : s; }
export function appTile(a) {
  return h('a', { class: 'app-tile', href: `#${appPath(a.app_id)}` },
    appCover(a),
    h('div', { class: 'at-info' },
      appIcon(a, 40),
      h('div', { class: 'at-txt' },
        h('div', { class: 'at-title', text: a.title }),
        h('div', { class: 'at-sub', text: a.tagline || catLabel(a.category) }),
        h('div', { class: 'at-meta' },
          a.friends_count ? h('span', { class: 'avatars-stack' }, (a.friends || []).slice(0, 3).map((f) => avatar(f, 18))) : null,
          a.friends_count ? h('span', { text: t('store.friendsN', { n: a.friends_count }) })
            : a.rating ? [icon('starf', 12, 'on'), h('span', { text: a.rating.toFixed(1) })]
              : h('span', { text: catLabel(a.category) })))));
}
export function appRow(a, n) {
  return h('a', { class: 'app-row', href: `#${appPath(a.app_id)}` },
    n ? h('span', { class: `rank-n ${n <= 3 ? `r${n}` : ''}`, text: String(n) }) : null,
    appIcon(a),
    h('div', { class: 'rank-main' }, h('strong', { text: a.title }), h('small', { text: a.tagline || catLabel(a.category) }),
      h('small', { class: 'at-meta' }, a.rating ? [icon('starf', 12, 'on'), ` ${a.rating.toFixed(1)} · `] : null, catLabel(a.category), a.likes ? ` · ${t('store.likesN', { n: compact(a.likes) })}` : '')),
    icon('chev', 18, 'lr-chev'));
}

// ---------------------------------------------------------- notizie --
const NEWS_COLORS = ['#6a72ff', '#2fd6ff', '#c45bff', '#ff7a59', '#14c38e', '#f5a623', '#ff4f8b', '#5a6482'];
export function newsItem(n, big = false) {
  const img = n.has_image ? apiImg(`/news/${E(n.news_id)}/image`, 'news-img') : h('div', { class: 'news-ph', style: { background: `linear-gradient(135deg, ${NEWS_COLORS[(n.color || 0) % 8]}, #1b2030)` } }, icon('globe', 26));
  const attrs = n.link ? { class: `news ${big ? 'news-big' : ''}`, href: n.link, target: '_blank', rel: 'noopener noreferrer' } : { class: `news ${big ? 'news-big' : ''}`, href: '#', onclick: (e) => { e.preventDefault(); newsSheet(n); } };
  return h('a', attrs, img, h('div', { class: 'news-txt' },
    h('div', { class: 'news-title', text: n.title }),
    h('div', { class: 'news-meta', text: [n.source || n.tag, relTime(n.created_at)].filter(Boolean).join(' · ') })));
}
function newsSheet(n) {
  sheet({ title: n.title, subtitle: [n.source || n.tag, relTime(n.created_at)].filter(Boolean).join(' · '), content: richText(n.body, 'desc') });
}

// ---------------------------------------------------------- console --
export function consoleCard(c, { compactMode = false } = {}) {
  const on = c && c.online;
  const el = h('div', { class: `console-card ${on ? 'is-on' : ''}` });
  let stateTxt; let note = null;
  if (!c) stateTxt = t('console.loading');
  else if (!c.paired) { stateTxt = t('console.never'); note = t('console.neverShort'); }
  else if (on) stateTxt = t('console.on');
  else { stateTxt = c.last_seen ? t('console.offSince', { when: relTime(c.last_seen) }) : t('console.off'); note = t('console.offShort'); }
  add(el, h('a', { class: 'cc-top', href: '#/console', style: { color: 'inherit' } },
    h('div', { class: 'cc-ps' }, icon('console', 28)),
    h('div', { class: 'cc-txt' }, h('h3', { text: t('console.title') }), h('div', { class: 'cc-state' }, h('span', { class: 'cc-led' }), h('span', { text: stateTxt }))),
    icon('chev', 18, 'lr-chev')));
  if (on && c.game) add(el, h('div', { class: 'cc-game' }, gameIcon({ game_id: c.game.id, game_name: c.game.name }, 36), h('div', { class: 'rank-main' }, h('small', { text: t('console.playing') }), h('strong', { text: c.game.name || c.game.id }))));
  if (on && c.link) {
    add(el, h('a', { class: 'btn btn-grad btn-block', href: c.link, target: '_blank', rel: 'noopener' }, icon('external', 20), h('span', { text: t('console.openRemote') })));
    if (!compactMode) add(el, h('p', { class: 'cc-note', text: t('console.sameWifi') }));
  } else if (note) add(el, h('p', { class: 'cc-note', text: note }));
  return el;
}

// ------------------------------------------------------- installa sulla PS5 --
export async function installOnPS5(app, button) {
  return busy(button, async () => {
    try {
      const r = await post('/console/queue', { app_id: Number(app.app_id) });
      vibrate(15);
      if (r.online) toast(t('store.queuedOnline', { title: app.title }), { kind: 'ok', icon: 'console', duration: 5000 });
      else {
        sheet({
          title: t('store.queuedTitle'),
          content: (close) => h('div', { class: 'stack' },
            h('div', { class: 'note' }, icon('console', 20), h('span', {}, h('strong', { text: app.title }), ' — ', t('store.queuedOffline'))),
            h('p', { class: 'muted small', text: t('store.queuedOfflineHint') }),
            btn(t('common.ok'), { block: true, onclick: () => close() })),
        });
      }
    } catch (e) {
      toast(e.code === 'queue_full' ? t('err.queue_full') : errorText(e), { kind: 'error' });
    }
  });
}
