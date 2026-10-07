// Omega UI — Impostazioni › Account › Notifiche: quali notifiche arrivano e da
// chi. Le regole le applica il server quando una notifica nasce (notifyprefs.js):
// tipo spento o "solo preferiti" = non si crea; orari di silenzio e gioco in
// corso = resta nell'elenco ma senza avviso. Le stesse scelte si fanno dalla
// web app.
#include "app.h"
#include <math.h>
#include <stdlib.h>
#include <time.h>

static const struct { const char *key, *name, *desc; } TY[] = {
  { "online", N_("Un amico è online"), N_("Quando un amico accende la console") },
  { "game_start", N_("Un amico inizia a giocare"), N_("Ogni volta che un amico apre o cambia gioco") },
  { "message", N_("Messaggi"), N_("Messaggi privati") },
  { "party_invite", N_("Inviti a un party"), N_("Qualcuno ti invita nel suo party") },
  { "game_invite", N_("Inviti a giocare"), N_("Un amico ti propone un gioco") },
  { "friend_request", N_("Richieste di amicizia"), N_("Qualcuno vuole aggiungerti") },
  { "friend_accept", N_("Richieste accettate"), N_("Un tuo invito di amicizia è stato accettato") },
  { "post_like", N_("Mi piace ai tuoi post"), N_("Nella bacheca della Community") },
  { "post_comment", N_("Commenti ai tuoi post"), N_("Nella bacheca della Community") },
  { "store_comment", N_("Commenti ai tuoi homebrew"), N_("Sulle app che hai pubblicato nello Store") },
  { "store_recommend", N_("App consigliate dagli amici"), N_("Un amico ti consiglia un homebrew") },
  { "store_update", N_("Aggiornamenti delle app"), N_("Versioni nuove delle app desiderate o installate") },
};
#define NTY (int)(sizeof TY / sizeof *TY)
static const char *MODES[3] = { "all", "favorites", "off" };
static const char *MODE_L[3] = { N_("Tutti"), N_("Solo preferiti"), N_("Nessuno") };
static const char *GAME[3] = { "all", "important", "off" };
static const char *GAME_L[3] = { N_("Tutte"), N_("Solo importanti"), N_("Nessuna") };
// orari di silenzio: preimpostati, comodi col controller
static const struct { const char *from, *to; } QUIET[] = { { "23:00", "08:00" }, { "22:00", "07:00" }, { "00:00", "09:00" }, { "21:00", "09:00" }, { "13:00", "15:00" } };
#define NQUIET (int)(sizeof QUIET / sizeof *QUIET)

static int mode[NTY], game_mode = 1, quiet_on, quiet_idx, loaded, loading;
typedef struct { char oid[32]; int avatar, fav, muted; } NFriend;
static NFriend nf[MAX_FRIENDS]; static int nnf;
static int sel; static float anim, scroll;
// righe: 0..NTY-1 tipi, NTY in gioco, NTY+1 orari, poi gli amici
#define ROW_GAME  NTY
#define ROW_QUIET (NTY + 1)
#define ROW_FR0   (NTY + 2)

static int tz_offset(void) {
  time_t t = time(NULL); struct tm g = *gmtime(&t), l = *localtime(&t);
  int d = (l.tm_hour - g.tm_hour) * 60 + (l.tm_min - g.tm_min);
  if (l.tm_yday != g.tm_yday) d += (l.tm_yday > g.tm_yday || (l.tm_yday == 0 && g.tm_yday > 300)) ? 1440 : -1440;
  return d;
}

