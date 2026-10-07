// Omega UI — jailbreak e componenti. Riconosce cosa c'è sulla console
// (OnionHEN, etaHEN 1.x/2.x, Payload Manager, autoloader BD-JB/Y2JB) e sceglie
// UN solo caricatore che avvii il servizio di Omega a ogni accensione:
//   OnionHEN  → /data/OnionHEN/payloads/omega_redirect.elf (+ .auto_start)
//               e la pagina "Omega" nel Toolbox: /data/OnionHEN/plugins/OMGA00001.elf
//   etaHEN 2  → /data/etaHEN/payloads/omega_redirect.elf (+ .auto_start)
//   etaHEN 1  → /data/etaHEN/plugins/omega_redirect.elf (+ .auto_start)
//   Payload Manager → payloads/OmegaRedirect/ + autoload.txt + AUTOLOAD_ENABLED=1
//   autoloader      → /data/ps5_autoloader/ + autoload.txt
// Le voci di Omega negli altri caricatori si tolgono, perché il servizio non
// parta due volte (in ogni caso una seconda copia si ritira da sola).
// Niente si installa senza l'OK dell'utente: si mostra prima cosa si farà.
// I file da installare li porta l'installer in /data/homebrew/OmegaUI/payloads.
#include "app.h"
#include <dirent.h>
#include <fcntl.h>
#include <math.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#define R_ OMEGA_SYSROOT
#define BUNDLE      OMEGA_HB_ROOT "/OmegaUI/payloads"
#define SETUP_FILE  OMEGA_DIR "/hen-setup.txt"       // "<proprietario> <rifiutato 0/1> <richiesta>"
// Sale quando il servizio porta qualcosa di nuovo che vale una seconda domanda
// a chi aveva detto no (2: la voce del party continua in gioco solo col servizio).
#define SETUP_ASK   2
#define DAEMON      "omega_redirect.elf"
#define PLUGIN      "OMGA00001.elf"
#define ONION       R_ "/data/OnionHEN"
#define ETA         R_ "/data/etaHEN"
#define PLDMGR      R_ "/data/pldmgr"
#define AUTOLOADER  R_ "/data/ps5_autoloader"

enum { OWN_NONE, OWN_ONION, OWN_ETA2, OWN_ETA1, OWN_PLDMGR, OWN_AUTOLOADER };
static int owner;
static int has_onion, has_eta, has_eta2, has_pldmgr, has_autoloader;

static int is_dir(const char *p) { struct stat st; return stat(p, &st) == 0 && S_ISDIR(st.st_mode); }
static int is_file(const char *p) { struct stat st; return stat(p, &st) == 0 && S_ISREG(st.st_mode); }
static long fsize(const char *p) { struct stat st; return stat(p, &st) == 0 ? (long)st.st_size : -1; }

static void detect(void) {
  has_onion = is_dir(ONION);
  has_eta = is_file(ETA "/config.ini") || is_dir(ETA "/plugins");
  has_eta2 = has_eta && (is_dir(ETA "/payloads") || is_file(ETA "/etaHEN.bin") || is_file(ETA "/daemons/util.elf"));
  has_pldmgr = is_dir(PLDMGR);
  has_autoloader = is_file(AUTOLOADER "/autoload.txt");
  owner = has_onion ? OWN_ONION : has_eta2 ? OWN_ETA2 : has_eta ? OWN_ETA1 : has_pldmgr ? OWN_PLDMGR : has_autoloader ? OWN_AUTOLOADER : OWN_NONE;
}

static const char *owner_name(int o) {
  switch (o) {
    case OWN_ONION: return "OnionHEN";
    case OWN_ETA2: return "etaHEN 2";
    case OWN_ETA1: return "etaHEN";
    case OWN_PLDMGR: return "Payload Manager";
    case OWN_AUTOLOADER: return _("autoloader");
  }
  return _("nessun caricatore");
}

