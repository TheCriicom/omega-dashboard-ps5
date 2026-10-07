// Omega UI — pannelli sopra la home: menu e conferme, Centro di controllo,
// Game Base, notifiche, profilo, scelta avatar, chat, ricerca, impostazioni e
// lettore di notizie.
#include "app.h"
#include "servers.h"
#include <math.h>
#include <stdlib.h>
#include <time.h>

// ------------------------------------------------------------------ utilità --
static void backdrop(float t, int strength) { fill_rect(0, 0, SCREEN_W, SCREEN_H, RGB(2, 4, 10), (int)(strength * t)); }

// Pannello che entra da destra; ritorna la x. Il velo scuro copre solo la parte
// lasciata libera: fondere tutto lo schermo a ogni fotogramma costa troppo al
// renderer software.
static int side_panel(float t, int w) {
  float e = ease_out(t);
  int x = SCREEN_W - (int)(w * e);
  fill_rect(0, 0, x, SCREEN_H, RGB(2, 4, 10), (int)(110 * t));
  grad_h(x - 60, 0, 60, SCREEN_H, C_BLACK, 0, C_BLACK, (int)(120 * t));
  fill_rect(x, 0, w, SCREEN_H, RGB(16, 19, 30), t > 0.98f ? 255 : (int)(250 * t));
  fill_rect(x, 0, 2, SCREEN_H, RGB(70, 84, 120), (int)(90 * t));
  return x;
}

static void row_bg(int x, int y, int w, int h, float fa, int alpha) {
  if (fa <= 0.01f) return;
  fill_rrect(x, y, w, h, 18, C_WHITE, (int)(alpha * 0.11f * fa));
  stroke_rrect(x - 3, y - 3, w + 6, h + 6, 21, 3, C_WHITE, (int)(alpha * fa));
}

static void status_dot(int cx, int cy, const char *status, int playing, int alpha) {
  Col c = playing ? C_ACC2 : !strcmp(status, "online") ? C_OK : !strcmp(status, "away") ? C_WARN : !strcmp(status, "dnd") ? C_ERR : C_FAINT;
  fill_circle(cx, cy, 11, RGB(16, 19, 30), alpha);
  fill_circle(cx, cy, 7, c, alpha);
}

static void friend_status(const Friend *f, char *out, size_t n) {
  if (f->game_id[0]) { char t[64]; play_time(f->started, t, sizeof t); snprintf(out, n, "%s \xC2\xB7 %s", f->game_name[0] ? f->game_name : f->game_id, t); }
  else if (friend_online(f)) snprintf(out, n, "%s%s%s", !strcmp(f->status, "away") ? _("Assente") : !strcmp(f->status, "dnd") ? _("Non disturbare") : _("Online"),
                                       f->status_msg[0] ? " \xC2\xB7 " : "", f->status_msg);
  else if (f->last_seen[0]) { char t[64]; rel_time(f->last_seen, t, sizeof t); snprintf(out, n, _("Offline \xC2\xB7 visto %s"), t); }
  else snprintf(out, n, "%s", _("Offline"));
}

// scorrimento verticale di una lista che tiene visibile la riga selezionata
static float list_scroll(float *cur, int sel, int row_h, int visible_h) {
  float target = 0;
  int y = sel * row_h;
  if (y + row_h > visible_h) target = (float)(y + row_h - visible_h + row_h / 2);
  *cur = approach(*cur, target, 14.0f);
  return *cur;
}

// --------------------------------------------------------------------- menu --
static char mn_title[160]; static char mn_items[32][160]; static int mn_n, mn_sel; static MenuFn mn_fn; static void *mn_ud;
static float mn_anim;

void menu_open(const char *title, const char **items, int n, MenuFn fn, void *ud) {
  snprintf(mn_title, sizeof mn_title, "%s", title ? title : "");
  mn_n = n > 32 ? 32 : n;
  for (int i = 0; i < mn_n; i++) snprintf(mn_items[i], sizeof mn_items[i], "%s", items[i]);
  mn_sel = 0; mn_anim = 0; mn_fn = fn; mn_ud = ud;
  ov_push(OV_MENU);
}

// voce selezionata all'apertura (es. la scelta attuale)
void menu_select(int i) { if (i >= 0 && i < mn_n) mn_sel = i, mn_anim = (float)i; }

void menu_draw(float t) {
  backdrop(t, 120);
  mn_anim = approach(mn_anim, (float)mn_sel, 18.0f);
  // largo quanto la voce più lunga (le scelte della Privacy portano il valore accanto)
  int w = 640, rh = 82, h = 110 + mn_n * rh + 30;
  for (int i = 0; i < mn_n; i++) { int tw = text_w(font(W_MED, 28), mn_items[i]) + 130; if (tw > w) w = tw; }
  if (w > 1180) w = 1180;
  if (h > SCREEN_H - 120) h = SCREEN_H - 120;
  int x = SCREEN_W / 2 - w / 2, y = SCREEN_H / 2 - h / 2 + (int)((1 - ease_out(t)) * 50);
  int a = (int)(255 * t);
  shadow_rrect(x, y, w, h, 30, 40, a * 70 / 100);
  fill_rrect(x, y, w, h, 30, C_PANEL, a);
  stroke_rrect(x, y, w, h, 30, 1, RGB(80, 92, 125), a / 2);
  draw_text_fit(font(W_MED, 30), mn_title, x + 40, y + 36, w - 80, C_TXT, a, AL_L);
  int ly = y + 104;
  static float sc; float off = list_scroll(&sc, mn_sel, rh, h - 140);
  SDL_Rect clip = { x, ly - 8, w, h - 120 };
  SDL_RenderSetClipRect(R, &clip);
  for (int i = 0; i < mn_n; i++) {
    int ry = ly + i * rh - (int)off;
    float fa = clampf(1 - fabsf(mn_anim - i), 0, 1);
    row_bg(x + 20, ry, w - 40, rh - 10, fa, a);
    draw_text_fit(font(mn_sel == i ? W_MED : W_REG, 28), mn_items[i], x + 48, ry + 18, w - 120, C_TXT, a, AL_L);
  }
  SDL_RenderSetClipRect(R, NULL);
}

void menu_input(int b) {
  if (b == B_UP && mn_sel > 0) mn_sel--;
  else if (b == B_DOWN && mn_sel < mn_n - 1) mn_sel++;
  else if (b == B_O) ov_pop();
  else if (b == B_X) { MenuFn fn = mn_fn; void *ud = mn_ud; int s = mn_sel; ov_pop(); if (fn) fn(s, ud); }
}

// ----------------------------------------------------------------- conferma --
static char cf_msg[512], cf_yes[96]; static MenuFn cf_fn; static void *cf_ud; static int cf_sel;
void confirm_open(const char *msg, const char *yes, MenuFn fn, void *ud) {
  snprintf(cf_msg, sizeof cf_msg, "%s", msg); snprintf(cf_yes, sizeof cf_yes, "%s", yes);
  cf_fn = fn; cf_ud = ud; cf_sel = 1;
  ov_push(OV_CONFIRM);
}
void confirm_draw(float t) {
  backdrop(t, 150);
  int w = 820, h = 330, x = SCREEN_W / 2 - w / 2, y = SCREEN_H / 2 - h / 2 + (int)((1 - ease_out(t)) * 40);
  int a = (int)(255 * t);
  shadow_rrect(x, y, w, h, 30, 40, a * 70 / 100);
  fill_rrect(x, y, w, h, 30, C_PANEL, a);
  draw_text_wrap(font(W_REG, 32), cf_msg, x + 50, y + 50, w - 100, 3, 42, C_TXT, a);
  int bw = 340, by = y + h - 120;
  for (int i = 0; i < 2; i++) {
    int bx = x + 50 + i * (bw + 40);
    int foc = cf_sel == i;
    if (foc) shadow_rrect(bx, by, bw, 76, 38, 16, a / 2);
    fill_rrect(bx, by, bw, 76, 38, foc ? C_WHITE : RGB(48, 54, 72), a);
    draw_text(font(W_MED, 28), i == 0 ? cf_yes : _("Annulla"), bx + bw / 2, by + 22, foc ? RGB(10, 12, 20) : C_TXT, a, AL_C);
  }
}
void confirm_input(int b) {
  if (b == B_LEFT) cf_sel = 0; else if (b == B_RIGHT) cf_sel = 1;
  else if (b == B_O) ov_pop();
  else if (b == B_X) { MenuFn fn = cf_fn; void *ud = cf_ud; int s = cf_sel; ov_pop(); if (s == 0 && fn) fn(0, ud); }
}

// ------------------------------------------------------ Centro di controllo --
enum { CC_NOTIF, CC_GB, CC_COMMUNITY, CC_PARTY, CC_MSG, CC_MUSIC, CC_BROWSER, CC_PROFILE, CC_SETTINGS, CC_POWER, CC_N };
static int cc_sel; static float cc_anim;

static void power_menu(int idx, void *ud) {
  (void)ud;
  if (idx == 0) app_quit();
  else if (idx == 1) do_logout();
}

void cc_draw(float t) {
  backdrop(t, 120);
  cc_anim = approach(cc_anim, (float)cc_sel, 16.0f);
  float e = ease_out(t);
  int h = 470, y = SCREEN_H - (int)(h * e);
  int a = (int)(255 * t);
  grad_v(0, y - 120, SCREEN_W, 120, RGB(10, 13, 22), 0, RGB(10, 13, 22), (int)(220 * t));
  fill_rect(0, y, SCREEN_W, h, RGB(10, 13, 22), (int)(235 * t));
  char clock[16], date[96] = ""; time_t tt = time(NULL); struct tm *lt = localtime(&tt);
  snprintf(clock, sizeof clock, "%02d:%02d", lt ? lt->tm_hour : 0, lt ? lt->tm_min : 0);
  if (lt) date_long(date, sizeof date, lt->tm_wday, lt->tm_mday, lt->tm_mon);
  draw_text(font(W_LIGHT, 64), clock, 110, y + 26, C_WHITE, a, AL_L);
  draw_text(font(W_REG, 28), date, 290, y + 56, C_DIM, a, AL_L);
  draw_avatar(S.me, S.my_avatar, SCREEN_W - 150, y + 62, 64, a);
  draw_text(font(W_MED, 28), S.me, SCREEN_W - 196, y + 46, C_TXT, a, AL_R);

  static const char *lbl[CC_N] = { N_("Notifiche"), N_("Game Base"), N_("Community"), N_("Party"), N_("Messaggi"), N_("Musica"), N_("Browser"), N_("Profilo"), N_("Impostazioni"), N_("Spegni") };
  static const int ico[CC_N] = { IC_BELL, IC_FRIENDS, IC_NEWS, IC_PARTY, IC_CHAT, IC_MUSIC, IC_GLOBE, IC_USER, IC_GEAR, IC_POWER };
  int cw = 172, ch = 222, gap = 12, x0 = (SCREEN_W - (CC_N * cw + (CC_N - 1) * gap)) / 2, cy = y + 140;
  for (int i = 0; i < CC_N; i++) {
    int cx = x0 + i * (cw + gap);
    float fa = clampf(1 - fabsf(cc_anim - i), 0, 1);
    int lift = (int)(14 * fa);
    int foc = cc_sel == i;
    if (foc) shadow_rrect(cx, cy - lift, cw, ch, 28, 22, a * 60 / 100);
    fill_rrect(cx, cy - lift, cw, ch, 28, mix(RGB(30, 35, 50), RGB(235, 238, 245), fa), a);
    Col fg = mix(C_TXT, RGB(14, 16, 24), fa);
    if (i == CC_MUSIC && music_now_line()) music_mini(cx + cw / 2 - 30, cy - lift + 52, a);
    if (i == CC_MUSIC && music_now_line()) { draw_icon(music_playing() ? IC_PAUSE : IC_PLAY, cx + cw / 2 + 24, cy - lift + 82, 40, fg, a); }
    else draw_icon(ico[i], cx + cw / 2, cy - lift + 82, 70, fg, a);
    draw_text_fit(font(W_MED, 26), _(lbl[i]), cx + cw / 2, cy - lift + 140, cw - 16, fg, a, AL_C);
    char st[48] = "";
    switch (i) {
      case CC_NOTIF: if (S.unread_notif) snprintf(st, sizeof st, _("%d nuove"), S.unread_notif); break;
      case CC_GB: snprintf(st, sizeof st, _("%d online"), friends_online_count()); break;
      case CC_COMMUNITY: if (S.unread_groups) snprintf(st, sizeof st, _("%d nei gruppi"), S.unread_groups); else snprintf(st, sizeof st, "%s", _("Bacheca e gruppi")); break;
      case CC_PARTY: snprintf(st, sizeof st, "%s", S.party.active ? (voice_state() == 1 ? _("Voce attiva") : _("Attivo")) : S.ninv ? _("Invito!") : _("Nessuno")); break;
      case CC_MSG: if (S.unread_msg) snprintf(st, sizeof st, _("%d non letti"), S.unread_msg); break;
      case CC_PROFILE: snprintf(st, sizeof st, "%s", S.me); break;
      case CC_MUSIC: snprintf(st, sizeof st, "%s", music_now_line() ? music_now_line() : _("Radio e file")); break;
    }
    if (st[0]) draw_text_fit(font(W_REG, 21), st, cx + cw / 2, cy - lift + 176, cw - 24, mix(C_DIM, RGB(70, 76, 96), fa), a, AL_C);
    int badge = i == CC_NOTIF ? S.unread_notif : i == CC_MSG ? S.unread_msg : i == CC_GB ? S.in_req : i == CC_PARTY ? S.ninv : i == CC_COMMUNITY ? S.unread_groups : 0;
    if (badge) draw_badge(cx + cw - 26, cy - lift + 26, badge, a);
  }
  int ic[3] = { IC_BTN_X, IC_BTN_O, IC_BTN_SQ };
  const char *lb[3] = { _("Apri"), _("Chiudi"), music_playing() ? _("Pausa musica") : _("Riprendi musica") };
  hints(ic, lb, music_now_line() ? 3 : 2, a);
}

