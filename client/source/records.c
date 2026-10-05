// Omega UI — Record e Trofei: le due schede pubbliche della Community e la
// finestra dei trofei di un utente.
//   Record   tutti gli iscritti: giocatori per tempo di gioco, giochi più
//            giocati, maratone (un giocatore su un solo gioco); settimana o sempre
//   Trofei   classifica pubblica per punti dei trofei importati dalla console
//   Finestra i set di un utente e, dentro un set, i singoli trofei
// Chi compare lo decide ciascuno in Impostazioni › Privacy (account.c).
#include "app.h"
#include <math.h>
#include <stdlib.h>

#define ROW_H 96
#define VIEW_TOP (CM_TOP + 150)
#define VIEW_H (SCREEN_H - VIEW_TOP - 100)

enum { L_PLAYERS, L_GAMES, L_MARATHON, NLISTS };

typedef struct { char oid[32]; int avatar; long secs; int rank, me, games; char gid[24], game[100], icon[20]; } RPlayer;
typedef struct { char gid[24], game[100], icon[20]; long secs; int players, now; char top[32]; long top_secs; } RGame;
typedef struct { char oid[32]; int avatar; int p, g, s, b, points, sets, rank, me; } TRank;

static RPlayer players[52], marathons[20], gplayers[52]; static int nplayers, nmarathons, ngplayers;
static RGame games[30]; static int ngames;
static struct { int listed, rank, of, online, playing, loaded; } rme;
static TRank tranks[52]; static int ntranks;
static struct { int listed, importing, rank, of, loaded; } tme;
static struct { int open; RGame g; } gview;             // classifica di un solo gioco
static int list, sel, week, sync_seen;
static float scroll, scroll_t;

static const Col GRADE_COL[4] = { { 176, 222, 255, 255 }, { 255, 200, 60, 255 }, { 200, 210, 225, 255 }, { 215, 140, 80, 255 } };   // platino, oro, argento, bronzo

// ------------------------------------------------------------------ utilità --
static void parse_user(JVal *u, char *oid, size_t n, int *avatar) {
  oid[0] = 0; *avatar = 0;
  if (!u) return;
  jcpy(oid, n, u, "online_id");
  *avatar = (int)jnum(u, "avatar", 0);
  media_note_json(oid, u);
}

static void reset_sel(void) { sel = 0; scroll = scroll_t = 0; }

static void follow(int count) {
  if (sel >= count) sel = count ? count - 1 : 0;
  float t = scroll_t;
  int ys = sel * ROW_H;
  if (ys - t < 0) t = (float)ys;
  if (ys + ROW_H - t > VIEW_H) t = (float)(ys + ROW_H - VIEW_H);
  scroll_t = t < 0 ? 0 : t;
  scroll = approach(scroll, scroll_t, 14.0f);
}

static Col rank_col(int rank) {
  return rank == 1 ? RGB(255, 200, 60) : rank == 2 ? RGB(200, 210, 225) : rank == 3 ? RGB(215, 140, 80) : C_DIM;
}

// Icona di un gioco: quella della console se è installato, altrimenti quella
// arrivata dal server, altrimenti un segnaposto.
static void game_icon(const char *gid, const char *media, int x, int y, int s, int a) {
  for (int k = 0; k < napps; k++) if (apps[k].tex && !strcmp(apps[k].tid, gid)) { draw_tex(apps[k].tex, x, y, s, s, a); return; }
  if (media[0] && draw_media_frames(media, 1, x, y, s, s, 128, 128, 18, 0, a)) return;
  fill_rrect(x, y, s, s, 14, RGB(40, 48, 70), a);
  draw_icon(IC_GAMEPAD, x + s / 2, y + s / 2, s * 55 / 100, C_DIM, a);
}

// Coppa di un grado (0 platino … 3 bronzo) e i quattro conteggi in fila; ritorna la larghezza.
static void cup(int cx, int cy, int r, int grade, int a) {
  fill_circle(cx, cy, r, GRADE_COL[grade & 3], a);
  draw_icon(IC_STAR, cx, cy, r * 13 / 10, RGB(20, 22, 32), a);
}
int trophy_counts(int x, int y, int p, int g, int s, int b, int a) {
  const int v[4] = { p, g, s, b };
  TTF_Font *f = font(W_MED, 23);
  int x0 = x;
  for (int i = 0; i < 4; i++) {
    cup(x + 13, y + 14, 13, i, a);
    char n[12]; snprintf(n, sizeof n, "%d", v[i]);
    x += 34 + draw_text(f, n, x + 32, y, C_TXT, a, AL_L) + 18;
  }
  return x - x0;
}
static int grade_index(const char *g) { return g[0] == 'P' ? 0 : g[0] == 'G' ? 1 : g[0] == 'S' ? 2 : 3; }

