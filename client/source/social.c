// Omega UI — modello social. Le chiamate sono asincrone (net_req) e le risposte
// aggiornano S sul thread principale. Il sync periodico porta contatori, amici
// con presenza, party, inviti e notifiche nuove, che diventano toast.
#include "app.h"
#include <stdlib.h>
#include <time.h>

Social S;
Chat CH;
Profile PR;
char g_token[700];

static Uint32 last_sync; static int sync_busy;
#define SYNC_MS      4000
#define CHAT_POLL_MS 2500

// -------------------------------------------------------------------- tempo --
static long days_from_civil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  long era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = (unsigned)(y - era * 400);
  unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (long)doe - 719468;
}
long iso_epoch(const char *iso) {
  int Y, M, D, h, m, s;
  if (!iso || sscanf(iso, "%d-%d-%dT%d:%d:%d", &Y, &M, &D, &h, &m, &s) != 6) return 0;
  return days_from_civil(Y, (unsigned)M, (unsigned)D) * 86400L + h * 3600L + m * 60L + s;
}
static long now_epoch(void) {
  if (S.server_epoch) return S.server_epoch + (long)((SDL_GetTicks() - S.server_ticks) / 1000);
  return (long)time(NULL);
}
void rel_time(const char *iso, char *out, size_t n) {
  long t = iso_epoch(iso);
  if (!t) { out[0] = 0; return; }
  long d = now_epoch() - t; if (d < 0) d = 0;
  if (d < 60) snprintf(out, n, "%s", _("adesso"));
  else if (d < 3600) snprintf(out, n, _("%ld min fa"), d / 60);
  else if (d < 86400) snprintf(out, n, _("%ld h fa"), d / 3600);
  else if (d < 86400 * 7) snprintf(out, n, _("%ld g fa"), d / 86400);
  else snprintf(out, n, _("%ld sett. fa"), d / (86400 * 7));
}
void play_time(const char *iso, char *out, size_t n) {
  long t = iso_epoch(iso);
  if (!t) { out[0] = 0; return; }
  long d = now_epoch() - t; if (d < 0) d = 0;
  if (d < 60) snprintf(out, n, "%s", _("ha appena iniziato"));
  else if (d < 3600) snprintf(out, n, _("da %ld min"), d / 60);
  else snprintf(out, n, _("da %ld h %ld min"), d / 3600, (d % 3600) / 60);
}

int friend_online(const Friend *f) { return strcmp(f->status, "offline") != 0 && f->status[0]; }
const Friend *friend_find(const char *oid) {
  for (int i = 0; i < S.nfriends; i++) if (!strcasecmp(S.friends[i].oid, oid)) return &S.friends[i];
  return NULL;
}
int friends_online_count(void) {
  int n = 0; for (int i = 0; i < S.nfriends; i++) n += friend_online(&S.friends[i]); return n;
}

// ------------------------------------------------------------------ parsing --
static void parse_friend(Friend *f, JVal *o) {
  memset(f, 0, sizeof *f);
  jcpy(f->oid, sizeof f->oid, o, "online_id");
  f->avatar = (int)jnum(o, "avatar", 0);
  jcpy(f->about, sizeof f->about, o, "about_me");
  JVal *p = jget(o, "presence");
  jcpy(f->status, sizeof f->status, p, "status");
  if (!f->status[0]) snprintf(f->status, sizeof f->status, "offline");
  jcpy(f->game_id, sizeof f->game_id, p, "game_id");
  jcpy(f->game_name, sizeof f->game_name, p, "game_name");
  jcpy(f->started, sizeof f->started, p, "started_at");
  jcpy(f->last_seen, sizeof f->last_seen, p, "last_seen");
  jcpy(f->status_msg, sizeof f->status_msg, o, "status_message");
  if (!f->status_msg[0]) jcpy(f->status_msg, sizeof f->status_msg, p, "status_message");
  media_note_json(f->oid, o);
}

static void parse_userref(UserRef *u, JVal *o, const char *oidkey) {
  memset(u, 0, sizeof *u);
  jcpy(u->oid, sizeof u->oid, o, oidkey);
  u->avatar = (int)jnum(o, "avatar", 0);
  jcpy(u->when, sizeof u->when, o, "since");
  if (!u->when[0]) jcpy(u->when, sizeof u->when, o, "started_at");
  if (!u->when[0]) jcpy(u->when, sizeof u->when, o, "last_played");
  jcpy(u->relation, sizeof u->relation, o, "relation");
  media_note_json(u->oid, o);
}

static void parse_notif(Notif *n, JVal *o) {
  memset(n, 0, sizeof *n);
  jcpy(n->id, sizeof n->id, o, "notification_id");
  jcpy(n->type, sizeof n->type, o, "type");
  jcpy(n->title, sizeof n->title, o, "title");
  jcpy(n->body, sizeof n->body, o, "body");
  jcpy(n->ref, sizeof n->ref, o, "ref");
  jcpy(n->actor, sizeof n->actor, o, "actor");
  n->avatar = (int)jnum(o, "avatar", 0);
  n->read = jbool(o, "read");
  jcpy(n->when, sizeof n->when, o, "created_at");
}

