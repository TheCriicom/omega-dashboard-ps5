// Omega UI — Community: pannello a schermo intero con quattro schede (L1/R1).
//   Bacheca   post tuoi e degli amici: mi piace (✕), commenti (△), opzioni (□)
//   Gruppi    chat di gruppo persistenti
//   Persone   suggerimenti (amici in comune, stessi giochi)
//   Tempo     i tuoi giochi più giocati e la classifica degli amici
//   Record    tempo di gioco di tutti gli iscritti (records.c)
//   Trofei    classifica pubblica dei trofei (records.c)
#include "app.h"
#include <math.h>
#include <stdlib.h>

enum { TAB_FEED, TAB_GROUPS, TAB_PEOPLE, TAB_STATS, TAB_RECORDS, TAB_TROPHIES, NTABS };
#define IS_PUBLIC(t) ((t) == TAB_RECORDS || (t) == TAB_TROPHIES)
enum { SUB_NONE, SUB_POST, SUB_CHAT, SUB_PICK };

typedef struct { char oid[32]; int avatar; } URef;
typedef struct { char id[16]; URef au; char text[520]; char game[100]; char when[32]; int likes, liked, comments, mine; } Post;
typedef struct { char id[16]; URef au; char text[320]; char when[32]; int mine; } PComment;
typedef struct { char id[16]; char name[48]; char owner[32]; int count, nmem; URef mem[6]; char last_from[32]; char last[200]; char when[32]; int unread; } Group;
typedef struct { char id[16]; URef from; char text[520]; char when[32]; int mine, system; } GMsg;
typedef struct { URef u; int mutual; char reason[24]; int requested; } Sugg;
typedef struct { URef u; long secs; int rank, me; } Rank;
typedef struct { char gid[24]; char game[100]; long secs, week; int sessions; } GStat;

#define MAXP 60
#define MAXC 80
#define MAXG 40
#define MAXM 120
#define FEED_PAGE 20
#define GROUP_POLL_MS 3000

static int tab, sub, sel, loading;
static float sel_anim, scroll, scroll_t, tab_anim;

static Post posts[MAXP]; static int nposts; static char next_before[16]; static int feed_end;
static Post cur;  static PComment coms[MAXC]; static int ncoms;
static Group groups[MAXG]; static int ngroups;
static char chat_id[16], chat_name[48], chat_owner[32]; static GMsg gm[MAXM]; static int ngm; static Uint32 chat_poll_at;
static Sugg sugg[20]; static int nsugg;
static Rank ranks[40]; static int nranks; static int period_week = 1;
static GStat gst[30]; static int ngst; static long tot_secs, week_secs;
// selezione multipla di amici (nuovo gruppo o aggiunta di membri)
static char pick_name[48]; static int pick_mode; static int picked[MAX_FRIENDS];

// ------------------------------------------------------------------ utilità --
static void parse_user(JVal *u, URef *r) {
  r->oid[0] = 0; r->avatar = 0;
  if (!u) return;
  jcpy(r->oid, sizeof r->oid, u, "online_id");
  r->avatar = (int)jnum(u, "avatar", 0);
  media_note_json(r->oid, u);
}

void cm_hours(long secs, char *out, size_t n) {
  if (secs < 3600) snprintf(out, n, _("%ld min"), secs / 60);
  else if (secs < 36000) snprintf(out, n, _("%.1f h"), secs / 3600.0);
  else snprintf(out, n, _("%ld h"), secs / 3600);
}

static int list_count(void);

static void reset_sel(void) { sel = 0; sel_anim = 0; scroll = scroll_t = 0; }

void cm_card(int x, int y, int w, int h, int foc, int a) {
  if (foc) shadow_rrect(x, y, w, h, 22, 22, a * 60 / 100);
  fill_rrect(x, y, w, h, 22, foc ? mix(C_PANEL, C_WHITE, 0.10f) : C_PANEL, a * 94 / 100);
  if (foc) stroke_rrect(x - 4, y - 4, w + 8, h + 8, 26, 3, C_WHITE, a);
}

void cm_chip(int x, int y, const char *label, Col c, int a) {
  TTF_Font *f = font(W_MED, 20);
  int w = text_w(f, label) + 28;
  fill_rrect(x, y, w, 34, 17, c, a * 30 / 100);
  draw_text(f, label, x + 14, y + 17 - TTF_FontHeight(f) / 2, mix(c, C_WHITE, 0.6f), a, AL_L);
}

// ------------------------------------------------------------------ bacheca --
static void parse_post(JVal *o, Post *p) {
  memset(p, 0, sizeof *p);
  jcpy(p->id, sizeof p->id, o, "post_id");
  parse_user(jget(o, "author"), &p->au);
  jcpy(p->text, sizeof p->text, o, "text");
  jcpy(p->game, sizeof p->game, o, "game_name");
  jcpy(p->when, sizeof p->when, o, "created_at");
  p->likes = (int)jnum(o, "likes", 0); p->liked = jbool(o, "liked");
  p->comments = (int)jnum(o, "comments", 0); p->mine = jbool(o, "mine");
}

static void on_feed(int st, JVal *j, const char *raw, void *ud) {
  (void)raw;
  int more = ud != NULL;
  loading = 0;
  if (st != 200) { if (!more) set_msg(_("Bacheca non disponibile"), 1); return; }
  if (!more) nposts = 0;
  int got = 0;
  JFOR(o, jget(j, "posts")) { if (nposts >= MAXP) break; parse_post(o, &posts[nposts++]); got++; }
  jcpy(next_before, sizeof next_before, j, "next_before");
  feed_end = !got || !next_before[0] || nposts >= MAXP;
}
static void load_feed(int more) {
  char path[96];
  if (more) { if (feed_end || loading) return; snprintf(path, sizeof path, OMEGA_API "/feed?limit=%d&before=%s", FEED_PAGE, next_before); }
  else { snprintf(path, sizeof path, OMEGA_API "/feed?limit=%d", FEED_PAGE); feed_end = 0; }
  loading = 1;
  net_req(HTTP_GET, path, NULL, on_feed, more ? (void *)1 : NULL);
}

