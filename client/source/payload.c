// Omega UI — payload ELF di Payload Manager. Compaiono in home accanto ai
// giochi e partono in background, con Omega che resta aperta: l'ELF si manda a
// elfldr sulla porta 9021 e, se non risponde, lo avvia websrv come demone.
#include "app.h"
#include <dirent.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef PS5
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#define ELFLDR_PORT 9021
#endif

// i componenti di Omega stessa non si elencano
static int own(const char *name) { return !strncmp(name, "Omega", 5) || !strncmp(name, "omega_", 6) || !strncmp(name, "OMGA", 4); }

static int has_suffix(const char *s, const char *suf) {
  size_t a = strlen(s), b = strlen(suf);
  return a >= b && !strcmp(s + a - b, suf);
}

// nome da mostrare: "name" del .elf.json di Payload Manager, altrimenti la cartella
static void sidecar_name(const char *elf, const char *fallback, char *out, size_t n) {
  char p[720]; snprintf(p, sizeof p, has_suffix(elf, ".omega.json") ? "%s" : "%s.json", elf);
  snprintf(out, n, "%s", fallback);
  char *buf = file_read(p, 8191, NULL);
  if (!buf) return;
  JVal *j = json_parse(buf);
  free(buf);
  const char *nm = jstr(j, "name", "");
  if (nm[0]) snprintf(out, n, "%s", nm);
  json_free(j);
}

int payload_scan(AppEntry *list, int n, int max) {
  DIR *dp = opendir(OMEGA_PLD_ROOT);
  if (!dp) return n;
  struct dirent *e;
  while ((e = readdir(dp)) && n < max) {
    if (e->d_name[0] == '.' || own(e->d_name)) continue;
    char dir[400]; snprintf(dir, sizeof dir, "%s/%s", OMEGA_PLD_ROOT, e->d_name);
    DIR *d2 = opendir(dir); if (!d2) continue;
    struct dirent *f; char elf[600] = "";
    while ((f = readdir(d2))) if (has_suffix(f->d_name, ".elf")) { snprintf(elf, sizeof elf, "%s/%s", dir, f->d_name); break; }
    closedir(d2);
    if (!elf[0]) continue;
    AppEntry *a = &list[n]; memset(a, 0, sizeof *a);
    a->pld = 1;
    snprintf(a->dir, sizeof a->dir, "%s", elf);
    sidecar_name(elf, e->d_name, a->name, sizeof a->name);
    snprintf(a->tid, sizeof a->tid, "PL%08x", fnv1a(e->d_name));
    snprintf(a->sub, sizeof a->sub, "%s", N_("Payload \xC2\xB7 in background"));   // tradotto quando si disegna
    a->avg = g_theme_base;
    n++;
  }
  closedir(dp);
  // OnionHEN / etaHEN 2: cartella piatta di .elf (lì li mette anche lo Store)
  char flat[300];
  if (hen_payload_dir(flat, sizeof flat) && (dp = opendir(flat))) {
    while ((e = readdir(dp)) && n < max) {
      if (e->d_name[0] == '.' || own(e->d_name) || !has_suffix(e->d_name, ".elf")) continue;
      AppEntry *a = &list[n]; memset(a, 0, sizeof *a);
      a->pld = 1;
      snprintf(a->dir, sizeof a->dir, "%s/%s", flat, e->d_name);
      char base[96]; snprintf(base, sizeof base, "%.*s", (int)(strlen(e->d_name) - 4), e->d_name);
      char side[700]; snprintf(side, sizeof side, "%s.omega.json", a->dir);   // nome dato dallo Store
      sidecar_name(side, base, a->name, sizeof a->name);
      snprintf(a->tid, sizeof a->tid, "PL%08x", fnv1a(e->d_name));
      snprintf(a->sub, sizeof a->sub, "%s", N_("Payload \xC2\xB7 in background"));
      a->avg = g_theme_base;
      n++;
    }
    closedir(dp);
  }
  return n;
}

