// omega_redirect — spegnimento, riavvio e modalità riposo.
//
// Il sistema non avvisa un payload prima di spegnersi o addormentarsi, e i
// thread che restano a metà (porte audio aperte, microfono, connessioni,
// giochi montati da un disco USB) sono la causa tipica di console che si
// bloccano spegnendosi o che vanno in panic al risveglio. Si legge quindi lo
// stato del gestore di sistema dal flag del kernel "SceSystemStateMgrInfo"
// (stato nei 16 bit bassi: 100 spegnimento in corso, 300 sospensione in corso,
// 500 standby, 1000 acceso) e "SceSystemStateMgrStatus" (bit 0x200000: la UI
// di sistema sta chiudendo, arriva prima). Stesso metodo e stessi valori di
// drakmor/ShadowMountPlus e di OnionHEN (GPL-3), provati sui firmware recenti.
//
// Riposo: si ferma tutto ciò che usa hardware, rete o disco e si smontano i
// giochi montati da Omega (al risveglio il disco USB può non esserci più).
// Spegnimento: lo stesso, poi il main esce prima che arrivi il SIGKILL.
#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "power.h"

#define OMEGA_DIR "/data/Omega"
#define EXT_MOUNTED OMEGA_DIR "/ext-mounted.txt"     // scritto dalla UI (drives.c): un title id per riga
#define PDL_PAUSED  OMEGA_DIR "/gameupd-paused.txt"   // download di PatchDL fermati da noi, da riprendere
#define PDL_PORT 12880

#define ST_SHUTDOWN 100u
#define ST_SUSPEND  300u
#define ST_STANDBY  500u
#define ST_WORKING  1000u
#define STATUS_SHELLUI_SHUTDOWN 0x200000ULL
#define RESUME_GRACE_S 5          // al risveglio rete e dischi tornano con calma

int sceKernelOpenEventFlag(intptr_t *ef, const char *name);
int sceKernelPollEventFlag(intptr_t ef, uint64_t bits, unsigned int wait_mode, uint64_t *result);
int sceKernelCloseEventFlag(intptr_t ef);

// dagli altri moduli del demone
void lg(const char *fmt, ...);
void voice_power(int sleeping);     // voice.c: chiude sessione, microfono e porta audio
void player_power(int sleeping);    // player.c: ferma l'uscita audio e la porta della musica
void ctl_power(int sleeping);       // ctl.c: chiude e riapre il server di controllo

static volatile int sleeping, stopping, prepared;
static time_t prepared_at;
#define PREPARE_TIMEOUT_S 90     // la UI ha chiesto di spegnere ma la console è rimasta accesa
static pthread_mutex_t mx = PTHREAD_MUTEX_INITIALIZER;

int power_sleeping(void) { return sleeping || prepared; }
int power_stopping(void) { return stopping; }

// ------------------------------------------------------------ montaggi --
int unmount(const char *dir, int flags);
int sceSystemServiceGetAppIdOfRunningBigApp(void);
int sceSystemServiceGetAppTitleId(int appId, char *titleId);
// keep_running: in riposo il gioco in primo piano è solo sospeso e al
// risveglio riparte dai suoi file, quindi il suo montaggio resta.
static void unmount_games(int keep_running) {
  char running[16] = "";
  if (keep_running) { int id = sceSystemServiceGetAppIdOfRunningBigApp(); if (id >= 0) sceSystemServiceGetAppTitleId(id, running); }
  FILE *f = fopen(EXT_MOUNTED, "r"); if (!f) return;
  char ids[64][16]; int n = 0; char line[64];
  while (n < 64 && fgets(line, sizeof line, f)) {
    line[strcspn(line, "\r\n")] = 0;
    if (strlen(line) == 9) snprintf(ids[n++], sizeof ids[0], "%s", line);
  }
  fclose(f);
  int done = 0;
  for (int i = n - 1; i >= 0; i--) {    // al contrario di come sono stati montati, come fa il sistema
    if (running[0] && !strcmp(running, ids[i])) continue;
    char p[64]; snprintf(p, sizeof p, "/system_ex/app/%s", ids[i]);
    if (unmount(p, 0) == 0 || unmount(p, MNT_FORCE) == 0) done++;
  }
  if (n) lg("alimentazione: smontati %d giochi da disco esterno su %d", done, n);
  // l'elenco resta: al prossimo avvio del gioco la UI lo rimonta
}

