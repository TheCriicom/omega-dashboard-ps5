// Omega UI — home in stile console:
//   barra: Giochi · Esplora ............ store  cerca  browser  notifiche  amici  ⚙  avatar  ora
//   fila dei giochi (quello a fuoco si ingrandisce e lo sfondo sfuma sul suo)
//   titolo  ·  [ Gioca ]  [ ••• ]
//   schede: amici che giocano ora · chi ci ha giocato · notizie del gioco
//   attività degli amici
// Scendendo, la pagina scorre verso l'alto come sulla console.
#include "app.h"
#include <dirent.h>
#include <fcntl.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

AppEntry apps[MAX_APPS];
int napps;

#ifdef PS5
typedef struct { uint32_t sz; uint32_t user_id; uint32_t app_opt; uint64_t crash_report; uint64_t check_flag; } LncAppParam;
int sceUserServiceGetForegroundUser(uint32_t *userId);
int sceLncUtilLaunchApp(const char *tid, const char *argv[], LncAppParam *param);
int sceLncUtilKillApp(uint32_t appId);
int sceSystemServiceGetAppIdOfRunningBigApp(void);
int sceSystemServiceGetAppTitleId(int appId, char *titleId);
static const char *APP_DIRS[] = { "/user/app", "/system_ex/app" };
static const char *META_DIRS[] = { "/user/appmeta", "/user/app", "/system_ex/app" };
static const char *META_SUB[] = { "", "/sce_sys", "/sce_sys" };
#define N_APP_DIRS 2
#define N_META 3
#else
static const char *APP_DIRS[] = { OMEGA_DIR "/apps" };
static const char *META_DIRS[] = { OMEGA_DIR "/apps" };
static const char *META_SUB[] = { "/sce_sys" };
#define N_APP_DIRS 1
#define N_META 1
#endif

// -------------------------------------------------------------------- stato --
enum { Z_TOP, Z_ROW, Z_ACT, Z_CARDS, Z_FEED };
enum { T_GAMES, T_EXPLORE };
static int tab = T_GAMES, zone = Z_ROW;
static int app_sel, top_sel = 0, act_sel, card_sel, feed_sel;
static int ex_zone = 1, ex_news_sel, ex_fr_sel, ex_act_sel;   // Esplora: 1 notizie, 2 amici, 3 attività
static float row_scroll, page_scroll, page_scroll_t, tab_anim;
static float tile_size[MAX_APPS];
static float move_lift;                 // Sposta: quanto è sollevata la tessera presa (0..1)
static float sel_anim_card, sel_anim_feed, sel_anim_act, sel_anim_top;
static Uint32 focus_at; static int focus_loaded = -1;
static Uint32 last_feed;
static Uint32 entered_at;
static int launching = -1; static float launch_t; static Uint32 launch_done_at;

#define TOP_ICONS 6
#define TOP_N (2 + TOP_ICONS)  // Giochi, Esplora, Musica, Cerca, Notifiche, Game Base, Impostazioni, Profilo
#define META_MAX (128 * 1024 - 1)
#define FOCUS_MS 300   // il fuoco deve restare fermo tanto prima di caricare sfondo e scheda

// ------------------------------------------------------------ scansione app --
static int is_title_id(const char *s) {
  if (strlen(s) != 9) return 0;
  for (int i = 0; i < 4; i++) if (s[i] < 'A' || s[i] > 'Z') return 0;
  for (int i = 4; i < 9; i++) if (s[i] < '0' || s[i] > '9') return 0;
  return 1;
}

// param.sfo (giochi PS4) da un buffer: titolo in italiano (TITLE_05) se c'è, altrimenti TITLE
static int sfo_title_buf(const char *b, int len, char *out, size_t n) {
  if (len <= 20 || memcmp(b, "\0PSF", 4)) return 0;
  uint32_t keyt = *(const uint32_t *)(b + 8), datat = *(const uint32_t *)(b + 12), cnt = *(const uint32_t *)(b + 16);
  int found = 0;
  for (int pass = 0; pass < 2 && !found; pass++) {
    const char *want = pass == 0 ? "TITLE_05" : "TITLE";
    for (uint32_t i = 0; i < cnt && 20 + i * 16 + 16 <= (uint32_t)len; i++) {
      const unsigned char *e = (const unsigned char *)b + 20 + i * 16;
      uint16_t ko = *(const uint16_t *)e; uint32_t dl = *(const uint32_t *)(e + 4), doff = *(const uint32_t *)(e + 12);
      if (keyt + ko >= (uint32_t)len || datat + doff + dl > (uint32_t)len) continue;
      if (!strcmp(b + keyt + ko, want)) { snprintf(out, n, "%.*s", (int)dl, b + datat + doff); found = out[0] != 0; break; }
    }
  }
  return found;
}

static int sfo_title(const char *path, char *out, size_t n) {
  size_t len = 0; char *b = file_read(path, META_MAX, &len);
  if (!b) return 0;
  int ok = sfo_title_buf(b, (int)len, out, n);
  free(b);
  return ok;
}