void cc_input(int b) {
  if (b == B_SQ) { music_toggle(); return; }
  if (b == B_LEFT && cc_sel > 0) cc_sel--;
  else if (b == B_RIGHT && cc_sel < CC_N - 1) cc_sel++;
  else if (b == B_O || b == B_OPT) ov_pop();
  else if (b == B_X) {
    ov_pop();
    switch (cc_sel) {
      case CC_NOTIF: social_load_notifications(); ov_push(OV_NOTIF); break;
      case CC_GB: gb_open(0); break;
      case CC_COMMUNITY: community_open(0); break;
      case CC_PARTY: gb_open(2); break;
      case CC_MSG: gb_open(3); break;
      case CC_MUSIC: music_open(); break;
      case CC_BROWSER: browser_open(NULL); break;
      case CC_PROFILE: profile_open(S.me); break;
      case CC_SETTINGS: ov_push(OV_SETTINGS); break;
      case CC_POWER: { const char *it[] = { _("Chiudi Omega"), _("Esci dall'account") }; menu_open(_("Opzioni di alimentazione"), it, 2, power_menu, NULL); break; }
    }
  }
}

// ---------------------------------------------------------------- Game Base --
enum { GB_FRIENDS, GB_REQ, GB_PARTY, GB_MSG, GB_NTABS };
enum { IT_ACTION, IT_FRIEND, IT_REQ_IN, IT_REQ_OUT, IT_HEADER, IT_PMEMBER, IT_PINVITE, IT_CONV, IT_EMPTY, IT_PARTYCARD };
typedef struct { int type; int idx; int icon; char title[100]; char sub[200]; char oid[32]; int avatar; int badge; } GbItem;
static int gb_tab, gb_zone = 1, gb_sel; static float gb_tab_anim, gb_sel_anim, gb_scroll;
static GbItem gbi[200]; static int ngbi;

static void add_item(int type, int idx, int icon, const char *title, const char *sub, const char *oid, int avatar, int badge) {
  if (ngbi >= 200) return;
  GbItem *it = &gbi[ngbi++]; memset(it, 0, sizeof *it);
  it->type = type; it->idx = idx; it->icon = icon; it->avatar = avatar; it->badge = badge;
  snprintf(it->title, sizeof it->title, "%s", title ? title : "");
  snprintf(it->sub, sizeof it->sub, "%s", sub ? sub : "");
  snprintf(it->oid, sizeof it->oid, "%s", oid ? oid : "");
}
static int selectable(const GbItem *it) { return it->type != IT_HEADER && it->type != IT_EMPTY && it->type != IT_PARTYCARD; }

static void gb_build(void) {
  ngbi = 0;
  char sub[200];
  switch (gb_tab) {
    case GB_FRIENDS:
      add_item(IT_ACTION, 0, IC_ADDUSER, _("Aggiungi amici"), _("Cerca un ID online"), NULL, 0, 0);
      snprintf(sub, sizeof sub, _("Amici \xC2\xB7 %d online su %d"), friends_online_count(), S.nfriends);
      add_item(IT_HEADER, 0, -1, sub, NULL, NULL, 0, 0);
      for (int i = 0; i < S.nfriends; i++) { friend_status(&S.friends[i], sub, sizeof sub); add_item(IT_FRIEND, i, -1, S.friends[i].oid, sub, S.friends[i].oid, S.friends[i].avatar, 0); }
      if (!S.nfriends) add_item(IT_EMPTY, 0, IC_FRIENDS, _("Ancora nessun amico"), _("Aggiungi qualcuno con il suo ID online."), NULL, 0, 0);
      break;
    case GB_REQ:
      snprintf(sub, sizeof sub, _("Ricevute \xC2\xB7 %d"), S.nin);
      add_item(IT_HEADER, 0, -1, sub, NULL, NULL, 0, 0);
      for (int i = 0; i < S.nin; i++) { char t[64]; rel_time(S.in[i].when, t, sizeof t); snprintf(sub, sizeof sub, _("Vuole diventare tuo amico \xC2\xB7 %s"), t); add_item(IT_REQ_IN, i, -1, S.in[i].oid, sub, S.in[i].oid, S.in[i].avatar, 0); }
      if (!S.nin) add_item(IT_EMPTY, 0, IC_ADDUSER, _("Nessuna richiesta ricevuta"), NULL, NULL, 0, 0);
      snprintf(sub, sizeof sub, _("Inviate \xC2\xB7 %d"), S.nout);
      add_item(IT_HEADER, 0, -1, sub, NULL, NULL, 0, 0);
      for (int i = 0; i < S.nout; i++) { char t[64]; rel_time(S.out[i].when, t, sizeof t); snprintf(sub, sizeof sub, _("In attesa di risposta \xC2\xB7 %s"), t); add_item(IT_REQ_OUT, i, -1, S.out[i].oid, sub, S.out[i].oid, S.out[i].avatar, 0); }
      if (!S.nout) add_item(IT_EMPTY, 0, IC_SEND, _("Nessuna richiesta inviata"), NULL, NULL, 0, 0);
      break;
    case GB_PARTY:
      if (S.party.active) {
        add_item(IT_PARTYCARD, 0, -1, S.party.name, NULL, NULL, 0, 0);
        int muted = 0; for (int i = 0; i < S.party.nmembers; i++) if (!strcasecmp(S.party.members[i].oid, S.me)) muted = S.party.members[i].muted;
        add_item(IT_ACTION, 10, IC_CHAT, _("Chat del party"), S.npmsg ? S.pmsg[S.npmsg - 1].body : _("Scrivi al gruppo"), NULL, 0, 0);
        add_item(IT_ACTION, 11, IC_ADDUSER, _("Invita amici"), _("Solo amici"), NULL, 0, 0);
        add_item(IT_ACTION, 12, muted ? IC_MICOFF : IC_MIC, muted ? _("Attiva microfono") : _("Disattiva microfono"), NULL, NULL, 0, 0);
        add_item(IT_ACTION, 13, IC_EXIT, _("Lascia il party"), NULL, NULL, 0, 0);
        snprintf(sub, sizeof sub, _("Membri \xC2\xB7 %d"), S.party.nmembers);
        add_item(IT_HEADER, 0, -1, sub, NULL, NULL, 0, 0);
        for (int i = 0; i < S.party.nmembers; i++) {
          PartyMember *m = &S.party.members[i];
          snprintf(sub, sizeof sub, "%s%s%s", m->owner ? _("Leader") : _("Membro"), m->game_name[0] ? " \xC2\xB7 " : "", m->game_name);
          add_item(IT_PMEMBER, i, -1, m->oid, sub, m->oid, m->avatar, 0);
        }
        for (int i = 0; i < S.party.ninvited; i++) add_item(IT_PMEMBER, 100 + i, -1, S.party.invited[i].oid, _("Invitato \xC2\xB7 in attesa"), S.party.invited[i].oid, S.party.invited[i].avatar, 0);
      } else {
        add_item(IT_ACTION, 20, IC_PARTY, _("Avvia un party"), _("Crea un gruppo e invita i tuoi amici"), NULL, 0, 0);
      }
      snprintf(sub, sizeof sub, _("Inviti ricevuti \xC2\xB7 %d"), S.ninv);
      add_item(IT_HEADER, 0, -1, sub, NULL, NULL, 0, 0);
      for (int i = 0; i < S.ninv; i++) {
        snprintf(sub, sizeof sub, S.inv[i].members == 1 ? _("Invito da %s \xC2\xB7 %d membro") : _("Invito da %s \xC2\xB7 %d membri"), S.inv[i].from, S.inv[i].members);
        add_item(IT_PINVITE, i, -1, S.inv[i].name, sub, S.inv[i].from, S.inv[i].avatar, 0);
      }
      if (!S.ninv) add_item(IT_EMPTY, 0, IC_PARTY, _("Nessun invito"), NULL, NULL, 0, 0);
      break;
    case GB_MSG:
      add_item(IT_ACTION, 30, IC_PLUS, _("Nuovo messaggio"), _("Scrivi a un amico"), NULL, 0, 0);
      add_item(IT_HEADER, 0, -1, _("Conversazioni"), NULL, NULL, 0, 0);
      for (int i = 0; i < S.nconv; i++) {
        char t[64]; rel_time(S.conv[i].when, t, sizeof t);
        snprintf(sub, sizeof sub, S.conv[i].from_me ? _("Tu: %s") : "%s", S.conv[i].last);
        add_item(IT_CONV, i, -1, S.conv[i].oid, sub, S.conv[i].oid, S.conv[i].avatar, S.conv[i].unread);
      }
      if (!S.nconv) add_item(IT_EMPTY, 0, IC_CHAT, _("Nessuna conversazione"), _("I messaggi con i tuoi amici appariranno qui."), NULL, 0, 0);
      break;
  }
}

static void gb_fix_sel(int dir) {
  if (!ngbi) { gb_sel = 0; return; }
  if (gb_sel >= ngbi) gb_sel = ngbi - 1;
  if (gb_sel < 0) gb_sel = 0;
  int guard = 0;
  while (!selectable(&gbi[gb_sel]) && guard++ < ngbi) {
    gb_sel += dir ? dir : 1;
    if (gb_sel >= ngbi) { gb_sel = ngbi - 1; dir = -1; }
    if (gb_sel < 0) { gb_sel = 0; dir = 1; }
  }
}

void gb_open(int tab) {
  gb_tab = tab; gb_zone = 1; gb_sel = 0; gb_scroll = 0;
  social_load_friends(); social_load_conversations(); social_sync_now();
  gb_build(); gb_fix_sel(1);
  ov_push(OV_GAMEBASE);
}

static int item_height(const GbItem *it) {
  switch (it->type) { case IT_HEADER: return 70; case IT_EMPTY: return 150; case IT_PARTYCARD: return 262; default: return 104; }
}

static void draw_partycard(int x, int y, int w, int alpha) {
  news_art(NULL, x, y, w, 242, 24, alpha);
  draw_icon(IC_PARTY, x + 70, y + 70, 64, C_WHITE, alpha);
  draw_text_fit(font(W_MED, 34), S.party.name, x + 130, y + 40, w - 160, C_WHITE, alpha, AL_L);
  char mm[48], m[128]; snprintf(mm, sizeof mm, S.party.nmembers == 1 ? _("%d membro") : _("%d membri"), S.party.nmembers);
  snprintf(m, sizeof m, "%s \xC2\xB7 %s", mm, S.party.owner ? _("sei il leader") : _("sei nel party"));
  draw_text(font(W_REG, 24), m, x + 130, y + 88, RGB(220, 230, 255), alpha, AL_L);
  const char *vs = voice_state() == 1 ? _("Voce attiva") : voice_state() == 2 ? _("Solo ascolto: microfono non disponibile") : _("Voce in avvio...");
  draw_icon(voice_state() == 1 ? IC_MIC : IC_MICOFF, x + w - 60, y + 50, 30, C_WHITE, alpha);
  draw_text(font(W_REG, 20), vs, x + w - 84, y + 38, RGB(220, 230, 255), alpha, AL_R);
  // avatar dei membri; un anello animato segnala chi sta parlando
  for (int i = 0; i < S.party.nmembers && i < 8; i++) {
    int cx = x + 60 + i * 56, cy = y + 152;
    PartyMember *pm = &S.party.members[i];
    if (!pm->muted && voice_speaking(pm->oid)) { ring(cx, cy, 29, 4, C_OK, alpha); ring(cx, cy, 33 + (int)(4 * sinf((float)g_time * 9)), 2, C_OK, alpha / 2); }
    else if (pm->muted) { fill_circle(cx + 17, cy + 17, 11, RGB(160, 40, 50), alpha); draw_icon(IC_MICOFF, cx + 17, cy + 17, 14, C_WHITE, alpha); }
    draw_avatar(pm->oid, pm->avatar, cx, cy, 48, alpha);
  }
  // in gioco la voce la porta avanti il servizio: come si comanda da lì
  static int onion = -1; if (onion < 0) onion = strstr(hen_name(), "OnionHEN") != NULL;
  draw_text_fit(font(W_REG, 20), onion ? _("In gioco: microfono con L2+R3 › Plugin › Omega; gli altri si sentono con Omega aperta")
                                       : _("In gioco: microfono dal Telecomando; gli altri si sentono con Omega aperta"),
                x + 34, y + 200, w - 68, RGB(200, 212, 245), alpha, AL_L);
}