static void on_posted(int st, JVal *j, const char *raw, void *ud) {
  (void)j; (void)raw; (void)ud;
  if (st == 201 || st == 200) { set_msg(_("Pubblicato sulla bacheca"), 0); load_feed(0); reset_sel(); }
  else if (st == 429) set_msg(_("Hai scritto molto: riprova più tardi"), 1);
  else set_msg(_("Pubblicazione non riuscita"), 1);
}
static void write_post(void) {
  char t[520] = "";
  if (!edit_text(_("Cosa vuoi condividere con i tuoi amici?"), t, sizeof t, 0) || !t[0]) return;
  char e[1100], body[1200];
  json_escape(e, sizeof e, t);
  snprintf(body, sizeof body, "{\"text\":\"%s\"}", e);
  net_req(HTTP_POST, OMEGA_API "/posts", body, on_posted, NULL);
}

static void on_liked(int st, JVal *j, const char *raw, void *ud) {
  (void)raw;
  Post *p = ud;
  if (st != 200 || !p) return;
  p->likes = (int)jnum(j, "likes", p->likes); p->liked = jbool(j, "liked");
}
static void toggle_like(Post *p) {
  char path[64], body[24];
  snprintf(path, sizeof path, OMEGA_API "/posts/%s/like", p->id);
  snprintf(body, sizeof body, "{\"like\":%s}", p->liked ? "false" : "true");
  p->liked = !p->liked; p->likes += p->liked ? 1 : -1;     // subito a schermo, poi il server conferma
  net_req(HTTP_POST, path, body, on_liked, p);
}

static void on_comments(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; (void)ud;
  if (st != 200) return;
  ncoms = 0;
  JFOR(o, jget(j, "comments")) {
    if (ncoms >= MAXC) break;
    PComment *c = &coms[ncoms++]; memset(c, 0, sizeof *c);
    jcpy(c->id, sizeof c->id, o, "comment_id");
    parse_user(jget(o, "author"), &c->au);
    jcpy(c->text, sizeof c->text, o, "text");
    jcpy(c->when, sizeof c->when, o, "created_at");
    c->mine = jbool(o, "mine");
  }
  cur.comments = ncoms;
}
static void load_comments(void) {
  char path[64]; snprintf(path, sizeof path, OMEGA_API "/posts/%s/comments", cur.id);
  net_req(HTTP_GET, path, NULL, on_comments, NULL);
}
static void open_post(const Post *p) { cur = *p; ncoms = 0; sub = SUB_POST; reset_sel(); load_comments(); }

static void on_commented(int st, JVal *j, const char *raw, void *ud) {
  (void)j; (void)raw; (void)ud;
  if (st == 201 || st == 200) load_comments();
  else set_msg(st == 403 ? _("Non puoi commentare questo post") : _("Commento non inviato"), 1);
}
static void write_comment(void) {
  char t[320] = "";
  if (!edit_text(_("Scrivi un commento"), t, sizeof t, 0) || !t[0]) return;
  char e[700], body[760], path[64];
  json_escape(e, sizeof e, t);
  snprintf(body, sizeof body, "{\"text\":\"%s\"}", e);
  snprintf(path, sizeof path, OMEGA_API "/posts/%s/comments", cur.id);
  net_req(HTTP_POST, path, body, on_commented, NULL);
}

// segnalazioni ed eliminazioni (post e commenti)
static char act_post[16], act_comment[16];
static void on_done_reload(int st, JVal *j, const char *raw, void *ud) {
  (void)j; (void)raw;
  if (st >= 200 && st < 300) { set_msg(ud ? (const char *)ud : _("Fatto"), 0); if (sub == SUB_POST) load_comments(); else load_feed(0); }
  else set_msg(_("Operazione non riuscita"), 1);
}
static const char *REASONS[] = { "spam", "contenuto_offensivo", "molestie", "altro" };
static void report_pick(int idx, void *ud) {
  (void)ud; if (idx < 0 || idx > 3) return;
  char path[96], body[64];
  if (act_comment[0]) snprintf(path, sizeof path, OMEGA_API "/posts/%s/comments/%s/report", act_post, act_comment);
  else snprintf(path, sizeof path, OMEGA_API "/posts/%s/report", act_post);
  snprintf(body, sizeof body, "{\"reason\":\"%s\"}", REASONS[idx]);
  net_req(HTTP_POST, path, body, on_done_reload, (void *)_("Grazie, la segnalazione è stata inviata"));
}
static void ask_report(void) {
  const char *items[] = { _("Spam"), _("Contenuto offensivo"), _("Molestie"), _("Altro") };
  menu_open(_("Segnala"), items, 4, report_pick, NULL);
}
static void delete_yes(int idx, void *ud) {
  (void)idx; (void)ud;
  char path[96];
  if (act_comment[0]) snprintf(path, sizeof path, OMEGA_API "/posts/%s/comments/%s", act_post, act_comment);
  else { snprintf(path, sizeof path, OMEGA_API "/posts/%s", act_post); if (sub == SUB_POST) sub = SUB_NONE; }
  net_req(HTTP_DELETE, path, NULL, on_done_reload, (void *)_("Eliminato"));
}
static char opt_oid[32]; static int opt_mine;
static void post_opt_pick(int idx, void *ud) {
  (void)ud;
  if (idx == 0) profile_open(opt_oid);
  else if (idx == 1) { if (opt_mine) confirm_open(_("Eliminare definitivamente?"), _("Elimina"), delete_yes, NULL); else ask_report(); }
}
static void post_options(const char *post_id, const char *comment_id, const char *author, int can_delete) {
  snprintf(act_post, sizeof act_post, "%s", post_id);
  snprintf(act_comment, sizeof act_comment, "%s", comment_id ? comment_id : "");
  snprintf(opt_oid, sizeof opt_oid, "%s", author);
  opt_mine = can_delete;
  const char *mine[] = { _("Visualizza profilo"), _("Elimina") };
  const char *other[] = { _("Visualizza profilo"), _("Segnala") };
  menu_open(comment_id ? _("Commento") : _("Post"), can_delete ? mine : other, 2, post_opt_pick, NULL);
}

