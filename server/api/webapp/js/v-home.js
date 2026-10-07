// Home: saluto, stato, la mia console, amici online, attività, notizie.
import { t } from './i18n.js';
import { get, post } from './api.js';
import { add, h, icon, avatar, replace, skeletonList, empty, errorState, relTime, presenceKind, badge, toast, errorText, gameIcon, btn } from './ui.js';
import { state } from './state.js';
import { consoleCard, newsItem, statusSheet, STATUS } from './common.js';
import { userPath } from './nav.js';

function greeting() {
  const hr = new Date().getHours();
  return hr < 5 ? t('home.hiNight') : hr < 13 ? t('home.hiMorning') : hr < 18 ? t('home.hiAfternoon') : t('home.hiEvening');
}

export async function home(ctx) {
  const { page } = ctx;
  const hello = h('div', { class: 'hello' });
  const quick = h('div', { class: 'quick' });
  const consoleBox = h('div', {}, consoleCard(null));
  const invitesBox = h('div');
  const onlineBox = h('div', {}, skeletonList(1));
  const activityBox = h('div', { class: 'card' }, skeletonList(4));
  const newsBox = h('div', { class: 'news-list' }, skeletonList(3, 'card'));

  add(page, 
    hello, quick,
    h('div', { class: 'grid2 mt8' },
      h('div', {},
        h('section', { class: 'sec' }, consoleBox),
        invitesBox,
        h('section', { class: 'sec' }, h('div', { class: 'sec-head' }, h('h2', { class: 'sec-title', text: t('home.onlineNow') }), h('a', { class: 'sec-link', href: '#/friends' }, t('home.allFriends'), icon('chev', 16))), onlineBox),
        h('section', { class: 'sec' }, h('div', { class: 'sec-head' }, h('h2', { class: 'sec-title', text: t('home.activity') })), activityBox)),
      h('div', {},
        h('section', { class: 'sec' }, h('div', { class: 'sec-head' }, h('h2', { class: 'sec-title', text: t('home.news') })), newsBox))));

  function drawHello() {
    const me = state.me || { online_id: state.account ? state.account.online_id : '' };
    const st = STATUS.find((s) => s.mode === (me.status_mode || 'online')) || STATUS[0];
    replace(hello,
      h('a', { href: `#${userPath(me.online_id || '')}`, 'aria-label': t('nav.me') }, avatar(me, 58)),
      h('div', { class: 'hello-txt' },
        h('h2', {}, greeting(), ', ', h('span', { class: 'grad-text', text: me.online_id || '' })),
        h('button', { class: 'status-btn', type: 'button', onclick: () => statusSheet(drawHello) },
          h('span', { class: `pdot-i ${st.cls}` }), h('span', { text: me.status_message ? `${t(`status.${st.mode}`)} · ${me.status_message}` : t(`status.${st.mode}`) }))));
  }
  function drawQuick() {
    const c = state.counts;
    replace(quick,
      h('a', { class: 'quick-btn', href: '#/messages' }, h('span', { class: 'quick-ic q1' }, icon('chat', 22)), t('nav.messages'), badge(c.unread_messages)),
      h('a', { class: 'quick-btn', href: '#/party' }, h('span', { class: 'quick-ic q2' }, icon('party', 22)), t('nav.party'), badge(state.partyInvites.length)),
      h('a', { class: 'quick-btn', href: '#/community?compose=1' }, h('span', { class: 'quick-ic q3' }, icon('edit', 22)), t('home.qPost')),
      h('a', { class: 'quick-btn', href: '#/store' }, h('span', { class: 'quick-ic q4' }, icon('store', 22)), t('nav.store')));
  }
  function drawOnline() {
    if (!state.synced) return;
    const on = state.friends.filter((f) => presenceKind(f.presence) !== 'offline');
    if (!state.friends.length) {
      replace(onlineBox, h('div', { class: 'card' }, empty('friends', t('home.noFriends'), t('home.noFriendsText'), btn(t('friends.find'), { kind: 'soft', icon: 'useradd', onclick: () => ctx.nav('/friends?tab=search') }))));
      return;
    }
    if (!on.length) {
      replace(onlineBox, h('div', { class: 'card row' }, h('span', { class: 'lr-ic' }, icon('moon', 20)), h('div', { class: 'lr-main' }, h('span', { class: 'lr-title', text: t('home.nobodyOnline') }), h('span', { class: 'lr-sub', text: t('home.nobodyOnlineText', { n: state.friends.length }) }))));
      return;
    }
    replace(onlineBox, h('div', { class: 'friends-strip' }, on.map((f) => {
      const playing = presenceKind(f.presence) === 'playing';
      const av = avatar(f, 60, { presence: f.presence });
      return h('a', { class: 'fs-item', href: `#${userPath(f.online_id)}` },
        playing ? h('span', { class: 'fs-ring' }, av) : av,
        h('span', { class: 'fs-name', text: f.online_id }),
        playing ? h('span', { class: 'fs-game', text: f.presence.game_name || f.presence.game_id }) : null);
    })));
  }
  function drawInvites() {
    const inv = state.partyInvites;
    if (!inv.length) { replace(invitesBox); return; }
    replace(invitesBox, h('section', { class: 'sec' }, h('div', { class: 'list' }, inv.map((i) => h('div', { class: 'lrow' },
      h('span', { class: 'lr-ic c-play' }, icon('party', 20)),
      h('span', { class: 'lr-main' }, h('span', { class: 'lr-title', text: t('party.inviteFrom', { name: i.from_online_id }) }), h('span', { class: 'lr-sub', text: `${i.name} · ${t('party.membersN', { n: i.members })}` })),
      h('span', { class: 'friend-actions' },
        btn(t('party.join'), { size: 'sm', onclick: async (e) => { try { await post('/party/join', { party_id: i.party_id }); ctx.nav('/party'); } catch (ex) { toast(errorText(ex), { kind: 'error' }); } } }),
        btn(t('party.decline'), { size: 'sm', kind: 'ghost', onclick: async () => { try { await post('/party/decline', { party_id: i.party_id }); state.partyInvites = state.partyInvites.filter((x) => x !== i); drawInvites(); } catch (ex) { toast(errorText(ex), { kind: 'error' }); } } })))))));
  }

  async function loadConsole() {
    try { replace(consoleBox, consoleCard(await get('/console'), { compactMode: false })); }
    catch (e) { replace(consoleBox, consoleCard({ paired: false })); }
  }
  async function loadActivity() {
    try {
      const r = await get('/activity');
      const items = r.activity || [];
      if (!items.length) { replace(activityBox, empty('sparkle', t('home.noActivity'), t('home.noActivityText'))); activityBox.firstChild.classList.add('compact'); return; }
      let limit = 8;
      const draw = () => {
        replace(activityBox, items.slice(0, limit).map((a) => {
          const isGame = a.type === 'game_start';
          const txt = isGame ? t('act.game', { game: a.game_name || a.game_id }) : a.type === 'online' ? t('act.online') : (a.detail || a.type);
          return h('a', { class: 'act', href: `#${isGame && a.game_id ? `/game/${encodeURIComponent(a.game_id)}?name=${encodeURIComponent(a.game_name || '')}` : userPath(a.online_id)}`, style: { color: 'inherit' } },
            h('span', { class: 'act-av' }, avatar(a, 40), isGame ? h('span', { class: 'act-ic' }, icon('gamepad', 12)) : null),
            h('div', { class: 'act-txt' }, h('strong', { text: a.online_id }), ' ', txt, h('div', { class: 'act-time', text: relTime(a.created_at) })));
        }), items.length > limit ? h('div', { class: 'load-more' }, btn(t('common.showMore'), { kind: 'ghost', size: 'sm', onclick: () => { limit += 12; draw(); } })) : null);
      };
      draw();
    } catch (e) { replace(activityBox, errorState(e, loadActivity)); }
  }
  async function loadNews() {
    try {
      const r = await get('/news');
      const items = (r.news || []).slice(0, 9);
      if (!items.length) { replace(newsBox, h('div', { class: 'card' }, empty('globe', t('home.noNews'), null))); return; }
      replace(newsBox, newsItem(items[0], true), items.slice(1).map((n) => newsItem(n)));
    } catch (e) { replace(newsBox, errorState(e, loadNews)); }
  }

  drawHello(); drawQuick(); drawOnline(); drawInvites();
  ctx.on('sync', () => { drawHello(); drawQuick(); drawOnline(); drawInvites(); });
  ctx.onRefresh(() => Promise.all([loadConsole(), loadActivity(), loadNews()]));
  ctx.every(30000, loadConsole);
  ctx.every(60000, loadActivity);
  await Promise.all([loadConsole(), loadActivity(), loadNews()]);
}
