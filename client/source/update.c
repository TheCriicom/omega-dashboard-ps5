// Omega UI — aggiornamenti automatici dell'app e del demone.
//
// Si interroga sempre OMEGA_BASE_URL, anche se l'utente usa un altro server.
// Il manifest (/updates/manifest.json) è firmato Ed25519 con una chiave che
// resta sul computer di chi pubblica: né il server né la rete (il TLS non è
// verificato) possono far installare altro. Ogni file deve avere lo SHA-256
// del manifest, e "seq" impedisce di tornare a un manifest più vecchio.
//
// I percorsi di destinazione sono fissi, mai presi dal server; un componente
// che non è installato sulla console non viene aggiunto. L'app nuova vale dal
// riavvio, il demone dal prossimo avvio della console.
#include "app.h"
#include <signal.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include "monocypher-ed25519.h"
#include <ctype.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define UPD_URL  OMEGA_BASE_URL "/updates"
#define UPD_DIR  OMEGA_DIR "/update"
#define UPD_STATE OMEGA_DIR "/update.json"
#define FIRST_CHECK_MS (20u * 1000)
#define EVERY_MS       (6u * 3600 * 1000)

// chiave pubblica di chi firma gli aggiornamenti (publish-update.sh)
static const char PUBKEY_HEX[] = "231ab09e789dc09d0248a3410dcb3d474cea89c112ddeec790a39689db9076f2";

// install_in: se il file manca ma questa cartella esiste, lo si installa;
// altrimenti si aggiorna solo se è già installato (l'attivazione la fa hen.c, dopo l'OK).
typedef struct { const char *id; const char *path; int sidecar; const char *install_in; } Target;
static const Target TARGETS[] = {
  { "omega_ui",       OMEGA_HB_ROOT "/OmegaUI/OmegaUI.elf", 0, NULL },
  // copie da cui la configurazione (hen.c) installa servizio e plugin
  { "omega_redirect", OMEGA_HB_ROOT "/OmegaUI/payloads/omega_redirect.elf", 0, OMEGA_HB_ROOT "/OmegaUI/payloads" },
  { "omega_onion",    OMEGA_HB_ROOT "/OmegaUI/payloads/OMGA00001.elf", 0, OMEGA_HB_ROOT "/OmegaUI/payloads" },
  // copie attive, ovunque la configurazione le abbia messe (solo se ci sono già)
  { "omega_redirect", OMEGA_PLD_ROOT "/OmegaRedirect/omega_redirect.elf", 1, NULL },   // + .elf.json di Payload Manager
  { "omega_redirect", OMEGA_SYSROOT "/data/OnionHEN/payloads/omega_redirect.elf", 0, NULL },
  { "omega_redirect", OMEGA_SYSROOT "/data/etaHEN/payloads/omega_redirect.elf", 0, NULL },
  { "omega_redirect", OMEGA_SYSROOT "/data/etaHEN/plugins/omega_redirect.elf", 0, NULL },
  { "omega_redirect", OMEGA_SYSROOT "/data/ps5_autoloader/omega_redirect.elf", 0, NULL },
  { "omega_onion",    OMEGA_SYSROOT "/data/OnionHEN/plugins/OMGA00001.elf", 0, NULL },   // pagina nel Toolbox di OnionHEN
};
#define NT (int)(sizeof TARGETS / sizeof TARGETS[0])

// esito per il thread principale
static SDL_atomic_t done_ui, done_daemon;
static char new_ver[32];

// ------------------------------------------------------------------ utilità --
static int unhex(const char *h, unsigned char *out, size_t n) {
  if (strlen(h) < n * 2) return -1;
  for (size_t i = 0; i < n; i++) {
    unsigned v; if (sscanf(h + 2 * i, "%2x", &v) != 1) return -1;
    out[i] = (unsigned char)v;
  }
  return 0;
}
// "2026.10.03.2" > "1.20": confronto numerico parte per parte
static void verparts(const char *v, long out[6]) {
  for (int i = 0; i < 6; i++) {
    out[i] = 0;
    while (*v && !isdigit((unsigned char)*v)) v++;
    while (isdigit((unsigned char)*v)) out[i] = out[i] * 10 + (*v++ - '0');
  }
}
static int vercmp(const char *a, const char *b) {
  long x[6], y[6]; verparts(a, x); verparts(b, y);
  for (int i = 0; i < 6; i++) if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
  return 0;
}

