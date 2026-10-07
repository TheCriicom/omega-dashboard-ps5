// Omega UI — altre modalità del menu (Personalizza › Home › Modalità del menu).
//
//   1 Classica PS4  barra delle funzioni in alto, fila di icone con quella scelta
//                   grande e il nome sotto, come la home della PS4;
//   2 XMB (PS3)     categorie in orizzontale e voci in verticale, con le onde
//                   che scorrono sullo sfondo, come la XrossMediaBar della PS3;
//   3 Griglia       tutta la libreria in una griglia, con il gioco scelto in alto;
//   4 Carosello     copertine in fila che ruotano attorno a quella al centro, con
//                   il riflesso sul pavimento;
//   5 Cinema        elenco elegante a sinistra e il gioco scelto in grande.
//
// La selezione è quella della home (home_sel), quindi sfondo, scheda del gioco,
// avvio con l'animazione e opzioni (R1) restano gli stessi in ogni modalità.
#include "app.h"
#include <math.h>
#include <time.h>

const char *hs_name(int i) {
  static const char *N[] = { N_("Omega"), N_("Classica PS4"), N_("XMB (PS3)"), N_("Griglia"), N_("Carosello"), N_("Cinema") };
  return i >= 0 && i < 6 ? _(N[i]) : "";
}

static float sel_f;                // selezione animata (posizione della fila)
static int ps4_zone;               // 0 icone, 1 barra delle funzioni
static int ps4_fn; static float ps4_fn_f;

// ----------------------------------------------------------- pezzi comuni --
static void status_corner(int a) {
  char clock[64]; clock_text(clock, sizeof clock);
  int rx = SCREEN_W - 80, y = 50;
  rx -= draw_text(font(W_REG, 30), clock, rx, y, C_TXT, a, AL_R) + 26;
  draw_avatar(S.me, S.my_avatar, rx - 26, y + 18, 50, a);
  rx -= 70;
  int badge = S.unread_notif + S.in_req + S.unread_msg + S.ninv;
  if (badge) { draw_icon(IC_BELL, rx - 14, y + 18, 30, C_TXT, a); draw_badge(rx + 2, y, badge, a); rx -= 60; }
  if (music_now_line()) { music_mini(rx - 30, y + 4, a); }
}

static const char *sub_of(const AppEntry *ap) {
  static char b[160];
  if (ap->builtin == 1) return _("Community");
  if (ap->builtin == 2) { snprintf(b, sizeof b, _("Cartella \xC2\xB7 %d app"), layout_folder_count(ap->tid)); return b; }
  if (ap->builtin) return _("App di Omega");
  if (ap->hb) return _("Homebrew");
  if (ap->pld) return _("Payload");
  if (ap->ext) { snprintf(b, sizeof b, _("Su %s"), ap->drive); return b; }
  return ap->tid;
}

static void hints_alt(int a, const char *extra_lbl) {
  int ic[5]; const char *lb[5]; int n = 0;
  ic[n] = IC_BTN_X; lb[n++] = nrow && apps[home_selected()].builtin ? _("Apri") : _("Gioca");
  ic[n] = -1; lb[n++] = _("R1  Opzioni");
  ic[n] = -1; lb[n++] = _("L1  Esplora");
  ic[n] = IC_BTN_OPT; lb[n++] = extra_lbl ? extra_lbl : _("Centro di controllo");
  if (ov_depth() == 0 && g_prefs.hints) { grad_v(0, SCREEN_H - 120, SCREEN_W, 120, RGB(4, 8, 18), 0, RGB(4, 8, 18), a * 70 / 100); hints(ic, lb, n, a); }
}

static void empty_home(int a) {
  draw_icon(IC_GAMEPAD, SCREEN_W / 2, 420, 110, C_DIM, a);
  draw_text(font(W_MED, 34), _("Nessun gioco installato"), SCREEN_W / 2, 500, C_TXT, a, AL_C);
}

