// Omega UI — pannelli sopra la home: menu e conferme, Centro di controllo,
// Game Base, notifiche, profilo, scelta avatar, chat, ricerca, impostazioni e
// lettore di notizie.
#include "app.h"
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
  if (f->game_id[0]) { char t[40]; play_time(f->started, t, sizeof t); snprintf(out, n, "%s \xC2\xB7 %s", f->game_name[0] ? f->game_name : f->game_id, t); }
  else if (friend_online(f)) snprintf(out, n, "%s%s%s", !strcmp(f->status, "away") ? "Assente" : !strcmp(f->status, "dnd") ? "Non disturbare" : "Online",
                                       f->status_msg[0] ? " \xC2\xB7 " : "", f->status_msg);
  else if (f->last_seen[0]) { char t[40]; rel_time(f->last_seen, t, sizeof t); snprintf(out, n, "Offline \xC2\xB7 visto %s", t); }
  else snprintf(out, n, "Offline");
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
static char mn_title[96]; static char mn_items[16][96]; static int mn_n, mn_sel; static MenuFn mn_fn; static void *mn_ud;
static float mn_anim;

void menu_open(const char *title, const char **items, int n, MenuFn fn, void *ud) {
  snprintf(mn_title, sizeof mn_title, "%s", title ? title : "");
  mn_n = n > 16 ? 16 : n;
  for (int i = 0; i < mn_n; i++) snprintf(mn_items[i], sizeof mn_items[i], "%s", items[i]);
  mn_sel = 0; mn_anim = 0; mn_fn = fn; mn_ud = ud;
  ov_push(OV_MENU);
}

void menu_draw(float t) {
  backdrop(t, 120);
  mn_anim = approach(mn_anim, (float)mn_sel, 18.0f);
  int w = 640, rh = 82, h = 110 + mn_n * rh + 30;
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
static char cf_msg[220], cf_yes[48]; static MenuFn cf_fn; static void *cf_ud; static int cf_sel;
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
    draw_text(font(W_MED, 28), i == 0 ? cf_yes : "Annulla", bx + bw / 2, by + 22, foc ? RGB(10, 12, 20) : C_TXT, a, AL_C);
  }
}
void confirm_input(int b) {
  if (b == B_LEFT) cf_sel = 0; else if (b == B_RIGHT) cf_sel = 1;
  else if (b == B_O) ov_pop();
  else if (b == B_X) { MenuFn fn = cf_fn; void *ud = cf_ud; int s = cf_sel; ov_pop(); if (s == 0 && fn) fn(0, ud); }
}

// ------------------------------------------------------ Centro di controllo --
enum { CC_NOTIF, CC_GB, CC_COMMUNITY, CC_PARTY, CC_MSG, CC_BROWSER, CC_PROFILE, CC_SETTINGS, CC_POWER, CC_N };
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
  char clock[16], date[64]; time_t tt = time(NULL); struct tm *lt = localtime(&tt);
  static const char *GG[7] = { "domenica", "lunedì", "martedì", "mercoledì", "giovedì", "venerdì", "sabato" };
  static const char *MM[12] = { "gennaio", "febbraio", "marzo", "aprile", "maggio", "giugno", "luglio", "agosto", "settembre", "ottobre", "novembre", "dicembre" };
  snprintf(clock, sizeof clock, "%02d:%02d", lt ? lt->tm_hour : 0, lt ? lt->tm_min : 0);
  snprintf(date, sizeof date, "%s %d %s", lt ? GG[lt->tm_wday] : "", lt ? lt->tm_mday : 0, lt ? MM[lt->tm_mon] : "");
  draw_text(font(W_LIGHT, 64), clock, 110, y + 26, C_WHITE, a, AL_L);
  draw_text(font(W_REG, 28), date, 290, y + 56, C_DIM, a, AL_L);
  draw_avatar(S.me, S.my_avatar, SCREEN_W - 150, y + 62, 64, a);
  draw_text(font(W_MED, 28), S.me, SCREEN_W - 196, y + 46, C_TXT, a, AL_R);

  static const char *lbl[CC_N] = { "Notifiche", "Game Base", "Community", "Party", "Messaggi", "Browser", "Profilo", "Impostazioni", "Spegni" };
  static const int ico[CC_N] = { IC_BELL, IC_FRIENDS, IC_NEWS, IC_PARTY, IC_CHAT, IC_GLOBE, IC_USER, IC_GEAR, IC_POWER };
  int cw = 186, ch = 222, gap = 14, x0 = (SCREEN_W - (CC_N * cw + (CC_N - 1) * gap)) / 2, cy = y + 140;
  for (int i = 0; i < CC_N; i++) {
    int cx = x0 + i * (cw + gap);
    float fa = clampf(1 - fabsf(cc_anim - i), 0, 1);
    int lift = (int)(14 * fa);
    int foc = cc_sel == i;
    if (foc) shadow_rrect(cx, cy - lift, cw, ch, 28, 22, a * 60 / 100);
    fill_rrect(cx, cy - lift, cw, ch, 28, mix(RGB(30, 35, 50), RGB(235, 238, 245), fa), a);
    Col fg = mix(C_TXT, RGB(14, 16, 24), fa);
    draw_icon(ico[i], cx + cw / 2, cy - lift + 82, 70, fg, a);
    draw_text(font(W_MED, 26), lbl[i], cx + cw / 2, cy - lift + 140, fg, a, AL_C);
    char st[48] = "";
    switch (i) {
      case CC_NOTIF: if (S.unread_notif) snprintf(st, sizeof st, "%d nuove", S.unread_notif); break;
      case CC_GB: snprintf(st, sizeof st, "%d online", friends_online_count()); break;
      case CC_COMMUNITY: if (S.unread_groups) snprintf(st, sizeof st, "%d nei gruppi", S.unread_groups); else snprintf(st, sizeof st, "Bacheca e gruppi"); break;
      case CC_PARTY: snprintf(st, sizeof st, "%s", S.party.active ? (voice_state() == 1 ? "Voce attiva" : "Attivo") : S.ninv ? "Invito!" : "Nessuno"); break;
      case CC_MSG: if (S.unread_msg) snprintf(st, sizeof st, "%d non letti", S.unread_msg); break;
      case CC_PROFILE: snprintf(st, sizeof st, "%s", S.me); break;
    }
    if (st[0]) draw_text_fit(font(W_REG, 21), st, cx + cw / 2, cy - lift + 176, cw - 24, mix(C_DIM, RGB(70, 76, 96), fa), a, AL_C);
    int badge = i == CC_NOTIF ? S.unread_notif : i == CC_MSG ? S.unread_msg : i == CC_GB ? S.in_req : i == CC_PARTY ? S.ninv : i == CC_COMMUNITY ? S.unread_groups : 0;
    if (badge) draw_badge(cx + cw - 26, cy - lift + 26, badge, a);
  }
  const int ic[] = { IC_BTN_X, IC_BTN_O };
  const char *lb[] = { "Apri", "Chiudi" };
  hints(ic, lb, 2, a);
}

