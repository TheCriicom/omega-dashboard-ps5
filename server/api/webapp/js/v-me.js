// Profilo / Altro, impostazioni (privacy, account, app), notifiche e le loro
// preferenze, informazioni sul server, "La mia console".
import { t, LANG } from './i18n.js';
import { auth, api, get, post, del, E } from './api.js';
import { add,
  h, icon, iconBtn, avatar, apiImg, avatarColors, replace, skeletonList, empty, errorState, relTime, btn, busy, toast, errorText,
  section, listRow, segmented, toggle, sheet, formSheet, confirmDialog, gameIcon, num, badge, vibrate,
} from './ui.js';
import { state, emit, pref, setPref } from './state.js';
import { statusSheet, STATUS, consoleCard, userRow } from './common.js';
import { go, userPath, notifTarget } from './nav.js';
import { canInstall, promptInstall, isStandalone } from './install.js';

// ---------------------------------------------------------------- me --
export async function me(ctx) {
  const { page } = ctx;
  const head = h('div');
  add(page, head, h('div', { class: 'grid2' },
    h('div', {},
      section(t('me.you'), h('div', { class: 'list' },
        listRow({ icon: 'user', iconCls: 'c-accent', title: t('me.publicProfile'), sub: t('me.publicProfileSub'), onClick: () => go(userPath(auth.onlineId || (state.me && state.me.online_id) || '')) }),
        listRow({ icon: 'console', iconCls: 'c-play', title: t('console.title'), sub: t('me.consoleSub'), href: '#/console' }),
        listRow({ icon: 'party', iconCls: 'c-play', title: t('nav.party'), sub: state.party ? state.party.name : t('me.partySub'), href: '#/party', trail: badge(state.partyInvites.length) }),
        listRow({ icon: 'chat', iconCls: 'c-accent', title: t('nav.messages'), href: '#/messages', trail: badge(state.counts.unread_messages) }),
        listRow({ icon: 'trophy', iconCls: 'c-warn', title: t('trophies.mine'), href: `#/trophies/${E(auth.onlineId || '')}` }),
        listRow({ icon: 'clock', iconCls: 'c-ok', title: t('stats.title'), href: '#/stats' }))),
      section(t('me.settings'), h('div', { class: 'list' },
        listRow({ icon: 'bell', iconCls: 'c-danger', title: t('nprefs.title'), sub: t('me.notifSub'), href: '#/settings/notifications' }),
        listRow({ icon: 'shield', iconCls: 'c-ok', title: t('privacy.title'), sub: t('me.privacySub'), href: '#/settings/privacy' }),
        listRow({ icon: 'ban', title: t('blocked.title'), href: '#/settings/blocked' }),
        listRow({ icon: 'sliders', iconCls: 'c-accent', title: t('appset.title'), sub: t('me.appSub'), href: '#/settings/app' }),
        listRow({ icon: 'key', title: t('account.title'), sub: t('me.accountSub'), href: '#/settings/account' })))),
    h('div', {},
      section(t('me.omega'), h('div', { class: 'list' },
        canInstall() ? listRow({ icon: 'phone', iconCls: 'c-accent', title: t('pwa.title'), sub: t('pwa.banner'), onClick: promptInstall }) : null,
        listRow({ icon: 'install', iconCls: 'c-play', title: t('me.installConsole'), sub: t('me.installConsoleSub'), href: '/installa' }),
        listRow({ icon: 'mail', iconCls: 'c-warn', title: t('feedback.title'), sub: t('feedback.sub'), onClick: feedback }),
        listRow({ icon: 'info', title: t('about.title'), href: '#/about' }),
        listRow({ icon: 'lock', title: t('auth.privacy'), href: '/legal/privacy' }),
        listRow({ icon: 'info', title: t('auth.terms'), href: '/legal/terms' }))),
      section(null, h('div', { class: 'list' }, listRow({ icon: 'logout', iconCls: 'c-danger', title: t('nav.logout'), danger: true, chevron: false, onClick: logout }))),
      h('p', { class: 'small muted center mt16', text: t('me.footer') }))));

  function drawHead() {
    const m = state.me || { online_id: auth.onlineId || '' };
    const st = STATUS.find((s) => s.mode === (m.status_mode || 'online')) || STATUS[0];
    const av = h('button', { type: 'button', class: 'avatar-edit', 'aria-label': t('me.changeAvatar'), onclick: avatarSheet, style: { position: 'relative', borderRadius: '50%' } },
      avatar(m, 76), h('span', { class: 'lr-ic c-accent', style: { position: 'absolute', right: '-4px', bottom: '-4px', width: '28px', height: '28px', borderRadius: '50%', boxShadow: '0 0 0 3px var(--card)' } }, icon('camera', 15)));
    replace(head, h('div', { class: 'me-card mt8' }, av,
      h('div', { class: 'me-txt' }, h('h2', { text: m.online_id }),
        h('p', { text: m.about_me || t('me.noBio') }),
        h('div', { class: 'row', style: { gap: '8px', marginTop: '8px', flexWrap: 'wrap' } },
          h('button', { class: 'status-btn', type: 'button', style: { marginTop: 0 }, onclick: () => statusSheet(drawHead) }, h('span', { class: `pdot-i ${st.cls}` }), h('span', { text: m.status_message ? `${t(`status.${st.mode}`)} · ${m.status_message}` : t(`status.${st.mode}`) })),
          h('button', { class: 'status-btn', type: 'button', style: { marginTop: 0 }, onclick: editBio }, icon('edit', 14), h('span', { text: t('me.editBio') }))))));
  }
  async function editBio() {
    const v = await formSheet({ title: t('me.editBio'), fields: [{ name: 'bio', label: t('me.bio'), type: 'textarea', max: 160, value: (state.me && state.me.about_me) || '', placeholder: t('me.bioPh'), hint: t('me.bioHint') }],
      onSubmit: async (vals) => { await post('/profile', { about_me: vals.bio }); return vals; } });
    if (v) { state.me = { ...state.me, about_me: v.bio.trim() || null }; drawHead(); toast(t('common.saved'), { kind: 'ok' }); emit('resync'); }
  }
  ctx.on('sync', drawHead);
  drawHead();
}

