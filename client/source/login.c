// Omega UI — splash, scelta dell'utente, accesso e registrazione.
#include "app.h"
#include "servers.h"
#include <math.h>
#include <stdlib.h>

// Il server accetta registrazioni solo dalle app che presentano questa chiave.
#ifndef OMEGA_REG_KEY
#define OMEGA_REG_KEY ""
#endif

static float splash_t;

// ------------------------------------------------------------------- splash --
static void logo(int cx, int cy, float scale, int alpha) {
  int r = (int)(78 * scale);
  glow(cx, cy, (int)(r * 2.6f), RGB(40, 120, 255), alpha * 55 / 100);
  draw_icon(IC_OMEGA, cx, cy, (int)(r * 2.9f), C_WHITE, alpha);
}

void splash_enter(void) { splash_t = 0; }

void splash_update(void) {
  splash_t += g_dt;
  if (splash_t > 2.8f && g_boot_state != 0) scene_set(g_boot_state == 1 ? SC_USERS : SC_LOGIN);
}

void splash_draw(void) {
  float t = splash_t;
  fill_rect(0, 0, SCREEN_W, SCREEN_H, RGB(2, 4, 12), (int)(255 * clampf(1.2f - t * 0.5f, 0, 1)));
  particles_draw((int)(255 * clampf(t - 0.4f, 0, 1)));
  float a = ease_out(clampf((t - 0.3f) / 0.9f, 0, 1));
  float sc = 0.86f + 0.14f * ease_out(clampf((t - 0.3f) / 1.2f, 0, 1));
  int cx = SCREEN_W / 2, cy = SCREEN_H / 2 - 60;
  logo(cx, cy, sc, (int)(255 * a));
  float ta = ease_out(clampf((t - 0.9f) / 0.8f, 0, 1));
  draw_text(font(W_LIGHT, 64), "O M E G A", cx, cy + 130 + (int)((1 - ta) * 20), C_WHITE, (int)(255 * ta), AL_C);
  // fascio di luce che attraversa il logo
  float sw = clampf((t - 1.2f) / 1.0f, 0, 1);
  if (sw > 0 && sw < 1) glow(cx - 400 + (int)(800 * ease_in_out(sw)), cy + 40, 260, RGB(120, 190, 255), (int)(120 * sinf(sw * 3.14159f)));
  if (t > 2.0f && g_boot_state == 0) draw_spinner(cx, SCREEN_H - 160, 18, (int)(200 * clampf(t - 2.0f, 0, 1)));
}

// ------------------------------------------------------------ scelta utente --
static int us_sel; static float us_anim;

void users_draw(void) {
  particles_draw(160);
  us_anim = approach(us_anim, (float)us_sel, 14.0f);
  draw_text(font(W_LIGHT, 54), "Chi sta usando questo controller?", SCREEN_W / 2, 200, C_TXT, 255, AL_C);
  int n = 2, gap = 320, cx0 = SCREEN_W / 2 - (n - 1) * gap / 2, cy = SCREEN_H / 2 + 10;
  for (int i = 0; i < n; i++) {
    int cx = cx0 + i * gap;
    float f = clampf(1 - fabsf(us_anim - i), 0, 1);
    int size = 200 + (int)(40 * ease_out(f));
    float pulse = 0.5f + 0.5f * sinf((float)g_time * 3);
    if (i == 0) {
      if (f > 0.05f) ring(cx, cy, size / 2 + 14, 5, C_WHITE, (int)(255 * f * (0.7f + 0.3f * pulse)));
      draw_avatar(S.me, S.my_avatar, cx, cy, size, 255);
      draw_text(font(f > 0.5f ? W_MED : W_REG, 34), S.me[0] ? S.me : "Utente", cx, cy + size / 2 + 40, f > 0.5f ? C_WHITE : C_DIM, 255, AL_C);
      draw_text(font(W_REG, 24), "Account Omega", cx, cy + size / 2 + 88, C_FAINT, (int)(255 * f), AL_C);
    } else {
      if (f > 0.05f) ring(cx, cy, size / 2 + 14, 5, C_WHITE, (int)(255 * f * (0.7f + 0.3f * pulse)));
      fill_circle(cx, cy, size / 2, RGB(40, 46, 64), 230);
      draw_icon(IC_PLUS, cx, cy, size / 3, C_TXT, 255);
      draw_text(font(f > 0.5f ? W_MED : W_REG, 34), "Altro utente", cx, cy + size / 2 + 40, f > 0.5f ? C_WHITE : C_DIM, 255, AL_C);
    }
  }
  const int ic[] = { IC_BTN_X };
  const char *lb[] = { "Conferma" };
  hints(ic, lb, 1, 255);
}