const char *hen_name(void) {
  static char s[96];
  detect();
  s[0] = 0;
  if (has_onion) snprintf(s, sizeof s, "OnionHEN");
  else if (has_eta) snprintf(s, sizeof s, "%s", has_eta2 ? "etaHEN 2" : "etaHEN");
  if (has_pldmgr) { size_t l = strlen(s); snprintf(s + l, sizeof s - l, "%sPayload Manager", l ? " \xC2\xB7 " : ""); }
  if (!s[0]) snprintf(s, sizeof s, "%s", has_autoloader ? _("autoloader") : _("non rilevato"));
  return s;
}

// dove installare un payload dello Store (cartella piatta per i due HEN)
// il servizio attivo, ovunque l'abbia messo la configurazione (o un'installazione vecchia)
int hen_daemon_path(char *out, size_t n) {
  static const char *paths[] = {
    OMEGA_SYSROOT "/data/OnionHEN/payloads/omega_redirect.elf", OMEGA_SYSROOT "/data/etaHEN/payloads/omega_redirect.elf",
    OMEGA_SYSROOT "/data/etaHEN/plugins/omega_redirect.elf", OMEGA_PLD_ROOT "/OmegaRedirect/omega_redirect.elf",
    OMEGA_SYSROOT "/data/ps5_autoloader/omega_redirect.elf", NULL };
  struct stat st;
  for (int i = 0; paths[i]; i++) if (stat(paths[i], &st) == 0 && S_ISREG(st.st_mode)) { snprintf(out, n, "%s", paths[i]); return 1; }
  return 0;
}

int hen_payload_dir(char *out, size_t n) {
  detect();
  if (owner == OWN_ONION) { snprintf(out, n, ONION "/payloads"); return 1; }
  if (owner == OWN_ETA2) { snprintf(out, n, ETA "/payloads"); return 1; }
  out[0] = 0; return 0;   // 0 = Payload Manager, una cartella per payload
}

// --------------------------------------------------------------- utilità --
static int copy_file(const char *src, const char *dst) {
  int in = open(src, O_RDONLY); if (in < 0) return -1;
  char tmp[700]; snprintf(tmp, sizeof tmp, "%s.part", dst);
  int out = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0777); if (out < 0) { close(in); return -2; }
  char buf[65536]; ssize_t r; int rc = 0;
  while ((r = read(in, buf, sizeof buf)) > 0) if (write(out, buf, (size_t)r) != r) { rc = -3; break; }
  close(in); close(out);
  if (rc || rename(tmp, dst) != 0) { unlink(tmp); return rc ? rc : -4; }
  chmod(dst, 0777);
  return 0;
}
static int touch(const char *p) { FILE *f = fopen(p, "a"); if (!f) return -1; fclose(f); return 0; }
static int mkdirs(const char *p) {
  char t[600]; snprintf(t, sizeof t, "%s", p);
  for (char *c = t + 1; *c; c++) if (*c == '/') { *c = 0; mkdir(t, 0777); *c = '/'; }
  return mkdir(t, 0777) == 0 || is_dir(t) ? 0 : -1;
}
// stesso contenuto? (dimensione + sha256: i file sono piccoli)
static int same_file(const char *a, const char *b) {
  if (fsize(a) < 0 || fsize(a) != fsize(b)) return 0;
  char x[65], y[65];
  return !file_sha256(a, x) && !file_sha256(b, y) && !strcmp(x, y);
}