// ------------------------------------------------------------------- gruppi --
static void on_groups(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; (void)ud;
  if (st != 200) return;
  ngroups = 0;
  JFOR(o, jget(j, "groups")) {
    if (ngroups >= MAXG) break;
    Group *g = &groups[ngroups++]; memset(g, 0, sizeof *g);
    jcpy(g->id, sizeof g->id, o, "group_id");
    jcpy(g->name, sizeof g->name, o, "name");
    jcpy(g->owner, sizeof g->owner, o, "owner");
    g->count = (int)jnum(o, "member_count", 0);
    g->unread = (int)jnum(o, "unread", 0);
    JFOR(m, jget(o, "members")) { if (g->nmem >= 6) break; parse_user(m, &g->mem[g->nmem++]); }
    JVal *l = jget(o, "last_message");
    if (l && l->t == J_OBJ) {
      JVal *f = jget(l, "from");
      if (f && f->t == J_OBJ) jcpy(g->last_from, sizeof g->last_from, f, "online_id"); else jcpy(g->last_from, sizeof g->last_from, l, "from");
      jcpy(g->last, sizeof g->last, l, "text");
      jcpy(g->when, sizeof g->when, l, "created_at");
    }
  }
}
static void load_groups(void) { net_req(HTTP_GET, OMEGA_API "/groups", NULL, on_groups, NULL); }

static void on_gmsgs(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; (void)ud;
  if (st != 200 || sub != SUB_CHAT) return;
  JFOR(o, jget(j, "messages")) {
    char id[16]; jcpy(id, sizeof id, o, "message_id");
    int dup = 0; for (int i = 0; i < ngm; i++) if (!strcmp(gm[i].id, id)) dup = 1;
    if (dup) continue;
    if (ngm >= MAXM) { memmove(gm, gm + 1, sizeof gm[0] * (MAXM - 1)); ngm--; }
    GMsg *m = &gm[ngm++]; memset(m, 0, sizeof *m);
    snprintf(m->id, sizeof m->id, "%s", id);
    parse_user(jget(o, "from"), &m->from);
    jcpy(m->text, sizeof m->text, o, "text");
    jcpy(m->when, sizeof m->when, o, "created_at");
    m->mine = jbool(o, "mine"); m->system = jbool(o, "system");
  }
}
static void poll_chat(void) {
  char path[96];
  snprintf(path, sizeof path, OMEGA_API "/groups/%s/messages?after=%s&limit=50", chat_id, ngm ? gm[ngm - 1].id : "0");
  net_req(HTTP_GET, path, NULL, on_gmsgs, NULL);
  chat_poll_at = SDL_GetTicks();
}
static void open_chat(const Group *g) {
  snprintf(chat_id, sizeof chat_id, "%s", g->id);
  snprintf(chat_name, sizeof chat_name, "%s", g->name);
  snprintf(chat_owner, sizeof chat_owner, "%s", g->owner);
  ngm = 0; sub = SUB_CHAT; reset_sel();
  poll_chat();
}
static void on_gsent(int st, JVal *j, const char *raw, void *ud) {
  (void)j; (void)raw; (void)ud;
  if (st == 201 || st == 200) poll_chat();
  else set_msg(st == 429 ? _("Stai scrivendo troppo in fretta") : _("Messaggio non inviato"), 1);
}
static void write_gmsg(void) {
  char t[520] = "";
  if (!edit_text(chat_name, t, sizeof t, 0) || !t[0]) return;
  char e[1100], body[1200], path[64];
  json_escape(e, sizeof e, t);
  snprintf(body, sizeof body, "{\"text\":\"%s\"}", e);
  snprintf(path, sizeof path, OMEGA_API "/groups/%s/messages", chat_id);
  net_req(HTTP_POST, path, body, on_gsent, NULL);
}

static void on_group_changed(int st, JVal *j, const char *raw, void *ud) {
  (void)raw;
  if (st >= 200 && st < 300) {
    set_msg(ud ? (const char *)ud : _("Fatto"), 0);
    load_groups();
    if (sub == SUB_CHAT) poll_chat();
    const char *gid = jstr(jget(j, "group"), "group_id", "");
    if (gid[0] && sub == SUB_NONE) { Group g; memset(&g, 0, sizeof g); snprintf(g.id, sizeof g.id, "%s", gid); jcpy(g.name, sizeof g.name, jget(j, "group"), "name"); snprintf(g.owner, sizeof g.owner, "%s", S.me); open_chat(&g); }
  } else {
    const char *e = jstr(j, "error", "");
    set_msg(!strcmp(e, "blocked") ? _("Uno degli utenti non è disponibile") : !strcmp(e, "not_friends") ? _("Puoi aggiungere solo i tuoi amici") : _("Operazione non riuscita"), 1);
  }
}
static void leave_yes(int idx, void *ud) {
  (void)idx; (void)ud;
  char path[64]; snprintf(path, sizeof path, OMEGA_API "/groups/%s/leave", chat_id);
  sub = SUB_NONE; reset_sel();
  net_req(HTTP_POST, path, "{}", on_group_changed, (void *)_("Sei uscito dal gruppo"));
}
static void start_pick(int mode) {
  memset(picked, 0, sizeof picked);
  pick_mode = mode; sub = SUB_PICK; reset_sel();
}
static void chat_opt_pick(int idx, void *ud) {
  (void)ud;
  int owner = !strcasecmp(chat_owner, S.me);
  if (idx == 0) start_pick(1);
  else if (idx == 1 && owner) {
    char n[48]; snprintf(n, sizeof n, "%s", chat_name);
    if (!edit_text(_("Nome del gruppo"), n, sizeof n, 0) || !n[0]) return;
    char e[120], body[160], path[64];
    json_escape(e, sizeof e, n); snprintf(body, sizeof body, "{\"name\":\"%s\"}", e);
    snprintf(path, sizeof path, OMEGA_API "/groups/%s", chat_id);
    snprintf(chat_name, sizeof chat_name, "%s", n);
    net_req(HTTP_POST, path, body, on_group_changed, (void *)_("Gruppo rinominato"));
  } else confirm_open(_("Uscire dal gruppo? Non riceverai più i suoi messaggi."), P_("gruppo", "Esci"), leave_yes, NULL);
}
static void chat_options(void) {
  const char *own[] = { _("Aggiungi amici"), _("Rinomina"), _("Esci dal gruppo") };
  const char *mem[] = { _("Aggiungi amici"), _("Esci dal gruppo") };
  if (!strcasecmp(chat_owner, S.me)) menu_open(chat_name, own, 3, chat_opt_pick, NULL);
  else menu_open(chat_name, mem, 2, chat_opt_pick, NULL);
}
static void finish_pick(void) {
  char list[MAX_FRIENDS * 40] = ""; size_t o = 0; int n = 0;
  for (int i = 0; i < S.nfriends; i++) if (picked[i]) { o += (size_t)snprintf(list + o, sizeof list - o, "%s\"%s\"", n ? "," : "", S.friends[i].oid); n++; }
  if (!n) { set_msg(_("Scegli almeno un amico"), 1); return; }
  static char body[MAX_FRIENDS * 40 + 160]; char path[64];
  if (pick_mode == 0) {
    char e[120]; json_escape(e, sizeof e, pick_name);
    snprintf(body, sizeof body, "{\"name\":\"%s\",\"members\":[%s]}", e, list);
    sub = SUB_NONE; reset_sel();
    net_req(HTTP_POST, OMEGA_API "/groups", body, on_group_changed, (void *)_("Gruppo creato"));
  } else {
    snprintf(body, sizeof body, "{\"add\":[%s]}", list);
    snprintf(path, sizeof path, OMEGA_API "/groups/%s/members", chat_id);
    sub = SUB_CHAT; reset_sel();
    net_req(HTTP_POST, path, body, on_group_changed, (void *)(n == 1 ? _("Amico aggiunto") : _("Amici aggiunti")));
  }
}
static void new_group(void) {
  if (!S.nfriends) { set_msg(_("Aggiungi qualche amico per creare un gruppo"), 0); return; }
  snprintf(pick_name, sizeof pick_name, _("Gruppo di %s"), S.me);
  if (!edit_text(_("Nome del nuovo gruppo"), pick_name, sizeof pick_name, 0) || !pick_name[0]) return;
  start_pick(0);
}

