// Omega UI — avvio, ciclo principale, input, pila dei pannelli, toast e
// messaggi, sessione e comandi di debug da file.
// Flusso: splash → (sessione valida ? scelta utente : accesso) → home.
#include "app.h"
#include <math.h>
#include <signal.h>
#include <SDL_image.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_OVERLAYS    8
#define MAX_TOASTS      4
#define TOAST_LIFE      5.0f
#define MSG_SECONDS     3.2f
#define REPEAT_DELAY_MS 380     // ripetizione delle direzioni tenute premute
#define REPEAT_MS       85
#define STICK_ON        20000   // stick sinistro come croce, con isteresi
#define STICK_OFF       12000
#define FRAME_MS        16

Scene g_scene = SC_SPLASH;
static float scene_t = 1;           // dissolvenza tra scene
static Scene scene_prev = SC_SPLASH;
static int running = 1;
char f_user[64], f_pass[128], f_email[96], f_confirm[128];
static char g_msg[512]; static int g_msg_err; static float g_msg_t;

// ----------------------------------------------------------------- pannelli --
typedef struct { Overlay o; float t; int closing; } OvSlot;
static OvSlot ovs[MAX_OVERLAYS]; static int novs;

void ov_push(Overlay o) {
  if (novs >= MAX_OVERLAYS) return;
  ovs[novs].o = o; ovs[novs].t = 0; ovs[novs].closing = 0; novs++;
}
void ov_pop(void) {
  for (int i = novs - 1; i >= 0; i--) if (!ovs[i].closing) { ovs[i].closing = 1; break; }
}
Overlay ov_top(void) {
  for (int i = novs - 1; i >= 0; i--) if (!ovs[i].closing) return ovs[i].o;
  return OV_NONE;
}
int ov_depth(void) { int n = 0; for (int i = 0; i < novs; i++) n += !ovs[i].closing; return n; }
void ov_clear(void) { for (int i = 0; i < novs; i++) ovs[i].closing = 1; }

static void ov_update(void) {
  for (int i = 0; i < novs; i++) {
    ovs[i].t = approach(ovs[i].t, ovs[i].closing ? 0 : 1, ovs[i].closing ? 16.0f : 11.0f);
  }
  int w = 0;
  for (int i = 0; i < novs; i++) {
    if (ovs[i].closing && ovs[i].t <= 0.01f) {
      if (ovs[i].o == OV_CHAT) CH.open = 0;
      continue;
    }
    ovs[w++] = ovs[i];
  }
  novs = w;
}

static void ov_draw_one(Overlay o, float t) {
  switch (o) {
    case OV_CC: cc_draw(t); break;
    case OV_GAMEBASE: gb_draw(t); break;
    case OV_NOTIF: notif_draw(t); break;
    case OV_PROFILE: profile_draw(t); break;
    case OV_CHAT: chat_draw(t); break;
    case OV_SEARCH: search_draw(t); break;
    case OV_MENU: menu_draw(t); break;
    case OV_CONFIRM: confirm_draw(t); break;
    case OV_AVATAR: avatar_draw(t); break;
    case OV_SETTINGS: settings_draw(t); break;
    case OV_NEWS: news_draw(t); break;
    case OV_BROWSER: browser_draw(t); break;
    case OV_GALLERY: gallery_draw(t); break;
    case OV_STORE: store_draw(t); break;
    case OV_DOC: doc_draw(t); break;
    case OV_COMMUNITY: community_draw(t); break;
    case OV_TROPHIES: trophies_draw(t); break;
    case OV_ABOUT: about_draw(t); break;
    case OV_MUSIC: music_draw(t); break;
    case OV_SYSTEM: system_draw(t); break;
    case OV_FILES: files_draw(t); break;
    case OV_REMOTE: remote_draw(t); break;
    case OV_SETUP: setup_draw(t); break;
    case OV_WHATSNEW: whatsnew_draw(t); break;
    case OV_CUSTOM: custom_draw(t); break;
    default: break;
  }
}

// -------------------------------------------------------------------- toast --
typedef struct { int icon; char actor[32]; int avatar; char title[200]; char body[260]; float t; float life; int active; } Toast;
static Toast toasts[MAX_TOASTS];

void toast(int icon, const char *actor, int avatar, const char *title, const char *body) {
  if (g_prefs.dnd == 2) return;   // Non disturbare › Nascoste: restano nel centro notifiche
  for (int i = MAX_TOASTS - 1; i > 0; i--) toasts[i] = toasts[i - 1];   // il più vecchio esce
  Toast *t = &toasts[0]; memset(t, 0, sizeof *t);
  t->icon = icon; t->avatar = avatar; t->active = 1; t->life = pref_toast_life();
  if (!g_prefs.dnd) sfx_play(SFX_NOTIFY);
  snprintf(t->actor, sizeof t->actor, "%s", actor ? actor : "");
  snprintf(t->title, sizeof t->title, "%s", title ? title : "");
  snprintf(t->body, sizeof t->body, "%s", body ? body : "");
}