// ---------------------------------------------------------- Classica PS4 --
typedef struct { int ic; const char *name; int top; } Fn;
static const Fn PS4_FN[] = {
  { IC_BELL, N_("Notifiche"), 4 }, { IC_FRIENDS, N_("Amici"), 5 }, { IC_PARTY, N_("Party"), 5 }, { IC_USER, N_("Profilo"), 7 },
  { IC_TROPHY, N_("Trofei"), -2 }, { IC_MUSIC, N_("Musica"), 2 }, { IC_SEARCH, N_("Cerca"), 3 }, { IC_GEAR, N_("Impostazioni"), 6 }, { IC_POWER, N_("Alimentazione"), -1 },
};
#define PS4_NFN (int)(sizeof PS4_FN / sizeof *PS4_FN)

static void ps4_draw(int a) {
  int sel = home_selected();
  // barra delle funzioni
  int fy = 92, fs = 84, total = PS4_NFN * fs, fx = SCREEN_W / 2 - total / 2;
  ps4_fn_f = approach(ps4_fn_f, (float)ps4_fn, 18.0f);
  fill_rrect(fx - 24, fy - 44, total + 48, 88, 44, RGB(8, 12, 24), a * (ps4_zone ? 70 : 40) / 100);
  for (int i = 0; i < PS4_NFN; i++) {
    int cx = fx + i * fs + fs / 2, on = ps4_zone && ps4_fn == i;
    if (on) fill_circle(cx, fy, 36, C_WHITE, a);
    draw_icon(PS4_FN[i].ic, cx, fy, on ? 34 : 30, on ? RGB(10, 12, 20) : C_TXT, ps4_zone ? a : a * 75 / 100);
    if (i == 0 && S.unread_notif) draw_badge(cx + 22, fy - 22, S.unread_notif, a);
    if (i == 1 && S.in_req + S.unread_msg + S.ninv) draw_badge(cx + 22, fy - 22, S.in_req + S.unread_msg + S.ninv, a);
  }
  if (ps4_zone) draw_text(font(W_MED, 26), _(PS4_FN[ps4_fn].name), fx + ps4_fn * fs + fs / 2, fy + 52, C_WHITE, a, AL_C);
  status_corner(a);
  if (!nrow) { empty_home(a); return; }
  // fila: la scelta è grande e sta a sinistra, le altre piccole a seguire
  const int small = 180, big = 290, gap = 16, y0 = 300;
  sel_f = approach(sel_f, (float)sel, 12.0f);
  int jw = home_jobs(1520, y0 + big - small, small, a * 90 / 100);
  (void)jw;
  float x = 150 - (sel_f - (int)sel_f) * (small + gap);
  int first = (int)sel_f;
  for (int i = first - 1; i < nrow; i++) {
    if (i < 0) continue;
    int s = i == sel ? big : small;
    int tx = i < first ? 150 - (small + gap) * (first - i) : (int)x, ty = y0 + (big - s);
    if (tx > SCREEN_W) break;
    int al = tx < 140 ? a * 35 / 100 : a;
    if (i == sel) shadow_rrect(tx, ty, s, s, 26, 30, a * 70 / 100);
    home_tile(i, tx, ty, s, al);
    if (i == sel && !ps4_zone) focus_ring(tx, ty, s, s, 26, 0.5f + 0.5f * sinf((float)g_time * 3.2f), a);
    if (i >= first) x += s + gap;
  }
  // il nome sotto la scelta, come sulla PS4, con "Avvia"
  AppEntry *ap = &apps[sel];
  draw_text_fit(font(W_MED, 40), ap->name, 150, y0 + big + 28, 1100, C_WHITE, a, AL_L);
  draw_text_fit(font(W_REG, 24), sub_of(ap), 150, y0 + big + 82, 900, C_DIM, a, AL_L);
  int pw = pill(150, y0 + big + 130, 70, ap->builtin ? _("Apri") : _("Avvia"), IC_PLAY, !ps4_zone, ps4_zone ? 0 : 1, a);
  (void)pw;
  // amici che ci giocano, sotto
  int np = 0; for (int k = 0; k < S.nfriends; k++) if (!strcmp(S.friends[k].game_id, ap->tid)) np++;
  if (np) { char m[96]; snprintf(m, sizeof m, np == 1 ? _("%d amico sta giocando") : _("%d amici stanno giocando"), np); fill_circle(158, y0 + big + 248, 7, C_OK, a); draw_text(font(W_REG, 24), m, 176, y0 + big + 233, C_TXT, a, AL_L); }
}

