// Store: vetrina, scaffali, ricerca, scheda app (voti, commenti, lista
// desideri, consiglia a un amico, "Installa sulla PS5"), creatori.
import { t } from './i18n.js';
import { get, post, del, E } from './api.js';
import { add,
  h, icon, iconBtn, avatar, apiImg, replace, skeletonList, empty, errorState, relTime, btn, busy, toast, errorText,
  stars, compact, num, bytes, section, sheet, confirmDialog, formSheet, richText, autoGrow, vibrate, segmented,
} from './ui.js';
import { state, pref, setPref } from './state.js';
import { appTile, appRow, appCover, appIcon, catLabel, pickFriends, reportSheet, installOnPS5 } from './common.js';
import { go, userPath, appPath } from './nav.js';

const CATS = [
  { id: 'emulatore', icon: 'gamepad' }, { id: 'gioco', icon: 'sparkle' }, { id: 'utility', icon: 'settings' },
  { id: 'app', icon: 'grid' }, { id: 'trucchi', icon: 'key' },
];

function shelf(title, apps, more) {
  return section(title, more ? { action: h('a', { class: 'sec-link', href: more }, t('common.all'), icon('chev', 16)) } : {},
    h('div', { class: 'shelf' }, apps.map(appTile)));
}

export async function store(ctx) {
  const { page } = ctx;
  const searchBox = h('a', { class: 'search', href: '#/store/browse?focus=1', 'aria-label': t('store.searchPh') },
    icon('search', 18), h('div', { class: 'input', style: { color: 'var(--text-3)', display: 'flex', alignItems: 'center' }, text: t('store.searchPh') }));
  const heroBox = h('div', {}, h('div', { class: 'skel', style: { aspectRatio: '16/9', borderRadius: '28px' } }));
  const shelves = h('div', {}, skeletonList(4, 'tile'));
  add(page, h('div', { class: 'store-search mt8' }, searchBox, iconBtn('bookmark', t('store.wishlist'), () => go('/store/browse?wish=1'))), h('div', { class: 'mt16' }, heroBox), shelves);
  ctx.setActions(iconBtn('search', t('store.search'), () => go('/store/browse?focus=1')));

  async function load() {
    try {
      const [top, trending, friends, wish, recent, downloads, creators] = await Promise.all([
        get('/store/apps?sort=top'), get('/store/apps?sort=trending'), get('/store/apps?sort=friends'), get('/store/apps?wish=1'),
        get('/store/apps?sort=recent'), get('/store/apps?sort=downloads'), get('/store/creators').catch(() => ({ creators: [] })),
      ]);
      drawHero(top.apps.filter((a) => a.has_cover).slice(0, 5));
      const pieces = [];
      if (friends.apps.length) pieces.push(shelf(t('store.friendsShelf'), friends.apps.slice(0, 12), '#/store/browse?sort=friends'));
      pieces.push(shelf(t('store.trending'), trending.apps.slice(0, 12), '#/store/browse?sort=trending'));
      if (wish.apps.length) pieces.push(shelf(t('store.wishShelf'), wish.apps, '#/store/browse?wish=1'));
      pieces.push(section(t('store.categories'), h('div', { class: 'cat-grid' }, CATS.map((c, i) => h('a', { class: `cat cat-${i}`, href: `#/store/browse?cat=${c.id}` }, icon(c.icon, 64), catLabel(c.id))))));
      pieces.push(h('div', { class: 'grid2' },
        section(t('store.mostDownloaded'), { action: h('a', { class: 'sec-link', href: '#/store/browse?sort=downloads' }, t('common.all'), icon('chev', 16)) }, h('div', { class: 'list' }, downloads.apps.slice(0, 6).map((a, i) => appRow(a, i + 1)))),
        section(t('store.topRated'), { action: h('a', { class: 'sec-link', href: '#/store/browse?sort=top' }, t('common.all'), icon('chev', 16)) }, h('div', { class: 'list' }, top.apps.slice(0, 6).map((a, i) => appRow(a, i + 1))))));
      pieces.push(shelf(t('store.new'), recent.apps.slice(0, 12), '#/store/browse?sort=recent'));
      for (const c of CATS.slice(0, 3)) {
        const list = recent.apps.filter((a) => a.category === c.id).slice(0, 10);
        if (list.length >= 3) pieces.push(shelf(catLabel(c.id), list, `#/store/browse?cat=${c.id}`));
      }
      if (creators.creators.length) {
        pieces.push(section(t('store.creators'), { action: h('a', { class: 'sec-link', href: '#/store/creators' }, t('common.all'), icon('chev', 16)) },
          h('div', { class: 'friends-strip', style: { gap: '12px' } }, creators.creators.map(creatorCard))));
      }
      pieces.push(h('div', { class: 'note mt24' }, icon('console', 18), h('span', { text: t('store.howInstall') })));
      replace(shelves, ...pieces);
    } catch (e) { replace(shelves, errorState(e, load)); replace(heroBox); }
  }

  let heroTimer = null;
  function drawHero(list) {
    if (!list.length) { replace(heroBox); return; }
    let i = 0;
    const link = h('a', { class: 'store-hero' });
    const dots = h('div', { class: 'hero-dots', 'aria-hidden': 'true' }, list.map(() => h('span')));
    const paint = () => {
      const a = list[i];
      link.href = `#${appPath(a.app_id)}`;
      link.setAttribute('aria-label', a.title);
      replace(link, appCover(a, 'hero-bg'), dots,
        h('div', { class: 'hero-art' }, appCover(a, 'hero-img')),
        h('div', { class: 'hero-txt' },
          h('div', { class: 'hero-main' }, h('div', { class: 'hero-kicker', text: t('store.featured') }), h('h2', { text: a.title }), h('p', { text: a.tagline || '' }),
            h('div', { class: 'hero-meta' }, h('span', { class: 'pill', text: catLabel(a.category) }), a.friends_count ? h('span', { class: 'pill pill-accent', text: t('store.friendsN', { n: a.friends_count }) }) : null,
              a.rating ? h('span', { class: 'pill' }, icon('starf', 12, 'on'), a.rating.toFixed(1)) : null))));
      [...dots.children].forEach((d, k) => d.classList.toggle('on', k === i));
    };
    paint();
    replace(heroBox, link);
    const step = (d) => { i = (i + d + list.length) % list.length; paint(); };
    clearInterval(heroTimer);
    heroTimer = setInterval(() => { if (document.visibilityState === 'visible') step(1); }, 6000);
    ctx.onCleanup(() => clearInterval(heroTimer));
    let x0 = null;
    link.addEventListener('touchstart', (e) => { x0 = e.touches[0].clientX; }, { passive: true });
    link.addEventListener('touchend', (e) => {
      if (x0 == null) return;
      const dx = e.changedTouches[0].clientX - x0; x0 = null;
      if (Math.abs(dx) > 40) { e.preventDefault(); step(dx < 0 ? 1 : -1); clearInterval(heroTimer); }
    });
  }
  ctx.onRefresh(load);
  await load();
}