#ifdef PS5
static uint32_t be32(const unsigned char *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

// Giochi PS4 installati come un unico app.pkg, senza param.sfo estratto: il
// param.sfo sta in chiaro nella tabella delle voci dell'intestazione (id 0x1000).
static int pkg_title(const char *path, char *out, size_t n) {
  int fd = open(path, O_RDONLY); if (fd < 0) return 0;
  unsigned char h[32]; int ok = 0;
  if (read(fd, h, 32) == 32 && !memcmp(h, "\x7f" "CNT", 4)) {
    uint32_t count = be32(h + 0x10), table = be32(h + 0x18);
    if (count && count < 4096) {
      unsigned char *t = malloc((size_t)count * 32);
      if (t && lseek(fd, table, SEEK_SET) == (off_t)table && read(fd, t, (size_t)count * 32) == (ssize_t)(count * 32)) {
        for (uint32_t i = 0; i < count && !ok; i++) {
          const unsigned char *e = t + i * 32;
          if (be32(e) != 0x1000) continue;                 // param.sfo
          uint32_t off = be32(e + 16), size = be32(e + 20);
          if (!size || size > 256 * 1024) break;
          char *sfo = malloc(size);
          if (sfo && lseek(fd, off, SEEK_SET) == (off_t)off && read(fd, sfo, size) == (ssize_t)size) ok = sfo_title_buf(sfo, (int)size, out, n);
          free(sfo);
        }
      }
      free(t);
    }
  }
  close(fd);
  return ok;
}

// ripiego: il nome usato dal riconoscimento vocale (pronunciation.xml), in inglese
static int pron_title(const char *path, char *out, size_t n) {
  char *b = file_read(path, META_MAX, NULL); if (!b) return 0;
  int ok = 0;
  const char *l = strstr(b, "<language id=\"01\""); if (!l) l = b;
  const char *t = strstr(l, "<text>");
  if (t) { t += 6; const char *e = strstr(t, "</text>"); if (e && e > t) { snprintf(out, n, "%.*s", (int)(e - t), t); ok = out[0] != 0; } }
  free(b);
  return ok;
}
#endif

// param.json (giochi PS5): titolo nella lingua dell'app se c'è, altrimenti lingua predefinita
static int json_title(const char *path, char *out, size_t n) {
  char *b = file_read(path, META_MAX, NULL);
  if (!b) return 0;
  int ok = 0;
  JVal *j = json_parse(b);
  JVal *lp = jget(j, "localizedParameters");
  if (lp) {
    const char *def = jstr(lp, "defaultLanguage", "en-US");
    const char *langs[3] = { i18n_locale(), def, "en-US" };
    for (int i = 0; i < 3 && !ok; i++) {
      const char *t = jstr(jget(lp, langs[i]), "titleName", NULL);
      if (t && *t) { snprintf(out, n, "%s", t); ok = 1; }
    }
  }
  if (!ok) { const char *t = jstr(j, "titleName", NULL); if (t && *t) { snprintf(out, n, "%s", t); ok = 1; } }
  json_free(j);
  if (!ok && json_str(b, "titleName", out, n) == 0 && out[0]) ok = 1;
  free(b);
  return ok;
}

static void app_meta(AppEntry *a) {
  char p[300];
  a->name[0] = a->icon[0] = a->art[0] = 0;
  for (int d = 0; d < N_META; d++) {
    if (!a->name[0]) { snprintf(p, sizeof p, "%s/%s%s/param.json", META_DIRS[d], a->tid, META_SUB[d]); json_title(p, a->name, sizeof a->name); }
    if (!a->name[0]) { snprintf(p, sizeof p, "%s/%s%s/param.sfo", META_DIRS[d], a->tid, META_SUB[d]); sfo_title(p, a->name, sizeof a->name); }
    if (!a->icon[0]) { snprintf(p, sizeof p, "%s/%s%s/icon0.png", META_DIRS[d], a->tid, META_SUB[d]); if (access(p, 0) == 0) snprintf(a->icon, sizeof a->icon, "%s", p); }
    if (!a->art[0]) {
      const char *pics[3] = { "pic1.png", "pic0.png", "pic2.png" };
      for (int k = 0; k < 3 && !a->art[0]; k++) {
        snprintf(p, sizeof p, "%s/%s%s/%s", META_DIRS[d], a->tid, META_SUB[d], pics[k]);
        if (access(p, 0) == 0) snprintf(a->art, sizeof a->art, "%s", p);
      }
    }
  }
#ifdef PS5
  if (!a->name[0]) { snprintf(p, sizeof p, "/user/app/%s/app.pkg", a->tid); pkg_title(p, a->name, sizeof a->name); }
  if (!a->name[0]) { snprintf(p, sizeof p, "/user/appmeta/%s/pronunciation.xml", a->tid); pron_title(p, a->name, sizeof a->name); }
#endif
  if (!a->name[0]) snprintf(a->name, sizeof a->name, "%s", a->tid);
}

// "Ultimi giocati": quando si è avviato ogni titolo (OMEGA_DIR/recent.txt, "TID secondi")
#define RECENT_FILE OMEGA_DIR "/recent.txt"
static long recent_of(const char *tid) {
  FILE *f = fopen(RECENT_FILE, "r"); if (!f) return 0;
  char t[40]; long v, out = 0;
  while (fscanf(f, "%39s %ld", t, &v) == 2) if (!strcmp(t, tid)) out = v;
  fclose(f); return out;
}
static void recent_note(const char *tid) {
  char lines[200][56]; int n = 0;
  FILE *f = fopen(RECENT_FILE, "r");
  if (f) { char t[40]; long v; while (n < 199 && fscanf(f, "%39s %ld", t, &v) == 2) if (strcmp(t, tid)) snprintf(lines[n++], sizeof lines[0], "%s %ld", t, v); fclose(f); }
  f = fopen(RECENT_FILE, "w"); if (!f) return;
  for (int i = 0; i < n; i++) fprintf(f, "%s\n", lines[i]);
  fprintf(f, "%s %ld\n", tid, (long)time(NULL));
  fclose(f);
}

static int app_cmp(const AppEntry *a, const AppEntry *b) {
  if (g_prefs.sort == 1 && a->last_played != b->last_played) return a->last_played > b->last_played ? -1 : 1;
  if (g_prefs.sort == 2) { int ka = a->hb || a->pld, kb = b->hb || b->pld; if (ka != kb) return ka - kb; }
  return strcasecmp(a->name, b->name);
}

void scan_apps(void) {
  for (int i = 0; i < napps; i++) if (apps[i].tex) SDL_DestroyTexture(apps[i].tex);
  napps = 0;
  for (int d = 0; d < N_APP_DIRS && napps < MAX_APPS; d++) {
    DIR *dp = opendir(APP_DIRS[d]); if (!dp) continue;
    struct dirent *e;
    while ((e = readdir(dp)) && napps < MAX_APPS) {
      if (!is_title_id(e->d_name) || !strncmp(e->d_name, "NPXS", 4)) continue;
#ifdef PS5
      // solo i titoli registrati nel sistema (hanno l'appmeta): gli altri non si possono avviare
      { char meta[96]; snprintf(meta, sizeof meta, "/user/appmeta/%s", e->d_name); if (access(meta, 0) != 0) continue; }
#endif
      int dup = 0; for (int i = 0; i < napps; i++) if (!strcmp(apps[i].tid, e->d_name)) dup = 1;
      if (dup) continue;
      AppEntry *a = &apps[napps]; memset(a, 0, sizeof *a);
      snprintf(a->tid, sizeof a->tid, "%s", e->d_name);
      app_meta(a);
      a->avg = g_theme_base;
      napps++;
    }
    closedir(dp);
  }
  // homebrew websrv: compaiono come i giochi, tranne Omega stessa
  if (g_prefs.show_hb) {
    DIR *dp = opendir(OMEGA_HB_ROOT);
    struct dirent *e;
    while (dp && (e = readdir(dp)) && napps < MAX_APPS) {
      if (e->d_name[0] == '.' || !strcmp(e->d_name, "OmegaUI")) continue;
      char dir[256], f[320];
      snprintf(dir, sizeof dir, "%s/%s", OMEGA_HB_ROOT, e->d_name);
      int has_js = (snprintf(f, sizeof f, "%s/homebrew.js", dir), access(f, 0) == 0);
      int has_eboot = (snprintf(f, sizeof f, "%s/eboot.elf", dir), access(f, 0) == 0);
      if (!has_js && !has_eboot) continue;
      AppEntry *a = &apps[napps]; memset(a, 0, sizeof *a);
      a->hb = 1;
      snprintf(a->dir, sizeof a->dir, "%s", dir);
      snprintf(a->tid, sizeof a->tid, "HB%08x", fnv1a(e->d_name));
      if (!has_js || !hb_meta(dir, a->name, sizeof a->name, a->sub, sizeof a->sub)) snprintf(a->name, sizeof a->name, "%s", e->d_name);
      if (!hb_icon(dir, a->icon, sizeof a->icon)) a->icon[0] = 0;
      a->avg = g_theme_base;
      napps++;
    }
    if (dp) closedir(dp);
  }
  if (g_prefs.show_hb) napps = payload_scan(apps, napps, MAX_APPS);
  if (g_prefs.ext_games) napps = drives_merge(apps, napps, MAX_APPS);   // giochi sui dischi esterni
  for (int i = 0; i < napps; i++) apps[i].last_played = recent_of(apps[i].tid);
  for (int i = 0; i < napps; i++) for (int k = i + 1; k < napps; k++)
    if (app_cmp(&apps[k], &apps[i]) < 0) { AppEntry t = apps[i]; apps[i] = apps[k]; apps[k] = t; }
  // Le app di Omega stanno in home come tessere, prima dei giochi: Community
  // (bacheca, gruppi, record e trofei), Store, Browser e Installa PKG.
  {
    static const struct { int b; const char *tid, *name; int need_login; } SYS[] = {
      { 1, TID_COMMUNITY, N_("Community"), 1 }, { 3, TID_STORE, N_("Store"), 1 }, { 4, TID_BROWSER, N_("Browser"), 0 }, { 5, TID_PKGS, N_("Installa PKG"), 0 }, { 7, TID_GAMEUPD, N_("Aggiornamenti"), 0 }, { 6, TID_MOBILE, N_("App mobile"), 0 } };
    int k = 0;
    for (unsigned d = 0; d < sizeof SYS / sizeof *SYS; d++) {
      if ((SYS[d].need_login && !g_token[0]) || napps >= MAX_APPS) continue;
      memmove(&apps[k + 1], &apps[k], sizeof(AppEntry) * (size_t)(napps - k));
      AppEntry *a = &apps[k++]; memset(a, 0, sizeof *a);
      a->builtin = SYS[d].b; a->tex_state = 2; a->avg = C_ACC;
      snprintf(a->tid, sizeof a->tid, "%s", SYS[d].tid);
      snprintf(a->name, sizeof a->name, "%s", _(SYS[d].name));
      napps++;
    }
  }
  layout_apply();        // cartelle e app nascoste: la fila sono le prime nrow voci
  omega_log("scan_apps: %d titoli, %d tessere in home", napps, nrow);
}

static void on_icon(const char *key, SDL_Texture *t, SDL_Color avg, void *ud) {
  (void)ud;
  for (int i = 0; i < napps; i++) if (!strcmp(apps[i].tid, key)) {
    apps[i].tex = t; apps[i].avg = avg; apps[i].tex_state = 2; apps[i].appear = 0;
    if (i == app_sel && focus_loaded == i) bg_set_game(apps[i].tid, apps[i].art, apps[i].avg);
    return;
  }
  if (t) SDL_DestroyTexture(t);
}

static void request_icons(void) {
  for (int i = 0; i < napps; i++) if (apps[i].tex_state == 0) {
    apps[i].tex_state = 1;
    if (apps[i].icon[0]) load_req(LOAD_ICON, apps[i].tid, apps[i].icon, 256, 256, 30, RGB(30, 60, 140), on_icon, NULL);
    else apps[i].tex_state = 2;
  }
}

void home_enter(void) {
  static int once;
  if (!once) { once = 1; drives_after_game(); install_cleanup(); }   // giochi esterni rimasti montati dall'ultima partita, file orfani
  scan_apps();
  for (int i = 0; i < nrow; i++) tile_size[i] = 150;
  if (app_sel >= nrow) app_sel = 0;
  zone = Z_ROW; tab = T_GAMES; page_scroll = page_scroll_t = 0;
  request_icons();
  consync_start(0);      // nomi e icone dei giochi, trofei della console
  focus_at = SDL_GetTicks(); focus_loaded = -1;
  social_load_news(); social_load_activity(); social_load_friends();
  last_feed = SDL_GetTicks(); entered_at = SDL_GetTicks();
  if (!nrow) bg_set_default();
}

// ---------------------------------------------------------------- avvio app --
#ifdef PS5
// Chiude il gioco già aperto (sospeso) se è un altro: altrimenti il sistema
// rifiuta il nuovo avvio con 0x80940010 (ALREADY_RUNNING_KILL_NEEDED).
static int kill_running_game(const char *except_tid) {
  int app = sceSystemServiceGetAppIdOfRunningBigApp();
  if (app < 0) return 0;
  char tid[64] = ""; sceSystemServiceGetAppTitleId(app, tid);
  if (!tid[0] || !strncmp(tid, "NPXS", 4) || !strcmp(tid, g_host_tid) || (except_tid && !strcmp(tid, except_tid))) return 0;
  int rc = sceLncUtilKillApp((uint32_t)app);
  omega_log("chiuso il gioco aperto %s (app 0x%x) rc=0x%x", tid, app, rc);
  SDL_Delay(1500);   // tempo al sistema per liberare le risorse
  return 1;
}
#endif

// ------------------------------------------------ avvio degli homebrew --
static SDL_atomic_t hbl_state;   // 0 libero, 1 richiesta in corso, 2 risposta arrivata
static int hbl_rc; static char hbl_err[400], hbl_dir[256], hbl_name[96]; static Uint32 hbl_at;
static int hbl_thread(void *ud) {
  (void)ud;
  hbl_rc = hb_launch(hbl_dir, -1, 0, hbl_err, sizeof hbl_err, NULL, 0, NULL);
  SDL_AtomicSet(&hbl_state, 2);
  return 0;
}
static void hbl_start(int idx) {
  if (SDL_AtomicGet(&hbl_state)) return;
  snprintf(hbl_dir, sizeof hbl_dir, "%s", apps[idx].dir); snprintf(hbl_name, sizeof hbl_name, "%s", apps[idx].name);
  hbl_at = SDL_GetTicks(); hbl_err[0] = 0;
  prefs_save();                  // websrv ci chiude di colpo: niente da perdere
  SDL_AtomicSet(&hbl_state, 1);
  SDL_Thread *t = SDL_CreateThreadWithStackSize(hbl_thread, "hblaunch", 256 * 1024, NULL);
  if (t) SDL_DetachThread(t); else hbl_thread(NULL);
}
static void hbl_poll(void) {
  int st = SDL_AtomicGet(&hbl_state);
  if (!st) return;
  Uint32 age = SDL_GetTicks() - hbl_at;
  if (st == 1) {
    // websrv bloccato (issue #52): la richiesta non torna mai
    if (age > 20000) { set_msg(_("websrv non risponde più: rimandalo dal caricatore di payload e riprova"), 1); unlink(OMEGA_DIR "/hb-launching"); SDL_AtomicSet(&hbl_state, 3); }
    return;
  }
  if (st == 3) return;           // il thread appeso finirà da sé
  if (hbl_rc < 0) { set_msg(hbl_err, 1); social_presence("online", NULL, NULL); SDL_AtomicSet(&hbl_state, 0); return; }
  if (hbl_rc == 2) {             // demone (es. Transmission): Omega resta aperta
    char m[256]; snprintf(m, sizeof m, _("%s avviato in background"), hbl_name);
    set_msg(m, 0); social_presence("online", NULL, NULL); SDL_AtomicSet(&hbl_state, 0); return;
  }
#ifdef PS5
  // websrv ha risposto ma Omega è ancora viva: di norma ci chiude prima di
  // rispondere. Si aspetta qualche secondo, poi si dice cosa fare.
  if (age < 12000) return;
  unlink(OMEGA_DIR "/hb-launching");
  set_msg(_("L'homebrew non è partito: websrv ha risposto ma non ha chiuso Omega. Prova ad aprirlo dalla Home di sistema o rimanda websrv."), 1);
#endif
  social_presence("online", NULL, NULL);
  SDL_AtomicSet(&hbl_state, 0);
}

static void do_launch(int idx) {
  recent_note(apps[idx].tid);   // per l'ordine "Ultimi giocati"
  if (apps[idx].ext) {
    // gioco su disco esterno: si monta (sola lettura) e, la prima volta, si registra
    char err[300];
    if (drives_prepare_launch(&apps[idx], err, sizeof err) != 0) { set_msg(err, 1); social_presence("online", NULL, NULL); return; }
  }
  if (apps[idx].hb) {
    // parametri già risolti da hb_step: qui parte solo la richiesta a websrv,
    // in un thread (può aspettare la rete); l'esito arriva in hbl_poll().
    // Omega non si chiude da sola: la chiude websrv quando avvia l'homebrew.
    hbl_start(idx);
    return;
  }
#ifdef PS5
  uint32_t uid = 0; sceUserServiceGetForegroundUser(&uid);
  const char *argv[] = { NULL };
  LncAppParam p; memset(&p, 0, sizeof p);
  p.sz = sizeof p; p.user_id = uid; p.check_flag = 1 /*SkipLaunchCheck*/;
  kill_running_game(apps[idx].tid);
  int rc = sceLncUtilLaunchApp(apps[idx].tid, argv, &p);
  omega_log("LncLaunch %s uid=0x%x rc=0x%x", apps[idx].tid, uid, rc);
  if (rc == (int)0x80940010 || rc == (int)0x8094000c) {
    // ancora un'app in mezzo: chiudila (anche se è lo stesso titolo, sospeso) e riprova
    int app = sceSystemServiceGetAppIdOfRunningBigApp();
    char tid[64] = ""; if (app >= 0) sceSystemServiceGetAppTitleId(app, tid);
    if (app >= 0 && strcmp(tid, g_host_tid)) { sceLncUtilKillApp((uint32_t)app); SDL_Delay(1500); }
    rc = sceLncUtilLaunchApp(apps[idx].tid, argv, &p);
    omega_log("LncLaunch (riprova) %s rc=0x%x", apps[idx].tid, rc);
  }
  if (rc == (int)0x80940031) { set_msg(_("Questo titolo non è registrato nel sistema e non può partire"), 1); social_presence("online", NULL, NULL); return; }
#else
  int rc = 0;
  omega_log("(desktop) avvio simulato di %s", apps[idx].tid);
#endif
  if (rc < 0) { char e[160]; snprintf(e, sizeof e, _("Avvio non riuscito (0x%x)"), rc); set_msg(e, 1); social_presence("online", NULL, NULL); return; }
#ifdef PS5
  // Omega si chiude e lascia tutta la memoria al gioco; al ritorno alla Home
  // il demone omega_redirect la riapre
  app_quit_later(2500);
#endif
}

static void start_launch(int idx);
// Homebrew websrv: homebrew.js si valuta un passo alla volta (click, sottomenu,
// scelta di file) e può usare la rete, quindi ogni passo gira in un thread e la
// home resta viva. L'esito arriva in hb_poll().
#define HB_MENU_N 16             // voci per pagina: quante ne mostra menu_open
static char hb_names[HB_MENU_N][64]; static const char *hb_items[HB_MENU_N]; static int hb_pending = -1;
static SDL_atomic_t hb_state;    // 0 libero, 1 al lavoro, 2 finito
static void hb_pick(int i, void *ud);
static int hb_rc, hb_nn, hb_arg, hb_shown; static Uint32 hb_t0;
static char hb_err[400], hb_dir[256], hb_tid[16];
static int hb_thread(void *ud) {
  (void)ud;
  hb_nn = 0;
  hb_rc = hb_launch(hb_dir, hb_arg, 1, hb_err, sizeof hb_err, hb_names, HB_MENU_N, &hb_nn);
  SDL_AtomicSet(&hb_state, 2);
  return 0;
}
// choice: -1 click sull'homebrew, >= 0 voce scelta nel menu precedente
static void hb_step(int idx, int choice) {
  if (SDL_AtomicGet(&hb_state)) return;
  snprintf(hb_dir, sizeof hb_dir, "%s", apps[idx].dir); snprintf(hb_tid, sizeof hb_tid, "%s", apps[idx].tid);
  hb_arg = choice; hb_t0 = SDL_GetTicks(); hb_shown = 0;
  SDL_AtomicSet(&hb_state, 1);
  SDL_Thread *t = SDL_CreateThreadWithStackSize(hb_thread, "hbstep", 2 * 1024 * 1024, NULL);
  if (t) SDL_DetachThread(t); else hb_thread(NULL);
}
static void hb_poll(void) {
  int s = SDL_AtomicGet(&hb_state);
  if (s == 1 && !hb_shown && SDL_GetTicks() - hb_t0 > 500) { hb_shown = 1; set_msg(_("Caricamento dell'homebrew..."), 0); }
  if (s != 2) return;
  SDL_AtomicSet(&hb_state, 0);
  int idx = -1;
  for (int i = 0; i < napps; i++) if (!strcmp(apps[i].tid, hb_tid)) idx = i;
  if (idx < 0) return;
  if (hb_rc < 0) { set_msg(hb_err, 1); return; }
  if (hb_shown) set_msg("", 0);
  if (hb_rc == 1) {
    for (int i = 0; i < hb_nn; i++) hb_items[i] = hb_names[i];
    hb_pending = idx;
    // titolo: quello dello script (es. pickFile) o il nome dell'app, con l'eventuale pagina "(1/3)"
    int own = hb_err[0] && hb_err[0] != '(';
    char t[96]; snprintf(t, sizeof t, "%s%s%s", own ? "" : apps[idx].name, !own && hb_err[0] ? " " : "", hb_err);
    menu_open(t, hb_items, hb_nn, hb_pick, NULL);
    return;
  }
  start_launch(idx);
}
// sottomenu degli homebrew: la scelta avvia il passo successivo
static void hb_pick(int i, void *ud) {
  (void)ud;
  if (hb_pending < 0 || hb_pending >= napps || i < 0) return;
  hb_step(hb_pending, i);
}

void launch_app(int idx) {
  if (idx < 0 || idx >= napps || launching >= 0 || SDL_AtomicGet(&hb_state)) return;
  if (SDL_AtomicGet(&hbl_state) == 1) { set_msg(_("Avvio già in corso..."), 0); return; }
  if (apps[idx].builtin == 1) { community_open(0); return; }
  if (apps[idx].builtin == 2) { folder_open(apps[idx].tid); return; }
  if (apps[idx].builtin == 3) { sfx_play(SFX_OPEN); store_open(); return; }
  if (apps[idx].builtin == 4) { sfx_play(SFX_OPEN); browser_open(NULL); return; }
  if (apps[idx].builtin == 5) { sfx_play(SFX_OPEN); pkgs_open(); return; }
  if (apps[idx].builtin == 6) { sfx_play(SFX_OPEN); mobile_open(); return; }
  if (apps[idx].builtin == 7) { sfx_play(SFX_OPEN); gameupd_open(); return; }
  if (apps[idx].hb) { hb_step(idx, -1); return; }
  if (apps[idx].pld) {
    char err[400];
    if (payload_run(apps[idx].dir, err, sizeof err) != 0) { set_msg(err, 1); return; }
    char m[256]; snprintf(m, sizeof m, _("%s avviato in background"), apps[idx].name);
    sfx_play(SFX_LAUNCH); set_msg(m, 0);
    return;
  }
  start_launch(idx);
}

static void start_launch(int idx) {
  if (idx < 0 || idx >= napps || launching >= 0) return;
  launching = idx; launch_t = 0; launch_done_at = 0;
  sfx_play(SFX_LAUNCH);
  social_presence("online", apps[idx].tid, apps[idx].name);
}

// ---------------------------------------------------- grafica delle notizie --
typedef struct { int color, w, h, r, top; SDL_Texture *t; Uint32 used; } ArtSlot;
static ArtSlot arts[24];

static SDL_Texture *make_art(int color, int w, int h, int r, int top_only) {
  SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
  if (!s) return NULL;
  Col a = avatar_col(color * 3 + 1), b = avatar_col(color * 3 + 2);
  if (color == 0) { a = RGB(0, 70, 200); b = RGB(0, 180, 255); }
  Uint32 *px = s->pixels; int pitch = s->pitch / 4;
  for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
    float fx = (float)x / w, fy = (float)y / h;
    float t = clampf(fx * 0.7f + fy * 0.5f, 0, 1);
    float g1 = 0.5f / (1 + ((fx - 0.8f) * (fx - 0.8f) + (fy - 0.2f) * (fy - 0.2f)) * 14);
    float rings = 0.06f * sinf((hypotf(fx - 0.85f, fy - 0.1f)) * 40.0f) * (1 - fy);
    float k = 0.55f + g1 + rings;
    int R_ = (int)((a.r + (b.r - a.r) * t) * k), G_ = (int)((a.g + (b.g - a.g) * t) * k), B_ = (int)((a.b + (b.b - a.b) * t) * k);
    R_ = R_ > 255 ? 255 : R_; G_ = G_ > 255 ? 255 : G_; B_ = B_ > 255 ? 255 : B_;
    float cov = 1;
    float ccx = x < r ? (float)r : (x >= w - r ? (float)(w - r) : -1), ccy = y < r ? (float)r : (!top_only && y >= h - r ? (float)(h - r) : -1);
    if (ccx >= 0 && ccy >= 0) cov = clampf((float)r + 0.5f - hypotf(x + 0.5f - ccx, y + 0.5f - ccy), 0, 1);
    px[y * pitch + x] = ((Uint32)(cov * 255) << 24) | ((Uint32)R_ << 16) | ((Uint32)G_ << 8) | (Uint32)B_;
  }
  SDL_Texture *t = SDL_CreateTextureFromSurface(R, s);
  SDL_FreeSurface(s);
  if (t) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
  return t;
}