// ------------------------------------------------------------------ persone --
static void on_sugg(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; (void)ud;
  if (st != 200) return;
  nsugg = 0;
  JFOR(o, jget(j, "users")) {
    if (nsugg >= 20) break;
    Sugg *s = &sugg[nsugg++]; memset(s, 0, sizeof *s);
    parse_user(o, &s->u);
    s->mutual = (int)jnum(o, "mutual", 0);
    jcpy(s->reason, sizeof s->reason, o, "reason");
  }
}

// -------------------------------------------------------------------- tempo --
static void on_stats_me(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; (void)ud;
  if (st != 200) return;
  tot_secs = (long)jnum(j, "total_seconds", 0); week_secs = (long)jnum(j, "week_seconds", 0);
  ngst = 0;
  JFOR(o, jget(j, "games")) {
    if (ngst >= 30) break;
    GStat *g = &gst[ngst++]; memset(g, 0, sizeof *g);
    jcpy(g->gid, sizeof g->gid, o, "game_id"); jcpy(g->game, sizeof g->game, o, "game_name");
    if (!g->game[0]) snprintf(g->game, sizeof g->game, "%s", g->gid);
    g->secs = (long)jnum(o, "seconds", 0); g->week = (long)jnum(o, "week_seconds", 0); g->sessions = (int)jnum(o, "sessions", 0);
  }
}
static void on_ranking(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; (void)ud;
  if (st != 200) return;
  nranks = 0;
  JFOR(o, jget(j, "ranking")) {
    if (nranks >= 40) break;
    Rank *r = &ranks[nranks++]; memset(r, 0, sizeof *r);
    parse_user(jget(o, "user"), &r->u);
    r->secs = (long)jnum(o, "seconds", 0); r->rank = (int)jnum(o, "rank", nranks); r->me = jbool(o, "me");
  }
}
static void load_stats(void) {
  net_req(HTTP_GET, OMEGA_API "/stats/me", NULL, on_stats_me, NULL);
  net_req(HTTP_GET, period_week ? OMEGA_API "/stats/friends?period=week" : OMEGA_API "/stats/friends?period=all", NULL, on_ranking, NULL);
}

static void load_tab(void) {
  switch (tab) {
    case TAB_FEED: load_feed(0); break;
    case TAB_GROUPS: load_groups(); break;
    case TAB_PEOPLE: net_req(HTTP_GET, OMEGA_API "/friends/suggestions", NULL, on_sugg, NULL); break;
    case TAB_STATS: load_stats(); break;
    case TAB_RECORDS: case TAB_TROPHIES: records_enter(tab == TAB_TROPHIES); records_load(tab == TAB_TROPHIES); break;
  }
}

void community_open(int t) {
  tab = t < 0 || t >= NTABS ? 0 : t; sub = SUB_NONE; reset_sel(); tab_anim = (float)tab;
  load_tab();
  ov_push(OV_COMMUNITY);
}

// apre direttamente un post (dalle notifiche di "mi piace" e commenti)
static void on_one_post(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; (void)ud;
  if (st != 200) { set_msg(st == 404 ? _("Il post non c'è più") : _("Post non disponibile"), 1); return; }
  Post p; parse_post(jget(j, "post"), &p);
  open_post(&p);
}
void community_open_post(const char *post_id) {
  community_open(TAB_FEED);
  char path[96]; snprintf(path, sizeof path, OMEGA_API "/posts/%s", post_id);
  net_req(HTTP_GET, path, NULL, on_one_post, NULL);
}

// ------------------------------------------------------------------ disegno --
#define CX CM_X
#define CW CM_W
#define TOP CM_TOP

static int post_h(const Post *p) {
  int tw = text_w(font(W_REG, 28), p->text), lines = tw / (CW - 200) + 1;
  if (lines > 6) lines = 6;
  return 196 + lines * 38 + (p->game[0] ? 44 : 0);
}