function creatorCard(c) {
  return h('a', { class: 'creator', href: `#${userPath(c.online_id)}` }, avatar(c, 56), h('strong', { text: c.online_id }),
    h('small', { text: t('store.appsN', { n: c.apps }) }), h('small', { text: `${compact(c.downloads)} ${t('store.installs')} · ${compact(c.likes)} ♥` }));
}

// ------------------------------------------------------------- ricerca --
export async function storeBrowse(ctx) {
  const { page, query } = ctx;
  let q = query.q || '';
  let sort = query.sort || (query.wish ? 'recent' : 'top');
  let cat = query.cat || '';
  const wish = query.wish === '1';
  ctx.setTitle(wish ? t('store.wishlist') : cat ? catLabel(cat) : sort === 'friends' ? t('store.friendsShelf') : t('store.browse'));
  const input = h('input', { class: 'input', type: 'search', value: q, placeholder: t('store.searchPh'), 'aria-label': t('store.searchPh'), enterkeyhint: 'search' });
  const sorts = [['top', t('store.sortTop')], ['trending', t('store.sortTrending')], ['downloads', t('store.sortDownloads')], ['recent', t('store.sortRecent')]];
  if (sort === 'friends') sorts.unshift(['friends', t('store.sortFriends')]);
  const sortRow = h('div', { class: 'chips-scroll' });
  const catRow = h('div', { class: 'chips-scroll mt8' });
  const grid = h('div', { class: 'mt16' }, skeletonList(6, 'tile'));
  add(page, h('div', { class: 'search mt8' }, icon('search', 18), input), h('div', { class: 'mt12' }, sortRow), catRow, grid);
  const paintChips = () => {
    replace(sortRow, sorts.map(([v, l]) => h('button', { class: `chip ${sort === v ? 'on' : ''}`, type: 'button', 'aria-pressed': String(sort === v), text: l, onclick: () => { sort = v; paintChips(); load(); } })));
    replace(catRow, h('button', { class: `chip ${!cat ? 'on' : ''}`, type: 'button', text: t('store.allCats'), onclick: () => { cat = ''; paintChips(); paint(); } }),
      CATS.map((c) => h('button', { class: `chip ${cat === c.id ? 'on' : ''}`, type: 'button', onclick: () => { cat = c.id; paintChips(); paint(); } }, icon(c.icon, 16), catLabel(c.id))));
  };
  paintChips();
  let apps = [];
  let seq = 0;
  async function load() {
    const my = ++seq;
    replace(grid, skeletonList(6, 'tile'));
    const params = new URLSearchParams();
    params.set('sort', sort);
    if (q.trim()) params.set('q', q.trim());
    if (wish) params.set('wish', '1');
    if (query.tag) params.set('tag', query.tag);
    history.replaceState(null, '', `#/store/browse?${new URLSearchParams({ ...(q.trim() ? { q: q.trim() } : {}), sort, ...(cat ? { cat } : {}), ...(wish ? { wish: '1' } : {}) })}`);
    try {
      const r = await get(`/store/apps?${params}`);
      if (my !== seq) return;
      apps = r.apps;
      paint();
    } catch (e) { if (my === seq) replace(grid, errorState(e, load)); }
  }
  function paint() {
    const list = cat ? apps.filter((a) => a.category === cat) : apps;
    if (!list.length) {
      replace(grid, wish ? empty('bookmark', t('store.wishEmpty'), t('store.wishEmptyText'), btn(t('store.explore'), { kind: 'soft', onclick: () => go('/store') }))
        : empty('search', q ? t('store.noResults', { q }) : t('store.nothing'), t('store.noResultsText')));
      return;
    }
    replace(grid, h('p', { class: 'muted small', style: { marginBottom: '12px' }, text: t('store.resultsN', { n: list.length }) }), h('div', { class: 'app-grid' }, list.map(appTile)));
  }
  let timer = null;
  input.addEventListener('input', () => { clearTimeout(timer); timer = setTimeout(() => { q = input.value; load(); }, 300); });
  if (query.focus) setTimeout(() => input.focus(), 250);
  ctx.onRefresh(load);
  await load();
}

