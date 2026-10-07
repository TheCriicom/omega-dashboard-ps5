// "Perché Omega": tre schede da scorrere al primo avvio.
import { t } from './i18n.js';
import { h, icon, mark, btn } from './ui.js';
import { installBanner } from './install.js';

export function onboarding(onDone) {
  const cards = [
    { art: () => mark(110), title: t('onb.t1'), text: t('onb.p1') },
    { art: () => icon('friends', 96), title: t('onb.t2'), text: t('onb.p2') },
    { art: () => icon('store', 96), title: t('onb.t3'), text: t('onb.p3') },
  ];
  const track = h('div', { class: 'onb-track' }, cards.map((c, i) => h('section', { class: 'onb-card', 'aria-roledescription': 'slide', 'aria-label': `${i + 1} / ${cards.length}` },
    h('div', { class: 'onb-art' }, c.art()), h('h2', { text: c.title }), h('p', { text: c.text }))));
  const dots = h('div', { class: 'onb-dots', 'aria-hidden': 'true' }, cards.map(() => h('span')));
  const next = btn(t('onb.next'), { size: 'lg', kind: 'grad' });
  const wrap = h('div', { class: 'onb', role: 'dialog', 'aria-modal': 'true', 'aria-label': t('onb.aria') },
    h('button', { class: 'onb-skip', type: 'button', text: t('onb.skip'), onclick: finish }),
    track, h('div', { class: 'onb-foot' }, dots, next));
  let idx = 0;
  const sync = () => {
    idx = Math.round(track.scrollLeft / Math.max(1, track.clientWidth));
    [...dots.children].forEach((d, i) => d.classList.toggle('on', i === idx));
    next.querySelector('span').textContent = idx === cards.length - 1 ? t('onb.start') : t('onb.next');
  };
  track.addEventListener('scroll', () => requestAnimationFrame(sync), { passive: true });
  next.addEventListener('click', () => {
    if (idx >= cards.length - 1) { finish(); return; }
    track.scrollTo({ left: (idx + 1) * track.clientWidth, behavior: 'smooth' });
  });
  wrap.addEventListener('keydown', (e) => {
    if (e.key === 'ArrowRight') track.scrollTo({ left: (idx + 1) * track.clientWidth, behavior: 'smooth' });
    if (e.key === 'ArrowLeft') track.scrollTo({ left: (idx - 1) * track.clientWidth, behavior: 'smooth' });
    if (e.key === 'Escape') finish();
  });
  function finish() {
    wrap.style.transition = 'opacity .3s'; wrap.style.opacity = '0';
    setTimeout(() => wrap.remove(), 300);
    onDone();
    setTimeout(() => installBanner(), 1500);
  }
  document.body.appendChild(wrap);
  sync();
  next.focus();
}