static int ps4_input(int b) {
  int sel = home_selected();
  if (ps4_zone) {
    if (b == B_LEFT && ps4_fn > 0) { ps4_fn--; sfx_play(SFX_MOVE); }
    else if (b == B_RIGHT && ps4_fn < PS4_NFN - 1) { ps4_fn++; sfx_play(SFX_MOVE); }
    else if (b == B_DOWN || b == B_O) ps4_zone = 0;
    else if (b == B_X) {
      int t = PS4_FN[ps4_fn].top;
      if (t == -1) ov_push(OV_CC); else if (t == -2) trophies_open(S.me); else if (ps4_fn == 2) chat_open_party(); else home_top(t);
    } else return 0;
    return 1;
  }
  if (b == B_LEFT && sel > 0) { home_set_sel(sel - 1); sfx_play(SFX_MOVE); }
  else if (b == B_RIGHT && sel < nrow - 1) { home_set_sel(sel + 1); sfx_play(SFX_MOVE); }
  else if (b == B_UP) { ps4_zone = 1; sfx_play(SFX_MOVE); }
  else if (b == B_X && nrow) launch_app(sel);
  else if (b == B_R1 && nrow) home_more(sel);
  else return 0;
  return 1;
}

// ------------------------------------------------------------- XMB (PS3) --
typedef void (*XAct)(int);
typedef struct { int ic; char name[96]; XAct act; int arg; int app; } XItem;   // app >= 0: tessera della home
enum { XC_USERS, XC_SETTINGS, XC_MUSIC, XC_GAMES, XC_NET, XC_FRIENDS, XC_N };
static int xcat = XC_GAMES; static float xcat_f = XC_GAMES;
static int xsel[XC_N]; static float xsel_f[XC_N];
static XItem xit[MAX_APPS + 32]; static int nxit;

static void xa_top(int t) { home_top(t); }
static void xa_ov(int o) { ov_push((Overlay)o); }
static void xa_custom(int x) { (void)x; custom_open(); }
static void xa_system(int x) { (void)x; system_open(); }
static void xa_remote(int x) { (void)x; mobile_open(); }
static void xa_pkgs(int x) { (void)x; pkgs_open(); }
static void xa_trophies(int x) { (void)x; trophies_open(S.me); }
static void xa_status(int x) { (void)x; status_menu(); }
static void xa_music(int x) { (void)x; music_open(); }
static void xa_toggle(int x) { (void)x; music_toggle(); }
static void xa_party(int x) { (void)x; chat_open_party(); }
static void xa_profile(int i) { if (i >= 0 && i < S.nfriends) profile_open(S.friends[i].oid); }
static void xa_files(int x) { (void)x; files_open(NULL); }
static void xa_paths(int x) { (void)x; pkgs_open(); paths_menu(); }
static void xa_storage(int x) { (void)x; storage_open(); }