// --------------------------------------------------------------- caricamento --
static void parse_players(JVal *arr, RPlayer *out, int *n, int max) {
  *n = 0;
  JFOR(o, arr) {
    if (*n >= max) break;
    RPlayer *p = &out[(*n)++]; memset(p, 0, sizeof *p);
    parse_user(jget(o, "user"), p->oid, sizeof p->oid, &p->avatar);
    p->secs = (long)jnum(o, "seconds", 0); p->rank = (int)jnum(o, "rank", *n); p->me = jbool(o, "me"); p->games = (int)jnum(o, "games", 0);
    jcpy(p->gid, sizeof p->gid, o, "game_id"); jcpy(p->game, sizeof p->game, o, "game_name"); jcpy(p->icon, sizeof p->icon, o, "game_icon");
  }
}
static void parse_game(JVal *o, RGame *g) {
  memset(g, 0, sizeof *g);
  jcpy(g->gid, sizeof g->gid, o, "game_id"); jcpy(g->game, sizeof g->game, o, "game_name"); jcpy(g->icon, sizeof g->icon, o, "game_icon");
  if (!g->game[0]) snprintf(g->game, sizeof g->game, "%s", g->gid);
  g->secs = (long)jnum(o, "seconds", 0); g->players = (int)jnum(o, "players", 0); g->now = (int)jnum(o, "playing_now", 0);
  JVal *t = jget(o, "top");
  if (t) { int av; parse_user(jget(t, "user"), g->top, sizeof g->top, &av); g->top_secs = (long)jnum(t, "seconds", 0); }
}
static void on_records(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; (void)ud;
  if (st != 200) return;
  parse_players(jget(j, "players"), players, &nplayers, 52);
  parse_players(jget(j, "marathons"), marathons, &nmarathons, 20);
  ngames = 0;
  JFOR(o, jget(j, "games")) { if (ngames >= 30) break; parse_game(o, &games[ngames++]); }
  JVal *m = jget(j, "me"), *n = jget(j, "now");
  rme.listed = jbool(m, "listed"); rme.rank = (int)jnum(m, "rank", 0); rme.of = (int)jnum(m, "of", 0);
  rme.online = (int)jnum(n, "online", 0); rme.playing = (int)jnum(n, "playing", 0); rme.loaded = 1;
}
static void on_game_ranking(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; (void)ud;
  if (st != 200 || strcmp(jstr(jget(j, "game"), "game_id", ""), gview.g.gid)) return;
  parse_players(jget(j, "ranking"), gplayers, &ngplayers, 52);
  int now = gview.g.now; parse_game(jget(j, "game"), &gview.g); gview.g.now = now;
}
static void load_game_ranking(void) {
  char path[120]; snprintf(path, sizeof path, OMEGA_API "/records/games/%s?period=%s", gview.g.gid, week ? "week" : "all");
  net_req(HTTP_GET, path, NULL, on_game_ranking, NULL);
}
static void on_tranking(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; (void)ud;
  if (st != 200) return;
  ntranks = 0;
  JFOR(o, jget(j, "ranking")) {
    if (ntranks >= 52) break;
    TRank *t = &tranks[ntranks++]; memset(t, 0, sizeof *t);
    parse_user(jget(o, "user"), t->oid, sizeof t->oid, &t->avatar);
    t->p = (int)jnum(o, "p", 0); t->g = (int)jnum(o, "g", 0); t->s = (int)jnum(o, "s", 0); t->b = (int)jnum(o, "b", 0);
    t->points = (int)jnum(o, "points", 0); t->sets = (int)jnum(o, "sets", 0); t->rank = (int)jnum(o, "rank", ntranks); t->me = jbool(o, "me");
  }
  JVal *m = jget(j, "me");
  tme.listed = jbool(m, "listed"); tme.importing = jbool(m, "importing"); tme.rank = (int)jnum(m, "rank", 0); tme.of = (int)jnum(m, "of", 0); tme.loaded = 1;
}

void records_load(int trophies) {
  if (trophies) { net_req(HTTP_GET, OMEGA_API "/trophies/ranking", NULL, on_tranking, NULL); sync_seen = consync_rev(); }
  else if (gview.open) load_game_ranking();
  else net_req(HTTP_GET, week ? OMEGA_API "/records?period=week" : OMEGA_API "/records?period=all", NULL, on_records, NULL);
}
void records_enter(int trophies) { (void)trophies; gview.open = 0; reset_sel(); }

