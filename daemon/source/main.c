// omega_redirect — demone avviato da Payload Manager che fa di Omega la shell:
//  · se l'utente ha scelto Omega come Home (home-mode.txt = 1, domanda della UI
//    al primo avvio): all'avvio lancia la UI tramite websrv e la rilancia quando
//    si torna alla Home di sistema. Altrimenti Omega resta un'app qualsiasi;
//  · ogni 30 s comunica al server il gioco in primo piano, anche se avviato
//    dalla Home di sistema;
//  · durante il gioco controlla ogni 8 s le notifiche nuove e le mostra come
//    notifiche di sistema;
//  · suona la musica (player.c) anche mentre si gioca, comandato dalla UI
//    attraverso il server di controllo locale (ctl.c).
// Quando la UI è in primo piano (ui-active recente) presenza e notifiche le
// gestisce lei. Usa solo lo stato del primo piano e /hbldr di websrv.
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "ctl.h"
#include "lib.h"
#include "json.h"
#include "player.h"

#ifndef OMEGA_BASE_URL
#define OMEGA_BASE_URL "https://play.omegasuite.it"
#endif
#ifndef OMEGA_API
#define OMEGA_API "/api/v1"
#endif

// Cartella dati condivisa con la UI (source/omega.h): i percorsi devono coincidere.
#define OMEGA_DIR        "/data/Omega"
#define OMEGA_DIR_LEGACY "/data/OmegaPSNLab"   // nome delle prime versioni, migrato all'avvio
#define LOG              OMEGA_DIR "/omega-redirect.log"
#define PLAYER_STATE     OMEGA_DIR "/player.json"
#define LIBRARY_FILE     OMEGA_DIR "/library.json"   // "La mia libreria" (lib.c)
#define FAN_FILE         OMEGA_DIR "/fan.txt"      // soglia della ventola scelta in Sistema
#define HOME_MODE_FILE   OMEGA_DIR "/home-mode.txt" // "1" = Omega come Home (scelta nella UI)
#define OMEGA_SESSION    OMEGA_DIR "/session.json"
#define OMEGA_UI_ACTIVE  OMEGA_DIR "/ui-active"
#define OMEGA_ELF        "/data/homebrew/OmegaUI/OmegaUI.elf"
#define OMEGA_CWD        "/data/homebrew/OmegaUI"
#define WEBSRV_PORT      8080

#define BOOT_DELAY_S     20    // lascia partire websrv e il resto dell'autoload
#define BOOT_TRIES       20
#define RELAUNCH_COOLDOWN_S 10
#define PRESENCE_EVERY_S 30
#define NOTIFY_EVERY_S   8
#define UI_FRESH_S       15    // la UI aggiorna ui-active ogni 2 s quando è in primo piano
#define LOOP_S           2

#define HTTP_GET  0
#define HTTP_POST 1

int sceSystemServiceGetAppIdOfRunningBigApp(void);
int sceSystemServiceGetAppTitleId(int appId, char *titleId);
int sceSystemServiceParamGetInt(int paramId, int *value);

// Traduzioni (i18n_data.c, generato da omega-ui-src/tools/i18n-gen.mjs con gli
// stessi cataloghi della UI): lingua scelta nella UI o, se manca, della console.
const char *i18n_tr(const char *msgid) __attribute__((format_arg(1)));
void i18n_init(int sys_lang);
const char *i18n_code(void);
#define _(s) i18n_tr(s)

// SCE_SYSTEM_SERVICE_PARAM_ID_LANG = 1; -1 se non disponibile
static int sys_lang(void) { int v = -1; return sceSystemServiceParamGetInt(1, &v) == 0 ? v : -1; }

static void lg(const char *fmt, ...) {
  int fd = open(LOG, O_WRONLY | O_CREAT | O_APPEND, 0666); if (fd < 0) return;
  char b[512]; int n = snprintf(b, sizeof b, "[%lu] ", (unsigned long)time(NULL));
  va_list ap; va_start(ap, fmt); n += vsnprintf(b + n, sizeof b - n, fmt, ap); va_end(ap);
  if (n < (int)sizeof b - 1) b[n++] = '\n'; write(fd, b, (size_t)n); close(fd);
}

void player_log(const char *fmt, ...) {
  char b[400]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
  lg("%s", b);
}

