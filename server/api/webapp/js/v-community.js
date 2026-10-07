// Community: bacheca, gruppi, classifiche/record, trofei, tempo di gioco.
import { t } from './i18n.js';
import { get, post, del, E } from './api.js';
import { add,
  h, icon, iconBtn, avatar, apiImg, replace, skeletonList, empty, errorState, relTime, tabs, segmented, btn, busy, toast,
  errorText, autoGrow, duration, num, gameIcon, section, sheet, formSheet, confirmDialog, badge, vibrate,
} from './ui.js';
import { state, emit, pref, setPref } from './state.js';
import { postCard, pickFriends, userRow, newsItem } from './common.js';
import { chatThread } from './v-chat.js';
import { go, userPath } from './nav.js';

const TABS = ['feed', 'groups', 'records', 'trophies', 'playtime'];

export async function community(ctx) {
  const { page, query } = ctx;
  let tab = TABS.includes(query.tab) ? query.tab : pref('communityTab', 'feed');
  const body = h('div');
  const mkTabs = () => tabs([
    { value: 'feed', label: t('comm.feed'), icon: 'community' },
    { value: 'groups', label: t('comm.groups'), icon: 'group', badge: state.counts.unread_groups },
    { value: 'records', label: t('comm.records'), icon: 'chart' },
    { value: 'trophies', label: t('comm.trophies'), icon: 'trophy' },
    { value: 'playtime', label: t('comm.playtime'), icon: 'clock' },
  ], tab, (v) => { tab = v; setPref('communityTab', v); history.replaceState(null, '', `#/community?tab=${v}`); draw(); });
  let tb = mkTabs();
  add(page, tb, body);
  let refresh = null;
  async function draw() {
    ctx.setActions();
    const sub = { ...ctx, onRefresh: (fn) => { refresh = fn; } };
    refresh = null;
    replace(body);
    if (tab === 'feed') await feedTab(sub, body, query.compose === '1');
    else if (tab === 'groups') await groupsTab(sub, body);
    else if (tab === 'records') await recordsTab(sub, body);
    else if (tab === 'trophies') await trophiesTab(sub, body);
    else await playtimeTab(sub, body);
  }
  ctx.onRefresh(() => (refresh ? refresh() : draw()));
  ctx.on('sync', () => { const n = mkTabs(); tb.replaceWith(n); tb = n; });
  await draw();
}