void users_input(int b) {
  if (b == B_LEFT && us_sel > 0) us_sel--;
  else if (b == B_RIGHT && us_sel < 1) us_sel++;
  else if (b == B_X) {
    if (us_sel == 0) { social_presence("online", NULL, NULL); scene_set(SC_HOME); }
    else {
      // un altro account: la sessione salvata si chiude
      g_net_gen++; session_clear(); g_token[0] = 0; social_reset();
      scene_set(SC_LOGIN);
    }
  }
}

// -------------------------------------------------------------------- campi --
static void field(int x, int y, int w, const char *label, const char *value, int pw, int focused, float fa) {
  int h = 96;
  if (focused) shadow_rrect(x, y, w, h, 20, 20, 90);
  fill_rrect(x, y, w, h, 20, mix(RGB(26, 31, 46), RGB(44, 52, 74), fa), 235);
  if (fa > 0.02f) stroke_rrect(x - 4, y - 4, w + 8, h + 8, 24, 3, C_WHITE, (int)(255 * fa));
  draw_text(font(W_REG, 22), label, x + 28, y + 14, focused ? C_ACC2 : C_FAINT, 255, AL_L);
  char shown[160];
  if (!value || !value[0]) {
    if (focused) {
      int px = x + 28 + draw_text(font(W_LIGHT, 30), "Premi", x + 28, y + 46, C_FAINT, 255, AL_L) + 12;
      ring(px + 15, y + 64, 15, 2, C_FAINT, 255); draw_icon(IC_BTN_X, px + 15, y + 64, 20, C_FAINT, 255);
      draw_text(font(W_LIGHT, 30), "per scrivere", px + 42, y + 46, C_FAINT, 255, AL_L);
    }
    return;
  }
  if (pw) { size_t l = strlen(value); if (l > 40) l = 40; for (size_t i = 0; i < l; i++) { shown[i * 3] = (char)0xE2; shown[i * 3 + 1] = (char)0x80; shown[i * 3 + 2] = (char)0xA2; } shown[l * 3] = 0; }
  else snprintf(shown, sizeof shown, "%s", value);
  draw_text_fit(font(W_REG, 32), shown, x + 28, y + 44, w - 56, C_TXT, 255, AL_L);
}

static void button(int x, int y, int w, const char *label, int primary, int focused, float fa) {
  int h = 84;
  if (focused) shadow_rrect(x, y, w, h, 42, 22, 110);
  Col bg = primary ? C_ACC : mix(RGB(40, 46, 64), C_WHITE, fa);
  if (primary && focused) bg = RGB(30, 136, 255);
  fill_rrect(x, y, w, h, 42, bg, 250);
  if (fa > 0.02f && primary) stroke_rrect(x - 5, y - 5, w + 10, h + 10, 47, 3, C_WHITE, (int)(255 * fa));
  Col fg = primary ? C_WHITE : mix(C_TXT, RGB(12, 14, 22), fa);
  draw_text(font(W_MED, 32), label, x + w / 2, y + (h - TTF_FontHeight(font(W_MED, 32))) / 2, fg, 255, AL_C);
}