// riga in un file di testo (autoload.txt): presente?
static int has_line(const char *path, const char *line) {
  char *b = file_read(path, 64 * 1024, NULL); if (!b) return 0;
  int found = 0;
  for (char *l = strtok(b, "\r\n"); l; l = strtok(NULL, "\r\n")) { while (*l == ' ') l++; if (!strcmp(l, line)) { found = 1; break; } }
  free(b); return found;
}
static int add_line(const char *path, const char *line) {
  if (has_line(path, line)) return 0;
  char *b = file_read(path, 64 * 1024, NULL);
  FILE *f = fopen(path, "a"); if (!f) { free(b); return -1; }
  size_t L = b ? strlen(b) : 0;
  if (L && b[L - 1] != '\n') fputc('\n', f);
  fprintf(f, "%s\n", line); fclose(f); free(b);
  return 0;
}
// toglie la riga e l'eventuale "!ritardo" che la precede (aggiunto da noi)
static int remove_line(const char *path, const char *line) {
  char *b = file_read(path, 64 * 1024, NULL); if (!b) return 0;
  char *out = malloc(strlen(b) + 2); if (!out) { free(b); return -1; }
  out[0] = 0;
  char *lines[400]; int n = 0;
  for (char *l = strtok(b, "\r\n"); l && n < 400; l = strtok(NULL, "\r\n")) lines[n++] = l;
  int changed = 0;
  for (int i = 0; i < n; i++) {
    if (!strcmp(lines[i], line)) { changed = 1; continue; }
    if (lines[i][0] == '!' && i + 1 < n && !strcmp(lines[i + 1], line)) continue;
    strcat(out, lines[i]); strcat(out, "\n");
  }
  if (changed) { FILE *f = fopen(path, "w"); if (f) { fputs(out, f); fclose(f); } }
  free(out); free(b);
  return changed;
}
// valore "chiave=valore" dentro [sezione] di un .ini; out vuoto se manca
static void ini_get(const char *path, const char *section, const char *key, char *out, size_t n) {
  out[0] = 0;
  char *b = file_read(path, 256 * 1024, NULL); if (!b) return;
  int in = section == NULL;
  for (char *l = strtok(b, "\r\n"); l; l = strtok(NULL, "\r\n")) {
    while (*l == ' ' || *l == '\t') l++;
    if (*l == '[') { in = section && !strncmp(l + 1, section, strlen(section)) && l[1 + strlen(section)] == ']'; continue; }
    size_t k = strlen(key);
    if (in && !strncmp(l, key, k)) { char *v = l + k; while (*v == ' ') v++; if (*v == '=') { v++; while (*v == ' ') v++; snprintf(out, n, "%s", v); break; } }
  }
  free(b);
}
// cambia solo quella riga, lasciando il resto del file com'è
// section "" = file senza sezioni (pldmgr_config.txt); il file si crea se manca
static int ini_set(const char *path, const char *section, const char *key, const char *val) {
  char *b = file_read(path, 256 * 1024, NULL);
  if (!b) { FILE *f = fopen(path, "w"); if (!f) return -1; if (section[0]) fprintf(f, "[%s]\n", section); fprintf(f, "%s=%s\n", key, val); fclose(f); return 0; }
  size_t cap = strlen(b) + 512; char *o = malloc(cap); if (!o) { free(b); return -1; }
  size_t at = 0; int in = !section[0], done = 0;
  char *p = b;
  while (*p) {
    char *e = strchr(p, '\n'); size_t L = e ? (size_t)(e - p + 1) : strlen(p);
    char line[512]; size_t ll = L < sizeof line - 1 ? L : sizeof line - 1; memcpy(line, p, ll); line[ll] = 0;
    char *t = line; while (*t == ' ' || *t == '\t') t++;
    if (*t == '[' && section[0]) {
      if (in && !done) { at += (size_t)snprintf(o + at, cap - at, "%s=%s\n", key, val); done = 1; }
      in = !strncmp(t + 1, section, strlen(section)) && t[1 + strlen(section)] == ']';
    }
    if (in && !done && !strncmp(t, key, strlen(key)) && (t[strlen(key)] == '=' || t[strlen(key)] == ' ')) {
      at += (size_t)snprintf(o + at, cap - at, "%s=%s\n", key, val); done = 1;
    } else { memcpy(o + at, p, L); at += L; }
    p += L;
  }
  if (!done) {
    if (at && o[at - 1] != '\n') o[at++] = '\n';
    if (in && section[0]) at += (size_t)snprintf(o + at, cap - at, "%s=%s\n", key, val);       // sezione in fondo al file
    else if (section[0]) at += (size_t)snprintf(o + at, cap - at, "[%s]\n%s=%s\n", section, key, val);
    else at += (size_t)snprintf(o + at, cap - at, "%s=%s\n", key, val);
  }
  o[at] = 0;
  char tmp[600]; snprintf(tmp, sizeof tmp, "%s.omega-tmp", path);
  FILE *f = fopen(tmp, "w"); int rc = -1;
  if (f) { fputs(o, f); fclose(f); rc = rename(tmp, path); }
  free(o); free(b);
  return rc;
}

