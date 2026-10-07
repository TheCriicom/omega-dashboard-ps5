// Accesso, registrazione e accettazione dei termini aggiornati.
import { t } from './i18n.js';
import { auth, post } from './api.js';
import { h, icon, iconBtn, mark, btn, busy, segmented, errorText, toast } from './ui.js';

const ONLINE_ID = /^[A-Za-z][A-Za-z0-9_-]{2,15}$/;

function pwField(name, label, autocomplete) {
  const input = h('input', { class: 'input', type: 'password', name, id: `a-${name}`, autocomplete, required: true, minlength: 8, maxlength: 128 });
  const eye = iconBtn('eye', t('auth.showPw'), () => {
    const show = input.type === 'password';
    input.type = show ? 'text' : 'password';
    eye.querySelector('use').setAttribute('href', show ? '#i-eyeoff' : '#i-eye');
    eye.setAttribute('aria-label', show ? t('auth.hidePw') : t('auth.showPw'));
  });
  return { input, el: h('label', { class: 'field', for: `a-${name}` }, h('span', { class: 'field-label', text: label }), h('div', { class: 'pw-wrap' }, input, eye)) };
}

export function authView(onDone) {
  let mode = 'login';
  const card = h('div', { class: 'auth-card' });
  const seg = segmented([{ value: 'login', label: t('auth.login') }, { value: 'register', label: t('auth.register') }], mode, (v) => { mode = v; draw(); }, { label: t('auth.mode') });
  seg.classList.add('full');

  function draw() {
    card.replaceChildren(seg, mode === 'login' ? loginForm() : registerForm());
  }

  function loginForm() {
    const oid = h('input', { class: 'input', name: 'online_id', id: 'a-oid', autocomplete: 'username', autocapitalize: 'none', spellcheck: 'false', required: true, maxlength: 16, placeholder: t('auth.oidPh') });
    const pw = pwField('password', t('auth.password'), 'current-password');
    const err = h('p', { class: 'form-error', role: 'alert' });
    const go = btn(t('auth.loginBtn'), { type: 'submit', block: true, size: 'lg' });
    const form = h('form', { class: 'form', novalidate: true },
      h('label', { class: 'field', for: 'a-oid' }, h('span', { class: 'field-label', text: t('auth.onlineId') }), oid),
      pw.el, err, go,
      h('p', { class: 'small muted center', text: t('auth.sameAccount') }));
    form.addEventListener('submit', async (e) => {
      e.preventDefault();
      err.textContent = '';
      if (!oid.value.trim() || !pw.input.value) { err.textContent = t('auth.fillAll'); return; }
      await busy(go, async () => {
        try {
          const s = await post('/auth/login', { online_id: oid.value.trim(), password: pw.input.value });
          auth.set(s);
          onDone();
        } catch (ex) { err.textContent = errorText(ex); }
      });
    });
    setTimeout(() => { if (matchMedia('(pointer: fine)').matches) oid.focus(); }, 50);
    return form;
  }

  function registerForm() {
    const oid = h('input', { class: 'input', name: 'online_id', id: 'r-oid', autocomplete: 'username', autocapitalize: 'none', spellcheck: 'false', maxlength: 16, placeholder: t('auth.oidPh') });
    const email = h('input', { class: 'input', type: 'email', name: 'email', id: 'r-email', autocomplete: 'email', placeholder: t('auth.emailPh') });
    const pw = pwField('new-password', t('auth.password'), 'new-password');
    const pw2 = pwField('new-password2', t('auth.password2'), 'new-password');
    const key = h('input', { class: 'input', name: 'registration_key', id: 'r-key', autocomplete: 'off' });
    const keyField = h('label', { class: 'field', for: 'r-key', hidden: true }, h('span', { class: 'field-label', text: t('auth.regKey') }), key, h('small', { class: 'field-hint', text: t('auth.regKeyHint') }));
    const terms = h('input', { type: 'checkbox', id: 'r-terms' });
    const err = h('p', { class: 'form-error', role: 'alert' });
    const go = btn(t('auth.registerBtn'), { type: 'submit', block: true, size: 'lg' });
    const form = h('form', { class: 'form', novalidate: true },
      h('label', { class: 'field', for: 'r-oid' }, h('span', { class: 'field-label', text: t('auth.onlineId') }), oid, h('small', { class: 'field-hint', text: t('auth.oidHint') })),
      h('label', { class: 'field', for: 'r-email' }, h('span', { class: 'field-label', text: t('auth.emailOpt') }), email, h('small', { class: 'field-hint', text: t('auth.emailHint') })),
      pw.el, pw2.el, keyField,
      h('label', { class: 'check', for: 'r-terms' }, terms, h('span', {},
        t('auth.accept1'), ' ', h('a', { href: '/legal/terms', target: '_blank', rel: 'noopener', text: t('auth.terms') }), ' ', t('auth.accept2'), t('auth.accept2').endsWith("'") ? '' : ' ',
        h('a', { href: '/legal/privacy', target: '_blank', rel: 'noopener', text: t('auth.privacy') }), '.')),
      err, go);
    form.addEventListener('submit', async (e) => {
      e.preventDefault();
      err.textContent = '';
      const id = oid.value.trim();
      if (!ONLINE_ID.test(id)) { err.textContent = t('err.invalid_online_id'); oid.focus(); return; }
      if (pw.input.value.length < 8) { err.textContent = t('err.invalid_password'); pw.input.focus(); return; }
      if (pw.input.value.toLowerCase().includes(id.toLowerCase())) { err.textContent = t('auth.pwHasId'); pw.input.focus(); return; }
      if (pw.input.value !== pw2.input.value) { err.textContent = t('auth.pwMismatch'); pw2.input.focus(); return; }
      if (!terms.checked) { err.textContent = t('err.terms_not_accepted'); return; }
      await busy(go, async () => {
        try {
          const body = { online_id: id, password: pw.input.value, accept_terms: true };
          if (email.value.trim()) body.email = email.value.trim();
          if (key.value.trim()) body.registration_key = key.value.trim();
          await post('/auth/register', body);
          const s = await post('/auth/login', { online_id: id, password: pw.input.value });
          auth.set(s);
          toast(t('auth.welcome', { name: id }), { kind: 'ok' });
          onDone();
        } catch (ex) {
          if (ex.code === 'registration_closed') { keyField.hidden = false; key.focus(); }
          err.textContent = errorText(ex);
        }
      });
    });
    return form;
  }

  draw();
  return h('div', { class: 'auth' }, h('div', { class: 'auth-box' },
    h('div', { class: 'auth-brand' }, mark(64), h('h1', { text: 'Omega' }), h('p', { text: t('auth.tagline') })),
    card,
    h('div', { class: 'auth-foot' },
      h('span', {}, t('auth.noConsole'), ' ', h('a', { href: '/installa', text: t('auth.installLink') })),
      h('span', {}, h('a', { href: '/legal/terms', target: '_blank', rel: 'noopener', text: t('auth.terms') }), ' · ',
        h('a', { href: '/legal/privacy', target: '_blank', rel: 'noopener', text: t('auth.privacy') }), ' · ',
        h('a', { href: '/source', target: '_blank', rel: 'noopener', text: t('auth.source') })))));
}

