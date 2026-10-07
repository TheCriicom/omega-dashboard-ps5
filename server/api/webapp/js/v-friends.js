// Amici: elenco con presenza, richieste, ricerca, suggerimenti; profilo utente.
import { t } from './i18n.js';
import { get, post, E } from './api.js';
import { add,
  h, icon, iconBtn, avatar, apiImg, replace, skeletonList, empty, errorState, relTime, presenceKind, presenceText,
  tabs, btn, busy, toast, errorText, duration, gameIcon, num, section,
} from './ui.js';
import { state } from './state.js';
import {
  userRow, friendRequest, acceptRequest, declineRequest, removeFriend, userActionsSheet, inviteToParty, presenceSub,
} from './common.js';
import { go, userPath, chatPath } from './nav.js';

export async function friends(ctx) {
  const { page, query } = ctx;
  let tab = ['list', 'requests', 'search'].includes(query.tab) ? query.tab : 'list';
  const body = h('div');
  const reqCount = () => state.counts.incoming_requests;
  const tabBar = () => tabs([
    { value: 'list', label: t('friends.tabList'), icon: 'friends' },
    { value: 'requests', label: t('friends.tabRequests'), icon: 'useradd', badge: reqCount() },
    { value: 'search', label: t('friends.tabSearch'), icon: 'search' },
  ], tab, (v) => { tab = v; history.replaceState(null, '', `#/friends?tab=${v}`); draw(); });
  let tb = tabBar();
  add(page, tb, body);
  ctx.setActions(iconBtn('useradd', t('friends.find'), () => { tab = 'search'; const n = tabBar(); tb.replaceWith(n); tb = n; draw(); }));

  let data = null;
  async function load() {
    data = await get('/friends');
    return data;
  }

  async function draw() {
    if (tab === 'list') return drawList();
    if (tab === 'requests') return drawRequests();
    return drawSearch();
  }

  async function drawList() {
    if (!data) replace(body, skeletonList(6));
    try { if (!data) await load(); } catch (e) { replace(body, errorState(e, () => { data = null; drawList(); })); return; }
    if (tab !== 'list') return;
    const list = data.friends;
    if (!list.length) {
      replace(body, empty('friends', t('friends.none'), t('friends.noneText'), btn(t('friends.find'), { icon: 'search', onclick: () => { tab = 'search'; const n = tabBar(); tb.replaceWith(n); tb = n; draw(); } })));
      return;
    }
    const q = h('input', { class: 'input', type: 'search', placeholder: t('friends.filter'), 'aria-label': t('friends.filter') });
    const groupsEl = h('div');
    const paint = () => {
      const f = q.value.trim().toLowerCase();
      const rows = list.filter((x) => !f || x.online_id.toLowerCase().includes(f));
      const playing = rows.filter((x) => presenceKind(x.presence) === 'playing');
      const online = rows.filter((x) => ['online', 'away', 'dnd'].includes(presenceKind(x.presence)));
      const offline = rows.filter((x) => presenceKind(x.presence) === 'offline')
        .sort((a, b) => (Date.parse(b.presence.last_seen || 0) || 0) - (Date.parse(a.presence.last_seen || 0) || 0));
      const grp = (title, arr) => arr.length ? section(`${title} · ${arr.length}`, h('div', { class: 'list' }, arr.map((u) => userRow(u, {
        trail: h('span', { class: 'friend-actions' },
          iconBtn('chat', t('user.message'), () => go(chatPath(u.online_id))),
          iconBtn('more', t('common.more'), () => userActionsSheet(u, 'friend', () => { data = null; drawList(); }))),
      })))) : null;
      replace(groupsEl, grp(t('friends.playing'), playing), grp(t('friends.online'), online), grp(t('friends.offline'), offline),
        !rows.length ? empty('search', t('friends.noMatch'), null) : null);
    };
    q.addEventListener('input', paint);
    replace(body, h('div', { class: 'search' }, icon('search', 18), q), groupsEl);
    paint();
  }

  async function drawRequests() {
    replace(body, skeletonList(3));
    try { await load(); } catch (e) { replace(body, errorState(e, drawRequests)); return; }
    if (tab !== 'requests') return;
    const inc = data.incoming_requests; const out = data.outgoing_requests;
    if (!inc.length && !out.length) { replace(body, empty('useradd', t('friends.noRequests'), t('friends.noRequestsText'))); return; }
    replace(body,
      inc.length ? section(t('friends.incoming'), h('div', { class: 'list' }, inc.map((u) => {
        const row = h('div', { class: 'lrow' },
          h('a', { href: `#${userPath(u.online_id)}` }, avatar(u, 46)),
          h('a', { class: 'lr-main', href: `#${userPath(u.online_id)}`, style: { color: 'inherit' } }, h('span', { class: 'lr-title', text: u.online_id }), h('span', { class: 'lr-sub', text: t('friends.wantsFriend', { when: relTime(u.since) }) })),
          h('span', { class: 'friend-actions' },
            btn(t('friends.accept'), { size: 'sm', onclick: async (e) => { if (await acceptRequest(u.online_id)) { data = null; drawRequests(); } } }),
            iconBtn('close', t('friends.decline'), async () => { if (await declineRequest(u.online_id)) { toast(t('friends.declined')); data = null; drawRequests(); } })));
        return row;
      }))) : null,
      out.length ? section(t('friends.outgoing'), h('div', { class: 'list' }, out.map((u) => userRow(u, {
        sub: t('friends.sentWhen', { when: relTime(u.since) }),
        trail: btn(t('common.cancel'), { size: 'sm', kind: 'ghost', onclick: async () => { if (await removeFriend(u.online_id, true)) { data = null; drawRequests(); } } }),
      })))) : null);
  }

  async function drawSearch() {
    const q = h('input', { class: 'input', type: 'search', placeholder: t('friends.searchPh'), 'aria-label': t('friends.searchPh'), autocapitalize: 'none', spellcheck: 'false', enterkeyhint: 'search' });
    const results = h('div');
    const sugg = h('div', {}, skeletonList(2, 'card'));
    replace(body, h('div', { class: 'search' }, icon('search', 18), q), results, section(t('friends.suggestions'), sugg));
    let timer = null; let seq = 0;
    q.addEventListener('input', () => { clearTimeout(timer); timer = setTimeout(runSearch, 280); });
    async function runSearch() {
      const s = q.value.trim();
      const my = ++seq;
      if (!s) { replace(results); return; }
      replace(results, h('div', { class: 'mt12' }, skeletonList(3)));
      try {
        const r = await get(`/users/search?q=${E(s)}`);
        if (my !== seq) return;
        if (!r.users.length) { replace(results, empty('search', t('friends.noResults', { q: s }), t('friends.noResultsText'))); return; }
        replace(results, section(t('friends.results'), h('div', { class: 'list' }, r.users.map((u) => userRow(u, { sub: t(`rel.${u.relation}`), presence: false, trail: relButton(u) })))));
      } catch (e) { if (my === seq) replace(results, errorState(e, runSearch)); }
    }
    try {
      const r = await get('/friends/suggestions');
      if (!r.users.length) { replace(sugg, h('div', { class: 'card' }, empty('sparkle', t('friends.noSuggest'), t('friends.noSuggestText')))); sugg.querySelector('.empty').classList.add('compact'); return; }
      replace(sugg, h('div', { class: 'suggest-grid' }, r.users.map((u) => {
        const b = btn(t('user.add'), { size: 'sm', icon: 'useradd', block: true });
        b.addEventListener('click', () => busy(b, async () => { try { await friendRequest(u.online_id); b.replaceWith(h('span', { class: 'pill pill-ok', text: t('rel.outgoing') })); } catch (e) { toast(errorText(e), { kind: 'error' }); } }));
        return h('div', { class: 'suggest' }, h('a', { href: `#${userPath(u.online_id)}` }, avatar(u, 64)), h('strong', { text: u.online_id }),
          h('small', { text: u.reason === 'amici_in_comune' ? t('friends.mutualN', { n: u.mutual }) : t('friends.sameGames') }), b);
      })));
    } catch (e) { replace(sugg, errorState(e, drawSearch)); }
    if (matchMedia('(pointer: fine)').matches) q.focus();
  }

  function relButton(u) {
    if (u.relation === 'friend') return h('span', { class: 'pill pill-ok', text: t('rel.friend') });
    if (u.relation === 'outgoing') return h('span', { class: 'pill', text: t('rel.outgoing') });
    if (u.relation === 'incoming') { const b = btn(t('friends.accept'), { size: 'sm' }); b.onclick = () => busy(b, async () => { if (await acceptRequest(u.online_id)) b.replaceWith(h('span', { class: 'pill pill-ok', text: t('rel.friend') })); }); return b; }
    if (u.relation === 'blocked') return h('span', { class: 'pill pill-danger', text: t('rel.blocked') });
    const b = btn(t('user.add'), { size: 'sm', kind: 'soft', icon: 'useradd' });
    b.onclick = () => busy(b, async () => { try { await friendRequest(u.online_id); b.replaceWith(h('span', { class: 'pill', text: t('rel.outgoing') })); } catch (e) { toast(errorText(e), { kind: 'error' }); } });
    return b;
  }

  ctx.onRefresh(async () => { data = null; await draw(); });
  ctx.on('sync', () => { const n = tabBar(); tb.replaceWith(n); tb = n; if (tab === 'list' && data) { data = null; drawList(); } });
  await draw();
}

