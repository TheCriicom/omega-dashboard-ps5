// Messaggi diretti, chat (riusata da party e gruppi) e party.
import { t } from './i18n.js';
import { get, post, E } from './api.js';
import { add,
  h, icon, iconBtn, avatar, replace, skeletonList, empty, errorState, relTime, clockTime, dayLabel, presenceKind,
  btn, busy, toast, errorText, autoGrow, richText, confirmDialog, sheet, formSheet, vibrate, section, badge,
} from './ui.js';
import { state, emit } from './state.js';
import { pickFriends, presenceSub, userActionsSheet } from './common.js';
import { go, userPath, chatPath } from './nav.js';

// --------------------------------------------------- componente chat --
// opts: { fetch(afterId) -> [{id, mine, text, created_at, from, system}], send(text) -> msg, pollMs, emptyText, showNames }
export function chatThread(ctx, opts) {
  const scroller = h('div', { class: 'chat-scroll', role: 'log', 'aria-live': 'polite' });
  const ta = h('textarea', { class: 'input', rows: 1, maxlength: opts.max || 500, placeholder: opts.placeholder || t('chat.placeholder'), 'aria-label': opts.placeholder || t('chat.placeholder'), enterkeyhint: 'send' });
  const send = h('button', { class: 'send-btn', type: 'submit', 'aria-label': t('common.send'), disabled: true }, icon('send', 20));
  const form = h('form', { class: 'composer' }, ta, send);
  const wrap = h('div', { class: `chat ${opts.inline ? 'inline' : ''}` }, scroller, opts.disabled ? h('div', { class: 'note mt8', style: { marginBottom: '16px' } }, icon('info', 18), h('span', { text: opts.disabled })) : form);
  const msgs = [];
  let lastId = 0;
  let loadedOnce = false;
  autoGrow(ta, 140);
  ta.addEventListener('input', () => { send.disabled = !ta.value.trim(); });
  ta.addEventListener('keydown', (e) => {
    if (e.key === 'Enter' && !e.shiftKey && !e.isComposing && matchMedia('(pointer: fine)').matches) { e.preventDefault(); form.requestSubmit(); }
  });
  const nearBottom = () => window.innerHeight + window.scrollY >= document.documentElement.scrollHeight - 140;
  const toBottom = (smooth) => requestAnimationFrame(() => window.scrollTo({ top: document.documentElement.scrollHeight, behavior: smooth ? 'smooth' : 'auto' }));

  function paint() {
    const nodes = [];
    let prevDay = null;
    msgs.forEach((m, i) => {
      const day = new Date(m.created_at).toDateString();
      if (day !== prevDay) { nodes.push(h('div', { class: 'chat-day', text: dayLabel(m.created_at) })); prevDay = day; }
      if (m.system) { nodes.push(h('div', { class: 'sys-msg', text: m.text })); return; }
      const prev = msgs[i - 1]; const next = msgs[i + 1];
      const sameAs = (a) => a && !a.system && a.mine === m.mine && (a.from && a.from.online_id) === (m.from && m.from.online_id)
        && Math.abs(Date.parse(a.created_at) - Date.parse(m.created_at)) < 5 * 60000 && new Date(a.created_at).toDateString() === day;
      const cont = sameAs(prev); const nxt = sameAs(next);
      const showAv = opts.showNames && !m.mine;
      nodes.push(h('div', { class: `bubble-row ${m.mine ? 'mine' : ''} ${cont ? 'cont' : ''} ${nxt ? 'next' : ''}` },
        showAv ? h('span', { class: 'bubble-av' }, !nxt && m.from ? h('a', { href: `#${userPath(m.from.online_id)}` }, avatar(m.from, 28)) : null) : null,
        h('div', { class: `bubble ${m.pending ? 'pending' : ''} ${m.failed ? 'failed' : ''}` },
          showAv && !cont && m.from ? h('div', { class: 'bubble-who', text: m.from.online_id }) : null,
          richText(m.text, ''),
          !nxt ? h('span', { class: 'bubble-time', text: m.failed ? t('chat.failed') : m.pending ? t('chat.sending') : clockTime(m.created_at) }) : null)));
    });
    if (!msgs.length) nodes.push(empty(opts.emptyIcon || 'chat', opts.emptyTitle || t('chat.emptyTitle'), opts.emptyText || t('chat.emptyText')));
    replace(scroller, nodes);
  }

  async function pull(initial) {
    const stick = initial || nearBottom();
    const fresh = await opts.fetch(lastId);
    loadedOnce = true;
    let added = 0;
    for (const m of fresh) {
      if (msgs.some((x) => x.id === m.id)) continue;
      // il mio messaggio in attesa torna dal server: lo sostituisce
      const pend = m.mine ? msgs.findIndex((x) => x.pending && x.text === m.text) : -1;
      if (pend >= 0) msgs.splice(pend, 1);
      msgs.push(m); added++;
      lastId = Math.max(lastId, Number(m.id) || 0);
    }
    if (added || initial) { msgs.sort((a, b) => (a.pending ? 1 : 0) - (b.pending ? 1 : 0) || Number(a.id) - Number(b.id)); paint(); if (stick && !(initial && opts.inline)) toBottom(!initial); }
    return added;
  }

  form.addEventListener('submit', async (e) => {
    e.preventDefault();
    const text = ta.value.trim();
    if (!text) return;
    ta.value = ''; send.disabled = true; ta.dispatchEvent(new Event('input'));
    const tmp = { id: `tmp${Date.now()}`, mine: true, text, created_at: new Date().toISOString(), pending: true, from: state.me };
    msgs.push(tmp); paint(); toBottom(true); vibrate(6);
    try {
      await opts.send(text);
      await pull(false);
      const still = msgs.indexOf(tmp);
      if (still >= 0 && tmp.pending) { tmp.pending = false; paint(); }
    } catch (ex) {
      tmp.pending = false; tmp.failed = true; paint();
      toast(errorText(ex), { kind: 'error', action: t('common.retry'), onAction: () => { msgs.splice(msgs.indexOf(tmp), 1); ta.value = text; form.requestSubmit(); } });
    }
    ta.focus();
  });

  return {
    el: wrap,
    async start() {
      replace(scroller, skeletonList(4));
      await pull(true);
      ctx.every(opts.pollMs || 4000, () => pull(false));
      if (!opts.inline && matchMedia('(pointer: fine)').matches) ta.focus();
    },
    pull,
  };
}