static void toasts_draw(void) {
  // Personalizza › Posizione delle notifiche: alto a destra, alto a sinistra, basso a destra
  int pos = g_prefs.toast_pos, left = pos == 1, bottom = pos == 2;
  int y = bottom ? SCREEN_H - 170 : 34;
  for (int i = 0; i < MAX_TOASTS; i++) {
    Toast *t = &toasts[i];
    if (!t->active) continue;
    t->life -= g_dt;
    t->t = approach(t->t, t->life > 0 ? 1 : 0, t->life > 0 ? 9.0f : 12.0f);
    if (t->life <= 0 && t->t < 0.02f) { t->active = 0; continue; }
    int w = 600, h = t->body[0] ? 128 : 100;
    float e = ease_out(t->t);
    int x = left ? 40 + (int)(w * e) - w - (int)((1 - e) * 60) : SCREEN_W - 40 - (int)(w * e) + (int)((1 - e) * 60);
    if (bottom) y -= (int)(h * e);
    int a = (int)(255 * t->t);
    shadow_rrect(x, y, w, h, 22, 24, a * 60 / 100);
    fill_rrect(x, y, w, h, 22, C_PANEL, pref_panel_alpha(a * 96 / 100));
    stroke_rrect(x, y, w, h, 22, 1, RGB(70, 80, 105), a / 2);
    int cx = x + 66, cy = y + h / 2;
    if (t->actor[0]) {
      draw_avatar(t->actor, t->avatar, cx, cy, 72, a);
      fill_circle(cx + 26, cy + 26, 17, C_ACC, a);
      draw_icon(t->icon, cx + 26, cy + 26, 20, C_WHITE, a);
    } else {
      fill_circle(cx, cy, 36, C_ACC, a);
      draw_icon(t->icon, cx, cy, 40, C_WHITE, a);
    }
    int tx = x + 124, tw = w - 150;
    if (t->body[0]) {
      draw_text_fit(font(W_MED, 27), t->title, tx, y + 26, tw, C_TXT, a, AL_L);
      draw_text_fit(font(W_REG, 24), t->body, tx, y + 68, tw, C_DIM, a, AL_L);
    } else {
      draw_text_wrap(font(W_MED, 26), t->title, tx, y + 18, tw, 2, 32, C_TXT, a);
    }
    if (t->life > 0) fill_rrect(x + 24, y + h - 8, (int)((w - 48) * (t->life / pref_toast_life())), 3, 2, C_ACC2, a * 70 / 100);
    if (bottom) y -= (int)(16 * e); else y += (int)((h + 16) * e);
  }
}

// ------------------------------------------------------- messaggio in basso --
void set_msg(const char *m, int err) {
  snprintf(g_msg, sizeof g_msg, "%s", m ? m : "");
  g_msg_err = err; g_msg_t = g_msg[0] ? MSG_SECONDS : 0;
}
static void msg_draw(void) {
  static float vis = 0;
  if (g_msg_t > 0) g_msg_t -= g_dt;
  vis = approach(vis, g_msg_t > 0 && g_msg[0] ? 1 : 0, 10.0f);
  if (vis < 0.01f) return;
  TTF_Font *f = font(W_MED, 26);
  int w = text_w(f, g_msg) + 100, h = 64;
  int x = (SCREEN_W - w) / 2, y = SCREEN_H - 150 + (int)((1 - ease_out(vis)) * 40);
  int a = (int)(255 * vis);
  shadow_rrect(x, y, w, h, 32, 18, a / 2);
  fill_rrect(x, y, w, h, 32, g_msg_err ? RGB(120, 30, 40) : RGB(30, 36, 52), a * 95 / 100);
  fill_circle(x + 34, y + 32, 13, g_msg_err ? C_ERR : C_OK, a);
  draw_icon(g_msg_err ? IC_CLOSE : IC_CHECK, x + 34, y + 32, 18, C_WHITE, a);
  draw_text(f, g_msg, x + 62, y + (h - TTF_FontHeight(f)) / 2, C_TXT, a, AL_L);
}