// -------------------------------------------------------------- il piano --
enum { ST_DAEMON, ST_DAEMON_AUTO, ST_PLUGIN, ST_PLUGIN_AUTO, ST_SHORTCUT, ST_PLDMGR_ON, ST_CLEAN, ST_N };
typedef struct { int todo, ok; char label[200]; } Step;
static Step steps[ST_N];
static char daemon_dst[300], daemon_src[300], plugin_src[300];

static const char *daemon_target(void) {
  switch (owner) {
    case OWN_ONION: return ONION "/payloads/" DAEMON;
    case OWN_ETA2: return ETA "/payloads/" DAEMON;
    case OWN_ETA1: return ETA "/plugins/" DAEMON;
    case OWN_PLDMGR: return PLDMGR "/payloads/OmegaRedirect/" DAEMON;
    case OWN_AUTOLOADER: return AUTOLOADER "/" DAEMON;
  }
  return "";
}

// voci di Omega in caricatori diversi da quello scelto
static int stray_entries(void) {
  int n = 0;
  if (owner != OWN_PLDMGR && has_line(PLDMGR "/autoload.txt", DAEMON)) n++;
  if (owner != OWN_AUTOLOADER && has_line(AUTOLOADER "/autoload.txt", DAEMON)) n++;
  if (owner != OWN_ONION && is_file(ONION "/payloads/" DAEMON ".auto_start")) n++;
  if (owner != OWN_ETA2 && is_file(ETA "/payloads/" DAEMON ".auto_start")) n++;
  if (owner != OWN_ETA1 && is_file(ETA "/plugins/" DAEMON ".auto_start")) n++;
  return n;
}