void cc_input(int b) {
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
      case CC_BROWSER: browser_open(NULL); break;
      case CC_PROFILE: profile_open(S.me); break;
      case CC_SETTINGS: ov_push(OV_SETTINGS); break;
      case CC_POWER: { static const char *it[] = { "Chiudi Omega", "Esci dall'account" }; menu_open("Opzioni di alimentazione", it, 2, power_menu, NULL); break; }
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
      add_item(IT_ACTION, 0, IC_ADDUSER, "Aggiungi amici", "Cerca un ID online", NULL, 0, 0);
      snprintf(sub, sizeof sub, "Amici \xC2\xB7 %d online su %d", friends_online_count(), S.nfriends);
      add_item(IT_HEADER, 0, -1, sub, NULL, NULL, 0, 0);
      for (int i = 0; i < S.nfriends; i++) { friend_status(&S.friends[i], sub, sizeof sub); add_item(IT_FRIEND, i, -1, S.friends[i].oid, sub, S.friends[i].oid, S.friends[i].avatar, 0); }
      if (!S.nfriends) add_item(IT_EMPTY, 0, IC_FRIENDS, "Ancora nessun amico", "Aggiungi qualcuno con il suo ID online.", NULL, 0, 0);
      break;
    case GB_REQ:
      snprintf(sub, sizeof sub, "Ricevute \xC2\xB7 %d", S.nin);
      add_item(IT_HEADER, 0, -1, sub, NULL, NULL, 0, 0);
      for (int i = 0; i < S.nin; i++) { char t[40]; rel_time(S.in[i].when, t, sizeof t); snprintf(sub, sizeof sub, "Vuole diventare tuo amico \xC2\xB7 %s", t); add_item(IT_REQ_IN, i, -1, S.in[i].oid, sub, S.in[i].oid, S.in[i].avatar, 0); }
      if (!S.nin) add_item(IT_EMPTY, 0, IC_ADDUSER, "Nessuna richiesta ricevuta", NULL, NULL, 0, 0);
      snprintf(sub, sizeof sub, "Inviate \xC2\xB7 %d", S.nout);
      add_item(IT_HEADER, 0, -1, sub, NULL, NULL, 0, 0);
      for (int i = 0; i < S.nout; i++) { char t[40]; rel_time(S.out[i].when, t, sizeof t); snprintf(sub, sizeof sub, "In attesa di risposta \xC2\xB7 %s", t); add_item(IT_REQ_OUT, i, -1, S.out[i].oid, sub, S.out[i].oid, S.out[i].avatar, 0); }
      if (!S.nout) add_item(IT_EMPTY, 0, IC_SEND, "Nessuna richiesta inviata", NULL, NULL, 0, 0);
      break;
    case GB_PARTY:
      if (S.party.active) {
        add_item(IT_PARTYCARD, 0, -1, S.party.name, NULL, NULL, 0, 0);
        int muted = 0; for (int i = 0; i < S.party.nmembers; i++) if (!strcasecmp(S.party.members[i].oid, S.me)) muted = S.party.members[i].muted;
        add_item(IT_ACTION, 10, IC_CHAT, "Chat del party", S.npmsg ? S.pmsg[S.npmsg - 1].body : "Scrivi al gruppo", NULL, 0, 0);
        add_item(IT_ACTION, 11, IC_ADDUSER, "Invita amici", "Solo amici", NULL, 0, 0);
        add_item(IT_ACTION, 12, muted ? IC_MICOFF : IC_MIC, muted ? "Attiva microfono" : "Disattiva microfono", NULL, NULL, 0, 0);
        add_item(IT_ACTION, 13, IC_EXIT, "Lascia il party", NULL, NULL, 0, 0);
        snprintf(sub, sizeof sub, "Membri \xC2\xB7 %d", S.party.nmembers);
        add_item(IT_HEADER, 0, -1, sub, NULL, NULL, 0, 0);
        for (int i = 0; i < S.party.nmembers; i++) {
          PartyMember *m = &S.party.members[i];
          snprintf(sub, sizeof sub, "%s%s%s", m->owner ? "Leader" : "Membro", m->game_name[0] ? " \xC2\xB7 " : "", m->game_name);
          add_item(IT_PMEMBER, i, -1, m->oid, sub, m->oid, m->avatar, 0);
        }
        for (int i = 0; i < S.party.ninvited; i++) add_item(IT_PMEMBER, 100 + i, -1, S.party.invited[i].oid, "Invitato \xC2\xB7 in attesa", S.party.invited[i].oid, S.party.invited[i].avatar, 0);
      } else {
        add_item(IT_ACTION, 20, IC_PARTY, "Avvia un party", "Crea un gruppo e invita i tuoi amici", NULL, 0, 0);
      }
      snprintf(sub, sizeof sub, "Inviti ricevuti \xC2\xB7 %d", S.ninv);
      add_item(IT_HEADER, 0, -1, sub, NULL, NULL, 0, 0);
      for (int i = 0; i < S.ninv; i++) {
        snprintf(sub, sizeof sub, "Invito da %s \xC2\xB7 %d %s", S.inv[i].from, S.inv[i].members, S.inv[i].members == 1 ? "membro" : "membri");
        add_item(IT_PINVITE, i, -1, S.inv[i].name, sub, S.inv[i].from, S.inv[i].avatar, 0);
      }
      if (!S.ninv) add_item(IT_EMPTY, 0, IC_PARTY, "Nessun invito", NULL, NULL, 0, 0);
      break;
    case GB_MSG:
      add_item(IT_ACTION, 30, IC_PLUS, "Nuovo messaggio", "Scrivi a un amico", NULL, 0, 0);
      add_item(IT_HEADER, 0, -1, "Conversazioni", NULL, NULL, 0, 0);
      for (int i = 0; i < S.nconv; i++) {
        char t[40]; rel_time(S.conv[i].when, t, sizeof t);
        snprintf(sub, sizeof sub, "%s%s", S.conv[i].from_me ? "Tu: " : "", S.conv[i].last);
        add_item(IT_CONV, i, -1, S.conv[i].oid, sub, S.conv[i].oid, S.conv[i].avatar, S.conv[i].unread);
      }
      if (!S.nconv) add_item(IT_EMPTY, 0, IC_CHAT, "Nessuna conversazione", "I messaggi con i tuoi amici appariranno qui.", NULL, 0, 0);
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
  switch (it->type) { case IT_HEADER: return 70; case IT_EMPTY: return 150; case IT_PARTYCARD: return 210; default: return 104; }
}