static void parse_news(News *n, JVal *o) {
  memset(n, 0, sizeof *n);
  jcpy(n->id, sizeof n->id, o, "news_id");
  jcpy(n->title, sizeof n->title, o, "title");
  jcpy(n->body, sizeof n->body, o, "body");
  jcpy(n->tag, sizeof n->tag, o, "tag");
  jcpy(n->game_id, sizeof n->game_id, o, "game_id");
  n->color = (int)jnum(o, "color", 0);
  jcpy(n->when, sizeof n->when, o, "created_at");
  jcpy(n->link, sizeof n->link, o, "link");
  n->has_image = jbool(o, "has_image");
}

static void parse_msg(Msg *m, JVal *o, int party) {
  memset(m, 0, sizeof *m);
  jcpy(m->id, sizeof m->id, o, "message_id");
  jcpy(m->body, sizeof m->body, o, "body");
  jcpy(m->when, sizeof m->when, o, "created_at");
  if (party) {
    jcpy(m->sender, sizeof m->sender, o, "sender");
    m->avatar = (int)jnum(o, "avatar", 0);
    m->mine = !strcasecmp(m->sender, S.me);
    m->system = !strncmp(m->body, "\xC2\xB7 ", 3);   // messaggi di servizio: "· x ha lasciato il party"
  } else {
    m->mine = jbool(o, "mine");
  }
}

int notif_icon(const char *type) {
  if (!strcmp(type, "friend_request")) return IC_ADDUSER;
  if (!strcmp(type, "friend_accept")) return IC_FRIENDS;
  if (!strcmp(type, "message")) return IC_CHAT;
  if (!strcmp(type, "party_invite")) return IC_PARTY;
  if (!strcmp(type, "game_start")) return IC_GAMEPAD;
  if (!strcmp(type, "online")) return IC_USER;
  if (!strcmp(type, "game_invite")) return IC_GAMEPAD;
  if (!strcmp(type, "post_like")) return IC_LIKE;
  if (!strcmp(type, "post_comment")) return IC_NEWS;
  return IC_BELL;
}

static void parse_party(JVal *p) {
  Party *P = &S.party;
  if (!p || p->t != J_OBJ) { memset(P, 0, sizeof *P); return; }
  char oldid[24]; snprintf(oldid, sizeof oldid, "%s", P->id);
  memset(P, 0, sizeof *P);
  P->active = 1;
  jcpy(P->id, sizeof P->id, p, "party_id");
  jcpy(P->name, sizeof P->name, p, "name");
  P->owner = jbool(p, "owner");
  jcpy(P->last_msg_id, sizeof P->last_msg_id, p, "last_message_id");
  JFOR(m, jget(p, "members")) {
    if (P->nmembers >= 16) break;
    PartyMember *pm = &P->members[P->nmembers++];
    jcpy(pm->oid, sizeof pm->oid, m, "online_id");
    pm->avatar = (int)jnum(m, "avatar", 0);
    pm->owner = jbool(m, "owner"); pm->muted = jbool(m, "muted");
    media_note_json(pm->oid, m);
    JVal *pr = jget(m, "presence");
    jcpy(pm->status, sizeof pm->status, pr, "status");
    jcpy(pm->game_name, sizeof pm->game_name, pr, "game_name");
  }
  // chi sta parlando: il server lo ricava dai pacchetti voce degli ultimi 600 ms
  JFOR(t, jget(p, "talking"))
    for (int i = 0; i < P->nmembers; i++) if (!strcasecmp(P->members[i].oid, jstr(t, NULL, ""))) P->members[i].talking = 1;
  JFOR(m, jget(p, "invited")) { if (P->ninvited >= 16) break; parse_userref(&P->invited[P->ninvited++], m, "online_id"); }
  if (strcmp(oldid, P->id)) S.npmsg = 0;      // party nuovo: la chat si ricarica
}