// Immagini degli articoli, ritagliate alla misura della scheda, in una piccola cache.
typedef struct { char key[40]; SDL_Texture *t; int state; Uint32 used; float appear; } NewsImg;
static NewsImg nimg[40];
static void on_news_img(const char *key, SDL_Texture *t, SDL_Color avg, void *ud) {
  (void)avg; (void)ud;
  for (int i = 0; i < 40; i++) if (nimg[i].state == 1 && !strcmp(nimg[i].key, key)) {
    nimg[i].t = t; nimg[i].state = t ? 2 : 3; nimg[i].appear = 0; return;
  }
  if (t) SDL_DestroyTexture(t);
}
static NewsImg *news_image(const News *n, int w, int h, int r) {
  char key[40]; snprintf(key, sizeof key, "%s:%dx%d", n->id, w, h);
  NewsImg *victim = NULL;
  for (int i = 0; i < 40; i++) {
    if (nimg[i].state && !strcmp(nimg[i].key, key)) { nimg[i].used = g_frame; return &nimg[i]; }
    if (nimg[i].state == 1) continue;                       // in arrivo
    if (!victim || !nimg[i].state || (victim->state && nimg[i].used < victim->used)) victim = &nimg[i];
  }
  if (!victim) return NULL;
  if (victim->t) SDL_DestroyTexture(victim->t);
  memset(victim, 0, sizeof *victim);
  snprintf(victim->key, sizeof victim->key, "%s", key);
  victim->state = 1; victim->used = g_frame;
  char path[96]; snprintf(path, sizeof path, OMEGA_API "/news/%s/image", n->id);
  load_req(LOAD_URL, key, path, w, h, -r, RGB(0, 0, 0), on_news_img, NULL);
  return victim;
}

void news_art(const News *n, int x, int y, int w, int h, int radius, int alpha) {
  // radius < 0: solo gli angoli in alto (immagine in cima a una scheda)
  int top = radius < 0; if (top) radius = -radius;
  NewsImg *img = (n && n->has_image && top) ? news_image(n, w, h, radius) : NULL;
  if (img && img->state == 2 && img->t) {
    img->appear = approach(img->appear, 1, 8.0f);
    if (img->appear < 1) news_art(NULL, x, y, w, h, -radius, alpha);   // dissolvenza dalla grafica generata
    draw_tex(img->t, x, y, w, h, (int)(alpha * img->appear));
    grad_v(x, y + h / 2, w, h - h / 2, C_BLACK, 0, C_BLACK, alpha * 55 / 100);
    return;
  }
  int color = n ? (n->color & 7) : 0;
  ArtSlot *hit = NULL, *victim = &arts[0];
  for (int i = 0; i < 24; i++) {
    if (arts[i].t && arts[i].color == color && arts[i].w == w && arts[i].h == h && arts[i].r == radius && arts[i].top == top) { hit = &arts[i]; break; }
    if (!arts[i].t) { victim = &arts[i]; break; }
    if (arts[i].used < victim->used) victim = &arts[i];
  }
  if (!hit) {
    if (victim->t) SDL_DestroyTexture(victim->t);
    victim->t = make_art(color, w, h, radius, top); victim->color = color; victim->w = w; victim->h = h; victim->r = radius; victim->top = top;
    hit = victim;
  }
  hit->used = g_frame;
  draw_tex(hit->t, x, y, w, h, alpha);
  draw_icon(IC_NEWS, x + w - 60, y + 56, 56, C_WHITE, alpha * 35 / 100);
}

// --------------------------------------------------------------- barra alta --
// ora secondo Personalizza: 24 o 12 ore, con o senza secondi
void clock_text(char *out, size_t n) {
  time_t tt = time(NULL); struct tm *lt = localtime(&tt);
  int h = lt ? lt->tm_hour : 0, m = lt ? lt->tm_min : 0, sec = lt ? lt->tm_sec : 0;
  if (g_prefs.clock12) {
    int h12 = h % 12 ? h % 12 : 12;
    if (g_prefs.clock_sec) snprintf(out, n, "%d:%02d:%02d %s", h12, m, sec, h < 12 ? "AM" : "PM");
    else snprintf(out, n, "%d:%02d %s", h12, m, h < 12 ? "AM" : "PM");
  } else if (g_prefs.clock_sec) snprintf(out, n, "%02d:%02d:%02d", h, m, sec);
  else snprintf(out, n, "%02d:%02d", h, m);
}
// "lun 5 ott", con i nomi tradotti
void date_text(char *out, size_t n) {
  static const char *const WD[7] = { N_("dom"), N_("lun"), NULL, N_("mer"), N_("gio"), N_("ven"), N_("sab") };
  static const char *const MO[12] = { N_("gen"), N_("feb"), N_("mar"), N_("apr"), N_("mag"), N_("giu"), N_("lug"), N_("ago"), N_("set"), N_("ott"), N_("nov"), N_("dic") };
  time_t tt = time(NULL); struct tm *lt = localtime(&tt);
  if (!lt) { out[0] = 0; return; }
  const char *wd = lt->tm_wday == 2 ? P_("giorno", "mar") : _(WD[lt->tm_wday % 7]);   // "mar" da solo è marzo
  const char *mo = _(MO[lt->tm_mon % 12]);
  snprintf(out, n, "%s %d %s", wd, lt->tm_mday, mo);
}
static void top_bar(int alpha) {
  sel_anim_top = approach(sel_anim_top, (float)top_sel, 18.0f);
  int focus = zone == Z_TOP;
  int x = 110, y = 44;
  const char *tabs[2] = { _("Giochi"), _("Esplora") };
  for (int i = 0; i < 2; i++) {
    int on = tab == i;
    TTF_Font *f = font(on ? W_MED : W_LIGHT, 38);
    int w = text_w(f, tabs[i]);
    int foc = focus && top_sel == i;
    if (foc) fill_rrect(x - 22, y - 10, w + 44, 66, 33, C_WHITE, alpha * 22 / 100);
    draw_text(f, tabs[i], x, y, on ? C_WHITE : C_DIM, alpha, AL_L);
    if (on) fill_rrect(x + w / 2 - (int)(20 + 10 * tab_anim), y + 58, (int)(40 + 20 * tab_anim), 4, 2, C_WHITE, alpha);
    x += w + 64;
  }
  char clock[64]; clock_text(clock, sizeof clock);
  TTF_Font *cf = font(W_REG, 32);
  int rx = SCREEN_W - 80;
  rx -= draw_text(cf, clock, rx, y + 6, C_TXT, alpha, AL_R) + 22;
  // data e temperatura accanto all'ora (Personalizza › Orologio e barra)
  char extra[96] = "";
  if (g_prefs.clock_date) date_text(extra, sizeof extra);
  if (g_prefs.bar_temp) {
    static int temp = -1; static Uint32 temp_at;
    if (!temp_at || SDL_GetTicks() - temp_at > 5000) { temp = sys_cpu_temp(); temp_at = SDL_GetTicks(); }
    if (temp > 0) { size_t l = strlen(extra); snprintf(extra + l, sizeof extra - l, "%s%d\xC2\xB0" "C", l ? "  \xC2\xB7  " : "", temp); }
  }
  if (extra[0]) rx -= draw_text(font(W_REG, 24), extra, rx, y + 14, C_DIM, alpha, AL_R) + 22;
  rx -= 18;
  // Store e Browser sono tessere della fila, come le app
  const int icons[TOP_ICONS] = { IC_MUSIC, IC_SEARCH, IC_BELL, IC_FRIENDS, IC_GEAR, -1 };
  int badges[TOP_ICONS] = { 0, 0, S.unread_notif, S.in_req + S.unread_msg + S.ninv, 0, 0 };
  int pos[TOP_ICONS];
  // da destra: avatar, impostazioni, amici, notifiche, cerca, musica
  int cx = rx - 30;
  for (int k = TOP_ICONS - 1; k >= 0; k--) { pos[k] = cx; cx -= k == TOP_ICONS - 1 ? 96 : 84; }
  for (int k = 0; k < TOP_ICONS; k++) {
    int idx = 2 + k, foc = focus && top_sel == idx;
    int px = pos[k], py = y + 24;
    if (k == TOP_ICONS - 1) {
      if (foc) ring(px, py, 38, 3, C_WHITE, alpha);
      draw_avatar(S.me, S.my_avatar, px, py, 60, alpha);
      fill_circle(px + 22, py + 22, 9, RGB(10, 14, 24), alpha);
      fill_circle(px + 22, py + 22, 6, C_OK, alpha);
    } else {
      if (foc) fill_circle(px, py, 36, C_WHITE, alpha);
      // musica in corso: barrette al posto della nota, e il titolo sotto quando è a fuoco
      if (icons[k] == IC_MUSIC && music_now_line()) {
        music_mini(px - 12, py - 14, alpha);
        if (foc) draw_text_fit(font(W_REG, 22), music_now_line(), px, py + 46, 420, C_TXT, alpha, AL_C);
      } else draw_icon(icons[k], px, py, 38, foc ? RGB(10, 12, 20) : C_TXT, alpha);
      if (badges[k]) draw_badge(px + 22, py - 20, badges[k], alpha);
    }
  }
}