// ------------------------------------------------------------ bacheca --
async function feedTab(ctx, body, focusCompose) {
  const ta = h('textarea', { class: 'input', rows: 2, maxlength: 600, placeholder: t('feed.composePh'), 'aria-label': t('feed.composePh') });
  const counter = h('span', { class: 'counter', text: '0/500' });
  const gameChip = h('button', { class: 'chip', type: 'button' }, icon('gamepad', 16), h('span', { text: t('feed.addGame') }));
  let game = null;
  gameChip.addEventListener('click', async () => {
    if (game) { game = null; gameChip.classList.remove('on'); gameChip.querySelector('span').textContent = t('feed.addGame'); return; }
    const v = await formSheet({ title: t('feed.addGame'), fields: [{ name: 'g', label: t('invite.game'), max: 128, required: true, placeholder: t('invite.gamePh') }], submit: t('common.add') });
    if (v && v.g.trim()) { game = v.g.trim(); gameChip.classList.add('on'); gameChip.querySelector('span').textContent = game; }
  });
  const publish = btn(t('feed.publish'), { size: 'sm', disabled: true });
  const sync = () => { const n = [...ta.value.trim()].length; counter.textContent = `${n}/500`; counter.classList.toggle('over', n > 500); publish.disabled = !n || n > 500; };
  ta.addEventListener('input', sync);
  autoGrow(ta, 220);
  const list = h('div', { class: 'feed' }, skeletonList(3, 'card'));
  publish.addEventListener('click', () => busy(publish, async () => {
    try {
      const r = await post('/posts', { text: ta.value.trim(), game_name: game || undefined, game_id: game ? game.toLowerCase().replace(/[^a-z0-9]+/g, '-').slice(0, 64) : undefined });
      ta.value = ''; sync(); game = null; gameChip.classList.remove('on'); gameChip.querySelector('span').textContent = t('feed.addGame');
      vibrate(12);
      const emptyEl = list.querySelector('.empty'); if (emptyEl) emptyEl.remove();
      list.prepend(postCard(r.post));
      toast(t('feed.published'), { kind: 'ok' });
    } catch (e) { toast(errorText(e), { kind: 'error' }); }
  }));
  add(body, h('div', { class: 'card composer-card' }, avatar(state.me || {}, 42),
    h('div', { style: { flex: 1, minWidth: 0 } }, ta, h('div', { class: 'composer-foot' }, gameChip, h('div', { class: 'row', style: { gap: '10px' } }, counter, publish)))),
  h('div', { class: 'mt16' }, list));
  if (focusCompose) setTimeout(() => ta.focus(), 300);

  let next = null;
  async function load(reset = true) {
    try {
      const r = await get(`/feed?limit=15${!reset && next ? `&before=${next}` : ''}`);
      next = r.next_before;
      if (reset) replace(list);
      list.querySelector('.load-more')?.remove();
      if (reset && !r.posts.length) { add(list, empty('community', t('feed.empty'), t('feed.emptyText'))); return; }
      add(list, ...r.posts.map((p) => postCard(p)));
      if (next) add(list, h('div', { class: 'load-more' }, btn(t('common.loadMore'), { kind: 'ghost', size: 'sm', onclick: (e) => busy(e.currentTarget, () => load(false)) })));
    } catch (e) { if (reset) replace(list, errorState(e, () => load(true))); else toast(errorText(e), { kind: 'error' }); }
  }
  ctx.onRefresh(() => load(true));
  await load(true);
}

// post singolo (da notifica)
export async function post_(ctx) {
  const { page, params } = ctx;
  add(page, skeletonList(1, 'card'));
  try {
    const r = await get(`/posts/${E(params.id)}`);
    replace(page, h('div', { style: { maxWidth: '680px', margin: '8px auto 0' } }, postCard(r.post, { expanded: true, onDeleted: () => go('/community') })));
  } catch (e) {
    if (e.code === 'post_not_found' || e.code === 'not_friends' || e.code === 'blocked') replace(page, empty('community', t('feed.notFound'), t('feed.notFoundText')));
    else replace(page, errorState(e, () => post_(ctx)));
  }
}