void gb_draw(float t) {
  gb_build();
  if (gb_zone == 1) gb_fix_sel(1);
  int w = 820, x = side_panel(t, w), a = (int)(255 * t);
  int px = x + 50, pw = w - 100;
  draw_icon(IC_FRIENDS, px + 22, 74, 44, C_TXT, a);
  draw_text(font(W_LIGHT, 44), _("Game Base"), px + 60, 46, C_WHITE, a, AL_L);
  const char *tabs[GB_NTABS] = { _("Amici"), _("Richieste"), _("Party"), _("Messaggi") };
  int badges[GB_NTABS] = { 0, S.in_req, S.ninv, S.unread_msg };
  gb_tab_anim = approach(gb_tab_anim, (float)gb_tab, 16.0f);
  int tx = px, ty = 130;
  for (int i = 0; i < GB_NTABS; i++) {
    TTF_Font *f = font(gb_tab == i ? W_MED : W_REG, 28);
    int tw = text_w(f, tabs[i]) + 40;
    if (gb_zone == 0 && gb_tab == i) fill_rrect(tx, ty, tw, 56, 28, C_WHITE, a);
    else if (gb_tab == i) fill_rrect(tx, ty, tw, 56, 28, C_WHITE, a * 18 / 100);
    draw_text(f, tabs[i], tx + 20, ty + 12, gb_zone == 0 && gb_tab == i ? RGB(12, 14, 22) : gb_tab == i ? C_WHITE : C_DIM, a, AL_L);
    if (badges[i]) draw_badge(tx + tw - 4, ty + 2, badges[i], a);
    tx += tw + 12;
  }
  int ly = 220, lh = SCREEN_H - ly - 90;
  int sel_y = 0; for (int i = 0; i < gb_sel && i < ngbi; i++) sel_y += item_height(&gbi[i]);
  float target = sel_y + 104 > lh ? (float)(sel_y + 104 - lh + 60) : 0;
  gb_scroll = approach(gb_scroll, gb_zone == 1 ? target : 0, 14.0f);
  gb_sel_anim = approach(gb_sel_anim, (float)gb_sel, 20.0f);
  SDL_Rect clip = { x, ly - 10, w, lh + 10 };
  SDL_RenderSetClipRect(R, &clip);
  int y = ly - (int)gb_scroll;
  for (int i = 0; i < ngbi; i++) {
    GbItem *it = &gbi[i];
    int ih = item_height(it);
    if (y + ih < ly - 20 || y > ly + lh) { y += ih; continue; }
    float fa = gb_zone == 1 ? clampf(1 - fabsf(gb_sel_anim - i), 0, 1) : 0;
    int foc = gb_zone == 1 && gb_sel == i;
    switch (it->type) {
      case IT_HEADER:
        draw_text(font(W_MED, 24), it->title, px + 8, y + 30, C_DIM, a, AL_L);
        break;
      case IT_EMPTY:
        fill_rrect(px, y + 10, pw, ih - 20, 20, RGB(255, 255, 255), a * 5 / 100);
        draw_icon(it->icon, px + 70, y + ih / 2, 50, C_FAINT, a);
        draw_text(font(W_MED, 27), it->title, px + 130, y + (it->sub[0] ? 40 : 56), C_DIM, a, AL_L);
        if (it->sub[0]) draw_text_fit(font(W_REG, 22), it->sub, px + 130, y + 80, pw - 160, C_FAINT, a, AL_L);
        break;
      case IT_PARTYCARD:
        draw_partycard(px, y, pw, a);
        break;
      default: {
        row_bg(px, y + 6, pw, ih - 12, fa, a);
        int cx = px + 54, cy = y + ih / 2;
        if (it->oid[0]) {
          draw_avatar(it->oid, it->avatar, cx, cy, 68, a);
          if (it->type == IT_FRIEND) { const Friend *f = &S.friends[it->idx]; if (friend_online(f)) status_dot(cx + 24, cy + 24, f->status, f->game_id[0] != 0, a); }
        } else {
          fill_circle(cx, cy, 34, foc ? C_WHITE : RGB(44, 52, 72), a);
          draw_icon(it->icon, cx, cy, 36, foc ? RGB(12, 14, 22) : C_TXT, a);
        }
        int tx2 = px + 110, tw2 = pw - 150 - (it->badge ? 50 : 0);
        draw_text_fit(font(W_MED, 28), it->title, tx2, y + (it->sub[0] ? 22 : 36), tw2, C_TXT, a, AL_L);
        if (it->sub[0]) {
          Col sc = C_DIM;
          if (it->type == IT_FRIEND && S.friends[it->idx].game_id[0]) sc = C_ACC2;
          else if (it->type == IT_FRIEND && friend_online(&S.friends[it->idx])) sc = C_OK;
          draw_text_fit(font(W_REG, 22), it->sub, tx2, y + 60, tw2, sc, a, AL_L);
        }
        if (it->badge) draw_badge(px + pw - 40, cy, it->badge, a);
        else if (it->type == IT_REQ_IN && foc) { draw_icon(IC_BTN_X, px + pw - 110, cy, 22, C_TXT, a); draw_text(font(W_REG, 20), _("Opzioni"), px + pw - 92, cy - 12, C_DIM, a, AL_L); }
        else if (it->type != IT_ACTION) draw_icon(IC_ARROW_R, px + pw - 34, cy, 24, C_FAINT, a);
        break;
      }
    }
    y += ih;
  }
  SDL_RenderSetClipRect(R, NULL);
  const int ic[] = { IC_BTN_X, IC_BTN_O };
  const char *lb[] = { _("Seleziona"), _("Indietro") };
  hints(ic, lb, 2, a);
}

// azioni della Game Base
static char act_oid[32]; static char act_party[24];
static void friend_menu_cb(int idx, void *ud) {
  (void)ud;
  const Friend *f = friend_find(act_oid);
  switch (idx) {
    case 0: profile_open(act_oid); break;
    case 1: chat_open(act_oid, f ? f->avatar : 0); break;
    case 2: if (S.party.active) social_party_invite(act_oid); else social_party_create(); break;
  }
}
static void remove_yes(int idx, void *ud) { (void)idx; (void)ud; social_friend_remove(act_oid); }
static void req_in_cb(int idx, void *ud) {
  (void)ud;
  if (idx == 0) social_friend_accept(act_oid);
  else if (idx == 1) social_friend_decline(act_oid);
  else profile_open(act_oid);
}
static void req_out_cb(int idx, void *ud) { (void)ud; if (idx == 0) social_friend_remove(act_oid); else profile_open(act_oid); }
static void pinvite_cb(int idx, void *ud) {
  (void)ud;
  if (idx == 0) social_party_join(act_party);
  else if (idx == 1) social_party_decline(act_party);
}
static void leave_yes(int idx, void *ud) { (void)idx; (void)ud; social_party_leave(); }

static char inv_names[16][40]; static int ninv_names;
static void invite_pick(int idx, void *ud) { (void)ud; if (idx >= 0 && idx < ninv_names) social_party_invite(inv_names[idx]); }
static void open_invite_menu(void) {
  static const char *items[16]; ninv_names = 0;
  for (int i = 0; i < S.nfriends && ninv_names < 16; i++) {
    int in = 0;
    for (int k = 0; k < S.party.nmembers; k++) if (!strcasecmp(S.party.members[k].oid, S.friends[i].oid)) in = 1;
    if (in) continue;
    snprintf(inv_names[ninv_names], sizeof inv_names[0], "%s", S.friends[i].oid);
    items[ninv_names] = inv_names[ninv_names]; ninv_names++;
  }
  if (!ninv_names) { set_msg(_("Nessun amico da invitare"), 0); return; }
  menu_open(_("Invita al party"), items, ninv_names, invite_pick, NULL);
}
static char msg_names[64][40]; static int nmsg_names;
static void newmsg_pick(int idx, void *ud) {
  (void)ud;
  if (idx >= 0 && idx < nmsg_names) { const Friend *f = friend_find(msg_names[idx]); chat_open(msg_names[idx], f ? f->avatar : 0); }
}
static void open_newmsg_menu(void) {
  static const char *items[16]; nmsg_names = 0;
  for (int i = 0; i < S.nfriends && nmsg_names < 16; i++) { snprintf(msg_names[nmsg_names], sizeof msg_names[0], "%s", S.friends[i].oid); items[nmsg_names] = msg_names[nmsg_names]; nmsg_names++; }
  if (!nmsg_names) { set_msg(_("Aggiungi un amico per scrivergli"), 0); return; }
  menu_open(_("Scrivi a"), items, nmsg_names, newmsg_pick, NULL);
}

static void friend_menu_dispatch(int idx, void *ud) {
  if (idx == 3) { confirm_open(_("Vuoi davvero rimuovere questo amico? Non vedrete più l'uno le attività dell'altro."), _("Rimuovi"), remove_yes, NULL); return; }
  friend_menu_cb(idx, ud);
}

static void gb_activate(void) {
  if (gb_sel < 0 || gb_sel >= ngbi) return;
  GbItem *it = &gbi[gb_sel];
  snprintf(act_oid, sizeof act_oid, "%s", it->oid);
  switch (it->type) {
    case IT_ACTION:
      switch (it->idx) {
        case 0: search_open(); break;
        case 10: chat_open_party(); break;
        case 11: open_invite_menu(); break;
        case 12: { int muted = 0; for (int i = 0; i < S.party.nmembers; i++) if (!strcasecmp(S.party.members[i].oid, S.me)) muted = S.party.members[i].muted; social_party_mute(!muted); break; }
        case 13: confirm_open(_("Vuoi lasciare il party?"), _("Lascia"), leave_yes, NULL); break;
        case 20: social_party_create(); break;
        case 30: open_newmsg_menu(); break;
      }
      break;
    case IT_FRIEND: {
      const char *it1[] = { _("Visualizza profilo"), _("Invia messaggio"), _("Invita al party"), _("Rimuovi amico") };
      const char *it2[] = { _("Visualizza profilo"), _("Invia messaggio"), _("Avvia un party"), _("Rimuovi amico") };
      menu_open(it->oid, S.party.active ? it1 : it2, 4, friend_menu_dispatch, NULL);
      break;
    }
    case IT_REQ_IN: { const char *m[] = { _("Accetta"), _("Rifiuta"), _("Visualizza profilo") }; menu_open(it->oid, m, 3, req_in_cb, NULL); break; }
    case IT_REQ_OUT: { const char *m[] = { _("Annulla richiesta"), _("Visualizza profilo") }; menu_open(it->oid, m, 2, req_out_cb, NULL); break; }
    case IT_PMEMBER: if (strcasecmp(it->oid, S.me)) profile_open(it->oid); else profile_open(S.me); break;
    case IT_PINVITE: {
      snprintf(act_party, sizeof act_party, "%s", S.inv[it->idx].party_id);
      const char *m[] = { _("Unisciti al party"), _("Rifiuta invito") };
      menu_open(S.inv[it->idx].name, m, 2, pinvite_cb, NULL);
      break;
    }
    case IT_CONV: chat_open(it->oid, it->avatar); break;
  }
}

void gb_input(int b) {
  gb_build();
  if (b == B_O || b == B_TRI) { ov_pop(); return; }
  if (b == B_L1 || b == B_R1) {
    gb_tab = (gb_tab + (b == B_R1 ? 1 : GB_NTABS - 1)) % GB_NTABS; gb_sel = 0; gb_scroll = 0;
    gb_build(); gb_fix_sel(1);
    return;
  }
  if (gb_zone == 0) {
    if (b == B_LEFT && gb_tab > 0) { gb_tab--; gb_sel = 0; }
    else if (b == B_RIGHT && gb_tab < GB_NTABS - 1) { gb_tab++; gb_sel = 0; }
    else if (b == B_DOWN || b == B_X) { gb_zone = 1; gb_build(); gb_sel = 0; gb_fix_sel(1); }
    if (gb_tab == GB_MSG) social_load_conversations();
    return;
  }
  if (b == B_UP) {
    int s = gb_sel - 1;
    while (s >= 0 && !selectable(&gbi[s])) s--;
    if (s < 0) gb_zone = 0; else gb_sel = s;
  } else if (b == B_DOWN) {
    int s = gb_sel + 1;
    while (s < ngbi && !selectable(&gbi[s])) s++;
    if (s < ngbi) gb_sel = s;
  } else if (b == B_LEFT || b == B_RIGHT) {
    gb_tab = (gb_tab + (b == B_RIGHT ? 1 : GB_NTABS - 1)) % GB_NTABS; gb_sel = 0; gb_scroll = 0; gb_build(); gb_fix_sel(1);
    if (gb_tab == GB_MSG) social_load_conversations();
  } else if (b == B_X) gb_activate();
}