static void draw_partycard(int x, int y, int w, int alpha) {
  news_art(NULL, x, y, w, 190, 24, alpha);
  draw_icon(IC_PARTY, x + 70, y + 70, 64, C_WHITE, alpha);
  draw_text_fit(font(W_MED, 34), S.party.name, x + 130, y + 40, w - 160, C_WHITE, alpha, AL_L);
  char m[64]; snprintf(m, sizeof m, "%d %s \xC2\xB7 %s", S.party.nmembers, S.party.nmembers == 1 ? "membro" : "membri", S.party.owner ? "sei il leader" : "sei nel party");
  draw_text(font(W_REG, 24), m, x + 130, y + 88, RGB(220, 230, 255), alpha, AL_L);
  const char *vs = voice_state() == 1 ? "Voce attiva" : voice_state() == 2 ? "Solo ascolto: microfono non disponibile" : "Voce in avvio...";
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
}

void gb_draw(float t) {
  gb_build();
  if (gb_zone == 1) gb_fix_sel(1);
  int w = 820, x = side_panel(t, w), a = (int)(255 * t);
  int px = x + 50, pw = w - 100;
  draw_icon(IC_FRIENDS, px + 22, 74, 44, C_TXT, a);
  draw_text(font(W_LIGHT, 44), "Game Base", px + 60, 46, C_WHITE, a, AL_L);
  static const char *tabs[GB_NTABS] = { "Amici", "Richieste", "Party", "Messaggi" };
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
        else if (it->type == IT_REQ_IN && foc) { draw_icon(IC_BTN_X, px + pw - 110, cy, 22, C_TXT, a); draw_text(font(W_REG, 20), "Opzioni", px + pw - 92, cy - 12, C_DIM, a, AL_L); }
        else if (it->type != IT_ACTION) draw_icon(IC_ARROW_R, px + pw - 34, cy, 24, C_FAINT, a);
        break;
      }
    }
    y += ih;
  }
  SDL_RenderSetClipRect(R, NULL);
  const int ic[] = { IC_BTN_X, IC_BTN_O };
  const char *lb[] = { "Seleziona", "Indietro" };
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
  if (!ninv_names) { set_msg("Nessun amico da invitare", 0); return; }
  menu_open("Invita al party", items, ninv_names, invite_pick, NULL);
}
static char msg_names[64][40]; static int nmsg_names;
static void newmsg_pick(int idx, void *ud) {
  (void)ud;
  if (idx >= 0 && idx < nmsg_names) { const Friend *f = friend_find(msg_names[idx]); chat_open(msg_names[idx], f ? f->avatar : 0); }
}
static void open_newmsg_menu(void) {
  static const char *items[16]; nmsg_names = 0;
  for (int i = 0; i < S.nfriends && nmsg_names < 16; i++) { snprintf(msg_names[nmsg_names], sizeof msg_names[0], "%s", S.friends[i].oid); items[nmsg_names] = msg_names[nmsg_names]; nmsg_names++; }
  if (!nmsg_names) { set_msg("Aggiungi un amico per scrivergli", 0); return; }
  menu_open("Scrivi a", items, nmsg_names, newmsg_pick, NULL);
}