// ------------------------------------------------------------- gruppi --
async function groupsTab(ctx, body) {
  const create = () => createGroup();
  ctx.setActions(iconBtn('plus', t('groups.create'), create));
  const box = h('div', {}, skeletonList(4));
  add(body, box);
  async function load() {
    try {
      const r = await get('/groups');
      if (!r.groups.length) { replace(box, empty('group', t('groups.empty'), t('groups.emptyText'), btn(t('groups.create'), { icon: 'plus', onclick: create }))); return; }
      replace(box, h('div', { class: 'row', style: { justifyContent: 'space-between', marginBottom: '12px' } },
        h('p', { class: 'muted small', text: t('groups.count', { n: r.groups.length }) }), btn(t('groups.create'), { size: 'sm', kind: 'soft', icon: 'plus', onclick: create })),
      h('div', { class: 'list' }, r.groups.map((g) => h('a', { class: `lrow ${g.unread ? 'conv-unread' : ''}`, href: `#/group/${E(g.group_id)}` },
        groupAvatar(g),
        h('span', { class: 'lr-main' }, h('span', { class: 'lr-title', text: g.name }),
          h('span', { class: 'lr-sub', text: g.last_message ? `${g.last_message.from && !g.last_message.text.startsWith(g.last_message.from) ? `${g.last_message.from}: ` : ''}${g.last_message.text}` : t('groups.membersN', { n: g.member_count }) })),
        h('span', { class: 'lr-trail', style: { flexDirection: 'column', alignItems: 'flex-end', gap: '4px' } }, g.last_message ? h('span', { text: relTime(g.last_message.created_at) }) : null, badge(g.unread))))));
    } catch (e) { replace(box, errorState(e, load)); }
  }
  ctx.onRefresh(load);
  await load();
}
function groupAvatar(g) {
  const m = (g.members || []).slice(0, 2);
  return h('span', { style: { position: 'relative', width: '50px', height: '50px', flex: 'none' } },
    m[0] ? h('span', { style: { position: 'absolute', left: 0, top: 0 } }, avatar(m[0], 34)) : null,
    m[1] ? h('span', { style: { position: 'absolute', right: 0, bottom: 0, borderRadius: '50%', boxShadow: '0 0 0 2px var(--card)' } }, avatar(m[1], 34)) : null);
}
async function createGroup() {
  const v = await formSheet({ title: t('groups.create'), subtitle: t('groups.createSub'), fields: [{ name: 'name', label: t('groups.name'), required: true, max: 40, placeholder: t('groups.namePh') }], submit: t('groups.chooseMembers') });
  if (!v) return;
  await pickFriends({
    title: t('groups.chooseMembers'), subtitle: v.name, multi: true, submit: t('groups.createBtn'),
    onPick: async (list) => {
      const r = await post('/groups', { name: v.name.trim(), members: list });
      toast(t('groups.created'), { kind: 'ok' });
      go(`/group/${E(r.group.group_id)}`);
    },
  });
}

export async function group(ctx) {
  const { page, params } = ctx;
  const id = params.id;
  let g = null;
  const head = () => h('button', { class: 'chat-head', type: 'button', onclick: membersSheet }, groupAvatar(g),
    h('span', { class: 'chat-head-txt' }, h('strong', { text: g.name }), h('small', { text: t('groups.membersN', { n: g.member_count }) })));
  async function loadCard() { g = (await get(`/groups/${E(id)}`)).group; ctx.setTitle(head()); }
  function membersSheet() {
    const meOid = state.me && state.me.online_id;
    const owner = g.owner === meOid;
    sheet({
      title: g.name, subtitle: t('groups.membersN', { n: g.member_count }),
      content: (close) => h('div', { class: 'stack' },
        h('div', { class: 'list' }, g.members.map((m) => userRow(m, {
          sub: m.online_id === g.owner ? t('groups.owner') : '', presence: false,
          trail: owner && m.online_id !== meOid ? iconBtn('userx', t('groups.remove'), async () => {
            if (!(await confirmDialog({ title: t('groups.removeQ', { name: m.online_id }), ok: t('groups.remove'), danger: true }))) return;
            try { await del(`/groups/${E(id)}/members/${E(m.online_id)}`); await loadCard(); close(); membersSheet(); } catch (e) { toast(errorText(e), { kind: 'error' }); }
          }) : null,
        }))),
        h('div', { class: 'sheet-actions' },
          h('button', { class: 'sheet-action', type: 'button', onclick: () => { close(); addMembers(); } }, icon('useradd', 22), h('span', { class: 'sa-text', text: t('groups.add') })),
          owner ? h('button', { class: 'sheet-action', type: 'button', onclick: () => { close(); rename(); } }, icon('edit', 22), h('span', { class: 'sa-text', text: t('groups.rename') })) : null,
          h('button', { class: 'sheet-action is-danger', type: 'button', onclick: async () => { close(); await leave(); } }, icon('logout', 22), h('span', { class: 'sa-text', text: t('groups.leave') })))),
    });
  }
  function addMembers() {
    pickFriends({ title: t('groups.add'), multi: true, exclude: g.members.map((m) => m.online_id), submit: t('common.add'),
      onPick: async (list) => { await post(`/groups/${E(id)}/members`, { add: list }); toast(t('groups.added'), { kind: 'ok' }); await loadCard(); thread.pull(false); } });
  }
  async function rename() {
    const v = await formSheet({ title: t('groups.rename'), fields: [{ name: 'name', label: t('groups.name'), value: g.name, required: true, max: 40 }], onSubmit: async (vals) => { await post(`/groups/${E(id)}`, { name: vals.name.trim() }); return vals; } });
    if (v) { await loadCard(); thread.pull(false); }
  }
  async function leave() {
    if (!(await confirmDialog({ title: t('groups.leaveQ', { name: g.name }), text: t('groups.leaveText'), ok: t('groups.leave'), danger: true }))) return;
    try { await post(`/groups/${E(id)}/leave`); toast(t('groups.left')); go('/community?tab=groups', { replace: true }); } catch (e) { toast(errorText(e), { kind: 'error' }); }
  }
  const thread = chatThread(ctx, {
    fetch: async (after) => {
      const r = await get(`/groups/${E(id)}/messages${after ? `?after=${after}` : ''}`);
      return r.messages.map((m) => ({ id: m.message_id, mine: m.mine && !m.system, text: m.text, created_at: m.created_at, from: m.from, system: m.system }));
    },
    send: (text) => post(`/groups/${E(id)}/messages`, { text }),
    showNames: true, emptyIcon: 'group',
  });
  ctx.setActions(iconBtn('friends', t('groups.members'), () => g && membersSheet()));
  try {
    await loadCard();
    add(page, thread.el);
    await thread.start();
    emit('resync');
  } catch (e) {
    if (e.code === 'not_member' || e.code === 'group_not_found') replace(page, empty('group', t('groups.notFound'), null));
    else replace(page, errorState(e, () => group(ctx)));
  }
}

