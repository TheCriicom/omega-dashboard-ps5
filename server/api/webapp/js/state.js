// Stato condiviso dell'app (ultimo /sync) e un piccolo bus di eventi.
export const state = {
  me: null,            // /sync.me + online_id
  account: null,       // /me
  counts: { unread_notifications: 0, unread_messages: 0, incoming_requests: 0, outgoing_requests: 0, unread_groups: 0 },
  friends: [],
  party: null,
  partyInvites: [],
  lastNotificationId: 0,
  synced: false,
};

const bus = new EventTarget();
export function on(name, fn) {
  const f = (e) => fn(e.detail);
  bus.addEventListener(name, f);
  return () => bus.removeEventListener(name, f);
}
export function emit(name, detail) { bus.dispatchEvent(new CustomEvent(name, { detail })); }

const PREF = 'omega.prefs';
let prefs = {};
try { prefs = JSON.parse(localStorage.getItem(PREF) || '{}') || {}; } catch { prefs = {}; }
export function pref(k, def) { return prefs[k] === undefined ? def : prefs[k]; }
export function setPref(k, v) {
  prefs[k] = v;
  try { localStorage.setItem(PREF, JSON.stringify(prefs)); } catch { /* privato */ }
}