// riga "Server: nome · host"; X apre la scelta del server
static void server_row(int x, int y, int w, int focused, float fa) {
  const Server *sv = srv_get(srv_current());
  char l[200]; snprintf(l, sizeof l, "Server: %s \xC2\xB7 %s", sv->name, srv_host(sv));
  fill_rrect(x, y, w, 60, 30, mix(RGB(26, 31, 46), RGB(44, 52, 74), fa), 200);
  if (fa > 0.02f) stroke_rrect(x - 4, y - 4, w + 8, 68, 34, 3, C_WHITE, (int)(255 * fa));
  draw_icon(IC_GLOBE, x + 36, y + 30, 26, focused ? C_ACC2 : C_FAINT, 255);
  draw_text_fit(font(W_REG, 24), l, x + 66, y + 16, w - 200, focused ? C_TXT : C_DIM, 255, AL_L);
  draw_text(font(W_MED, 22), "Cambia", x + w - 30, y + 18, focused ? C_ACC2 : C_FAINT, 255, AL_R);
}

static void auth_backdrop(const char *title, const char *sub) {
  particles_draw(200);
  grad_h(0, 0, 900, SCREEN_H, C_BLACK, 170, C_BLACK, 0);
  logo(200, 230, 0.55f, 255);
  draw_text(font(W_LIGHT, 30), "O M E G A", 290, 212, C_TXT, 255, AL_L);
  draw_text(font(W_LIGHT, 76), title, 140, 400, C_WHITE, 255, AL_L);
  draw_text_wrap(font(W_REG, 30), sub, 144, 510, 640, 4, 42, C_DIM, 255);
}

// -------------------------------------------------------------------- login --
static int lg_sel; static float lg_anim; static int lg_busy;

static void on_login(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; (void)ud;
  lg_busy = 0;
  if (st == 200 && j && jstr(j, "token", NULL)) { after_login(j); set_msg("Bentornato!", 0); return; }
  if (st == 401) set_msg("ID online o password non corretti", 1);
  else if (st == 403 && !strcmp(jstr(j, "error", ""), "account_banned")) {
    char m[240]; snprintf(m, sizeof m, "Account sospeso: %s", jstr(j, "detail", "violazione dei termini d'uso")); set_msg(m, 1);
  }
  else if (st == 429) set_msg("Troppi tentativi: riprova tra poco", 1);
  else if (st < 0) { char m[200]; snprintf(m, sizeof m, "Server %s non raggiungibile", server_label()); set_msg(m, 1); }
  else { char m[64]; snprintf(m, sizeof m, "Accesso non riuscito (HTTP %d)", st); set_msg(m, 1); }
}

static void do_login(void) {
  if (lg_busy) return;
  if (!f_user[0] || !f_pass[0]) { set_msg("Inserisci ID online e password", 1); return; }
  char u[160], p[300], body[520];
  json_escape(u, sizeof u, f_user); json_escape(p, sizeof p, f_pass);
  snprintf(body, sizeof body, "{\"online_id\":\"%s\",\"password\":\"%s\"}", u, p);
  lg_busy = 1;
  net_req(HTTP_POST, OMEGA_API "/auth/login", body, on_login, NULL);
}

void login_draw(void) {
  auth_backdrop("Accedi", "Entra con il tuo account Omega per ritrovare amici, party, messaggi e la tua libreria.");
  lg_anim = approach(lg_anim, (float)lg_sel, 16.0f);
  int x = 1060, w = 700, y = 300;
  fill_rrect(x - 60, y - 80, w + 120, 680, 36, RGB(12, 16, 28), 160);
  stroke_rrect(x - 60, y - 80, w + 120, 680, 36, 1, RGB(90, 110, 160), 70);
  server_row(x, y + 494, w, lg_sel == 4, clampf(1 - fabsf(lg_anim - 4), 0, 1));
  for (int i = 0; i < 4; i++) {
    float fa = clampf(1 - fabsf(lg_anim - i), 0, 1);
    if (i == 0) field(x, y, w, "ID online", f_user, 0, lg_sel == 0, fa);
    else if (i == 1) field(x, y + 124, w, "Password", f_pass, 1, lg_sel == 1, fa);
    else if (i == 2) button(x, y + 270, w, lg_busy ? "Accesso in corso..." : "Accedi", 1, lg_sel == 2, fa);
    else button(x, y + 376, w, "Crea un account Omega", 0, lg_sel == 3, fa);
  }
  if (lg_busy) draw_spinner(x + w - 50, y + 312, 14, 255);
  const int ic[] = { IC_BTN_X, IC_BTN_OPT };
  const char *lb[] = { "Seleziona", "Privacy e termini" };
  hints(ic, lb, 2, 255);
}