// Chiede a websrv (GET /hbldr) di avviare la UI. 0 = richiesta inviata.
static int launch_omega(void) {
  int s = socket(AF_INET, SOCK_STREAM, 0); if (s < 0) return -1;
  struct sockaddr_in a; memset(&a, 0, sizeof a);
  a.sin_family = AF_INET; a.sin_port = htons(WEBSRV_PORT);
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (connect(s, (struct sockaddr *)&a, sizeof a) != 0) { close(s); return -2; }
  const char *req =
    "GET /hbldr?pipe=0&daemon=0&path=" OMEGA_ELF "&cwd=" OMEGA_CWD " HTTP/1.1\r\n"
    "Host: 127.0.0.1\r\nConnection: close\r\n\r\n";
  write(s, req, strlen(req));
  char tmp[256]; read(s, tmp, sizeof tmp);
  close(s);
  return 0;
}

// Omega come Home? Si rilegge ogni 5 s: la scelta si cambia dalla UI in qualsiasi momento.
static int home_mode_on(void) {
  static int cached = 0; static time_t at;
  time_t now = time(NULL);
  if (at && now - at < 5) return cached;
  at = now;
  char b[8] = ""; int fd = open(HOME_MODE_FILE, O_RDONLY);
  if (fd >= 0) { if (read(fd, b, sizeof b - 1) < 0) b[0] = 0; close(fd); }
  cached = b[0] == '1';
  return cached;
}

static int is_home(void) {
  int appId = sceSystemServiceGetAppIdOfRunningBigApp();
  if (appId < 0) return 1;                 // nessuna app in primo piano
  char tid[64] = {0};
  if (sceSystemServiceGetAppTitleId(appId, tid) != 0) return 0;
  return strncmp(tid, "NPXS", 4) == 0;     // app di sistema (Home, impostazioni...)
}

// ------------------------------------------------------------------ HTTP(S) --
int sceSysmoduleLoadModule(uint16_t id);
int sceNetInit(void);
int sceNetPoolCreate(const char *name, int size, int flags);
int sceSslInit(uint64_t poolSize);
int sceHttpInit(int netMemId, int sslCtxId, uint64_t poolSize);
int sceHttpCreateTemplate(int httpCtxId, const char *ua, int httpVer, int autoProxy);
int sceHttpsDisableOption(int id, uint32_t flags);
int sceHttpSetConnectTimeOut(int id, uint32_t usec);
int sceHttpSetRecvTimeOut(int id, uint32_t usec);
typedef int (*ssl_cb_t)(int, void *const[], int, void *);
int sceHttpsSetSslCallback(int id, ssl_cb_t cb, void *arg);
int sceHttpCreateConnectionWithURL(int tmpl, const char *url, int keepAlive);
int sceHttpCreateRequestWithURL(int conn, int method, const char *url, uint64_t clen);
int sceHttpAddRequestHeader(int id, const char *name, const char *value, uint32_t mode);
int sceHttpSendRequest(int req, const void *data, size_t size);
int sceHttpGetStatusCode(int req, int *status);
int sceHttpReadData(int req, void *data, size_t size);
int sceHttpDeleteRequest(int req);
int sceHttpSetAutoRedirect(int id, int onOff);
int sceHttpDeleteConnection(int conn);

// Come nella UI, i certificati non vengono verificati.
static int tmpl = -1;
static int accept_cert(int e, void *const c[], int n, void *u) { (void)e; (void)c; (void)n; (void)u; return 0; }
static int http_ready(void) {
  if (tmpl >= 0) return 1;
  sceSysmoduleLoadModule(0x0009); sceSysmoduleLoadModule(0x0045); sceSysmoduleLoadModule(0x000A);
  sceNetInit();
  int pool = sceNetPoolCreate("omega-redirect", 32 * 1024, 0); if (pool < 0) return 0;
  int ssl = sceSslInit(256 * 1024); if (ssl < 0) return 0;
  int http = sceHttpInit(pool, ssl, 256 * 1024); if (http < 0) return 0;
  tmpl = sceHttpCreateTemplate(http, "OmegaRedirect/1.00", 2, 1); if (tmpl < 0) return 0;
  sceHttpSetConnectTimeOut(tmpl, 8 * 1000 * 1000);
  sceHttpSetRecvTimeOut(tmpl, 8 * 1000 * 1000);
  sceHttpsDisableOption(tmpl, 0x01);
  sceHttpsSetSslCallback(tmpl, accept_cert, NULL);
  return 1;
}