// ------------------------------------------------------------ barra comandi --
static int hint_ok = 1;   // solo il pannello in primo piano mostra i suoi comandi
void hints(const int *icons, const char **labels, int n, int alpha) {
  if (!hint_ok) return;
  TTF_Font *f = font(W_REG, 24);
  int total = 0;
  for (int i = 0; i < n; i++) total += 40 + text_w(f, labels[i]) + 36;
  int x = SCREEN_W - 60 - total, y = SCREEN_H - 62;
  for (int i = 0; i < n; i++) {
    int ic = icons[i];
    if (ic == IC_BTN_OPT) {
      fill_rrect(x, y + 2, 34, 26, 8, RGB(255, 255, 255), alpha * 22 / 100);
      draw_icon(IC_BTN_OPT, x + 17, y + 15, 22, C_TXT, alpha);
    } else if (ic >= 0) {
      ring(x + 15, y + 15, 15, 2, C_TXT, alpha * 70 / 100);
      draw_icon(ic, x + 15, y + 15, 20, C_TXT, alpha);
    }
    x += 42;
    x += draw_text(f, labels[i], x, y + 15 - TTF_FontHeight(f) / 2, C_DIM, alpha, AL_L) + 36;
  }
}

// ----------------------------------------------------------------- sessione --
void scene_set(Scene s) {
  if (s == g_scene) return;
  scene_prev = g_scene; g_scene = s; scene_t = 0;
  if (s == SC_HOME) home_enter();
}

void after_login(JVal *j) {
  jcpy(g_token, sizeof g_token, j, "token");
  char oid[64], acc[32], exp[64];
  jcpy(oid, sizeof oid, j, "online_id"); jcpy(acc, sizeof acc, j, "account_id"); jcpy(exp, sizeof exp, j, "expires_at");
  session_save(g_token, oid, acc, exp);
  social_reset();
  snprintf(S.me, sizeof S.me, "%s", oid);
  memset(f_pass, 0, sizeof f_pass); memset(f_confirm, 0, sizeof f_confirm);
  social_presence("online", NULL, NULL);
  social_sync_now();
  terms_refresh();
  scene_set(SC_HOME);
}

void do_logout(void) {
  if (g_token[0]) {
    net_req(HTTP_POST, OMEGA_API "/presence", "{\"status\":\"offline\"}", NULL, NULL);
    net_req(HTTP_POST, OMEGA_API "/auth/logout", NULL, NULL, NULL);
  }
  // le due richieste sopra partono col token attuale; le risposte in arrivo si scartano
  g_net_gen++;
  consync_stop();
  session_clear(); g_token[0] = 0;
  social_reset(); ov_clear();
  memset(f_pass, 0, sizeof f_pass);
  bg_set_default();
  scene_set(SC_LOGIN);
}

// Uscita dopo l'avvio di un gioco: niente presenza "offline", perché sta giocando.
static Uint32 quit_at;
void app_quit_later(Uint32 ms) { quit_at = SDL_GetTicks() + ms; }

void app_quit(void) {
  consync_stop();
  if (g_token[0]) {
    char buf[512]; omega_http(HTTP_POST, OMEGA_API "/presence", g_token, "{\"status\":\"offline\"}", buf, sizeof buf);
  }
  running = 0;
}

// La sessione salvata si verifica durante lo splash.
int g_boot_state = 0;
static void on_me(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; (void)ud;
  if (st == 200 && j) {
    jcpy(S.me, sizeof S.me, j, "online_id");
    terms_check(j);
    g_boot_state = 1;
    social_sync_now();   // avatar e contatori subito
  } else {
    if (st == 401) session_clear();
    g_token[0] = 0; g_boot_state = 2;
    if (st < 0) set_msg(_("Server Omega non raggiungibile"), 1);
  }
}
void boot_session(void) {
  char oid[64];
  session_load(g_token, sizeof g_token, oid, sizeof oid);
  if (!g_token[0]) { g_boot_state = 2; return; }
  snprintf(S.me, sizeof S.me, "%s", oid);
  net_req(HTTP_GET, OMEGA_API "/me", NULL, on_me, NULL);
}

// ----------------------------------------------------------------- tastiera --
// Testi per le prossime tastiere (comando di debug "text"): una piccola coda,
// per i passaggi che chiedono più testi di fila (es. indirizzo e nome).
static char dbg_text[4][512]; static int dbg_text_set;
static void input_reset(void);
static void ime_frame(void) { render_frame(); SDL_RenderPresent(R); }
int edit_text(const char *title, char *buf, size_t n, int pw) {
  if (dbg_text_set) {
    snprintf(buf, n, "%s", dbg_text[0]);
    for (int i = 1; i < dbg_text_set; i++) memcpy(dbg_text[i - 1], dbg_text[i], sizeof dbg_text[0]);
    dbg_text_set--;
    return 1;
  }
  char tmp[512] = "";
  int ok = ime_input(title, buf, pw, tmp, sizeof tmp, ime_frame);
  input_reset();   // i tasti premuti prima o durante la tastiera non devono restare incastrati
  if (ok) { snprintf(buf, n, "%s", tmp); return 1; }
  return 0;
}