// ---------------------------------------------------------------- notifiche --
static int nt_sel; static float nt_anim, nt_scroll; static int nt_marked;

void notif_draw(float t) {
  int w = 780, x = side_panel(t, w), a = (int)(255 * t);
  int px = x + 50, pw = w - 100;
  draw_icon(IC_BELL, px + 22, 74, 44, C_TXT, a);
  draw_text(font(W_LIGHT, 44), _("Notifiche"), px + 60, 46, C_WHITE, a, AL_L);
  if (!nt_marked && t > 0.9f && S.nnotif) { social_mark_notif_read(); nt_marked = 1; }
  if (nt_sel >= S.nnotif) nt_sel = S.nnotif - 1;
  if (nt_sel < 0) nt_sel = 0;
  nt_anim = approach(nt_anim, (float)nt_sel, 20.0f);
  int ly = 150, rh = 128, lh = SCREEN_H - ly - 90;
  float off = list_scroll(&nt_scroll, nt_sel, rh, lh);
  SDL_Rect clip = { x, ly - 10, w, lh + 10 };
  SDL_RenderSetClipRect(R, &clip);
  if (!S.nnotif) {
    draw_icon(IC_BELL, px + pw / 2, ly + 160, 90, C_FAINT, a);
    draw_text(font(W_MED, 30), _("Nessuna notifica"), px + pw / 2, ly + 240, C_DIM, a, AL_C);
    draw_text(font(W_REG, 24), _("Richieste, messaggi e inviti appariranno qui."), px + pw / 2, ly + 286, C_FAINT, a, AL_C);
  }
  for (int i = 0; i < S.nnotif; i++) {
    int y = ly + i * rh - (int)off;
    if (y + rh < ly - 20 || y > ly + lh) continue;
    Notif *n = &S.notif[i];
    float fa = clampf(1 - fabsf(nt_anim - i), 0, 1);
    row_bg(px, y + 6, pw, rh - 12, fa, a);
    if (!n->read) fill_circle(px + 18, y + rh / 2, 6, C_ACC2, a);
    int cx = px + 74, cy = y + rh / 2;
    if (n->actor[0]) { draw_avatar(n->actor, n->avatar, cx, cy, 70, a); fill_circle(cx + 25, cy + 25, 16, C_ACC, a); draw_icon(notif_icon(n->type), cx + 25, cy + 25, 18, C_WHITE, a); }
    else { fill_circle(cx, cy, 35, C_ACC, a); draw_icon(notif_icon(n->type), cx, cy, 36, C_WHITE, a); }
    int tx = px + 130, tw = pw - 160;
    if (!strcmp(n->type, "message")) {
      draw_text_fit(font(W_MED, 26), n->title, tx, y + 22, tw, C_TXT, a, AL_L);
      draw_text_fit(font(W_REG, 23), n->body, tx, y + 56, tw, C_DIM, a, AL_L);
    } else {
      draw_text_wrap(font(W_MED, 25), n->title, tx, y + 18, tw, 2, 31, C_TXT, a);
    }
    char tm[64]; rel_time(n->when, tm, sizeof tm);
    draw_text(font(W_REG, 20), tm, tx, y + rh - 38, C_FAINT, a, AL_L);
  }
  SDL_RenderSetClipRect(R, NULL);
  const int ic[] = { IC_BTN_X, IC_BTN_SQ, IC_BTN_O };
  const char *lb[] = { _("Apri"), _("Cancella tutte"), _("Indietro") };
  hints(ic, lb, 3, a);
}

static void clear_yes(int idx, void *ud) { (void)idx; (void)ud; social_clear_notif(); }

void notif_input(int b) {
  if (b == B_O) { ov_pop(); nt_marked = 0; return; }
  if (b == B_UP && nt_sel > 0) nt_sel--;
  else if (b == B_DOWN && nt_sel < S.nnotif - 1) nt_sel++;
  else if (b == B_SQ) { if (S.nnotif) confirm_open(_("Cancellare tutte le notifiche?"), _("Cancella"), clear_yes, NULL); }
  else if (b == B_X && S.nnotif) {
    Notif *n = &S.notif[nt_sel];
    nt_marked = 0;
    if (!strcmp(n->type, "friend_request")) { ov_pop(); gb_open(1); }
    else if (!strcmp(n->type, "message")) { const Friend *f = friend_find(n->ref); ov_pop(); chat_open(n->ref, f ? f->avatar : n->avatar); }
    else if (!strcmp(n->type, "party_invite")) { ov_pop(); gb_open(2); }
    else if (!strcmp(n->type, "post_like") || !strcmp(n->type, "post_comment")) { ov_pop(); community_open_post(n->ref); }
    else if (!strcmp(n->type, "game_invite")) {
      int k = -1; for (int i = 0; i < napps; i++) if (!strcmp(apps[i].tid, n->ref)) k = i;
      ov_pop();
      if (k >= 0) { ov_clear(); launch_app(k); } else set_msg(_("Questo gioco non è installato su questa console"), 1);
    }
    else if (n->actor[0]) { ov_pop(); profile_open(n->actor); }
  }
}

// ------------------------------------------------------------------ profilo --
static int pr_sel; static float pr_anim;
enum { PA_MSG, PA_PARTY, PA_REMOVE, PA_ADD, PA_ACCEPT, PA_DECLINE, PA_CANCEL, PA_BIO, PA_AVATAR, PA_COVER, PA_STATUS, PA_MORE, PA_UNBLOCK, PA_TROPHIES };
#define PR_MAX_ACTIONS 6
static int pr_actions(int *out, const char **lbl, int *icons) {
  int n = 0;
  #define ADD(id, l, ic) do { out[n] = id; lbl[n] = l; icons[n] = ic; n++; } while (0)
  // i trofei: i propri sempre, quelli degli altri se li mostrano
  int trophies = PR.loaded && !PR.blocked && (!strcmp(PR.relation, "self") || !PR.trophies.hidden);
  if (!strcmp(PR.relation, "self")) { ADD(PA_STATUS, _("Stato"), IC_CHECK); ADD(PA_TROPHIES, _("Trofei"), IC_STAR); ADD(PA_BIO, _("Modifica bio"), IC_NEWS); ADD(PA_AVATAR, _("Avatar"), IC_USER); ADD(PA_COVER, _("Copertina"), IC_ALBUM); }
  else if (PR.blocked) { ADD(PA_UNBLOCK, _("Sblocca"), IC_CHECK); }
  else if (!strcmp(PR.relation, "friend")) { ADD(PA_MSG, _("Messaggio"), IC_CHAT); ADD(PA_PARTY, S.party.active ? _("Invita al party") : _("Avvia party"), IC_PARTY); ADD(PA_REMOVE, _("Rimuovi"), IC_CLOSE); ADD(PA_MORE, P_("profilo", "Altro"), IC_MORE); }
  else if (!strcmp(PR.relation, "incoming")) { ADD(PA_ACCEPT, _("Accetta richiesta"), IC_CHECK); ADD(PA_DECLINE, _("Rifiuta"), IC_CLOSE); }
  else if (!strcmp(PR.relation, "outgoing")) { ADD(PA_CANCEL, _("Annulla richiesta"), IC_CLOSE); }
  else if (PR.loaded) { ADD(PA_ADD, _("Aggiungi amico"), IC_ADDUSER); }
  if (trophies && strcmp(PR.relation, "self")) ADD(PA_TROPHIES, _("Trofei"), IC_STAR);
  if (PR.loaded && !PR.blocked && strcmp(PR.relation, "self") && strcmp(PR.relation, "friend")) ADD(PA_MORE, P_("profilo", "Altro"), IC_MORE);
  #undef ADD
  return n;
}

void profile_draw(float t) {
  backdrop(t, 160);
  int w = 1560, h = 920, x = SCREEN_W / 2 - w / 2, y = SCREEN_H / 2 - h / 2 + (int)((1 - ease_out(t)) * 60);
  int a = (int)(255 * t);
  shadow_rrect(x, y, w, h, 34, 50, a * 70 / 100);
  fill_rrect(x, y, w, h, 34, RGB(18, 21, 32), a);
  News fake; memset(&fake, 0, sizeof fake); fake.color = PR.avatar % 8;
  news_art(&fake, x, y, w, 260, -34, a);
  // copertina personalizzata (foto o video in loop) sopra la grafica generata
  if (PR.cover_media[0]) draw_media_frames(PR.cover_media, PR.cover_frames, x, y, w, 260, 1170, 195, -26, 1, a);
  int ax = x + 170, ay = y + 260;
  fill_circle(ax, ay, 118, RGB(18, 21, 32), a);
  draw_avatar(PR.oid, PR.avatar, ax, ay, 220, a);
  int online = PR.status[0] && strcmp(PR.status, "offline");
  if (online) { fill_circle(ax + 78, ay + 78, 20, RGB(18, 21, 32), a); fill_circle(ax + 78, ay + 78, 14, PR.game_name[0] ? C_ACC2 : C_OK, a); }
  draw_text(font(W_LIGHT, 60), PR.oid, x + 320, y + 270, C_WHITE, a, AL_L);
  char st[200];
  if (PR.loading && !PR.loaded) snprintf(st, sizeof st, "%s", _("Caricamento..."));
  else if (PR.game_name[0]) { char tm[64]; play_time(PR.started, tm, sizeof tm); snprintf(st, sizeof st, _("Sta giocando a %s \xC2\xB7 %s"), PR.game_name, tm); }
  else snprintf(st, sizeof st, "%s", online ? _("Online") : _("Offline"));
  int sw = draw_text(font(W_REG, 28), st, x + 324, y + 346, online ? (PR.game_name[0] ? C_ACC2 : C_OK) : C_DIM, a, AL_L);
  if (PR.status_msg[0]) { char q[90]; snprintf(q, sizeof q, "\xE2\x80\x9C%s\xE2\x80\x9D", PR.status_msg); draw_text_fit(font(W_REG, 26), q, x + 350 + sw, y + 348, w - 760 - sw, C_TXT, a, AL_L); }
  const char *rel = !strcmp(PR.relation, "friend") ? _("Amico") : !strcmp(PR.relation, "self") ? _("Sei tu") : !strcmp(PR.relation, "incoming") ? _("Ti ha inviato una richiesta") : !strcmp(PR.relation, "outgoing") ? _("Richiesta inviata") : PR.loaded ? _("Non siete amici") : "";
  if (rel[0]) {
    int rw = text_w(font(W_MED, 22), rel) + 40;
    fill_rrect(x + w - rw - 60, y + 286, rw, 44, 22, C_WHITE, a * 14 / 100);
    draw_text(font(W_MED, 22), rel, x + w - rw / 2 - 60, y + 296, C_TXT, a, AL_C);
  }
  int ids[PR_MAX_ACTIONS], icons[PR_MAX_ACTIONS]; const char *lbl[PR_MAX_ACTIONS];
  int n = pr_actions(ids, lbl, icons);
  if (pr_sel >= n) pr_sel = n ? n - 1 : 0;
  pr_anim = approach(pr_anim, (float)pr_sel, 18.0f);
  int bx = x + 60, by = y + 420;
  for (int i = 0; i < n; i++) {
    float fa = clampf(1 - fabsf(pr_anim - i), 0, 1);
    int bw = pill(bx, by, 76, lbl[i], icons[i], pr_sel == i, fa, a);
    if (pr_sel == i) focus_ring(bx, by, bw, 76, 38, 0.5f + 0.5f * sinf((float)g_time * 3.2f), a);
    bx += bw + 20;
  }
  // colonne: info, giochi, attività
  int cy = y + 540;
  int c1 = x + 60, c2 = x + 560, c3 = x + 1060;
  draw_text(font(W_MED, 26), _("Informazioni"), c1, cy, C_DIM, a, AL_L);
  draw_text_wrap(font(W_REG, 27), PR.about[0] ? PR.about : (strcmp(PR.relation, "self") ? _("Nessuna bio.") : _("Aggiungi una bio con \"Modifica bio\".")), c1, cy + 46, 440, 4, 36, PR.about[0] ? C_TXT : C_FAINT, a);
  char line[160]; snprintf(line, sizeof line, PR.friends_count == 1 ? _("%d amico") : _("%d amici"), PR.friends_count);
  draw_icon(IC_FRIENDS, c1 + 16, cy + 222, 28, C_DIM, a);
  draw_text(font(W_REG, 25), line, c1 + 44, cy + 206, C_TXT, a, AL_L);
  if (PR.mutual_friends && strcmp(PR.relation, "self")) {
    char ref[64]; snprintf(ref, sizeof ref, _("%d amici"), 100);   // larghezza di riferimento della riga sopra
    snprintf(line, sizeof line, _("%d in comune"), PR.mutual_friends);
    int lw = text_w(font(W_REG, 25), ref) + 70;
    for (int i = 0; i < PR.nmutual && i < 3; i++) draw_avatar(PR.mutual[i], 0, c1 + lw + i * 26, cy + 222, 34, a);
    draw_text(font(W_REG, 22), line, c1 + lw + 3 * 26 + 10, cy + 210, C_DIM, a, AL_L);
  }
  if (PR.total_seconds > 0) {
    snprintf(line, sizeof line, _("%ld ore di gioco su Omega"), (PR.total_seconds + 1800) / 3600);
    draw_icon(IC_CLOCK, c1 + 16, cy + 314, 28, C_DIM, a);
    draw_text(font(W_REG, 25), line, c1 + 44, cy + 298, C_TXT, a, AL_L);
  }
  if (PR.loaded && !PR.blocked && !PR.trophies.hidden && PR.trophies.sets)
    trophy_counts(c1 + 2, cy + 342, PR.trophies.p, PR.trophies.g, PR.trophies.s, PR.trophies.b, a);
  if (PR.created[0]) {
    int Y, M, D;
    if (sscanf(PR.created, "%d-%d-%d", &Y, &M, &D) == 3 && M >= 1 && M <= 12) {
      char dt[64]; date_short(dt, sizeof dt, D, M - 1, Y);
      snprintf(line, sizeof line, _("Su Omega dal %s"), dt);
      draw_icon(IC_STAR, c1 + 16, cy + 268, 28, C_DIM, a);
      draw_text(font(W_REG, 25), line, c1 + 44, cy + 252, C_TXT, a, AL_L);
    }
  }
  draw_text(font(W_MED, 26), _("Giocati di recente"), c2, cy, C_DIM, a, AL_L);
  if (!PR.ngames) draw_text(font(W_REG, 24), strcmp(PR.relation, "friend") && strcmp(PR.relation, "self") ? _("Visibile solo agli amici") : _("Nessun gioco ancora"), c2, cy + 50, C_FAINT, a, AL_L);
  for (int i = 0; i < PR.ngames && i < 5; i++) {
    int yy = cy + 50 + i * 62;
    AppEntry *ap = NULL; for (int k = 0; k < napps; k++) if (!strcmp(apps[k].tid, PR.games[i].game_id)) ap = &apps[k];
    if (ap && ap->tex) draw_tex(ap->tex, c2, yy, 50, 50, a); else { fill_rrect(c2, yy, 50, 50, 10, RGB(40, 48, 70), a); draw_icon(IC_GAMEPAD, c2 + 25, yy + 25, 28, C_DIM, a); }
    draw_text_fit(font(W_MED, 24), PR.games[i].game_name[0] ? PR.games[i].game_name : PR.games[i].game_id, c2 + 66, yy, 360, C_TXT, a, AL_L);
    char tm[64]; rel_time(PR.games[i].last, tm, sizeof tm);
    draw_text(font(W_REG, 20), tm, c2 + 66, yy + 28, C_FAINT, a, AL_L);
  }
  draw_text(font(W_MED, 26), _("Attività"), c3, cy, C_DIM, a, AL_L);
  if (!PR.nrecent) draw_text(font(W_REG, 24), _("Nessuna attività"), c3, cy + 50, C_FAINT, a, AL_L);
  for (int i = 0; i < PR.nrecent && i < 5; i++) {
    int yy = cy + 50 + i * 62;
    Activity *ac = &PR.recent[i];
    char l[160];
    if (!strcmp(ac->type, "game_start")) snprintf(l, sizeof l, _("Ha giocato a %s"), ac->game_name);
    else if (!strcmp(ac->type, "online")) snprintf(l, sizeof l, "%s", _("È stato online"));
    else snprintf(l, sizeof l, "%s", ac->detail);
    fill_circle(c3 + 10, yy + 16, 5, C_ACC2, a);
    draw_text_fit(font(W_REG, 24), l, c3 + 28, yy, 420, C_TXT, a, AL_L);
    char tm[64]; rel_time(ac->when, tm, sizeof tm);
    draw_text(font(W_REG, 20), tm, c3 + 28, yy + 28, C_FAINT, a, AL_L);
  }
  if (PR.loading) draw_spinner(x + w - 60, y + 60, 14, a);
}