static void draw_post(const Post *p, int x, int y, int w, int foc, int a, int full) {
  int h = full ? 0 : post_h(p);
  if (!full) cm_card(x, y, w, h, foc, a);
  draw_avatar(p->au.oid, p->au.avatar, x + 66, y + 66, 76, a);
  draw_text(font(W_MED, 29), p->au.oid, x + 124, y + 30, C_TXT, a, AL_L);
  char tm[64]; rel_time(p->when, tm, sizeof tm);
  draw_text(font(W_REG, 22), tm, x + 124, y + 68, C_FAINT, a, AL_L);
  int ty = y + 120;
  if (p->game[0]) { cm_chip(x + 40, ty - 6, p->game, C_ACC2, a); ty += 44; }
  int lines = draw_text_wrap(font(W_REG, 28), p->text, x + 40, ty, w - 80, full ? 14 : 6, 38, C_TXT, a);
  ty += lines * 38 + 18;
  Col lc = p->liked ? RGB(255, 92, 120) : C_DIM;
  draw_icon(IC_LIKE, x + 54, ty + 14, 26, lc, a);
  char n[32]; snprintf(n, sizeof n, "%d", p->likes);
  int nx = x + 80 + draw_text(font(W_MED, 24), n, x + 76, ty, lc, a, AL_L) + 34;
  draw_icon(IC_CHAT, nx, ty + 14, 26, C_DIM, a);
  snprintf(n, sizeof n, "%d", p->comments);
  draw_text(font(W_MED, 24), n, nx + 24, ty, C_DIM, a, AL_L);
}

static void draw_header(int a) {
  const char *names[NTABS] = { _("Bacheca"), _("Gruppi"), _("Persone"), _("Tempo di gioco"), _("Record"), _("Trofei") };
  draw_icon(IC_FRIENDS, CX + 24, 84, 46, C_ACC2, a);
  draw_text(font(W_LIGHT, 48), _("Community"), CX + 66, 54, C_WHITE, a, AL_L);
  tab_anim = approach(tab_anim, (float)tab, 14.0f);
  // sei schede: nelle lingue dai nomi lunghi il carattere si stringe finché stanno nella riga
  int x = CX + 400, size = 28, pad = 22;
  for (; size > 20; size -= 2, pad = size > 24 ? 22 : 16) {
    int tot = 0; for (int i = 0; i < NTABS; i++) tot += text_w(font(W_MED, size), names[i]) + 2 * pad + 14;
    if (x + tot <= SCREEN_W - CX) break;
  }
  for (int i = 0; i < NTABS; i++) {
    TTF_Font *f = font(tab == i ? W_MED : W_REG, size);
    int w = text_w(f, names[i]) + 2 * pad;
    if (tab == i) fill_rrect(x, 56, w, 58, 29, C_WHITE, a * (sub ? 10 : 18) / 100);
    draw_text(f, names[i], x + pad, 85 - TTF_FontHeight(f) / 2, tab == i ? C_WHITE : C_DIM, a, AL_L);
    if (i == TAB_GROUPS && S.unread_groups) draw_badge(x + w - 6, 58, S.unread_groups, a);
    x += w + 14;
  }
}

static void follow(int y_sel, int h_sel, int view_h) {
  float t = scroll_t;
  if (y_sel - t < 0) t = (float)y_sel - 30;
  if (y_sel + h_sel - t > view_h) t = (float)(y_sel + h_sel - view_h + 30);
  if (t < 0) t = 0;
  scroll_t = t;
  scroll = approach(scroll, scroll_t, 14.0f);
}

static void draw_feed(int a) {
  int view = SCREEN_H - TOP - 100, y = 0, ysel = 0, hsel = 120;
  // la prima riga è il campo per scrivere un post
  int heights[MAXP + 1]; heights[0] = 120;
  for (int i = 0; i < nposts; i++) heights[i + 1] = post_h(&posts[i]) + 24;
  for (int i = 0; i < sel && i <= nposts; i++) ysel += heights[i];
  hsel = heights[sel <= nposts ? sel : 0];
  follow(ysel, hsel, view);
  SDL_Rect clip = { 0, TOP - 10, SCREEN_W, view + 20 };
  SDL_RenderSetClipRect(R, &clip);
  y = TOP - (int)scroll;
  cm_card(CX, y, CW, 96, sel == 0, a);
  draw_avatar(S.me, S.my_avatar, CX + 56, y + 48, 58, a);
  draw_text(font(W_REG, 28), _("Scrivi qualcosa ai tuoi amici..."), CX + 104, y + 30, C_DIM, a, AL_L);
  draw_icon(IC_SEND, CX + CW - 50, y + 48, 30, sel == 0 ? C_WHITE : C_FAINT, a);
  y += heights[0];
  for (int i = 0; i < nposts; i++) {
    if (y + heights[i + 1] > TOP - 40 && y < TOP + view + 40) draw_post(&posts[i], CX, y, CW, sel == i + 1, a, 0);
    y += heights[i + 1];
  }
  if (!nposts && !loading) {
    draw_icon(IC_NEWS, SCREEN_W / 2, y + 120, 80, C_FAINT, a);
    draw_text(font(W_MED, 32), _("La bacheca è vuota"), SCREEN_W / 2, y + 190, C_DIM, a, AL_C);
    draw_text(font(W_REG, 25), _("Scrivi il primo post o aggiungi qualche amico."), SCREEN_W / 2, y + 240, C_FAINT, a, AL_C);
  }
  if (loading) draw_spinner(SCREEN_W / 2, y + 60, 18, a);
  SDL_RenderSetClipRect(R, NULL);
  if (sel >= nposts - 2 && nposts) load_feed(1);
}

static void draw_post_detail(int a) {
  int view = SCREEN_H - TOP - 100;
  int ph = post_h(&cur) + 60, y0 = ph + 30;
  int ysel = sel == 0 ? 0 : y0 + (sel - 1) * 132;
  follow(ysel, sel == 0 ? ph : 120, view);
  SDL_Rect clip = { 0, TOP - 10, SCREEN_W, view + 20 };
  SDL_RenderSetClipRect(R, &clip);
  int y = TOP - (int)scroll;
  cm_card(CX, y, CW, ph, sel == 0, a);
  draw_post(&cur, CX, y, CW, 0, a, 1);
  y += y0;
  draw_text(font(W_MED, 26), ncoms ? _("Commenti") : _("Ancora nessun commento: premi Triangolo e scrivi il primo"), CX + 10, y - 2, C_DIM, a, AL_L);
  y += 50;
  for (int i = 0; i < ncoms; i++, y += 132) {
    if (y + 120 < TOP - 20 || y > TOP + view) continue;
    PComment *c = &coms[i];
    cm_card(CX + 60, y, CW - 60, 120, sel == i + 1, a);
    draw_avatar(c->au.oid, c->au.avatar, CX + 116, y + 60, 60, a);
    char tm[64]; rel_time(c->when, tm, sizeof tm);
    int nw = draw_text(font(W_MED, 24), c->au.oid, CX + 164, y + 18, C_TXT, a, AL_L);
    draw_text(font(W_REG, 20), tm, CX + 180 + nw, y + 22, C_FAINT, a, AL_L);
    draw_text_wrap(font(W_REG, 25), c->text, CX + 164, y + 54, CW - 260, 2, 30, C_TXT, a);
  }
  SDL_RenderSetClipRect(R, NULL);
}