function avatarSheet() {
  sheet({
    title: t('me.changeAvatar'),
    content: (close) => {
      const cur = (state.me && state.me.avatar) || 0;
      const pick = h('div', { class: 'av-pick' }, Array.from({ length: 16 }, (_, i) => h('button', { type: 'button', 'aria-pressed': String((cur & 15) === i && !(state.me && state.me.avatar_media)), 'aria-label': t('me.colorN', { n: i + 1 }),
        onclick: async () => {
          try {
            await post('/profile', { avatar: i });
            if (state.me && state.me.avatar_media) await post('/media/clear?kind=avatar');
            state.me = { ...state.me, avatar: i, avatar_media: null }; emit('badges'); emit('sync', {}); emit('resync'); close(); toast(t('common.saved'), { kind: 'ok' });
          } catch (e) { toast(errorText(e), { kind: 'error' }); }
        } }, avatar({ online_id: state.me && state.me.online_id, avatar: i }, 52))));
      const file = h('input', { type: 'file', accept: 'image/jpeg,image/png,image/webp,video/mp4,video/webm', hidden: true });
      const up = btn(t('me.uploadPhoto'), { icon: 'camera', block: true });
      up.addEventListener('click', () => file.click());
      file.addEventListener('change', () => {
        const f = file.files && file.files[0];
        if (!f) return;
        const ext = (f.name.split('.').pop() || 'jpg').toLowerCase();
        busy(up, async () => {
          try {
            await api(`/media/upload?kind=avatar&ext=${E(ext)}`, { method: 'POST', body: f, raw: true });
            toast(t('me.photoSaved'), { kind: 'ok' }); emit('resync'); close();
          } catch (e) { toast(errorText(e), { kind: 'error' }); }
        });
      });
      const coverFile = h('input', { type: 'file', accept: 'image/jpeg,image/png,image/webp', hidden: true });
      const cov = btn(t('me.uploadCover'), { icon: 'upload', kind: 'ghost', block: true });
      cov.addEventListener('click', () => coverFile.click());
      coverFile.addEventListener('change', () => {
        const f = coverFile.files && coverFile.files[0];
        if (!f) return;
        const ext = (f.name.split('.').pop() || 'jpg').toLowerCase();
        busy(cov, async () => {
          try { await api(`/media/upload?kind=cover&ext=${E(ext)}`, { method: 'POST', body: f, raw: true }); toast(t('me.coverSaved'), { kind: 'ok' }); emit('resync'); close(); }
          catch (e) { toast(errorText(e), { kind: 'error' }); }
        });
      });
      return h('div', { class: 'stack' }, up, file, cov, coverFile, h('p', { class: 'small muted', text: t('me.photoHint') }),
        h('div', { class: 'divider', style: { margin: '6px 0' } }), h('strong', { text: t('me.orColor') }), pick);
    },
  });
}