static void remove_from_profile(int idx, void *ud) { (void)idx; (void)ud; social_friend_remove(PR.oid); }
static void block_yes(int idx, void *ud) { (void)idx; (void)ud; social_block(PR.oid, 1); }
static const char *USER_REASONS[] = { "spam", "molestie", "contenuto_offensivo", "impersonificazione", "altro" };
static void report_user_pick(int idx, void *ud) { (void)ud; if (idx >= 0 && idx < 5) social_report_user(PR.oid, USER_REASONS[idx]); }
static void more_pick(int idx, void *ud) {
  (void)ud;
  if (idx == 0) confirm_open(_("Bloccarlo? Non potrà più scriverti, invitarti né vedere i tuoi post, e smetterete di essere amici."), _("Blocca"), block_yes, NULL);
  else if (idx == 1) { const char *r[] = { _("Spam"), _("Molestie"), _("Contenuto offensivo"), _("Si spaccia per un altro"), _("Altro") }; menu_open(_("Segnala utente"), r, 5, report_user_pick, NULL); }
}

void profile_input(int b) {
  int ids[PR_MAX_ACTIONS], icons[PR_MAX_ACTIONS]; const char *lbl[PR_MAX_ACTIONS];
  int n = pr_actions(ids, lbl, icons);
  if (b == B_O) { ov_pop(); return; }
  if (b == B_LEFT && pr_sel > 0) pr_sel--;
  else if (b == B_RIGHT && pr_sel < n - 1) pr_sel++;
  else if (b == B_X && n) {
    switch (ids[pr_sel]) {
      case PA_MSG: chat_open(PR.oid, PR.avatar); break;
      case PA_PARTY: if (S.party.active) social_party_invite(PR.oid); else { social_party_create(); set_msg(_("Party creato: ora invita dal profilo"), 0); } break;
      case PA_REMOVE: confirm_open(_("Vuoi davvero rimuovere questo amico?"), _("Rimuovi"), remove_from_profile, NULL); break;
      case PA_ADD: social_friend_request(PR.oid); snprintf(PR.relation, sizeof PR.relation, "outgoing"); break;
      case PA_ACCEPT: social_friend_accept(PR.oid); break;
      case PA_DECLINE: social_friend_decline(PR.oid); break;
      case PA_CANCEL: social_friend_remove(PR.oid); snprintf(PR.relation, sizeof PR.relation, "none"); break;
      case PA_BIO: { char bio[168]; snprintf(bio, sizeof bio, "%s", PR.about); if (edit_text(_("La tua bio"), bio, sizeof bio, 0)) { snprintf(PR.about, sizeof PR.about, "%s", bio); social_profile_update(bio, 0); } break; }
      case PA_AVATAR: ov_push(OV_AVATAR); break;
      case PA_COVER: gallery_open(1); break;
      case PA_STATUS: status_menu(); break;
      case PA_UNBLOCK: social_block(PR.oid, 0); break;
      case PA_TROPHIES: trophies_open(PR.oid); break;
      case PA_MORE: { const char *m[] = { _("Blocca"), _("Segnala") }; menu_open(PR.oid, m, 2, more_pick, NULL); break; }
    }
  }
}

// ------------------------------------------------------------ scelta avatar --
// Quattro righe da 8 personaggi illustrati (0-31), una riga da 16 colori
// (32-47), poi il tasto per una foto o un video personale (48).
#define AV_CHARS AV_ART_COUNT
#define AV_COLORS 16
#define AV_CUSTOM (AV_CHARS + AV_COLORS)
static int av_sel = -1;
static int av_index(int sel) { return sel < AV_CHARS ? AV_ART_FIRST + sel : sel - AV_CHARS; }
void avatar_draw(float t) {
  backdrop(t, 140);
  if (av_sel < 0) { av_sel = AV_CUSTOM; for (int i = 0; i < AV_CUSTOM; i++) if (av_index(i) == S.my_avatar) av_sel = i; }
  int w = 1320, h = 940, x = SCREEN_W / 2 - w / 2, y = SCREEN_H / 2 - h / 2 + (int)((1 - ease_out(t)) * 40);
  int a = (int)(255 * t);
  shadow_rrect(x, y, w, h, 30, 40, a * 70 / 100);
  fill_rrect(x, y, w, h, 30, C_PANEL, a);
  draw_text(font(W_LIGHT, 44), _("Scegli il tuo avatar"), x + 60, y + 36, C_WHITE, a, AL_L);
  draw_text(font(W_MED, 24), _("Personaggi"), x + 64, y + 104, C_DIM, a, AL_L);
  draw_text(font(W_MED, 24), _("Colori"), x + 64, y + 640, C_DIM, a, AL_L);
  float pulse = 0.5f + 0.5f * sinf((float)g_time * 4);
  int has_custom = media_of(S.me, NULL) != NULL;
  for (int i = 0; i < AV_CUSTOM; i++) {
    int chr = i < AV_CHARS, foc = av_sel == i, idx = av_index(i);
    int cx = chr ? x + 135 + (i % 8) * 150 : x + 90 + (i - AV_CHARS) * 76;
    int cy = chr ? y + 196 + (i / 8) * 122 : y + 718;
    int sz = chr ? (foc ? 112 : 98) : (foc ? 64 : 54);
    if (foc) ring(cx, cy, sz / 2 + 8, 4, C_WHITE, (int)(a * (0.7f + 0.3f * pulse)));
    // oid vuoto: draw_avatar altrimenti mostrerebbe la foto personalizzata
    if (chr) { char none[2] = ""; draw_avatar(none, idx, cx, cy, sz, a); }
    else draw_avatar_color(S.me, idx, cx, cy, sz, a);
    if (idx == S.my_avatar && !has_custom) {
      int o = chr ? 38 : 20, r = chr ? 16 : 11;
      fill_circle(cx + o, cy + o, r, C_OK, a); draw_icon(IC_CHECK, cx + o, cy + o, r * 13 / 10, C_WHITE, a);
    }
  }
  int by = y + h - 120, bw = 560, bx = x + w / 2 - bw / 2, foc = av_sel == AV_CUSTOM;
  if (foc) shadow_rrect(bx, by, bw, 80, 40, 16, a / 2);
  fill_rrect(bx, by, bw, 80, 40, foc ? C_WHITE : RGB(48, 54, 72), a);
  draw_icon(IC_PLAY, bx + 60, by + 40, 30, foc ? RGB(12, 14, 22) : C_TXT, a);
  draw_text(font(W_MED, 28), has_custom ? _("Cambia foto o video personale") : _("Usa una tua foto o un video"), bx + 100, by + 22, foc ? RGB(12, 14, 22) : C_TXT, a, AL_L);
}
void avatar_input(int b) {
  if (b == B_O) { av_sel = -1; ov_pop(); return; }
  if (av_sel == AV_CUSTOM) {
    if (b == B_UP) av_sel = AV_CHARS + AV_COLORS / 2;
    else if (b == B_X) { av_sel = -1; ov_pop(); gallery_open(0); }
    return;
  }
  if (av_sel >= AV_CHARS) {                       // riga dei colori
    int c = av_sel - AV_CHARS;
    if (b == B_LEFT && c > 0) av_sel--;
    else if (b == B_RIGHT && c < AV_COLORS - 1) av_sel++;
    else if (b == B_UP) av_sel = AV_CHARS - 8 + c / 2;
    else if (b == B_DOWN) av_sel = AV_CUSTOM;
  } else {                                        // personaggi, 8 per riga
    if (b == B_LEFT && av_sel % 8 > 0) av_sel--;
    else if (b == B_RIGHT && av_sel % 8 < 7) av_sel++;
    else if (b == B_UP && av_sel >= 8) av_sel -= 8;
    else if (b == B_DOWN) av_sel = av_sel + 8 < AV_CHARS ? av_sel + 8 : AV_CHARS + (av_sel % 8) * 2;
  }
  if (b == B_X) {
    int idx = av_index(av_sel);
    // scegliere un avatar Omega toglie la foto o il video personale
    if (media_of(S.me, NULL)) { net_req(HTTP_POST, OMEGA_API "/media/clear?kind=avatar", "{}", NULL, NULL); media_note(S.me, "", 0); }
    social_profile_update(NULL, idx); PR.avatar = idx; av_sel = -1; ov_pop();
  }
}