// --------------------------------------------------------------------- sync --
static void on_sync(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; (void)ud;
  sync_busy = 0;
  if (st == 401) { set_msg(_("Sessione scaduta: accedi di nuovo"), 1); do_logout(); return; }
  if (st != 200 || !j) return;
  const char *st_iso = jstr(j, "server_time", NULL);
  if (st_iso) { S.server_epoch = iso_epoch(st_iso); S.server_ticks = SDL_GetTicks(); }
  JVal *me = jget(j, "me");
  jcpy(S.me, sizeof S.me, me, "online_id");
  S.my_avatar = (int)jnum(me, "avatar", 0);
  jcpy(S.my_about, sizeof S.my_about, me, "about_me");
  jcpy(S.status_mode, sizeof S.status_mode, me, "status_mode");
  jcpy(S.status_msg, sizeof S.status_msg, me, "status_message");
  S.dnd = jbool(me, "dnd") || !strcmp(S.status_mode, "dnd");
  media_note_json(S.me, me);
  S.unread_notif = (int)jnum(j, "unread_notifications", 0);
  S.unread_msg = (int)jnum(j, "unread_messages", 0);
  S.in_req = (int)jnum(j, "incoming_requests", 0);
  S.out_req = (int)jnum(j, "outgoing_requests", 0);
  S.unread_groups = (int)jnum(j, "unread_groups", 0);

  S.nfriends = 0;
  JFOR(f, jget(j, "friends")) { if (S.nfriends >= MAX_FRIENDS) break; parse_friend(&S.friends[S.nfriends++], f); }

  int had_party = S.party.active;
  char last_pm[24]; snprintf(last_pm, sizeof last_pm, "%s", S.party.last_msg_id);
  parse_party(jget(j, "party"));
  if (S.party.active && (strcmp(last_pm, S.party.last_msg_id) || !had_party)) social_party_messages();

  S.ninv = 0;
  JFOR(i, jget(j, "party_invites")) {
    if (S.ninv >= 16) break;
    PartyInvite *pi = &S.inv[S.ninv++];
    jcpy(pi->party_id, sizeof pi->party_id, i, "party_id");
    jcpy(pi->name, sizeof pi->name, i, "name");
    jcpy(pi->from, sizeof pi->from, i, "from_online_id");
    pi->avatar = (int)jnum(i, "avatar", 0);
    pi->members = (int)jnum(i, "members", 0);
  }

  // le notifiche nuove diventano toast
  long last = atol(jstr(j, "last_notification_id", "0"));
  int had_new = 0;
  JFOR(n, jget(j, "notifications")) {
    Notif nn; parse_notif(&nn, n);
    // con "non disturbare" restano nella lista ma senza toast
    if (!S.dnd) toast(notif_icon(nn.type), nn.actor, nn.avatar, nn.title, nn.type[0] == 'm' ? nn.body : (nn.body[0] ? nn.body : NULL));
    had_new = 1;
    if (!strcmp(nn.type, "friend_request") || !strcmp(nn.type, "friend_accept")) social_load_friends();
    if (!strcmp(nn.type, "message") && CH.open && !CH.party && !strcasecmp(CH.oid, nn.ref)) chat_poll();
  }
  if (had_new && ov_top() == OV_NOTIF) social_load_notifications();
  if (had_new && ov_top() == OV_GAMEBASE) social_load_conversations();
  S.last_notif_id = last;
  S.synced_once = 1;
}

void social_sync_now(void) {
  if (!g_token[0] || sync_busy) return;
  sync_busy = 1; last_sync = SDL_GetTicks();
  char path[96]; snprintf(path, sizeof path, OMEGA_API "/sync?since=%ld", S.synced_once ? S.last_notif_id : 0L);
  net_req(HTTP_GET, path, NULL, on_sync, NULL);
}

void social_tick(void) {
  if (!g_token[0]) return;
  if (SDL_GetTicks() - last_sync > SYNC_MS) social_sync_now();
  if (CH.open && SDL_GetTicks() - CH.last_poll > CHAT_POLL_MS) chat_poll();
}

void social_reset(void) {
  memset(&S, 0, sizeof S); memset(&CH, 0, sizeof CH); memset(&PR, 0, sizeof PR);
  sync_busy = 0; last_sync = 0;
}

// -------------------------------------------------------- liste a richiesta --
static void on_friends(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; (void)ud;
  if (st != 200 || !j) return;
  S.nfriends = 0;
  JFOR(f, jget(j, "friends")) { if (S.nfriends >= MAX_FRIENDS) break; parse_friend(&S.friends[S.nfriends++], f); }
  S.nin = 0; JFOR(u, jget(j, "incoming_requests")) { if (S.nin >= MAX_REQ) break; parse_userref(&S.in[S.nin++], u, "online_id"); }
  S.nout = 0; JFOR(u, jget(j, "outgoing_requests")) { if (S.nout >= MAX_REQ) break; parse_userref(&S.out[S.nout++], u, "online_id"); }
  S.in_req = S.nin; S.out_req = S.nout;
  // stesso ordine del sync: chi gioca, chi è online, poi gli altri
  for (int a = 0; a < S.nfriends; a++) for (int b = a + 1; b < S.nfriends; b++) {
    Friend *x = &S.friends[a], *y = &S.friends[b];
    int rx = x->game_id[0] ? 0 : friend_online(x) ? 1 : 2, ry = y->game_id[0] ? 0 : friend_online(y) ? 1 : 2;
    if (ry < rx || (ry == rx && strcasecmp(y->oid, x->oid) < 0)) { Friend t = *x; *x = *y; *y = t; }
  }
}
void social_load_friends(void) { net_req(HTTP_GET, OMEGA_API "/friends", NULL, on_friends, NULL); }

static void on_notifs(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; (void)ud;
  if (st != 200 || !j) return;
  S.nnotif = 0;
  JFOR(n, jget(j, "notifications")) { if (S.nnotif >= MAX_NOTIF) break; parse_notif(&S.notif[S.nnotif++], n); }
}
void social_load_notifications(void) { net_req(HTTP_GET, OMEGA_API "/notifications", NULL, on_notifs, NULL); }
void social_mark_notif_read(void) {
  S.unread_notif = 0;
  net_req(HTTP_POST, OMEGA_API "/notifications/read", "{}", NULL, NULL);
}
static void on_cleared(int st, JVal *j, const char *raw, void *ud) { (void)j; (void)raw; (void)ud; if (st == 200) { S.nnotif = 0; S.unread_notif = 0; } }
void social_clear_notif(void) { net_req(HTTP_POST, OMEGA_API "/notifications/clear", "{}", on_cleared, NULL); }