// --------------------------------------------------------- classifiche --
function rankRow({ user, rank, me, main, sub, val, href }) {
  return h('a', { class: `rank-row ${me ? 'me' : ''}`, href: href || `#${userPath(user.online_id)}` },
    h('span', { class: `rank-n ${rank <= 3 ? `r${rank}` : ''}`, text: String(rank) }),
    avatar(user, 40),
    h('span', { class: 'rank-main' }, h('strong', { text: main || user.online_id }), sub ? h('small', { text: sub }) : null),
    h('span', { class: 'rank-val', text: val }));
}
function podium(rows, valFn) {
  const top = rows.slice(0, 3);
  if (top.length < 3) return null;
  const order = [top[1], top[0], top[2]];
  const medal = ['', '🥇', '🥈', '🥉'];
  return h('div', { class: 'podium' }, order.map((r) => h('a', { class: `pod pod-${r.rank}`, href: `#${userPath(r.user.online_id)}` },
    avatar(r.user, r.rank === 1 ? 72 : 58),
    h('div', { class: 'pod-base' }, h('span', { class: 'pod-medal', text: medal[r.rank] || String(r.rank) }), h('strong', { text: r.user.online_id }), h('small', { text: valFn(r) })))));
}

async function recordsTab(ctx, body) {
  let period = pref('recPeriod', 'week');
  const seg = segmented([{ value: 'week', label: t('rec.week') }, { value: 'all', label: t('rec.all') }], period, (v) => { period = v; setPref('recPeriod', v); load(); }, { label: t('rec.period') });
  const box = h('div', { class: 'mt16' }, skeletonList(5));
  add(body, h('div', { class: 'row', style: { justifyContent: 'space-between', flexWrap: 'wrap' } }, seg, h('span', { class: 'muted small', text: t('rec.hint') })), box);
  async function load() {
    replace(box, skeletonList(5));
    try {
      const r = await get(`/records?period=${period}`);
      const me = r.me || {};
      const tiles = h('div', { class: 'stat-tiles' },
        tile(t('rec.myRank'), me.listed ? (me.rank ? `#${me.rank}` : '—') : t('rec.hidden'), me.listed && me.of ? t('rec.ofN', { n: me.of }) : ''),
        tile(t('rec.myTime'), duration(me.seconds), period === 'week' ? t('rec.thisWeek') : t('rec.allTime')),
        tile(t('rec.onlineNow'), num(r.now.online), t('rec.players')),
        tile(t('rec.playingNow'), num(r.now.playing), t('rec.inGame')));
      const players = r.players || [];
      const games = r.games || [];
      const mar = r.marathons || [];
      replace(box, tiles,
        !me.listed ? h('div', { class: 'note mt12' }, icon('eyeoff', 18), h('span', {}, t('rec.notListed'), ' ', h('a', { href: '#/settings/privacy', text: t('privacy.title') }))) : null,
        h('div', { class: 'grid2 mt8' },
          h('div', {}, section(t('rec.players'),
            players.length ? [podium(players, (x) => duration(x.seconds)), h('div', { class: 'list' }, players.slice(players.length >= 3 ? 3 : 0).map((p) => rankRow({ user: p.user, rank: p.rank, me: p.me, sub: p.game_name ? t('rec.mostly', { game: p.game_name }) : null, val: duration(p.seconds) })))]
              : h('div', { class: 'card' }, empty('chart', t('rec.empty'), t('rec.emptyText'))))),
          h('div', {},
            section(t('rec.games'), games.length ? h('div', { class: 'list' }, games.slice(0, 15).map((g, i) => h('a', { class: 'rank-row', href: `#/records/game/${E(g.game_id)}` },
              h('span', { class: `rank-n ${i < 3 ? `r${i + 1}` : ''}`, text: String(i + 1) }), gameIcon(g, 40),
              h('span', { class: 'rank-main' }, h('strong', { text: g.game_name }), h('small', { text: `${t('rec.playersN', { n: g.players })}${g.playing_now ? ` · ${t('rec.nowN', { n: g.playing_now })}` : ''}` })),
              h('span', { class: 'rank-val', text: duration(g.seconds) })))) : h('div', { class: 'card' }, empty('gamepad', t('rec.noGames'), null))),
            mar.length ? section(t('rec.marathons'), h('div', { class: 'list' }, mar.slice(0, 10).map((m) => rankRow({ user: m.user, rank: m.rank, me: m.me, sub: m.game_name, val: duration(m.seconds) })))) : null)));
    } catch (e) { replace(box, errorState(e, load)); }
  }
  ctx.onRefresh(load);
  await load();
}
function tile(k, v, sub) { return h('div', { class: 'stat-tile' }, h('span', { class: 'k', text: k }), h('span', { class: 'v', text: v }), sub ? h('span', { class: 'muted small', text: sub }) : null); }