static void top_activate(void) {
  switch (top_sel) {
    case 0: tab = T_GAMES; tab_anim = 0; break;
    case 1: tab = T_EXPLORE; tab_anim = 0; social_load_news(); social_load_activity(); break;
    case 2: music_open(); break;
    case 3: search_open(); break;
    case 4: social_load_notifications(); ov_push(OV_NOTIF); break;
    case 5: gb_open(0); break;
    case 6: ov_push(OV_SETTINGS); break;
    case 7: profile_open(S.me); break;
  }
}

// -------------------------------------------------------------- fila giochi --
static int job_tiles(InstallView *iv);
static void draw_jobs(int *px, int ty, int s, int gap, int a);
// Una tessera della fila nella sua posizione (anche quella sollevata in Sposta).
static void row_tile(int i, int tx, int ty, int s, int alpha, float pulse) {
  int a = alpha;
  if (tx < 110) a = (int)(alpha * clampf(1 - (110 - tx) / 200.0f, 0, 1) * 0.6f);   // quelli già passati sfumano
  AppEntry *ap = &apps[i];
  int foc = i == app_sel;
  if (foc) shadow_rrect(tx, ty, s, s, 28, 26, a * 70 / 100);
  if (ap->builtin == 2) {
    // cartella: le prime quattro icone in un mosaico 2×2
    fill_rrect(tx, ty, s, s, 28, mix(C_PANEL, C_WHITE, 0.08f), a);
    stroke_rrect(tx, ty, s, s, 28, 2, C_WHITE, a * 18 / 100);
    int pad = s / 12, cell = (s - 3 * pad) / 2, k = 0;
    for (; k < 4; k++) {
      int j = layout_folder_app(ap->tid, k); if (j < 0) break;
      int cx = tx + pad + (k % 2) * (cell + pad), cy = ty + pad + (k / 2) * (cell + pad);
      if (apps[j].tex) draw_tex(apps[j].tex, cx, cy, cell, cell, a); else fill_rrect(cx, cy, cell, cell, 12, RGB(40, 48, 70), a);
    }
    if (!g_prefs.labels || foc) {
      fill_rrect(tx + 10, ty + s - 40, s - 20, 30, 15, RGB(8, 10, 20), a * 70 / 100);
      draw_text_fit(font(W_MED, 19), ap->name, tx + s / 2, ty + s - 37, s - 36, C_WHITE, a, AL_C);
    }
  } else if (ap->builtin) {
    sys_tile_draw(ap, tx, ty, s, a);
    if (ap->builtin == 1 && S.unread_groups) draw_badge(tx + s - 22, ty + 22, S.unread_groups, a);
  } else if (ap->tex) {
    ap->appear = approach(ap->appear, 1, 8.0f);
    SDL_SetTextureColorMod(ap->tex, 255, 255, 255);
    draw_tex(ap->tex, tx, ty, s, s, (int)(a * ap->appear));
  } else {
    fill_rrect(tx, ty, s, s, 28, RGB(30, 38, 60), a);
    draw_icon(IC_GAMEPAD, tx + s / 2, ty + s / 2 - 14, s / 3, C_DIM, a);
    draw_text_fit(font(W_REG, 20), ap->tid, tx + s / 2, ty + s - 46, s - 20, C_FAINT, a, AL_C);
  }
  if (foc && zone == Z_ROW && tab == T_GAMES) focus_ring(tx, ty, s, s, 28, pulse, a);
  // gioco su disco esterno: piccolo disco in basso a destra
  if (ap->ext) {
    fill_circle(tx + s - 24, ty + s - 24, 19, RGB(10, 14, 24), a * 90 / 100);
    draw_icon(IC_DRIVE, tx + s - 24, ty + s - 24, 24, C_ACC2, a);
  }
  // Personalizza › Nomi sotto le icone (quello scelto ha già il titolo grande)
  if (g_prefs.labels && !foc) draw_text_fit(font(W_REG, 19), ap->name, tx + s / 2, ty + s + 8, s + 10, C_DIM, a * 85 / 100, AL_C);
  // amici che ci stanno giocando
  int playing = 0; for (int k = 0; k < S.nfriends; k++) if (!strcmp(S.friends[k].game_id, ap->tid)) playing++;
  if (playing) {
    fill_rrect(tx + 10, ty + s - 42, 70, 32, 16, RGB(10, 14, 24), a * 85 / 100);
    draw_icon(IC_FRIENDS, tx + 32, ty + s - 26, 22, C_OK, a);
    char n[8]; snprintf(n, sizeof n, "%d", playing);
    draw_text(font(W_BOLD, 20), n, tx + 50, ty + s - 38, C_TXT, a, AL_L);
  }
}

static void game_row(int y0, int alpha) {
  if (!nrow) {
    int w = 900, h = 220, x = 110;
    fill_rrect(x, y0, w, h, 26, C_PANEL, alpha * 80 / 100);
    draw_icon(IC_GAMEPAD, x + 110, y0 + h / 2, 90, C_DIM, alpha);
    draw_text(font(W_MED, 34), _("Nessun gioco installato"), x + 200, y0 + 64, C_TXT, alpha, AL_L);
    draw_text(font(W_REG, 26), _("I giochi e le app installati compariranno qui."), x + 200, y0 + 116, C_DIM, alpha, AL_L);
    return;
  }
  // Personalizza › Dimensione delle icone: piccole, medie, grandi
  static const float BASE[3] = { 124, 150, 178 }, BIG[3] = { 200, 236, 270 }, MID[3] = { 170, 200, 230 };
  int ts = g_prefs.tiles % 3, gap = g_prefs.labels ? 30 : 22;
  for (int i = 0; i < nrow; i++) {
    float target = (i == app_sel && tab == T_GAMES) ? (zone == Z_ROW ? BIG[ts] : MID[ts]) : BASE[ts];
    tile_size[i] = approach(tile_size[i], target, 14.0f);
  }
  // tessere di lavoro (installazioni, ricezioni dal PC) dopo le app di Omega
  int nsys = 0; while (nsys < nrow && SYS_TILE(&apps[nsys])) nsys++;
  InstallView iv; int njob = job_tiles(&iv); int jobw = njob * ((int)BASE[ts] + gap + 60);   // installazioni, spostamenti, ricezioni
  float before = 0; for (int i = 0; i < app_sel; i++) before += tile_size[i] + gap;
  if (app_sel >= nsys) before += jobw;
  row_scroll = approach(row_scroll, before, 12.0f);
  float xr = 0;                                  // posizione nella fila, prima dello scorrimento
  float pulse = 0.5f + 0.5f * sinf((float)g_time * 3.2f);
  int mvi = layout_moving(), mx = 0, ms = 0;
  move_lift = approach(move_lift, mvi >= 0 ? 1.0f : 0.0f, 10.0f);
  for (int i = 0; i < nrow; i++) {
    if (i == nsys && njob) { int j0 = (int)(110 - row_scroll + xr), jx = j0; draw_jobs(&jx, y0 + (int)(BIG[ts] - BASE[ts]) / 2, (int)BASE[ts], gap, alpha); xr += (float)(jx - j0); }
    AppEntry *ap = &apps[i];
    // ognuna insegue il suo posto: riordinando, nascondendo o aggiungendo, le tessere scorrono invece di saltare
    if (!ap->vx_ok) { ap->vx = xr; ap->vx_ok = 1; }
    ap->vx = approach(ap->vx, xr, 16.0f);
    int s = (int)tile_size[i];
    int tx = (int)(110 - row_scroll + ap->vx), ty = y0;
    xr += s + gap;
    if (i == mvi) { mx = tx; ms = s; continue; }  // la si disegna sopra tutte, dopo
    if (tx > SCREEN_W || tx + s < -40) continue;
    row_tile(i, tx, ty, s, mvi >= 0 ? alpha * 55 / 100 : alpha, pulse);
  }
  // Sposta: la tessera presa, un po' più grande, appena sollevata e con
  // un'ombra ampia, che ondeggia piano; sotto, le frecce dove può ancora andare
  if (mvi >= 0) {
    float l = ease_out(clampf(move_lift, 0, 1));
    int s = (int)(ms * (1 + 0.08f * l));
    int tx = mx - (s - ms) / 2, ty = y0 - (int)(10 * l + 3 * sinf((float)g_time * 3.0f) * l) - (s - ms) / 2;
    shadow_rrect(tx, ty + (int)(22 * l), s, s, 30, 44, alpha * 65 / 100);
    row_tile(mvi, tx, ty, s, alpha, 1.0f);
    stroke_rrect(tx - 6, ty - 6, s + 12, s + 12, 34, 4, C_WHITE, (int)(alpha * (0.75f + 0.25f * pulse)));
    int cx = tx + s / 2, cy = y0 + ms + 34, la = (int)(alpha * l);
    if (mvi > 0) { fill_circle(cx - 30, cy, 20, RGB(255, 255, 255), la * 90 / 100); draw_text(font(W_BOLD, 28), "\xE2\x80\xB9", cx - 30, cy - 19, RGB(12, 14, 22), la, AL_C); }
    if (mvi < nrow - 1) { fill_circle(cx + 30, cy, 20, RGB(255, 255, 255), la * 90 / 100); draw_text(font(W_BOLD, 28), "\xE2\x80\xBA", cx + 30, cy - 19, RGB(12, 14, 22), la, AL_C); }
  }
}

// Tessere delle app di Omega: sfumatura propria, icona grande e nome.
void sys_tile_draw(const AppEntry *ap, int tx, int ty, int s, int a) {
  Col c1, c2; int ic;
  switch (ap->builtin) {
    case 3: c1 = RGB(255, 120, 60); c2 = RGB(200, 40, 110); ic = IC_STORE; break;
    case 4: c1 = RGB(40, 200, 220); c2 = RGB(30, 90, 210); ic = IC_GLOBE; break;
    case 5: c1 = RGB(70, 210, 130); c2 = RGB(20, 120, 110); ic = IC_BOX; break;
    case 6: c1 = RGB(196, 91, 255); c2 = RGB(70, 60, 200); ic = IC_CHAT; break;
    case 7: c1 = RGB(80, 170, 255); c2 = RGB(30, 70, 190); ic = IC_DOWNLOAD; break;
    default: c1 = mix(C_ACC, C_WHITE, 0.15f); c2 = mix(C_ACC, RGB(12, 16, 30), 0.45f); ic = IC_FRIENDS; break;
  }
  // sfumatura dall'alto: strati arrotondati sempre più bassi, niente bordi squadrati
  fill_rrect(tx, ty, s, s, 28, c2, a);
  for (int k = 0; k < 6; k++) fill_rrect(tx, ty, s, s - k * s / 7, 28, c1, a * 13 / 100);
  draw_icon(ic, tx + s / 2, ty + s / 2 - s / 12, s * 44 / 100, C_WHITE, a);
  draw_text_fit(font(W_MED, s > 190 ? 24 : 19), ap->name, tx + s / 2, ty + s - (s > 190 ? 50 : 38), s - 20, C_WHITE, a, AL_C);
}