static int post_json(const char *path, const char *token, const char *body) {
  if (!http_ready()) return -1;
  char url[256]; snprintf(url, sizeof url, "%s%s", OMEGA_BASE_URL, path);
  int conn = sceHttpCreateConnectionWithURL(tmpl, url, 0); if (conn < 0) return conn;
  int req = sceHttpCreateRequestWithURL(conn, HTTP_POST, url, strlen(body));
  if (req < 0) { sceHttpDeleteConnection(conn); return req; }
  char auth[760]; snprintf(auth, sizeof auth, "Bearer %s", token);
  sceHttpAddRequestHeader(req, "Authorization", auth, 1);
  sceHttpAddRequestHeader(req, "Content-Type", "application/json", 1);
  sceHttpAddRequestHeader(req, "Accept-Language", i18n_code(), 1);
  int status = -1;
  if (sceHttpSendRequest(req, body, strlen(body)) >= 0) {
    sceHttpGetStatusCode(req, &status);
    char sink[512]; while (sceHttpReadData(req, sink, sizeof sink) > 0) {}
  }
  sceHttpDeleteRequest(req); sceHttpDeleteConnection(conn);
  return status;
}

static int get_json(const char *path, const char *token, char *out, size_t n) {
  out[0] = 0;
  if (!http_ready()) return -1;
  char url[256]; snprintf(url, sizeof url, "%s%s", OMEGA_BASE_URL, path);
  int conn = sceHttpCreateConnectionWithURL(tmpl, url, 0); if (conn < 0) return conn;
  int req = sceHttpCreateRequestWithURL(conn, HTTP_GET, url, 0);
  if (req < 0) { sceHttpDeleteConnection(conn); return req; }
  char auth[760]; snprintf(auth, sizeof auth, "Bearer %s", token);
  sceHttpAddRequestHeader(req, "Authorization", auth, 1);
  sceHttpAddRequestHeader(req, "Accept-Language", i18n_code(), 1);   // titoli delle notifiche nella lingua dell'utente
  int status = -1;
  if (sceHttpSendRequest(req, NULL, 0) >= 0) {
    sceHttpGetStatusCode(req, &status);
    size_t total = 0; int k;
    while (total + 1 < n && (k = sceHttpReadData(req, out + total, n - 1 - total)) > 0) total += (size_t)k;
    out[total] = 0;
  }
  sceHttpDeleteRequest(req); sceHttpDeleteConnection(conn);
  return status;
}

// Un URL qualsiasi (JSON della libreria), seguendo i redirect sulla connessione:
// il template è condiviso con presenza e notifiche.
static pthread_mutex_t http_mx = PTHREAD_MUTEX_INITIALIZER;
static long fetch_url(const char *url, char *buf, size_t max) {
  pthread_mutex_lock(&http_mx);
  long got = -1;
  if (http_ready()) {
    int conn = sceHttpCreateConnectionWithURL(tmpl, url, 0);
    if (conn >= 0) {
      sceHttpSetAutoRedirect(conn, 1);
      int req = sceHttpCreateRequestWithURL(conn, HTTP_GET, url, 0);
      if (req >= 0) {
        sceHttpSetAutoRedirect(req, 1);
        int status = 0;
        if (sceHttpSendRequest(req, NULL, 0) >= 0 && sceHttpGetStatusCode(req, &status) >= 0 && status == 200) {
          size_t total = 0; int k;
          while (total < max && (k = sceHttpReadData(req, buf + total, max - total)) > 0) total += (size_t)k;
          got = (long)total;
        }
        sceHttpDeleteRequest(req);
      }
      sceHttpDeleteConnection(conn);
    }
  }
  pthread_mutex_unlock(&http_mx);
  return got;
}

// valore di "key":"..." in un JSON piatto
static int json_get(const char *js, const char *key, char *out, size_t n) {
  char pat[64]; snprintf(pat, sizeof pat, "\"%s\":\"", key);
  const char *p = strstr(js, pat); out[0] = 0; if (!p) return -1;
  p += strlen(pat); size_t i = 0;
  while (*p && *p != '"' && i + 1 < n) { if (*p == '\\' && p[1]) p++; out[i++] = *p++; }
  out[i] = 0; return 0;
}
static int read_small(const char *path, char *buf, size_t n) {
  int fd = open(path, O_RDONLY); if (fd < 0) return -1;
  int r = (int)read(fd, buf, n - 1); close(fd);
  if (r < 0) r = 0; buf[r] = 0; return r;
}