// ---------------------------------------------------- conversazioni --
export async function messages(ctx) {
  const { page } = ctx;
  const listBox = h('div', {}, skeletonList(6));
  const newMsg = () => pickFriends({ title: t('chat.new'), onPick: (oid) => go(chatPath(oid)) });
  ctx.setActions(iconBtn('edit', t('chat.new'), newMsg));
  add(page, listBox);
  async function load() {
    try {
      const r = await get('/messages');
      const conv = r.conversations || [];
      if (!conv.length) { replace(listBox, empty('chat', t('chat.noConv'), t('chat.noConvText'), btn(t('chat.new'), { icon: 'edit', onclick: newMsg }))); return; }
      const fmap = new Map(state.friends.map((f) => [f.online_id, f]));
      replace(listBox, h('div', { class: 'list' }, conv.map((c) => {
        const f = fmap.get(c.online_id);
        return h('a', { class: `lrow ${c.unread ? 'conv-unread' : ''}`, href: `#${chatPath(c.online_id)}` },
          avatar(c, 50, { presence: f ? f.presence : null }),
          h('span', { class: 'lr-main' }, h('span', { class: 'lr-title', text: c.online_id }), h('span', { class: 'lr-sub', text: `${c.last_from_me ? `${t('chat.you')}: ` : ''}${c.last_body}` })),
          h('span', { class: 'lr-trail', style: { flexDirection: 'column', alignItems: 'flex-end', gap: '4px' } }, h('span', { text: relTime(c.last_at) }), badge(c.unread)));
      })), h('p', { class: 'small muted center mt16', text: t('chat.groupsHint') }));
    } catch (e) { replace(listBox, errorState(e, load)); }
  }
  ctx.onRefresh(load);
  ctx.every(15000, load);
  ctx.on('sync', () => { if (state.counts.unread_messages) load(); });
  await load();
}