// ------------------------------------------- installazioni e caricamenti --
// Come sulla PS4: ciò che si sta installando (o arriva dal PC) compare nella
// fila con la sua icona e la barra che avanza; gli errori restano scritti lì.
static struct { int active; char name[96]; long long done, total; int files, nfiles; } upj;
static Uint32 jobs_at;
static void on_jobs(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; (void)ud;
  if (st != 200 || !j) { upj.active = 0; return; }
  JVal *u = jget(j, "upload");
  upj.active = jbool(u, "active");
  jcpy(upj.name, sizeof upj.name, u, "name");
  upj.done = (long long)jnum(u, "done", 0); upj.total = (long long)jnum(u, "total", 0);
  // giochi che il telefono o il PC hanno chiesto di installare subito
  JFOR(it, jget(j, "install")) {
    InstallReq r; memset(&r, 0, sizeof r);
    jcpy(r.url, sizeof r.url, it, "url"); jcpy(r.name, sizeof r.name, it, "title");
    jcpy(r.title_id, sizeof r.title_id, it, "title_id"); jcpy(r.icon, sizeof r.icon, it, "cover");
    jcpy(r.lib_id, sizeof r.lib_id, it, "id");
    const char *k = jstr(it, "kind", "auto");
    r.kind = !strcmp(k, "pkg") ? 1 : !strcmp(k, "zip") ? 2 : !strcmp(k, "elf") ? 3 : !strcmp(k, "folder") ? 4 : 0;
    if (strncmp(r.url, "file://", 7)) r.lib_id[0] = 0;   // solo i file caricati si tolgono dopo
    if (r.url[0]) install_begin(&r);
  }
}
// "Installa sulla PS5" chiesto dalla web app: comandi in coda sul server
static Uint32 queue_at;
static void on_queue(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; (void)ud;
  if (st != 200 || !j) return;
  JFOR(it, jget(j, "items")) { const char *id = jstr(it, "app_id", ""); if (*id) store_install_remote(id); }
}
static void jobs_poll(void) {
  Uint32 now = SDL_GetTicks();
  if (g_token[0] && now - queue_at > 15000) { queue_at = now; net_req(HTTP_GET, OMEGA_API "/console/queue", NULL, on_queue, NULL); }
  if (now - jobs_at < (upj.active ? 1000u : 2500u)) return;
  jobs_at = now;
  net_req(HTTP_GET, "http://127.0.0.1:9095/v1/console/jobs", NULL, on_jobs, NULL);
}

// icone delle tessere di lavoro (file locali o URL), in una piccola cache
typedef struct { char key[320]; SDL_Texture *t; int st; } JobIcon;
static JobIcon jicon[4];
static void on_jicon(const char *key, SDL_Texture *t, SDL_Color avg, void *ud) {
  (void)avg; (void)ud;
  int i = key[0] == 'j' ? key[1] - '0' : -1;   // chiave corta "jN": il caricatore tiene 47 caratteri
  if (i >= 0 && i < 4 && jicon[i].st == 1) { jicon[i].t = t; jicon[i].st = t ? 2 : 3; return; }
  if (t) SDL_DestroyTexture(t);
}
static SDL_Texture *job_icon(const char *path) {
  if (!path || !path[0]) return NULL;
  for (int i = 0; i < 4; i++) if (jicon[i].st && !strcmp(jicon[i].key, path)) return jicon[i].t;
  static int next; int slot = next++ % 4; JobIcon *j = &jicon[slot];
  if (j->st == 1) return NULL;
  if (j->t) SDL_DestroyTexture(j->t);
  memset(j, 0, sizeof *j); snprintf(j->key, sizeof j->key, "%s", path); j->st = 1;
  int url = !strncmp(path, "http", 4);
  char k[4] = { 'j', (char)('0' + slot), 0, 0 };
  load_req(url ? LOAD_URL : LOAD_ICON, k, path, 256, 256, 30, RGB(30, 60, 140), on_jicon, NULL);
  return NULL;
}

// una tessera di lavoro: icona velata, barra in basso, stato sotto
static void job_tile(int tx, int ty, int s, int a, const char *name, const char *icon, int fallback_ic, float prog, const char *line, int err, int done) {
  SDL_Texture *t = job_icon(icon);
  shadow_rrect(tx, ty, s, s, 28, 18, a * 50 / 100);
  if (t) { SDL_SetTextureColorMod(t, 255, 255, 255); draw_tex(t, tx, ty, s, s, done ? a : a * 55 / 100); }
  else {
    fill_rrect(tx, ty, s, s, 28, RGB(26, 34, 56), a);
    draw_icon(fallback_ic, tx + s / 2, ty + s / 2 - 16, s / 3, C_DIM, a);
    draw_text_fit(font(W_MED, 18), name, tx + s / 2, ty + s - 70, s - 24, C_TXT, a, AL_C);
  }
  if (!done) fill_rrect(tx, ty, s, s, 28, RGB(6, 8, 16), a * 35 / 100);
  // barra: indeterminata se il totale non si conosce
  int bx = tx + 14, bw = s - 28, by = ty + s - 26;
  fill_rrect(bx, by, bw, 10, 5, RGB(0, 0, 0), a * 55 / 100);
  if (err) fill_rrect(bx, by, bw, 10, 5, C_ERR, a);
  else if (prog >= 0) fill_rrect(bx, by, (int)(bw * clampf(prog, 0.02f, 1)), 10, 5, done ? C_OK : C_ACC2, a);
  else { int iw = bw / 3, ix = (int)(fmodf((float)g_time * 260, (float)(bw + iw)) - iw); int x0 = ix < 0 ? 0 : ix, x1 = ix + iw > bw ? bw : ix + iw; if (x1 > x0) fill_rrect(bx + x0, by, x1 - x0, 10, 5, C_ACC2, a); }
  // segno in alto a destra: scaricamento, fatto o errore
  int cx = tx + s - 24, cy = ty + 24;
  fill_circle(cx, cy, 18, err ? C_ERR : done ? RGB(0, 150, 90) : RGB(10, 14, 24), a);
  if (err) draw_text(font(W_BOLD, 24), "!", cx, cy - TTF_FontHeight(font(W_BOLD, 24)) / 2, C_WHITE, a, AL_C);
  else draw_icon(done ? IC_CHECK : IC_DOWNLOAD, cx, cy, 20, C_WHITE, a);
  if (!err && !done && prog >= 0) { char pc[8]; snprintf(pc, sizeof pc, "%d%%", (int)(prog * 100)); draw_text(font(W_BOLD, 26), pc, tx + s / 2, ty + s / 2 - 16, C_WHITE, a, AL_C); }
  // stato sotto la tessera (due righe per gli errori)
  draw_text_fit(font(W_MED, 19), name, tx, ty + s + 10, s + 60, C_TXT, a, AL_L);
  if (err) draw_text_wrap(font(W_REG, 17), line, tx, ty + s + 36, s + 120, 3, 21, C_ERR, a);
  else draw_text_fit(font(W_REG, 17), line, tx, ty + s + 36, s + 80, C_DIM, a, AL_L);
}

// quante tessere di lavoro ci sono adesso e quanto spazio prendono
static int job_tiles(InstallView *iv) {
  int n = install_view(iv);
  InstallView mv; n += storage_view(&mv); n += saves_view(&mv);
  return n + (upj.active ? 1 : 0);
}
static void draw_jobs(int *px, int ty, int s, int gap, int a) {
  InstallView iv; int n = install_view(&iv);
  if (n) {
    char line[200];
    if (iv.active) snprintf(line, sizeof line, "%s%s", iv.phase, iv.queued ? "" : "");
    else snprintf(line, sizeof line, "%s", iv.result);
    if (iv.active && iv.queued) { size_t l = strlen(line); snprintf(line + l, sizeof line - l, _("  ·  altri %d in coda"), iv.queued); }
    job_tile(*px, ty, s, a, iv.name, iv.icon, IC_BOX, iv.active ? iv.prog : 1, line, iv.err, !iv.active && !iv.err);
    *px += s + gap + 60;
  }
  InstallView mv;
  if (storage_view(&mv)) {
    job_tile(*px, ty, s, a, mv.name, NULL, IC_DRIVE, mv.active ? mv.prog : 1, mv.active ? mv.phase : mv.result, mv.err, !mv.active && !mv.err);
    *px += s + gap + 60;
  }
  if (saves_view(&mv)) {
    job_tile(*px, ty, s, a, mv.name, NULL, IC_CLOUD, mv.active ? mv.prog : 1, mv.active ? mv.phase : mv.result, mv.err, !mv.active && !mv.err);
    *px += s + gap + 60;
  }
  if (upj.active) {
    char line[160];
    float p = upj.total > 0 ? (float)upj.done / (float)upj.total : -1;
    snprintf(line, sizeof line, _("Dal PC  ·  %.0f / %.0f MB"), upj.done / 1048576.0, upj.total / 1048576.0);
    job_tile(*px, ty, s, a, upj.name[0] ? upj.name : _("Ricezione"), NULL, IC_CLOUD, p, line, 0, 0);
    *px += s + gap + 60;
  }
}

// ------------------------------------------------------- dettagli del gioco --
static int count_playing(const char *tid) {
  int n = 0; for (int k = 0; k < S.nfriends; k++) if (!strcmp(S.friends[k].game_id, tid)) n++; return n;
}

static void game_info(int y0, int alpha) {
  if (!nrow) return;
  AppEntry *ap = &apps[app_sel];
  TTF_Font *tf = font(W_LIGHT, 72);
  draw_text_fit(tf, ap->name, 110, y0, 1300, C_WHITE, alpha, AL_L);
  char sub[256], pl[96]; int np = count_playing(ap->tid);
  if (np) { snprintf(pl, sizeof pl, np == 1 ? _("%d amico sta giocando") : _("%d amici stanno giocando"), np); snprintf(sub, sizeof sub, "%s  \xC2\xB7  %s", ap->tid, pl); }
  else if (ap->builtin == 2) { int c = layout_folder_count(ap->tid); snprintf(sub, sizeof sub, c == 1 ? _("Cartella \xC2\xB7 %d app") : _("Cartella \xC2\xB7 %d app"), c); }
  else if (ap->builtin == 3) snprintf(sub, sizeof sub, "%s", _("Homebrew della community e La mia libreria, con voti, commenti e amici"));
  else if (ap->builtin == 4) snprintf(sub, sizeof sub, "%s", _("Naviga il web dalla console: segnalibri, cronologia e lettura comoda"));
  else if (ap->builtin == 5) snprintf(sub, sizeof sub, "%s", _("Installa i .pkg da chiavette, dischi USB e cartelle a scelta"));
  else if (ap->builtin == 7) snprintf(sub, sizeof sub, "%s", _("Scarica e installa gli aggiornamenti ufficiali dei tuoi giochi"));
  else if (ap->builtin == 6) snprintf(sub, sizeof sub, "%s", _("Omega sul telefono e sul PC: chat, party, amici, Store e caricamento dei giochi"));
  else if (ap->builtin) snprintf(sub, sizeof sub, "%s", _("Bacheca, gruppi, record e trofei di tutti gli iscritti"));
  else if (ap->hb) snprintf(sub, sizeof sub, "%s%s%s", _("Homebrew"), ap->sub[0] ? "  \xC2\xB7  " : "", ap->sub);
  else if (ap->ext) { char on[96]; snprintf(on, sizeof on, _("Su %s"), ap->drive); snprintf(sub, sizeof sub, "%s  \xC2\xB7  %s", ap->tid, on); }
  else snprintf(sub, sizeof sub, "%s  \xC2\xB7  %s", ap->tid, _("Installato"));
  int sx = 114;
  if (np) { fill_circle(sx + 8, y0 + 112, 7, C_OK, alpha); sx += 26; }
  draw_text(font(W_REG, 28), sub, sx, y0 + 96, np ? C_TXT : C_DIM, alpha, AL_L);

  sel_anim_act = approach(sel_anim_act, zone == Z_ACT ? (float)act_sel : -1.0f, 16.0f);
  int by = y0 + 168;
  float f0 = clampf(1 - fabsf(sel_anim_act - 0), 0, 1), f1 = clampf(1 - fabsf(sel_anim_act - 1), 0, 1);
  int w0 = pill(110, by, 84, ap->builtin ? _("Apri") : _("Gioca"), ap->builtin == 2 ? IC_FOLDER : SYS_TILE(ap) ? IC_ARROW_R : IC_PLAY, zone == Z_ACT && act_sel == 0, f0 > 0 ? f0 : 0.0f, alpha);
  pill(110 + w0 + 20, by, 84, "", IC_MORE, zone == Z_ACT && act_sel == 1, f1, alpha);
  if (zone == Z_ACT) focus_ring(act_sel == 0 ? 110 : 110 + w0 + 20, by, act_sel == 0 ? w0 : 84, 84, 42, 0.5f + 0.5f * sinf((float)g_time * 3.2f), (int)(alpha * (act_sel == 0 ? f0 : f1)));
}