static long state_seq(void) {
  char *s = file_read(UPD_STATE, 4096, NULL); if (!s) return 0;
  JVal *j = json_parse(s); long q = (long)jnum(j, "seq", 0); json_free(j); free(s); return q;
}
static void state_save(long seq) {
  FILE *f = fopen(UPD_STATE ".tmp", "w"); if (!f) return;
  fprintf(f, "{\"seq\":%ld,\"checked\":%ld}\n", seq, (long)time(NULL)); fclose(f);
  rename(UPD_STATE ".tmp", UPD_STATE);
}

// aggiorna "checksum" e "version" nel .elf.json di Payload Manager, il resto non si tocca
static void sidecar_update(const char *elf, const char *sha, const char *ver) {
  char p[400]; snprintf(p, sizeof p, "%s.json", elf);
  char *s = file_read(p, 64 * 1024, NULL); if (!s) return;
  static char out[70 * 1024]; size_t o = 0;
  const char *keys[2] = { "\"checksum\"", "\"version\"" }; const char *vals[2] = { sha, ver };
  const char *c = s;
  while (*c && o < sizeof out - 200) {
    int hit = -1;
    for (int k = 0; k < 2; k++) if (!strncmp(c, keys[k], strlen(keys[k]))) hit = k;
    if (hit < 0) { out[o++] = *c++; continue; }
    const char *q = strchr(c + strlen(keys[hit]), '"');          // inizio del valore
    const char *e = q ? strchr(q + 1, '"') : NULL;
    if (!q || !e) { out[o++] = *c++; continue; }
    o += (size_t)snprintf(out + o, sizeof out - o, "%s:\"%s\"", keys[hit], vals[hit]);
    c = e + 1;
  }
  out[o] = 0; free(s);
  char t[420]; snprintf(t, sizeof t, "%s.tmp", p);
  FILE *f = fopen(t, "w"); if (!f) return;
  fwrite(out, 1, o, f); fclose(f); rename(t, p);
}