export async function gameRecords(ctx) {
  const { page, params } = ctx;
  let period = pref('recPeriod', 'week');
  const box = h('div', {}, skeletonList(5));
  const seg = segmented([{ value: 'week', label: t('rec.week') }, { value: 'all', label: t('rec.all') }], period, (v) => { period = v; load(); });
  add(page, box);
  async function load() {
    try {
      const r = await get(`/records/games/${E(params.id)}?period=${period}`);
      const g = r.game;
      ctx.setTitle(g.game_name);
      replace(box,
        h('div', { class: 'card row mt8' }, gameIcon(g, 64), h('div', { class: 'rank-main' }, h('strong', { style: { fontSize: '18px' }, text: g.game_name }), h('small', { text: `${duration(g.seconds)} · ${t('rec.playersN', { n: g.players })}` })),
          h('a', { class: 'btn btn-ghost btn-sm', href: `#/game/${E(g.game_id)}?name=${E(g.game_name)}` }, t('game.open'))),
        h('div', { class: 'mt16' }, seg),
        section(t('rec.ranking'), r.ranking.length ? [podium(r.ranking, (x) => duration(x.seconds)), h('div', { class: 'list' }, r.ranking.slice(r.ranking.length >= 3 ? 3 : 0).map((x) => rankRow({ user: x.user, rank: x.rank, me: x.me, val: duration(x.seconds) })))]
          : h('div', { class: 'card' }, empty('chart', t('rec.empty'), null))));
    } catch (e) { replace(box, errorState(e, load)); }
  }
  ctx.onRefresh(load);
  await load();
}