// --------------------------------------------------------- una chat --
export async function chat(ctx) {
  const { page, params } = ctx;
  const oid = params.id;
  const f = state.friends.find((x) => x.online_id.toLowerCase() === oid.toLowerCase());
  const head = () => {
    const fr = state.friends.find((x) => x.online_id.toLowerCase() === oid.toLowerCase());
    return h('a', { class: 'chat-head', href: `#${userPath(oid)}` }, avatar(fr || { online_id: oid }, 34, { presence: fr ? fr.presence : null }),
      h('span', { class: 'chat-head-txt' }, h('strong', { text: fr ? fr.online_id : oid }), fr ? presenceSub(fr.presence) : h('small', { text: t('chat.notFriend') })));
  };
  ctx.setTitle(head());
  ctx.on('sync', () => ctx.setTitle(head()));
  ctx.setActions(iconBtn('more', t('common.more'), () => userActionsSheet({ online_id: oid }, f ? 'friend' : 'none', () => {})));
  const thread = chatThread(ctx, {
    fetch: async (after) => {
      const r = await get(`/messages/${E(oid)}${after ? `?after=${after}` : ''}`);
      return r.messages.map((m) => ({ id: m.message_id, mine: m.mine, text: m.body, created_at: m.created_at, from: m.mine ? state.me : { online_id: oid } }));
    },
    send: (text) => post(`/messages/${E(oid)}`, { text }),
    emptyTitle: t('chat.startWith', { name: oid }), emptyText: f ? t('chat.emptyText') : t('chat.strangerText'),
  });
  add(page, thread.el);
  try { await thread.start(); emit('resync'); }
  catch (e) {
    if (e.code === 'account_not_found') replace(page, empty('user', t('user.notFound'), null));
    else replace(page, errorState(e, () => chat(ctx)));
  }
}