static void parse(JVal *j) {
  JVal *t = jget(j, "types");
  for (int i = 0; i < NTY; i++) { const char *v = jstr(t, TY[i].key, "all"); mode[i] = !strcmp(v, "favorites") ? 1 : !strcmp(v, "off") ? 2 : 0; }
  const char *g = jstr(j, "in_game", "important"); game_mode = !strcmp(g, "all") ? 0 : !strcmp(g, "off") ? 2 : 1;
  JVal *q = jget(j, "quiet"); quiet_on = jbool(q, "enabled");
  const char *from = jstr(q, "from", "23:00"); quiet_idx = 0;
  for (int i = 0; i < NQUIET; i++) if (!strcmp(QUIET[i].from, from)) quiet_idx = i;
  nnf = 0;
  JFOR(f, jget(j, "friends")) {
    if (nnf >= MAX_FRIENDS) break;
    NFriend *x = &nf[nnf++]; jcpy(x->oid, sizeof x->oid, f, "online_id"); x->avatar = (int)jnum(f, "avatar", 0);
    x->fav = jbool(f, "favorite"); x->muted = jbool(f, "muted"); media_note_json(x->oid, f);
  }
  loaded = 1;
}
static void on_prefs(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; (void)ud; loading = 0;
  if (st == 200 && j) parse(j); else set_msg(_("Preferenze delle notifiche non disponibili: il server è aggiornato?"), 1);
}
static void save(void) {
  char body[1600]; size_t o = 0;
  o += (size_t)snprintf(body + o, sizeof body - o, "{\"types\":{");
  for (int i = 0; i < NTY; i++) o += (size_t)snprintf(body + o, sizeof body - o, "%s\"%s\":\"%s\"", i ? "," : "", TY[i].key, MODES[mode[i]]);
  snprintf(body + o, sizeof body - o, "},\"in_game\":\"%s\",\"quiet\":{\"enabled\":%s,\"from\":\"%s\",\"to\":\"%s\"},\"tz_offset\":%d}",
           GAME[game_mode], quiet_on ? "true" : "false", QUIET[quiet_idx].from, QUIET[quiet_idx].to, tz_offset());
  net_req(HTTP_POST, OMEGA_API "/notifications/prefs", body, on_prefs, NULL);
}
static void on_friend(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; (void)ud;
  if (st != 200 || !j) { set_msg(_("Operazione non riuscita"), 1); return; }
  const char *oid = jstr(j, "online_id", "");
  for (int i = 0; i < nnf; i++) if (!strcasecmp(nf[i].oid, oid)) { nf[i].fav = jbool(j, "favorite"); nf[i].muted = jbool(j, "muted"); }
}
static void friend_set(int i, int fav, int muted) {
  char esc[80], body[160]; json_escape(esc, sizeof esc, nf[i].oid);
  snprintf(body, sizeof body, "{\"online_id\":\"%s\",\"favorite\":%s,\"muted\":%s}", esc, fav ? "true" : "false", muted ? "true" : "false");
  nf[i].fav = fav; nf[i].muted = muted;   // subito a schermo, poi il server conferma
  net_req(HTTP_POST, OMEGA_API "/notifications/prefs/friend", body, on_friend, NULL);
}

void notifprefs_open(void) {
  sel = 0; scroll = 0;
  if (!loading) { loading = 1; net_req(HTTP_GET, OMEGA_API "/notifications/prefs", NULL, on_prefs, NULL); }
  if (ov_top() != OV_NOTIFPREFS) ov_push(OV_NOTIFPREFS);
}

static int nrows(void) { return ROW_FR0 + nnf; }

static void seg(int x, int y, int w, const char *const *labels, int n, int cur, int foc, int a) {
  int cw = w / n;
  fill_rrect(x, y, w, 46, 23, C_WHITE, a * 8 / 100);
  for (int i = 0; i < n; i++) {
    int on = i == cur;
    if (on) fill_rrect(x + i * cw + 3, y + 3, cw - 6, 40, 20, foc ? C_WHITE : C_ACC, a);
    draw_text_fit(font(on ? W_MED : W_REG, 20), _(labels[i]), x + i * cw + cw / 2, y + 11, cw - 16, on ? (foc ? RGB(12, 14, 22) : C_WHITE) : C_DIM, a, AL_C);
  }
}