// ------------------------------------------------------------------ disegno --
static void player_row(const RPlayer *p, int y, int foc, int a, int show_game) {
  cm_card(CM_X, y, CM_W, ROW_H - 16, foc, a);
  if (p->me && !foc) fill_rrect(CM_X, y, CM_W, ROW_H - 16, 22, C_ACC, a * 18 / 100);
  char n[8]; snprintf(n, sizeof n, "%d", p->rank);
  draw_text(font(W_BOLD, 32), n, CM_X + 56, y + 20, rank_col(p->rank), a, AL_C);
  draw_avatar(p->oid, p->avatar, CM_X + 140, y + 40, 60, a);
  char sub[200] = "";
  if (show_game == 2) snprintf(sub, sizeof sub, "%s", p->game);
  else if (show_game && p->game[0]) snprintf(sub, sizeof sub, p->games == 1 ? _("%d gioco \xC2\xB7 pi\xC3\xB9 giocato: %s") : _("%d giochi \xC2\xB7 pi\xC3\xB9 giocato: %s"), p->games, p->game);
  draw_text_fit(font(W_MED, 28), p->oid, CM_X + 190, sub[0] ? y + 8 : y + 22, 520, C_TXT, a, AL_L);
  if (sub[0]) draw_text_fit(font(W_REG, 22), sub, CM_X + 190, y + 44, 900, C_DIM, a, AL_L);
  if (show_game && p->gid[0]) game_icon(p->gid, p->icon, CM_X + CM_W - 330, y + 12, 56, a);
  char h[32]; cm_hours(p->secs, h, sizeof h);
  draw_text(font(W_MED, 30), h, CM_X + CM_W - 40, y + 20, C_TXT, a, AL_R);
}

static void game_row(const RGame *g, int idx, int y, int foc, int a) {
  cm_card(CM_X, y, CM_W, ROW_H - 16, foc, a);
  char n[8]; snprintf(n, sizeof n, "%d", idx + 1);
  draw_text(font(W_BOLD, 32), n, CM_X + 56, y + 20, rank_col(idx + 1), a, AL_C);
  game_icon(g->gid, g->icon, CM_X + 108, y + 8, 64, a);
  draw_text_fit(font(W_MED, 28), g->game, CM_X + 190, y + 8, 760, C_TXT, a, AL_L);
  char sub[220], pl[64], now[64] = "";
  snprintf(pl, sizeof pl, g->players == 1 ? _("%d giocatore") : _("%d giocatori"), g->players);
  if (g->now) snprintf(now, sizeof now, _("%d in gioco ora"), g->now);
  snprintf(sub, sizeof sub, "%s%s%s", pl, now[0] ? "  \xC2\xB7  " : "", now);
  draw_text_fit(font(W_REG, 22), sub, CM_X + 190, y + 44, 520, g->now ? C_OK : C_DIM, a, AL_L);
  if (g->top[0]) {
    char h[32], top[120]; cm_hours(g->top_secs, h, sizeof h);
    snprintf(top, sizeof top, _("Record: %s \xC2\xB7 %s"), g->top, h);
    draw_avatar(g->top, 0, CM_X + CM_W - 620, y + 40, 40, a);
    draw_text_fit(font(W_REG, 22), top, CM_X + CM_W - 590, y + 26, 330, C_DIM, a, AL_L);
  }
  char h[32]; cm_hours(g->secs, h, sizeof h);
  draw_text(font(W_MED, 30), h, CM_X + CM_W - 40, y + 20, C_TXT, a, AL_R);
}

static void empty(const char *title, const char *sub, int a) {
  draw_icon(IC_STAR, SCREEN_W / 2, VIEW_TOP + 130, 80, C_FAINT, a);
  draw_text(font(W_MED, 32), title, SCREEN_W / 2, VIEW_TOP + 200, C_DIM, a, AL_C);
  if (sub) draw_text_wrap_al(font(W_REG, 25), sub, SCREEN_W / 2 - 520, VIEW_TOP + 250, 1040, 3, 36, C_FAINT, a, AL_C);
}

static int records_count(void) {
  if (gview.open) return ngplayers;
  return list == L_PLAYERS ? nplayers : list == L_GAMES ? ngames : nmarathons;
}