// -------------------------------------------------------------------- input --
static void dispatch(int b) {
  Overlay o = ov_top();
  if (g_scene != SC_SPLASH && o != OV_NONE) {
    switch (o) {
      case OV_CC: cc_input(b); break;
      case OV_GAMEBASE: gb_input(b); break;
      case OV_NOTIF: notif_input(b); break;
      case OV_PROFILE: profile_input(b); break;
      case OV_CHAT: chat_input(b); break;
      case OV_SEARCH: search_input(b); break;
      case OV_MENU: menu_input(b); break;
      case OV_CONFIRM: confirm_input(b); break;
      case OV_AVATAR: avatar_input(b); break;
      case OV_SETTINGS: settings_input(b); break;
      case OV_NEWS: news_input(b); break;
      case OV_BROWSER: browser_input(b); break;
      case OV_GALLERY: gallery_input(b); break;
      case OV_STORE: store_input(b); break;
      case OV_DOC: doc_input(b); break;
      case OV_COMMUNITY: community_input(b); break;
      case OV_TROPHIES: trophies_input(b); break;
      case OV_ABOUT: about_input(b); break;
      case OV_MUSIC: music_input(b); break;
      case OV_SYSTEM: system_input(b); break;
      case OV_FILES: files_input(b); break;
      case OV_REMOTE: remote_input(b); break;
      case OV_SETUP: setup_input(b); break;
      case OV_WHATSNEW: whatsnew_input(b); break;
      case OV_CUSTOM: custom_input(b); break;
      default: break;
    }
    return;
  }
  switch (g_scene) {
    case SC_SPLASH: break;
    case SC_USERS: users_input(b); break;
    case SC_LOGIN: login_input(b); break;
    case SC_REGISTER: register_input(b); break;
    case SC_HOME: home_input(b); break;
  }
}

static int held = -1; static Uint32 held_at, held_next;
// ------------------------------------------------------------ salvaschermo --
// Personalizza › Salvaschermo: dopo N minuti senza tasti, sulla home.
// Orologio grande che si sposta piano (niente segni sugli schermi OLED),
// copertine dei giochi che scorrono, o schermo nero. Un tasto qualsiasi torna.
Uint32 g_last_input;
static int saver_on; static float saver_t;
static int saver_due(void) {
  static const int MIN[4] = { 0, 2, 5, 10 };
  int m = MIN[g_prefs.saver_min & 3];
  return m && g_scene == SC_HOME && SDL_GetTicks() - g_last_input > (Uint32)m * 60000u;
}
static void saver_draw(void) {
  if (!saver_on && saver_due()) { saver_on = 1; omega_log("salvaschermo"); }
  saver_t = approach(saver_t, saver_on ? 1 : 0, 2.5f);
  if (saver_t < 0.01f) return;
  int a = (int)(255 * saver_t);
  fill_rect(0, 0, SCREEN_W, SCREEN_H, C_BLACK, g_prefs.saver_style == 2 ? a : a * 92 / 100);
  if (g_prefs.saver_style == 2) return;
  float tt = (float)g_time;
  // posizione che vaga lentamente
  int cx = SCREEN_W / 2 + (int)(520 * sinf(tt * 0.031f)), cy = SCREEN_H / 2 + (int)(260 * sinf(tt * 0.047f + 1.3f));
  if (g_prefs.saver_style == 1 && napps) {
    // copertine: una alla volta, ogni 8 s, con dissolvenza
    int i = (int)(tt / 8.0f) % napps; float ph = fmodf(tt, 8.0f), k = ph < 1 ? ph : ph > 7 ? 8 - ph : 1;
    AppEntry *ap = &apps[i];
    int s = 340;
    glow(cx, cy, s, ap->avg, (int)(a * 0.5f * k));
    if (ap->tex) draw_tex(ap->tex, cx - s / 2, cy - s / 2, s, s, (int)(a * k));
    draw_text(font(W_LIGHT, 40), ap->name, cx, cy + s / 2 + 30, C_WHITE, (int)(a * k), AL_C);
    char c[64]; clock_text(c, sizeof c);
    draw_text(font(W_LIGHT, 44), c, SCREEN_W - 90, SCREEN_H - 110, C_DIM, a, AL_R);
    return;
  }
  char c[64], d[96]; clock_text(c, sizeof c); date_text(d, sizeof d);
  draw_text(font(W_LIGHT, 150), c, cx, cy - 110, C_WHITE, a * 90 / 100, AL_C);
  draw_text(font(W_REG, 40), d, cx, cy + 70, C_DIM, a, AL_C);
  if (music_now_line()) draw_text_fit(font(W_REG, 30), music_now_line(), cx, cy + 140, 900, C_ACC2, a, AL_C);
}