static void xadd(int ic, const char *name, XAct act, int arg, int app) {
  if (nxit >= (int)(sizeof xit / sizeof *xit)) return;
  XItem *x = &xit[nxit++]; x->ic = ic; snprintf(x->name, sizeof x->name, "%s", name); x->act = act; x->arg = arg; x->app = app;
}
static void xbuild(int c) {
  nxit = 0;
  switch (c) {
    case XC_USERS:
      xadd(IC_USER, S.me[0] ? S.me : _("Profilo"), xa_top, 7, -1);
      xadd(IC_TROPHY, _("Trofei"), xa_trophies, 0, -1);
      xadd(IC_CHAT, _("Stato"), xa_status, 0, -1);
      xadd(IC_BRUSH, _("Cambia avatar"), xa_ov, OV_AVATAR, -1);
      break;
    case XC_SETTINGS:
      xadd(IC_GEAR, _("Impostazioni"), xa_top, 6, -1);
      xadd(IC_BRUSH, _("Personalizza"), xa_custom, 0, -1);
      xadd(IC_FOLDER, _("Sistema e strumenti"), xa_system, 0, -1);
      xadd(IC_USB, _("Cartelle di giochi e PKG"), xa_paths, 0, -1);
      xadd(IC_DRIVE, _("Archivio e spostamenti"), xa_storage, 0, -1);
      xadd(IC_FOLDER, _("Gestore dei file"), xa_files, 0, -1);
      xadd(IC_CHAT, _("App mobile"), xa_remote, 0, -1);
      break;
    case XC_MUSIC:
      xadd(IC_MUSIC, _("Lettore musicale"), xa_music, 0, -1);
      xadd(music_playing() ? IC_PAUSE : IC_PLAY, music_now_line() ? music_now_line() : _("Riproduci / pausa"), xa_toggle, 0, -1);
      break;
    case XC_GAMES:
      for (int i = 0; i < nrow; i++) if (!SYS_TILE(&apps[i])) xadd(IC_GAMEPAD, apps[i].name, NULL, 0, i);
      xadd(IC_BOX, _("Installa PKG"), xa_pkgs, 0, -1);
      break;
    case XC_NET:
      for (int i = 0; i < nrow; i++) if (SYS_TILE(&apps[i])) xadd(IC_GLOBE, apps[i].name, NULL, 0, i);
      xadd(IC_BELL, _("Notifiche"), xa_top, 4, -1);
      break;
    case XC_FRIENDS:
      xadd(IC_FRIENDS, _("Amici e messaggi"), xa_top, 5, -1);
      xadd(IC_PARTY, _("Party"), xa_party, 0, -1);
      for (int i = 0; i < S.nfriends && nxit < 40; i++) if (friend_online(&S.friends[i])) xadd(IC_USER, S.friends[i].oid, xa_profile, i, -1);
      break;
  }
  if (xsel[c] >= nxit) xsel[c] = nxit ? nxit - 1 : 0;
}

// onde dello sfondo: tre nastri sinusoidali che scorrono piano
static void waves(int a) {
  Col base = mix(C_ACC2, C_WHITE, 0.25f);
  for (int w = 0; w < 3; w++) {
    float ph = (float)g_time * (0.35f + w * 0.12f) + w * 1.7f, amp = 46 + w * 18, yc = 640 + w * 26;
    int alpha = a * (24 - w * 5) / 100;
    SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(R, base.r, base.g, base.b, (Uint8)alpha);
    int py = 0;
    for (int x = 0; x <= SCREEN_W; x += 12) {
      int y = (int)(yc + sinf(x * 0.0032f + ph) * amp + sinf(x * 0.0071f - ph * 1.3f) * amp * 0.35f);
      if (x) for (int t = 0; t < 3 + w; t++) SDL_RenderDrawLine(R, x - 12, py + t * 3, x, y + t * 3);
      py = y;
    }
  }
}

static const struct { int ic; const char *name; } XCATS[XC_N] = {
  { IC_USER, N_("Utenti") }, { IC_GEAR, N_("Impostazioni") }, { IC_MUSIC, N_("Musica") }, { IC_GAMEPAD, N_("Giochi") }, { IC_GLOBE, N_("Rete") }, { IC_FRIENDS, N_("Amici") },
};