void login_input(int b) {
  if (b == B_UP && lg_sel > 0) lg_sel--;
  else if (b == B_DOWN && lg_sel < 4) lg_sel++;
  else if (b == B_OPT) privacy_menu();
  else if (b == B_X) {
    if (lg_sel == 0) { if (edit_text("ID online", f_user, sizeof f_user, 0)) lg_sel = 1; }
    else if (lg_sel == 1) { if (edit_text("Password", f_pass, sizeof f_pass, 1)) lg_sel = 2; }
    else if (lg_sel == 2) do_login();
    else if (lg_sel == 3) { scene_set(SC_REGISTER); set_msg("", 0); }
    else server_menu();
  }
}

// ------------------------------------------------------------ registrazione --
static int rg_sel; static float rg_anim; static int rg_busy; static int rg_terms;

static void on_register(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; (void)ud;
  rg_busy = 0;
  if (st == 201) {
    set_msg("Account creato! Accesso in corso...", 0);
    do_login();
    return;
  }
  const char *e = jstr(j, "error", ""), *d = jstr(j, "detail", "");
  if (!strcmp(e, "terms_not_accepted")) set_msg("Per creare l'account accetta i termini d'uso", 1);
  else if (st == 409) set_msg("ID online o email già in uso", 1);
  else if (st == 403) set_msg("Registrazione chiusa su questo server", 1);
  else if (!strcmp(e, "invalid_online_id")) set_msg("ID online: 3-16 caratteri, inizia con una lettera", 1);
  else if (!strcmp(e, "invalid_password")) set_msg(d[0] ? d : "Password non valida (minimo 8 caratteri)", 1);
  else if (!strcmp(e, "invalid_email")) set_msg("Email non valida", 1);
  else if (st == 429) set_msg("Troppe registrazioni: riprova più tardi", 1);
  else if (st < 0) set_msg("Server Omega non raggiungibile", 1);
  else set_msg("Registrazione non riuscita", 1);
}

static void do_register(void) {
  if (rg_busy) return;
  if (!f_user[0] || !f_pass[0]) { set_msg("ID online e password sono obbligatori", 1); return; }
  if (strlen(f_pass) < 8) { set_msg("La password deve avere almeno 8 caratteri", 1); return; }
  if (strcmp(f_pass, f_confirm)) { set_msg("Le password non coincidono", 1); return; }
  if (!rg_terms) { set_msg("Per creare l'account accetta i termini d'uso e l'informativa", 1); rg_sel = 4; return; }
  char u[160], p[300], e[200], em[240] = "", body[900];
  json_escape(u, sizeof u, f_user); json_escape(p, sizeof p, f_pass);
  if (f_email[0]) { json_escape(e, sizeof e, f_email); snprintf(em, sizeof em, ",\"email\":\"%s\"", e); }
  snprintf(body, sizeof body, "{\"online_id\":\"%s\",\"password\":\"%s\"%s,\"registration_key\":\"%s\",\"accept_terms\":true}", u, p, em, OMEGA_REG_KEY);
  rg_busy = 1;
  net_req(HTTP_POST, OMEGA_API "/auth/register", body, on_register, NULL);
}

static void terms_read_pick(int idx, void *ud) { (void)ud; doc_open(idx == 0 ? "terms" : "privacy"); }