async function feedback() {
  const v = await formSheet({
    title: t('feedback.title'), subtitle: t('feedback.sub'), submit: t('common.send'),
    fields: [
      { name: 'kind', label: t('feedback.kind'), type: 'select', value: 'idea', options: [{ value: 'idea', label: t('feedback.idea') }, { value: 'bug', label: t('feedback.bug') }] },
      { name: 'text', label: t('feedback.text'), type: 'textarea', rows: 5, max: 2000, required: true, placeholder: t('feedback.textPh') },
    ],
    onSubmit: async (vals) => { await post('/feedback', { kind: vals.kind, text: vals.text, app_version: 'web', lang: LANG }); return vals; },
  });
  if (v) toast(t('feedback.thanks'), { kind: 'ok' });
}

export async function logout() {
  if (!(await confirmDialog({ title: t('nav.logoutQ'), ok: t('nav.logout'), danger: true }))) return;
  try { await post('/auth/logout'); } catch { /* il token scade comunque */ }
  auth.clear();
  emit('logout');
}

// ------------------------------------------------------------ privacy --
export async function privacy(ctx) {
  const { page } = ctx;
  add(page, skeletonList(5));
  let p;
  async function save(patch, okMsg) {
    try { p = await post('/privacy', patch); toast(okMsg || t('common.saved'), { kind: 'ok' }); draw(); }
    catch (e) { toast(errorText(e), { kind: 'error' }); draw(); }
  }
  function choice(title, sub, key, opts) {
    return h('div', { class: 'np-type' }, h('div', { class: 'np-head' }, h('div', { class: 'np-txt' }, h('strong', { text: title }), h('small', { text: sub }))),
      segmented(opts.map(([v, l]) => ({ value: v, label: l })), p[key], (v) => save({ [key]: v }), { label: title, small: true }));
  }
  function sw(title, sub, key, confirmOff) {
    return listRow({ title, sub, chevron: false, trail: toggle(p[key], async (on, input) => {
      if (!on && confirmOff && !(await confirmDialog({ title: confirmOff.title, text: confirmOff.text, ok: confirmOff.ok, danger: true }))) { input.checked = true; return; }
      save({ [key]: on });
    }, title) });
  }
  function draw() {
    replace(page,
      section(t('privacy.who'), h('div', { class: 'list' },
        choice(t('privacy.messages'), t('privacy.messagesSub'), 'messages', [['everyone', t('privacy.everyone')], ['friends', t('privacy.friendsOnly')]]),
        choice(t('privacy.requests'), t('privacy.requestsSub'), 'friend_requests', [['everyone', t('privacy.everyone')], ['friends_of_friends', t('privacy.fof')], ['nobody', t('privacy.nobody')]]),
        choice(t('privacy.trophies'), t('privacy.trophiesSub'), 'trophies', [['everyone', t('privacy.everyone')], ['friends', t('privacy.friendsOnly')], ['nobody', t('privacy.nobody')]]))),
      section(t('privacy.visibility'), h('div', { class: 'list' },
        sw(t('privacy.activity'), t('privacy.activitySub'), 'show_activity'),
        sw(t('privacy.records'), t('privacy.recordsSub'), 'show_in_records'),
        sw(t('privacy.import'), t('privacy.importSub'), 'import_trophies', { title: t('privacy.importOffQ'), text: t('privacy.importOffText'), ok: t('privacy.importOff') }))),
      h('p', { class: 'small muted mt16', text: t('privacy.blockHint') }),
      h('a', { class: 'btn btn-ghost btn-sm mt8', href: '#/settings/blocked' }, icon('ban', 16), t('blocked.title')));
  }
  try { p = await get('/privacy'); draw(); } catch (e) { replace(page, errorState(e, () => privacy(ctx))); }
}

// ------------------------------------------------------------ bloccati --
export async function blocked(ctx) {
  const { page } = ctx;
  add(page, skeletonList(3));
  async function load() {
    try {
      const r = await get('/blocks');
      if (!r.users.length) { replace(page, empty('ban', t('blocked.empty'), t('blocked.emptyText'))); return; }
      replace(page, h('p', { class: 'small muted mt8', text: t('blocked.text') }), h('div', { class: 'list mt12' }, r.users.map((u) => userRow(u, {
        sub: '', presence: false,
        trail: btn(t('user.unblock'), { size: 'sm', kind: 'ghost', onclick: async () => { try { await del(`/users/${E(u.online_id)}/block`); toast(t('user.unblocked', { name: u.online_id }), { kind: 'ok' }); load(); } catch (e) { toast(errorText(e), { kind: 'error' }); } } }),
      }))));
    } catch (e) { replace(page, errorState(e, load)); }
  }
  ctx.onRefresh(load);
  await load();
}