static void friend_menu_dispatch(int idx, void *ud) {
  if (idx == 3) { confirm_open("Vuoi davvero rimuovere questo amico? Non vedrete più l'uno le attività dell'altro.", "Rimuovi", remove_yes, NULL); return; }
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
        case 13: confirm_open("Vuoi lasciare il party?", "Lascia", leave_yes, NULL); break;
        case 20: social_party_create(); break;
        case 30: open_newmsg_menu(); break;
      }
      break;
    case IT_FRIEND: {
      static const char *it1[] = { "Visualizza profilo", "Invia messaggio", "Invita al party", "Rimuovi amico" };
      static const char *it2[] = { "Visualizza profilo", "Invia messaggio", "Avvia un party", "Rimuovi amico" };
      menu_open(it->oid, S.party.active ? it1 : it2, 4, friend_menu_dispatch, NULL);
      break;
    }
    case IT_REQ_IN: { static const char *m[] = { "Accetta", "Rifiuta", "Visualizza profilo" }; menu_open(it->oid, m, 3, req_in_cb, NULL); break; }
    case IT_REQ_OUT: { static const char *m[] = { "Annulla richiesta", "Visualizza profilo" }; menu_open(it->oid, m, 2, req_out_cb, NULL); break; }
    case IT_PMEMBER: if (strcasecmp(it->oid, S.me)) profile_open(it->oid); else profile_open(S.me); break;
    case IT_PINVITE: {
      snprintf(act_party, sizeof act_party, "%s", S.inv[it->idx].party_id);
      static const char *m[] = { "Unisciti al party", "Rifiuta invito" };
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
  draw_text(font(W_LIGHT, 44), "Notifiche", px + 60, 46, C_WHITE, a, AL_L);
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
    draw_text(font(W_MED, 30), "Nessuna notifica", px + pw / 2, ly + 240, C_DIM, a, AL_C);
    draw_text(font(W_REG, 24), "Richieste, messaggi e inviti appariranno qui.", px + pw / 2, ly + 286, C_FAINT, a, AL_C);
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
    char tm[32]; rel_time(n->when, tm, sizeof tm);
    draw_text(font(W_REG, 20), tm, tx, y + rh - 38, C_FAINT, a, AL_L);
  }
  SDL_RenderSetClipRect(R, NULL);
  const int ic[] = { IC_BTN_X, IC_BTN_SQ, IC_BTN_O };
  const char *lb[] = { "Apri", "Cancella tutte", "Indietro" };
  hints(ic, lb, 3, a);
}

static void clear_yes(int idx, void *ud) { (void)idx; (void)ud; social_clear_notif(); }

void notif_input(int b) {
  if (b == B_O) { ov_pop(); nt_marked = 0; return; }
  if (b == B_UP && nt_sel > 0) nt_sel--;
  else if (b == B_DOWN && nt_sel < S.nnotif - 1) nt_sel++;
  else if (b == B_SQ) { if (S.nnotif) confirm_open("Cancellare tutte le notifiche?", "Cancella", clear_yes, NULL); }
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
      if (k >= 0) { ov_clear(); launch_app(k); } else set_msg("Questo gioco non è installato su questa console", 1);
    }
    else if (n->actor[0]) { ov_pop(); profile_open(n->actor); }
  }
}