static void plan(void) {
  detect();
  memset(steps, 0, sizeof steps);
  snprintf(daemon_dst, sizeof daemon_dst, "%s", daemon_target());
  // da dove copiare: la copia portata dall'installer, o una già installata
  snprintf(daemon_src, sizeof daemon_src, BUNDLE "/" DAEMON);
  if (!is_file(daemon_src)) snprintf(daemon_src, sizeof daemon_src, PLDMGR "/payloads/OmegaRedirect/" DAEMON);
  if (!is_file(daemon_src)) daemon_src[0] = 0;
  snprintf(plugin_src, sizeof plugin_src, BUNDLE "/" PLUGIN);
  if (!is_file(plugin_src)) plugin_src[0] = 0;
  if (owner == OWN_NONE) return;

  char mk[340];
  if (daemon_src[0] && (!is_file(daemon_dst) || (strcmp(daemon_src, daemon_dst) && !same_file(daemon_src, daemon_dst)))) {
    steps[ST_DAEMON].todo = 1;
    snprintf(steps[ST_DAEMON].label, sizeof steps[ST_DAEMON].label, _("Installare il servizio di Omega (musica, notifiche, App mobile) in %s"), owner_name(owner));
  }
  int autostart_ok = 0;
  if (owner == OWN_PLDMGR) {
    char on[16]; ini_get(PLDMGR "/pldmgr_config.txt", NULL, "AUTOLOAD_ENABLED", on, sizeof on);
    autostart_ok = has_line(PLDMGR "/autoload.txt", DAEMON);
    if (strcmp(on, "1")) { steps[ST_PLDMGR_ON].todo = 1; snprintf(steps[ST_PLDMGR_ON].label, sizeof steps[ST_PLDMGR_ON].label, "%s", _("Accendere l'avvio automatico di Payload Manager")); }
  } else if (owner == OWN_AUTOLOADER) autostart_ok = has_line(AUTOLOADER "/autoload.txt", DAEMON);
  else { snprintf(mk, sizeof mk, "%s.auto_start", daemon_dst); autostart_ok = is_file(mk); }
  if (!autostart_ok) { steps[ST_DAEMON_AUTO].todo = 1; snprintf(steps[ST_DAEMON_AUTO].label, sizeof steps[ST_DAEMON_AUTO].label, _("Avviarlo da solo a ogni accensione (con %s)"), owner_name(owner)); }

  if (owner == OWN_ONION && plugin_src[0]) {
    if (!is_file(ONION "/plugins/" PLUGIN) || !same_file(plugin_src, ONION "/plugins/" PLUGIN)) {
      steps[ST_PLUGIN].todo = 1; snprintf(steps[ST_PLUGIN].label, sizeof steps[ST_PLUGIN].label, "%s", _("Aggiungere la pagina Omega al Toolbox di OnionHEN (menu durante il gioco)"));
    }
    if (!is_file(ONION "/plugins/" PLUGIN ".auto_start")) { steps[ST_PLUGIN_AUTO].todo = 1; snprintf(steps[ST_PLUGIN_AUTO].label, sizeof steps[ST_PLUGIN_AUTO].label, "%s", _("Attivare la pagina Omega a ogni accensione")); }
    char sc[32]; ini_get(ONION "/config.ini", "shortcuts", "toolbox", sc, sizeof sc);
    if (!sc[0] || !strcmp(sc, "off")) { steps[ST_SHORTCUT].todo = 1; snprintf(steps[ST_SHORTCUT].label, sizeof steps[ST_SHORTCUT].label, "%s", _("Aprire il Toolbox anche in gioco tenendo premuti L2+R3")); }
  }
  if (stray_entries()) { steps[ST_CLEAN].todo = 1; snprintf(steps[ST_CLEAN].label, sizeof steps[ST_CLEAN].label, "%s", _("Togliere Omega dagli altri caricatori, perché non parta due volte")); }
}
static int todo_count(void) { int n = 0; for (int i = 0; i < ST_N; i++) n += steps[i].todo; return n; }

static int ctl_alive(void) {
  char buf[256];
  return omega_http(HTTP_GET, "http://127.0.0.1:9095/v1/ping", NULL, NULL, buf, sizeof buf) == 200;
}