// ---------------------------------------------------------------- controllo --
static int check_once(void) {
  mkdir(OMEGA_DIR, 0777); mkdir(UPD_DIR, 0777);
  char mpath[300], spath[300];
  snprintf(mpath, sizeof mpath, "%s/manifest.json", UPD_DIR);
  snprintf(spath, sizeof spath, "%s/manifest.sig", UPD_DIR);
  if (omega_url_download(UPD_URL "/manifest.json", mpath, NULL, NULL, NULL) != 200) return -1;
  if (omega_url_download(UPD_URL "/manifest.sig", spath, NULL, NULL, NULL) != 200) return -1;
  size_t mlen; char *m = file_read(mpath, 256 * 1024, &mlen); char *sg = file_read(spath, 1024, NULL);
  unsigned char sig[64], pub[32];
  int ok = m && sg && unhex(sg, sig, 64) == 0 && unhex(PUBKEY_HEX, pub, 32) == 0 &&
           crypto_ed25519_check(sig, pub, (const unsigned char *)m, mlen) == 0;
  free(sg);
  if (!ok) { omega_log("aggiornamenti: firma del manifest NON valida, ignorato"); free(m); return -2; }
  JVal *j = json_parse(m); free(m);
  long seq = (long)jnum(j, "seq", 0);
  if (seq < state_seq()) { omega_log("aggiornamenti: manifest vecchio (seq %ld), ignorato", seq); json_free(j); return 0; }
  JVal *comps = jget(j, "components");
  int ui_ready = 0;
  for (int t = 0; t < NT; t++) {
    JVal *c = NULL;
    JFOR(it, comps) if (!strcmp(jstr(it, "id", ""), TARGETS[t].id)) c = it;
    if (!c) continue;
    const char *ver = jstr(c, "version", ""), *sha = jstr(c, "sha256", ""), *file = jstr(c, "file", "");
    struct stat st;
    int fresh = 0;
    if (stat(TARGETS[t].path, &st) != 0) {
      if (!TARGETS[t].install_in || stat(TARGETS[t].install_in, &st) != 0 || !S_ISDIR(st.st_mode)) continue;   // non installato
      fresh = 1;
    }
    if (strlen(sha) != 64 || !file[0] || strchr(file, '/') || strstr(file, "..")) continue;
    // l'app confronta la propria versione, il demone il contenuto del file
    if (t == 0 && vercmp(ver, OMEGA_VERSION) <= 0) continue;
    char cur[65] = ""; if (!fresh) file_sha256(TARGETS[t].path, cur);
    if (!strcmp(cur, sha)) continue;
    char url[512], tmp[420];
    snprintf(url, sizeof url, "%s/%s", UPD_URL, file);
    snprintf(tmp, sizeof tmp, "%s.new", TARGETS[t].path);
    omega_log("aggiornamenti: scarico %s %s", TARGETS[t].id, ver);
    if (omega_url_download(url, tmp, NULL, NULL, NULL) != 200) { unlink(tmp); continue; }
    char got[65] = ""; file_sha256(tmp, got);
    if (strcmp(got, sha)) { omega_log("aggiornamenti: sha256 diverso per %s, scartato", TARGETS[t].id); unlink(tmp); continue; }
    chmod(tmp, 0755);
    char old[420]; snprintf(old, sizeof old, "%s.old", TARGETS[t].path);
    unlink(old); if (!fresh) link(TARGETS[t].path, old);          // la versione precedente resta come .old
    if (rename(tmp, TARGETS[t].path) != 0) { omega_log("aggiornamenti: sostituzione di %s fallita", TARGETS[t].path); unlink(tmp); continue; }
    if (TARGETS[t].sidecar) sidecar_update(TARGETS[t].path, sha, ver);

    omega_log("aggiornamenti: %s aggiornato a %s", TARGETS[t].id, ver);
    if (t == 0) { snprintf(new_ver, sizeof new_ver, "%s", ver); ui_ready = 1; }
    else if (!strcmp(TARGETS[t].id, "omega_redirect") && !TARGETS[t].install_in) SDL_AtomicSet(&done_daemon, 1);
  }
  json_free(j);
  state_save(seq);
  // il riavvio si propone solo a fine giro: prima devono finire anche servizio e plugin
  if (ui_ready) SDL_AtomicSet(&done_ui, 1);
  return 0;
}

// ------------------------------------------------------------- servizio --
// Il file del servizio si aggiorna, ma quello in esecuzione resterebbe il vecchio
// fino al riavvio della console: lo si chiude (/v1/quit) e si avvia il nuovo.
// Lo stesso se all'avvio di Omega il servizio configurato non risponde.
static SDL_atomic_t svc_result;   // 0 niente, 1 riavviato, 2 serve il riavvio della console
static int svc_alive(void) { char b[256]; return omega_http(HTTP_GET, "http://127.0.0.1:9095/v1/ping", NULL, NULL, b, sizeof b) == 200; }
// Servizio bloccato senza file del pid (versioni vecchie): lo si cerca per nome
// tra i processi (sysctl kern.proc). Si avanza con ki_structsize di ogni voce.
#ifdef PS5
#include <sys/sysctl.h>
#include <sys/user.h>
static int kill_by_name(const char *needle) {
  int mib[3] = { CTL_KERN, KERN_PROC, KERN_PROC_PROC }; size_t len = 0;
  if (sysctl(mib, 3, NULL, &len, NULL, 0) != 0 || !len) return 0;
  len += 64 * 1024;
  char *b = malloc(len); if (!b) return 0;
  int n = 0;
  if (sysctl(mib, 3, b, &len, NULL, 0) == 0) {
    for (size_t off = 0; off + sizeof(struct kinfo_proc) <= len + sizeof(struct kinfo_proc); ) {
      struct kinfo_proc *kp = (struct kinfo_proc *)(b + off);
      int sz = kp->ki_structsize;
      if (sz <= 0 || off + (size_t)sz > len) break;
      char comm[COMMLEN + 1]; memcpy(comm, kp->ki_comm, COMMLEN); comm[COMMLEN] = 0;
      if (strcasestr(comm, "omega") || strcasestr(comm, ".elf") || strcasestr(comm, "redirect")) omega_log("processo %d: %s", (int)kp->ki_pid, comm);
      if (strstr(comm, needle) && kp->ki_pid != getpid()) {
        int rc = kill(kp->ki_pid, SIGKILL);
        omega_log("servizio bloccato: chiuso %s (pid %d, %d)", comm, (int)kp->ki_pid, rc);
        n++;
      }
      off += (size_t)sz;
    }
  }
  free(b);
  return n;
}
#else
static int kill_by_name(const char *needle) { (void)needle; return 0; }
#endif