static void draw_records(int a) {
  int y0 = CM_TOP;
  if (gview.open) {
    game_icon(gview.g.gid, gview.g.icon, CM_X, y0 - 6, 110, a);
    draw_text_fit(font(W_LIGHT, 46), gview.g.game, CM_X + 136, y0 - 8, CM_W - 560, C_WHITE, a, AL_L);
    char h[32], sub[160], pl[64]; cm_hours(gview.g.secs, h, sizeof h);
    snprintf(pl, sizeof pl, gview.g.players == 1 ? _("%d giocatore") : _("%d giocatori"), gview.g.players);
    snprintf(sub, sizeof sub, _("%s \xC2\xB7 %s giocate in totale"), pl, h);
    draw_text(font(W_REG, 25), sub, CM_X + 138, y0 + 58, C_DIM, a, AL_L);
  } else {
    const char *names[NLISTS] = { _("Giocatori"), _("Giochi pi\xC3\xB9 giocati"), _("Maratone") };
    int x = CM_X;
    for (int i = 0; i < NLISTS; i++) {
      TTF_Font *f = font(list == i ? W_MED : W_REG, 26);
      int w = text_w(f, names[i]) + 48;
      fill_rrect(x, y0, w, 54, 27, list == i ? C_ACC : C_WHITE, list == i ? a : a * 8 / 100);
      draw_text(f, names[i], x + 24, y0 + 12, list == i ? C_WHITE : C_DIM, a, AL_L);
      x += w + 14;
    }
    if (rme.loaded) {
      char now[120]; snprintf(now, sizeof now, _("%d online \xC2\xB7 %d in gioco"), rme.online, rme.playing);
      fill_circle(x + 30, y0 + 27, 7, C_OK, a);
      draw_text(font(W_REG, 24), now, x + 48, y0 + 12, C_DIM, a, AL_L);
    }
    char pos[200] = "";
    if (rme.loaded && !rme.listed) snprintf(pos, sizeof pos, "%s", _("Non compari nei record: puoi cambiarlo in Impostazioni \xE2\x80\xBA Privacy"));
    else if (rme.rank) snprintf(pos, sizeof pos, _("La tua posizione: %d su %d"), rme.rank, rme.of);
    else if (rme.loaded) snprintf(pos, sizeof pos, "%s", list == L_MARATHON ? _("Il tempo pi\xC3\xB9 lungo di un giocatore su un solo gioco") : _("Gioca qualcosa per entrare in classifica"));
    draw_text(font(W_REG, 24), pos, CM_X + 6, y0 + 78, rme.loaded && !rme.listed ? C_WARN : C_DIM, a, AL_L);
  }
  cm_chip(CM_X + CM_W - 280, y0 + 10, week ? _("Questa settimana") : _("Da sempre"), C_ACC, a);

  int n = records_count();
  follow(n);
  SDL_Rect clip = { 0, VIEW_TOP - 10, SCREEN_W, VIEW_H + 20 };
  SDL_RenderSetClipRect(R, &clip);
  for (int i = 0; i < n; i++) {
    int y = VIEW_TOP + i * ROW_H - (int)scroll;
    if (y + ROW_H < VIEW_TOP - 20 || y > VIEW_TOP + VIEW_H) continue;
    if (gview.open) player_row(&gplayers[i], y, sel == i, a, 0);
    else if (list == L_PLAYERS) player_row(&players[i], y, sel == i, a, 1);
    else if (list == L_GAMES) game_row(&games[i], i, y, sel == i, a);
    else player_row(&marathons[i], y, sel == i, a, 2);
  }
  SDL_RenderSetClipRect(R, NULL);
  if (!n && (rme.loaded || gview.open)) empty(_("Nessun record ancora"), week ? _("Questa settimana non ha ancora giocato nessuno: prova \xC2\xAB" "Da sempre\xC2\xBB.") : NULL, a);
}