// ------------------------------------------------------------ profilo --
export async function user(ctx) {
  const { page, params } = ctx;
  const oid = params.id;
  ctx.setTitle(oid);
  add(page, skeletonList(4));
  let u;
  async function load() {
    u = await get(`/users/${E(oid)}`);
    draw();
  }
  function draw() {
    const self = u.relation === 'self';
    const p = u.presence || {};
    const kind = presenceKind(p);
    const cover = h('div', { class: 'profile-cover' }, u.cover_media ? apiImg(`/media/${u.cover_media}/1`, '') : null);
    const actions = h('div', { class: 'profile-actions' });
    if (self) add(actions, btn(t('me.editProfile'), { kind: 'ghost', icon: 'edit', onclick: () => go('/me') }));
    else {
      if (u.relation === 'friend') {
        add(actions, btn(t('user.message'), { icon: 'chat', onclick: () => go(chatPath(u.online_id)) }),
          btn(t('user.inviteParty'), { kind: 'ghost', icon: 'party', onclick: () => inviteToParty(u.online_id) }));
      } else if (u.relation === 'none') {
        const b = btn(t('user.add'), { icon: 'useradd' });
        b.onclick = () => busy(b, async () => { try { await friendRequest(u.online_id); await load(); } catch (e) { toast(errorText(e), { kind: 'error' }); } });
        add(actions, b, btn(t('user.message'), { kind: 'ghost', icon: 'chat', onclick: () => go(chatPath(u.online_id)) }));
      } else if (u.relation === 'incoming') {
        const b = btn(t('friends.accept'), { icon: 'usercheck' });
        b.onclick = () => busy(b, async () => { if (await acceptRequest(u.online_id)) await load(); });
        add(actions, b, btn(t('friends.decline'), { kind: 'ghost', onclick: async () => { if (await declineRequest(u.online_id)) await load(); } }));
      } else if (u.relation === 'outgoing') {
        add(actions, h('span', { class: 'pill', style: { height: '44px', padding: '0 16px' } }, icon('clock', 16), t('rel.outgoing')));
      } else if (u.relation === 'blocked') {
        add(actions, h('span', { class: 'pill pill-danger', style: { height: '44px', padding: '0 16px' } }, icon('ban', 16), t('rel.blocked')));
      }
      add(actions, h('button', { class: 'btn btn-ghost btn-square', type: 'button', 'aria-label': t('common.more'), onclick: () => userActionsSheet(u, u.relation, load) }, icon('more', 20)));
      ctx.setActions(iconBtn('more', t('common.more'), () => userActionsSheet(u, u.relation, load)));
    }
    const statusLine = h('div', { class: 'row', style: { justifyContent: 'center', flexWrap: 'wrap', gap: '8px' } },
      h('span', { class: `pill ${kind === 'playing' ? 'pill-play' : kind === 'online' ? 'pill-ok' : kind === 'away' ? 'pill-warn' : kind === 'dnd' ? 'pill-danger' : ''}` },
        kind === 'playing' ? icon('gamepad', 14) : h('span', { class: `pdot-i p-${kind}`, style: { width: '8px', height: '8px', borderRadius: '50%', display: 'block' } }),
        presenceText(p, { long: true })),
      u.status_message ? h('span', { class: 'pill', text: `“${u.status_message}”` }) : null);

    const tr = u.trophies || {};
    const st = u.stats || {};
    const content = [
      cover,
      h('div', { class: 'profile-head' },
        avatar(u, 104, { presence: self || u.relation === 'friend' ? p : null }),
        h('h2', { text: u.online_id }),
        statusLine,
        u.about_me ? h('p', { class: 'profile-bio', text: u.about_me }) : null,
        h('div', { class: 'profile-stats' },
          h('div', { class: 'pstat' }, h('b', { text: num(u.friends_count) }), h('span', { text: t('user.friends') })),
          !self ? h('div', { class: 'pstat' }, h('b', { text: num(u.mutual_friends) }), h('span', { text: t('user.mutual') })) : null,
          !tr.hidden ? h('div', { class: 'pstat' }, h('b', { text: num(tr.points || 0) }), h('span', { text: t('user.points') })) : null,
          !st.hidden ? h('div', { class: 'pstat' }, h('b', { text: duration(st.total_seconds) }), h('span', { text: t('user.played') })) : null),
        actions),
    ];
    const grid = h('div', { class: 'grid2 mt16' });
    const left = h('div'); const right = h('div');
    if (u.games && u.games.length) {
      add(left, section(t('user.recentGames'), h('div', { class: 'list' }, u.games.map((g) => h('a', { class: 'lrow', href: `#/game/${E(g.game_id)}?name=${E(g.game_name || '')}` },
        gameIcon(g, 44), h('span', { class: 'lr-main' }, h('span', { class: 'lr-title', text: g.game_name }), h('span', { class: 'lr-sub', text: t('user.lastPlayed', { when: relTime(g.last_played) }) })), icon('chev', 18, 'lr-chev'))))));
    }
    if (st.top_games && st.top_games.length) {
      const max = Math.max(...st.top_games.map((g) => g.seconds), 1);
      add(left, section(t('user.mostPlayed'), h('div', { class: 'card' }, st.top_games.map((g) => h('div', { class: 'bar-row' }, gameIcon(g, 44),
        h('div', { class: 'bar-main' }, h('div', { class: 'bar-top' }, h('strong', { text: g.game_name }), h('span', { text: duration(g.seconds) })), h('div', { class: 'progress' }, h('span', { style: { width: `${Math.max(4, (g.seconds / max) * 100)}%` } }))))))));
    }
    if (u.recent && u.recent.length) {
      add(left, section(t('home.activity'), h('div', { class: 'card' }, u.recent.map((a) => h('div', { class: 'act' },
        h('span', { class: 'lr-ic c-play' }, icon(a.type === 'game_start' ? 'gamepad' : 'sparkle', 18)),
        h('div', { class: 'act-txt' }, a.type === 'game_start' ? t('act.game', { game: a.game_name || a.game_id }) : t('act.online'), h('div', { class: 'act-time', text: relTime(a.created_at) })))))));
    }
    if (!tr.hidden) {
      add(right, section(t('trophies.title'), { action: h('a', { class: 'sec-link', href: `#/trophies/${E(u.online_id)}` }, t('common.all'), icon('chev', 16)) },
        h('a', { class: 'card', href: `#/trophies/${E(u.online_id)}`, style: { display: 'block', color: 'inherit' } },
          h('div', { class: 'row', style: { justifyContent: 'space-between' } },
            h('div', {}, h('div', { class: 'stat-tile', style: { padding: 0, border: 0, background: 'none' } }, h('span', { class: 'k', text: t('trophies.points') }), h('span', { class: 'v', text: num(tr.points || 0) }))),
            h('div', { class: 'trophy-sum' }, ['p', 'g', 's', 'b'].map((g) => h('span', { class: `tgrade g-${g}`, title: t(`grade.${g}`) }, icon('trophy', 18), num(tr[g] || 0))))),
          h('p', { class: 'small muted mt8', text: tr.sets ? t('trophies.setsN', { n: tr.sets }) : t('trophies.noneYet') }))));
    }
    if (u.mutual && u.mutual.length) {
      add(right, section(t('user.mutualFriends'), h('div', { class: 'list' }, u.mutual.map((m) => userRow(m, { sub: '', presence: false })))));
    }
    if (!self && u.relation !== 'friend' && u.relation !== 'blocked') {
      add(right, section(null, h('div', { class: 'note' }, icon('lock', 18), h('span', { text: t('user.privateNote') }))));
    }
    add(grid, left, right);
    content.push(grid);
    replace(page, ...content);
  }
  ctx.onRefresh(load);
  try { await load(); } catch (e) {
    if (e.code === 'blocked' || e.code === 'account_not_found') replace(page, empty('user', e.code === 'blocked' ? t('user.unavailable') : t('user.notFound'), null, btn(t('common.back'), { kind: 'ghost', onclick: () => history.back() })));
    else replace(page, errorState(e, () => user(ctx)));
  }
}