// ------------------------------------------------------------- account --
export async function account(ctx) {
  const { page } = ctx;
  add(page, skeletonList(3));
  let a = state.account;
  try { a = await get('/me'); state.account = a; } catch (e) { if (!a) { replace(page, errorState(e, () => account(ctx))); return; } }
  replace(page,
    section(t('account.data'), h('div', { class: 'card' }, h('dl', { class: 'kv' },
      h('dt', { text: t('auth.onlineId') }), h('dd', { text: a.online_id }),
      h('dt', { text: t('account.email') }), h('dd', { text: a.email || t('account.noEmail') }),
      h('dt', { text: t('account.created') }), h('dd', { text: new Date(a.created_at).toLocaleDateString(LANG, { day: 'numeric', month: 'long', year: 'numeric' }) }),
      h('dt', { text: t('account.lastLogin') }), h('dd', { text: relTime(a.last_login_at) }),
      h('dt', { text: t('account.terms') }), h('dd', { text: a.terms && a.terms.accepted ? a.terms.accepted : '—' })))),
    section(t('account.security'), h('div', { class: 'list' },
      listRow({ icon: 'key', iconCls: 'c-accent', title: t('account.changePw'), sub: t('account.changePwSub'), onClick: changePassword }),
      listRow({ icon: 'logout', title: t('nav.logout'), onClick: logout }))),
    section(t('account.yourData'), h('div', { class: 'list' },
      listRow({ icon: 'install', iconCls: 'c-ok', title: t('account.export'), sub: t('account.exportSub'), onClick: exportData }),
      listRow({ icon: 'trash', iconCls: 'c-danger', title: t('account.delete'), sub: t('account.deleteSub'), danger: true, onClick: deleteAccount }))));
}
async function changePassword() {
  const v = await formSheet({
    title: t('account.changePw'), subtitle: t('account.changePwSub'), submit: t('account.changePwBtn'),
    fields: [
      { name: 'cur', label: t('account.curPw'), type: 'password', required: true, autocomplete: 'current-password' },
      { name: 'pw', label: t('account.newPw'), type: 'password', required: true, autocomplete: 'new-password', hint: t('auth.pwHint') },
      { name: 'pw2', label: t('auth.password2'), type: 'password', required: true, autocomplete: 'new-password' },
    ],
    onSubmit: async (vals) => {
      if (vals.pw !== vals.pw2) throw new Error(t('auth.pwMismatch'));
      return post('/auth/password', { current_password: vals.cur, new_password: vals.pw });
    },
  });
  if (v) toast(v.other_sessions_closed ? t('account.pwChangedN', { n: v.other_sessions_closed }) : t('account.pwChanged'), { kind: 'ok' });
}
async function exportData() {
  try {
    const data = await get('/account/export');
    const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = h('a', { href: url, download: `omega-${auth.onlineId || 'dati'}-${new Date().toISOString().slice(0, 10)}.json` });
    document.body.appendChild(a); a.click(); a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 10000);
    toast(t('account.exported'), { kind: 'ok' });
  } catch (e) { toast(errorText(e), { kind: 'error' }); }
}
async function deleteAccount() {
  if (!(await confirmDialog({ title: t('account.deleteQ'), text: t('account.deleteText'), ok: t('account.deleteContinue'), danger: true }))) return;
  const v = await formSheet({
    title: t('account.delete'), subtitle: t('account.deletePwSub'), submit: t('account.deleteForever'), danger: true,
    fields: [{ name: 'pw', label: t('auth.password'), type: 'password', required: true, autocomplete: 'current-password' }],
    onSubmit: (vals) => post('/account/delete', { password: vals.pw }),
  });
  if (v) { auth.clear(); toast(t('account.deleted')); emit('logout'); }
}