// la porta 9095 accetta connessioni (anche se il servizio non risponde)
static int svc_port_busy(void) {
#ifdef PS5
  int s = socket(AF_INET, SOCK_STREAM, 0); if (s < 0) return 0;
  struct sockaddr_in a; memset(&a, 0, sizeof a);
  a.sin_family = AF_INET; a.sin_port = htons(9095); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  int busy = connect(s, (struct sockaddr *)&a, sizeof a) == 0;
  close(s);
  return busy;
#else
  return 0;
#endif
}
static int port_up(int port) {
#ifdef PS5
  int s = socket(AF_INET, SOCK_STREAM, 0); if (s < 0) return 0;
  struct sockaddr_in a; memset(&a, 0, sizeof a);
  a.sin_family = AF_INET; a.sin_port = htons((unsigned short)port); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  int ok = connect(s, (struct sockaddr *)&a, sizeof a) == 0;
  close(s);
  return ok;
#else
  (void)port; return 1;
#endif
}
static int elfldr_up(void) {
#ifdef PS5
  int s = socket(AF_INET, SOCK_STREAM, 0); if (s < 0) return 0;
  struct sockaddr_in a; memset(&a, 0, sizeof a);
  a.sin_family = AF_INET; a.sin_port = htons(9021); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  int ok = connect(s, (struct sockaddr *)&a, sizeof a) == 0;
  close(s);
  return ok;
#else
  return 1;
#endif
}
static int svc_stale(void) {
  char b[256], path[300]; struct stat st;
  if (omega_http(HTTP_GET, "http://127.0.0.1:9095/v1/ping", NULL, NULL, b, sizeof b) != 200) return 0;
  if (!hen_daemon_path(path, sizeof path) || stat(path, &st) != 0) return 0;
  const char *p = strstr(b, "\"started\":");
  long started = p ? atol(p + 10) : 0;   // versioni vecchie: niente "started"
  return (long)st.st_mtime > started + 5;
}
static int svc_start(int stop_old) {
  char path[300], err[200], b[256];
  if (!hen_daemon_path(path, sizeof path)) return 0;
  // bloccato: la porta è occupata ma non risponde. Lo si chiude di forza (pid scritto dal servizio)
  // (la porta può anche rifiutare le connessioni se la coda è piena: non si guarda)
  if (!svc_alive()) {
    char *pf = file_read(OMEGA_DIR "/omega_redirect.pid", 32, NULL);
    int pid = pf ? atoi(pf) : 0; free(pf);
    if (pid > 1 && pid != (int)getpid()) {
      int rc = kill(pid, SIGKILL);
      omega_log("servizio bloccato: chiuso il processo %d (%d)", pid, rc);
      for (int i = 0; i < 20 && svc_port_busy(); i++) SDL_Delay(250);
    }
    if (!svc_alive() && kill_by_name("omega_redirect")) SDL_Delay(1500);
  }
  if (svc_alive()) {
    if (!stop_old) return 0;
    if (!elfldr_up() && !port_up(8080)) return 2;   // nessun caricatore: il vecchio resta fino al riavvio
    if (omega_http(HTTP_POST, "http://127.0.0.1:9095/v1/quit", NULL, "{}", b, sizeof b) != 200) return 2;   // servizio vecchio, senza /v1/quit
    for (int i = 0; i < 20 && svc_alive(); i++) SDL_Delay(250);
  }
  if (payload_run_service(path, err, sizeof err) != 0) { omega_log("servizio: avvio non riuscito (%s)", err); return 2; }
  for (int i = 0; i < 16 && !svc_alive(); i++) SDL_Delay(250);
  int ok = svc_alive();
  omega_log("servizio: %s %s", path, ok ? "avviato" : "non risponde dopo l'avvio");
  return ok ? 1 : 2;
}

