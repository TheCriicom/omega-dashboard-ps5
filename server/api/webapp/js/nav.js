// Navigazione (hash router) usabile da tutte le viste senza dipendenze circolari.
export function go(path, { replace = false } = {}) {
  if (window.omegaNav) { window.omegaNav(path, { replace }); return; }
  location.hash = `#${path}`;
}
export const E = encodeURIComponent;
export const userPath = (oid) => `/user/${E(oid)}`;
export const chatPath = (oid) => `/messages/${E(oid)}`;
export const appPath = (id) => `/store/app/${E(id)}`;

// dove porta una notifica
export function notifTarget(n) {
  switch (n.type) {
    case 'message': return n.ref ? chatPath(n.ref) : '/messages';
    case 'friend_request': return '/friends?tab=requests';
    case 'friend_accept': case 'online': return n.ref ? userPath(n.ref) : '/friends';
    case 'party_invite': return '/party';
    case 'post_like': case 'post_comment': return n.ref ? `/post/${E(n.ref)}` : '/community';
    case 'store_comment': case 'store_recommend': case 'store_update': return n.ref ? appPath(n.ref) : '/store';
    case 'game_start': return n.ref ? `/game/${E(n.ref)}` : (n.actor ? userPath(n.actor) : '/friends');
    case 'game_invite': return n.actor ? userPath(n.actor) : '/friends';
    default: return null;
  }
}