static void draw_groups(int a) {
  int view = SCREEN_H - TOP - 100, rh = 136;
  follow(sel * rh, rh, view);
  SDL_Rect clip = { 0, TOP - 10, SCREEN_W, view + 20 };
  SDL_RenderSetClipRect(R, &clip);
  int y = TOP - (int)scroll;
  cm_card(CX, y, CW, rh - 20, sel == 0, a);
  fill_circle(CX + 64, y + 58, 38, sel == 0 ? C_WHITE : C_ACC, a);
  draw_icon(IC_PLUS, CX + 64, y + 58, 34, sel == 0 ? RGB(12, 14, 22) : C_WHITE, a);
  draw_text(font(W_MED, 30), _("Nuovo gruppo"), CX + 130, y + 26, C_TXT, a, AL_L);
  draw_text(font(W_REG, 23), _("Una chat che resta, con gli amici che scegli tu"), CX + 130, y + 66, C_DIM, a, AL_L);
  y += rh;
  for (int i = 0; i < ngroups; i++, y += rh) {
    if (y + rh < TOP - 20 || y > TOP + view) continue;
    Group *g = &groups[i];
    cm_card(CX, y, CW, rh - 20, sel == i + 1, a);
    for (int k = g->nmem - 1; k >= 0 && k < 3; k--) draw_avatar(g->mem[k].oid, g->mem[k].avatar, CX + 54 + k * 26, y + 58, 56, a);
    int tx = CX + 180;
    draw_text_fit(font(W_MED, 30), g->name, tx, y + 22, CW - 420, C_TXT, a, AL_L);
    char ln[260];
    if (g->last[0]) snprintf(ln, sizeof ln, "%s: %s", g->last_from, g->last); else snprintf(ln, sizeof ln, g->count == 1 ? _("%d membro \xC2\xB7 nessun messaggio") : _("%d membri \xC2\xB7 nessun messaggio"), g->count);
    draw_text_fit(font(W_REG, 23), ln, tx, y + 66, CW - 420, g->unread ? C_TXT : C_DIM, a, AL_L);
    if (g->when[0]) { char tm[64]; rel_time(g->when, tm, sizeof tm); draw_text(font(W_REG, 21), tm, CX + CW - 40, y + 26, C_FAINT, a, AL_R); }
    if (g->unread) draw_badge(CX + CW - 56, y + 76, g->unread, a);
  }
  SDL_RenderSetClipRect(R, NULL);
}

static void draw_chat(int a) {
  int top = TOP + 10, bottom = SCREEN_H - 120;
  draw_text_fit(font(W_MED, 34), chat_name, CX, TOP - 50, CW, C_WHITE, a, AL_L);
  fill_rrect(CX, top, CW, bottom - top, 26, C_PANEL, a * 80 / 100);
  SDL_Rect clip = { CX, top + 10, CW, bottom - top - 20 };
  SDL_RenderSetClipRect(R, &clip);
  int y = bottom - 30;
  TTF_Font *f = font(W_REG, 26);
  for (int i = ngm - 1; i >= 0 && y > top; i--) {
    GMsg *m = &gm[i];
    if (m->system) { y -= 44; draw_text(font(W_REG, 22), m->text, CX + CW / 2, y, C_FAINT, a, AL_C); continue; }
    int maxw = CW * 6 / 10, tw = text_w(f, m->text), lines = tw / (maxw - 40) + 1; if (lines > 6) lines = 6;
    int bw = (lines > 1 ? maxw : tw + 44), bh = lines * 34 + 24 + (m->mine ? 0 : 30);
    y -= bh + 14;
    int bx = m->mine ? CX + CW - 40 - bw : CX + 110;
    if (!m->mine) draw_avatar(m->from.oid, m->from.avatar, CX + 60, y + bh - 30, 52, a);
    fill_rrect(bx, y, bw, bh, 22, m->mine ? C_ACC : RGB(44, 52, 72), a);
    int ty = y + 12;
    if (!m->mine) { draw_text(font(W_MED, 21), m->from.oid, bx + 22, ty, C_ACC2, a, AL_L); ty += 30; }
    draw_text_wrap(f, m->text, bx + 22, ty, bw - 44, 6, 34, C_WHITE, a);
  }
  if (!ngm) draw_text(font(W_REG, 26), _("Nessun messaggio: scrivi tu per primo con X"), CX + CW / 2, (top + bottom) / 2, C_DIM, a, AL_C);
  SDL_RenderSetClipRect(R, NULL);
  if (SDL_GetTicks() - chat_poll_at > GROUP_POLL_MS) poll_chat();
}

static void draw_pick(int a) {
  int view = SCREEN_H - TOP - 100, rh = 100;
  draw_text(font(W_MED, 30), pick_mode == 0 ? pick_name : _("Aggiungi amici al gruppo"), CX, TOP - 54, C_WHITE, a, AL_L);
  follow(sel * rh, rh, view);
  SDL_Rect clip = { 0, TOP - 10, SCREEN_W, view + 20 };
  SDL_RenderSetClipRect(R, &clip);
  int y = TOP - (int)scroll;
  for (int i = 0; i < S.nfriends; i++, y += rh) {
    if (y + rh < TOP - 20 || y > TOP + view) continue;
    cm_card(CX, y, CW, rh - 14, sel == i, a);
    draw_avatar(S.friends[i].oid, S.friends[i].avatar, CX + 56, y + 43, 60, a);
    draw_text(font(W_MED, 28), S.friends[i].oid, CX + 110, y + 26, C_TXT, a, AL_L);
    int bx = CX + CW - 80, by = y + 25;
    fill_rrect(bx, by, 38, 38, 10, picked[i] ? C_ACC : RGB(20, 24, 36), a);
    stroke_rrect(bx, by, 38, 38, 10, 2, picked[i] ? C_ACC : C_FAINT, a);
    if (picked[i]) draw_icon(IC_CHECK, bx + 19, by + 19, 26, C_WHITE, a);
  }
  SDL_RenderSetClipRect(R, NULL);
}