void register_draw(void) {
  auth_backdrop("Crea account", "Scegli il tuo ID online: è il nome con cui i tuoi amici ti troveranno su Omega.");
  rg_anim = approach(rg_anim, (float)rg_sel, 16.0f);
  int x = 1060, w = 700, y = 120;
  fill_rrect(x - 60, y - 60, w + 120, 940, 36, RGB(12, 16, 28), 160);
  stroke_rrect(x - 60, y - 60, w + 120, 940, 36, 1, RGB(90, 110, 160), 70);
  const char *lbl[4] = { "ID online", "Email (facoltativa)", "Password (min. 8 caratteri)", "Conferma password" };
  char *val[4] = { f_user, f_email, f_pass, f_confirm };
  for (int i = 0; i < 4; i++) {
    float fa = clampf(1 - fabsf(rg_anim - i), 0, 1);
    field(x, y + i * 120, w, lbl[i], val[i], i >= 2, rg_sel == i, fa);
  }
  // consenso: casella e testo; triangolo apre i documenti
  { float fc = clampf(1 - fabsf(rg_anim - 4), 0, 1); int cy = y + 486;
    fill_rrect(x, cy, w, 96, 20, mix(RGB(26, 31, 46), RGB(44, 52, 74), fc), 220);
    if (fc > 0.02f) stroke_rrect(x - 4, cy - 4, w + 8, 104, 24, 3, C_WHITE, (int)(255 * fc));
    fill_rrect(x + 26, cy + 30, 36, 36, 9, rg_terms ? C_ACC : RGB(20, 24, 36), 255);
    stroke_rrect(x + 26, cy + 30, 36, 36, 9, 2, rg_terms ? C_ACC : C_FAINT, 255);
    if (rg_terms) draw_icon(IC_CHECK, x + 44, cy + 48, 26, C_WHITE, 255);
    draw_text_wrap(font(W_REG, 23), "Ho almeno 14 anni e accetto i Termini d'uso e l'Informativa sulla privacy", x + 84, cy + 16, w - 110, 2, 31, C_TXT, 255); }
  float fa5 = clampf(1 - fabsf(rg_anim - 5), 0, 1), fa6 = clampf(1 - fabsf(rg_anim - 6), 0, 1);
  button(x, y + 610, w, rg_busy ? "Creazione in corso..." : "Crea account", 1, rg_sel == 5, fa5);
  button(x, y + 714, w, "Ho già un account", 0, rg_sel == 6, fa6);
  if (rg_busy) draw_spinner(x + w - 50, y + 652, 14, 255);
  if (rg_sel == 4) {
    const int ic[] = { IC_BTN_X, IC_BTN_TRI, IC_BTN_O };
    const char *lb[] = { rg_terms ? "Togli il consenso" : "Accetto", "Leggi termini e privacy", "Indietro" };
    hints(ic, lb, 3, 255);
  } else {
    const int ic[] = { IC_BTN_X, IC_BTN_O };
    const char *lb[] = { "Seleziona", "Indietro" };
    hints(ic, lb, 2, 255);
  }
}

void register_input(int b) {
  if (b == B_UP && rg_sel > 0) rg_sel--;
  else if (b == B_DOWN && rg_sel < 6) rg_sel++;
  else if (b == B_O) scene_set(SC_LOGIN);
  else if (b == B_TRI && rg_sel == 4) {
    static const char *items[] = { "Termini d'uso", "Informativa sulla privacy" };
    menu_open("Leggi prima di accettare", items, 2, terms_read_pick, NULL);
  }
  else if (b == B_X) {
    if (rg_sel == 0) { if (edit_text("ID online", f_user, sizeof f_user, 0)) rg_sel = 1; }
    else if (rg_sel == 1) { if (edit_text("Email", f_email, sizeof f_email, 0)) rg_sel = 2; }
    else if (rg_sel == 2) { if (edit_text("Password", f_pass, sizeof f_pass, 1)) rg_sel = 3; }
    else if (rg_sel == 3) { if (edit_text("Conferma password", f_confirm, sizeof f_confirm, 1)) rg_sel = 4; }
    else if (rg_sel == 4) rg_terms = !rg_terms;
    else if (rg_sel == 5) do_register();
    else scene_set(SC_LOGIN);
  }
}
