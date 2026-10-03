// Omega UI — privacy e account: documenti legali letti dal server, esportazione
// ed eliminazione dei dati, termini aggiornati e scelta del server.
#include "app.h"
#include "servers.h"
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

// ---------------------------------------------------------------- documenti --
#define DOC_SECT 20
static struct {
  int loading, error;
  char kind[16];
  char title[96];
  int n; struct { char title[120]; char *body; int h; } s[DOC_SECT];
  float scroll, target, max;
} D;
static char g_terms_version[24];     // versione corrente dei termini (dall'ultimo /me)

static void doc_free(void) { for (int i = 0; i < D.n; i++) { free(D.s[i].body); D.s[i].body = NULL; } D.n = 0; }

static void on_legal(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; (void)ud;
  D.loading = 0;
  JVal *d = jget(jget(j, "documents"), D.kind);
  if (st != 200 || !d) { D.error = 1; return; }
  snprintf(D.title, sizeof D.title, "%s", jstr(d, "title", ""));
  doc_free();
  JVal *ss = jget(d, "sections");
  for (JVal *s = ss ? ss->child : NULL; s && D.n < DOC_SECT; s = s->next) {
    snprintf(D.s[D.n].title, sizeof D.s[D.n].title, "%s", jstr(s, "title", ""));
    D.s[D.n].body = strdup(jstr(s, "body", ""));
    D.s[D.n].h = -1;
    D.n++;
  }
  const char *v = jstr(j, "version", "");
  if (v[0]) snprintf(g_terms_version, sizeof g_terms_version, "%s", v);
}

void doc_open(const char *kind) {
  doc_free();
  memset(&D, 0, sizeof D);
  snprintf(D.kind, sizeof D.kind, "%s", kind);
  snprintf(D.title, sizeof D.title, "%s", !strcmp(kind, "privacy") ? "Informativa sulla privacy" : !strcmp(kind, "terms") ? "Termini d'uso" : "Licenze open source");
  D.loading = 1;
  net_req(HTTP_GET, OMEGA_API "/legal", NULL, on_legal, NULL);
  ov_push(OV_DOC);
}

void doc_draw(float t) {
  fill_rect(0, 0, SCREEN_W, SCREEN_H, RGB(2, 4, 10), (int)(170 * t));
  int w = 1400, h = 940, x = SCREEN_W / 2 - w / 2, y = SCREEN_H / 2 - h / 2 + (int)((1 - ease_out(t)) * 50);
  int a = (int)(255 * t);
  shadow_rrect(x, y, w, h, 34, 50, a * 70 / 100);
  fill_rrect(x, y, w, h, 34, C_PANEL, a);
  stroke_rrect(x, y, w, h, 34, 1, RGB(80, 92, 125), a / 2);
  draw_icon(IC_NEWS, x + 76, y + 70, 40, C_ACC2, a);
  draw_text_fit(font(W_LIGHT, 44), D.title, x + 116, y + 42, w - 200, C_WHITE, a, AL_L);
  int cx = x + 76, cw = w - 152, top = y + 130, ch = h - 170;
  fill_rect(x + 40, top - 14, w - 80, 1, RGB(255, 255, 255), a * 10 / 100);
  if (D.loading) { draw_spinner(x + w / 2, y + h / 2, 22, a); return; }
  if (D.error) {
    draw_text(font(W_REG, 30), "Documento non disponibile: server non raggiungibile.", x + w / 2, y + h / 2 - 20, C_DIM, a, AL_C);
    return;
  }
  D.scroll = approach(D.scroll, D.target, 14.0f);
  SDL_Rect clip = { x + 20, top, w - 40, ch };
  SDL_RenderSetClipRect(R, &clip);
  int yy = top + 10 - (int)D.scroll;
  for (int i = 0; i < D.n; i++) {
    int sh = D.s[i].h;
    if (sh > 0 && (yy + sh < top - 40 || yy > top + ch + 40)) { yy += sh; continue; }   // fuori vista
    int y0 = yy;
    if (D.s[i].title[0]) { draw_text(font(W_MED, 30), D.s[i].title, cx, yy, C_TXT, a, AL_L); yy += 48; }
    int lines = draw_text_wrap(font(W_REG, 26), D.s[i].body, cx, yy, cw, 80, 38, C_DIM, a);
    yy += lines * 38 + 34;
    D.s[i].h = yy - y0;
  }
  D.max = (float)(yy + (int)D.scroll - top - ch + 10);
  if (D.max < 0) D.max = 0;
  SDL_RenderSetClipRect(R, NULL);
  // barra di scorrimento
  if (D.max > 0) {
    int bh = ch * ch / (ch + (int)D.max); if (bh < 60) bh = 60;
    int by = top + (int)((ch - bh) * (D.scroll / D.max));
    fill_rrect(x + w - 30, top, 6, ch, 3, RGB(255, 255, 255), a * 8 / 100);
    fill_rrect(x + w - 30, by, 6, bh, 3, C_ACC2, a * 80 / 100);
  }
  const int ic[] = { IC_BTN_O };
  const char *lb[] = { "Chiudi" };
  hints(ic, lb, 1, a);
}