// ------------------------------------------------------------------ profilo --
static int pr_sel; static float pr_anim;
enum { PA_MSG, PA_PARTY, PA_REMOVE, PA_ADD, PA_ACCEPT, PA_DECLINE, PA_CANCEL, PA_BIO, PA_AVATAR, PA_COVER, PA_LOGOUT, PA_STATUS, PA_MORE, PA_UNBLOCK };
static int pr_actions(int *out, const char **lbl, int *icons) {
  int n = 0;
  #define ADD(id, l, ic) do { out[n] = id; lbl[n] = l; icons[n] = ic; n++; } while (0)
  if (!strcmp(PR.relation, "self")) { ADD(PA_STATUS, "Stato", IC_CHECK); ADD(PA_BIO, "Modifica bio", IC_NEWS); ADD(PA_AVATAR, "Avatar", IC_USER); ADD(PA_COVER, "Copertina", IC_STAR); ADD(PA_LOGOUT, "Esci", IC_EXIT); }
  else if (PR.blocked) { ADD(PA_UNBLOCK, "Sblocca", IC_CHECK); }
  else if (!strcmp(PR.relation, "friend")) { ADD(PA_MSG, "Messaggio", IC_CHAT); ADD(PA_PARTY, S.party.active ? "Invita al party" : "Avvia party", IC_PARTY); ADD(PA_REMOVE, "Rimuovi", IC_CLOSE); ADD(PA_MORE, "Altro", IC_MORE); }
  else if (!strcmp(PR.relation, "incoming")) { ADD(PA_ACCEPT, "Accetta richiesta", IC_CHECK); ADD(PA_DECLINE, "Rifiuta", IC_CLOSE); }
  else if (!strcmp(PR.relation, "outgoing")) { ADD(PA_CANCEL, "Annulla richiesta", IC_CLOSE); }
  else if (PR.loaded) { ADD(PA_ADD, "Aggiungi amico", IC_ADDUSER); }
  if (PR.loaded && !PR.blocked && strcmp(PR.relation, "self") && strcmp(PR.relation, "friend")) ADD(PA_MORE, "Altro", IC_MORE);
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
  if (PR.loading && !PR.loaded) snprintf(st, sizeof st, "Caricamento...");
  else if (PR.game_name[0]) { char tm[40]; play_time(PR.started, tm, sizeof tm); snprintf(st, sizeof st, "Sta giocando a %s \xC2\xB7 %s", PR.game_name, tm); }
  else snprintf(st, sizeof st, "%s", online ? "Online" : "Offline");
  int sw = draw_text(font(W_REG, 28), st, x + 324, y + 346, online ? (PR.game_name[0] ? C_ACC2 : C_OK) : C_DIM, a, AL_L);
  if (PR.status_msg[0]) { char q[90]; snprintf(q, sizeof q, "\xE2\x80\x9C%s\xE2\x80\x9D", PR.status_msg); draw_text_fit(font(W_REG, 26), q, x + 350 + sw, y + 348, w - 760 - sw, C_TXT, a, AL_L); }
  const char *rel = !strcmp(PR.relation, "friend") ? "Amico" : !strcmp(PR.relation, "self") ? "Sei tu" : !strcmp(PR.relation, "incoming") ? "Ti ha inviato una richiesta" : !strcmp(PR.relation, "outgoing") ? "Richiesta inviata" : PR.loaded ? "Non siete amici" : "";
  if (rel[0]) {
    int rw = text_w(font(W_MED, 22), rel) + 40;
    fill_rrect(x + w - rw - 60, y + 286, rw, 44, 22, C_WHITE, a * 14 / 100);
    draw_text(font(W_MED, 22), rel, x + w - rw / 2 - 60, y + 296, C_TXT, a, AL_C);
  }
  int ids[6], icons[6]; const char *lbl[6];
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
  draw_text(font(W_MED, 26), "Informazioni", c1, cy, C_DIM, a, AL_L);
  draw_text_wrap(font(W_REG, 27), PR.about[0] ? PR.about : (strcmp(PR.relation, "self") ? "Nessuna bio." : "Aggiungi una bio con \"Modifica bio\"."), c1, cy + 46, 440, 4, 36, PR.about[0] ? C_TXT : C_FAINT, a);
  char line[96]; snprintf(line, sizeof line, "%d %s", PR.friends_count, PR.friends_count == 1 ? "amico" : "amici");
  draw_icon(IC_FRIENDS, c1 + 16, cy + 222, 28, C_DIM, a);
  draw_text(font(W_REG, 25), line, c1 + 44, cy + 206, C_TXT, a, AL_L);
  if (PR.mutual_friends && strcmp(PR.relation, "self")) {
    snprintf(line, sizeof line, "%d in comune", PR.mutual_friends);
    int lw = text_w(font(W_REG, 25), "000 amici") + 70;
    for (int i = 0; i < PR.nmutual && i < 3; i++) draw_avatar(PR.mutual[i], 0, c1 + lw + i * 26, cy + 222, 34, a);
    draw_text(font(W_REG, 22), line, c1 + lw + 3 * 26 + 10, cy + 210, C_DIM, a, AL_L);
  }
  if (PR.total_seconds > 0) {
    snprintf(line, sizeof line, "%ld ore di gioco su Omega", (PR.total_seconds + 1800) / 3600);
    draw_icon(IC_CLOCK, c1 + 16, cy + 314, 28, C_DIM, a);
    draw_text(font(W_REG, 25), line, c1 + 44, cy + 298, C_TXT, a, AL_L);
  }
  if (PR.created[0]) {
    int Y, M, D; static const char *MM[12] = { "gen", "feb", "mar", "apr", "mag", "giu", "lug", "ago", "set", "ott", "nov", "dic" };
    if (sscanf(PR.created, "%d-%d-%d", &Y, &M, &D) == 3 && M >= 1 && M <= 12) {
      snprintf(line, sizeof line, "Su Omega dal %d %s %d", D, MM[M - 1], Y);
      draw_icon(IC_STAR, c1 + 16, cy + 268, 28, C_DIM, a);
      draw_text(font(W_REG, 25), line, c1 + 44, cy + 252, C_TXT, a, AL_L);
    }
  }
  draw_text(font(W_MED, 26), "Giocati di recente", c2, cy, C_DIM, a, AL_L);
  if (!PR.ngames) draw_text(font(W_REG, 24), strcmp(PR.relation, "friend") && strcmp(PR.relation, "self") ? "Visibile solo agli amici" : "Nessun gioco ancora", c2, cy + 50, C_FAINT, a, AL_L);
  for (int i = 0; i < PR.ngames && i < 5; i++) {
    int yy = cy + 50 + i * 62;
    AppEntry *ap = NULL; for (int k = 0; k < napps; k++) if (!strcmp(apps[k].tid, PR.games[i].game_id)) ap = &apps[k];
    if (ap && ap->tex) draw_tex(ap->tex, c2, yy, 50, 50, a); else { fill_rrect(c2, yy, 50, 50, 10, RGB(40, 48, 70), a); draw_icon(IC_GAMEPAD, c2 + 25, yy + 25, 28, C_DIM, a); }
    draw_text_fit(font(W_MED, 24), PR.games[i].game_name[0] ? PR.games[i].game_name : PR.games[i].game_id, c2 + 66, yy, 360, C_TXT, a, AL_L);
    char tm[40]; rel_time(PR.games[i].last, tm, sizeof tm);
    draw_text(font(W_REG, 20), tm, c2 + 66, yy + 28, C_FAINT, a, AL_L);
  }
  draw_text(font(W_MED, 26), "Attività", c3, cy, C_DIM, a, AL_L);
  if (!PR.nrecent) draw_text(font(W_REG, 24), "Nessuna attività", c3, cy + 50, C_FAINT, a, AL_L);
  for (int i = 0; i < PR.nrecent && i < 5; i++) {
    int yy = cy + 50 + i * 62;
    Activity *ac = &PR.recent[i];
    char l[160];
    if (!strcmp(ac->type, "game_start")) snprintf(l, sizeof l, "Ha giocato a %s", ac->game_name);
    else if (!strcmp(ac->type, "online")) snprintf(l, sizeof l, "È stato online");
    else snprintf(l, sizeof l, "%s", ac->detail);
    fill_circle(c3 + 10, yy + 16, 5, C_ACC2, a);
    draw_text_fit(font(W_REG, 24), l, c3 + 28, yy, 420, C_TXT, a, AL_L);
    char tm[40]; rel_time(ac->when, tm, sizeof tm);
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
  if (idx == 0) confirm_open("Bloccarlo? Non potrà più scriverti, invitarti né vedere i tuoi post, e smetterete di essere amici.", "Blocca", block_yes, NULL);
  else if (idx == 1) { static const char *r[] = { "Spam", "Molestie", "Contenuto offensivo", "Si spaccia per un altro", "Altro" }; menu_open("Segnala utente", r, 5, report_user_pick, NULL); }
}
static void logout_yes(int idx, void *ud) { (void)idx; (void)ud; do_logout(); }

void profile_input(int b) {
  int ids[6], icons[6]; const char *lbl[6];
  int n = pr_actions(ids, lbl, icons);
  if (b == B_O) { ov_pop(); return; }
  if (b == B_LEFT && pr_sel > 0) pr_sel--;
  else if (b == B_RIGHT && pr_sel < n - 1) pr_sel++;
  else if (b == B_X && n) {
    switch (ids[pr_sel]) {
      case PA_MSG: chat_open(PR.oid, PR.avatar); break;
      case PA_PARTY: if (S.party.active) social_party_invite(PR.oid); else { social_party_create(); set_msg("Party creato: ora invita dal profilo", 0); } break;
      case PA_REMOVE: confirm_open("Vuoi davvero rimuovere questo amico?", "Rimuovi", remove_from_profile, NULL); break;
      case PA_ADD: social_friend_request(PR.oid); snprintf(PR.relation, sizeof PR.relation, "outgoing"); break;
      case PA_ACCEPT: social_friend_accept(PR.oid); break;
      case PA_DECLINE: social_friend_decline(PR.oid); break;
      case PA_CANCEL: social_friend_remove(PR.oid); snprintf(PR.relation, sizeof PR.relation, "none"); break;
      case PA_BIO: { char bio[168]; snprintf(bio, sizeof bio, "%s", PR.about); if (edit_text("La tua bio", bio, sizeof bio, 0)) { snprintf(PR.about, sizeof PR.about, "%s", bio); social_profile_update(bio, 0); } break; }
      case PA_AVATAR: ov_push(OV_AVATAR); break;
      case PA_COVER: gallery_open(1); break;
      case PA_LOGOUT: confirm_open("Vuoi uscire dal tuo account Omega?", "Esci", logout_yes, NULL); break;
      case PA_STATUS: status_menu(); break;
      case PA_UNBLOCK: social_block(PR.oid, 0); break;
      case PA_MORE: { static const char *m[] = { "Blocca", "Segnala" }; menu_open(PR.oid, m, 2, more_pick, NULL); break; }
    }
  }
}

// ------------------------------------------------------------ scelta avatar --
// Righe 0-1: personaggi illustrati · righe 2-3: colori · riga 4: foto o video.
static int av_sel = -1;
static int av_index(int sel) { return sel < 16 ? AV_ART_FIRST + sel : sel - 16; }
void avatar_draw(float t) {
  backdrop(t, 140);
  if (av_sel < 0) { av_sel = 32; for (int i = 0; i < 32; i++) if (av_index(i) == S.my_avatar) av_sel = i; }
  int w = 1320, h = 940, x = SCREEN_W / 2 - w / 2, y = SCREEN_H / 2 - h / 2 + (int)((1 - ease_out(t)) * 40);
  int a = (int)(255 * t);
  shadow_rrect(x, y, w, h, 30, 40, a * 70 / 100);
  fill_rrect(x, y, w, h, 30, C_PANEL, a);
  draw_text(font(W_LIGHT, 44), "Scegli il tuo avatar", x + 60, y + 36, C_WHITE, a, AL_L);
  draw_text(font(W_MED, 24), "Personaggi", x + 64, y + 108, C_DIM, a, AL_L);
  draw_text(font(W_MED, 24), "Colori", x + 64, y + 482, C_DIM, a, AL_L);
  float pulse = 0.5f + 0.5f * sinf((float)g_time * 4);
  int custom_frames = 0; int has_custom = media_of(S.me, &custom_frames) != NULL;
  for (int i = 0; i < 32; i++) {
    int row = i / 8, col = i % 8;
    int cx = x + 130 + col * 152, cy = y + 210 + row * 150 + (row >= 2 ? 50 : 0);
    int foc = av_sel == i, idx = av_index(i);
    if (foc) ring(cx, cy, 70, 4, C_WHITE, (int)(a * (0.7f + 0.3f * pulse)));
    // oid vuoto: draw_avatar altrimenti mostrerebbe la foto personalizzata
    if (idx >= AV_ART_FIRST) { char none[2] = ""; draw_avatar(none, idx, cx, cy, foc ? 124 : 108, a); }
    else draw_avatar_color(S.me, idx, cx, cy, foc ? 124 : 108, a);
    if (idx == S.my_avatar && !has_custom) { fill_circle(cx + 42, cy + 42, 17, C_OK, a); draw_icon(IC_CHECK, cx + 42, cy + 42, 22, C_WHITE, a); }
  }
  int by = y + h - 120, bw = 560, bx = x + w / 2 - bw / 2, foc = av_sel == 32;
  if (foc) shadow_rrect(bx, by, bw, 80, 40, 16, a / 2);
  fill_rrect(bx, by, bw, 80, 40, foc ? C_WHITE : RGB(48, 54, 72), a);
  draw_icon(IC_PLAY, bx + 60, by + 40, 30, foc ? RGB(12, 14, 22) : C_TXT, a);
  draw_text(font(W_MED, 28), has_custom ? "Cambia foto o video personale" : "Usa una tua foto o un video", bx + 100, by + 22, foc ? RGB(12, 14, 22) : C_TXT, a, AL_L);
}
void avatar_input(int b) {
  if (b == B_O) { av_sel = -1; ov_pop(); return; }
  if (av_sel == 32) {
    if (b == B_UP) av_sel = 28;
    else if (b == B_X) { av_sel = -1; ov_pop(); gallery_open(0); }
    return;
  }
  if (b == B_LEFT && av_sel % 8 > 0) av_sel--;
  else if (b == B_RIGHT && av_sel % 8 < 7) av_sel++;
  else if (b == B_UP && av_sel >= 8) av_sel -= 8;
  else if (b == B_DOWN) av_sel = av_sel + 8 < 32 ? av_sel + 8 : 32;
  else if (b == B_X) {
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
    char m[64]; snprintf(m, sizeof m, "Chat del party \xC2\xB7 %d membri", S.party.nmembers);
    draw_text(font(W_REG, 23), m, px + 100, 92, C_DIM, a, AL_L);
  } else {
    draw_avatar(CH.oid, CH.avatar, px + 40, 80, 80, a);
    const Friend *f = friend_find(CH.oid);
    if (f && friend_online(f)) status_dot(px + 68, 108, f->status, f->game_id[0] != 0, a);
    draw_text(font(W_MED, 34), CH.oid, px + 100, 48, C_WHITE, a, AL_L);
    char st[160] = "Offline"; if (f) friend_status(f, st, sizeof st);
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
    draw_text(font(W_REG, 26), "Nessun messaggio: scrivi il primo!", px + pw / 2, (top + bottom) / 2 + 20, C_DIM, a, AL_C);
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
    char tm[32]; rel_time(m->when, tm, sizeof tm);
    if (i == CH.nmsg - 1 && tm[0]) draw_text(font(W_REG, 18), tm, m->mine ? bx + bw : bx, y + bh + 4, C_FAINT, a, m->mine ? AL_R : AL_L);
  }
  int content_top = y;
  SDL_RenderSetClipRect(R, NULL);
  float maxs = (float)(top - content_top); if (maxs < 0) maxs = 0;
  if (CH.scroll_t > maxs + 20) CH.scroll_t = maxs + 20;
  int iy = SCREEN_H - 160;
  fill_rrect(px, iy, pw, 84, 42, RGB(36, 42, 60), a);
  draw_icon(IC_BTN_X, px + 44, iy + 42, 26, C_TXT, a);
  draw_text(font(W_REG, 26), "Scrivi un messaggio...", px + 80, iy + 26, C_DIM, a, AL_L);
  fill_circle(px + pw - 42, iy + 42, 30, C_ACC, a);
  draw_icon(IC_SEND, px + pw - 40, iy + 42, 30, C_WHITE, a);
  const int ic[] = { IC_BTN_X, IC_BTN_O };
  const char *lb[] = { "Scrivi", "Indietro" };
  hints(ic, lb, 2, a);
}

void chat_input(int b) {
  if (b == B_O) { ov_pop(); social_load_conversations(); return; }
  if (b == B_UP) CH.scroll_t += 120;
  else if (b == B_DOWN) { CH.scroll_t -= 120; if (CH.scroll_t < 0) CH.scroll_t = 0; }
  else if (b == B_X) {
    char txt[500] = "";
    if (edit_text(CH.party ? "Messaggio al party" : "Messaggio", txt, sizeof txt, 0) && txt[0]) chat_send(txt);
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
  if (!strcmp(r, "friend")) return "Amico";
  if (!strcmp(r, "outgoing")) return "Richiesta inviata";
  if (!strcmp(r, "incoming")) return "Ti ha chiesto l'amicizia";
  return "";
}

void search_draw(float t) {
  backdrop(t, 150);
  int w = 1100, h = 900, x = SCREEN_W / 2 - w / 2, y = SCREEN_H / 2 - h / 2 + (int)((1 - ease_out(t)) * 50);
  int a = (int)(255 * t);
  shadow_rrect(x, y, w, h, 34, 50, a * 70 / 100);
  fill_rrect(x, y, w, h, 34, C_PANEL, a);
  draw_text(font(W_LIGHT, 46), "Aggiungi amici", x + 60, y + 44, C_WHITE, a, AL_L);
  int fy = y + 130, focf = sr_sel == 0;
  fill_rrect(x + 60, fy, w - 120, 90, 45, focf ? RGB(52, 60, 84) : RGB(36, 42, 60), a);
  if (focf) stroke_rrect(x + 56, fy - 4, w - 112, 98, 49, 3, C_WHITE, a);
  draw_icon(IC_SEARCH, x + 112, fy + 45, 36, C_TXT, a);
  draw_text(font(sr_q[0] ? W_REG : W_LIGHT, 30), sr_q[0] ? sr_q : "Cerca per ID online", x + 150, fy + 26, sr_q[0] ? C_TXT : C_DIM, a, AL_L);
  if (S.users_loading) draw_spinner(x + w - 110, fy + 45, 14, a);
  draw_text(font(W_MED, 24), sr_q[0] ? "Risultati" : "Suggeriti", x + 64, fy + 120, C_DIM, a, AL_L);
  int ly = fy + 170, rh = 104, lh = y + h - 40 - ly;
  sr_anim = approach(sr_anim, (float)(sr_sel - 1), 20.0f);
  float off = list_scroll(&sr_scroll, sr_sel > 0 ? sr_sel - 1 : 0, rh, lh);
  SDL_Rect clip = { x, ly - 8, w, lh + 8 };
  SDL_RenderSetClipRect(R, &clip);
  if (!S.users_loading && !S.nusers) draw_text(font(W_REG, 26), sr_q[0] ? "Nessun utente trovato" : "Nessun suggerimento", x + w / 2, ly + 60, C_FAINT, a, AL_C);
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
    const char *act = !strcmp(u->relation, "none") ? "Aggiungi" : !strcmp(u->relation, "incoming") ? "Accetta" : !strcmp(u->relation, "friend") ? "Messaggio" : "Annulla";
    int ic = !strcmp(u->relation, "none") ? IC_ADDUSER : !strcmp(u->relation, "incoming") ? IC_CHECK : !strcmp(u->relation, "friend") ? IC_CHAT : IC_CLOSE;
    int pw2 = text_w(font(W_MED, 25), act) + 90;
    pill(x + w - 80 - pw2, ry + 18, 56, act, ic, sr_sel == i + 1, sr_sel == i + 1 ? 1.0f : 0.0f, a);
  }
  SDL_RenderSetClipRect(R, NULL);
  const int ic[] = { IC_BTN_X, IC_BTN_TRI, IC_BTN_O };
  const char *lb[] = { "Seleziona", "Profilo", "Indietro" };
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
      if (edit_text("Cerca ID online", q, sizeof q, 0)) { snprintf(sr_q, sizeof sr_q, "%s", q); S.nusers = 0; social_search(sr_q); sr_sel = 0; }
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
  static char l0[48], l1[48], l2[48];
  static const char *items[3] = { l0, l1, l2 };
  snprintf(l0, sizeof l0, "Musica di sottofondo: %s", audio_music_on() ? "attiva" : "spenta");
  snprintf(l1, sizeof l1, "Effetti sonori: %s", audio_sfx_on() ? "attivi" : "spenti");
  snprintf(l2, sizeof l2, "Volume musica: %d%%", audio_music_level());
  menu_open("Audio", items, 3, audio_pick, NULL);
}
static void audio_pick(int idx, void *ud) {
  (void)ud;
  if (idx == 0) audio_set(!audio_music_on(), -1, -1);
  else if (idx == 1) audio_set(-1, !audio_sfx_on(), -1);
  else { int l = audio_music_level(); l = l >= 80 ? 30 : l >= 50 ? 80 : 55; audio_set(-1, -1, l); }
  open_audio_menu();   // il menu resta aperto con i valori aggiornati
}
static void theme_pick(int idx, void *ud) { (void)ud; theme_apply(idx, 1); char m[64]; snprintf(m, sizeof m, "Tema: %s", theme_name(idx)); set_msg(m, 0); }

void settings_draw(float t) {
  int w = 780, x = side_panel(t, w), a = (int)(255 * t);
  int px = x + 50, pw = w - 100;
  draw_icon(IC_GEAR, px + 22, 74, 44, C_TXT, a);
  draw_text(font(W_LIGHT, 44), "Impostazioni", px + 60, 46, C_WHITE, a, AL_L);
  int y = 132;
  fill_rrect(px, y, pw, 128, 24, RGB(255, 255, 255), a * 6 / 100);
  draw_avatar(S.me, S.my_avatar, px + 72, y + 64, 88, a);
  draw_text_fit(font(W_MED, 32), S.me, px + 140, y + 22, pw - 170, C_TXT, a, AL_L);
  char info[160];
  snprintf(info, sizeof info, "Amici: %d \xC2\xB7 online: %d \xC2\xB7 giochi: %d", S.nfriends, friends_online_count(), napps);
  draw_text_fit(font(W_REG, 22), info, px + 140, y + 72, pw - 170, C_DIM, a, AL_L);
  y += 152;
  enum { N_OPT = 9 };
  static const char *opts[N_OPT] = { "Il mio profilo", "Cambia avatar", "Modifica bio", "Tema", "Audio", "Server", "Privacy e dati", "Esci dall'account", "Chiudi Omega" };
  static const int oic[N_OPT] = { IC_USER, IC_STAR, IC_NEWS, IC_GEAR, IC_BELL, IC_GLOBE, IC_CHECK, IC_EXIT, IC_POWER };
  st_anim = approach(st_anim, (float)st_sel, 20.0f);
  const int rs = 74, rh = 66;
  for (int i = 0; i < N_OPT; i++) {
    int ry = y + i * rs;
    float fa = clampf(1 - fabsf(st_anim - i), 0, 1);
    row_bg(px, ry, pw, rh, fa, a);
    fill_circle(px + 46, ry + rh / 2, 26, st_sel == i ? C_WHITE : RGB(44, 52, 72), a);
    draw_icon(oic[i], px + 46, ry + rh / 2, 28, st_sel == i ? RGB(12, 14, 22) : C_TXT, a);
    draw_text(font(W_MED, 27), opts[i], px + 92, ry + 16, i == N_OPT - 1 ? C_ERR : C_TXT, a, AL_L);
    if (i == 4) { char au[48]; snprintf(au, sizeof au, "Musica %s \xC2\xB7 Effetti %s", audio_music_on() ? "s\xC3\xAC" : "no", audio_sfx_on() ? "s\xC3\xAC" : "no"); draw_text(font(W_REG, 22), au, px + pw - 30, ry + 21, C_DIM, a, AL_R); }
    if (i == 3) { draw_text(font(W_REG, 23), theme_name(g_theme), px + pw - 30, ry + 19, C_DIM, a, AL_R); fill_circle(px + pw - 40 - text_w(font(W_REG, 23), theme_name(g_theme)) - 20, ry + rh / 2, 10, C_ACC, a); }
    if (i == 5) draw_text_fit(font(W_REG, 23), server_label(), px + pw - 30, ry + 19, 260, C_DIM, a, AL_R);
  }
  const int ic[] = { IC_BTN_X, IC_BTN_O };
  const char *lb[] = { "Seleziona", "Indietro" };
  hints(ic, lb, 2, a);
}

void settings_input(int b) {
  if (b == B_O) { ov_pop(); return; }
  if (b == B_UP && st_sel > 0) st_sel--;
  else if (b == B_DOWN && st_sel < 8) st_sel++;
  else if (b == B_X) {
    switch (st_sel) {
      case 0: profile_open(S.me); break;
      case 1: ov_push(OV_AVATAR); break;
      case 2: { char bio[168]; snprintf(bio, sizeof bio, "%s", S.my_about); if (edit_text("La tua bio", bio, sizeof bio, 0)) social_profile_update(bio, 0); break; }
      case 3: {
        static const char *names[N_THEMES];
        for (int i = 0; i < N_THEMES; i++) names[i] = theme_name(i);
        menu_open("Tema", names, N_THEMES, theme_pick, NULL);
        break;
      }
      case 4: open_audio_menu(); break;
      case 5: server_menu(); break;
      case 6: privacy_menu(); break;
      case 7: confirm_open("Vuoi uscire dal tuo account Omega?", "Esci", st_logout_yes, NULL); break;
      case 8: confirm_open("Chiudere Omega e tornare alla home di sistema?", "Chiudi", st_quit_yes, NULL); break;
    }
  }
}

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
  char tm[32]; rel_time(nw.when, tm, sizeof tm);
  draw_text(font(W_REG, 24), tm, x + 62, y + 440, C_FAINT, a, AL_L);
  draw_text_wrap(font(W_REG, 30), nw.body, x + 60, y + 500, w - 120, 7, 42, C_TXT, a);
  if (nw.link[0]) {
    draw_text(font(W_MED, 22), "Leggi l'articolo completo su:", x + 62, y + h - 110, C_DIM, a, AL_L);
    draw_text_fit(font(W_REG, 22), nw.link, x + 62, y + h - 76, w - 124, C_ACC2, a, AL_L);
  }
  if (nw.link[0]) { const int ic[] = { IC_BTN_X, IC_BTN_O }; const char *lb[] = { "Apri nel browser", "Chiudi" }; hints(ic, lb, 2, a); }
  else { const int ic[] = { IC_BTN_O }; const char *lb[] = { "Chiudi" }; hints(ic, lb, 1, a); }
}
void news_input(int b) {
  if (b == B_O) ov_pop();
  else if (b == B_X && nw.link[0]) { ov_pop(); browser_open(nw.link); }
}