static void on_news(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; (void)ud;
  if (st != 200 || !j) return;
  S.nnews = 0;
  JFOR(n, jget(j, "news")) { if (S.nnews >= MAX_NEWS) break; parse_news(&S.news[S.nnews++], n); }
}
void social_load_news(void) { net_req(HTTP_GET, OMEGA_API "/news", NULL, on_news, NULL); }

static void on_activity(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; (void)ud;
  if (st != 200 || !j) return;
  S.nact = 0;
  JFOR(a, jget(j, "activity")) {
    if (S.nact >= MAX_ACT) break;
    Activity *x = &S.act[S.nact++]; memset(x, 0, sizeof *x);
    jcpy(x->oid, sizeof x->oid, a, "online_id"); x->avatar = (int)jnum(a, "avatar", 0);
    jcpy(x->type, sizeof x->type, a, "type"); jcpy(x->game_id, sizeof x->game_id, a, "game_id");
    jcpy(x->game_name, sizeof x->game_name, a, "game_name"); jcpy(x->detail, sizeof x->detail, a, "detail");
    jcpy(x->when, sizeof x->when, a, "created_at");
    media_note_json(x->oid, a);
  }
}
void social_load_activity(void) { net_req(HTTP_GET, OMEGA_API "/activity", NULL, on_activity, NULL); }

static void on_convs(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; (void)ud;
  if (st != 200 || !j) return;
  S.nconv = 0;
  JFOR(c, jget(j, "conversations")) {
    if (S.nconv >= MAX_CONV) break;
    Conv *x = &S.conv[S.nconv++]; memset(x, 0, sizeof *x);
    jcpy(x->oid, sizeof x->oid, c, "online_id"); x->avatar = (int)jnum(c, "avatar", 0);
    x->unread = (int)jnum(c, "unread", 0); jcpy(x->last, sizeof x->last, c, "last_body");
    x->from_me = jbool(c, "last_from_me"); jcpy(x->when, sizeof x->when, c, "last_at");
    media_note_json(x->oid, c);
  }
}
void social_load_conversations(void) { net_req(HTTP_GET, OMEGA_API "/messages", NULL, on_convs, NULL); }

static void on_game(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; (void)ud;
  S.game_loading = 0;
  if (st != 200 || !j) return;
  if (strcmp(jstr(j, "game_id", ""), S.game_tid)) return;   // gioco non più a fuoco
  S.ngame_now = 0; JFOR(u, jget(j, "playing_now")) { if (S.ngame_now >= 16) break; parse_userref(&S.game_now[S.ngame_now++], u, "online_id"); }
  S.ngame_played = 0; JFOR(u, jget(j, "played")) { if (S.ngame_played >= 16) break; parse_userref(&S.game_played[S.ngame_played++], u, "online_id"); }
  S.game_players = (int)jnum(j, "players_now", 0);
  S.ngame_news = 0; JFOR(n, jget(j, "news")) { if (S.ngame_news >= 8) break; parse_news(&S.game_news[S.ngame_news++], n); }
}
void social_load_game(const char *tid) {
  if (!strcmp(S.game_tid, tid) && S.game_loading) return;
  if (strcmp(S.game_tid, tid)) { S.ngame_now = S.ngame_played = S.ngame_news = 0; S.game_players = 0; }
  snprintf(S.game_tid, sizeof S.game_tid, "%s", tid);
  S.game_loading = 1;
  // il nome del gioco serve al server per trovare le notizie che ne parlano
  char name[100] = "", enc[300];
  for (int i = 0; i < napps; i++) if (!strcmp(apps[i].tid, tid)) snprintf(name, sizeof name, "%s", apps[i].name);
  url_encode(enc, sizeof enc, name, "");
  char path[420]; snprintf(path, sizeof path, OMEGA_API "/games/%s?name=%s", tid, enc);
  net_req(HTTP_GET, path, NULL, on_game, NULL);
}

static void on_search(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; (void)ud;
  S.users_loading = 0;
  if (st != 200 || !j) return;
  S.nusers = 0;
  JFOR(u, jget(j, "users")) { if (S.nusers >= MAX_USERS) break; parse_userref(&S.users[S.nusers++], u, "online_id"); }
}
void social_search(const char *q) {
  char enc[128]; url_encode(enc, sizeof enc, q, "-_");
  char path[200]; snprintf(path, sizeof path, OMEGA_API "/users/search?q=%s", enc);
  S.users_loading = 1;
  net_req(HTTP_GET, path, NULL, on_search, NULL);
}