static int started_now, need_reboot, phase;   // phase: 0 proposta, 1 fatto
static void run_plan(void) {
  char mk[340];
  if (steps[ST_DAEMON].todo) {
    char dir[300]; snprintf(dir, sizeof dir, "%s", daemon_dst); char *sl = strrchr(dir, '/'); if (sl) *sl = 0;
    steps[ST_DAEMON].ok = mkdirs(dir) == 0 && (!strcmp(daemon_src, daemon_dst) || copy_file(daemon_src, daemon_dst) == 0);
  }
  if (steps[ST_DAEMON_AUTO].todo) {
    if (owner == OWN_PLDMGR) steps[ST_DAEMON_AUTO].ok = add_line(PLDMGR "/autoload.txt", DAEMON) == 0;
    else if (owner == OWN_AUTOLOADER) steps[ST_DAEMON_AUTO].ok = add_line(AUTOLOADER "/autoload.txt", DAEMON) == 0;
    else {
      snprintf(mk, sizeof mk, "%s.auto_start", daemon_dst); steps[ST_DAEMON_AUTO].ok = touch(mk) == 0;
      if (owner == OWN_ONION) {   // priorità alta e nessun ritardo (schema di OnionHEN, version numerico)
        snprintf(mk, sizeof mk, "%s.json", daemon_dst);
        FILE *f = fopen(mk, "w"); if (f) { fputs("{\"version\":1,\"priority\":900,\"delay_seconds\":0}\n", f); fclose(f); }
      }
    }
  }
  if (steps[ST_PLDMGR_ON].todo) steps[ST_PLDMGR_ON].ok = ini_set(PLDMGR "/pldmgr_config.txt", "", "AUTOLOAD_ENABLED", "1") == 0;
  if (steps[ST_PLUGIN].todo) steps[ST_PLUGIN].ok = mkdirs(ONION "/plugins") == 0 && copy_file(plugin_src, ONION "/plugins/" PLUGIN) == 0;
  if (steps[ST_PLUGIN_AUTO].todo) steps[ST_PLUGIN_AUTO].ok = touch(ONION "/plugins/" PLUGIN ".auto_start") == 0;
  if (steps[ST_SHORTCUT].todo) steps[ST_SHORTCUT].ok = ini_set(ONION "/config.ini", "shortcuts", "toolbox", "l2_r3") == 0;
  if (steps[ST_CLEAN].todo) {
    if (owner != OWN_PLDMGR) remove_line(PLDMGR "/autoload.txt", DAEMON);
    if (owner != OWN_AUTOLOADER) remove_line(AUTOLOADER "/autoload.txt", DAEMON);
    if (owner != OWN_ONION) unlink(ONION "/payloads/" DAEMON ".auto_start");
    if (owner != OWN_ETA2) unlink(ETA "/payloads/" DAEMON ".auto_start");
    if (owner != OWN_ETA1) unlink(ETA "/plugins/" DAEMON ".auto_start");
    steps[ST_CLEAN].ok = stray_entries() == 0;
  }
  // il servizio parte subito se non c'è già (altrimenti al prossimo avvio)
  started_now = 0;
  if (!ctl_alive() && is_file(daemon_dst)) {
    char err[200];
    if (payload_run_service(daemon_dst, err, sizeof err) == 0) { SDL_Delay(1500); started_now = ctl_alive(); }
  }
  need_reboot = steps[ST_PLUGIN].todo || steps[ST_PLUGIN_AUTO].todo || steps[ST_SHORTCUT].todo || (!started_now && !ctl_alive());
  FILE *f = fopen(SETUP_FILE, "w"); if (f) { fprintf(f, "%d 0\n", owner); fclose(f); }
  omega_log("componenti: %s, servizio %s", owner_name(owner), started_now ? "avviato subito" : "attivo dal prossimo avvio");
}

// ------------------------------------------------------------- pannello --
static int sel; static float anim;
static int asking_home;          // la stessa finestra fa anche la domanda "Omega come Home?"

void hen_ask_home(void) { asking_home = 1; sel = home_mode() == 0 ? 1 : 0; ov_push(OV_SETUP); }

void hen_check(int ask) {
  plan();
  if (!todo_count()) { if (ask > 1) set_msg(_("Tutto a posto: i componenti di Omega sono già installati e attivi"), 0); return; }
  if (ask == 1) {   // all'avvio: non insistere se l'utente ha detto "non chiedere più" per questo caricatore
    char *s = file_read(SETUP_FILE, 64, NULL);
    int o = -1, no = 0, gen = 0; if (s) { sscanf(s, "%d %d %d", &o, &no, &gen); free(s); }
    if (no && o == owner && gen >= SETUP_ASK) return;
  }
  phase = 0; sel = 0; asking_home = 0;
  ov_push(OV_SETUP);
}