// nome del gioco da appmeta: param.json (PS5) o param.sfo (PS4)
static void game_name(const char *tid, char *out, size_t n) {
  static char buf[64 * 1024];
  char p[160]; out[0] = 0;
  snprintf(p, sizeof p, "/user/appmeta/%s/param.json", tid);
  if (read_small(p, buf, sizeof buf) > 0 && json_get(buf, "titleName", out, n) == 0 && out[0]) return;
  snprintf(p, sizeof p, "/user/appmeta/%s/param.sfo", tid);
  int len = read_small(p, buf, sizeof buf);
  if (len > 20 && !memcmp(buf, "\0PSF", 4)) {
    uint32_t keyt = *(uint32_t *)(buf + 8), datat = *(uint32_t *)(buf + 12), cnt = *(uint32_t *)(buf + 16);
    for (uint32_t i = 0; i < cnt && 20 + i * 16 + 16 <= (uint32_t)len; i++) {
      const unsigned char *e = (const unsigned char *)buf + 20 + i * 16;
      uint16_t ko = *(const uint16_t *)e; uint32_t dl = *(const uint32_t *)(e + 4), doff = *(const uint32_t *)(e + 12);
      if (keyt + ko >= (uint32_t)len || datat + doff + dl > (uint32_t)len) continue;
      if (!strcmp(buf + keyt + ko, "TITLE")) { snprintf(out, n, "%.*s", (int)dl, buf + datat + doff); return; }
    }
  }
  snprintf(out, n, "%s", tid);
}

static void json_esc(char *dst, size_t n, const char *src) {
  size_t o = 0;
  for (; *src && o + 3 < n; src++) {
    if (*src == '"' || *src == '\\') dst[o++] = '\\';
    if ((unsigned char)*src >= 0x20) dst[o++] = *src;
  }
  dst[o] = 0;
}

// ----------------------------------------------------- notifiche di sistema --
typedef struct {
  int type, reqId, priority, msgId, targetId, userId, unk1, unk2, appId, errorNum, unk3;
  unsigned char useIconImageUri;
  char message[1024], iconUri[1024], unk[1024];
} NotifyReq;
int sceKernelSendNotificationRequest(int device, NotifyReq *req, size_t size, int blocking);

static void sys_notify(const char *msg) {
  NotifyReq r;   // sullo stack: la chiamano anche i thread del lettore
  memset(&r, 0, sizeof r);
  r.type = 0; r.targetId = -1;
  snprintf(r.message, sizeof r.message, "%s", msg);
  sceKernelSendNotificationRequest(0, &r, sizeof r, 0);
}

static long notif_last; static int notif_baseline;
static int friends_online = -1;          // dall'ultima sincronizzazione, per il telecomando e il plugin
static char sync_buf[64 * 1024];

static void notify_tick(const char *token, int ui_fresh) {
  if (ui_fresh) { notif_baseline = 0; return; }        // la UI le mostra come toast
  char path[96]; snprintf(path, sizeof path, OMEGA_API "/sync?since=%ld", notif_baseline ? notif_last : 0L);
  if (get_json(path, token, sync_buf, sizeof sync_buf) != 200) return;
  // amici connessi: chi ha una presenza diversa da offline
  JVal *sj = json_parse(sync_buf);
  if (sj) {
    int n = 0;
    JFOR(f, jget(sj, "friends")) { const char *st = jstr(jget(f, "presence"), "status", "offline"); if (strcmp(st, "offline")) n++; }
    friends_online = n; json_free(sj);
  }
  char tmp[32];
  if (json_get(sync_buf, "last_notification_id", tmp, sizeof tmp) == 0) {
    long last = atol(tmp);
    if (!notif_baseline) { notif_last = last; notif_baseline = 1; return; }   // le precedenti non si ripetono
    // per ogni notifica nuova: "title", più "body" per i messaggi
    const char *p = strstr(sync_buf, "\"notifications\":[");
    int shown = 0;
    while (p && (p = strstr(p, "\"notification_id\":\"")) && shown < 4) {
      const char *end = strchr(p, '}'); if (!end) break;
      char item[1200]; size_t len = (size_t)(end - p); if (len >= sizeof item) len = sizeof item - 1;
      memcpy(item, p, len); item[len] = 0;
      char title[300] = "", body[400] = "", type[32] = "", msg[800];
      json_get(item, "title", title, sizeof title); json_get(item, "body", body, sizeof body); json_get(item, "type", type, sizeof type);
      if (!strcmp(type, "message")) snprintf(msg, sizeof msg, _("Omega \xC2\xB7 %s: %s"), title, body);
      else snprintf(msg, sizeof msg, _("Omega \xC2\xB7 %s"), title);
      sys_notify(msg); shown++;
      p = end;
    }
    if (shown) lg("notifiche di sistema: %d", shown);
    notif_last = last;
  }
}