// schede sotto il gioco: amici che giocano, chi ha giocato, notizie
#define NCARD_FIXED 2
static int ncards(void) { return NCARD_FIXED + S.ngame_news; }

static void card_frame(int x, int y, int w, int h, int focused, float fa, int alpha) {
  if (focused) shadow_rrect(x, y, w, h, 24, 24, alpha * 60 / 100);
  fill_rrect(x, y, w, h, 24, C_PANEL, pref_panel_alpha(alpha * 88 / 100));
  if (fa > 0.02f) stroke_rrect(x - 6, y - 6, w + 12, h + 12, 30, 4, C_WHITE, (int)(alpha * fa));
}

static void card_people(int x, int y, int w, int h, const char *title, UserRef *u, int n, int now, int focused, float fa, int alpha) {
  card_frame(x, y, w, h, focused, fa, alpha);
  draw_icon(now ? IC_GAMEPAD : IC_CLOCK, x + 44, y + 44, 30, now ? C_OK : C_DIM, alpha);
  draw_text(font(W_MED, 26), title, x + 74, y + 28, C_TXT, alpha, AL_L);
  if (!n) {
    const char *empty = now ? _("Nessun amico sta giocando ora") : _("Nessun amico ci ha ancora giocato");
    draw_text_wrap(font(W_REG, 24), empty, x + 32, y + 100, w - 64, 2, 32, C_DIM, alpha);
    if (now && S.game_players > 0) {
      char m[160]; snprintf(m, sizeof m, S.game_players == 1 ? _("%d giocatore su Omega in questo momento") : _("%d giocatori su Omega in questo momento"), S.game_players);
      draw_text_fit(font(W_REG, 22), m, x + 32, y + h - 56, w - 64, C_FAINT, alpha, AL_L);
    }
    return;
  }
  int yy = y + 90;
  for (int i = 0; i < n && i < 3; i++) {
    draw_avatar(u[i].oid, u[i].avatar, x + 58, yy + 26, 52, alpha);
    if (now) { fill_circle(x + 78, yy + 46, 9, C_PANEL, alpha); fill_circle(x + 78, yy + 46, 6, C_OK, alpha); }
    draw_text_fit(font(W_MED, 25), u[i].oid, x + 100, yy + 2, w - 130, C_TXT, alpha, AL_L);
    char t[64]; if (now) play_time(u[i].when, t, sizeof t); else rel_time(u[i].when, t, sizeof t);
    draw_text(font(W_REG, 21), t, x + 100, yy + 32, C_DIM, alpha, AL_L);
    yy += 66;
  }
  if (n > 3) { char m[24]; snprintf(m, sizeof m, "+%d", n - 3); draw_text(font(W_MED, 22), m, x + w - 30, y + 32, C_DIM, alpha, AL_R); }
}

static void card_news(int x, int y, int w, int h, const News *n, int focused, float fa, int alpha) {
  card_frame(x, y, w, h, focused, fa, alpha);
  news_art(n, x, y, w, 128, -24, alpha);
  fill_rrect(x + 22, y + 22, text_w(font(W_BOLD, 18), n->tag) + 26, 32, 16, RGB(0, 0, 0), alpha * 45 / 100);
  draw_text(font(W_BOLD, 18), n->tag, x + 35, y + 27, C_WHITE, alpha, AL_L);
  draw_text_wrap(font(W_MED, 26), n->title, x + 26, y + 142, w - 52, 2, 32, C_TXT, alpha);
  char t[64]; rel_time(n->when, t, sizeof t);
  draw_text(font(W_REG, 20), t, x + 26, y + h - 40, C_FAINT, alpha, AL_L);
}

static void game_cards(int y0, int alpha) {
  sel_anim_card = approach(sel_anim_card, zone == Z_CARDS ? (float)card_sel : -5.0f, 16.0f);
  draw_text(font(W_MED, 30), _("Amici e notizie"), 110, y0 - 56, C_TXT, alpha, AL_L);
  if (S.game_loading && !S.ngame_now && !S.ngame_played) draw_spinner(380, y0 - 38, 10, alpha);
  int x = 110, h = 330;
  int widths[2] = { 470, 420 };
  float scroll_x = 0;
  // scorre quando la scheda a fuoco uscirebbe dallo schermo
  { int cx = 110; for (int i = 0; i < card_sel && zone == Z_CARDS; i++) cx += (i < 2 ? widths[i] : 440) + 26;
    if (cx > 1100) scroll_x = (float)(cx - 1100); }
  static float sx = 0; sx = approach(sx, scroll_x, 12.0f);
  x -= (int)sx;
  for (int i = 0; i < ncards(); i++) {
    int w = i < 2 ? widths[i] : 440;
    float fa = clampf(1 - fabsf(sel_anim_card - i), 0, 1);
    int foc = zone == Z_CARDS && card_sel == i;
    if (x < SCREEN_W && x + w > 0) {
      if (i == 0) card_people(x, y0, w, h, _("Stanno giocando ora"), S.game_now, S.ngame_now, 1, foc, fa, alpha);
      else if (i == 1) card_people(x, y0, w, h, _("Hanno giocato di recente"), S.game_played, S.ngame_played, 0, foc, fa, alpha);
      else card_news(x, y0, w, h, &S.game_news[i - 2], foc, fa, alpha);
    }
    x += w + 26;
  }
}

// attività degli amici
static void act_line(const Activity *a, char *out, size_t n) {
  if (!strcmp(a->type, "game_start")) snprintf(out, n, _("ha iniziato a giocare a %s"), a->game_name[0] ? a->game_name : a->game_id);
  else if (!strcmp(a->type, "online")) snprintf(out, n, "%s", _("è online"));
  else snprintf(out, n, "%s", a->detail[0] ? a->detail : a->type);
}

static void feed_row(int y0, int alpha, int *sel, float *anim, int focused_zone) {
  *anim = approach(*anim, focused_zone ? (float)*sel : -5.0f, 16.0f);
  draw_text(font(W_MED, 30), _("Cosa fanno i tuoi amici"), 110, y0 - 56, C_TXT, alpha, AL_L);
  int n = 0; const Activity *list[MAX_ACT];
  for (int i = 0; i < S.nact; i++) if (strcasecmp(S.act[i].oid, S.me)) list[n++] = &S.act[i];
  if (!n) {
    fill_rrect(110, y0, 900, 150, 24, C_PANEL, alpha * 80 / 100);
    draw_icon(IC_FRIENDS, 190, y0 + 75, 54, C_DIM, alpha);
    draw_text(font(W_MED, 28), _("Nessuna attività recente"), 260, y0 + 40, C_TXT, alpha, AL_L);
    draw_text(font(W_REG, 23), _("Aggiungi amici per vedere cosa giocano."), 260, y0 + 82, C_DIM, alpha, AL_L);
    return;
  }
  if (*sel >= n) *sel = n - 1;
  int w = 400, h = 180;
  static float sx[2]; float *sxp = &sx[sel == &feed_sel ? 0 : 1];
  float target = *sel > 3 && focused_zone ? (float)((*sel - 3) * (w + 24)) : 0;
  *sxp = approach(*sxp, target, 12.0f);
  int x = 110 - (int)*sxp;
  for (int i = 0; i < n; i++, x += w + 24) {
    if (x > SCREEN_W || x + w < 0) continue;
    float fa = clampf(1 - fabsf(*anim - i), 0, 1);
    card_frame(x, y0, w, h, focused_zone && *sel == i, fa, alpha);
    draw_avatar(list[i]->oid, list[i]->avatar, x + 62, y0 + 62, 72, alpha);
    draw_text_fit(font(W_MED, 26), list[i]->oid, x + 114, y0 + 32, w - 140, C_TXT, alpha, AL_L);
    char t[64]; rel_time(list[i]->when, t, sizeof t);
    draw_text(font(W_REG, 21), t, x + 114, y0 + 66, C_FAINT, alpha, AL_L);
    char l[200]; act_line(list[i], l, sizeof l);
    draw_text_wrap(font(W_REG, 24), l, x + 28, y0 + 112, w - 56, 2, 30, C_DIM, alpha);
  }
}

static const Activity *feed_item(int idx) {
  int n = 0;
  for (int i = 0; i < S.nact; i++) if (strcasecmp(S.act[i].oid, S.me)) { if (n == idx) return &S.act[i]; n++; }
  return NULL;
}
static int feed_count(void) { int n = 0; for (int i = 0; i < S.nact; i++) n += strcasecmp(S.act[i].oid, S.me) != 0; return n; }

// ------------------------------------------------------------------ Esplora --
static float ex_anim_news, ex_anim_fr, ex_anim_act, ex_scroll;

static void explore_draw(int alpha) {
  float target = ex_zone >= 3 ? 520.0f : ex_zone == 2 ? 200.0f : 0;
  ex_scroll = approach(ex_scroll, zone == Z_TOP ? 0 : target, 9.0f);
  int y = 150 - (int)ex_scroll;
  // notizie: la prima grande, le altre in fila
  ex_anim_news = approach(ex_anim_news, zone != Z_TOP && ex_zone == 1 ? (float)ex_news_sel : -5.0f, 16.0f);
  draw_text(font(W_MED, 30), _("Notizie dal mondo dei videogiochi"), 110, y, C_TXT, alpha, AL_L);
  y += 60;
  if (!S.nnews) { draw_spinner(140, y + 40, 14, alpha); }
  static float nsx; float nt = 0;
  { int cx = 0; for (int i = 0; i < ex_news_sel; i++) cx += (i == 0 ? 980 : 420) + 26; if (cx > 700) nt = (float)(cx - 700); }
  nsx = approach(nsx, ex_zone == 1 ? nt : 0, 12.0f);
  int x = 110 - (int)nsx;
  for (int i = 0; i < S.nnews; i++) {
    int w = i == 0 ? 980 : 420, h = 400;
    float fa = clampf(1 - fabsf(ex_anim_news - i), 0, 1);
    int foc = zone != Z_TOP && ex_zone == 1 && ex_news_sel == i;
    if (x < SCREEN_W && x + w > 0) {
      card_frame(x, y, w, h, foc, fa, alpha);
      int ah = i == 0 ? 250 : 180;
      news_art(&S.news[i], x, y, w, ah, -24, alpha);
      fill_rrect(x + 24, y + 24, text_w(font(W_BOLD, 18), S.news[i].tag) + 26, 32, 16, C_BLACK, alpha * 45 / 100);
      draw_text(font(W_BOLD, 18), S.news[i].tag, x + 37, y + 29, C_WHITE, alpha, AL_L);
      if (i == 0) {
        draw_text_fit(font(W_LIGHT, 44), S.news[i].title, x + 30, y + ah - 4, w - 60, C_WHITE, alpha, AL_L);
        draw_text_wrap(font(W_REG, 24), S.news[i].body, x + 30, y + ah + 56, w - 60, 2, 30, C_DIM, alpha);
      } else {
        draw_text_wrap(font(W_MED, 27), S.news[i].title, x + 26, y + ah + 6, w - 52, 2, 34, C_TXT, alpha);
        draw_text_wrap(font(W_REG, 22), S.news[i].body, x + 26, y + ah + 84, w - 52, 2, 28, C_DIM, alpha);
      }
      char t[64]; rel_time(S.news[i].when, t, sizeof t);
      draw_text(font(W_REG, 20), t, x + w - 28, y + h - 40, C_FAINT, alpha, AL_R);
    }
    x += w + 26;
  }
  y += 470;
  // amici online
  ex_anim_fr = approach(ex_anim_fr, zone != Z_TOP && ex_zone == 2 ? (float)ex_fr_sel : -5.0f, 16.0f);
  char hdr[128]; snprintf(hdr, sizeof hdr, _("Amici  \xC2\xB7  %d online"), friends_online_count());
  draw_text(font(W_MED, 30), hdr, 110, y, C_TXT, alpha, AL_L);
  y += 64;
  int nf = 0;
  for (int i = 0; i < S.nfriends; i++) {
    const Friend *f = &S.friends[i];
    int cx = 180 + nf * 190;
    float fa = clampf(1 - fabsf(ex_anim_fr - nf), 0, 1);
    if (fa > 0.02f) ring(cx, y + 70, 70, 4, C_WHITE, (int)(alpha * fa));
    draw_avatar(f->oid, f->avatar, cx, y + 70, 120, friend_online(f) ? alpha : alpha * 55 / 100);
    if (friend_online(f)) { fill_circle(cx + 42, y + 112, 13, RGB(10, 14, 24), alpha); fill_circle(cx + 42, y + 112, 9, f->game_id[0] ? C_ACC2 : C_OK, alpha); }
    draw_text_fit(font(W_MED, 24), f->oid, cx, y + 150, 170, friend_online(f) ? C_TXT : C_DIM, alpha, AL_C);
    draw_text_fit(font(W_REG, 20), f->game_id[0] ? f->game_name : friend_online(f) ? _("Online") : _("Offline"), cx, y + 182, 170, C_FAINT, alpha, AL_C);
    nf++;
    if (cx > SCREEN_W) break;
  }
  if (!S.nfriends) {
    draw_text(font(W_REG, 26), _("Nessun amico ancora: apri la Game Base per aggiungerne."), 110, y + 30, C_DIM, alpha, AL_L);
  }
  y += 290;
  feed_row(y, alpha, &ex_act_sel, &ex_anim_act, zone != Z_TOP && ex_zone == 3);
}