static void xmb_draw(int a) {
  grad_v(0, 0, SCREEN_W, SCREEN_H, RGB(4, 8, 22), a * 35 / 100, mix(C_ACC, RGB(4, 8, 22), 0.6f), a * 45 / 100);
  waves(a);
  // orologio in alto a destra, come sulla PS3
  char clock[64], date[64]; clock_text(clock, sizeof clock); date_text(date, sizeof date);
  draw_text(font(W_LIGHT, 34), clock, SCREEN_W - 110, 60, C_WHITE, a, AL_R);
  draw_text(font(W_REG, 22), date, SCREEN_W - 110, 104, C_DIM, a, AL_R);
  xbuild(xcat);
  xcat_f = approach(xcat_f, (float)xcat, 12.0f);
  const int cx0 = 520, cy = 300, step = 210;
  for (int c = 0; c < XC_N; c++) {
    float d = c - xcat_f;
    int x = cx0 + (int)(d * step);
    if (x < -100 || x > SCREEN_W + 100) continue;
    int on = c == xcat, s = on ? 92 : 64;
    draw_icon(XCATS[c].ic, x, cy, s, C_WHITE, on ? a : a * 55 / 100);
    if (on) { glow(x, cy, 120, C_WHITE, a * 10 / 100); draw_text(font(W_REG, 26), _(XCATS[c].name), x, cy + 64, C_WHITE, a, AL_C); }
  }
  // voci della categoria: quelle prima salgono sopra la barra, la scelta e le
  // successive scendono sotto
  int n = nxit, s0 = xsel[xcat];
  xsel_f[xcat] = approach(xsel_f[xcat], (float)s0, 14.0f);
  float f = xsel_f[xcat];
  for (int i = 0; i < n; i++) {
    float d = i - f;
    int y = d < -0.01f ? cy - 120 + (int)(d * 90) : cy + 170 + (int)(d * 96) + (i == s0 ? 0 : 40);
    if (y < -80 || y > SCREEN_H) continue;
    XItem *it = &xit[i];
    int on = i == s0, isz = on ? 110 : 76, x = cx0;
    int al = d < -0.01f ? a * 45 / 100 : on ? a : a * 70 / 100;
    if (it->app >= 0) home_tile(it->app, x - isz / 2, y - isz / 2, isz, al);
    else { fill_circle(x, y, isz / 2, RGB(255, 255, 255), al * 10 / 100); draw_icon(it->ic, x, y, isz / 2, C_WHITE, al); }
    draw_text_fit(font(on ? W_MED : W_REG, on ? 34 : 26), it->name, x + isz / 2 + 34, y - (on ? 22 : 16), 1100, C_WHITE, al, AL_L);
    if (on && it->app >= 0) draw_text_fit(font(W_REG, 22), sub_of(&apps[it->app]), x + isz / 2 + 34, y + 20, 1000, C_DIM, a, AL_L);
  }
  if (xcat == XC_GAMES || xcat == XC_NET) home_jobs(SCREEN_W - 420, 780, 150, a * 85 / 100);
}

static int xmb_input(int b) {
  xbuild(xcat);
  int *s = &xsel[xcat];
  if (b == B_LEFT && xcat > 0) { xcat--; sfx_play(SFX_MOVE); }
  else if (b == B_RIGHT && xcat < XC_N - 1) { xcat++; sfx_play(SFX_MOVE); }
  else if (b == B_UP && *s > 0) { (*s)--; sfx_play(SFX_MOVE); }
  else if (b == B_DOWN && *s < nxit - 1) { (*s)++; sfx_play(SFX_MOVE); }
  else if ((b == B_X || b == B_R1) && *s < nxit) {
    XItem *it = &xit[*s];
    if (it->app >= 0) { home_set_sel(it->app); if (b == B_X) launch_app(it->app); else home_more(it->app); }
    else if (b == B_X && it->act) { sfx_play(SFX_SELECT); it->act(it->arg); }
  } else return 0;
  // la voce scelta tra i giochi guida lo sfondo
  if (*s < nxit && xit[*s].app >= 0) home_set_sel(xit[*s].app);
  return 1;
}

// ---------------------------------------------------------------- Griglia --
#define GR_COLS 8
static float gr_scroll;
static void grid_draw(int a) {
  status_corner(a);
  if (!nrow) { empty_home(a); return; }
  int sel = home_selected();
  AppEntry *ap = &apps[sel];
  draw_text_fit(font(W_LIGHT, 60), ap->name, 110, 120, 1300, C_WHITE, a, AL_L);
  draw_text_fit(font(W_REG, 24), sub_of(ap), 112, 196, 1000, C_DIM, a, AL_L);
  const int s = 176, gap = 26, x0 = (SCREEN_W - (GR_COLS * s + (GR_COLS - 1) * gap)) / 2, top = 270;
  int row = sel / GR_COLS;
  gr_scroll = approach(gr_scroll, (float)(row > 1 ? (row - 1) * (s + gap + 36) : 0), 12.0f);
  SDL_Rect clip = { 0, top - 20, SCREEN_W, SCREEN_H - top - 90 }; SDL_RenderSetClipRect(R, &clip);
  for (int i = 0; i < nrow; i++) {
    int x = x0 + (i % GR_COLS) * (s + gap), y = top + (i / GR_COLS) * (s + gap + 36) - (int)gr_scroll;
    if (y + s < top - 20 || y > SCREEN_H) continue;
    int on = i == sel, g = on ? 14 : 0;
    if (on) shadow_rrect(x - g / 2, y - g / 2, s + g, s + g, 24, 26, a * 70 / 100);
    home_tile(i, x - g / 2, y - g / 2, s + g, a);
    if (on) focus_ring(x - g / 2, y - g / 2, s + g, s + g, 24, 0.5f + 0.5f * sinf((float)g_time * 3.2f), a);
    if (g_prefs.labels || on) draw_text_fit(font(on ? W_MED : W_REG, 19), apps[i].name, x + s / 2, y + s + 10, s + 20, on ? C_WHITE : C_DIM, a, AL_C);
  }
  SDL_RenderSetClipRect(R, NULL);
  home_jobs(SCREEN_W - 380, 110, 120, a * 85 / 100);
}
static int grid_input(int b) {
  int sel = home_selected();
  if (b == B_LEFT && sel % GR_COLS) home_set_sel(sel - 1);
  else if (b == B_RIGHT && sel % GR_COLS != GR_COLS - 1 && sel + 1 < nrow) home_set_sel(sel + 1);
  else if (b == B_UP && sel >= GR_COLS) home_set_sel(sel - GR_COLS);
  else if (b == B_DOWN && sel + GR_COLS < nrow) home_set_sel(sel + GR_COLS);
  else if (b == B_DOWN && sel / GR_COLS < (nrow - 1) / GR_COLS) home_set_sel(nrow - 1);
  else if (b == B_X && nrow) launch_app(sel);
  else if (b == B_R1 && nrow) home_more(sel);
  else return 0;
  if (b != B_X && b != B_R1) sfx_play(SFX_MOVE);
  return 1;
}