static void press(int b) {
  g_last_input = SDL_GetTicks();
  if (saver_on) { saver_on = 0; return; }   // il tasto che sveglia non fa altro
  sfx_play(b <= B_RIGHT ? SFX_MOVE : b == B_X ? SFX_SELECT : b == B_O ? SFX_BACK : SFX_OPEN);
  dispatch(b);
  if (b <= B_RIGHT) { held = b; held_at = SDL_GetTicks(); held_next = held_at + REPEAT_DELAY_MS; }
}
static void release(int b) { if (held == b) held = -1; }
static int stick_dir;
static void input_reset(void) {
  held = -1; stick_dir = -1;
  SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
}
static void repeat_tick(void) {
  if (held < 0) return;
  Uint32 now = SDL_GetTicks();
  if (now >= held_next) { sfx_play(SFX_MOVE); dispatch(held); held_next = now + REPEAT_MS; }
}

static int map_button(int b) {
  switch (b) {
    case SDL_CONTROLLER_BUTTON_DPAD_UP: return B_UP;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return B_DOWN;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return B_LEFT;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return B_RIGHT;
    case SDL_CONTROLLER_BUTTON_A: return B_X;
    case SDL_CONTROLLER_BUTTON_B: return B_O;
    case SDL_CONTROLLER_BUTTON_Y: return B_TRI;
    case SDL_CONTROLLER_BUTTON_X: return B_SQ;
    case SDL_CONTROLLER_BUTTON_START: return B_OPT;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return B_L1;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return B_R1;
    default: return -1;
  }
}
static int map_key(SDL_Keycode k) {
  switch (k) {
    case SDLK_UP: return B_UP; case SDLK_DOWN: return B_DOWN;
    case SDLK_LEFT: return B_LEFT; case SDLK_RIGHT: return B_RIGHT;
    case SDLK_RETURN: case SDLK_x: return B_X;
    case SDLK_ESCAPE: case SDLK_BACKSPACE: case SDLK_o: return B_O;
    case SDLK_t: return B_TRI; case SDLK_q: return B_SQ;
    case SDLK_TAB: return B_OPT; case SDLK_1: return B_L1; case SDLK_2: return B_R1;
    default: return -1;
  }
}

static void stick(int axis, int value) {
  static int ax = 0, ay = 0;
  if (axis == SDL_CONTROLLER_AXIS_LEFTX) ax = value; else if (axis == SDL_CONTROLLER_AXIS_LEFTY) ay = value; else return;
  int d = -1;
  if (abs(ax) > STICK_ON || abs(ay) > STICK_ON) d = abs(ax) > abs(ay) ? (ax > 0 ? B_RIGHT : B_LEFT) : (ay > 0 ? B_DOWN : B_UP);
  else if (abs(ax) > STICK_OFF || abs(ay) > STICK_OFF) return;
  if (d != stick_dir) { if (stick_dir >= 0) release(stick_dir); stick_dir = d; if (d >= 0) press(d); }
}

// -------------------------------------------------------------- primo piano --
// La UI aggiorna ui-active solo quando è in primo piano: il demone omega_redirect
// allora le lascia presenza e notifiche, e le riprende quando c'è un gioco davanti.
#ifdef PS5
int sceSystemServiceGetAppIdOfRunningBigApp(void);
int sceSystemServiceGetAppTitleId(int appId, char *titleId);
#endif
char g_host_tid[64];   // titolo che ospita la UI: non va chiuso all'avvio di un gioco
int g_ui_fg = 1; Uint32 g_ui_fg_since;   // UI in primo piano, e da quando (voice.c)
static void foreground_tick(void) {
  static Uint32 last; static int was_fg = 1, inited; char *host = g_host_tid;
  Uint32 now = SDL_GetTicks();
  if (now - last < 2000) return;
  last = now;
  char tid[64] = "";
#ifdef PS5
  int app = sceSystemServiceGetAppIdOfRunningBigApp();
  if (app >= 0) sceSystemServiceGetAppTitleId(app, tid);
#endif
  if (!inited) { snprintf(host, sizeof g_host_tid, "%s", tid); inited = 1; }
  int fg = !strcmp(tid, host);
  audio_pause(!fg);
  if (fg && !g_ui_fg) g_ui_fg_since = now;
  g_ui_fg = fg;
  // fuori dal primo piano ui-active si toglie subito: il servizio riprende
  // presenza, notifiche e voce del party senza aspettare che invecchi
  if (!fg && was_fg) unlink(OMEGA_UI_ACTIVE);
  if (fg) {
    FILE *f = fopen(OMEGA_UI_ACTIVE, "w");
    if (f) { fprintf(f, "%lu\n", (unsigned long)now); fclose(f); }
    if (!was_fg && g_token[0]) { omega_log("di nuovo in primo piano"); social_presence("online", NULL, NULL); social_sync_now(); }
  }
  was_fg = fg;
}