// scheda di un gioco: amici che ci giocano, chi ci ha giocato, notizie
export async function game(ctx) {
  const { page, params, query } = ctx;
  const name = query.name || params.id;
  ctx.setTitle(name);
  const box = h('div', {}, skeletonList(4));
  add(page, box);
  async function load() {
    try {
      const r = await get(`/games/${E(params.id)}?name=${E(name)}`);
      replace(box,
        h('div', { class: 'card row mt8' }, gameIcon({ game_id: params.id, game_name: name }, 64),
          h('div', { class: 'rank-main' }, h('strong', { style: { fontSize: '18px' }, text: name }), h('small', { text: t('game.playersNow', { n: r.players_now }) })),
          h('a', { class: 'btn btn-ghost btn-sm', href: `#/records/game/${E(params.id)}` }, icon('chart', 16), t('rec.ranking'))),
        h('div', { class: 'grid2' },
          h('div', {},
            section(t('game.friendsNow'), r.playing_now.length ? h('div', { class: 'list' }, r.playing_now.map((f) => userRow(f, { sub: t('game.since', { when: relTime(f.started_at) }), presence: false }))) : h('div', { class: 'card' }, h('p', { class: 'muted small', text: t('game.nobodyNow') }))),
            r.played.length ? section(t('game.friendsPlayed'), h('div', { class: 'list' }, r.played.map((f) => userRow(f, { sub: t('user.lastPlayed', { when: relTime(f.last_played) }), presence: false })))) : null),
          h('div', {}, r.news.length ? section(t('home.news'), h('div', { class: 'news-list' }, r.news.map((n) => newsItem(n)))) : null)));
    } catch (e) { replace(box, errorState(e, load)); }
  }
  ctx.onRefresh(load);
  await load();
}

// --------------------------------------------------------------- trofei --
const GR = ['p', 'g', 's', 'b'];
function gradeRow(o) { return h('div', { class: 'trophy-sum' }, GR.map((g) => h('span', { class: `tgrade g-${g}`, title: t(`grade.${g}`) }, icon('trophy', 16), num(o[g] || 0)))); }

async function trophiesTab(ctx, body) {
  const box = h('div', {}, skeletonList(5));
  add(body, box);
  async function load() {
    try {
      const r = await get('/trophies/ranking');
      const me = r.me || {};
      const meOid = state.me && state.me.online_id;
      replace(box,
        h('a', { class: 'card row', href: `#/trophies/${E(meOid || '')}`, style: { color: 'inherit' } },
          h('span', { class: 'quick-ic', style: { background: 'linear-gradient(135deg,#f5c451,#ff8a3d)', width: '48px', height: '48px', borderRadius: '14px' } }, icon('trophy', 24)),
          h('div', { class: 'rank-main' }, h('strong', { text: t('trophies.mine') }),
            h('small', { text: !me.importing ? t('trophies.importOff') : me.listed ? (me.rank ? t('trophies.myRank', { rank: me.rank, n: me.of }) : t('trophies.noneYet')) : t('trophies.notListed') })),
          icon('chev', 18, 'lr-chev')),
        !me.importing ? h('div', { class: 'note mt12' }, icon('info', 18), h('span', {}, t('trophies.importHint'), ' ', h('a', { href: '#/settings/privacy', text: t('privacy.title') }))) : null,
        section(t('trophies.ranking'), r.ranking.length ? [podium(r.ranking, (x) => t('trophies.ptsN', { n: num(x.points) })),
          h('div', { class: 'list' }, r.ranking.slice(r.ranking.length >= 3 ? 3 : 0).map((x) => h('a', { class: `rank-row ${x.me ? 'me' : ''}`, href: `#/trophies/${E(x.user.online_id)}` },
            h('span', { class: `rank-n ${x.rank <= 3 ? `r${x.rank}` : ''}`, text: String(x.rank) }), avatar(x.user, 40),
            h('span', { class: 'rank-main' }, h('strong', { text: x.user.online_id }), gradeRow(x)),
            h('span', { class: 'rank-val', text: num(x.points) }))))]
          : h('div', { class: 'card' }, empty('trophy', t('trophies.rankEmpty'), t('trophies.rankEmptyText')))));
    } catch (e) { replace(box, errorState(e, load)); }
  }
  ctx.onRefresh(load);
  await load();
}