void doc_input(int b) {
  if (b == B_O || b == B_X) { ov_pop(); return; }
  float step = b == B_L1 || b == B_R1 ? 700.0f : 90.0f;
  if (b == B_DOWN || b == B_R1) D.target += step;
  else if (b == B_UP || b == B_L1) D.target -= step;
  if (D.target > D.max) D.target = D.max;
  if (D.target < 0) D.target = 0;
}

// ----------------------------------------------------------- dati e account --
static int write_file(const char *path, const char *data) {
  FILE *f = fopen(path, "wb");
  if (!f) return -1;
  size_t l = strlen(data), w = fwrite(data, 1, l, f);
  fclose(f);
  return w == l ? 0 : -1;
}

static void on_export(int st, JVal *j, const char *raw, void *ud) {
  (void)j; (void)ud;
  if (st != 200 || !raw || raw[0] != '{') {
    set_msg(st == 429 ? "Hai già scaricato i dati da poco: riprova tra un'ora" : "Esportazione non riuscita", 1);
    return;
  }
  mkdir(OMEGA_DIR, 0777);
  char path[300], where[300] = "";
  snprintf(path, sizeof path, "%s/omega-miei-dati.json", OMEGA_DIR);
  if (write_file(path, raw) == 0) snprintf(where, sizeof where, "%s", path);
  // copia anche su una chiavetta USB, se c'è
  static const char *usb[] = { "/mnt/usb0", "/mnt/usb1" };
  for (int i = 0; i < 2; i++) {
    if (access(usb[i], W_OK) != 0) continue;
    char up[300]; snprintf(up, sizeof up, "%s/omega-miei-dati.json", usb[i]);
    if (write_file(up, raw) == 0) { snprintf(where, sizeof where, "%s", up); break; }
  }
  char m[360];
  if (where[0]) snprintf(m, sizeof m, "Dati salvati in %s", where);
  else snprintf(m, sizeof m, "Impossibile salvare il file dei dati");
  set_msg(m, !where[0]);
}

static void on_delete(int st, JVal *j, const char *raw, void *ud) {
  (void)j; (void)raw; (void)ud;
  if (st == 200) { do_logout(); set_msg("Account eliminato. Tutti i tuoi dati sono stati cancellati.", 0); }
  else if (st == 401) set_msg("Password non corretta", 1);
  else if (st == 429) set_msg("Troppi tentativi: riprova più tardi", 1);
  else set_msg("Eliminazione non riuscita", 1);
}

static void delete_yes(int idx, void *ud) {
  (void)idx; (void)ud;
  char pw[128] = "";
  if (!edit_text("Password del tuo account", pw, sizeof pw, 1) || !pw[0]) return;
  char e[300], body[360];
  json_escape(e, sizeof e, pw);
  snprintf(body, sizeof body, "{\"password\":\"%s\"}", e);
  memset(pw, 0, sizeof pw);
  net_req(HTTP_POST, OMEGA_API "/account/delete", body, on_delete, NULL);
  memset(body, 0, sizeof body);
}