// --------------------------------------------------------------------- chat --
void chat_draw(float t) {
  int w = 980, x = side_panel(t, w), a = (int)(255 * t);
  int px = x + 50, pw = w - 100;
  if (CH.party) {
    fill_circle(px + 40, 80, 40, C_ACC, a); draw_icon(IC_PARTY, px + 40, 80, 44, C_WHITE, a);
    draw_text_fit(font(W_MED, 34), CH.oid, px + 100, 48, pw - 120, C_WHITE, a, AL_L);
    char m[128]; snprintf(m, sizeof m, _("Chat del party \xC2\xB7 %d membri"), S.party.nmembers);
    draw_text(font(W_REG, 23), m, px + 100, 92, C_DIM, a, AL_L);
  } else {
    draw_avatar(CH.oid, CH.avatar, px + 40, 80, 80, a);
    const Friend *f = friend_find(CH.oid);
    if (f && friend_online(f)) status_dot(px + 68, 108, f->status, f->game_id[0] != 0, a);
    draw_text(font(W_MED, 34), CH.oid, px + 100, 48, C_WHITE, a, AL_L);
    char st[160]; snprintf(st, sizeof st, "%s", _("Offline")); if (f) friend_status(f, st, sizeof st);
    draw_text_fit(font(W_REG, 23), st, px + 100, 92, pw - 120, f && friend_online(f) ? C_OK : C_DIM, a, AL_L);
  }
  fill_rect(px, 150, pw, 1, RGB(70, 84, 120), a / 2);
  // messaggi disegnati dal basso verso l'alto
  int top = 170, bottom = SCREEN_H - 190;
  CH.scroll = approach(CH.scroll, CH.scroll_t, 14.0f);
  SDL_Rect clip = { x, top, w, bottom - top };
  SDL_RenderSetClipRect(R, &clip);
  TTF_Font *bf = font(W_REG, 26);
  int y = bottom - 30 + (int)CH.scroll;
  if (CH.loading && !CH.nmsg) draw_spinner(px + pw / 2, (top + bottom) / 2, 18, a);
  if (!CH.loading && !CH.nmsg) {
    draw_icon(IC_CHAT, px + pw / 2, (top + bottom) / 2 - 40, 80, C_FAINT, a);
    draw_text(font(W_REG, 26), _("Nessun messaggio: scrivi il primo!"), px + pw / 2, (top + bottom) / 2 + 20, C_DIM, a, AL_C);
  }
  int maxbw = pw * 72 / 100;
  for (int i = CH.nmsg - 1; i >= 0; i--) {
    Msg *m = &CH.msg[i];
    if (m->system) {
      y -= 54;
      if (y < bottom && y > top - 60) draw_text_fit(font(W_REG, 21), m->body + 3, px + pw / 2, y + 12, pw - 40, C_FAINT, a, AL_C);
      continue;
    }
    int tw = text_w(bf, m->body);
    int lines = tw / (maxbw - 48) + 1; if (lines > 8) lines = 8;
    int bw = lines > 1 ? maxbw : tw + 48; if (bw < 90) bw = 90;
    int show_name = CH.party && !m->mine && (i == 0 || strcmp(CH.msg[i - 1].sender, m->sender) || CH.msg[i - 1].system);
    int bh = lines * 34 + 30 + (show_name ? 30 : 0);
    y -= bh + 14;
    if (y > bottom || y + bh < top - 40) continue;
    int bx = m->mine ? px + pw - bw : px + (CH.party ? 60 : 0);
    if (CH.party && !m->mine && show_name) draw_avatar(m->sender, m->avatar, px + 24, y + 26, 44, a);
    fill_rrect(bx, y, bw, bh, 24, m->mine ? C_ACC : RGB(42, 48, 66), a);
    int ty = y + 14;
    if (show_name) { draw_text(font(W_MED, 21), m->sender, bx + 24, ty, avatar_col(m->avatar), a, AL_L); ty += 30; }
    draw_text_wrap(bf, m->body, bx + 24, ty, bw - 48, 8, 34, C_WHITE, !strcmp(m->id, "pending") ? a * 70 / 100 : a);
    char tm[64]; rel_time(m->when, tm, sizeof tm);
    if (i == CH.nmsg - 1 && tm[0]) draw_text(font(W_REG, 18), tm, m->mine ? bx + bw : bx, y + bh + 4, C_FAINT, a, m->mine ? AL_R : AL_L);
  }
  int content_top = y;
  SDL_RenderSetClipRect(R, NULL);
  float maxs = (float)(top - content_top); if (maxs < 0) maxs = 0;
  if (CH.scroll_t > maxs + 20) CH.scroll_t = maxs + 20;
  int iy = SCREEN_H - 160;
  fill_rrect(px, iy, pw, 84, 42, RGB(36, 42, 60), a);
  draw_icon(IC_BTN_X, px + 44, iy + 42, 26, C_TXT, a);
  draw_text(font(W_REG, 26), _("Scrivi un messaggio..."), px + 80, iy + 26, C_DIM, a, AL_L);
  fill_circle(px + pw - 42, iy + 42, 30, C_ACC, a);
  draw_icon(IC_SEND, px + pw - 40, iy + 42, 30, C_WHITE, a);
  const int ic[] = { IC_BTN_X, IC_BTN_O };
  const char *lb[] = { _("Scrivi"), _("Indietro") };
  hints(ic, lb, 2, a);
}

void chat_input(int b) {
  if (b == B_O) { ov_pop(); social_load_conversations(); return; }
  if (b == B_UP) CH.scroll_t += 120;
  else if (b == B_DOWN) { CH.scroll_t -= 120; if (CH.scroll_t < 0) CH.scroll_t = 0; }
  else if (b == B_X) {
    char txt[500] = "";
    if (edit_text(CH.party ? _("Messaggio al party") : _("Messaggio"), txt, sizeof txt, 0) && txt[0]) chat_send(txt);
  }
}

// ------------------------------------------------------------------ ricerca --
static char sr_q[64]; static int sr_sel; static float sr_anim, sr_scroll;
void search_open(void) {
  sr_q[0] = 0; sr_sel = 0; S.nusers = 0;
  social_search("");
  ov_push(OV_SEARCH);
}

static const char *rel_label(const char *r) {
  if (!strcmp(r, "friend")) return _("Amico");
  if (!strcmp(r, "outgoing")) return _("Richiesta inviata");
  if (!strcmp(r, "incoming")) return _("Ti ha chiesto l'amicizia");
  return "";
}

void search_draw(float t) {
  backdrop(t, 150);
  int w = 1100, h = 900, x = SCREEN_W / 2 - w / 2, y = SCREEN_H / 2 - h / 2 + (int)((1 - ease_out(t)) * 50);
  int a = (int)(255 * t);
  shadow_rrect(x, y, w, h, 34, 50, a * 70 / 100);
  fill_rrect(x, y, w, h, 34, C_PANEL, a);
  draw_text(font(W_LIGHT, 46), _("Aggiungi amici"), x + 60, y + 44, C_WHITE, a, AL_L);
  int fy = y + 130, focf = sr_sel == 0;
  fill_rrect(x + 60, fy, w - 120, 90, 45, focf ? RGB(52, 60, 84) : RGB(36, 42, 60), a);
  if (focf) stroke_rrect(x + 56, fy - 4, w - 112, 98, 49, 3, C_WHITE, a);
  draw_icon(IC_SEARCH, x + 112, fy + 45, 36, C_TXT, a);
  draw_text(font(sr_q[0] ? W_REG : W_LIGHT, 30), sr_q[0] ? sr_q : _("Cerca per ID online"), x + 150, fy + 26, sr_q[0] ? C_TXT : C_DIM, a, AL_L);
  if (S.users_loading) draw_spinner(x + w - 110, fy + 45, 14, a);
  draw_text(font(W_MED, 24), sr_q[0] ? _("Risultati") : _("Suggeriti"), x + 64, fy + 120, C_DIM, a, AL_L);
  int ly = fy + 170, rh = 104, lh = y + h - 40 - ly;
  sr_anim = approach(sr_anim, (float)(sr_sel - 1), 20.0f);
  float off = list_scroll(&sr_scroll, sr_sel > 0 ? sr_sel - 1 : 0, rh, lh);
  SDL_Rect clip = { x, ly - 8, w, lh + 8 };
  SDL_RenderSetClipRect(R, &clip);
  if (!S.users_loading && !S.nusers) draw_text(font(W_REG, 26), sr_q[0] ? _("Nessun utente trovato") : _("Nessun suggerimento"), x + w / 2, ly + 60, C_FAINT, a, AL_C);
  for (int i = 0; i < S.nusers; i++) {
    int ry = ly + i * rh - (int)off;
    if (ry + rh < ly - 20 || ry > ly + lh) continue;
    UserRef *u = &S.users[i];
    float fa = clampf(1 - fabsf(sr_anim - i), 0, 1);
    row_bg(x + 60, ry, w - 120, rh - 12, fa, a);
    draw_avatar(u->oid, u->avatar, x + 116, ry + (rh - 12) / 2, 66, a);
    draw_text(font(W_MED, 28), u->oid, x + 170, ry + 18, C_TXT, a, AL_L);
    const char *rl = rel_label(u->relation);
    if (rl[0]) draw_text(font(W_REG, 22), rl, x + 170, ry + 54, !strcmp(u->relation, "friend") ? C_OK : C_DIM, a, AL_L);
    const char *act = !strcmp(u->relation, "none") ? _("Aggiungi") : !strcmp(u->relation, "incoming") ? _("Accetta") : !strcmp(u->relation, "friend") ? _("Messaggio") : _("Annulla");
    int ic = !strcmp(u->relation, "none") ? IC_ADDUSER : !strcmp(u->relation, "incoming") ? IC_CHECK : !strcmp(u->relation, "friend") ? IC_CHAT : IC_CLOSE;
    int pw2 = text_w(font(W_MED, 25), act) + 90;
    pill(x + w - 80 - pw2, ry + 18, 56, act, ic, sr_sel == i + 1, sr_sel == i + 1 ? 1.0f : 0.0f, a);
  }
  SDL_RenderSetClipRect(R, NULL);
  const int ic[] = { IC_BTN_X, IC_BTN_TRI, IC_BTN_O };
  const char *lb[] = { _("Seleziona"), _("Profilo"), _("Indietro") };
  hints(ic, lb, 3, a);
}

void search_input(int b) {
  if (b == B_O) { ov_pop(); return; }
  if (b == B_UP && sr_sel > 0) sr_sel--;
  else if (b == B_DOWN && sr_sel < S.nusers) sr_sel++;
  else if (b == B_TRI && sr_sel > 0) profile_open(S.users[sr_sel - 1].oid);
  else if (b == B_X) {
    if (sr_sel == 0) {
      char q[64]; snprintf(q, sizeof q, "%s", sr_q);
      if (edit_text(_("Cerca ID online"), q, sizeof q, 0)) { snprintf(sr_q, sizeof sr_q, "%s", q); S.nusers = 0; social_search(sr_q); sr_sel = 0; }
    } else {
      UserRef *u = &S.users[sr_sel - 1];
      if (!strcmp(u->relation, "none")) { social_friend_request(u->oid); snprintf(u->relation, sizeof u->relation, "outgoing"); }
      else if (!strcmp(u->relation, "incoming")) { social_friend_accept(u->oid); snprintf(u->relation, sizeof u->relation, "friend"); }
      else if (!strcmp(u->relation, "friend")) chat_open(u->oid, u->avatar);
      else if (!strcmp(u->relation, "outgoing")) { social_friend_remove(u->oid); snprintf(u->relation, sizeof u->relation, "none"); }
    }
  }
}

// ------------------------------------------------------------- impostazioni --
static int st_sel; static float st_anim;
static void st_logout_yes(int idx, void *ud) { (void)idx; (void)ud; do_logout(); }
static void st_quit_yes(int idx, void *ud) { (void)idx; (void)ud; app_quit(); }
static void audio_pick(int idx, void *ud);
static void open_audio_menu(void) {
  static char l0[96], l1[96], l2[96];
  static const char *items[3] = { l0, l1, l2 };
  snprintf(l0, sizeof l0, "%s", audio_music_on() ? _("Musica di sottofondo: attiva") : _("Musica di sottofondo: spenta"));
  snprintf(l1, sizeof l1, "%s", audio_sfx_on() ? _("Effetti sonori: attivi") : _("Effetti sonori: spenti"));
  snprintf(l2, sizeof l2, _("Volume musica: %d%%"), audio_music_level());
  menu_open(_("Audio"), items, 3, audio_pick, NULL);
}
static void audio_pick(int idx, void *ud) {
  (void)ud;
  if (idx == 0) audio_set(!audio_music_on(), -1, -1);
  else if (idx == 1) audio_set(-1, !audio_sfx_on(), -1);
  else { int l = audio_music_level(); l = l >= 80 ? 30 : l >= 50 ? 80 : 55; audio_set(-1, -1, l); }
  open_audio_menu();   // il menu resta aperto con i valori aggiornati
}
static void theme_pick(int idx, void *ud) { (void)ud; theme_apply(idx, 1); char m[96]; snprintf(m, sizeof m, _("Tema: %s"), theme_name(idx)); set_msg(m, 0); }