#ifdef PS5
static int send_elfldr(const char *elf) {
  FILE *f = fopen(elf, "rb");
  if (!f) return -1;
  int s = socket(AF_INET, SOCK_STREAM, 0);
  if (s < 0) { fclose(f); return -1; }
  struct timeval tv = { 10, 0 }; setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);   // elfldr bloccato: non si resta appesi
  struct sockaddr_in sa; memset(&sa, 0, sizeof sa);
  sa.sin_family = AF_INET; sa.sin_port = htons(ELFLDR_PORT); sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  int rc = -1;
  if (connect(s, (struct sockaddr *)&sa, sizeof sa) == 0) {
    char *buf = malloc(64 * 1024); size_t k; rc = buf ? 0 : -1;   // dal thread degli aggiornamenti e dal principale
    while (buf && (k = fread(buf, 1, 64 * 1024, f)) > 0) {
      for (size_t off = 0; off < k;) {
        ssize_t w = send(s, buf + off, k - off, 0);
        if (w <= 0) { rc = -1; break; }
        off += (size_t)w;
      }
      if (rc) break;
    }
    free(buf);
  }
  close(s); fclose(f);
  return rc;
}
#endif

// Il servizio di Omega: elfldr se c'è, altrimenti websrv (hbldr daemon=1).
int payload_run_service(const char *elf, char *err, size_t en) {
  struct stat st;
  if (stat(elf, &st) != 0) { snprintf(err, en, "%s", _("File del payload non trovato")); return -1; }
#ifdef PS5
  if (send_elfldr(elf) == 0) { omega_log("servizio %s inviato a elfldr :%d", elf, ELFLDR_PORT); return 0; }
  return payload_run(elf, err, en);   // websrv come ripiego
#else
  omega_log("(desktop) avvio simulato del servizio %s", elf);
  return 0;
#endif
}

int payload_run(const char *elf, char *err, size_t en) {
  struct stat st;
  if (stat(elf, &st) != 0) { snprintf(err, en, "%s", _("File del payload non trovato")); return -1; }
#ifdef PS5
  if (send_elfldr(elf) == 0) { omega_log("payload %s inviato a elfldr :%d", elf, ELFLDR_PORT); return 0; }
  char cwd[400]; snprintf(cwd, sizeof cwd, "%s", elf);
  char *slash = strrchr(cwd, '/'); if (slash) *slash = 0;
  char ep[1200], ec[1200], url[2600];
  url_encode(ep, sizeof ep, elf, "-_.~/"); url_encode(ec, sizeof ec, cwd, "-_.~/");
  snprintf(url, sizeof url, WEBSRV_URL "/hbldr?pipe=0&daemon=1&path=%s&cwd=%s", ep, ec);
  unsigned char b[16];
  if (omega_url_peek(url, b, sizeof b) >= 0) { omega_log("payload %s avviato via websrv", elf); return 0; }
  snprintf(err, en, "%s", _("Nessun loader disponibile: serve elfldr (porta 9021) o websrv (porta 8080)"));
  return -1;
#else
  omega_log("(desktop) avvio simulato del payload %s", elf);
  return 0;
#endif
}

int payload_remove(const char *elf) {
  char flat[300];
  if (hen_payload_dir(flat, sizeof flat) && !strncmp(elf, flat, strlen(flat)) && elf[strlen(flat)] == '/') {
    char p[720];
    snprintf(p, sizeof p, "%s.omega.json", elf); unlink(p);
    snprintf(p, sizeof p, "%s.auto_start", elf); unlink(p);
    snprintf(p, sizeof p, "%s.json", elf); unlink(p);
    return unlink(elf);
  }
  char dir[400]; snprintf(dir, sizeof dir, "%s", elf);
  char *slash = strrchr(dir, '/');
  if (!slash) return -1;
  *slash = 0;
  return hb_remove(dir, OMEGA_PLD_ROOT);
}