export function termsView(terms, onDone) {
  const go = btn(t('terms.accept'), { block: true, size: 'lg' });
  const err = h('p', { class: 'form-error', role: 'alert' });
  go.addEventListener('click', () => busy(go, async () => {
    try { await post('/account/terms', { version: terms.current }); onDone(); } catch (ex) { err.textContent = errorText(ex); }
  }));
  return h('div', { class: 'auth' }, h('div', { class: 'auth-box' },
    h('div', { class: 'auth-brand' }, h('div', { class: 'empty-ic' }, icon('shield', 30)), h('h1', { text: t('terms.title') }), h('p', { text: t('terms.text') })),
    h('div', { class: 'auth-card stack' },
      h('a', { class: 'lrow', href: '/legal/terms', target: '_blank', rel: 'noopener' }, h('span', { class: 'lr-ic c-accent' }, icon('info', 20)), h('span', { class: 'lr-main' }, h('span', { class: 'lr-title', text: t('auth.terms') })), icon('external', 18, 'lr-chev')),
      h('a', { class: 'lrow', href: '/legal/privacy', target: '_blank', rel: 'noopener' }, h('span', { class: 'lr-ic c-accent' }, icon('lock', 20)), h('span', { class: 'lr-main' }, h('span', { class: 'lr-title', text: t('auth.privacy') })), icon('external', 18, 'lr-chev')),
      err, go,
      btn(t('nav.logout'), { kind: 'ghost', block: true, onclick: () => { auth.clear(); location.reload(); } }))));
}