void notifprefs_draw(float t) {
  int a = (int)(255 * t);
  fill_rect(0, 0, SCREEN_W, SCREEN_H, mix(RGB(10, 12, 20), g_theme_base, 0.12f), t > 0.98f ? 255 : a);
  int x0 = 140, w = SCREEN_W - 280;
  draw_icon(IC_BELL, x0 + 22, 74, 44, C_ACC2, a);
  draw_text(font(W_LIGHT, 38), _("Notifiche"), x0 + 62, 52, C_WHITE, a, AL_L);
  draw_text_fit(font(W_REG, 22), _("Scegli cosa ti arriva e da chi. «Solo preferiti» = solo dagli amici con la stella."), x0 + 300, 66, w - 300, C_DIM, a, AL_L);
  if (!loaded) { draw_spinner(SCREEN_W / 2, 400, 22, a); return; }
  int top = 140, bottom = SCREEN_H - 130, rh = 82;
  anim = approach(anim, (float)sel, 20.0f);
  // posizione a schermo di ogni riga (le intestazioni prendono spazio)
  int ys[ROW_FR0 + MAX_FRIENDS]; int cy = 0;
  for (int i = 0; i < nrows(); i++) { if (i == ROW_GAME || i == ROW_FR0) cy += 60; ys[i] = cy; cy += rh; }
  float tgt = ys[sel] + rh > bottom - top ? (float)(ys[sel] + rh - (bottom - top) + 30) : 0;
  scroll = approach(scroll, tgt, 14.0f);
  SDL_Rect clip = { 0, top - 8, SCREEN_W, bottom - top + 8 }; SDL_RenderSetClipRect(R, &clip);
  int sw = 560, sx = x0 + w - sw - 20;
  for (int i = 0; i < nrows(); i++) {
    int y = top + ys[i] - (int)scroll;
    if (i == ROW_GAME) draw_text(font(W_MED, 26), _("Quando arrivano"), x0, y - 48, C_TXT, a, AL_L);
    if (i == ROW_FR0) { draw_text(font(W_MED, 26), _("Amici"), x0, y - 48, C_TXT, a, AL_L); draw_text(font(W_REG, 20), _("\xE2\x9C\x95 preferito   \xE2\x96\xA1 silenzia"), x0 + 120, y - 44, C_DIM, a, AL_L); }
    if (y + rh < top - 8 || y > bottom) continue;
    int foc = i == sel;
    float fa = clampf(1 - fabsf(anim - i), 0, 1);
    fill_rrect(x0, y, w, rh - 10, 18, C_WHITE, (int)(a * (0.04f + 0.08f * fa)));
    if (foc) stroke_rrect(x0 - 3, y - 3, w + 6, rh - 4, 21, 3, C_WHITE, a);
    if (i < NTY) {
      draw_text_fit(font(W_MED, 26), _(TY[i].name), x0 + 28, y + 10, sx - x0 - 40, C_TXT, a, AL_L);
      draw_text_fit(font(W_REG, 19), _(TY[i].desc), x0 + 28, y + 44, sx - x0 - 40, C_FAINT, a, AL_L);
      seg(sx, y + 12, sw, MODE_L, 3, mode[i], foc, a);
    } else if (i == ROW_GAME) {
      draw_text(font(W_MED, 26), _("Durante il gioco"), x0 + 28, y + 10, C_TXT, a, AL_L);
      draw_text_fit(font(W_REG, 19), _("Importanti: messaggi, inviti e richieste di amicizia. Le altre restano nell'elenco."), x0 + 28, y + 44, sx - x0 - 40, C_FAINT, a, AL_L);
      seg(sx, y + 12, sw, GAME_L, 3, game_mode, foc, a);
    } else if (i == ROW_QUIET) {
      draw_text(font(W_MED, 26), _("Orari di silenzio"), x0 + 28, y + 10, C_TXT, a, AL_L);
      draw_text_fit(font(W_REG, 19), _("In quegli orari niente avvisi: le notifiche restano nell'elenco"), x0 + 28, y + 44, sx - x0 - 40, C_FAINT, a, AL_L);
      char v[64]; if (quiet_on) snprintf(v, sizeof v, _("dalle %s alle %s"), QUIET[quiet_idx].from, QUIET[quiet_idx].to); else snprintf(v, sizeof v, "%s", _("Spenti"));
      fill_rrect(sx, y + 12, sw, 46, 23, quiet_on ? C_ACC : C_WHITE, quiet_on ? a : a * 8 / 100);
      draw_text(font(W_MED, 22), "\xE2\x80\xB9", sx + 24, y + 20, C_WHITE, a, AL_L);
      draw_text(font(W_MED, 22), v, sx + sw / 2, y + 22, C_WHITE, a, AL_C);
      draw_text(font(W_MED, 22), "\xE2\x80\xBA", sx + sw - 24, y + 20, C_WHITE, a, AL_R);
    } else {
      NFriend *f = &nf[i - ROW_FR0];
      draw_avatar(f->oid, f->avatar, x0 + 56, y + (rh - 10) / 2, 50, a);
      draw_text_fit(font(W_MED, 26), f->oid, x0 + 100, y + 20, 700, f->muted ? C_DIM : C_TXT, a, AL_L);
      int bx = x0 + w - 380;
      fill_rrect(bx, y + 14, 170, 44, 22, f->fav ? C_WARN : C_WHITE, f->fav ? a : a * 8 / 100);
      draw_icon(IC_STAR, bx + 30, y + 36, 22, f->fav ? RGB(20, 20, 30) : C_DIM, a);
      draw_text(font(W_MED, 20), _("Preferito"), bx + 52, y + 24, f->fav ? RGB(20, 20, 30) : C_DIM, a, AL_L);
      bx += 190;
      fill_rrect(bx, y + 14, 170, 44, 22, f->muted ? C_ERR : C_WHITE, f->muted ? a : a * 8 / 100);
      draw_icon(IC_BELL, bx + 30, y + 36, 22, f->muted ? C_WHITE : C_DIM, a);
      draw_text(font(W_MED, 20), f->muted ? _("Silenziato") : _("Silenzia"), bx + 52, y + 24, f->muted ? C_WHITE : C_DIM, a, AL_L);
    }
  }
  if (!nnf) draw_text(font(W_REG, 22), _("Aggiungi degli amici per scegliere i preferiti"), x0, top + cy - (int)scroll + 10, C_FAINT, a, AL_L);
  SDL_RenderSetClipRect(R, NULL);
  int ic[4]; const char *lb[4]; int n = 0;
  if (sel >= ROW_FR0) { ic[n] = IC_BTN_X; lb[n++] = _("Preferito"); ic[n] = IC_BTN_SQ; lb[n++] = _("Silenzia"); }
  else { ic[n] = -1; lb[n++] = _("\xE2\x86\x90 \xE2\x86\x92  Cambia"); }
  ic[n] = IC_BTN_O; lb[n++] = _("Indietro");
  hints(ic, lb, n, a);
}