// ------------------------------------------------------- impostazioni app --
export async function appSettings(ctx) {
  const { page } = ctx;
  const theme = segmented([{ value: 'dark', label: t('appset.dark'), icon: 'moon' }, { value: 'auto', label: t('appset.auto') }, { value: 'light', label: t('appset.light'), icon: 'sun' }],
    pref('theme', 'dark'), (v) => { setPref('theme', v); emit('theme'); }, { label: t('appset.theme') });
  theme.classList.add('full');
  add(page, 
    section(t('appset.theme'), h('div', { class: 'card stack' }, theme, h('p', { class: 'small muted', text: t('appset.themeHint') }))),
    section(t('appset.language'), h('div', { class: 'card' }, h('p', { text: t('appset.languageText', { lang: LANG === 'it' ? 'Italiano' : 'English' }) }))),
    section(t('appset.app'), h('div', { class: 'list' },
      isStandalone() ? listRow({ icon: 'check', iconCls: 'c-ok', title: t('appset.installed'), chevron: false })
        : listRow({ icon: 'phone', iconCls: 'c-accent', title: t('pwa.title'), sub: t('pwa.banner'), onClick: promptInstall }),
      listRow({ icon: 'sparkle', title: t('appset.onboarding'), onClick: () => { import('./onboarding.js').then((m) => m.onboarding(() => {})); } }),
      listRow({ icon: 'refresh', title: t('appset.reload'), sub: t('appset.reloadSub'), onClick: async () => {
        try { const keys = await caches.keys(); await Promise.all(keys.map((k) => caches.delete(k))); } catch { /* niente */ }
        location.reload();
      } }))),
    section(t('appset.keyboard'), h('div', { class: 'card' }, h('p', { class: 'small muted', text: t('appset.keyboardText') }))));
}

// ---------------------------------------------------------------- info --
export async function about(ctx) {
  const { page } = ctx;
  add(page, skeletonList(3));
  try {
    const i = await get('');
    replace(page,
      h('div', { class: 'big-console mt8' }, h('div', { class: 'bc-ps', style: { background: 'var(--grad-soft)' } }, icon('mark', 54)), h('h2', { text: i.service || 'Omega' }), h('p', { class: 'muted', text: i.description || '' })),
      section(t('about.server'), h('div', { class: 'card' }, h('dl', { class: 'kv' },
        h('dt', { text: t('about.address') }), h('dd', { text: location.host }),
        h('dt', { text: t('about.lang') }), h('dd', { text: i.lang || LANG }),
        h('dt', { text: t('about.langs') }), h('dd', { text: String((i.languages || []).length) }),
        h('dt', { text: t('about.dev') }), h('dd', {}, i.developer ? h('a', { href: i.developer.url, target: '_blank', rel: 'noopener', text: i.developer.name }) : '—'),
        h('dt', { text: t('about.webapp') }), h('dd', { text: 'web 1.0' })))),
      section(t('about.legal'), h('div', { class: 'list' },
        listRow({ icon: 'info', title: t('auth.terms'), href: '/legal/terms' }),
        listRow({ icon: 'lock', title: t('auth.privacy'), href: '/legal/privacy' }),
        listRow({ icon: 'json', title: t('about.licenses'), href: '/legal/licenses' }),
        listRow({ icon: 'folder', title: t('auth.source'), href: '/source' }))),
      h('p', { class: 'small muted center mt16', text: t('about.notSony') }));
  } catch (e) { replace(page, errorState(e, () => about(ctx))); }
}