// --------------------------------------------------------- comandi di debug --
// File OMEGA_CMD, una riga per comando: "key x|o|tri|sq|opt|up|down|left|right|l1|r1",
// "text <testo>" (prossima tastiera), "shot [file]", "wait <ms>", "micprobe", "system", "files", "store", "avatar", "quit".
// Il file si cancella appena letto.
static char cmdq[64][160]; static int ncmd, cmd_i; static Uint32 cmd_next, cmd_check;
static int want_shot; static char shot_path[256];

static void cmd_poll(void) {
  Uint32 now = SDL_GetTicks();
  if (cmd_i >= ncmd && now - cmd_check > 300) {
    cmd_check = now;
    FILE *fp = fopen(OMEGA_CMD, "r");
    if (fp) {
      ncmd = cmd_i = 0;
      while (ncmd < 64 && fgets(cmdq[ncmd], sizeof cmdq[0], fp)) {
        cmdq[ncmd][strcspn(cmdq[ncmd], "\r\n")] = 0;
        if (cmdq[ncmd][0]) ncmd++;
      }
      fclose(fp); unlink(OMEGA_CMD);
    }
  }
  if (cmd_i >= ncmd || now < cmd_next) return;
  char *c = cmdq[cmd_i++];
  cmd_next = now + 220;
  if (!strncmp(c, "key ", 4)) {
    const char *k = c + 4;
    static const char *names[B_COUNT] = { "up", "down", "left", "right", "x", "o", "tri", "sq", "opt", "l1", "r1", "l2", "r2" };
    for (int i = 0; i < B_COUNT; i++) if (!strcmp(k, names[i])) dispatch(i);
  } else if (!strncmp(c, "text ", 5)) { if (dbg_text_set < 4) snprintf(dbg_text[dbg_text_set++], sizeof dbg_text[0], "%s", c + 5); }
  else if (!strncmp(c, "wait ", 5)) cmd_next = now + (Uint32)atoi(c + 5);
  else if (!strncmp(c, "shot", 4)) { want_shot = 1; snprintf(shot_path, sizeof shot_path, "%s", c[4] == ' ' ? c + 5 : OMEGA_SHOT); }
  else if (!strcmp(c, "quit")) running = 0;
  else if (!strcmp(c, "micprobe")) mic_probe();
  else if (!strcmp(c, "system")) system_open();
  else if (!strcmp(c, "files")) files_open(NULL);
  else if (!strcmp(c, "store")) store_open();
  else if (!strcmp(c, "avatar")) ov_push(OV_AVATAR);
}

static void save_shot(void) {
  SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, SCREEN_W, SCREEN_H, 32, SDL_PIXELFORMAT_ARGB8888);
  if (!s) return;
  if (SDL_RenderReadPixels(R, NULL, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch) == 0) {
    if (IMG_SavePNG(s, shot_path) != 0) omega_log("shot: %s", IMG_GetError());
    else omega_log("shot salvato %s", shot_path);
  }
  SDL_FreeSurface(s);
}

// ------------------------------------------------------------------ disegno --
static void draw_scene(Scene s) {
  switch (s) {
    case SC_SPLASH: splash_draw(); break;
    case SC_USERS: users_draw(); break;
    case SC_LOGIN: login_draw(); break;
    case SC_REGISTER: register_draw(); break;
    case SC_HOME: home_draw(); break;
  }
}

// Scena congelata. Con un pannello aperto e fermo, sotto non cambia niente: si
// fotografa una volta la scena (già scurita dai veli del pannello) e poi a ogni
// fotogramma si copia la foto e si disegna solo il pannello. Prima si ridisegnava
// tutta la home e la si scuriva a tutto schermo: metà del tempo di un fotogramma.
static SDL_Texture *frz; static int frz_valid, frz_top = -1, frz_n, frz_scene, frz_theme; static Overlay frz_o; static unsigned frz_rev;
static Uint32 frz_at;
static void frz_build(SDL_Surface *s) {
  // i veli annotati mentre si disegnava il pannello, applicati alla foto
  if (SDL_LockSurface(s) == 0) {
    Uint32 *px = s->pixels; int pitch = s->pitch / 4;
    for (int v = 0; v < g_nveils; v++) {
      Veil *w = &g_veils[v];
      int a = w->a > 255 ? 255 : w->a, ia = 255 - a;
      int x0 = w->x < 0 ? 0 : w->x, y0 = w->y < 0 ? 0 : w->y;
      int x1 = w->x + w->w > s->w ? s->w : w->x + w->w, y1 = w->y + w->h > s->h ? s->h : w->y + w->h;
      int cr = w->c.r * a, cg = w->c.g * a, cb = w->c.b * a;
      for (int y = y0; y < y1; y++) {
        Uint32 *row = px + y * pitch;
        for (int x = x0; x < x1; x++) {
          Uint32 p = row[x];
          int r = ((int)((p >> 16) & 255) * ia + cr) / 255, g = ((int)((p >> 8) & 255) * ia + cg) / 255, b = ((int)(p & 255) * ia + cb) / 255;
          row[x] = 0xFF000000u | ((Uint32)r << 16) | ((Uint32)g << 8) | (Uint32)b;
        }
      }
    }
    SDL_UnlockSurface(s);
  }
  if (frz) SDL_DestroyTexture(frz);
  frz = SDL_CreateTextureFromSurface(R, s);
  if (frz) SDL_SetTextureBlendMode(frz, SDL_BLENDMODE_NONE);
}