// ------------------------------------------------------------- party --
export async function party(ctx) {
  const { page } = ctx;
  const top = h('div');
  const chatBox = h('div');
  add(page, top, chatBox);
  let thread = null;
  let partyId = null;
  let lastSig = null;

  async function load() {
    const r = await get('/party');
    state.party = r.party; state.partyInvites = r.invites || [];
    emit('badges');
    const sig = JSON.stringify(r, (k, v) => (k === 'last_seen' || k === 'messages' ? undefined : v));
    if (sig === lastSig) return;
    lastSig = sig;
    draw(r);
  }
  function draw(r) {
    const p = r.party;
    if (!p) {
      partyId = null; thread = null;
      replace(chatBox);
      const name = h('input', { class: 'input', maxlength: 40, placeholder: t('party.namePh', { name: (state.me && state.me.online_id) || '' }), 'aria-label': t('party.name') });
      const create = btn(t('party.create'), { kind: 'grad', icon: 'plus', size: 'lg', block: true });
      create.addEventListener('click', () => busy(create, async () => {
        try { await post('/party', { name: name.value.trim() || undefined }); toast(t('party.created'), { kind: 'ok' }); await load(); invite(); }
        catch (e) { toast(errorText(e), { kind: 'error' }); }
      }));
      replace(top,
        r.invites && r.invites.length ? section(t('party.invites'), h('div', { class: 'list' }, r.invites.map((i) => h('div', { class: 'lrow' },
          avatar({ online_id: i.from_online_id, avatar: i.avatar }, 44),
          h('span', { class: 'lr-main' }, h('span', { class: 'lr-title', text: i.name }), h('span', { class: 'lr-sub', text: `${t('party.inviteFrom', { name: i.from_online_id })} · ${t('party.membersN', { n: i.members })}` })),
          h('span', { class: 'friend-actions' },
            btn(t('party.join'), { size: 'sm', onclick: async () => { try { await post('/party/join', { party_id: i.party_id }); vibrate(12); await load(); } catch (e) { toast(errorText(e), { kind: 'error' }); } } }),
            iconBtn('close', t('party.decline'), async () => { try { await post('/party/decline', { party_id: i.party_id }); await load(); } catch (e) { toast(errorText(e), { kind: 'error' }); } })))))) : null,
        h('section', { class: 'sec' }, h('div', { class: 'party-hero stack' },
          h('div', { class: 'row' }, h('span', { class: 'quick-ic q2', style: { width: '52px', height: '52px', borderRadius: '16px' } }, icon('party', 26)),
            h('div', {}, h('h2', { text: t('party.noneTitle') }), h('p', { class: 'muted small', text: t('party.noneText') }))),
          name, create)),
        h('div', { class: 'note mt16' }, icon('mic', 18), h('span', { text: t('party.voiceNote') })));
      return;
    }
    const meOid = state.me && state.me.online_id;
    const mine = p.members.find((m) => m.online_id === meOid);
    const muteBtn = btn(mine && mine.muted ? t('party.unmute') : t('party.mute'), { kind: 'ghost', icon: mine && mine.muted ? 'micoff' : 'mic', size: 'sm' });
    muteBtn.addEventListener('click', () => busy(muteBtn, async () => { try { await post('/party/mute', { muted: !(mine && mine.muted) }); await load(); } catch (e) { toast(errorText(e), { kind: 'error' }); } }));
    const leave = btn(t('party.leave'), { kind: 'danger-soft', icon: 'logout', size: 'sm' });
    leave.addEventListener('click', async () => {
      if (!(await confirmDialog({ title: t('party.leaveQ'), text: p.members.length > 1 ? t('party.leaveText') : t('party.leaveLast'), ok: t('party.leave'), danger: true }))) return;
      try { await post('/party/leave'); toast(t('party.left')); await load(); emit('resync'); } catch (e) { toast(errorText(e), { kind: 'error' }); }
    });
    const talking = new Set((p.talking || []).map(String));
    replace(top, h('section', { class: 'sec' }, h('div', { class: 'party-hero' },
      h('div', { class: 'row', style: { alignItems: 'flex-start' } },
        h('div', { style: { flex: 1, minWidth: 0 } }, h('span', { class: 'pill pill-ok' }, h('span', { class: 'cc-led', style: { background: 'var(--ok)' } }), t('party.active')),
          h('h2', { class: 'mt8', text: p.name }), h('p', { class: 'muted small', text: `${t('party.membersN', { n: p.members.length })} · ${t('party.since', { when: relTime(p.created_at) })}` }))),
      h('div', { class: 'party-members' }, p.members.map((m) => h('a', { class: 'pm', href: `#${userPath(m.online_id)}` },
        avatar(m, 60, { presence: m.presence, cls: talking.has(m.online_id) ? 'talking' : '' }),
        h('span', { class: 'pm-name', text: m.online_id === meOid ? t('common.you') : m.online_id }),
        h('span', { class: 'pm-flags' }, m.owner ? icon('crown', 14) : null, m.muted ? icon('micoff', 14) : null))),
      p.invited.map((m) => h('div', { class: 'pm', style: { opacity: '.55' } }, avatar(m, 60), h('span', { class: 'pm-name', text: m.online_id }), h('span', { class: 'pm-flags', style: { fontSize: '11px' } }, t('party.invitedShort')))),
      h('button', { class: 'pm', type: 'button', onclick: invite, 'aria-label': t('party.invite') }, h('span', { class: 'avatar', style: { width: '60px', height: '60px', background: 'var(--card-3)', color: 'var(--text-2)' } }, icon('plus', 26)), h('span', { class: 'pm-name', text: t('party.invite') }))),
      h('div', { class: 'row-btns mt16', style: { justifyContent: 'flex-start' } }, btn(t('party.invite'), { icon: 'useradd', size: 'sm', onclick: invite }), muteBtn, leave))),
    h('div', { class: 'note mt12' }, icon('mic', 18), h('span', { text: t('party.voiceNote') })));
    if (partyId !== p.party_id) {
      partyId = p.party_id;
      thread = chatThread(ctx, {
        fetch: async (after) => {
          const r2 = await get(`/party/messages${after ? `?after=${after}` : ''}`);
          return r2.messages.map((m) => ({ id: m.message_id, mine: m.sender === meOid && !m.body.startsWith('· '), text: m.body.startsWith('· ') ? m.body.slice(2) : m.body, system: m.body.startsWith('· '), created_at: m.created_at, from: { online_id: m.sender, avatar: m.avatar } }));
        },
        send: (text) => post('/party/messages', { text }),
        showNames: true, inline: true, emptyIcon: 'party', emptyTitle: t('party.chatEmpty'), emptyText: t('party.chatEmptyText'), placeholder: t('party.chatPh'),
      });
      replace(chatBox, h('h2', { class: 'sec-title mt24', text: t('party.chat') }), thread.el);
      thread.start().catch(() => {});
    }
  }
  function invite() {
    const excl = state.party ? state.party.members.map((m) => m.online_id).concat(state.party.invited.map((m) => m.online_id)) : [];
    pickFriends({
      title: t('party.invite'), subtitle: t('party.inviteSub'), multi: true, exclude: excl, submit: t('party.sendInvites'),
      onPick: async (list) => {
        for (const oid of list) await post('/party/invite', { online_id: oid });
        toast(t('party.invitesSent', { n: list.length }), { kind: 'ok', icon: 'party' });
        await load();
      },
    });
  }
  ctx.onRefresh(load);
  ctx.every(8000, load);
  try { await load(); } catch (e) { replace(page, errorState(e, () => party(ctx))); }
}