// --------------------------------------------------------------- PatchDL --
// Richiesta HTTP minima a PatchDL in locale, con timeout brevi: allo
// spegnimento non si aspetta nessuno.
static int pdl(const char *method, const char *path, char *out, size_t n) {
  if (out && n) out[0] = 0;
  int s = socket(AF_INET, SOCK_STREAM, 0); if (s < 0) return -1;
  struct timeval tv = { 1, 0 };
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv); setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
  struct sockaddr_in a; memset(&a, 0, sizeof a);
  a.sin_family = AF_INET; a.sin_port = htons(PDL_PORT); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (connect(s, (struct sockaddr *)&a, sizeof a) != 0) { close(s); return -1; }
  char req[300];
  int post = !strcmp(method, "POST");
  int rl = snprintf(req, sizeof req, "%s %s HTTP/1.0\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\nContent-Length: %d\r\n\r\n%s",
                    method, path, post ? 2 : 0, post ? "{}" : "");
  if (send(s, req, (size_t)rl, 0) != rl) { close(s); return -1; }
  static char buf[64 * 1024]; size_t got = 0; ssize_t k;
  while (got + 1 < sizeof buf && (k = recv(s, buf + got, sizeof buf - 1 - got, 0)) > 0) got += (size_t)k;
  close(s); buf[got] = 0;
  int st = -1; if (sscanf(buf, "HTTP/%*s %d", &st) != 1) return -1;
  char *b = strstr(buf, "\r\n\r\n"); if (out && n && b) snprintf(out, n, "%s", b + 4);
  return st;
}

// Download attivi messi in pausa (ogni pezzo finito resta su disco): così
// PatchDL non scrive in /data mentre la console si spegne o dorme.
static void pause_downloads(void) {
  static char dl[32 * 1024];
  if (pdl("GET", "/api/downloads", dl, sizeof dl) != 200) return;
  FILE *f = fopen(PDL_PAUSED, "a");
  int n = 0;
  for (char *p = dl; (p = strstr(p, "\"title_id\":\"")); ) {
    p += 12; char tid[16]; int i = 0;
    while (*p && *p != '"' && i < 15) tid[i++] = *p++;
    tid[i] = 0;
    char *obj_end = strchr(p, '}'); if (!obj_end) break;
    char *st = strstr(p, "\"state\":\"");
    if (st && st < obj_end && (!strncmp(st + 9, "active", 6) || !strncmp(st + 9, "queued", 6))) {
      char path[80]; snprintf(path, sizeof path, "/api/titles/%s/pause", tid);
      if (pdl("POST", path, NULL, 0) == 200) { if (f) fprintf(f, "%s\n", tid); n++; }
    }
    p = obj_end;
  }
  if (f) { fflush(f); fsync(fileno(f)); fclose(f); }
  if (n) lg("alimentazione: %d download di aggiornamenti in pausa", n);
}
static void resume_downloads(void) {
  FILE *f = fopen(PDL_PAUSED, "r"); if (!f) return;
  char line[64]; int n = 0;
  while (fgets(line, sizeof line, f)) {
    line[strcspn(line, "\r\n")] = 0; if (!line[0]) continue;
    char path[80]; snprintf(path, sizeof path, "/api/titles/%.20s/download", line);
    if (pdl("POST", path, NULL, 0) >= 200) n++;
  }
  fclose(f); unlink(PDL_PAUSED);
  if (n) lg("alimentazione: %d download di aggiornamenti ripresi", n);
}

// ------------------------------------------------------------ transizioni --
// close_ctl 0 solo per la richiesta della UI: deve poter rispondere e, se la
// console non si spegne, mandare resume_power sulla stessa porta.
static void quiesce(const char *why, int shutting_down, int close_ctl) {
  lg("alimentazione: %s, fermo audio, rete e dischi", why);
  voice_power(1);
  player_power(1);
  if (close_ctl) ctl_power(1);
  pause_downloads();
  unmount_games(!shutting_down);
  sync();
}
static void wake(void) {
  lg("alimentazione: di nuovo acceso");
  ctl_power(0);
  player_power(0);
  voice_power(0);
  resume_downloads();
}