void render_frame(void) {
  int top = -1, stable = g_scene != SC_SPLASH && scene_t >= 1 && g_prefs.freeze;
  for (int i = 0; i < novs; i++) { if (!ovs[i].closing) top = i; if (ovs[i].closing || ovs[i].t < 0.999f) stable = 0; }
  // in Personalizza la home sotto deve muoversi: è l'anteprima dal vivo
  stable = stable && top >= 0 && ovs[top].o != OV_CUSTOM;
  // la foto vale finché restano uguali pannelli, scena e tema (e al massimo 10 s:
  // sotto, la home si aggiorna da sola)
  int same = stable && frz_valid && frz && frz_top == top && frz_n == novs && frz_o == ovs[top].o && frz_scene == (int)g_scene
             && frz_theme == g_theme && frz_rev == g_prefs_rev && SDL_GetTicks() - frz_at < 10000;
  if (same) {
    draw_tex(frz, 0, 0, SCREEN_W, SCREEN_H, 255);
    g_veil_skip = 1; hint_ok = 1;
    ov_draw_one(ovs[top].o, ovs[top].t);
    g_veil_skip = 0;
  } else {
    frz_valid = 0;
    bg_draw();          // la velatura del tema è già dentro lo sfondo (bake_tint)
    hint_ok = ov_depth() == 0;
    draw_scene(g_scene);
    hint_ok = 1;
    if (scene_t < 1) fill_rect(0, 0, SCREEN_W, SCREEN_H, C_BLACK, (int)(255 * (1 - ease_out(scene_t))));
    if (g_scene != SC_SPLASH) {
      SDL_Surface *shot = NULL;
      for (int i = 0; i < novs; i++) {
        if (stable && i == top) {
          // tutto quello che sta sotto il pannello in cima, da fotografare
          shot = SDL_CreateRGBSurfaceWithFormat(0, SCREEN_W, SCREEN_H, 32, SDL_PIXELFORMAT_ARGB8888);
          if (shot && SDL_RenderReadPixels(R, NULL, SDL_PIXELFORMAT_ARGB8888, shot->pixels, shot->pitch) != 0) { SDL_FreeSurface(shot); shot = NULL; }
          g_nveils = 0; g_veil_rec = shot != NULL;
        }
        hint_ok = i == top; ov_draw_one(ovs[i].o, ovs[i].t);
        g_veil_rec = 0;
      }
      hint_ok = 1;
      if (shot) {
        frz_build(shot); SDL_FreeSurface(shot);
        frz_valid = frz != NULL; frz_top = top; frz_n = novs; frz_o = ovs[top].o; frz_scene = (int)g_scene; frz_theme = g_theme; frz_rev = g_prefs_rev; frz_at = SDL_GetTicks();
      }
    }
  }
  upload_overlay(); files_overlay();
  install_overlay();
  toasts_draw();
  msg_draw();
  saver_draw();
}

#ifdef OMEGA_DIR_LEGACY
// Le prime versioni usavano un'altra cartella dati: si rinomina una volta sola.
// Il demone fa lo stesso controllo, quindi vince chi parte per primo.
static void migrate_data_dir(void) {
  struct stat st;
  if (stat(OMEGA_DIR_LEGACY, &st) == 0 && stat(OMEGA_DIR, &st) != 0 && rename(OMEGA_DIR_LEGACY, OMEGA_DIR) == 0)
    omega_log("cartella dati spostata in %s", OMEGA_DIR);
}
#endif