static void draw_people(int a) {
  int view = SCREEN_H - TOP - 100, rh = 120;
  follow(sel * rh, rh, view);
  SDL_Rect clip = { 0, TOP - 10, SCREEN_W, view + 20 };
  SDL_RenderSetClipRect(R, &clip);
  int y = TOP - (int)scroll;
  cm_card(CX, y, CW, rh - 20, sel == 0, a);
  fill_circle(CX + 60, y + 50, 34, sel == 0 ? C_WHITE : C_ACC, a);
  draw_icon(IC_SEARCH, CX + 60, y + 50, 32, sel == 0 ? RGB(12, 14, 22) : C_WHITE, a);
  draw_text(font(W_MED, 29), _("Cerca per ID online"), CX + 120, y + 32, C_TXT, a, AL_L);
  y += rh;
  draw_text(font(W_MED, 25), nsugg ? _("Persone che potresti conoscere") : _("Nessun suggerimento per ora: aggiungi qualche amico"), CX + 6, y, C_DIM, a, AL_L);
  y += 50;
  for (int i = 0; i < nsugg; i++, y += rh) {
    if (y + rh < TOP - 20 || y > TOP + view) continue;
    Sugg *s = &sugg[i];
    cm_card(CX, y, CW, rh - 20, sel == i + 1, a);
    draw_avatar(s->u.oid, s->u.avatar, CX + 60, y + 50, 66, a);
    draw_text(font(W_MED, 29), s->u.oid, CX + 120, y + 18, C_TXT, a, AL_L);
    char why[160];
    if (!strcmp(s->reason, "stessi_giochi")) snprintf(why, sizeof why, "%s", _("Gioca ai tuoi stessi giochi"));
    else snprintf(why, sizeof why, s->mutual == 1 ? _("%d amico in comune") : _("%d amici in comune"), s->mutual);
    draw_text(font(W_REG, 23), why, CX + 120, y + 58, C_DIM, a, AL_L);
    pill(CX + CW - 300, y + 22, 56, s->requested ? _("Richiesta inviata") : _("Aggiungi"), s->requested ? IC_CHECK : IC_ADDUSER, sel == i + 1, sel == i + 1 ? 1.0f : 0.0f, a);
  }
  SDL_RenderSetClipRect(R, NULL);
}

static void draw_stats(int a) {
  int lx = CX, lw = CW * 55 / 100 - 30, rx = CX + CW * 55 / 100, rw = CW * 45 / 100;
  char t1[32], t2[32];
  cm_hours(week_secs, t1, sizeof t1); cm_hours(tot_secs, t2, sizeof t2);
  cm_card(lx, TOP, lw, 150, 0, a);
  draw_text(font(W_REG, 23), _("Questa settimana"), lx + 40, TOP + 30, C_DIM, a, AL_L);
  draw_text(font(W_LIGHT, 54), t1, lx + 40, TOP + 62, C_WHITE, a, AL_L);
  draw_text(font(W_REG, 23), _("In totale"), lx + lw / 2 + 20, TOP + 30, C_DIM, a, AL_L);
  draw_text(font(W_LIGHT, 54), t2, lx + lw / 2 + 20, TOP + 62, C_WHITE, a, AL_L);
  int y = TOP + 190;
  draw_text(font(W_MED, 26), ngst ? _("I tuoi giochi") : _("Gioca qualcosa: il tempo comparirà qui"), lx + 6, y, C_DIM, a, AL_L);
  y += 50;
  long maxs = ngst ? gst[0].secs : 1; if (maxs <= 0) maxs = 1;
  for (int i = 0; i < ngst && y < SCREEN_H - 160; i++, y += 78) {
    GStat *g = &gst[i];
    draw_text_fit(font(W_MED, 25), g->game, lx + 6, y, lw - 160, C_TXT, a, AL_L);
    cm_hours(g->secs, t1, sizeof t1);
    draw_text(font(W_REG, 23), t1, lx + lw, y + 2, C_DIM, a, AL_R);
    fill_rrect(lx + 6, y + 40, lw - 6, 12, 6, RGB(255, 255, 255), a * 8 / 100);
    fill_rrect(lx + 6, y + 40, (int)((lw - 6) * (double)g->secs / maxs), 12, 6, C_ACC2, a);
  }
  cm_card(rx, TOP, rw, SCREEN_H - TOP - 120, sel == 0, a);
  draw_text(font(W_MED, 29), _("Classifica degli amici"), rx + 36, TOP + 28, C_TXT, a, AL_L);
  cm_chip(rx + 36, TOP + 76, period_week ? _("Questa settimana") : _("Da sempre"), C_ACC, a);
  draw_text(font(W_REG, 21), _("Quadrato: cambia periodo"), rx + rw - 36, TOP + 84, C_FAINT, a, AL_R);
  y = TOP + 140;
  for (int i = 0; i < nranks && y < SCREEN_H - 200; i++, y += 84) {
    Rank *r = &ranks[i];
    if (r->me) fill_rrect(rx + 20, y - 6, rw - 40, 76, 18, C_ACC, a * 22 / 100);
    Col rc = r->rank == 1 ? RGB(255, 200, 60) : r->rank == 2 ? RGB(200, 210, 225) : r->rank == 3 ? RGB(215, 140, 80) : C_DIM;
    char n[8]; snprintf(n, sizeof n, "%d", r->rank);
    draw_text(font(W_BOLD, 30), n, rx + 66, y + 14, rc, a, AL_C);
    draw_avatar(r->u.oid, r->u.avatar, rx + 140, y + 32, 56, a);
    draw_text_fit(font(W_MED, 26), r->u.oid, rx + 184, y + 16, rw - 380, C_TXT, a, AL_L);
    cm_hours(r->secs, t1, sizeof t1);
    draw_text(font(W_MED, 25), t1, rx + rw - 40, y + 18, C_TXT, a, AL_R);
  }
  if (!nranks) draw_text(font(W_REG, 24), _("Nessun dato ancora"), rx + rw / 2, TOP + 240, C_FAINT, a, AL_C);
}

