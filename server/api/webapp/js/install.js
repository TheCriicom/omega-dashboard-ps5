// Installazione come app: beforeinstallprompt (Android/PC) o istruzioni (iPhone/iPad).
import { t } from './i18n.js';
import { h, icon, iconBtn, btn, sheet } from './ui.js';
import { pref, setPref } from './state.js';

let deferred = null;
window.addEventListener('beforeinstallprompt', (e) => { e.preventDefault(); deferred = e; });
window.addEventListener('appinstalled', () => { deferred = null; setPref('installed', true); document.querySelector('.install-banner')?.remove(); });

export const isStandalone = () => matchMedia('(display-mode: standalone)').matches || navigator.standalone === true;
export const isIOS = () => /iphone|ipad|ipod/i.test(navigator.userAgent) || (navigator.platform === 'MacIntel' && navigator.maxTouchPoints > 1);
export const canInstall = () => !isStandalone() && (!!deferred || isIOS());

export async function promptInstall() {
  if (deferred) {
    deferred.prompt();
    const r = await deferred.userChoice.catch(() => null);
    deferred = null;
    if (r && r.outcome === 'accepted') setPref('installed', true);
    return;
  }
  if (isIOS()) { iosSheet(); return; }
  sheet({ title: t('pwa.title'), content: h('p', { class: 'muted', text: t('pwa.desktopHint') }) });
}

function iosSheet() {
  sheet({
    title: t('pwa.iosTitle'), subtitle: t('pwa.iosSub'),
    content: h('div', { class: 'ios-steps' },
      h('div', { class: 'ios-step' }, h('span', { class: 'lr-ic c-accent' }, icon('upload', 20)), h('span', { text: t('pwa.ios1') })),
      h('div', { class: 'ios-step' }, h('span', { class: 'lr-ic c-accent' }, icon('plus', 20)), h('span', { text: t('pwa.ios2') })),
      h('div', { class: 'ios-step' }, h('span', { class: 'lr-ic c-accent' }, icon('check', 20)), h('span', { text: t('pwa.ios3') }))),
  });
}

export function installBanner() {
  if (isStandalone() || pref('installed', false)) return;
  const dismissed = pref('installDismissed', 0);
  if (dismissed && Date.now() - dismissed < 14 * 86400000) return;
  // su Android/PC aspetta l'evento; su iOS mostra le istruzioni
  const show = () => {
    if (document.querySelector('.install-banner') || isStandalone()) return;
    const el = h('div', { class: 'install-banner', role: 'region', 'aria-label': t('pwa.title') },
      h('img', { class: 'ib-ic', src: '/app/icons/icon-192.png', alt: '' }),
      h('div', { class: 'ib-txt' }, h('strong', { text: t('pwa.title') }), h('small', { text: isIOS() ? t('pwa.bannerIos') : t('pwa.banner') })),
      btn(isIOS() ? t('pwa.how') : t('pwa.install'), { size: 'sm', onclick: () => { el.remove(); promptInstall(); } }),
      iconBtn('close', t('common.close'), () => { setPref('installDismissed', Date.now()); el.classList.remove('in'); setTimeout(() => el.remove(), 300); }));
    document.body.appendChild(el);
    requestAnimationFrame(() => el.classList.add('in'));
  };
  if (deferred || isIOS()) show();
  else window.addEventListener('beforeinstallprompt', () => setTimeout(show, 800), { once: true });
}