int main(int argc, char **argv) {
  signal(SIGPIPE, SIG_IGN);   // una connessione chiusa a metà non deve chiudere Omega
  (void)argc; (void)argv;
#ifdef OMEGA_DIR_LEGACY
  migrate_data_dir();
#endif
  omega_log("Omega UI avvio");
  i18n_init();
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) < 0) { omega_log("SDL_Init: %s", SDL_GetError()); return 1; }
#ifdef PS5
  SDL_Window *win = SDL_CreateWindow("Omega", 0, 0, SCREEN_W, SCREEN_H, SDL_WINDOW_FULLSCREEN);
#else
  SDL_Window *win = SDL_CreateWindow("Omega", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SCREEN_W, SCREEN_H, getenv("OMEGA_HIDDEN") ? SDL_WINDOW_HIDDEN : 0);
#endif
  if (!win) { omega_log("CreateWindow: %s", SDL_GetError()); return 1; }
  R = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE | SDL_RENDERER_PRESENTVSYNC);
  if (!R) R = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
  if (!R) { omega_log("CreateRenderer: %s", SDL_GetError()); return 1; }
  SDL_RenderSetLogicalSize(R, SCREEN_W, SCREEN_H);
  if (TTF_Init() < 0) { omega_log("TTF_Init: %s", TTF_GetError()); return 1; }
  IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG);
  for (int i = 0; i < SDL_NumJoysticks(); i++) if (SDL_IsGameController(i)) SDL_GameControllerOpen(i);

  gfx_init();
  if (net_setup() != 0) omega_log("net_setup fallito");
  netq_init();
  loader_init();
  audio_init();
#ifdef OMEGA_UPDATES
  update_init();
#endif
  stick_dir = -1;
  splash_enter();
  boot_session();

  Uint32 last = SDL_GetTicks();
  while (running) {
    Uint32 now = SDL_GetTicks();
    g_dt = (now - last) / 1000.0f; if (g_dt > 0.1f) g_dt = 0.1f; if (g_dt <= 0) g_dt = 0.001f;
    g_time += g_dt; last = now;

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) running = 0;
      else if (e.type == SDL_CONTROLLERDEVICEADDED) SDL_GameControllerOpen(e.cdevice.which);
      else if (e.type == SDL_CONTROLLERBUTTONDOWN) { int b = map_button(e.cbutton.button); if (b >= 0) press(b); }
      else if (e.type == SDL_CONTROLLERBUTTONUP) { int b = map_button(e.cbutton.button); if (b >= 0) release(b); }
      else if (e.type == SDL_CONTROLLERAXISMOTION) stick(e.caxis.axis, e.caxis.value);
      else if (e.type == SDL_KEYDOWN && !e.key.repeat) { int b = map_key(e.key.keysym.sym); if (b >= 0) press(b); }
      else if (e.type == SDL_KEYUP) { int b = map_key(e.key.keysym.sym); if (b >= 0) release(b); }
    }
    repeat_tick();
    if (quit_at && SDL_GetTicks() >= quit_at) { omega_log("chiusura per lasciare memoria al gioco"); running = 0; }
    cmd_poll();
    netq_pump();
    loader_pump();
    social_tick();
    gallery_tick();
    install_tick();
    music_tick();
    files_tick();
    setup_tick();
    foreground_tick();
    ov_update();
    scene_t = approach(scene_t, 1, 6.0f);
    if (g_scene == SC_SPLASH) splash_update();
    if (g_scene == SC_HOME) { home_update(); terms_tick(); whatsnew_tick(); }
#ifdef OMEGA_UPDATES
    update_tick();
#endif
    voice_tick();

    gfx_frame();
    render_frame();
    if (want_shot) { save_shot(); want_shot = 0; }
    SDL_RenderPresent(R);
    Uint32 spent = SDL_GetTicks() - now;
    // tempi dei fotogrammi nel log ogni 15 s, per tarare il renderer software
    { static Uint32 acc, frames, worst, since;
      acc += spent; frames++; if (spent > worst) worst = spent;
      if (now - since > 15000) { if (since) omega_log("frame: media %.1f ms, peggiore %u ms (%u frame)", (double)acc / frames, worst, frames); acc = frames = worst = 0; since = now; } }
    // Personalizza › Fluidità: 60, 30, o automatica (30 quando la home è ferma da 5 s)
    Uint32 frame_ms = FRAME_MS;
    if (g_prefs.fps == 1) frame_ms = 33;
    else if (g_prefs.fps == 2 && (saver_on || (g_scene == SC_HOME && ov_depth() == 0 && SDL_GetTicks() - g_last_input > 5000 && !g_busy_motion))) frame_ms = 33;
    if (spent < frame_ms) SDL_Delay(frame_ms - spent);
  }
  voice_shutdown(); system_shutdown();
  unlink(OMEGA_UI_ACTIVE);   // la UI si chiude (gioco avviato): la voce del party passa al servizio
  TTF_Quit(); SDL_Quit();
  return 0;
}