// ----------------------------------------------------------- notifiche --
const NICON = { message: 'chat', friend_request: 'useradd', friend_accept: 'usercheck', party_invite: 'party', game_invite: 'gamepad', game_start: 'gamepad', online: 'user', post_like: 'heart', post_comment: 'comment', store_comment: 'comment', store_recommend: 'gift', store_update: 'install' };
export async function notifications(ctx) {
  const { page } = ctx;
  const box = h('div', {}, skeletonList(6));
  add(page, box);
  ctx.setActions(iconBtn('settings', t('nprefs.title'), () => go('/settings/notifications')));
  let items = [];
  async function load() {
    try {
      const r = await get('/notifications');
      items = r.notifications || [];
      draw();
    } catch (e) { replace(box, errorState(e, load)); }
  }
  function draw() {
    const unread = items.filter((n) => !n.read).length;
    if (!items.length) {
      replace(box, empty('bell', t('notif.empty'), t('notif.emptyText'), btn(t('nprefs.title'), { kind: 'ghost', icon: 'settings', onclick: () => go('/settings/notifications') })));
      return;
    }
    const readAll = btn(t('notif.readAll'), { size: 'sm', kind: 'soft', icon: 'check', disabled: !unread });
    readAll.addEventListener('click', () => busy(readAll, async () => { try { await post('/notifications/read', {}); items.forEach((n) => { n.read = true; }); state.counts.unread_notifications = 0; emit('badges'); draw(); } catch (e) { toast(errorText(e), { kind: 'error' }); } }));
    const clearAll = btn(t('notif.clear'), { size: 'sm', kind: 'ghost', icon: 'trash' });
    clearAll.addEventListener('click', async () => {
      if (!(await confirmDialog({ title: t('notif.clearQ'), ok: t('notif.clear'), danger: true }))) return;
      try { await post('/notifications/clear'); items = []; state.counts.unread_notifications = 0; emit('badges'); draw(); } catch (e) { toast(errorText(e), { kind: 'error' }); }
    });
    const groups = [];
    const today = new Date().toDateString();
    const g1 = items.filter((n) => new Date(n.created_at).toDateString() === today);
    const g2 = items.filter((n) => new Date(n.created_at).toDateString() !== today);
    if (g1.length) groups.push([t('time.today'), g1]);
    if (g2.length) groups.push([t('notif.earlier'), g2]);
    replace(box, h('div', { class: 'row', style: { justifyContent: 'space-between', flexWrap: 'wrap' } },
      h('span', { class: 'muted small', text: unread ? t('notif.unreadN', { n: unread }) : t('notif.allRead') }), h('div', { class: 'row', style: { gap: '8px' } }, readAll, clearAll)),
    groups.map(([title, list]) => section(title, h('div', { class: 'list' }, list.map((n) => {
      const target = notifTarget(n);
      const row = h(target ? 'button' : 'div', { class: `notif ${n.read ? '' : 'unread'}`, type: target ? 'button' : undefined },
        n.actor ? h('span', { class: 'act-av' }, avatar({ online_id: n.actor, avatar: n.avatar }, 44), h('span', { class: 'act-ic' }, icon(NICON[n.type] || 'bell', 12))) : h('span', { class: 'notif-ic' }, icon(NICON[n.type] || 'bell', 20)),
        h('div', { class: 'notif-main' }, h('div', { class: 'notif-title', text: n.title }), n.body ? h('div', { class: 'notif-body', text: n.body }) : null,
          h('div', { class: 'notif-time', text: `${relTime(n.created_at)}${n.silent ? ` · ${t('notif.silent')}` : ''}` })));
      if (target) row.addEventListener('click', async () => {
        if (!n.read) { n.read = true; post('/notifications/read', { id: n.notification_id }).then(() => emit('resync')).catch(() => {}); }
        go(target);
      });
      return row;
    })))));
  }
  ctx.onRefresh(load);
  ctx.every(15000, load);
  await load();
}