void setup_draw(float t) {
  int a = (int)(255 * t);
  fill_rect(0, 0, SCREEN_W, SCREEN_H, RGB(2, 4, 10), (int)(170 * t));
  int w = 1240, h = 820, x = SCREEN_W / 2 - w / 2, y = SCREEN_H / 2 - h / 2 + (int)((1 - ease_out(t)) * 40);
  shadow_rrect(x, y, w, h, 34, 50, a * 70 / 100);
  fill_rrect(x, y, w, h, 34, C_PANEL, a);
  draw_logo(x + 100, y + 96, 96, a);
  if (asking_home) {
    draw_text(font(W_LIGHT, 42), _("Omega come Home?"), x + 170, y + 66, C_WHITE, a, AL_L);
    draw_text_wrap(font(W_REG, 28), _("Se vuoi, Omega prende il posto della Home della console: si apre all'accensione e ogni volta che torni alla Home."), x + 70, y + 200, w - 140, 3, 42, C_TXT, a);
    draw_text_wrap(font(W_REG, 28), _("Se preferisci di no, Omega resta un'app come le altre: la apri tu da websrv quando ti serve. Musica, notifiche durante i giochi e caricamenti dall'App mobile funzionano in tutti e due i casi."), x + 70, y + 350, w - 140, 4, 42, C_DIM, a);
    draw_text_wrap(font(W_REG, 23), _("Puoi cambiare idea quando vuoi in Impostazioni › Omega come Home."), x + 70, y + 560, w - 140, 2, 32, C_FAINT, a);
    const char *hb[2] = { _("Sì, usa Omega come Home"), _("No, solo come app") };
    anim = approach(anim, (float)sel, 18.0f);
    for (int i = 0; i < 2; i++) {
      int bw = 460, bx = x + w - 70 - 2 * bw - 24 + i * (bw + 24), by = y + h - 100;
      float fa = clampf(1 - fabsf(anim - i), 0, 1);
      fill_rrect(bx, by, bw, 64, 32, mix(RGB(48, 54, 72), C_WHITE, fa), a);
      draw_text_fit(font(W_MED, 26), hb[i], bx + bw / 2, by + 32 - TTF_FontHeight(font(W_MED, 26)) / 2, bw - 30, mix(C_TXT, RGB(12, 14, 22), fa), a, AL_C);
    }
    const int ic[] = { IC_BTN_X }; const char *lb[] = { _("Scegli") };
    hints(ic, lb, 1, a);
    return;
  }
  draw_text(font(W_LIGHT, 42), phase ? _("Fatto") : _("Prepariamo la console"), x + 170, y + 52, C_WHITE, a, AL_L);
  char l[200];
  if (owner == OWN_NONE) snprintf(l, sizeof l, "%s", _("Nessun caricatore rilevato (OnionHEN, etaHEN, Payload Manager)"));
  else snprintf(l, sizeof l, _("Jailbreak rilevato: %s"), owner_name(owner));
  draw_text_fit(font(W_REG, 26), l, x + 172, y + 106, w - 230, C_ACC2, a, AL_L);

  int ly = y + 190;
  if (owner == OWN_NONE) {
    draw_text_wrap(font(W_REG, 27), _("Senza un caricatore il servizio di Omega non parte da solo all'accensione: musica, notifiche durante i giochi e caricamenti dall'App mobile funzionano solo dopo averlo avviato a mano. Installa OnionHEN, etaHEN o Payload Manager e riapri questa schermata da Impostazioni › Sistema e strumenti."),
                   x + 70, ly, w - 140, 6, 40, C_TXT, a);
  } else {
    draw_text(font(W_MED, 27), phase ? _("Ecco cosa è stato fatto:") : _("Per far funzionare Omega al meglio installo e attivo:"), x + 70, ly, C_TXT, a, AL_L);
    ly += 56;
    for (int i = 0; i < ST_N; i++) {
      if (!steps[i].todo) continue;
      Col c = phase ? (steps[i].ok ? C_OK : C_ERR) : C_ACC2;
      draw_icon(phase ? (steps[i].ok ? IC_CHECK : IC_CLOSE) : IC_ARROW_R, x + 92, ly + 16, 26, c, a);
      draw_text_wrap(font(W_REG, 25), steps[i].label, x + 126, ly, w - 200, 2, 32, C_TXT, a);
      ly += 66;
    }
    if (phase) {
      const char *end = started_now ? _("Il servizio è già partito.") : need_reboot ? _("Riavvia la console per attivare tutto.") : _("Il servizio era già attivo.");
      draw_text_wrap(font(W_MED, 26), end, x + 70, ly + 10, w - 140, 2, 34, C_WHITE, a);
      if (steps[ST_SHORTCUT].todo && steps[ST_SHORTCUT].ok)
        draw_text_wrap(font(W_REG, 23), _("In gioco: tieni premuti L2+R3, poi Toolbox › Plugin › Omega."), x + 70, ly + 54, w - 140, 2, 30, C_DIM, a);
    } else
      draw_text_wrap(font(W_REG, 22), _("Si modificano solo i file di Omega e, se serve, l'impostazione del caricatore. Puoi rifarlo quando vuoi da Impostazioni › Sistema e strumenti."), x + 70, y + h - 170, w - 140, 2, 30, C_FAINT, a);
  }

  // pulsanti
  const char *btn[3]; int nb = 0;
  if (phase || owner == OWN_NONE) btn[nb++] = _("OK");
  else { btn[nb++] = _("Installa e attiva"); btn[nb++] = _("Non ora"); btn[nb++] = _("Non chiedere più"); }
  if (sel >= nb) sel = nb - 1;
  anim = approach(anim, (float)sel, 18.0f);
  int bw = 330, gap = 24, bx0 = x + w - 70 - nb * bw - (nb - 1) * gap, by = y + h - 100;
  for (int i = 0; i < nb; i++) {
    int bx = bx0 + i * (bw + gap);
    float fa = clampf(1 - fabsf(anim - i), 0, 1);
    fill_rrect(bx, by, bw, 64, 32, mix(RGB(48, 54, 72), C_WHITE, fa), a);
    draw_text_fit(font(W_MED, 26), btn[i], bx + bw / 2, by + 32 - TTF_FontHeight(font(W_MED, 26)) / 2, bw - 30, mix(C_TXT, RGB(12, 14, 22), fa), a, AL_C);
  }
  const int ic[] = { IC_BTN_X }; const char *lb[] = { _("Scegli") };
  hints(ic, lb, 1, a);
}