// ----------------------------------------------------------------- presenza --
static char host_tid[16];      // titolo che ospita la UI: non va riportato come gioco
static int reported_game;

// token della sessione salvata dalla UI; 0 se non c'è nessuno collegato
static int session_token(char *token, size_t n) {
  char sess[2048];
  return read_small(OMEGA_SESSION, sess, sizeof sess) > 0 && json_get(sess, "token", token, n) == 0 && token[0];
}

static int ui_in_foreground(void) {
  struct stat st;
  return stat(OMEGA_UI_ACTIVE, &st) == 0 && time(NULL) - st.st_mtime < UI_FRESH_S;
}

static void notify_from_loop(void) {
  char token[700];
  if (session_token(token, sizeof token)) notify_tick(token, ui_in_foreground());
}

static void presence_tick(void) {
  i18n_init(sys_lang());     // la lingua si può cambiare dalla UI in qualunque momento
  char token[700];
  if (!session_token(token, sizeof token)) return;
  int ui_fresh = ui_in_foreground();
  char tid[64] = {0};
  int appId = sceSystemServiceGetAppIdOfRunningBigApp();
  if (appId >= 0) sceSystemServiceGetAppTitleId(appId, tid);
  if (ui_fresh) {
    if (tid[0]) snprintf(host_tid, sizeof host_tid, "%s", tid);
    reported_game = 0;
    return;
  }
  int game = tid[0] && strncmp(tid, "NPXS", 4) != 0 && strcmp(tid, host_tid) != 0;
  if (game) {
    char name[128], esc[260], body[400];
    game_name(tid, name, sizeof name); json_esc(esc, sizeof esc, name);
    snprintf(body, sizeof body, "{\"status\":\"online\",\"game_id\":\"%s\",\"game_name\":\"%s\"}", tid, esc);
    int rc = post_json(OMEGA_API "/presence", token, body);
    if (!reported_game) lg("presenza: gioco %s (%s) -> %d", tid, name, rc);
    reported_game = 1;
  } else if (reported_game) {
    int rc = post_json(OMEGA_API "/presence", token, "{\"status\":\"online\"}");
    lg("presenza: nessun gioco -> %d", rc);
    reported_game = 0;
  }
}

// Brano nuovo: se si sta giocando lo si dice con una notifica di sistema
// (nella UI lo mostra già il mini lettore).
static void on_track(const char *title, const char *artist) {
  if (!title || !title[0] || ui_in_foreground() || is_home()) return;
  char msg[600];
  if (artist && artist[0]) snprintf(msg, sizeof msg, "\xE2\x99\xAA %s \xE2\x80\x94 %s", title, artist);
  else snprintf(msg, sizeof msg, "\xE2\x99\xAA %s", title);
  sys_notify(msg);
}

// /v1/system per il telecomando: cosa sta girando e quanto scalda
int sceKernelGetCpuTemperature(int *t);
int sceKernelGetCurrentFanDuty(int *unk, int *duty);
static void system_json(char *out, size_t n) {
  char tid[64] = "", name[128] = "", esc[260] = "";
  int appId = sceSystemServiceGetAppIdOfRunningBigApp();
  if (appId >= 0 && sceSystemServiceGetAppTitleId(appId, tid) == 0 && strncmp(tid, "NPXS", 4) && strcmp(tid, host_tid) && !ui_in_foreground()) {
    game_name(tid, name, sizeof name); json_esc(esc, sizeof esc, name);
  }
  int t = -1, unk = 0, fan = -1;
  if (sceKernelGetCpuTemperature(&t) != 0) t = -1;
  if (sceKernelGetCurrentFanDuty(&unk, &fan) != 0) fan = -1;
  snprintf(out, n, "{\"game\":\"%s\",\"title_id\":\"%s\",\"cpu_t\":%d,\"fan\":%d,\"friends_online\":%d,\"lang\":\"%s\"}", esc, esc[0] ? tid : "", t, fan, friends_online, i18n_code());
}