static void draw_trophy_ranking(int a) {
  int y0 = CM_TOP;
  draw_text(font(W_MED, 30), _("Classifica dei trofei"), CM_X + 6, y0, C_TXT, a, AL_L);
  draw_text(font(W_REG, 23), _("Platino 300 \xC2\xB7 oro 90 \xC2\xB7 argento 30 \xC2\xB7 bronzo 15 punti. I trofei arrivano dalla console."), CM_X + 6, y0 + 44, C_DIM, a, AL_L);
  char pos[220] = "";
  if (tme.loaded && !tme.importing) snprintf(pos, sizeof pos, "%s", _("L'importazione dei trofei \xC3\xA8 spenta: accendila in Impostazioni \xE2\x80\xBA Privacy"));
  else if (tme.loaded && !tme.listed) snprintf(pos, sizeof pos, "%s", _("I tuoi trofei non sono pubblici: puoi cambiarlo in Impostazioni \xE2\x80\xBA Privacy"));
  else if (tme.rank) snprintf(pos, sizeof pos, _("La tua posizione: %d su %d"), tme.rank, tme.of);
  else if (consync_busy()) snprintf(pos, sizeof pos, "%s", _("Sto leggendo i trofei di questa console..."));
  draw_text(font(W_REG, 24), pos, CM_X + 6, y0 + 82, tme.loaded && (!tme.listed || !tme.importing) ? C_WARN : C_DIM, a, AL_L);
  if (consync_busy()) draw_spinner(CM_X + CM_W - 40, y0 + 24, 14, a);
  else if (sync_seen != consync_rev()) records_load(1);       // importazione finita: la classifica è cambiata

  follow(ntranks);
  SDL_Rect clip = { 0, VIEW_TOP - 10, SCREEN_W, VIEW_H + 20 };
  SDL_RenderSetClipRect(R, &clip);
  for (int i = 0; i < ntranks; i++) {
    int y = VIEW_TOP + i * ROW_H - (int)scroll;
    if (y + ROW_H < VIEW_TOP - 20 || y > VIEW_TOP + VIEW_H) continue;
    TRank *t = &tranks[i];
    cm_card(CM_X, y, CM_W, ROW_H - 16, sel == i, a);
    if (t->me && sel != i) fill_rrect(CM_X, y, CM_W, ROW_H - 16, 22, C_ACC, a * 18 / 100);
    char n[8]; snprintf(n, sizeof n, "%d", t->rank);
    draw_text(font(W_BOLD, 32), n, CM_X + 56, y + 20, rank_col(t->rank), a, AL_C);
    draw_avatar(t->oid, t->avatar, CM_X + 140, y + 40, 60, a);
    draw_text_fit(font(W_MED, 28), t->oid, CM_X + 190, y + 8, 520, C_TXT, a, AL_L);
    char sub[64]; snprintf(sub, sizeof sub, t->sets == 1 ? _("%d gioco con trofei") : _("%d giochi con trofei"), t->sets);
    draw_text(font(W_REG, 22), sub, CM_X + 190, y + 44, C_DIM, a, AL_L);
    trophy_counts(CM_X + CM_W - 760, y + 26, t->p, t->g, t->s, t->b, a);
    char pts[40]; snprintf(pts, sizeof pts, _("%d punti"), t->points);
    draw_text(font(W_MED, 30), pts, CM_X + CM_W - 40, y + 20, C_TXT, a, AL_R);
  }
  SDL_RenderSetClipRect(R, NULL);
  if (!ntranks && tme.loaded) empty(_("Nessun trofeo in classifica"), _("I trofei ottenuti nei giochi si leggono da soli dalla console quando apri Omega. Con Quadrato li rileggi adesso."), a);
}

void records_draw(int trophies, int a) { if (trophies) draw_trophy_ranking(a); else draw_records(a); }

int records_hints(int trophies, int *ic, const char **lb) {
  int n = 0;
  if (trophies) {
    if (ntranks) { ic[n] = IC_BTN_X; lb[n++] = _("Vedi i trofei"); }
    ic[n] = IC_BTN_TRI; lb[n++] = _("I miei trofei");
    ic[n] = IC_BTN_SQ; lb[n++] = _("Rileggi dalla console");
  } else {
    if (records_count()) { ic[n] = IC_BTN_X; lb[n++] = !gview.open && list == L_GAMES ? _("Classifica del gioco") : _("Profilo"); }
    ic[n] = IC_BTN_SQ; lb[n++] = _("Periodo");
  }
  ic[n] = IC_BTN_O; lb[n++] = gview.open ? _("Indietro") : _("Chiudi");
  return n;
}