// -------------------------------------------------------------- Carosello --
static void carousel_draw(int a) {
  status_corner(a);
  if (!nrow) { empty_home(a); return; }
  int sel = home_selected();
  sel_f = approach(sel_f, (float)sel, 9.0f);
  const int cy = 470, S0 = 400;
  // dal più lontano al più vicino, così quello al centro sta sopra
  int order[15], no = 0;
  for (int d = 7; d >= 1; d--) { int l = (int)roundf(sel_f) - d, r = (int)roundf(sel_f) + d; if (l >= 0) order[no++] = l; if (r < nrow) order[no++] = r; }
  if ((int)roundf(sel_f) < nrow) order[no++] = (int)roundf(sel_f);
  for (int k = 0; k < no; k++) {
    int i = order[k]; float d = i - sel_f, ad = fabsf(d);
    float sc = powf(0.74f, ad);
    int s = (int)(S0 * sc), x = SCREEN_W / 2 + (int)(d * 250 * (1.0f - 0.08f * ad)) - s / 2, y = cy - s / 2;
    if (x + s < 0 || x > SCREEN_W) continue;
    int al = (int)(a * clampf(1.0f - ad * 0.22f, 0.15f, 1));
    if (ad < 0.5f) shadow_rrect(x, y, s, s, 30, 40, a * 70 / 100);
    home_tile(i, x, y, s, al);
    // riflesso sul pavimento
    AppEntry *ap = &apps[i];
    if (ap->tex && !ap->builtin) {
      SDL_SetTextureAlphaMod(ap->tex, (Uint8)(al * 22 / 100));
      SDL_Rect dst = { x, y + s + 8, s, s / 2 };
      SDL_Rect src; int tw, th; SDL_QueryTexture(ap->tex, NULL, NULL, &tw, &th); src = (SDL_Rect){ 0, th / 2, tw, th / 2 };
      SDL_RenderCopyEx(R, ap->tex, &src, &dst, 0, NULL, SDL_FLIP_VERTICAL);
      SDL_SetTextureAlphaMod(ap->tex, 255);
    }
  }
  grad_v(0, cy + S0 / 2 + 20, SCREEN_W, 220, RGB(4, 8, 18), 0, RGB(4, 8, 18), a * 80 / 100);
  AppEntry *ap = &apps[sel];
  draw_text_fit(font(W_LIGHT, 54), ap->name, SCREEN_W / 2, cy + S0 / 2 + 80, 1400, C_WHITE, a, AL_C);
  draw_text_fit(font(W_REG, 24), sub_of(ap), SCREEN_W / 2, cy + S0 / 2 + 150, 1000, C_DIM, a, AL_C);
  home_jobs(110, 110, 120, a * 85 / 100);
}
static int carousel_input(int b) {
  int sel = home_selected();
  if (b == B_LEFT && sel > 0) { home_set_sel(sel - 1); sfx_play(SFX_MOVE); }
  else if (b == B_RIGHT && sel < nrow - 1) { home_set_sel(sel + 1); sfx_play(SFX_MOVE); }
  else if (b == B_X && nrow) launch_app(sel);
  else if (b == B_R1 && nrow) home_more(sel);
  else return 0;
  return 1;
}