// Lingua: "Automatica" segue la console; la scelta vale subito e si salva.
static void lang_pick(int idx, void *ud) {
  (void)ud;
  i18n_set(idx <= 0 ? "auto" : i18n_lang_code(idx - 1));
  social_sync_now();     // i testi che arrivano dal server tornano nella lingua nuova
}
static void open_lang_menu(void) {
  static char autol[96];
  static const char *items[32];
  int n = i18n_count(), sel = 0;
  snprintf(autol, sizeof autol, "%s", _("Automatica (console)"));
  items[0] = autol;
  for (int i = 0; i < n && i < 31; i++) {
    items[i + 1] = i18n_lang_name(i18n_lang_code(i));
    if (!strcmp(i18n_pref(), i18n_lang_code(i))) sel = i + 1;
  }
  menu_open(_("Lingua"), items, n + 1, lang_pick, NULL);
  menu_select(sel);
}

// Impostazioni in sezioni: a sinistra Account, Aspetto, Home e giochi, Sistema,
// Aiuto; a destra le voci della sezione con il valore attuale. Prima era un
// unico elenco di 15 righe: ora ogni cosa sta dove la si cerca.
enum {
  SO_PROFILE, SO_AVATAR, SO_BIO, SO_PRIVACY, SO_LOGOUT,
  SO_CUSTOM, SO_STYLE, SO_THEME, SO_LANG, SO_AUDIO,
  SO_HOME, SO_HIDDEN, SO_PATHS, SO_PKGS, SO_AUTOMOUNT,
  SO_SYSTEM, SO_REMOTE, SO_SERVER, SO_FILES,
  SO_WHY, SO_FEEDBACK, SO_ABOUT, SO_QUIT, N_OPT
};
static const struct { const char *name; int ic; int first, n; } ST_SECT[] = {
  { N_("Account"), IC_USER, SO_PROFILE, 5 }, { N_("Aspetto"), IC_BRUSH, SO_CUSTOM, 5 }, { N_("Home e giochi"), IC_HOME, SO_HOME, 5 },
  { N_("Sistema"), IC_GEAR, SO_SYSTEM, 4 }, { N_("Aiuto"), IC_IDEA, SO_WHY, 4 },
};
#define ST_NSECT (int)(sizeof ST_SECT / sizeof *ST_SECT)
static int st_sect, st_col; static float st_sect_anim;   // st_col: 0 sezioni, 1 voci
static const char *st_label(int o) {
  static const char *L[N_OPT] = { N_("Il mio profilo"), N_("Cambia avatar"), N_("Modifica bio"), N_("Privacy"), N_("Esci dall'account"),
    N_("Personalizza"), N_("Modalità del menu"), N_("Tema"), N_("Lingua"), N_("Audio"),
    N_("Omega come Home"), N_("App nascoste"), N_("Cartelle di giochi e PKG"), N_("Installa PKG"), N_("Montaggio automatico"),
    N_("Sistema e strumenti"), N_("Telecomando dal telefono"), N_("Server"), N_("Gestore dei file"),
    N_("Perché Omega"), N_("Segnala un bug o chiedi una funzione"), N_("Informazioni su Omega"), N_("Chiudi Omega") };
  return _(L[o]);
}
static int st_icon(int o) {
  static const int I[N_OPT] = { IC_USER, IC_STAR, IC_NEWS, IC_SHIELD, IC_EXIT, IC_BRUSH, IC_GRID, IC_GEAR, IC_CHAT, IC_VOLUME,
    IC_HOME, IC_CLOSE, IC_DRIVE, IC_BOX, IC_USB, IC_FOLDER, IC_GLOBE, IC_CLOUD, IC_FOLDER, IC_HEART, IC_BUG, IC_MORE, IC_POWER };
  return I[o];
}
static void st_value(int o, char *v, size_t n) {
  v[0] = 0;
  switch (o) {
    case SO_AUDIO: snprintf(v, n, _("Musica %s \xC2\xB7 Effetti %s"), audio_music_on() ? _("s\xC3\xAC") : _("no"), audio_sfx_on() ? _("s\xC3\xAC") : _("no")); break;
    case SO_THEME: snprintf(v, n, "%s", theme_name(g_theme)); break;
    case SO_LANG: snprintf(v, n, "%s", i18n_lang_name(i18n_code())); break;
    case SO_SERVER: snprintf(v, n, "%s", server_label()); break;
    case SO_HOME: snprintf(v, n, "%s", home_mode() == 1 ? _("s\xC3\xAC") : _("no")); break;
    case SO_HIDDEN: snprintf(v, n, "%d", layout_hidden_count()); break;
    case SO_STYLE: snprintf(v, n, "%s", hs_name(g_prefs.home_style)); break;
    case SO_AUTOMOUNT: snprintf(v, n, "%s", g_prefs.automount ? _("s\xC3\xAC") : _("no")); break;
    case SO_PATHS: { char g[32][300], k[32][300]; snprintf(v, n, _("%d giochi \xC2\xB7 %d pkg"), paths_list(0, g, 32), paths_list(1, k, 32)); break; }
  }
}

void settings_draw(float t) {
  int w = 1240, x = side_panel(t, w), a = (int)(255 * t);
  int px = x + 50, pw = w - 100;
  draw_icon(IC_GEAR, px + 22, 74, 44, C_TXT, a);
  draw_text(font(W_LIGHT, 44), _("Impostazioni"), px + 60, 46, C_WHITE, a, AL_L);
  int y = 132;
  fill_rrect(px, y, pw, 112, 24, RGB(255, 255, 255), a * 6 / 100);
  draw_avatar(S.me, S.my_avatar, px + 66, y + 56, 80, a);
  draw_text_fit(font(W_MED, 32), S.me, px + 130, y + 18, pw - 170, C_TXT, a, AL_L);
  char info[160];
  snprintf(info, sizeof info, _("Amici: %d \xC2\xB7 online: %d \xC2\xB7 giochi: %d"), S.nfriends, friends_online_count(), napps);
  draw_text_fit(font(W_REG, 22), info, px + 130, y + 64, pw - 170, C_DIM, a, AL_L);
  y += 136;
  // sezioni
  int sw = 330;
  st_sect_anim = approach(st_sect_anim, (float)st_sect, 20.0f);
  for (int i = 0; i < ST_NSECT; i++) {
    int ry = y + i * 74, on = i == st_sect;
    float fa = clampf(1 - fabsf(st_sect_anim - i), 0, 1);
    Col bg = on ? (st_col == 0 ? C_WHITE : RGB(52, 60, 82)) : RGB(255, 255, 255);
    fill_rrect(px, ry, sw, 62, 20, bg, on ? a : (int)(a * (4 + 6 * fa) / 100));
    Col fg = on && st_col == 0 ? RGB(12, 14, 22) : C_TXT;
    draw_icon(ST_SECT[i].ic, px + 40, ry + 31, 26, fg, a);
    draw_text_fit(font(on ? W_MED : W_REG, 27), _(ST_SECT[i].name), px + 76, ry + 14, sw - 96, fg, a, AL_L);
  }
  // voci della sezione
  int ix = px + sw + 30, iw = pw - sw - 30, rs = 78, rh = 66;
  st_anim = approach(st_anim, (float)st_sel, 20.0f);
  const int f0 = ST_SECT[st_sect].first, nn = ST_SECT[st_sect].n;
  for (int k = 0; k < nn; k++) {
    int o = f0 + k, ry = y + k * rs;
    float fa = st_col ? clampf(1 - fabsf(st_anim - k), 0, 1) : 0;
    row_bg(ix, ry, iw, rh, fa, a);
    int on = st_col && st_sel == k;
    fill_circle(ix + 48, ry + rh / 2, 24, on ? C_WHITE : RGB(44, 52, 72), a);
    draw_icon(st_icon(o), ix + 48, ry + rh / 2, 25, on ? RGB(12, 14, 22) : C_TXT, a);
    char v[128]; st_value(o, v, sizeof v);
    int vw = v[0] ? draw_text_fit(font(W_REG, 23), v, ix + iw - 30, ry + (rh - TTF_FontHeight(font(W_REG, 23))) / 2, 340, C_DIM, a, AL_R) : 0;
    TTF_Font *fo = font(W_MED, 28);
    draw_text_fit(fo, st_label(o), ix + 92, ry + (rh - TTF_FontHeight(fo)) / 2, iw - 92 - 30 - (vw ? vw + 24 : 0), o == SO_QUIT || o == SO_LOGOUT ? C_ERR : C_TXT, a, AL_L);
  }
  const int ic[] = { IC_BTN_X, IC_BTN_O };
  const char *lb[] = { _("Seleziona"), _("Indietro") };
  hints(ic, lb, 2, a);
}

// ------------------------------------------------- bug e richieste --
static int fb_kind;
static void fb_done(int st, JVal *j, const char *raw, void *ud) {
  (void)j; (void)raw; (void)ud;
  set_msg(st == 201 ? _("Grazie! Il messaggio è arrivato allo sviluppatore") : st == 429 ? _("Troppi messaggi: riprova più tardi") : _("Invio non riuscito: controlla la rete"), st != 201);
}
static void fb_send(int with_logs) {
  char text[900] = "";
  if (!edit_text(fb_kind ? _("Descrivi la funzione che vorresti") : _("Descrivi il problema: cosa facevi e cosa è successo"), text, sizeof text, 0) || strlen(text) < 4) return;
  size_t cap = with_logs ? 200 * 1024 : 4096, o = 0; char *b = malloc(cap); if (!b) return;
  char esc[1900]; json_escape(esc, sizeof esc, text);
  o += (size_t)snprintf(b, cap, "{\"kind\":\"%s\",\"text\":\"%s\",\"app_version\":\"%s\",\"lang\":\"%s\"", fb_kind ? "idea" : "bug", esc, OMEGA_VERSION, i18n_code());
  if (with_logs) {
    // le ultime righe del registro aiutano a capire il problema (niente password)
    size_t len = 0; char *lg = file_read(OMEGA_LOG, 2 * 1024 * 1024, &len);
    if (lg) {
      const char *from = len > 60000 ? lg + len - 60000 : lg;
      o += (size_t)snprintf(b + o, cap - o, ",\"log\":\"");
      for (const unsigned char *c = (const unsigned char *)from; *c && o + 8 < cap - 4; c++) {
        if (*c == '"' || *c == '\\') { b[o++] = '\\'; b[o++] = (char)*c; }
        else if (*c == '\n') { b[o++] = '\\'; b[o++] = 'n'; }
        else if (*c >= 0x20) b[o++] = (char)*c;
      }
      b[o++] = '"';
      free(lg);
    }
  }
  if (o + 2 < cap) { b[o++] = '}'; b[o] = 0; }
  net_req(HTTP_POST, OMEGA_API "/feedback", b, fb_done, NULL);
  free(b);
  set_msg(_("Invio..."), 0);
}
static void fb_logs(int idx, void *ud) { (void)ud; if (idx >= 0) fb_send(idx == 0); }
static void fb_pick(int idx, void *ud) {
  (void)ud; if (idx < 0) return;
  fb_kind = idx;
  if (idx == 0) { static const char *it[2]; it[0] = _("Sì, allega il registro (consigliato)"); it[1] = _("No, solo il testo"); menu_open(_("Allegare il registro di Omega? Aiuta a trovare il problema"), it, 2, fb_logs, NULL); }
  else fb_send(0);
}
void feedback_open(void) {
  static const char *it[2]; it[0] = _("Segnala un bug"); it[1] = _("Chiedi una funzione nuova");
  menu_open(_("Scrivi allo sviluppatore"), it, 2, fb_pick, NULL);
}

static void style_pick(int idx, void *ud) {
  (void)ud; if (idx < 0 || idx > 5) return;
  int n = 0; const PrefDef *t = prefs_table(&n);
  for (int i = 0; i < n; i++) if (!strcmp(t[i].key, "home_style")) pref_set(&t[i], idx);
}
static void automount_toggle(void) {
  int n = 0; const PrefDef *t = prefs_table(&n);
  for (int i = 0; i < n; i++) if (!strcmp(t[i].key, "automount")) pref_set(&t[i], !g_prefs.automount);
  set_msg(g_prefs.automount ? _("Montaggio automatico attivo: i giochi dei dischi si registrano da soli") : _("Montaggio automatico spento"), 0);
}