void power_prepare(void) {
  pthread_mutex_lock(&mx);
  if (!sleeping && !prepared) { prepared = 1; prepared_at = time(NULL); quiesce("richiesta dalla UI", 1, 0); }
  pthread_mutex_unlock(&mx);
}
void power_resume(void) {
  pthread_mutex_lock(&mx);
  if (prepared && !sleeping && !stopping) { prepared = 0; wake(); }
  pthread_mutex_unlock(&mx);
}

static int open_flag(intptr_t *ef, const char *name) {
  if (*ef >= 0) return 1;
  if (sceKernelOpenEventFlag(ef, name) < 0) { *ef = -1; return 0; }
  return 1;
}
static int poll_flag(intptr_t *ef, uint64_t *pattern) {
  if (*ef < 0) return 0;
  if (sceKernelPollEventFlag(*ef, UINT64_MAX, 2 /* OR */, pattern) < 0) { sceKernelCloseEventFlag(*ef); *ef = -1; return 0; }
  return 1;
}

static void *watch(void *arg) {
  (void)arg;
  intptr_t info = -1, status = -1;
  unsigned last = 0; int shellui_down = 0, logged_missing = 0;
  time_t woke_at = 0;
  for (;;) {
    int have_info = open_flag(&info, "SceSystemStateMgrInfo");
    open_flag(&status, "SceSystemStateMgrStatus");
    if (!have_info && !logged_missing++) lg("alimentazione: flag di sistema non disponibile, niente gestione del riposo");
    uint64_t p = 0;
    if (poll_flag(&info, &p)) {
      unsigned st = (unsigned)(p & 0xFFFF);
      if (st != last) {
        lg("alimentazione: stato %u -> %u", last, st);
        pthread_mutex_lock(&mx);
        if (st == ST_SHUTDOWN) {
          if (!sleeping && !prepared) quiesce("spegnimento in corso", 1, 1);
          else { if (prepared && !sleeping) ctl_power(1); unmount_games(0); sync(); }   // in riposo era rimasto montato il gioco in primo piano
          sleeping = 1; stopping = 1;
        } else if (st == ST_SUSPEND || st == ST_STANDBY) {
          if (!sleeping && !prepared) quiesce(st == ST_SUSPEND ? "sospensione in corso" : "standby", 0, 1);
          else if (prepared && !sleeping) ctl_power(1);
          sleeping = 1;
        } else if (st == ST_WORKING && sleeping && !stopping) {
          woke_at = time(NULL);          // il risveglio vero dopo qualche secondo
        }
        pthread_mutex_unlock(&mx);
        last = st;
      }
    }
    uint64_t s = 0;
    if (poll_flag(&status, &s)) {
      int down = (s & STATUS_SHELLUI_SHUTDOWN) != 0;
      if (down && !shellui_down) {
        pthread_mutex_lock(&mx);
        if (!sleeping && !prepared) quiesce("la UI di sistema si chiude", 0, 1);
        else if (prepared && !sleeping) ctl_power(1);
        sleeping = 1;
        pthread_mutex_unlock(&mx);
      }
      shellui_down = down;
    }
    if (prepared && !sleeping && time(NULL) - prepared_at > PREPARE_TIMEOUT_S) {
      lg("alimentazione: la console non si è spenta, riprendo");
      power_resume();
    }
    if (woke_at && time(NULL) - woke_at >= RESUME_GRACE_S) {
      woke_at = 0;
      pthread_mutex_lock(&mx);
      if (sleeping && !stopping && last == ST_WORKING && !shellui_down) { sleeping = 0; prepared = 0; wake(); }
      pthread_mutex_unlock(&mx);
    }
    usleep(sleeping ? 500 * 1000 : 100 * 1000);
  }
  return NULL;
}

void power_start(void) {
  pthread_t t;
  pthread_attr_t at; pthread_attr_init(&at);
  pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
  if (pthread_create(&t, &at, watch, NULL) != 0) lg("alimentazione: thread non avviato");
  pthread_attr_destroy(&at);
}
