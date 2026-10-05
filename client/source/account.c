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
  char title[160];
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
  snprintf(D.title, sizeof D.title, "%s", !strcmp(kind, "privacy") ? _("Informativa sulla privacy") : !strcmp(kind, "terms") ? _("Termini d'uso") : _("Licenze open source"));
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
    draw_text(font(W_REG, 30), _("Documento non disponibile: server non raggiungibile."), x + w / 2, y + h / 2 - 20, C_DIM, a, AL_C);
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
  const char *lb[] = { _("Chiudi") };
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
    set_msg(st == 429 ? _("Hai già scaricato i dati da poco: riprova tra un'ora") : _("Esportazione non riuscita"), 1);
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
  if (where[0]) snprintf(m, sizeof m, _("Dati salvati in %s"), where);
  else snprintf(m, sizeof m, "%s", _("Impossibile salvare il file dei dati"));
  set_msg(m, !where[0]);
}

static void on_delete(int st, JVal *j, const char *raw, void *ud) {
  (void)j; (void)raw; (void)ud;
  if (st == 200) { do_logout(); set_msg(_("Account eliminato. Tutti i tuoi dati sono stati cancellati."), 0); }
  else if (st == 401) set_msg(_("Password non corretta"), 1);
  else if (st == 429) set_msg(_("Troppi tentativi: riprova più tardi"), 1);
  else set_msg(_("Eliminazione non riuscita"), 1);
}

static void delete_yes(int idx, void *ud) {
  (void)idx; (void)ud;
  char pw[128] = "";
  if (!edit_text(_("Password del tuo account"), pw, sizeof pw, 1) || !pw[0]) return;
  char e[300], body[360];
  json_escape(e, sizeof e, pw);
  snprintf(body, sizeof body, "{\"password\":\"%s\"}", e);
  memset(pw, 0, sizeof pw);
  net_req(HTTP_POST, OMEGA_API "/account/delete", body, on_delete, NULL);
  memset(body, 0, sizeof body);
}

// Privacy: tutte le scelte dell'utente in un solo menu, lette e scritte con
// GET/POST /privacy. Sotto le scelte: stato, utenti bloccati, documenti e dati.
static struct { char messages[12], requests[24], trophies[12]; int activity, records, import, loaded; } PV;
static void privacy_menu_open(int focus);
static void privacy_save(void) {
  char body[260];
  snprintf(body, sizeof body, "{\"messages\":\"%s\",\"friend_requests\":\"%s\",\"show_activity\":%s,\"show_in_records\":%s,\"trophies\":\"%s\",\"import_trophies\":%s}",
           PV.messages, PV.requests, PV.activity ? "true" : "false", PV.records ? "true" : "false", PV.trophies, PV.import ? "true" : "false");
  net_req(HTTP_POST, OMEGA_API "/privacy", body, NULL, NULL);
}
static void on_privacy(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; (void)ud;
  if (st != 200) { set_msg(_("Impostazioni non disponibili"), 1); return; }
  jcpy(PV.messages, sizeof PV.messages, j, "messages");
  jcpy(PV.requests, sizeof PV.requests, j, "friend_requests");
  jcpy(PV.trophies, sizeof PV.trophies, j, "trophies");
  if (!PV.trophies[0]) snprintf(PV.trophies, sizeof PV.trophies, "everyone");
  PV.activity = jbool(j, "show_activity"); PV.records = jbool(j, "show_in_records"); PV.import = jbool(j, "import_trophies");
  PV.loaded = 1;
  privacy_menu_open(0);
}
static char blocked[40][32]; static int nblocked;
static void unblock_pick(int idx, void *ud) { (void)ud; if (idx >= 0 && idx < nblocked) social_block(blocked[idx], 0); }
static void on_blocks(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; (void)ud;
  if (st != 200) return;
  static const char *items[40]; nblocked = 0;
  JFOR(u, jget(j, "users")) { if (nblocked >= 40) break; jcpy(blocked[nblocked], sizeof blocked[0], u, "online_id"); items[nblocked] = blocked[nblocked]; nblocked++; }
  if (!nblocked) { set_msg(_("Non hai bloccato nessuno"), 0); return; }
  menu_open(_("Utenti bloccati (X per sbloccare)"), items, nblocked, unblock_pick, NULL);
}
static void import_off_yes(int idx, void *ud) {
  (void)idx; (void)ud;
  PV.import = 0; privacy_save();
  set_msg(_("Importazione spenta: i trofei importati sono stati cancellati"), 0);
}

enum { PM_STATUS, PM_MESSAGES, PM_REQUESTS, PM_ACTIVITY, PM_RECORDS, PM_TROPHIES, PM_IMPORT, PM_BLOCKS, PM_NOTICE, PM_TERMS, PM_LICENSES, PM_EXPORT, PM_DELETE, PM_N };
#define PM_GUEST_FIRST PM_NOTICE      // senza account restano solo i documenti
#define PM_GUEST_N     3