static int upd_thread(void *arg) {
  (void)arg;
  SDL_Delay(FIRST_CHECK_MS);
  Uint32 next_check = 0;
  int restarts = 0; Uint32 restart_window = SDL_GetTicks();
  for (;;) {
    // servizio configurato ma fermo (crash o avvio mancato): si riavvia, al massimo 5 volte l'ora
    if (SDL_GetTicks() - restart_window > 3600u * 1000) { restarts = 0; restart_window = SDL_GetTicks(); }
    if (restarts < 5 && !svc_alive()) { int rr = svc_start(0); if (rr) { restarts++; if (rr == 1) omega_log("servizio: non rispondeva, riavviato"); } }
    else if (restarts < 5 && svc_stale()) { omega_log("servizio: gira una versione vecchia, lo sostituisco"); restarts++; svc_start(1); }
    if (!next_check || SDL_GetTicks() >= next_check) {
      int r = check_once();
      if (SDL_AtomicGet(&done_daemon)) SDL_AtomicSet(&svc_result, svc_start(1));
      if (r < 0 && r != -2) omega_log("aggiornamenti: server Omega non raggiungibile (%d)", r);
      next_check = SDL_GetTicks() + EVERY_MS;
    }
    SDL_Delay(60 * 1000);
  }
  return 0;
}

void update_init(void) {
  mkdir(OMEGA_HB_ROOT "/OmegaUI/payloads", 0777);   // chi viene da versioni vecchie riceve qui le copie
  SDL_CreateThread(upd_thread, "update", NULL);
}

// -------------------------------------------------------- thread principale --
static void restart_now(int idx, void *ud) {
  (void)idx; (void)ud;
#ifdef PS5
  // websrv chiude l'app in primo piano (Omega) e avvia la versione nuova
  unsigned char b[16];
  if (omega_url_peek(WEBSRV_URL "/hbldr?pipe=0&daemon=0&path=%2Fdata%2Fhomebrew%2FOmegaUI%2FOmegaUI.elf&cwd=%2Fdata%2Fhomebrew%2FOmegaUI", b, sizeof b) < 0)
    set_msg(_("Riavvia Omega per usare la nuova versione"), 0);
#else
  set_msg("(desktop) riavvio non disponibile", 0);
#endif
}

void update_tick(void) {
  int sr = SDL_AtomicGet(&svc_result);
  if (sr && SDL_AtomicGet(&done_daemon) && g_scene == SC_HOME) {
    SDL_AtomicSet(&done_daemon, 0); SDL_AtomicSet(&svc_result, 0);
    if (sr == 1) toast(IC_DOWNLOAD, NULL, 0, _("Servizio Omega aggiornato"), _("Musica e App mobile usano gi\xC3\xA0 la nuova versione"));
    else toast(IC_DOWNLOAD, NULL, 0, _("Servizio Omega aggiornato"), _("Attivo dal prossimo riavvio della console"));
  }
  if (SDL_AtomicGet(&done_ui) && g_scene == SC_HOME && ov_depth() == 0 && !install_busy()) {
    SDL_AtomicSet(&done_ui, 0);
    static char q[512];
    snprintf(q, sizeof q, _("Omega %s \xC3\xA8 stato scaricato e installato. Riavviare ora per usarlo?"), new_ver);
    confirm_open(q, _("Riavvia"), restart_now, NULL);
  }
}