// 1 = tasto gestito qui; 0 = lo gestisce la Community (schede, chiusura).
int records_input(int trophies, int b) {
  int n = trophies ? ntranks : records_count();
  if (b == B_UP) { if (sel > 0) sel--; return 1; }
  if (b == B_DOWN) { if (sel < n - 1) sel++; return 1; }
  if (trophies) {
    if (b == B_X && sel < ntranks) { trophies_open(tranks[sel].oid); return 1; }
    if (b == B_TRI) { trophies_open(S.me); return 1; }
    if (b == B_SQ) {
      if (tme.loaded && !tme.importing) { set_msg(_("L'importazione dei trofei \xC3\xA8 spenta: accendila in Impostazioni \xE2\x80\xBA Privacy"), 1); return 1; }
      consync_start(1); set_msg(_("Rileggo i trofei di questa console..."), 0);
      return 1;
    }
    return 0;
  }
  if (gview.open) {
    if (b == B_O) { gview.open = 0; reset_sel(); return 1; }
    if (b == B_L1 || b == B_R1) return 1;
    if (b == B_X && sel < ngplayers) { profile_open(gplayers[sel].oid); return 1; }
    if (b == B_SQ) { week = !week; load_game_ranking(); return 1; }
    return 0;
  }
  if (b == B_LEFT || b == B_RIGHT) { list = (list + (b == B_RIGHT ? 1 : NLISTS - 1)) % NLISTS; reset_sel(); return 1; }
  if (b == B_SQ) { week = !week; reset_sel(); records_load(0); return 1; }
  if (b == B_X && sel < n) {
    if (list == L_PLAYERS) profile_open(players[sel].oid);
    else if (list == L_MARATHON) profile_open(marathons[sel].oid);
    else { gview.open = 1; gview.g = games[sel]; ngplayers = 0; reset_sel(); load_game_ranking(); }
    return 1;
  }
  return 0;
}

// --------------------------------------------------- i trofei di un utente --
typedef struct { char np[16], title[110], icon[20], last[32]; int parsed, e[4], t[4], earned, total, progress, points; } TSetRow;
typedef struct { int id, grade, hidden, earned; char name[130], detail[260], when[32]; } TRow;
static struct {
  char oid[32]; int loading, status;       // status: 0 ok, 403 non visibili, altro = errore
  int sum[4], points;
  TSetRow sets[150]; int nsets;
  int in_set; TSetRow cur; TRow rows[220]; int nrows; int set_loading;
  int sel; float scroll, scroll_t;
} TV;

#define TV_TOP 250
#define TV_H (SCREEN_H - TV_TOP - 100)

static void on_user_sets(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; (void)ud;
  if (st == 200 && strcasecmp(jstr(j, "online_id", ""), TV.oid)) return;
  TV.loading = 0; TV.status = st == 200 ? 0 : st;
  if (st != 200) return;
  JVal *s = jget(j, "summary");
  TV.sum[0] = (int)jnum(s, "p", 0); TV.sum[1] = (int)jnum(s, "g", 0); TV.sum[2] = (int)jnum(s, "s", 0); TV.sum[3] = (int)jnum(s, "b", 0);
  TV.points = (int)jnum(s, "points", 0);
  TV.nsets = 0;
  JFOR(o, jget(j, "sets")) {
    if (TV.nsets >= 150) break;
    TSetRow *r = &TV.sets[TV.nsets++]; memset(r, 0, sizeof *r);
    jcpy(r->np, sizeof r->np, o, "np_id"); jcpy(r->title, sizeof r->title, o, "title"); jcpy(r->icon, sizeof r->icon, o, "icon_media");
    jcpy(r->last, sizeof r->last, o, "last_earned");
    r->parsed = jbool(o, "parsed"); r->earned = (int)jnum(o, "earned_count", 0); r->total = (int)jnum(o, "total_count", 0);
    r->progress = (int)jnum(o, "progress", 0); r->points = (int)jnum(o, "points", 0);
    static const char *K[4] = { "p", "g", "s", "b" };
    for (int k = 0; k < 4; k++) { r->e[k] = (int)jnum(jget(o, "earned"), K[k], 0); r->t[k] = (int)jnum(jget(o, "total"), K[k], 0); }
  }
}
static void on_user_set(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; (void)ud;
  if (st != 200 || strcmp(jstr(j, "np_id", ""), TV.cur.np)) { if (st != 200) TV.set_loading = 0; return; }
  TV.set_loading = 0; TV.nrows = 0;
  JFOR(o, jget(j, "trophies")) {
    if (TV.nrows >= 220) break;
    TRow *r = &TV.rows[TV.nrows++]; memset(r, 0, sizeof *r);
    r->id = (int)jnum(o, "id", 0); r->grade = grade_index(jstr(o, "grade", "B")); r->hidden = jbool(o, "hidden"); r->earned = jbool(o, "earned");
    jcpy(r->name, sizeof r->name, o, "name"); jcpy(r->detail, sizeof r->detail, o, "detail"); jcpy(r->when, sizeof r->when, o, "earned_at");
  }
}