// ----------------------------------------------------------------- amicizie --
static void after_friend_action(int st, JVal *j, const char *raw, void *ud) {
  (void)raw;
  const char *what = ud;
  if (st >= 200 && st < 300) {
    const char *res = jstr(j, "result", "");
    char m[256];
    if (!strcmp(res, "pending")) snprintf(m, sizeof m, _("Richiesta di amicizia inviata a %s"), jstr(j, "online_id", ""));
    else if (!strcmp(res, "accepted")) snprintf(m, sizeof m, _("Tu e %s ora siete amici"), jstr(j, "online_id", ""));
    else if (!strcmp(res, "already_friends")) snprintf(m, sizeof m, "%s", _("Siete già amici"));
    else if (!strcmp(res, "declined")) snprintf(m, sizeof m, "%s", _("Richiesta rifiutata"));
    else if (!strcmp(res, "removed")) snprintf(m, sizeof m, "%s", what && !strcmp(what, "cancel") ? _("Richiesta annullata") : _("Amico rimosso"));
    else snprintf(m, sizeof m, "%s", _("Fatto"));
    set_msg(m, 0);
  } else {
    const char *e = jstr(j, "error", "");
    set_msg(!strcmp(e, "account_not_found") ? _("Utente non trovato") : !strcmp(e, "cannot_befriend_self") ? _("Non puoi aggiungere te stesso") : _("Operazione non riuscita"), 1);
  }
  social_load_friends();
  social_sync_now();
  // aggiorna la relazione nei risultati di ricerca e nel profilo aperto
  if (ov_top() == OV_SEARCH) for (int i = 0; i < S.nusers; i++) {
    const char *oid = jstr(j, "online_id", "");
    if (!strcasecmp(S.users[i].oid, oid)) {
      const char *res = jstr(j, "result", "");
      snprintf(S.users[i].relation, sizeof S.users[i].relation, "%s",
        !strcmp(res, "pending") ? "outgoing" : !strcmp(res, "accepted") || !strcmp(res, "already_friends") ? "friend" : "none");
    }
  }
  if (PR.loaded && !strcasecmp(PR.oid, jstr(j, "online_id", ""))) profile_open(PR.oid);
}
static void post_oid(const char *path, const char *oid, const char *tag) {
  char body[96]; snprintf(body, sizeof body, "{\"online_id\":\"%s\"}", oid);
  net_req(HTTP_POST, path, body, after_friend_action, (void *)tag);
}
void social_friend_request(const char *oid) { post_oid(OMEGA_API "/friends/request", oid, "request"); }
void social_friend_accept(const char *oid) { post_oid(OMEGA_API "/friends/accept", oid, "accept"); }
void social_friend_decline(const char *oid) { post_oid(OMEGA_API "/friends/decline", oid, "decline"); }
void social_friend_remove(const char *oid) {
  char path[96]; snprintf(path, sizeof path, OMEGA_API "/friends/%s", oid);
  int outgoing = 0; for (int i = 0; i < S.nout; i++) if (!strcasecmp(S.out[i].oid, oid)) outgoing = 1;
  net_req(HTTP_DELETE, path, NULL, after_friend_action, outgoing ? "cancel" : "remove");
}

// -------------------------------------------------------------------- party --
static void on_party_msgs(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; (void)ud;
  if (st != 200 || !j) return;
  S.npmsg = 0;
  JFOR(m, jget(j, "messages")) { if (S.npmsg >= MAX_MSG) break; parse_msg(&S.pmsg[S.npmsg++], m, 1); }
  if (CH.open && CH.party) {
    int grew = S.npmsg > CH.nmsg;
    memcpy(CH.msg, S.pmsg, sizeof(Msg) * (size_t)S.npmsg); CH.nmsg = S.npmsg;
    if (grew) CH.scroll_t = 0;   // torna in fondo
    CH.loading = 0;
  }
}
void social_party_messages(void) {
  if (!S.party.active) return;
  net_req(HTTP_GET, OMEGA_API "/party/messages?after=0", NULL, on_party_msgs, NULL);
}
static void on_party(int st, JVal *j, const char *raw, void *ud) {
  (void)raw;
  if (st >= 200 && st < 300) {
    JVal *p = jget(j, "party");
    if (p) parse_party(p);
    if (ud) set_msg((const char *)ud, 0);
    social_party_messages();
  } else {
    const char *e = jstr(j, "error", "");
    set_msg(!strcmp(e, "no_invite") ? _("L'invito non è più valido") : !strcmp(e, "not_friends") ? _("Puoi invitare solo i tuoi amici") : _("Operazione party non riuscita"), 1);
  }
  social_sync_now();
}
void social_party_create(void) { net_req(HTTP_POST, OMEGA_API "/party", "{}", on_party, (void *)_("Party creato")); }
void social_party_invite(const char *oid) {
  char body[96]; snprintf(body, sizeof body, "{\"online_id\":\"%s\"}", oid);
  static char m[192]; snprintf(m, sizeof m, _("Invito inviato a %s"), oid);
  net_req(HTTP_POST, OMEGA_API "/party/invite", body, on_party, m);
}
void social_party_join(const char *pid) {
  char body[64]; snprintf(body, sizeof body, "{\"party_id\":\"%s\"}", pid);
  net_req(HTTP_POST, OMEGA_API "/party/join", body, on_party, (void *)_("Sei entrato nel party"));
}
void social_party_decline(const char *pid) {
  char body[64]; snprintf(body, sizeof body, "{\"party_id\":\"%s\"}", pid);
  net_req(HTTP_POST, OMEGA_API "/party/decline", body, on_party, (void *)_("Invito rifiutato"));
}
static void on_left(int st, JVal *j, const char *raw, void *ud) {
  (void)j; (void)raw; (void)ud;
  if (st == 200) { memset(&S.party, 0, sizeof S.party); S.npmsg = 0; set_msg(_("Hai lasciato il party"), 0); if (CH.open && CH.party) CH.open = 0; }
  social_sync_now();
}
void social_party_leave(void) { net_req(HTTP_POST, OMEGA_API "/party/leave", "{}", on_left, NULL); }
void social_party_mute(int muted) {
  for (int i = 0; i < S.party.nmembers; i++) if (!strcasecmp(S.party.members[i].oid, S.me)) S.party.members[i].muted = muted;
  net_req(HTTP_POST, OMEGA_API "/party/mute", muted ? "{\"muted\":true}" : "{\"muted\":false}", NULL, NULL);
  set_msg(muted ? _("Microfono disattivato") : _("Microfono attivato"), 0);
}
static void on_sent_party(int st, JVal *j, const char *raw, void *ud) {
  (void)j; (void)raw; (void)ud;
  if (st == 201) social_party_messages(); else set_msg(_("Messaggio non inviato"), 1);
}
void social_party_send(const char *text) {
  char esc[1100], body[1200];
  json_escape(esc, sizeof esc, text);
  snprintf(body, sizeof body, "{\"text\":\"%s\"}", esc);
  net_req(HTTP_POST, OMEGA_API "/party/messages", body, on_sent_party, NULL);
}