static float row_scroll_target_diff(void) {
  float before = 0; for (int i = 0; i < app_sel; i++) before += tile_size[i] + 22;
  return before - row_scroll;
}

// ------------------------------------------------------------------ disegno --
// disco collegato o tolto: si rifà la fila tenendo il gioco scelto, e lo si dice
static void ext_rescan(void) {
  static int seen;   // il primo giro (all'avvio) non va annunciato
  char keep[16]; snprintf(keep, sizeof keep, "%s", nrow ? apps[app_sel].tid : "");
  int ext0 = 0, n0 = napps; for (int i = 0; i < napps; i++) ext0 += apps[i].ext;
  scan_apps();
  int ext1 = 0; for (int i = 0; i < napps; i++) { ext1 += apps[i].ext; tile_size[i] = 150; }
  int in0 = n0 - ext0, in1 = napps - ext1;   // titoli installati, homebrew e payload
  if (seen && in1 > in0) { char m[160]; snprintf(m, sizeof m, in1 - in0 == 1 ? _("Nuovo titolo in home") : _("%d nuovi titoli in home"), in1 - in0); set_msg(m, 0); }
  app_sel = 0; for (int i = 0; i < nrow; i++) if (!strcmp(apps[i].tid, keep)) app_sel = i;
  request_icons();
  if (seen && ext1 > ext0) { char m[160]; snprintf(m, sizeof m, ext1 - ext0 == 1 ? _("Disco collegato: 1 gioco in più in home") : _("Disco collegato: %d giochi in più in home"), ext1 - ext0); set_msg(m, 0); }
  else if (seen && ext1 < ext0) set_msg(_("Disco scollegato: i suoi giochi sono spariti dalla home"), 0);
  seen = 1;
}

void home_update(void) {
  hbl_poll();
  jobs_poll();
  if (drives_changed() && launching < 0) ext_rescan();
  for (int i = 0; i < napps; i++) if (apps[i].tex_state == 0) { request_icons(); break; }
  // mentre la fila scorre o la pagina si muove, niente effetti costosi
  g_busy_motion = (fabsf(row_scroll_target_diff()) > 2 || fabsf(page_scroll - page_scroll_t) > 2) ? 1 : 0;
  if (nrow && tab == T_GAMES && launching < 0) {
    if (focus_loaded != app_sel && SDL_GetTicks() - focus_at > FOCUS_MS) {
      focus_loaded = app_sel;
      bg_set_game(apps[app_sel].tid, apps[app_sel].art, apps[app_sel].avg);
      if (apps[app_sel].builtin) { S.ngame_now = S.ngame_played = S.ngame_news = S.game_players = 0; S.game_tid[0] = 0; }   // niente scheda del gioco di prima
      else if (!apps[app_sel].hb) social_load_game(apps[app_sel].tid);
    }
  }
  if (tab == T_EXPLORE) bg_set_default();
  else if (focus_loaded == app_sel && nrow && SDL_GetTicks() - focus_at > FOCUS_MS) bg_set_game(apps[app_sel].tid, apps[app_sel].art, apps[app_sel].avg);
  if (SDL_GetTicks() - last_feed > 30000) {
    last_feed = SDL_GetTicks();
    social_load_activity(); social_load_news();
    if (nrow && tab == T_GAMES && !apps[app_sel].hb && !apps[app_sel].builtin) { S.game_loading = 0; social_load_game(apps[app_sel].tid); }
  }
  hb_poll();
  if (launching >= 0) {
    launch_t += g_dt;
    if (launch_t > 0.75f && !launch_done_at) { do_launch(launching); launch_done_at = SDL_GetTicks(); }
    if (launch_done_at && SDL_GetTicks() - launch_done_at > 1500) launching = -1;
  }
  tab_anim = approach(tab_anim, 1, 8.0f);
}

enum { GM_PLAY, GM_ORDER, GM_PARTY, GM_REFRESH, GM_FOLDER, GM_HIDE, GM_INVITE, GM_MOVE, GM_SAVES, GM_DELETE, GM_N };
static void game_more_menu(int idx, void *ud);
static void sys_more_menu(int idx, void *ud);
// Opzioni della tessera i: giochi e app, cartelle, app di Omega.
static void more_menu(int i) {
  if (apps[i].builtin == 2) { layout_folder_menu(apps[i].tid); return; }
  if (SYS_TILE(&apps[i])) {
    const char *it[3] = { _("Apri"), _("Cambia posizione"), _("Nascondi dalla home") };
    menu_open(apps[i].name, it, 3, sys_more_menu, NULL);
    return;
  }
  const char *it[GM_N] = { _("Gioca"), _("Cambia posizione"), _("Proponi al party"), _("Aggiorna informazioni"), _("Sposta in una cartella"), _("Nascondi dalla home"), _("Invita un amico a giocare"), _("Sposta su un altro disco"), _("Salvataggi online"), _("Elimina dalla console") };
  menu_open(apps[i].name, it, GM_N, game_more_menu, NULL);
}

// ------------------------------------------- per le altre modalità del menu --
// homestyles.c disegna la home in altri modi (PS4, XMB, griglia...) ma usa la
// stessa selezione: sfondo, scheda del gioco e avvio restano quelli di qui.
int  home_selected(void) { return app_sel; }
void home_set_sel(int i) { if (i >= 0 && i < nrow && i != app_sel) { app_sel = i; focus_at = SDL_GetTicks(); } }
int  home_launching(void) { return launching; }
void home_more(int i) {
  if (i < 0 || i >= nrow) return;
  app_sel = i;
  more_menu(i);
}
void home_tile(int i, int x, int y, int s, int a) {   // una tessera come nella fila (icona, cartella, app di Omega)
  if (i < 0 || i >= napps) return;
  AppEntry *ap = &apps[i];
  if (ap->builtin == 2) {
    fill_rrect(x, y, s, s, s / 6, mix(C_PANEL, C_WHITE, 0.08f), a);
    int pad = s / 12, cell = (s - 3 * pad) / 2;
    for (int k = 0; k < 4; k++) { int j = layout_folder_app(ap->tid, k); if (j < 0) break; int cx = x + pad + (k % 2) * (cell + pad), cy = y + pad + (k / 2) * (cell + pad); if (apps[j].tex) draw_tex(apps[j].tex, cx, cy, cell, cell, a); else fill_rrect(cx, cy, cell, cell, 8, RGB(40, 48, 70), a); }
  } else if (ap->builtin) sys_tile_draw(ap, x, y, s, a);
  else if (ap->tex) { SDL_SetTextureColorMod(ap->tex, 255, 255, 255); draw_tex(ap->tex, x, y, s, s, a); }
  else { fill_rrect(x, y, s, s, s / 6, RGB(30, 38, 60), a); draw_icon(IC_GAMEPAD, x + s / 2, y + s / 2, s / 3, C_DIM, a); }
  if (ap->ext && s >= 90) { fill_circle(x + s - s / 7, y + s - s / 7, s / 9, RGB(10, 14, 24), a * 90 / 100); draw_icon(IC_DRIVE, x + s - s / 7, y + s - s / 7, s / 7, C_ACC2, a); }
}
void home_top(int which) { top_sel = which; top_activate(); }
// tessere di lavoro (installazioni, ricezioni) per le altre modalità
int home_jobs(int x, int y, int s, int a) { InstallView iv; if (!job_tiles(&iv)) return 0; int jx = x; draw_jobs(&jx, y, s, 22, a); return jx - x; }

static void launch_overlay(void);
void home_draw(void) {
  if (g_prefs.home_style && tab == T_GAMES) {
    particles_draw(90);
    hs_draw();
    launch_overlay();
    return;
  }
  page_scroll_t = tab == T_GAMES ? (zone == Z_CARDS ? 470.0f : zone == Z_FEED ? 860.0f : 0.0f) : 0.0f;
  page_scroll = approach(page_scroll, page_scroll_t, 9.0f);
  int ps = (int)page_scroll;
  // velo più scuro quando la pagina scorre, per leggere le schede
  if (ps > 0) fill_rect(0, 0, SCREEN_W, SCREEN_H, RGB(4, 8, 18), (int)(150 * clampf(page_scroll / 470.0f, 0, 1)));
  particles_draw(90);   // quante e quali: Personalizza › Particelle
  SDL_Rect clip = { 0, 118, SCREEN_W, SCREEN_H - 118 };
  if (ps > 20 || tab == T_EXPLORE) SDL_RenderSetClipRect(R, &clip);
  if (tab == T_GAMES) {
    game_row(130 - ps, 255);
    game_info(440 - ps, 255);
    if (g_prefs.cards) {   // Personalizza › Sotto il gioco scelto
      game_cards(830 - ps, 255);
      feed_row(1260 - ps, 255, &feed_sel, &sel_anim_feed, zone == Z_FEED);
    }
  } else {
    explore_draw(255);
  }
  SDL_RenderSetClipRect(R, NULL);
  // sfumatura sotto la barra quando la pagina scorre
  if (ps > 20 || tab == T_EXPLORE) grad_v(0, 118, SCREEN_W, 70, RGB(4, 8, 18), (int)(170 * clampf(page_scroll / 200.0f + (tab == T_EXPLORE), 0, 1)), RGB(4, 8, 18), 0);
  top_bar(255);
  int ic[4]; const char *lb[4]; int n = 0;
  ic[n] = IC_BTN_X; lb[n++] = tab == T_GAMES && zone <= Z_ACT ? (nrow && apps[app_sel].builtin ? _("Apri") : _("Gioca")) : _("Seleziona");
  ic[n] = IC_BTN_TRI; lb[n++] = _("Game Base");
  ic[n] = IC_BTN_SQ; lb[n++] = _("Notifiche");
  ic[n] = IC_BTN_OPT; lb[n++] = _("Centro di controllo");
  // i comandi si vedono solo nei primi secondi, poi la home resta pulita
  Uint32 since = SDL_GetTicks() - entered_at;
  int ha = since < 9000 ? 255 : since < 10000 ? (int)(255 * (10000 - since) / 1000) : 0;
  if (g_prefs.hints == 0) ha = 0; else if (g_prefs.hints == 2) ha = 255;   // Personalizza › Barra dei comandi
  if (layout_moving() >= 0) {    // in Sposta i comandi si vedono sempre
    n = 0; ha = 255;
    ic[n] = -1; lb[n++] = _("\xE2\x97\x80 \xE2\x96\xB6  Sposta");
    ic[n] = -1; lb[n++] = _("L2 / R2  In testa / In fondo");
    ic[n] = IC_BTN_X; lb[n++] = _("Conferma");
    ic[n] = IC_BTN_O; lb[n++] = _("Annulla");
  }
  if (ov_depth() == 0 && ha > 0) {
    grad_v(0, SCREEN_H - 120, SCREEN_W, 120, RGB(4, 8, 18), 0, RGB(4, 8, 18), ha * 80 / 100);
    hints(ic, lb, n, ha);
  }

  launch_overlay();
}

// avvio gioco: il riquadro "vola" verso il centro e lo schermo si spegne
static void launch_overlay(void) {
  if (launching >= 0) {
    float t = clampf(launch_t / 0.75f, 0, 1), e = ease_in_out(t);
    AppEntry *ap = &apps[launching];
    int s0 = 236, s1 = 420;
    int s = s0 + (int)((s1 - s0) * e);
    int x = (int)(110 + (SCREEN_W / 2 - s / 2 - 110) * e), y = (int)(130 + (SCREEN_H / 2 - s / 2 - 60 - 130) * e);
    fill_rect(0, 0, SCREEN_W, SCREEN_H, C_BLACK, (int)(235 * e));
    glow(x + s / 2, y + s / 2, s, ap->avg, (int)(160 * e));
    if (ap->tex) draw_tex(ap->tex, x, y, s, s, 255); else fill_rrect(x, y, s, s, 30, RGB(30, 38, 60), 255);
    draw_text(font(W_LIGHT, 40), ap->name, SCREEN_W / 2, y + s + 40, C_WHITE, (int)(255 * e), AL_C);
    if (launch_done_at) draw_spinner(SCREEN_W / 2, y + s + 130, 16, 220);
  }
}