// --------------------------------------------------------- scheda app --
export async function storeApp(ctx) {
  const { page, params } = ctx;
  const id = params.id;
  add(page, h('div', { class: 'app-page' }, h('div', { class: 'skel', style: { aspectRatio: '16/9' } }), h('div', { style: { padding: '16px' } }, skeletonList(3))));
  let a;
  async function load() {
    a = await get(`/store/apps/${E(id)}`);
    ctx.setTitle(a.title);
    draw();
  }

  function draw() {
    const wishBtn = h('button', { class: `btn btn-ghost`, type: 'button', 'aria-pressed': String(!!a.wished), 'aria-label': t('store.wish') }, icon(a.wished ? 'bookmarkf' : 'bookmark', 20), h('span', { text: a.wished ? t('store.wished') : t('store.wish') }));
    wishBtn.addEventListener('click', () => busy(wishBtn, async () => {
      try {
        const r = await post(`/store/apps/${E(id)}/wish`, { on: !a.wished });
        a.wished = r.wished; a.wishes = r.wishes; vibrate(10);
        toast(a.wished ? t('store.wishAdded') : t('store.wishRemoved'), { kind: 'ok', icon: 'bookmark' });
        draw();
      } catch (e) { toast(errorText(e), { kind: 'error' }); }
    }));
    const installBtn = btn(t('store.install'), { kind: 'grad', icon: 'console', size: 'lg' });
    installBtn.addEventListener('click', () => installOnPS5(a, installBtn));
    const recBtn = h('button', { class: 'btn btn-ghost', type: 'button', 'aria-label': t('store.recommend'), title: t('store.recommend') }, icon('gift', 20), h('span', { text: t('store.recommendShort') }));
    recBtn.addEventListener('click', recommend);
    const moreBtn = h('button', { class: 'btn btn-ghost btn-square', type: 'button', 'aria-label': t('common.more') }, icon('more', 20));
    moreBtn.addEventListener('click', () => sheet({ title: a.title, actions: [
      { icon: 'link', label: t('store.copyLink'), onClick: async () => { try { await navigator.clipboard.writeText(`${location.origin}/app/#/store/app/${a.app_id}`); toast(t('common.copied'), { kind: 'ok' }); } catch { toast(t('err.generic'), { kind: 'error' }); } } },
      a.homepage_url ? { icon: 'external', label: t('store.homepage'), onClick: () => window.open(a.homepage_url, '_blank', 'noopener') } : null,
      !a.mine ? { icon: 'flag', label: t('store.report'), danger: true, onClick: () => reportSheet('store', `/store/apps/${E(id)}/report`) } : null,
    ] }));


    const shots = [];
    for (let i = 0; i < (a.nscreens || 0); i++) {
      const s = apiImg(`/store/apps/${E(id)}/shot/${i}`, '', t('store.shotN', { n: i + 1 }));
      s.tabIndex = 0; s.setAttribute('role', 'button'); s.setAttribute('aria-label', t('store.shotN', { n: i + 1 }));
      s.addEventListener('click', () => lightbox(`/store/apps/${E(id)}/shot/${i}`));
      s.addEventListener('keydown', (e) => { if (e.key === 'Enter') lightbox(`/store/apps/${E(id)}/shot/${i}`); });
      shots.push(s);
    }
    const desc = h('p', { class: 'desc clamp', text: a.description || a.tagline || '' });
    const moreDesc = h('button', { class: 'more-btn', type: 'button', text: t('common.readMore'), onclick: () => { desc.classList.remove('clamp'); moreDesc.remove(); } });

    replace(page, h('div', { class: 'app-page' },
      h('div', { class: 'app-banner' }, appCover(a, 'banner-bg')),
      h('div', { class: 'app-head' }, appIcon(a),
        h('div', { class: 'app-head-txt' }, h('h1', { text: a.title }),
          h('p', {}, h('a', { href: `#${userPath(a.author.online_id)}`, text: a.author.online_id }), ` · ${catLabel(a.category)}`))),
      h('div', { class: 'app-body' },
        a.tagline ? h('p', { class: 'mt12', style: { fontSize: '15.5px' }, text: a.tagline }) : null,
        h('div', { class: 'app-cta' }, installBtn, wishBtn, recBtn, moreBtn),
        h('p', { class: 'small muted mt8', text: t('store.installHint') }),
        h('div', { class: 'app-facts' },
          fact(a.rating ? a.rating.toFixed(1) : '—', a.ratings ? t('store.ratingsN', { n: a.ratings }) : t('store.noRatings'), a.rating ? 'starf' : null),
          fact(compact(a.likes), t('store.likes'), 'up'),
          fact(a.size_bytes ? bytes(a.size_bytes) : (a.file_kind || '—').toUpperCase(), a.size_bytes ? t('store.size') : t('store.format')),
          fact(a.version || '—', t('store.version'))),
        a.friends_count ? h('a', { class: 'card row mt16', href: '#', onclick: (e) => { e.preventDefault(); friendsSheet(); }, style: { color: 'inherit' } },
          h('span', { class: 'avatars-stack' }, a.friends.slice(0, 5).map((f) => avatar(f, 32))),
          h('span', { class: 'lr-main' }, h('span', { class: 'lr-title', text: t('store.friendsInstalled', { n: a.friends_count }) }), h('span', { class: 'lr-sub', text: a.friends.slice(0, 3).map((f) => f.online_id).join(', ') })),
          icon('chev', 18, 'lr-chev')) : null,
        shots.length ? section(t('store.screens'), h('div', { class: 'shots' }, shots)) : null,
        section(t('store.about'), desc, desc.textContent.length > 280 ? moreDesc : null,
          a.hashtags && a.hashtags.length ? h('div', { class: 'tags mt12' }, a.hashtags.map((x) => h('a', { class: 'tag', href: `#/store/browse?tag=${E(x)}&q=${E(x)}`, text: `#${x}` }))) : null),
        h('div', { class: 'grid2' },
          h('div', {}, section(t('store.ratings'), ratingCard()), section(t('store.comments'), commentsBox())),
          h('div', {}, section(t('store.info'), infoCard()))))));
  }

  function fact(v, k, ic) { return h('div', { class: 'fact' }, h('b', {}, ic ? icon(ic, 15, ic === 'starf' ? 'on' : '') : null, v), h('span', { text: k })); }

  function ratingCard() {
    const myStars = h('div', { class: 'rate-stars', role: 'radiogroup', 'aria-label': t('store.yourRating') });
    const paint = (n) => replace(myStars, [1, 2, 3, 4, 5].map((s) => h('button', {
      class: `rate-star ${s <= n ? 'on' : ''}`, type: 'button', role: 'radio', 'aria-checked': String(s === n), 'aria-label': t('store.starsN', { n: s }),
      onclick: async () => {
        const prev = a.my_rating; a.my_rating = s; paint(s); vibrate(8);
        try { const r = await post(`/store/apps/${E(id)}/rate`, { stars: s }); a.rating = r.rating; a.ratings = r.ratings; toast(t('store.rated'), { kind: 'ok', icon: 'starf' }); draw(); }
        catch (e) { a.my_rating = prev; paint(prev); toast(errorText(e), { kind: 'error' }); }
      },
    }, icon(s <= n ? 'starf' : 'star', 28))));
    paint(a.my_rating || 0);
    const vote = (v) => async () => {
      const want = a.my_vote === v ? 0 : v;
      try { const r = await post(`/store/apps/${E(id)}/vote`, { value: want }); Object.assign(a, { likes: r.likes, dislikes: r.dislikes, my_vote: r.my_vote }); vibrate(8); draw(); }
      catch (e) { toast(errorText(e), { kind: 'error' }); }
    };
    return h('div', { class: 'card stack' },
      h('div', { class: 'rate-box' },
        h('div', {}, h('div', { class: 'rate-big', text: a.rating ? a.rating.toFixed(1) : '—' }), stars(a.rating, 14), h('div', { class: 'small muted', text: a.ratings ? t('store.ratingsN', { n: a.ratings }) : t('store.noRatings') })),
        h('div', { class: 'stack', style: { gap: '4px' } }, h('span', { class: 'small muted', text: a.my_rating ? t('store.yourRating') : t('store.tapToRate') }), myStars)),
      h('div', { class: 'vote-row' },
        h('button', { class: `vote-btn ${a.my_vote === 1 ? 'on-up' : ''}`, type: 'button', 'aria-pressed': String(a.my_vote === 1), onclick: vote(1) }, icon('up', 18), `${t('store.like')} · ${compact(a.likes)}`),
        h('button', { class: `vote-btn ${a.my_vote === -1 ? 'on-dn' : ''}`, type: 'button', 'aria-pressed': String(a.my_vote === -1), onclick: vote(-1) }, icon('dn', 18), `${t('store.dislike')} · ${compact(a.dislikes)}`)));
  }

  function infoCard() {
    const rows = [
      [t('store.version'), a.version], [t('store.category'), catLabel(a.category)], [t('store.platform'), a.platform],
      [t('store.format'), a.file_kind && a.file_kind.toUpperCase()], [t('store.size'), a.size_bytes && bytes(a.size_bytes)],
      [t('store.titleId'), a.title_id], [t('store.license'), a.license], [t('store.installs'), num(a.downloads)],
      [t('store.wishes'), num(a.wishes)], [t('store.published'), relTime(a.created_at)],
    ].filter(([, v]) => v);
    return h('div', { class: 'card' }, h('dl', { class: 'kv' }, rows.map(([k, v]) => [h('dt', { text: k }), h('dd', { text: String(v) })])),
      a.homepage_url ? h('a', { class: 'btn btn-ghost btn-block btn-sm mt16', href: a.homepage_url, target: '_blank', rel: 'noopener noreferrer' }, icon('external', 16), t('store.homepage')) : null);
  }

  function commentsBox() {
    const box = h('div', { class: 'card stack' });
    const ta = h('textarea', { class: 'input', rows: 2, maxlength: 500, placeholder: t('store.commentPh'), 'aria-label': t('store.commentPh') });
    const sendB = btn(t('common.send'), { size: 'sm', disabled: true });
    ta.addEventListener('input', () => { sendB.disabled = !ta.value.trim(); });
    autoGrow(ta, 160);
    sendB.addEventListener('click', () => busy(sendB, async () => {
      try { await post(`/store/apps/${E(id)}/comments`, { text: ta.value.trim() }); ta.value = ''; const r = await get(`/store/apps/${E(id)}/comments`); a.comments = r.comments; toast(t('store.commented'), { kind: 'ok' }); draw(); }
      catch (e) { toast(errorText(e), { kind: 'error' }); }
    }));
    const list = (a.comments || []).map((c) => h('div', { class: 'cmt' },
      h('a', { href: `#${userPath(c.author.online_id)}` }, avatar(c.author, 34)),
      h('div', { style: { flex: 1, minWidth: 0 } },
        h('div', { class: 'cmt-b' }, h('a', { class: 'who', href: `#${userPath(c.author.online_id)}`, text: c.author.online_id }), richText(c.body, '')),
        h('div', { class: 'cmt-meta' }, h('span', { text: relTime(c.created_at) }),
          c.mine || a.mine ? h('button', { type: 'button', text: t('common.delete'), onclick: async () => {
            if (!(await confirmDialog({ title: t('store.deleteCommentQ'), ok: t('common.delete'), danger: true }))) return;
            try { await del(`/store/apps/${E(id)}/comments/${E(c.comment_id)}`); a.comments = a.comments.filter((x) => x !== c); draw(); } catch (e) { toast(errorText(e), { kind: 'error' }); }
          } }) : null,
          !c.mine ? h('button', { type: 'button', text: t('feed.report'), onclick: () => reportSheet('store_comment', `/store/apps/${E(id)}/report`, { comment_id: c.comment_id }) }) : null))));
    add(box, h('div', { class: 'cmt-form', style: { alignItems: 'flex-start' } }, avatar(state.me || {}, 34), h('div', { style: { flex: 1 }, class: 'stack' }, ta, h('div', { style: { alignSelf: 'flex-end' } }, sendB))));
    if (list.length) add(box, h('div', { class: 'divider', style: { margin: '4px 0' } }), ...list);
    else add(box, h('p', { class: 'muted small center', text: t('store.noComments') }));
    return box;
  }

  function friendsSheet() {
    sheet({ title: t('store.friendsInstalled', { n: a.friends_count }), content: h('div', { class: 'list' }, a.friends.map((f) => h('a', { class: 'lrow', href: `#${userPath(f.online_id)}` }, avatar(f, 40), h('span', { class: 'lr-main' }, h('span', { class: 'lr-title', text: f.online_id })), icon('chev', 18, 'lr-chev')))) });
  }

  function recommend() {
    pickFriends({
      title: t('store.recommend'), subtitle: a.title,
      onPick: async (oid) => {
        const v = await formSheet({ title: t('store.recommendTo', { name: oid }), subtitle: a.title, submit: t('store.recommendSend'),
          fields: [{ name: 'note', label: t('store.recNote'), type: 'textarea', max: 200, placeholder: t('store.recNotePh') }],
          onSubmit: async (vals) => { await post(`/store/apps/${E(id)}/recommend`, { online_id: oid, note: vals.note || undefined }); return vals; } });
        if (v) toast(t('store.recommended', { name: oid }), { kind: 'ok', icon: 'gift' });
      },
    });
  }

  ctx.onRefresh(load);
  try { await load(); } catch (e) {
    if (e.code === 'app_not_found') replace(page, empty('store', t('store.notFound'), t('store.notFoundText'), btn(t('store.explore'), { kind: 'soft', onclick: () => go('/store') })));
    else replace(page, errorState(e, () => storeApp(ctx)));
  }
}