void trophies_open(const char *oid) {
  memset(&TV, 0, sizeof TV);
  snprintf(TV.oid, sizeof TV.oid, "%s", oid);
  TV.loading = 1;
  char path[96]; snprintf(path, sizeof path, OMEGA_API "/users/%s/trophies", oid);
  net_req(HTTP_GET, path, NULL, on_user_sets, NULL);
  if (ov_top() != OV_TROPHIES) ov_push(OV_TROPHIES);
}

static void tv_follow(int count, int rh) {
  if (TV.sel >= count) TV.sel = count ? count - 1 : 0;
  float t = TV.scroll_t; int ys = TV.sel * rh;
  if (ys - t < 0) t = (float)ys;
  if (ys + rh - t > TV_H) t = (float)(ys + rh - TV_H);
  TV.scroll_t = t < 0 ? 0 : t;
  TV.scroll = approach(TV.scroll, TV.scroll_t, 14.0f);
}

void trophies_draw(float t) {
  int a = (int)(255 * t);
  int self = !strcasecmp(TV.oid, S.me);
  fill_rect(0, 0, SCREEN_W, SCREEN_H, mix(RGB(10, 12, 20), g_theme_base, 0.12f), a);
  grad_v(0, 0, SCREEN_W, 420, mix(RGB(255, 200, 60), C_BLACK, 0.72f), a * 45 / 100, RGB(10, 12, 20), 0);
  draw_avatar(TV.oid, 0, CM_X + 44, 96, 88, a);
  char title[120]; snprintf(title, sizeof title, self ? _("I tuoi trofei") : _("Trofei di %s"), TV.oid);
  draw_text_fit(font(W_LIGHT, 48), title, CM_X + 110, 50, 900, C_WHITE, a, AL_L);
  if (!TV.status && !TV.loading) {
    int w = trophy_counts(CM_X + 112, 118, TV.sum[0], TV.sum[1], TV.sum[2], TV.sum[3], a);
    char pts[40]; snprintf(pts, sizeof pts, _("%d punti"), TV.points);
    draw_text(font(W_MED, 23), pts, CM_X + 112 + w + 10, 118, C_DIM, a, AL_L);
  }
  if (TV.loading || TV.set_loading) draw_spinner(SCREEN_W - CM_X - 20, 90, 14, a);

  if (TV.status) {
    empty(TV.status == 403 ? _("Questi trofei non sono visibili") : _("Trofei non disponibili"), TV.status == 403 ? _("L'utente ha scelto di non mostrarli.") : NULL, a);
  } else if (TV.in_set) {
    game_icon("", TV.cur.icon, CM_X, 176, 60, a);
    draw_text_fit(font(W_MED, 30), TV.cur.title, CM_X + 78, 176, CM_W - 500, C_TXT, a, AL_L);
    char pr[80]; snprintf(pr, sizeof pr, _("%d di %d \xC2\xB7 %d%%"), TV.cur.earned, TV.cur.total, TV.cur.progress);
    draw_text(font(W_REG, 23), pr, CM_X + 78, 212, C_DIM, a, AL_L);
    const int rh = 104;
    tv_follow(TV.nrows, rh);
    SDL_Rect clip = { 0, TV_TOP - 10, SCREEN_W, TV_H + 20 };
    SDL_RenderSetClipRect(R, &clip);
    for (int i = 0; i < TV.nrows; i++) {
      int y = TV_TOP + i * rh - (int)TV.scroll;
      if (y + rh < TV_TOP - 20 || y > TV_TOP + TV_H) continue;
      TRow *r = &TV.rows[i];
      cm_card(CM_X, y, CM_W, rh - 16, TV.sel == i, a);
      int la = r->earned ? a : a * 40 / 100;
      cup(CM_X + 60, y + 44, 28, r->grade, la);
      const char *name = r->name[0] ? r->name : _("Trofeo nascosto");
      draw_text_fit(font(W_MED, 27), name, CM_X + 116, y + 10, CM_W - 520, r->earned ? C_TXT : C_DIM, a, AL_L);
      const char *det = r->detail[0] ? r->detail : (r->name[0] ? "" : _("Si scopre ottenendolo."));
      draw_text_fit(font(W_REG, 22), det, CM_X + 116, y + 48, CM_W - 520, C_DIM, a, AL_L);
      if (r->earned) {
        char tm[64] = ""; if (r->when[0]) rel_time(r->when, tm, sizeof tm);
        draw_icon(IC_CHECK, CM_X + CM_W - 56, y + 44, 30, C_OK, a);
        draw_text(font(W_REG, 22), tm[0] ? tm : _("Ottenuto"), CM_X + CM_W - 86, y + 30, C_OK, a, AL_R);
      } else draw_text(font(W_REG, 22), _("Da ottenere"), CM_X + CM_W - 40, y + 30, C_FAINT, a, AL_R);
    }
    SDL_RenderSetClipRect(R, NULL);
    if (!TV.nrows && !TV.set_loading) empty(_("Elenco dei trofei non ancora disponibile"), NULL, a);
  } else {
    const int rh = 112;
    tv_follow(TV.nsets, rh);
    SDL_Rect clip = { 0, TV_TOP - 70, SCREEN_W, TV_H + 80 };
    SDL_RenderSetClipRect(R, &clip);
    for (int i = 0; i < TV.nsets; i++) {
      int y = TV_TOP - 60 + i * rh - (int)TV.scroll;
      if (y + rh < TV_TOP - 80 || y > TV_TOP + TV_H) continue;
      TSetRow *r = &TV.sets[i];
      cm_card(CM_X, y, CM_W, rh - 16, TV.sel == i, a);
      game_icon("", r->icon, CM_X + 16, y + 12, 72, a);
      draw_text_fit(font(W_MED, 28), r->title, CM_X + 110, y + 10, CM_W - 900, C_TXT, a, AL_L);
      char sub[120];
      if (!r->parsed) snprintf(sub, sizeof sub, "%s", _("Importato: lettura dei trofei in arrivo"));
      else {
        char tm[64] = ""; if (r->last[0]) rel_time(r->last, tm, sizeof tm);
        int l = snprintf(sub, sizeof sub, _("%d di %d"), r->earned, r->total);
        if (tm[0]) snprintf(sub + l, sizeof sub - (size_t)l, "  \xC2\xB7  %s", tm);
      }
      draw_text_fit(font(W_REG, 22), sub, CM_X + 110, y + 50, CM_W - 900, C_DIM, a, AL_L);
      trophy_counts(CM_X + CM_W - 760, y + 34, r->e[0], r->e[1], r->e[2], r->e[3], a);
      int bx = CM_X + CM_W - 300, bw = 190;
      fill_rrect(bx, y + 44, bw, 10, 5, C_WHITE, a * 10 / 100);
      if (r->progress > 0) fill_rrect(bx, y + 44, bw * r->progress / 100, 10, 5, r->progress >= 100 ? GRADE_COL[0] : C_ACC2, a);
      char pc[12]; snprintf(pc, sizeof pc, "%d%%", r->progress);
      draw_text(font(W_MED, 26), pc, CM_X + CM_W - 40, y + 32, C_TXT, a, AL_R);
    }
    SDL_RenderSetClipRect(R, NULL);
    if (!TV.nsets && !TV.loading)
      empty(_("Nessun trofeo ancora"), self ? _("I trofei ottenuti nei giochi si leggono da soli dalla console quando apri Omega. Con Quadrato li rileggi adesso.") : NULL, a);
  }
  int ic[3]; const char *lb[3]; int n = 0;
  if (!TV.status && !TV.in_set && TV.nsets) { ic[n] = IC_BTN_X; lb[n++] = _("Vedi i trofei"); }
  if (self && !TV.in_set) { ic[n] = IC_BTN_SQ; lb[n++] = _("Rileggi dalla console"); }
  ic[n] = IC_BTN_O; lb[n++] = TV.in_set ? _("Indietro") : _("Chiudi");
  hints(ic, lb, n, a);
}

void trophies_input(int b) {
  int n = TV.in_set ? TV.nrows : TV.nsets;
  if (b == B_O) { if (TV.in_set) { TV.in_set = 0; TV.sel = 0; TV.scroll = TV.scroll_t = 0; } else ov_pop(); return; }
  if (b == B_UP && TV.sel > 0) TV.sel--;
  else if (b == B_DOWN && TV.sel < n - 1) TV.sel++;
  else if (b == B_X && !TV.in_set && !TV.status && TV.sel < TV.nsets) {
    TV.cur = TV.sets[TV.sel]; TV.in_set = 1; TV.nrows = 0; TV.set_loading = 1; TV.sel = 0; TV.scroll = TV.scroll_t = 0;
    char path[140]; snprintf(path, sizeof path, OMEGA_API "/users/%s/trophies/%s", TV.oid, TV.cur.np);
    net_req(HTTP_GET, path, NULL, on_user_set, NULL);
  } else if (b == B_SQ && !TV.in_set && !strcasecmp(TV.oid, S.me)) {
    consync_start(1); set_msg(_("Rileggo i trofei di questa console..."), 0);
  }
}