// ------------------------------------------------------- presenza e profilo --
void social_presence(const char *status, const char *game_id, const char *game_name) {
  char body[400], gn[220] = "";
  if (game_id && game_id[0]) {
    json_escape(gn, sizeof gn, game_name ? game_name : game_id);
    snprintf(body, sizeof body, "{\"status\":\"%s\",\"game_id\":\"%s\",\"game_name\":\"%s\"}", status, game_id, gn);
  } else snprintf(body, sizeof body, "{\"status\":\"%s\"}", status);
  net_req(HTTP_POST, OMEGA_API "/presence", body, NULL, NULL);
}

static void on_profile_saved(int st, JVal *j, const char *raw, void *ud) {
  (void)j; (void)raw; (void)ud;
  set_msg(st == 200 ? _("Profilo aggiornato") : _("Salvataggio non riuscito"), st != 200);
  social_sync_now();
  if (PR.loaded && !strcasecmp(PR.oid, S.me)) profile_open(S.me);
}
void social_profile_update(const char *about, int avatar) {
  char body[500], esc[400];
  if (about) { json_escape(esc, sizeof esc, about); snprintf(body, sizeof body, "{\"about_me\":\"%s\"}", esc); S.my_about[0] = 0; snprintf(S.my_about, sizeof S.my_about, "%s", about); }
  else { snprintf(body, sizeof body, "{\"avatar\":%d}", avatar); S.my_avatar = avatar; }
  net_req(HTTP_POST, OMEGA_API "/profile", body, on_profile_saved, NULL);
}

// --------------------------------------------------------------------- chat --
static void on_thread(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; (void)ud;
  CH.loading = 0;
  if (st != 200 || !j || !CH.open || CH.party) return;
  if (strcasecmp(jstr(j, "online_id", ""), CH.oid)) return;
  int n = 0; Msg tmp[MAX_MSG];
  JFOR(m, jget(j, "messages")) { if (n >= MAX_MSG) break; parse_msg(&tmp[n++], m, 0); }
  if (n != CH.nmsg || (n && strcmp(tmp[n - 1].id, CH.msg[CH.nmsg - 1].id))) {
    memcpy(CH.msg, tmp, sizeof(Msg) * (size_t)n); CH.nmsg = n; CH.scroll_t = 0;
  }
  // il server ha già azzerato i non letti di questa conversazione
  for (int i = 0; i < S.nconv; i++) if (!strcasecmp(S.conv[i].oid, CH.oid)) { S.unread_msg -= S.conv[i].unread; S.conv[i].unread = 0; }
  if (S.unread_msg < 0) S.unread_msg = 0;
}
void chat_poll(void) {
  CH.last_poll = SDL_GetTicks();
  if (!CH.open) return;
  if (CH.party) { social_party_messages(); return; }
  char path[96]; snprintf(path, sizeof path, OMEGA_API "/messages/%s", CH.oid);
  net_req(HTTP_GET, path, NULL, on_thread, NULL);
}
void chat_open(const char *oid, int avatar) {
  memset(&CH, 0, sizeof CH);
  CH.open = 1; CH.party = 0; snprintf(CH.oid, sizeof CH.oid, "%s", oid); CH.avatar = avatar; CH.loading = 1;
  chat_poll();
  ov_push(OV_CHAT);
}
void chat_open_party(void) {
  memset(&CH, 0, sizeof CH);
  CH.open = 1; CH.party = 1; snprintf(CH.oid, sizeof CH.oid, "%s", S.party.name);
  memcpy(CH.msg, S.pmsg, sizeof(Msg) * (size_t)S.npmsg); CH.nmsg = S.npmsg; CH.loading = S.npmsg == 0;
  chat_poll();
  ov_push(OV_CHAT);
}
static void on_sent(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; (void)ud;
  if (st == 201) chat_poll();
  else set_msg(!strcmp(jstr(j, "error", ""), "not_friends") ? _("Puoi scrivere solo ai tuoi amici") : _("Messaggio non inviato"), 1);
}
void chat_send(const char *text) {
  if (!text || !*text) return;
  if (CH.party) { social_party_send(text); }
  else {
    char esc[1100], body[1200], path[96];
    json_escape(esc, sizeof esc, text);
    snprintf(body, sizeof body, "{\"text\":\"%s\"}", esc);
    snprintf(path, sizeof path, OMEGA_API "/messages/%s", CH.oid);
    net_req(HTTP_POST, path, body, on_sent, NULL);
  }
  // eco immediata, sostituita dal prossimo poll
  if (CH.nmsg < MAX_MSG) {
    Msg *m = &CH.msg[CH.nmsg++]; memset(m, 0, sizeof *m);
    snprintf(m->body, sizeof m->body, "%s", text); m->mine = 1;
    snprintf(m->sender, sizeof m->sender, "%s", S.me); m->avatar = S.my_avatar;
    snprintf(m->id, sizeof m->id, "pending");
  }
  CH.scroll_t = 0;
}