static void privacy_pick(int idx, void *ud) {
  (void)ud;
  switch (idx) {
    case PM_STATUS: status_menu(); return;
    case PM_MESSAGES: snprintf(PV.messages, sizeof PV.messages, "%s", strcmp(PV.messages, "friends") ? "friends" : "everyone"); break;
    case PM_REQUESTS: {
      const char *next = !strcmp(PV.requests, "everyone") ? "friends_of_friends" : !strcmp(PV.requests, "friends_of_friends") ? "nobody" : "everyone";
      snprintf(PV.requests, sizeof PV.requests, "%s", next);
      break;
    }
    case PM_ACTIVITY: PV.activity = !PV.activity; break;
    case PM_RECORDS: PV.records = !PV.records; break;
    case PM_TROPHIES: {
      const char *next = !strcmp(PV.trophies, "everyone") ? "friends" : !strcmp(PV.trophies, "friends") ? "nobody" : "everyone";
      snprintf(PV.trophies, sizeof PV.trophies, "%s", next);
      break;
    }
    case PM_IMPORT:
      if (PV.import) { confirm_open(_("Spegnere l'importazione dei trofei? Quelli gi\xC3\xA0 importati vengono cancellati dal server."), _("Spegni"), import_off_yes, NULL); return; }
      PV.import = 1; privacy_save(); consync_start(1);
      privacy_menu_open(idx);
      return;
    case PM_BLOCKS: net_req(HTTP_GET, OMEGA_API "/blocks", NULL, on_blocks, NULL); return;
    case PM_NOTICE: doc_open("privacy"); return;
    case PM_TERMS: doc_open("terms"); return;
    case PM_LICENSES: doc_open("licenses"); return;
    case PM_EXPORT: set_msg(_("Preparazione dei tuoi dati..."), 0); net_req(HTTP_GET, OMEGA_API "/account/export", NULL, on_export, NULL); return;
    case PM_DELETE: confirm_open(_("Eliminare per sempre l'account e tutto ci\xC3\xB2 che hai pubblicato? Non si pu\xC3\xB2 annullare."), _("Elimina"), delete_yes, NULL); return;
    default: return;
  }
  privacy_save();
  privacy_menu_open(idx);       // il menu resta aperto, sulla stessa voce, con il valore nuovo
}
static void privacy_pick_guest(int idx, void *ud) { privacy_pick(idx + PM_GUEST_FIRST, ud); }

static const char *privacy_docs(int i) {
  return i == PM_NOTICE ? _("Informativa sulla privacy") : i == PM_TERMS ? _("Termini d'uso") : i == PM_LICENSES ? _("Licenze open source")
       : i == PM_EXPORT ? _("Scarica i miei dati") : _("Elimina account");
}
static void privacy_menu_open(int focus) {
  static char l[PM_BLOCKS][200]; static const char *items[PM_N];
  const char *mode = S.status_mode[0] ? S.status_mode : "online";
  snprintf(l[PM_STATUS], sizeof l[0], _("Il mio stato: %s"), !strcmp(mode, "invisible") ? _("invisibile") : !strcmp(mode, "away") ? _("assente") : !strcmp(mode, "dnd") ? _("non disturbare") : _("online"));
  snprintf(l[PM_MESSAGES], sizeof l[0], "%s", strcmp(PV.messages, "friends") ? _("Chi pu\xC3\xB2 scrivermi: tutti") : _("Chi pu\xC3\xB2 scrivermi: solo amici"));
  snprintf(l[PM_REQUESTS], sizeof l[0], "%s", !strcmp(PV.requests, "nobody") ? _("Richieste di amicizia: nessuno") : !strcmp(PV.requests, "friends_of_friends") ? _("Richieste di amicizia: amici di amici") : _("Richieste di amicizia: tutti"));
  snprintf(l[PM_ACTIVITY], sizeof l[0], "%s", PV.activity ? _("Mostra agli amici cosa gioco e il mio tempo: s\xC3\xAC") : _("Mostra agli amici cosa gioco e il mio tempo: no"));
  snprintf(l[PM_RECORDS], sizeof l[0], "%s", !PV.activity ? _("Compari nei Record pubblici: no (attivit\xC3\xA0 nascosta)") : PV.records ? _("Compari nei Record pubblici: s\xC3\xAC") : _("Compari nei Record pubblici: no"));
  snprintf(l[PM_TROPHIES], sizeof l[0], "%s", !strcmp(PV.trophies, "nobody") ? _("Chi vede i miei trofei: nessuno") : !strcmp(PV.trophies, "friends") ? _("Chi vede i miei trofei: solo amici") : _("Chi vede i miei trofei: tutti (anche in classifica)"));
  snprintf(l[PM_IMPORT], sizeof l[0], "%s", PV.import ? _("Importa i trofei da questa console: s\xC3\xAC") : _("Importa i trofei da questa console: no"));
  for (int i = 0; i < PM_BLOCKS; i++) items[i] = l[i];
  items[PM_BLOCKS] = _("Utenti bloccati");
  for (int i = PM_NOTICE; i < PM_N; i++) items[i] = privacy_docs(i);
  menu_open(_("Privacy"), items, PM_N, privacy_pick, NULL);
  menu_select(focus);
}