void notifprefs_input(int b) {
  if (b == B_O) { ov_pop(); return; }
  if (!loaded) return;
  if (b == B_UP && sel > 0) { sel--; sfx_play(SFX_MOVE); return; }
  if (b == B_DOWN && sel < nrows() - 1) { sel++; sfx_play(SFX_MOVE); return; }
  int d = b == B_RIGHT || b == B_X ? 1 : b == B_LEFT ? -1 : 0;
  if (sel < NTY && d) { mode[sel] = (mode[sel] + d + 3) % 3; sfx_play(SFX_SELECT); save(); }
  else if (sel == ROW_GAME && d) { game_mode = (game_mode + d + 3) % 3; sfx_play(SFX_SELECT); save(); }
  else if (sel == ROW_QUIET && d) {
    // spenti → 23-08 → 22-07 → ... → spenti
    int s = quiet_on ? quiet_idx + 1 : 0; s = (s + d + NQUIET + 1) % (NQUIET + 1);
    quiet_on = s > 0; if (s) quiet_idx = s - 1;
    sfx_play(SFX_SELECT); save();
  } else if (sel >= ROW_FR0 && sel - ROW_FR0 < nnf) {
    NFriend *f = &nf[sel - ROW_FR0];
    if (b == B_X) friend_set(sel - ROW_FR0, !f->fav, f->fav ? f->muted : 0);
    else if (b == B_SQ) friend_set(sel - ROW_FR0, f->muted ? f->fav : 0, !f->muted);
  }
}