// ------------------------------------------------------------------ profilo --
static void on_profile(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; (void)ud;
  PR.loading = 0;
  if (st != 200 || !j) { if (st == 404) set_msg(_("Utente non trovato"), 1); return; }
  if (strcasecmp(jstr(j, "online_id", ""), PR.oid)) return;
  jcpy(PR.oid, sizeof PR.oid, j, "online_id");
  PR.avatar = (int)jnum(j, "avatar", 0);
  jcpy(PR.about, sizeof PR.about, j, "about_me");
  media_note_json(PR.oid, j);
  jcpy(PR.cover_media, sizeof PR.cover_media, j, "cover_media");
  PR.cover_frames = (int)jnum(j, "cover_frames", 0);
  jcpy(PR.relation, sizeof PR.relation, j, "relation");
  PR.friends_count = (int)jnum(j, "friends_count", 0);
  jcpy(PR.created, sizeof PR.created, j, "created_at");
  JVal *p = jget(j, "presence");
  jcpy(PR.status, sizeof PR.status, p, "status");
  jcpy(PR.game_name, sizeof PR.game_name, p, "game_name");
  jcpy(PR.started, sizeof PR.started, p, "started_at");
  PR.nrecent = 0;
  JFOR(a, jget(j, "recent")) {
    if (PR.nrecent >= 8) break;
    Activity *x = &PR.recent[PR.nrecent++]; memset(x, 0, sizeof *x);
    jcpy(x->type, sizeof x->type, a, "type"); jcpy(x->game_name, sizeof x->game_name, a, "game_name");
    jcpy(x->detail, sizeof x->detail, a, "detail"); jcpy(x->when, sizeof x->when, a, "created_at");
  }
  jcpy(PR.status_msg, sizeof PR.status_msg, j, "status_message");
  PR.mutual_friends = (int)jnum(j, "mutual_friends", 0);
  PR.blocked = jbool(j, "blocked");
  PR.nmutual = 0;
  JFOR(m, jget(j, "mutual")) { if (PR.nmutual >= 5) break; jcpy(PR.mutual[PR.nmutual], sizeof PR.mutual[0], m, "online_id"); media_note_json(PR.mutual[PR.nmutual], m); PR.nmutual++; }
  PR.total_seconds = (long)jnum(jget(j, "stats"), "total_seconds", 0);
  PR.ngames = 0;
  JFOR(g, jget(j, "games")) {
    if (PR.ngames >= 6) break;
    jcpy(PR.games[PR.ngames].game_id, sizeof PR.games[0].game_id, g, "game_id");
    jcpy(PR.games[PR.ngames].game_name, sizeof PR.games[0].game_name, g, "game_name");
    jcpy(PR.games[PR.ngames].last, sizeof PR.games[0].last, g, "last_played");
    PR.games[PR.ngames].sessions = (int)jnum(g, "sessions", 0);
    PR.ngames++;
  }
  PR.loaded = 1;
}
void profile_open(const char *oid) {
  int same = PR.loaded && !strcasecmp(PR.oid, oid);
  if (!same) { memset(&PR, 0, sizeof PR); snprintf(PR.oid, sizeof PR.oid, "%s", oid); }
  PR.loading = 1;
  char path[96]; snprintf(path, sizeof path, OMEGA_API "/users/%s", oid);
  net_req(HTTP_GET, path, NULL, on_profile, NULL);
  if (ov_top() != OV_PROFILE) ov_push(OV_PROFILE);
}

// --------------------------------------------------- stato, blocchi, inviti --
static void on_status(int st, JVal *j, const char *raw, void *ud) {
  (void)j; (void)raw; (void)ud;
  if (st == 200) { set_msg(_("Stato aggiornato"), 0); social_sync_now(); }
  else set_msg(_("Stato non aggiornato"), 1);
}
void social_set_status(const char *mode, const char *message) {
  char e[200], body[260];
  json_escape(e, sizeof e, message ? message : "");
  snprintf(body, sizeof body, "{\"mode\":\"%s\",\"message\":\"%s\"}", mode, e);
  snprintf(S.status_mode, sizeof S.status_mode, "%s", mode);
  net_req(HTTP_POST, OMEGA_API "/status", body, on_status, NULL);
}