void privacy_menu(void) {
  if (g_token[0]) { net_req(HTTP_GET, OMEGA_API "/privacy", NULL, on_privacy, NULL); return; }
  static const char *items[PM_GUEST_N];
  for (int i = 0; i < PM_GUEST_N; i++) items[i] = privacy_docs(PM_GUEST_FIRST + i);
  menu_open(_("Privacy e termini"), items, PM_GUEST_N, privacy_pick_guest, NULL);
}

// ------------------------------------------------------------ termini nuovi --
static int terms_pending;
static void on_terms_ok(int st, JVal *j, const char *raw, void *ud) {
  (void)j; (void)raw; (void)ud;
  if (st == 200) set_msg(_("Grazie! Termini accettati"), 0);
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
  confirm_open(_("Termini d'uso e privacy sono stati aggiornati (Impostazioni > Privacy). Li accetti?"), _("Accetto"), terms_yes, NULL);
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
  char m[256]; snprintf(m, sizeof m, _("Collegato a %s: accedi o crea un account"), srv_name(s));
  set_msg(m, 0);
}
static void switch_yes(int idx, void *ud) { (void)idx; (void)ud; switch_now(srv_target); }

static void ask_switch(int i) {
  const Server *s = srv_get(i);
  if (!s || i == srv_current()) return;
  srv_target = i;
  if (!g_token[0]) { switch_now(i); return; }
  static char q[512];
  snprintf(q, sizeof q, _("Collegarsi a %s (%s)? Uscirai dall'account di questo server."), srv_name(s), srv_host(s));
  confirm_open(q, _("Collegati"), switch_yes, NULL);
}

static void remove_yes(int idx, void *ud) {
  (void)idx; (void)ud;
  if (srv_target == srv_current()) switch_now(0);
  srv_remove(srv_target);
  set_msg(_("Server rimosso"), 0);
}
static void remove_pick(int idx, void *ud) {
  (void)ud;
  srv_target = idx + 1;    // l'elenco non contiene Omega
  const Server *s = srv_get(srv_target);
  if (!s) return;
  static char q[512];
  snprintf(q, sizeof q, _("Togliere %s (%s) dall'elenco?"), srv_name(s), srv_host(s));
  confirm_open(q, _("Togli"), remove_yes, NULL);
}

static void add_server(void) {
  char url[200] = "https://", norm[200], name[48];
  if (!edit_text(_("Indirizzo del server (es. https://mio-server.it)"), url, sizeof url, 0)) return;
  if (srv_normalize(url, norm, sizeof norm) < 0) { set_msg(_("Indirizzo non valido"), 1); return; }
  snprintf(name, sizeof name, "%s", strstr(norm, "://") + 3);
  if (!edit_text(_("Nome del server"), name, sizeof name, 0)) return;
  int i = srv_add(name, norm);
  if (i == -2) { set_msg(_("Elenco pieno: togli un server prima"), 1); return; }
  if (i < 0) { set_msg(_("Indirizzo non valido"), 1); return; }
  if (i == srv_current()) { set_msg(_("Stai già usando questo server"), 0); return; }
  if (!g_token[0]) { switch_now(i); return; }
  set_msg(_("Server aggiunto"), 0);
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
    menu_open(_("Togli un server"), items, n - 1, remove_pick, NULL);
  }
}

void server_menu(void) {
  static char names[SRV_MAX + 2][200]; static const char *items[SRV_MAX + 2];
  int n = srv_count(), k = 0;
  for (int i = 0; i < n; i++, k++) {
    const Server *s = srv_get(i);
    snprintf(names[k], sizeof names[k], i == srv_current() ? _("%s \xC2\xB7 %s  (in uso)") : "%s \xC2\xB7 %s", srv_name(s), srv_host(s));
    items[k] = names[k];
  }
  snprintf(names[k], sizeof names[k], "%s", _("+ Aggiungi un server")); items[k] = names[k]; k++;
  if (n > 1) { snprintf(names[k], sizeof names[k], "%s", _("Togli un server dall'elenco")); items[k] = names[k]; k++; }
  menu_open(_("Server"), items, k, server_pick, NULL);
}

const char *server_label(void) { return srv_get(srv_current())->name; }