// ---------------------------------------------- preferenze notifiche --
const NTYPES = [
  ['message', 'chat'], ['party_invite', 'party'], ['game_invite', 'gamepad'], ['friend_request', 'useradd'], ['friend_accept', 'usercheck'],
  ['online', 'user'], ['game_start', 'gamepad'], ['post_like', 'heart'], ['post_comment', 'comment'],
  ['store_comment', 'comment'], ['store_recommend', 'gift'], ['store_update', 'install'],
];
export async function notifPrefs(ctx) {
  const { page } = ctx;
  add(page, skeletonList(6));
  let p;
  let saving = Promise.resolve();
  function save(patch) {
    saving = saving.then(async () => {
      try { p = await post('/notifications/prefs', { ...patch, tz_offset: -new Date().getTimezoneOffset() }); flash(); }
      catch (e) { toast(errorText(e), { kind: 'error' }); }
    });
    return saving;
  }
  let flashTimer = null;
  const savedPill = h('span', { class: 'pill pill-ok', style: { opacity: 0, transition: 'opacity .2s' } }, icon('check', 14), t('common.saved'));
  function flash() { savedPill.style.opacity = 1; clearTimeout(flashTimer); flashTimer = setTimeout(() => { savedPill.style.opacity = 0; }, 1400); }
  ctx.setActions(savedPill);
  const modes = [{ value: 'all', label: t('nprefs.all') }, { value: 'favorites', label: t('nprefs.favorites') }, { value: 'off', label: t('nprefs.off') }];

  function draw() {
    const quietOn = p.quiet.enabled;
    const from = h('input', { class: 'input', type: 'time', value: p.quiet.from, disabled: !quietOn, 'aria-label': t('nprefs.from') });
    const to = h('input', { class: 'input', type: 'time', value: p.quiet.to, disabled: !quietOn, 'aria-label': t('nprefs.to') });
    from.addEventListener('change', () => { if (/^\d\d:\d\d$/.test(from.value)) save({ quiet: { from: from.value } }); });
    to.addEventListener('change', () => { if (/^\d\d:\d\d$/.test(to.value)) save({ quiet: { to: to.value } }); });
    const tz = -new Date().getTimezoneOffset();
    const tzLabel = `UTC${tz >= 0 ? '+' : '−'}${String(Math.floor(Math.abs(tz) / 60)).padStart(2, '0')}:${String(Math.abs(tz) % 60).padStart(2, '0')}`;
    const inGame = segmented([{ value: 'all', label: t('nprefs.igAll') }, { value: 'important', label: t('nprefs.igImportant') }, { value: 'off', label: t('nprefs.igOff') }], p.in_game, (v) => save({ in_game: v }), { label: t('nprefs.inGame') });
    inGame.classList.add('full');
    replace(page,
      h('div', { class: 'note mt8' }, icon('info', 18), h('span', {}, h('strong', { text: t('nprefs.defaultsTitle') }), ' ', t('nprefs.defaults'))),
      section(t('nprefs.types'), h('div', { class: 'list' }, NTYPES.map(([type, ic]) => {
        const seg = segmented(modes, p.types[type], (v) => save({ types: { [type]: v } }), { label: t(`ntype.${type}`), small: true });
        return h('div', { class: 'np-type' }, h('div', { class: 'np-head' }, h('span', { class: 'lr-ic c-accent' }, icon(ic, 18)),
          h('div', { class: 'np-txt' }, h('strong', { text: t(`ntype.${type}`) }), h('small', { text: t(`ntype.${type}Hint`) }))), seg);
      }))),
      h('p', { class: 'small muted mt8', text: t('nprefs.favHint') }),
      section(t('nprefs.inGame'), h('div', { class: 'card stack' }, inGame, h('p', { class: 'small muted', text: t(`nprefs.ig_${p.in_game}`) }))),
      section(t('nprefs.quiet'), h('div', { class: 'list' },
        listRow({ icon: 'moon', iconCls: 'c-accent', title: t('nprefs.quietOn'), sub: t('nprefs.quietSub'), chevron: false, trail: toggle(quietOn, (on) => save({ quiet: { enabled: on } }).then(draw), t('nprefs.quietOn')) }),
        h('div', { class: 'np-type' }, h('div', { class: 'time-row' }, h('label', {}, t('nprefs.from'), from), h('label', {}, t('nprefs.to'), to)),
          h('small', { class: 'muted', text: t('nprefs.tz', { tz: tzLabel }) })))),
      section(t('nprefs.friends'), p.friends.length ? h('div', { class: 'list' }, p.friends.map((f) => {
        const fav = h('button', { class: `fav-btn ${f.favorite ? 'on-fav' : ''}`, type: 'button', 'aria-pressed': String(f.favorite), 'aria-label': t('nprefs.favorite'), title: t('nprefs.favorite') }, icon(f.favorite ? 'starf' : 'star', 20));
        const mute = h('button', { class: `fav-btn ${f.muted ? 'on-mute' : ''}`, type: 'button', 'aria-pressed': String(f.muted), 'aria-label': t('nprefs.mute'), title: t('nprefs.mute') }, icon('bellmute', 20));
        const upd = async (patch) => {
          try { const r = await post('/notifications/prefs/friend', { online_id: f.online_id, ...patch }); f.favorite = r.favorite; f.muted = r.muted; vibrate(8); paintF(); flash(); }
          catch (e) { toast(errorText(e), { kind: 'error' }); }
        };
        const sub = h('span', { class: 'lr-sub' });
        const paintF = () => {
          fav.className = `fav-btn ${f.favorite ? 'on-fav' : ''}`; fav.setAttribute('aria-pressed', String(f.favorite)); fav.querySelector('use').setAttribute('href', f.favorite ? '#i-starf' : '#i-star');
          mute.className = `fav-btn ${f.muted ? 'on-mute' : ''}`; mute.setAttribute('aria-pressed', String(f.muted));
          sub.textContent = f.muted ? t('nprefs.mutedSub') : f.favorite ? t('nprefs.favSub') : t('nprefs.normalSub');
        };
        fav.addEventListener('click', () => upd({ favorite: !f.favorite }));
        mute.addEventListener('click', () => upd({ muted: !f.muted }));
        paintF();
        return h('div', { class: 'lrow' }, avatar(f, 42), h('span', { class: 'lr-main' }, h('span', { class: 'lr-title', text: f.online_id }), sub), h('span', { class: 'friend-actions' }, fav, mute));
      })) : h('div', { class: 'card' }, empty('friends', t('friends.none'), t('nprefs.noFriends')))));
  }
  try { p = await get('/notifications/prefs'); draw(); } catch (e) { replace(page, errorState(e, () => notifPrefs(ctx))); }
}