// La soglia della ventola si perde a ogni riavvio: se l'utente ne ha scelta
// una in Omega, la si riapplica (stesso comando di etaHEN, 55-80 °C).
static void fan_restore(void) {
  char b[16];
  if (read_small(FAN_FILE, b, sizeof b) <= 0) return;
  int t = atoi(b); if (t < 55 || t > 80) return;
  int fd = open("/dev/icc_fan", O_RDONLY, 0);
  if (fd < 0) { lg("ventola: /dev/icc_fan non disponibile"); return; }
  char data[10] = { 0, 0, 0, 0, 0, (char)t, 0, 0, 0, 0 };
  int rc = ioctl(fd, 0xC01C8F07, data); close(fd);
  lg("ventola: soglia %d C -> %d", t, rc);
}

// Stesso controllo della UI (main.c): chi parte per primo rinomina la cartella.
static void migrate_data_dir(void) {
  struct stat st;
  if (stat(OMEGA_DIR_LEGACY, &st) == 0 && stat(OMEGA_DIR, &st) != 0 && rename(OMEGA_DIR_LEGACY, OMEGA_DIR) == 0)
    lg("cartella dati spostata in %s", OMEGA_DIR);
}

int main(void);

// Un crash finisce nel log con il segnale, l'indirizzo e l'ultima richiesta servita:
// sottraendo l'indirizzo di main si ritrova la riga nel file .elf.
static void on_crash(int sig, siginfo_t *si, void *ucv) {
  unsigned long rip = 0;
#if defined(PS5) && defined(__x86_64__)
  rip = (unsigned long)((ucontext_t *)ucv)->uc_mcontext.mc_rip;
#else
  (void)ucv;
#endif
  lg("CRASH segnale %d, indirizzo %p, codice %#lx (main %p), ultima richiesta: %s", sig, si ? si->si_addr : NULL, rip, (void *)main, ctl_last_request());
  _exit(128 + sig);
}
static void guard_signals(void) {
  // il telefono o il browser chiudono spesso la connessione a metà risposta:
  // senza questo la scrittura sul socket chiuso (SIGPIPE) uccide il servizio
  signal(SIGPIPE, SIG_IGN);
  struct sigaction sa; memset(&sa, 0, sizeof sa);
  sa.sa_sigaction = on_crash; sa.sa_flags = SA_SIGINFO;
  int sigs[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT };
  for (unsigned i = 0; i < sizeof sigs / sizeof *sigs; i++) sigaction(sigs[i], &sa, NULL);
}

int main(void) {
  guard_signals();
  migrate_data_dir();
  lg("==== omega_redirect avvio ====");
  { int v = sys_lang(); i18n_init(v); lg("lingua: %s (valore di sistema %d)", i18n_code(), v); }
  // Una sola copia: più caricatori (OnionHEN, etaHEN, Payload Manager, autoloader)
  // possono avviare il demone. La porta di controllo fa da lucchetto: se è già
  // occupata c'è un altro omega_redirect e questo si ritira subito.
  ctl_on_system(system_json);
  if (ctl_start(OMEGA_CTL_PORT, OMEGA_DIR) != 0) { lg("un'altra copia di omega_redirect è già attiva: esco"); return 0; }
  fan_restore();
  // il lettore parte subito: non dipende da websrv
  player_init(PLAYER_STATE);
  lib_init(LIBRARY_FILE, fetch_url);
  player_on_track(on_track);
  sleep(BOOT_DELAY_S);

  // avviato a console accesa (dalla UI, dopo la configurazione dell'HEN): Omega è già aperta
  if (ui_in_foreground()) lg("Omega è già aperta: nessun avvio iniziale");
  else if (home_mode_on()) {
    for (int i = 0; i < BOOT_TRIES; i++) {
      if (launch_omega() == 0) { lg("avvio iniziale: Omega lanciata"); break; }
      lg("websrv non pronto (tentativo %d)", i + 1); sleep(3);
    }
  } else lg("Omega non è impostata come Home: nessun avvio automatico della UI");

  int last_home = 1; time_t last = time(NULL), last_presence = 0, last_notify = 0;
  for (;;) {
    int home = is_home();
    time_t now = time(NULL);
    // si rilancia solo al ritorno alla Home, non finché ci si resta
    if (home && !last_home && (now - last) > RELAUNCH_COOLDOWN_S && home_mode_on()) {
      if (launch_omega() == 0) { lg("redirect: tornato alla Home -> Omega"); last = now; }
    }
    last_home = home;
    if (now - last_presence >= PRESENCE_EVERY_S) { last_presence = now; presence_tick(); }
    if (now - last_notify >= NOTIFY_EVERY_S) { last_notify = now; notify_from_loop(); }
    sleep(LOOP_S);
  }
  return 0;
}