// chi può contattarmi, cosa vedono gli altri, utenti bloccati
static struct { char messages[12], requests[24]; int activity, loaded; } PV;
static void privacy_menu_open(void);
static void privacy_save(void) {
  char body[160];
  snprintf(body, sizeof body, "{\"messages\":\"%s\",\"friend_requests\":\"%s\",\"show_activity\":%s}",
           PV.messages, PV.requests, PV.activity ? "true" : "false");
  net_req(HTTP_POST, OMEGA_API "/privacy", body, NULL, NULL);
}
static void on_privacy(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; (void)ud;
  if (st != 200) { set_msg("Impostazioni non disponibili", 1); return; }
  jcpy(PV.messages, sizeof PV.messages, j, "messages");
  jcpy(PV.requests, sizeof PV.requests, j, "friend_requests");
  PV.activity = jbool(j, "show_activity"); PV.loaded = 1;
  privacy_menu_open();
}
static char blocked[40][32]; static int nblocked;
static void unblock_pick(int idx, void *ud) { (void)ud; if (idx >= 0 && idx < nblocked) social_block(blocked[idx], 0); }
static void on_blocks(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; (void)ud;
  if (st != 200) return;
  static const char *items[40]; nblocked = 0;
  JFOR(u, jget(j, "users")) { if (nblocked >= 40) break; jcpy(blocked[nblocked], sizeof blocked[0], u, "online_id"); items[nblocked] = blocked[nblocked]; nblocked++; }
  if (!nblocked) { set_msg("Non hai bloccato nessuno", 0); return; }
  menu_open("Utenti bloccati (X per sbloccare)", items, nblocked, unblock_pick, NULL);
}
static void contact_pick(int idx, void *ud) {
  (void)ud;
  if (idx == 0) snprintf(PV.messages, sizeof PV.messages, "%s", strcmp(PV.messages, "friends") ? "friends" : "everyone");
  else if (idx == 1) {
    const char *next = !strcmp(PV.requests, "everyone") ? "friends_of_friends" : !strcmp(PV.requests, "friends_of_friends") ? "nobody" : "everyone";
    snprintf(PV.requests, sizeof PV.requests, "%s", next);
  } else if (idx == 2) PV.activity = !PV.activity;
  else { net_req(HTTP_GET, OMEGA_API "/blocks", NULL, on_blocks, NULL); return; }
  privacy_save();
  privacy_menu_open();       // il menu resta aperto con i valori aggiornati
}
static void privacy_menu_open(void) {
  static char l[3][96]; static const char *items[4];
  snprintf(l[0], sizeof l[0], "Chi può scrivermi: %s", strcmp(PV.messages, "friends") ? "tutti" : "solo amici");
  snprintf(l[1], sizeof l[1], "Richieste di amicizia: %s", !strcmp(PV.requests, "nobody") ? "nessuno" : !strcmp(PV.requests, "friends_of_friends") ? "amici di amici" : "tutti");
  snprintf(l[2], sizeof l[2], "Mostra cosa gioco e il mio tempo: %s", PV.activity ? "sì" : "no");
  items[0] = l[0]; items[1] = l[1]; items[2] = l[2]; items[3] = "Utenti bloccati";
  menu_open("Contatti e visibilità", items, 4, contact_pick, NULL);
}

static void privacy_pick(int idx, void *ud) {
  (void)ud;
  switch (idx) {
    case 0: net_req(HTTP_GET, OMEGA_API "/privacy", NULL, on_privacy, NULL); break;
    case 1: doc_open("privacy"); break;
    case 2: doc_open("terms"); break;
    case 3: doc_open("licenses"); break;
    case 4: set_msg("Preparazione dei tuoi dati...", 0); net_req(HTTP_GET, OMEGA_API "/account/export", NULL, on_export, NULL); break;
    case 5: confirm_open("Eliminare per sempre l'account e tutto ciò che hai pubblicato? Non si può annullare.", "Elimina", delete_yes, NULL); break;
  }
}
static void privacy_pick_guest(int idx, void *ud) { privacy_pick(idx + 1, ud); }

void privacy_menu(void) {
  static const char *items[] = { "Contatti e visibilità", "Informativa sulla privacy", "Termini d'uso", "Licenze open source", "Scarica i miei dati", "Elimina account" };
  if (g_token[0]) menu_open("Privacy e dati", items, 6, privacy_pick, NULL);
  else menu_open("Privacy e termini", items + 1, 3, privacy_pick_guest, NULL);
}

// ------------------------------------------------------------ termini nuovi --
static int terms_pending;
static void on_terms_ok(int st, JVal *j, const char *raw, void *ud) {
  (void)j; (void)raw; (void)ud;
  if (st == 200) set_msg("Grazie! Termini accettati", 0);
}
static void terms_yes(int idx, void *ud) {
  (void)idx; (void)ud;
  char body[80]; snprintf(body, sizeof body, "{\"version\":\"%s\"}", g_terms_version);
  net_req(HTTP_POST, OMEGA_API "/account/terms", body, on_terms_ok, NULL);
}
// se i termini sono cambiati si chiede di accettarli (alla prima occasione libera)
void terms_check(JVal *me) {
  JVal *t = jget(me, "terms");
  if (!t) return;
  const char *v = jstr(t, "current", "");
  if (v[0]) snprintf(g_terms_version, sizeof g_terms_version, "%s", v);
  terms_pending = jbool(t, "needs_accept") && v[0];
}
static void on_me_terms(int st, JVal *j, const char *raw, void *ud) { (void)raw; (void)ud; if (st == 200) terms_check(j); }
void terms_refresh(void) { net_req(HTTP_GET, OMEGA_API "/me", NULL, on_me_terms, NULL); }
void terms_tick(void) {
  if (!terms_pending || g_scene != SC_HOME || ov_depth() > 0) return;
  terms_pending = 0;
  confirm_open("Termini d'uso e privacy sono stati aggiornati (Impostazioni > Privacy e dati). Li accetti?", "Accetto", terms_yes, NULL);
}