// ----------------------------------------------------------------- Cinema --
static float ci_scroll;
static void cinema_draw(int a) {
  grad_h(0, 0, 900, SCREEN_H, RGB(4, 6, 14), a * 90 / 100, RGB(4, 6, 14), 0);
  status_corner(a);
  draw_text(font(W_LIGHT, 34), _("La tua libreria"), 110, 60, C_DIM, a, AL_L);
  if (!nrow) { empty_home(a); return; }
  int sel = home_selected();
  const int top = 150, rh = 78, vis = 10;
  ci_scroll = approach(ci_scroll, (float)(sel > vis / 2 ? (sel - vis / 2) * rh : 0), 12.0f);
  SDL_Rect clip = { 0, top - 10, 860, rh * vis + 20 }; SDL_RenderSetClipRect(R, &clip);
  for (int i = 0; i < nrow; i++) {
    int y = top + i * rh - (int)ci_scroll;
    if (y + rh < top - 10 || y > top + rh * vis + 10) continue;
    int on = i == sel;
    if (on) { fill_rrect(96, y - 4, 720, rh - 6, 18, C_WHITE, a * 12 / 100); fill_rrect(96, y + 10, 5, rh - 34, 2, C_ACC2, a); }
    home_tile(i, 118, y + 4, 58, on ? a : a * 70 / 100);
    draw_text_fit(font(on ? W_MED : W_REG, on ? 30 : 27), apps[i].name, 198, y + 8, 590, on ? C_WHITE : C_DIM, a, AL_L);
    if (on) draw_text_fit(font(W_REG, 19), sub_of(&apps[i]), 198, y + 44, 590, C_FAINT, a, AL_L);
  }
  SDL_RenderSetClipRect(R, NULL);
  // il gioco scelto, in grande a destra
  AppEntry *ap = &apps[sel];
  int s = 420, x = SCREEN_W - s - 160, y = 230;
  shadow_rrect(x, y, s, s, 34, 50, a * 80 / 100);
  home_tile(sel, x, y, s, a);
  draw_text_fit(font(W_LIGHT, 56), ap->name, x + s, y + s + 40, 900, C_WHITE, a, AL_R);
  draw_text_fit(font(W_REG, 24), sub_of(ap), x + s, y + s + 112, 800, C_DIM, a, AL_R);
  home_jobs(980, 820, 120, a * 85 / 100);
}
static int cinema_input(int b) {
  int sel = home_selected();
  if (b == B_UP && sel > 0) { home_set_sel(sel - 1); sfx_play(SFX_MOVE); }
  else if (b == B_DOWN && sel < nrow - 1) { home_set_sel(sel + 1); sfx_play(SFX_MOVE); }
  else if (b == B_LEFT) { home_set_sel(sel > 5 ? sel - 5 : 0); sfx_play(SFX_MOVE); }
  else if (b == B_RIGHT) { home_set_sel(sel + 5 < nrow ? sel + 5 : nrow - 1); sfx_play(SFX_MOVE); }
  else if (b == B_X && nrow) launch_app(sel);
  else if (b == B_R1 && nrow) home_more(sel);
  else return 0;
  return 1;
}

// ----------------------------------------------------------------- ingresso --
void hs_draw(void) {
  int a = 255;
  switch (g_prefs.home_style) {
    case 1: ps4_draw(a); break;
    case 2: xmb_draw(a); break;
    case 3: grid_draw(a); break;
    case 4: carousel_draw(a); break;
    default: cinema_draw(a); break;
  }
  hints_alt(a, NULL);
}

int hs_input(int b) {
  if (home_launching() >= 0) return 1;
  switch (g_prefs.home_style) {
    case 1: return ps4_input(b);
    case 2: return xmb_input(b);
    case 3: return grid_input(b);
    case 4: return carousel_input(b);
    default: return cinema_input(b);
  }
}