function lightbox(path) {
  const closeBtn = iconBtn('close', t('common.close'), () => close());
  const el = h('div', { class: 'lightbox', role: 'dialog', 'aria-modal': 'true' }, apiImg(path, '', ''), closeBtn);
  const close = () => { el.remove(); document.removeEventListener('keydown', esc, true); };
  const esc = (e) => { if (e.key === 'Escape') { e.stopPropagation(); close(); } };
  el.addEventListener('click', (e) => { if (e.target === el) close(); });
  document.addEventListener('keydown', esc, true);
  document.body.appendChild(el);
  closeBtn.focus();
}

export async function storeCreators(ctx) {
  const { page } = ctx;
  add(page, skeletonList(5));
  async function load() {
    try {
      const r = await get('/store/creators');
      if (!r.creators.length) { replace(page, empty('user', t('store.noCreators'), t('store.noCreatorsText'))); return; }
      replace(page, h('p', { class: 'muted small mt8', text: t('store.creatorsText') }), h('div', { class: 'list mt12' }, r.creators.map((c, i) => h('a', { class: 'rank-row', href: `#${userPath(c.online_id)}` },
        h('span', { class: `rank-n ${i < 3 ? `r${i + 1}` : ''}`, text: String(i + 1) }), avatar(c, 44),
        h('span', { class: 'rank-main' }, h('strong', { text: c.online_id }), h('small', { text: `${t('store.appsN', { n: c.apps })} · ${compact(c.likes)} ${t('store.likes').toLowerCase()}` })),
        h('span', { class: 'rank-val', text: `${compact(c.downloads)} ${t('store.installs').toLowerCase()}` })))));
    } catch (e) { replace(page, errorState(e, load)); }
  }
  ctx.onRefresh(load);
  await load();
}