static const char *MODES[] = { "online", "away", "dnd", "invisible" };
static void status_pick(int idx, void *ud) {
  (void)ud;
  if (idx >= 0 && idx < 4) { social_set_status(MODES[idx], S.status_msg); return; }
  if (idx == 4) {
    char m[64]; snprintf(m, sizeof m, "%s", S.status_msg);
    if (!edit_text(_("Il tuo messaggio di stato (es. \"Cerco gente per Warzone\")"), m, sizeof m, 0)) return;
    snprintf(S.status_msg, sizeof S.status_msg, "%s", m);
    social_set_status(S.status_mode[0] ? S.status_mode : "online", m);
  }
}
void status_menu(void) {
  static char l[5][160]; static const char *items[5];
  static const char *names[4] = { N_("Online"), N_("Assente"), N_("Non disturbare (niente avvisi)"), N_("Invisibile (appari offline)") };
  for (int i = 0; i < 4; i++) {
    snprintf(l[i], sizeof l[i], !strcmp(S.status_mode[0] ? S.status_mode : "online", MODES[i]) ? _("%s  (attuale)") : "%s", _(names[i]));
    items[i] = l[i];
  }
  if (S.status_msg[0]) snprintf(l[4], sizeof l[4], _("Messaggio: %s"), S.status_msg);
  else snprintf(l[4], sizeof l[4], "%s", _("Scrivi un messaggio di stato"));
  items[4] = l[4];
  menu_open(_("Il tuo stato"), items, 5, status_pick, NULL);
}

static void on_block(int st, JVal *j, const char *raw, void *ud) {
  (void)j; (void)raw;
  if (st >= 200 && st < 300) { set_msg(ud ? _("Utente bloccato") : _("Utente sbloccato"), 0); social_load_friends(); if (ov_top() == OV_PROFILE) profile_open(PR.oid); }
  else set_msg(_("Operazione non riuscita"), 1);
}
void social_block(const char *oid, int block) {
  char path[96]; snprintf(path, sizeof path, OMEGA_API "/users/%s/block", oid);
  net_req(block ? HTTP_POST : HTTP_DELETE, path, block ? "{}" : NULL, on_block, block ? (void *)1 : NULL);
}

static void on_reported(int st, JVal *j, const char *raw, void *ud) {
  (void)j; (void)raw; (void)ud;
  set_msg(st >= 200 && st < 300 ? _("Grazie, la segnalazione è stata inviata") : _("Segnalazione non riuscita"), !(st >= 200 && st < 300));
}
void social_report_user(const char *oid, const char *reason) {
  char path[96], body[80];
  snprintf(path, sizeof path, OMEGA_API "/users/%s/report", oid);
  snprintf(body, sizeof body, "{\"reason\":\"%s\"}", reason);
  net_req(HTTP_POST, path, body, on_reported, NULL);
}

static void on_invited(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; (void)ud;
  if (st >= 200 && st < 300) set_msg(_("Invito inviato"), 0);
  else set_msg(!strcmp(jstr(j, "error", ""), "blocked") ? _("Non puoi invitare questo utente") : st == 429 ? _("Troppi inviti: riprova più tardi") : _("Invito non inviato"), 1);
}
void social_invite_game(const char *oid, const char *game_id, const char *game_name) {
  char e1[80], e2[220], e3[220], body[600];
  json_escape(e1, sizeof e1, oid); json_escape(e2, sizeof e2, game_id); json_escape(e3, sizeof e3, game_name);
  snprintf(body, sizeof body, "{\"to\":[\"%s\"],\"game_id\":\"%s\",\"game_name\":\"%s\"}", e1, e2, e3);
  net_req(HTTP_POST, OMEGA_API "/invites", body, on_invited, NULL);
}

static char inv_gid[24], inv_gname[100], inv_names[MAX_FRIENDS][32]; static int ninv_names;
static void invite_pick(int idx, void *ud) { (void)ud; if (idx >= 0 && idx < ninv_names) social_invite_game(inv_names[idx], inv_gid, inv_gname); }
void invite_to_game_menu(const char *game_id, const char *game_name) {
  static char labels[24][128]; static const char *items[24];
  snprintf(inv_gid, sizeof inv_gid, "%s", game_id); snprintf(inv_gname, sizeof inv_gname, "%s", game_name);
  ninv_names = 0;
  // gli amici online per primi
  for (int pass = 0; pass < 2; pass++)
    for (int i = 0; i < S.nfriends && ninv_names < 24; i++) {
      int on = friend_online(&S.friends[i]);
      if (on != !pass) continue;
      snprintf(inv_names[ninv_names], sizeof inv_names[0], "%s", S.friends[i].oid);
      snprintf(labels[ninv_names], sizeof labels[0], on ? _("%s  \xC2\xB7 online") : "%s", S.friends[i].oid);
      items[ninv_names] = labels[ninv_names]; ninv_names++;
    }
  if (!ninv_names) { set_msg(_("Aggiungi un amico per invitarlo"), 0); return; }
  static char title[240]; snprintf(title, sizeof title, _("Invita a giocare a %s"), game_name);
  menu_open(title, items, ninv_names, invite_pick, NULL);
}