void community_draw(float t) {
  int a = (int)(255 * t);
  fill_rect(0, 0, SCREEN_W, SCREEN_H, mix(RGB(10, 12, 20), g_theme_base, 0.12f), a);
  grad_v(0, 0, SCREEN_W, 420, mix(C_ACC, C_BLACK, 0.55f), a * 45 / 100, RGB(10, 12, 20), 0);
  sel_anim = approach(sel_anim, (float)sel, 18.0f);
  if (sub != SUB_CHAT && sub != SUB_PICK) draw_header(a);
  switch (sub) {
    case SUB_POST: draw_post_detail(a); break;
    case SUB_CHAT: draw_chat(a); break;
    case SUB_PICK: draw_pick(a); break;
    default:
      if (tab == TAB_FEED) draw_feed(a);
      else if (tab == TAB_GROUPS) draw_groups(a);
      else if (tab == TAB_PEOPLE) draw_people(a);
      else if (tab == TAB_STATS) draw_stats(a);
      else records_draw(tab == TAB_TROPHIES, a);
  }
  int ic[4]; const char *lb[4]; int n = 0;
  if (!sub && IS_PUBLIC(tab)) { hints(ic, lb, records_hints(tab == TAB_TROPHIES, ic, lb), a); return; }
  switch (sub) {
    case SUB_POST: ic[n] = IC_BTN_X; lb[n++] = sel ? _("Opzioni") : (cur.liked ? _("Non mi piace più") : _("Mi piace")); ic[n] = IC_BTN_TRI; lb[n++] = _("Commenta"); ic[n] = IC_BTN_O; lb[n++] = _("Indietro"); break;
    case SUB_CHAT: ic[n] = IC_BTN_X; lb[n++] = _("Scrivi"); ic[n] = IC_BTN_SQ; lb[n++] = _("Opzioni"); ic[n] = IC_BTN_O; lb[n++] = _("Indietro"); break;
    case SUB_PICK: ic[n] = IC_BTN_X; lb[n++] = _("Seleziona"); ic[n] = IC_BTN_TRI; lb[n++] = pick_mode == 0 ? _("Crea il gruppo") : _("Aggiungi"); ic[n] = IC_BTN_O; lb[n++] = _("Annulla"); break;
    default:
      if (tab == TAB_FEED && sel > 0) { ic[n] = IC_BTN_X; lb[n++] = _("Mi piace"); ic[n] = IC_BTN_TRI; lb[n++] = _("Commenti"); ic[n] = IC_BTN_SQ; lb[n++] = _("Opzioni"); }
      else if (tab == TAB_STATS) { ic[n] = IC_BTN_TRI; lb[n++] = _("Il tuo riepilogo"); ic[n] = IC_BTN_SQ; lb[n++] = _("Periodo"); }
      else { ic[n] = IC_BTN_X; lb[n++] = _("Seleziona"); }
      ic[n] = IC_BTN_O; lb[n++] = _("Chiudi");
  }
  hints(ic, lb, n, a);
}

// -------------------------------------------------------------------- input --
static int list_count(void) {
  switch (sub) {
    case SUB_POST: return ncoms + 1;
    case SUB_PICK: return S.nfriends;
    case SUB_CHAT: return 1;
  }
  switch (tab) {
    case TAB_FEED: return nposts + 1;
    case TAB_GROUPS: return ngroups + 1;
    case TAB_PEOPLE: return nsugg + 1;
    default: return 1;
  }
}

static void people_pick(void) {
  if (sel == 0) { search_open(); return; }
  Sugg *s = &sugg[sel - 1];
  if (s->requested) { profile_open(s->u.oid); return; }
  social_friend_request(s->u.oid);
  s->requested = 1;
}

void community_input(int b) {
  if (!sub && IS_PUBLIC(tab) && records_input(tab == TAB_TROPHIES, b)) return;
  int n = list_count();
  if (b == B_O) {
    if (sub == SUB_PICK) { sub = pick_mode ? SUB_CHAT : SUB_NONE; reset_sel(); }
    else if (sub) { sub = SUB_NONE; reset_sel(); load_tab(); }
    else ov_pop();
    return;
  }
  if (!sub && (b == B_L1 || b == B_R1)) {
    tab = (tab + (b == B_R1 ? 1 : NTABS - 1)) % NTABS;
    reset_sel(); load_tab();
    return;
  }
  if (b == B_UP && sel > 0) { sel--; return; }
  if (b == B_DOWN && sel < n - 1) { sel++; return; }
  switch (sub) {
    case SUB_POST:
      if (b == B_TRI) write_comment();
      else if (b == B_X && sel == 0) toggle_like(&cur);
      else if ((b == B_X || b == B_SQ) && sel > 0) { PComment *c = &coms[sel - 1]; post_options(cur.id, c->id, c->au.oid, c->mine || cur.mine); }
      else if (b == B_SQ) post_options(cur.id, NULL, cur.au.oid, cur.mine);
      return;
    case SUB_CHAT:
      if (b == B_X) write_gmsg();
      else if (b == B_SQ) chat_options();
      return;
    case SUB_PICK:
      if (b == B_X && sel < S.nfriends) picked[sel] = !picked[sel];
      else if (b == B_TRI) finish_pick();
      return;
  }
  switch (tab) {
    case TAB_FEED:
      if (sel == 0) { if (b == B_X || b == B_TRI) write_post(); }
      else {
        Post *p = &posts[sel - 1];
        if (b == B_X) toggle_like(p);
        else if (b == B_TRI) open_post(p);
        else if (b == B_SQ) post_options(p->id, NULL, p->au.oid, p->mine);
      }
      break;
    case TAB_GROUPS:
      if (b == B_X) { if (sel == 0) new_group(); else open_chat(&groups[sel - 1]); }
      break;
    case TAB_PEOPLE:
      if (b == B_X) people_pick();
      else if (b == B_TRI && sel > 0) profile_open(sugg[sel - 1].u.oid);
      break;
    case TAB_STATS:
      if (b == B_SQ || b == B_X) { period_week = !period_week; load_stats(); }
      else if (b == B_TRI) wrap_open(0);
      break;
  }
}