// ------------------------------------------------------- la mia console --
export async function consoleView(ctx) {
  const { page } = ctx;
  add(page, skeletonList(3, 'card'));
  async function load() {
    let c;
    try { c = await get('/console'); } catch (e) { replace(page, errorState(e, load)); return; }
    const on = c.online;
    const feats = [
      ['upload', t('console.f1'), t('console.f1t')],
      ['library', t('console.f2'), t('console.f2t')],
      ['music', t('console.f3'), t('console.f3t')],
    ];
    const big = h('div', { class: `big-console mt8 ${on ? 'is-on' : ''}` },
      h('div', { class: 'bc-ps' }, icon('console', 44)),
      h('span', { class: `pill ${on ? 'pill-ok' : c.paired ? '' : 'pill-warn'}` }, h('span', { class: 'cc-led', style: on ? { background: 'var(--ok)' } : {} }), on ? t('console.on') : c.paired ? t('console.off') : t('console.never')),
      h('h2', { text: on ? (c.game ? t('console.playingGame', { game: c.game.name || c.game.id }) : t('console.onTitle')) : c.paired ? t('console.offTitle') : t('console.neverTitle') }),
      h('p', { class: 'muted', style: { maxWidth: '520px' }, text: on ? t('console.onText') : c.paired ? t('console.offText', { when: c.last_seen ? relTime(c.last_seen) : '—' }) : t('console.neverText') }),
      on && c.link ? h('a', { class: 'btn btn-grad remote-btn', href: c.link, target: '_blank', rel: 'noopener' }, icon('external', 22), h('span', { text: t('console.openRemote') })) : null,
      on && !c.link ? h('div', { class: 'note warn' }, icon('alert', 18), h('span', { text: t('console.noLink') })) : null,
      on ? h('p', { class: 'small muted', style: { maxWidth: '480px' } }, icon('wifi', 14, ''), ' ', t('console.sameWifiLong')) : null);
    const parts = [big];
    if (on || c.paired) {
      parts.push(section(t('console.remoteFor'), h('div', { class: 'feat-list' }, feats.map(([ic, ti, tx]) => h('div', { class: 'feat' }, h('span', { class: 'lr-ic c-accent' }, icon(ic, 20)), h('div', {}, h('h4', { text: ti }), h('p', { text: tx })))))));
      parts.push(h('div', { class: 'note mt12' }, icon('shield', 18), h('span', { text: t('console.whyDirect') })));
    }
    if (c.paired && !on) {
      parts.push(section(t('console.offWorks'), h('div', { class: 'card ok-list' },
        [t('console.ow1'), t('console.ow2'), t('console.ow3'), t('console.ow4')].map((s) => h('div', { class: 'ok-item' }, icon('check', 18), h('span', { text: s }))))));
      parts.push(section(t('console.wake'), h('div', { class: 'card steps' },
        h('div', { class: 'step' }, h('p', { text: t('console.w1') })), h('div', { class: 'step' }, h('p', { text: t('console.w2') })), h('div', { class: 'step' }, h('p', { text: t('console.w3') })))));
    }
    if (!c.paired) {
      parts.push(section(t('console.howPair'), h('div', { class: 'card steps' },
        h('div', { class: 'step' }, h('p', {}, h('strong', { text: t('console.p1') }), ' ', t('console.p1t'), ' ', h('a', { href: '/installa', text: t('console.p1link') }))),
        h('div', { class: 'step' }, h('p', {}, h('strong', { text: t('console.p2') }), ' ', t('console.p2t', { name: auth.onlineId || '' }))),
        h('div', { class: 'step' }, h('p', {}, h('strong', { text: t('console.p3') }), ' ', t('console.p3t'))))));
      parts.push(section(t('console.offWorks'), h('div', { class: 'card ok-list' },
        [t('console.ow1'), t('console.ow2'), t('console.ow3')].map((s) => h('div', { class: 'ok-item' }, icon('check', 18), h('span', { text: s }))))));
    }
    if (c.paired) {
      parts.push(section(t('console.details'), h('div', { class: 'card' }, h('dl', { class: 'kv' },
        h('dt', { text: t('console.state') }), h('dd', { text: on ? t('console.on') : t('console.off') }),
        h('dt', { text: t('console.lastSignal') }), h('dd', { text: c.last_seen ? relTime(c.last_seen) : '—' }),
        on && c.lan_ip ? [h('dt', { text: t('console.address') }), h('dd', { class: 'mono', text: `${c.lan_ip}:${c.port}` })] : null,
        c.version ? [h('dt', { text: t('console.version') }), h('dd', { text: c.version })] : null))));
    }
    parts.push(h('a', { class: 'btn btn-ghost btn-block mt16', href: '#/store' }, icon('store', 20), t('console.toStore')));
    replace(page, ...parts);
  }
  ctx.onRefresh(load);
  ctx.every(15000, load);
  await load();
}