void settings_input(int b) {
  int nn = ST_SECT[st_sect].n;
  if (st_col == 0) {
    if (b == B_O) { ov_pop(); return; }
    if (b == B_UP && st_sect > 0) { st_sect--; st_sel = 0; sfx_play(SFX_MOVE); }
    else if (b == B_DOWN && st_sect < ST_NSECT - 1) { st_sect++; st_sel = 0; sfx_play(SFX_MOVE); }
    else if (b == B_RIGHT || b == B_X) { st_col = 1; st_sel = 0; sfx_play(SFX_SELECT); }
    return;
  }
  if (b == B_O || b == B_LEFT) { st_col = 0; return; }
  if (b == B_UP && st_sel > 0) { st_sel--; sfx_play(SFX_MOVE); }
  else if (b == B_DOWN && st_sel < nn - 1) { st_sel++; sfx_play(SFX_MOVE); }
  else if (b == B_X) {
    switch (ST_SECT[st_sect].first + st_sel) {
      case SO_PROFILE: profile_open(S.me); break;
      case SO_CUSTOM: custom_open(); break;
      case SO_AVATAR: ov_push(OV_AVATAR); break;
      case SO_BIO: { char bio[168]; snprintf(bio, sizeof bio, "%s", S.my_about); if (edit_text(_("La tua bio"), bio, sizeof bio, 0)) social_profile_update(bio, 0); break; }
      case SO_THEME: {
        static const char *names[N_THEMES];
        for (int i = 0; i < N_THEMES; i++) names[i] = theme_name(i);
        menu_open(_("Tema"), names, N_THEMES, theme_pick, NULL);
        break;
      }
      case SO_STYLE: { static const char *it[6]; for (int i = 0; i < 6; i++) it[i] = hs_name(i); menu_open(_("Modalità del menu"), it, 6, style_pick, NULL); menu_select(g_prefs.home_style); break; }
      case SO_LANG: open_lang_menu(); break;
      case SO_AUDIO: open_audio_menu(); break;
      case SO_SERVER: server_menu(); break;
      case SO_HOME: home_mode_menu(); break;
      case SO_HIDDEN: hidden_menu(); break;
      case SO_PATHS: paths_menu(); break;
      case SO_PKGS: pkgs_open(); break;
      case SO_AUTOMOUNT: automount_toggle(); break;
      case SO_SYSTEM: system_open(); break;
      case SO_REMOTE: remote_open(); break;
      case SO_FILES: files_open(NULL); break;
      case SO_PRIVACY: privacy_menu(); break;
      case SO_WHY: ov_push(OV_WHY); break;
      case SO_FEEDBACK: feedback_open(); break;
      case SO_ABOUT: ov_push(OV_ABOUT); break;
      case SO_LOGOUT: confirm_open(_("Vuoi uscire dal tuo account Omega?"), _("Esci"), st_logout_yes, NULL); break;
      case SO_QUIT: confirm_open(_("Chiudere Omega e tornare alla home di sistema?"), _("Chiudi"), st_quit_yes, NULL); break;
    }
  }
}

// ---------------------------------------------------------- perché Omega --
// I motivi per usare la dash, detti in breve: si apre da Impostazioni › Aiuto
// e dalla finestra delle novità.
static float why_scroll; static int why_sel;
static const struct { int ic; const char *t, *d; } WHY[] = {
  { IC_GAMEPAD, N_("Tutti i tuoi giochi in un posto"), N_("Giochi installati, homebrew, payload e giochi sui dischi esterni nella stessa fila, con cartelle, ordine e app nascoste.") },
  { IC_USB, N_("Dischi e chiavette senza pensieri"), N_("Colleghi un disco e i suoi giochi compaiono da soli, anche nella Home della console: il montaggio automatico fa il lavoro di ShadowMount.") },
  { IC_BOX, N_("Installi i PKG con un tasto"), N_("Da chiavette, dischi, cartelle a scelta o dal PC via Wi-Fi, con l'icona del gioco e la barra che avanza come sulla PS4, in coda uno alla volta.") },
  { IC_FRIENDS, N_("Amici, party e chat"), N_("Vedi chi gioca e a cosa, parli in party con la voce, scrivi messaggi e inviti gli amici: un social pensato per la console con jailbreak.") },
  { IC_STORE, N_("Uno Store della community"), N_("Homebrew pubblicati dagli utenti con voti, stelle, commenti, novità degli amici e liste dei desideri; e La mia libreria per i tuoi backup.") },
  { IC_TROPHY, N_("Trofei e record"), N_("I trofei della console nel profilo e in classifica, il tempo di gioco della settimana e le maratone di tutti gli iscritti.") },
  { IC_GLOBE, N_("Il telefono diventa un telecomando"), N_("Musica, caricamento dei giochi dal PC, libreria e JSON, temperatura della console: tutto dal browser del telefono, con un PIN.") },
  { IC_MUSIC, N_("Musica e radio"), N_("Radio, file e server personali, con il lettore che continua anche fuori dalla home e i comandi nel menu in gioco.") },
  { IC_GRID, N_("Fatta a modo tuo"), N_("Sei modalità del menu (Omega, PS4, XMB, Griglia, Carosello, Cinema), temi, sfondi, suoni e più di trenta opzioni.") },
  { IC_HEART, N_("Gratis, aperta e nella tua lingua"), N_("Sorgente GPL scaricabile, 27 lingue, nessun account Sony, aggiornamenti automatici e un tasto per segnalare bug o chiedere funzioni.") },
};
#define NWHY (int)(sizeof WHY / sizeof *WHY)
void why_draw(float t) {
  backdrop(t, 185);
  int w = 1500, h = 900, x = SCREEN_W / 2 - w / 2, y = SCREEN_H / 2 - h / 2 + (int)((1 - ease_out(t)) * 40), a = (int)(255 * t);
  shadow_rrect(x, y, w, h, 34, 40, a * 70 / 100);
  fill_rrect(x, y, w, h, 34, RGB(16, 20, 34), a);
  glow(x + 160, y + 120, 320, C_ACC, a * 20 / 100);
  draw_logo(x + 110, y + 104, 96, a);
  draw_text(font(W_LIGHT, 50), _("Perché Omega"), x + 190, y + 58, C_WHITE, a, AL_L);
  draw_text(font(W_REG, 24), _("Dieci buoni motivi per usarla come Home"), x + 192, y + 124, C_DIM, a, AL_L);
  int cw = (w - 150) / 2, ch = 150, top = y + 196, vis = h - 280;
  int row = why_sel / 2;
  why_scroll = approach(why_scroll, (float)(row > 1 ? (row - 1) * (ch + 18) : 0), 12.0f);
  SDL_Rect clip = { x, top - 6, w, vis + 12 }; SDL_RenderSetClipRect(R, &clip);
  for (int i = 0; i < NWHY; i++) {
    int cx = x + 50 + (i % 2) * (cw + 50), cy = top + (i / 2) * (ch + 18) - (int)why_scroll;
    if (cy + ch < top - 6 || cy > top + vis) continue;
    int on = i == why_sel;
    fill_rrect(cx, cy, cw, ch, 22, C_WHITE, a * (on ? 12 : 5) / 100);
    if (on) stroke_rrect(cx - 3, cy - 3, cw + 6, ch + 6, 25, 3, C_WHITE, a);
    fill_circle(cx + 56, cy + 56, 34, mix(C_ACC, C_WHITE, 0.1f), a);
    draw_icon(WHY[i].ic, cx + 56, cy + 56, 32, C_WHITE, a);
    draw_text_fit(font(W_MED, 28), _(WHY[i].t), cx + 110, cy + 20, cw - 130, C_WHITE, a, AL_L);
    draw_text_wrap(font(W_REG, 21), _(WHY[i].d), cx + 110, cy + 60, cw - 130, 3, 26, C_DIM, a);
  }
  SDL_RenderSetClipRect(R, NULL);
  const int ic[] = { IC_BTN_O };
  const char *lb[] = { _("Chiudi") };
  hints(ic, lb, 1, a);
}
void why_input(int b) {
  if (b == B_O || b == B_X) { ov_pop(); return; }
  if (b == B_LEFT && why_sel % 2) why_sel--;
  else if (b == B_RIGHT && why_sel % 2 == 0 && why_sel + 1 < NWHY) why_sel++;
  else if (b == B_UP && why_sel >= 2) why_sel -= 2;
  else if (b == B_DOWN && why_sel + 2 < NWHY) why_sel += 2;
}

// --------------------------------------------------------- informazioni --
// Crediti: chi sviluppa Omega, licenza, sorgente e dichiarazione di non affiliazione.
void about_draw(float t) {
  backdrop(t, 160);
  int w = 1040, h = 740, x = SCREEN_W / 2 - w / 2, y = SCREEN_H / 2 - h / 2 + (int)((1 - ease_out(t)) * 50);
  int a = (int)(255 * t), cx = SCREEN_W / 2;
  shadow_rrect(x, y, w, h, 34, 50, a * 70 / 100);
  fill_rrect(x, y, w, h, 34, C_PANEL, a);
  stroke_rrect(x, y, w, h, 34, 1, RGB(80, 92, 125), a / 2);
  glow(cx, y + 150, 190, RGB(40, 120, 255), a * 45 / 100);
  draw_logo(cx, y + 150, 170, a);
  draw_text(font(W_LIGHT, 30), "O M E G A", cx, y + 252, C_TXT, a, AL_C);
  char l[256];
  snprintf(l, sizeof l, _("Versione %s"), OMEGA_VERSION);
  draw_text(font(W_REG, 24), l, cx, y + 300, C_DIM, a, AL_C);
  fill_rect(x + 90, y + 356, w - 180, 1, RGB(80, 92, 125), a / 2);
  draw_text_fit(font(W_MED, 32), _("Sviluppato da TheCriicom"), cx, y + 386, w - 140, C_WHITE, a, AL_C);
  draw_text(font(W_REG, 26), "outlinedigital.it", cx, y + 434, C_ACC2, a, AL_C);
  draw_text_fit(font(W_REG, 25), _("Software libero \xE2\x80\x94 GPL-3.0-or-later"), cx, y + 500, w - 140, C_TXT, a, AL_C);
  snprintf(l, sizeof l, _("Il codice sorgente \xC3\xA8 su %s/source"), srv_host(srv_get(srv_current())));
  draw_text_fit(font(W_REG, 23), l, cx, y + 540, w - 140, C_DIM, a, AL_C);
  const char *disc = _("Omega \xC3\xA8 un progetto indipendente: non \xC3\xA8 affiliato a Sony Interactive Entertainment e non usa i suoi account n\xC3\xA9 i suoi server.");
  draw_text_wrap_al(font(W_REG, 22), disc, cx, y + 608, w - 220, 3, 32, C_FAINT, a, AL_C);
  const int ic[] = { IC_BTN_O };
  const char *lb[] = { _("Indietro") };
  hints(ic, lb, 1, a);
}
void about_input(int b) { if (b == B_O || b == B_X) ov_pop(); }

// ------------------------------------------------------------------ notizie --
static News nw;
void news_open(const News *n) { if (!n) return; nw = *n; ov_push(OV_NEWS); }
void news_draw(float t) {
  backdrop(t, 170);
  int w = 1300, h = 860, x = SCREEN_W / 2 - w / 2, y = SCREEN_H / 2 - h / 2 + (int)((1 - ease_out(t)) * 60);
  int a = (int)(255 * t);
  shadow_rrect(x, y, w, h, 34, 50, a * 70 / 100);
  fill_rrect(x, y, w, h, 34, RGB(18, 21, 32), a);
  news_art(&nw, x, y, w, 380, -34, a);
  grad_v(x, y + 200, w, 180, RGB(18, 21, 32), 0, RGB(18, 21, 32), a);
  int tw = text_w(font(W_BOLD, 20), nw.tag) + 30;
  fill_rrect(x + 60, y + 50, tw, 38, 19, C_BLACK, a * 45 / 100);
  draw_text(font(W_BOLD, 20), nw.tag, x + 75, y + 57, C_WHITE, a, AL_L);
  draw_text_wrap(font(W_LIGHT, 54), nw.title, x + 60, y + 300, w - 120, 2, 64, C_WHITE, a);
  char tm[64]; rel_time(nw.when, tm, sizeof tm);
  draw_text(font(W_REG, 24), tm, x + 62, y + 440, C_FAINT, a, AL_L);
  draw_text_wrap(font(W_REG, 30), nw.body, x + 60, y + 500, w - 120, 7, 42, C_TXT, a);
  if (nw.link[0]) {
    draw_text(font(W_MED, 22), _("Leggi l'articolo completo su:"), x + 62, y + h - 110, C_DIM, a, AL_L);
    draw_text_fit(font(W_REG, 22), nw.link, x + 62, y + h - 76, w - 124, C_ACC2, a, AL_L);
  }
  if (nw.link[0]) { const int ic[] = { IC_BTN_X, IC_BTN_O }; const char *lb[] = { _("Apri nel browser"), _("Chiudi") }; hints(ic, lb, 2, a); }
  else { const int ic[] = { IC_BTN_O }; const char *lb[] = { _("Chiudi") }; hints(ic, lb, 1, a); }
}
void news_input(int b) {
  if (b == B_O) ov_pop();
  else if (b == B_X && nw.link[0]) { ov_pop(); browser_open(nw.link); }
}