// -------------------------------------------------------------------- input --
// I giochi si disinstallano, homebrew e payload si cancellano dalla loro cartella.
static void delete_confirmed(int idx, void *ud) {
  (void)ud;
  if (idx != 0 || !nrow) return;
  AppEntry *ap = &apps[app_sel];
  char name[96]; snprintf(name, sizeof name, "%s", ap->name);
  int rc = ap->hb ? hb_remove(ap->dir, OMEGA_HB_ROOT) : ap->pld ? payload_remove(ap->dir) : store_uninstall(ap->tid);
  omega_log("elimina %s (%s): rc=0x%x", name, ap->hb ? ap->dir : ap->tid, (unsigned)rc);
  if (rc != 0) { char m[160]; snprintf(m, sizeof m, _("Eliminazione non riuscita (0x%x)"), (unsigned)rc); set_msg(m, 1); return; }
  scan_apps();
  for (int i = 0; i < nrow; i++) tile_size[i] = 150;
  if (app_sel >= nrow) app_sel = nrow ? nrow - 1 : 0;
  focus_at = SDL_GetTicks(); focus_loaded = -1;
  if (!nrow) bg_set_default();
  char m[256]; snprintf(m, sizeof m, _("%s eliminato dalla console"), name);
  set_msg(m, 0);
}

// Opzioni di un gioco in home. Le voci: Gioca, Proponi al party, Aggiorna
// informazioni, Sposta in una cartella, Nascondi dalla home, Invita un amico,
// Elimina dalla console.
static void game_more_menu(int idx, void *ud) {
  (void)ud;
  if (!nrow) return;
  AppEntry *ap = &apps[app_sel];
  switch (idx) {
    case GM_PLAY: launch_app(app_sel); break;
    case GM_ORDER: home_move_start(app_sel); break;
    case GM_PARTY: {
      if (!S.party.active) { set_msg(_("Non sei in un party: creane uno dalla Game Base"), 1); return; }
      char m[200]; snprintf(m, sizeof m, _("Giochiamo a %s?"), ap->name);
      social_party_send(m); set_msg(_("Invito a giocare inviato al party"), 0);
      break;
    }
    case GM_REFRESH: S.game_loading = 0; social_load_game(ap->tid); set_msg(_("Informazioni aggiornate"), 0); break;
    case GM_FOLDER: layout_move_menu(ap->tid); break;
    case GM_HIDE: {
      char m[200]; snprintf(m, sizeof m, _("%s nascosta: la rimetti in Impostazioni \xE2\x80\xBA App nascoste"), ap->name);
      layout_hide(ap->tid, 1); set_msg(m, 0);
      break;
    }
    case GM_INVITE: invite_to_game_menu(ap->tid, ap->name); break;
    case GM_MOVE: if (ap->hb || ap->pld) set_msg(_("Si spostano solo i giochi: homebrew e payload stanno nelle cartelle del caricatore"), 1); else storage_move_app(app_sel); break;
    case GM_SAVES: if (ap->hb || ap->pld) set_msg(_("Gli homebrew non hanno salvataggi di sistema"), 1); else saves_open(ap->tid); break;
    case GM_DELETE: {
      static char q[512];
      snprintf(q, sizeof q, ap->hb ? _("Eliminare l'homebrew %s dalla console?") : _("Eliminare %s dalla console? Il gioco verrà disinstallato."), ap->name);
      confirm_open(q, _("Elimina"), delete_confirmed, NULL);
      break;
    }
  }
}

static void sys_more_menu(int idx, void *ud) {
  (void)ud;
  if (!nrow || !SYS_TILE(&apps[app_sel])) return;
  AppEntry *ap = &apps[app_sel];
  if (idx == 0) launch_app(app_sel);
  else if (idx == 1) home_move_start(app_sel);
  else if (idx == 2) {
    char m[200]; snprintf(m, sizeof m, _("%s nascosta: la rimetti in Impostazioni \xE2\x80\xBA App nascoste"), ap->name);
    layout_hide(ap->tid, 1); set_msg(m, 0);
  }
}

// ---------------------------------------------------------------- Sposta --
// La tessera scelta si solleva e segue le frecce; le altre le fanno posto
// scorrendo (AppEntry.vx in game_row). tile_size va dietro alle tessere,
// altrimenti quella scelta tornerebbe piccola a ogni passo.
void home_move_start(int i) {
  if (i < 0 || i >= nrow) return;
  app_sel = i; zone = Z_ROW; tab = T_GAMES;
  layout_move_begin(i);
  move_lift = 0;
  sfx_play(SFX_OPEN);
}
static void home_move_step(int d) {
  int from = layout_moving(); if (from < 0) return;
  int to = layout_move_step(d);
  if (to == from) return;
  float ts = tile_size[from];
  if (to > from) memmove(&tile_size[from], &tile_size[from + 1], sizeof(float) * (size_t)(to - from));
  else memmove(&tile_size[to + 1], &tile_size[to], sizeof(float) * (size_t)(from - to));
  tile_size[to] = ts;
  app_sel = to; focus_at = SDL_GetTicks();
  sfx_play(SFX_MOVE);
}
static void home_move_finish(int keep) {
  char tid[16]; snprintf(tid, sizeof tid, "%s", apps[app_sel].tid);
  layout_move_end(keep);
  for (int i = 0; i < nrow; i++) if (!strcmp(apps[i].tid, tid)) app_sel = i;
  for (int i = 0; i < nrow; i++) tile_size[i] = i == app_sel ? tile_size[i] : 150;
  sfx_play(keep ? SFX_SELECT : SFX_BACK);
}
// 1 se il tasto è della modalità Sposta (stile Omega; gli altri stili passano da hs_move_input)
static int move_input(int b) {
  if (layout_moving() < 0) return 0;
  if (b == B_X) home_move_finish(1);
  else if (b == B_O) home_move_finish(0);
  else if (g_prefs.home_style) { int d = hs_move_input(b); if (d) home_move_step(d); }
  else if (b == B_LEFT) home_move_step(-1);
  else if (b == B_RIGHT) home_move_step(1);
  else if (b == B_L2) home_move_step(-1000);    // in testa
  else if (b == B_R2) home_move_step(1000);     // in fondo
  return 1;
}

// Dopo un cambio di cartelle o di app nascoste: si rifà la fila senza
// rileggere i dischi, tenendo a fuoco keep_tid (o la tessera di prima, o la
// cartella in cui è finita).
void home_relayout(const char *keep_tid) {
  char keep[16]; snprintf(keep, sizeof keep, "%s", keep_tid ? keep_tid : nrow && app_sel < nrow ? apps[app_sel].tid : "");
  // via le tessere delle cartelle, poi Community in testa e le app nell'ordine scelto
  int n = 0;
  for (int i = 0; i < napps; i++) if (apps[i].builtin != 2) apps[n++] = apps[i];
  napps = n;
  for (int i = 0; i < napps; i++) for (int k = i + 1; k < napps; k++) {
    int si = SYS_TILE(&apps[i]), sk = SYS_TILE(&apps[k]);
    int before = sk ? (!si || sys_tile_order(apps[k].builtin) < sys_tile_order(apps[i].builtin)) : (!si && app_cmp(&apps[k], &apps[i]) < 0);
    if (before) { AppEntry t = apps[i]; apps[i] = apps[k]; apps[k] = t; }
  }
  layout_apply();
  const char *f = keep[0] ? layout_folder_of(keep) : NULL;
  int sel = -1;
  for (int i = 0; i < nrow; i++) if (!strcmp(apps[i].tid, f ? f : keep)) sel = i;
  if (sel < 0) sel = app_sel < nrow ? app_sel : nrow - 1;
  app_sel = sel < 0 ? 0 : sel;
  for (int i = 0; i < nrow; i++) tile_size[i] = 150;
  focus_at = SDL_GetTicks(); focus_loaded = -1;
  if (!nrow) bg_set_default();
}

static UserRef pick_list[16]; static int npick;
static void pick_friend(int idx, void *ud) { (void)ud; if (idx >= 0 && idx < npick) profile_open(pick_list[idx].oid); }

static void open_people(UserRef *u, int n, const char *title) {
  if (!n) { set_msg(_("Nessun amico da mostrare"), 0); return; }
  static const char *items[16]; static char names[16][40];
  npick = n > 16 ? 16 : n;
  for (int i = 0; i < npick; i++) { pick_list[i] = u[i]; snprintf(names[i], sizeof names[i], "%s", u[i].oid); items[i] = names[i]; }
  menu_open(title, items, npick, pick_friend, NULL);
}

void home_input(int b) {
  if (launching >= 0) return;
  if (move_input(b)) return;
  if (b == B_TRI) { gb_open(0); return; }
  if (b == B_OPT) { ov_push(OV_CC); return; }
  if (b == B_SQ) { social_load_notifications(); ov_push(OV_NOTIF); return; }
  if (g_prefs.home_style && tab == T_GAMES && hs_input(b)) return;
  if (b == B_L1 || b == B_R1) { tab = b == B_L1 ? T_GAMES : T_EXPLORE; tab_anim = 0; if (tab == T_EXPLORE) { social_load_news(); social_load_activity(); } return; }

  if (zone == Z_TOP) {
    if (b == B_LEFT && top_sel > 0) top_sel--;
    else if (b == B_RIGHT && top_sel < TOP_N - 1) top_sel++;
    else if (b == B_DOWN) { zone = Z_ROW; if (tab == T_EXPLORE) ex_zone = 1; }
    else if (b == B_X) top_activate();
    else if (b == B_O) { zone = Z_ROW; }
    return;
  }

  if (tab == T_EXPLORE) {
    if (b == B_UP) { if (ex_zone == 1) { zone = Z_TOP; top_sel = 1; } else ex_zone--; }
    else if (b == B_DOWN && ex_zone < 3) ex_zone++;
    else if (b == B_O) { zone = Z_TOP; top_sel = 1; }
    else if (ex_zone == 1) {
      if (b == B_LEFT && ex_news_sel > 0) ex_news_sel--;
      else if (b == B_RIGHT && ex_news_sel < S.nnews - 1) ex_news_sel++;
      else if (b == B_X && S.nnews) news_open(&S.news[ex_news_sel]);
    } else if (ex_zone == 2) {
      if (b == B_LEFT && ex_fr_sel > 0) ex_fr_sel--;
      else if (b == B_RIGHT && ex_fr_sel < S.nfriends - 1) ex_fr_sel++;
      else if (b == B_X && S.nfriends) profile_open(S.friends[ex_fr_sel].oid);
    } else {
      int n = feed_count();
      if (b == B_LEFT && ex_act_sel > 0) ex_act_sel--;
      else if (b == B_RIGHT && ex_act_sel < n - 1) ex_act_sel++;
      else if (b == B_X) { const Activity *a = feed_item(ex_act_sel); if (a) profile_open(a->oid); }
    }
    return;
  }

  switch (zone) {
    case Z_ROW:
      if (b == B_LEFT && app_sel > 0) { app_sel--; focus_at = SDL_GetTicks(); }
      else if (b == B_RIGHT && app_sel < nrow - 1) { app_sel++; focus_at = SDL_GetTicks(); }
      else if (b == B_UP) { zone = Z_TOP; top_sel = 0; }
      else if (b == B_DOWN) { if (nrow) { zone = Z_ACT; act_sel = 0; } else if (g_prefs.cards) zone = Z_FEED; }
      else if (b == B_X && nrow) launch_app(app_sel);
      break;
    case Z_ACT:
      if (b == B_LEFT && act_sel > 0) act_sel--;
      else if (b == B_RIGHT && act_sel < 1) act_sel++;
      else if (b == B_UP || b == B_O) zone = Z_ROW;
      else if (b == B_DOWN && g_prefs.cards) { zone = Z_CARDS; card_sel = 0; }
      else if (b == B_X) {
        if (act_sel == 0) launch_app(app_sel);
        else more_menu(app_sel);
      }
      break;
    case Z_CARDS:
      if (b == B_LEFT && card_sel > 0) card_sel--;
      else if (b == B_RIGHT && card_sel < ncards() - 1) card_sel++;
      else if (b == B_UP || b == B_O) zone = Z_ACT;
      else if (b == B_DOWN) { zone = Z_FEED; feed_sel = 0; }
      else if (b == B_X) {
        if (card_sel == 0) open_people(S.game_now, S.ngame_now, _("Stanno giocando ora"));
        else if (card_sel == 1) open_people(S.game_played, S.ngame_played, _("Hanno giocato di recente"));
        else news_open(&S.game_news[card_sel - 2]);
      }
      break;
    case Z_FEED: {
      int n = feed_count();
      if (b == B_LEFT && feed_sel > 0) feed_sel--;
      else if (b == B_RIGHT && feed_sel < n - 1) feed_sel++;
      else if (b == B_UP || b == B_O) zone = nrow ? Z_CARDS : Z_ROW;
      else if (b == B_X) { const Activity *a = feed_item(feed_sel); if (a) profile_open(a->oid); }
      break;
    }
  }
}