// ------------------------------------------------------------------- server --
static int srv_target;

static void switch_now(int i) {
  const Server *s = srv_get(i);
  if (!s) return;
  if (g_token[0]) {
    // logout sincrono: va fatto sul server attuale, prima di cambiare indirizzo
    char buf[512];
    omega_http(HTTP_POST, OMEGA_API "/presence", g_token, "{\"status\":\"offline\"}", buf, sizeof buf);
    omega_http(HTTP_POST, OMEGA_API "/auth/logout", g_token, NULL, buf, sizeof buf);
  }
  srv_select(i);
  g_net_gen++;
  session_clear(); g_token[0] = 0;
  social_reset(); ov_clear();
  memset(f_pass, 0, sizeof f_pass);
  bg_set_default();
  scene_set(SC_LOGIN);
  char m[160]; snprintf(m, sizeof m, "Collegato a %s: accedi o crea un account", s->name);
  set_msg(m, 0);
}
static void switch_yes(int idx, void *ud) { (void)idx; (void)ud; switch_now(srv_target); }

static void ask_switch(int i) {
  const Server *s = srv_get(i);
  if (!s || i == srv_current()) return;
  srv_target = i;
  if (!g_token[0]) { switch_now(i); return; }
  static char q[220];
  snprintf(q, sizeof q, "Collegarsi a %s (%s)? Uscirai dall'account di questo server.", s->name, srv_host(s));
  confirm_open(q, "Collegati", switch_yes, NULL);
}

static void remove_yes(int idx, void *ud) {
  (void)idx; (void)ud;
  if (srv_target == srv_current()) switch_now(0);
  srv_remove(srv_target);
  set_msg("Server rimosso", 0);
}
static void remove_pick(int idx, void *ud) {
  (void)ud;
  srv_target = idx + 1;    // l'elenco non contiene Omega
  const Server *s = srv_get(srv_target);
  if (!s) return;
  static char q[220];
  snprintf(q, sizeof q, "Togliere %s (%s) dall'elenco?", s->name, srv_host(s));
  confirm_open(q, "Togli", remove_yes, NULL);
}

static void add_server(void) {
  char url[200] = "https://", norm[200], name[48];
  if (!edit_text("Indirizzo del server (es. https://mio-server.it)", url, sizeof url, 0)) return;
  if (srv_normalize(url, norm, sizeof norm) < 0) { set_msg("Indirizzo non valido", 1); return; }
  snprintf(name, sizeof name, "%s", strstr(norm, "://") + 3);
  if (!edit_text("Nome del server", name, sizeof name, 0)) return;
  int i = srv_add(name, norm);
  if (i == -2) { set_msg("Elenco pieno: togli un server prima", 1); return; }
  if (i < 0) { set_msg("Indirizzo non valido", 1); return; }
  if (i == srv_current()) { set_msg("Stai già usando questo server", 0); return; }
  if (!g_token[0]) { switch_now(i); return; }
  set_msg("Server aggiunto", 0);
  ask_switch(i);
}

static void server_pick(int idx, void *ud) {
  (void)ud;
  int n = srv_count();
  if (idx < n) ask_switch(idx);
  else if (idx == n) add_server();
  else if (n > 1) {
    static char names[SRV_MAX][120]; static const char *items[SRV_MAX];
    for (int i = 1; i < n; i++) { snprintf(names[i - 1], sizeof names[0], "%s \xC2\xB7 %s", srv_get(i)->name, srv_host(srv_get(i))); items[i - 1] = names[i - 1]; }
    menu_open("Togli un server", items, n - 1, remove_pick, NULL);
  }
}

void server_menu(void) {
  static char names[SRV_MAX + 2][140]; static const char *items[SRV_MAX + 2];
  int n = srv_count(), k = 0;
  for (int i = 0; i < n; i++, k++) {
    const Server *s = srv_get(i);
    snprintf(names[k], sizeof names[k], "%s \xC2\xB7 %s%s", s->name, srv_host(s), i == srv_current() ? "  (in uso)" : "");
    items[k] = names[k];
  }
  snprintf(names[k], sizeof names[k], "+ Aggiungi un server"); items[k] = names[k]; k++;
  if (n > 1) { snprintf(names[k], sizeof names[k], "Togli un server dall'elenco"); items[k] = names[k]; k++; }
  menu_open("Server", items, k, server_pick, NULL);
}

const char *server_label(void) { return srv_get(srv_current())->name; }