void setup_input(int b) {
  if (asking_home) {
    if (b == B_LEFT && sel > 0) { sel--; sfx_play(SFX_MOVE); }
    else if (b == B_RIGHT && sel < 1) { sel++; sfx_play(SFX_MOVE); }
    else if (b == B_X) {
      home_mode_set(sel == 0);
      set_msg(sel == 0 ? _("Omega sarà la tua Home: si apre all'avvio e quando torni alla Home") : _("Omega resta un'app: la apri tu da websrv quando vuoi"), 0);
      asking_home = 0; ov_pop(); sfx_play(SFX_SELECT);
    }
    return;   // la scelta serve: ○ non chiude
  }
  int nb = phase || owner == OWN_NONE ? 1 : 3;
  if (b == B_LEFT && sel > 0) { sel--; sfx_play(SFX_MOVE); }
  else if (b == B_RIGHT && sel < nb - 1) { sel++; sfx_play(SFX_MOVE); }
  else if (b == B_O) ov_pop();
  else if (b == B_X) {
    if (phase || owner == OWN_NONE) { ov_pop(); return; }
    if (sel == 0) { run_plan(); phase = 1; sel = 0; sfx_play(SFX_SELECT); }
    else if (sel == 1) ov_pop();
    else { FILE *f = fopen(SETUP_FILE, "w"); if (f) { fprintf(f, "%d 1 %d\n", owner, SETUP_ASK); fclose(f); } ov_pop(); set_msg(_("Va bene: puoi farlo quando vuoi da Impostazioni › Sistema e strumenti"), 0); }
  }
}