export async function trophies(ctx) {
  const { page, params } = ctx;
  const oid = params.user;
  ctx.setTitle(t('trophies.of', { name: oid }));
  add(page, skeletonList(5));
  async function load() {
    try {
      const r = await get(`/users/${E(oid)}/trophies`);
      const s = r.summary || {};
      replace(page,
        h('div', { class: 'card mt8' },
          h('div', { class: 'row', style: { justifyContent: 'space-between', flexWrap: 'wrap' } },
            h('a', { class: 'row', href: `#${userPath(r.online_id)}`, style: { color: 'inherit' } }, avatar({ online_id: r.online_id }, 48), h('div', { class: 'rank-main' }, h('strong', { text: r.online_id }), h('small', { text: t('trophies.setsN', { n: s.sets || 0 }) }))),
            h('div', { class: 'stat-tile', style: { padding: 0, border: 0, background: 'none', alignItems: 'flex-end' } }, h('span', { class: 'k', text: t('trophies.points') }), h('span', { class: 'v', text: num(s.points || 0) }))),
          h('div', { class: 'divider' }), gradeRow(s)),
        section(t('trophies.games'), r.sets.length ? h('div', { class: 'list' }, r.sets.map((x) => h('a', { class: 'tset', href: `#/trophies/${E(r.online_id)}/${E(x.np_id)}` },
          x.icon_media ? apiImg(`/media/${x.icon_media}/1`, 'game-ic', '') : gameIcon({ game_name: x.title }, 52),
          h('div', { class: 'tset-main' }, h('strong', { style: { overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }, text: x.title }),
            h('div', { class: 'row', style: { gap: '10px' } }, h('div', { class: 'progress', style: { flex: 1 } }, h('span', { style: { width: `${x.progress}%` } })), h('small', { class: 'muted', text: `${x.progress}%` })),
            gradeRow(x.earned)),
          icon('chev', 18, 'lr-chev')))) : h('div', { class: 'card' }, empty('trophy', t('trophies.noneYet'), t('trophies.noneText')))));
      page.querySelectorAll('.tset > .pimg.game-ic').forEach((el) => { el.style.width = '52px'; el.style.height = '52px'; });
    } catch (e) {
      if (e.status === 403) replace(page, empty('lock', t('trophies.private'), t('trophies.privateText')));
      else replace(page, errorState(e, load));
    }
  }
  ctx.onRefresh(load);
  await load();
}

export async function trophySet(ctx) {
  const { page, params } = ctx;
  add(page, skeletonList(6));
  async function load() {
    try {
      const r = await get(`/users/${E(params.user)}/trophies/${E(params.np)}`);
      ctx.setTitle(r.title);
      const got = r.trophies.filter((x) => x.earned).length;
      replace(page,
        h('div', { class: 'card row mt8' }, r.icon_media ? apiImg(`/media/${r.icon_media}/1`, 'game-ic') : gameIcon({ game_name: r.title }, 64),
          h('div', { class: 'rank-main' }, h('strong', { style: { fontSize: '17px' }, text: r.title }), h('small', { text: t('trophies.earnedOf', { a: got, b: r.trophies.length }) }),
            h('div', { class: 'progress mt8' }, h('span', { style: { width: `${r.trophies.length ? (got * 100) / r.trophies.length : 0}%` } })))),
        section(null, h('div', { class: 'list' }, r.trophies.map((x) => h('div', { class: `trophy ${x.earned ? '' : 'locked'}` },
          h('span', { class: `trophy-ic g-${x.grade}` }, icon(x.hidden && !x.name ? 'lock' : 'trophy', 24)),
          h('div', { class: 'rank-main' }, h('strong', { text: x.name || t('trophies.hidden') }), h('small', { style: { whiteSpace: 'normal' }, text: x.detail || t('trophies.hiddenText') }),
            x.earned_at ? h('small', { class: 'muted', text: t('trophies.earnedAt', { when: relTime(x.earned_at) }) }) : null),
          x.earned ? icon('check', 20) : null)))));
      page.querySelectorAll('.card > .pimg.game-ic').forEach((el) => { el.style.width = '64px'; el.style.height = '64px'; });
    } catch (e) { replace(page, errorState(e, load)); }
  }
  ctx.onRefresh(load);
  await load();
}

// ------------------------------------------------------- tempo di gioco --
async function playtimeTab(ctx, body) {
  let period = pref('ptPeriod', 'week');
  const mine = h('div', {}, skeletonList(2, 'card'));
  const fr = h('div', {}, skeletonList(4));
  const seg = segmented([{ value: 'week', label: t('rec.week') }, { value: 'all', label: t('rec.all') }], period, (v) => { period = v; setPref('ptPeriod', v); loadFriends(); });
  add(body, mine, h('div', { class: 'grid2' }, h('div', { id: 'pt-games' }), h('div', {}, section(t('stats.friendsRank'), { action: seg }, fr))));
  const gamesBox = body.querySelector('#pt-games');
  async function loadMine() {
    try {
      const r = await get('/stats/me');
      replace(mine, h('div', { class: 'stat-tiles' },
        tile(t('stats.total'), duration(r.total_seconds)), tile(t('stats.week'), duration(r.week_seconds)),
        tile(t('stats.games'), num(r.games.length)), tile(t('stats.top'), r.games[0] ? r.games[0].game_name : '—')));
      const max = Math.max(1, ...r.games.map((g) => g.seconds));
      replace(gamesBox, section(t('stats.perGame'), r.games.length ? h('div', { class: 'card' }, r.games.map((g) => h('a', { class: 'bar-row', href: `#/records/game/${E(g.game_id)}`, style: { color: 'inherit' } }, gameIcon(g, 44),
        h('div', { class: 'bar-main' }, h('div', { class: 'bar-top' }, h('strong', { text: g.game_name }), h('span', { text: duration(g.seconds) })),
          h('div', { class: 'progress' }, h('span', { style: { width: `${Math.max(3, (g.seconds / max) * 100)}%` } })),
          h('small', { class: 'muted', text: `${t('stats.sessionsN', { n: g.sessions })}${g.week_seconds ? ` · ${t('stats.thisWeek', { d: duration(g.week_seconds) })}` : ''}${g.last_played ? ` · ${relTime(g.last_played)}` : ''}` })))))
        : h('div', { class: 'card' }, empty('clock', t('stats.empty'), t('stats.emptyText')))));
    } catch (e) { replace(mine, errorState(e, loadMine)); }
  }
  async function loadFriends() {
    replace(fr, skeletonList(4));
    try {
      const r = await get(`/stats/friends?period=${period}`);
      if (!r.ranking.length || (r.ranking.length === 1 && !r.ranking[0].seconds)) { replace(fr, h('div', { class: 'card' }, empty('friends', t('stats.friendsEmpty'), null))); return; }
      replace(fr, h('div', { class: 'list' }, r.ranking.map((x) => rankRow({ user: x.user, rank: x.rank, me: x.me, val: duration(x.seconds) }))));
    } catch (e) { replace(fr, errorState(e, loadFriends)); }
  }
  ctx.onRefresh(() => Promise.all([loadMine(), loadFriends()]));
  await Promise.all([loadMine(), loadFriends()]);
}

export async function stats(ctx) {
  ctx.setTitle(t('stats.title'));
  await playtimeTab(ctx, ctx.page);
}
