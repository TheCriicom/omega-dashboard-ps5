// Omega Installer — payload che installa (o aggiorna) Omega sulla console.
//
// Scarica dal server Omega il manifest firmato degli aggiornamenti, ne verifica
// la firma Ed25519 e lo SHA-256 di ogni file, poi installa:
//   OmegaUI.elf + homebrew.js + icon0.png  → /data/homebrew/OmegaUI/
//   omega_redirect.elf, OMGA00001.elf      → /data/homebrew/OmegaUI/payloads/
// Il servizio e il plugin NON si attivano qui: al primo avvio Omega riconosce il
// jailbreak (OnionHEN, etaHEN, Payload Manager...) e, dopo l'OK dell'utente,
// li installa e li abilita nel caricatore giusto (omega-ui-src/source/hen.c).
// e chiede a websrv di avviare Omega. L'avanzamento compare nelle notifiche
// di sistema. Si invia con qualunque ELF loader (websrv /elfldr, porta 9021).
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "monocypher-ed25519.h"

#ifndef OMEGA_BASE_URL
#define OMEGA_BASE_URL "https://play.omegasuite.it"
#endif
#define UPDATES     OMEGA_BASE_URL "/updates"
// Ripiego senza SceHttp/SceSsl: http semplice sulla porta 80 (Caddy serve
// /updates e /download anche così). L'integrità non cambia: firma e sha256.
#define RAW_HOST    "play.omegasuite.it"
#define RAW_IP      "217.160.179.162"     // se il DNS della console non risponde
#define HB_DIR      "/data/homebrew/OmegaUI"
#define PLD_DIR     "/data/pldmgr/payloads/OmegaRedirect"
#define DATA_DIR    "/data/Omega"           // condivisa con la UI e il demone
#define DATA_DIR_LEGACY "/data/OmegaPSNLab"  // nome delle prime versioni, migrato all'avvio
#define LOG_FILE    DATA_DIR "/omega-installer.log"

static const char PUBKEY_HEX[] = "231ab09e789dc09d0248a3410dcb3d474cea89c112ddeec790a39689db9076f2";

// ---------------------------------------------------------------------- log --
static void lg(const char *fmt, ...) {
  int fd = open(LOG_FILE, O_WRONLY | O_CREAT | O_APPEND, 0666);
  if (fd < 0) return;
  char b[512];
  int n = snprintf(b, sizeof b, "[%lu] ", (unsigned long)time(NULL));
  va_list ap; va_start(ap, fmt); n += vsnprintf(b + n, sizeof b - n, fmt, ap); va_end(ap);
  if (n < (int)sizeof b - 1) b[n++] = '\n';
  write(fd, b, (size_t)n);
  close(fd);
}

typedef struct {
  int type, reqId, priority, msgId, targetId, userId, unk1, unk2, appId, errorNum, unk3;
  unsigned char useIconImageUri;
  char message[1024], iconUri[1024], unk[1024];
} NotifyReq;
int sceKernelSendNotificationRequest(int device, NotifyReq *req, size_t size, int blocking);

// ---------------------------------------------------------------- lingua --
// Traduzioni (i18n_data.c, generato da omega-ui-src/tools/i18n-gen.mjs con gli
// stessi cataloghi della UI). libSceSystemService non è tra le librerie
// dell'installer: sceSystemServiceParamGetInt si risolve a runtime, come fa la
// UI con libSceAudioIn.
const char *i18n_tr(const char *msgid) __attribute__((format_arg(1)));
void i18n_init(int sys_lang);
const char *i18n_code(void);
#define _(s) i18n_tr(s)
int sceKernelLoadStartModule(const char *path, size_t argc, const void *argv, uint32_t flags, void *opt, int *res);
intptr_t kernel_dynlib_dlsym(int pid, uint32_t handle, const char *sym);

static int sys_lang(void) {      // valore di SCE_SYSTEM_SERVICE_PARAM_ID_LANG (1), -1 se non disponibile
  int res = 0, v = -1;
  int h = sceKernelLoadStartModule("/system/common/lib/libSceSystemService.sprx", 0, NULL, 0, NULL, &res);
  if (h < 0) { lg("libSceSystemService non caricata 0x%x", h); return -1; }
  int (*get)(int, int *) = (int (*)(int, int *))kernel_dynlib_dlsym(getpid(), (uint32_t)h, "sceSystemServiceParamGetInt");
  if (!get) return -1;
  int rc = get(1, &v);
  lg("lingua di sistema: rc=0x%x valore=%d", rc, v);
  return rc == 0 ? v : -1;
}

static void notify(const char *msg) {
  static NotifyReq r;
  memset(&r, 0, sizeof r);
  r.targetId = -1;
  snprintf(r.message, sizeof r.message, "%s", msg);
  sceKernelSendNotificationRequest(0, &r, sizeof r, 0);
  lg("%s", msg);
}

// ------------------------------------------------------------------ sha-256 --
typedef struct { uint32_t s[8]; uint64_t len; unsigned char buf[64]; size_t n; } Sha256;
static const uint32_t K[64] = {
  0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
  0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
  0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
  0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
  0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
  0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2 };
#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
static void sha_block(Sha256 *c, const unsigned char *p) {
  uint32_t w[64];
  for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
  for (int i = 16; i < 64; i++)
    w[i] = w[i - 16] + (ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3)) + w[i - 7] + (ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10));
  uint32_t a = c->s[0], b = c->s[1], cc = c->s[2], d = c->s[3], e = c->s[4], f = c->s[5], g = c->s[6], h = c->s[7];
  for (int i = 0; i < 64; i++) {
    uint32_t t1 = h + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
    uint32_t t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & cc) ^ (b & cc));
    h = g; g = f; f = e; e = d + t1; d = cc; cc = b; b = a; a = t1 + t2;
  }
  c->s[0] += a; c->s[1] += b; c->s[2] += cc; c->s[3] += d; c->s[4] += e; c->s[5] += f; c->s[6] += g; c->s[7] += h;
}
static void sha_init(Sha256 *c) {
  static const uint32_t iv[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
  memcpy(c->s, iv, sizeof iv); c->len = 0; c->n = 0;
}
static void sha_update(Sha256 *c, const unsigned char *p, size_t n) {
  c->len += n;
  while (n) {
    size_t k = 64 - c->n; if (k > n) k = n;
    memcpy(c->buf + c->n, p, k); c->n += k; p += k; n -= k;
    if (c->n == 64) { sha_block(c, c->buf); c->n = 0; }
  }
}
static void sha_hex(Sha256 *c, char out[65]) {
  uint64_t bits = c->len * 8; unsigned char pad = 0x80, z = 0;
  sha_update(c, &pad, 1);
  while (c->n != 56) sha_update(c, &z, 1);
  unsigned char L[8]; for (int i = 0; i < 8; i++) L[i] = (unsigned char)(bits >> (56 - 8 * i));
  sha_update(c, L, 8);
  for (int i = 0; i < 8; i++) snprintf(out + i * 8, 9, "%08x", c->s[i]);
}
static int file_sha256(const char *path, char out[65]) {
  FILE *f = fopen(path, "rb"); if (!f) return -1;
  Sha256 c; sha_init(&c);
  static unsigned char b[65536]; size_t k;
  while ((k = fread(b, 1, sizeof b, f)) > 0) sha_update(&c, b, k);
  fclose(f); sha_hex(&c, out);
  return 0;
}

// --------------------------------------------------------------------- http --
int sceSysmoduleLoadModule(uint16_t id);
int sceNetInit(void);
int sceNetPoolCreate(const char *name, int size, int flags);
int sceSslInit(uint64_t poolSize);
int sceHttpInit(int netMemId, int sslCtxId, uint64_t poolSize);
int sceHttpCreateTemplate(int httpCtxId, const char *ua, int httpVer, int autoProxy);
int sceHttpsDisableOption(int id, uint32_t flags);
int sceHttpSetConnectTimeOut(int id, uint32_t usec);
int sceHttpSetRecvTimeOut(int id, uint32_t usec);
int sceHttpSetResponseHeaderMaxSize(int id, size_t size);
typedef int (*ssl_cb_t)(int, void *const[], int, void *);
int sceHttpsSetSslCallback(int id, ssl_cb_t cb, void *arg);
int sceHttpCreateConnectionWithURL(int tmpl, const char *url, int keepAlive);
int sceHttpCreateRequestWithURL(int conn, int method, const char *url, uint64_t clen);
int sceHttpSendRequest(int req, const void *data, size_t size);
int sceHttpGetStatusCode(int req, int *status);
int sceHttpReadData(int req, void *data, size_t size);
int sceHttpDeleteRequest(int req);
int sceHttpDeleteConnection(int conn);

static int tmpl = -1;
// la verifica TLS è disattivata (nessun archivio di CA): l'integrità la garantiscono firma e sha256
static int accept_cert(int e, void *const c[], int n, void *u) { (void)e; (void)c; (void)n; (void)u; return 0; }

static int http_init(void) {
  sceSysmoduleLoadModule(0x0009); sceSysmoduleLoadModule(0x0045); sceSysmoduleLoadModule(0x000A);
  int rc = sceNetInit();
  int pool = sceNetPoolCreate("omega-installer", 64 * 1024, 0);
  int ssl = pool >= 0 ? sceSslInit(512 * 1024) : -1;
  int http = ssl >= 0 ? sceHttpInit(pool, ssl, 512 * 1024) : -1;
  tmpl = http >= 0 ? sceHttpCreateTemplate(http, "OmegaInstaller/1.0", 2, 1) : -1;
  // ogni passo nel registro: è la prima cosa da guardare quando "la rete non c'è"
  lg("rete: net 0x%x pool 0x%x ssl 0x%x http 0x%x template 0x%x", (unsigned)rc, (unsigned)pool, (unsigned)ssl, (unsigned)http, (unsigned)tmpl);
  if (tmpl < 0) return -1;
  sceHttpSetConnectTimeOut(tmpl, 15 * 1000 * 1000);
  sceHttpSetRecvTimeOut(tmpl, 60 * 1000 * 1000);
  sceHttpSetResponseHeaderMaxSize(tmpl, 64 * 1024);
  sceHttpsDisableOption(tmpl, 0x01);
  sceHttpsSetSslCallback(tmpl, accept_cert, NULL);
  return 0;
}

// ----------------------------------------------------------- http semplice --
typedef struct { uint32_t s_addr; } NetInAddr;
int sceNetResolverCreate(const char *name, int memid, int flags);
int sceNetResolverStartNtoa(int rid, const char *hostname, NetInAddr *addr, int timeout, int retry, int flags);
int sceNetResolverDestroy(int rid);
int sceNetPoolCreate(const char *name, int size, int flags);
static int raw_pool = -1;

static int resolve(struct in_addr *out) {
  if (raw_pool < 0) raw_pool = sceNetPoolCreate("omega-installer-dns", 16 * 1024, 0);
  int rid = raw_pool >= 0 ? sceNetResolverCreate("omega-installer", raw_pool, 0) : -1;
  NetInAddr a = { 0 };
  int rc = rid >= 0 ? sceNetResolverStartNtoa(rid, RAW_HOST, &a, 5 * 1000 * 1000, 2, 0) : rid;
  if (rid >= 0) sceNetResolverDestroy(rid);
  if (rc >= 0 && a.s_addr) { out->s_addr = a.s_addr; return 0; }
  lg("dns: %s non risolto (0x%x), uso %s", RAW_HOST, (unsigned)rc, RAW_IP);
  return inet_pton(AF_INET, RAW_IP, out) == 1 ? 0 : -1;
}

// GET http://RAW_HOST<path> → file, con socket e timeout nostri. Status HTTP o <0.
static int raw_get(const char *url, const char *dest) {
  const char *path = strstr(url, "://"); path = path ? strchr(path + 3, '/') : NULL;
  if (!path) return -1;
  struct sockaddr_in sa; memset(&sa, 0, sizeof sa);
  sa.sin_family = AF_INET; sa.sin_port = htons(80);
  if (resolve(&sa.sin_addr)) return -1;
  int s = socket(AF_INET, SOCK_STREAM, 0); if (s < 0) return -1;
  struct timeval tv = { 20, 0 };
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv); setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
  if (connect(s, (struct sockaddr *)&sa, sizeof sa) != 0) { lg("http semplice: connessione fallita (%d)", errno); close(s); return -1; }
  char req[600];
  int n = snprintf(req, sizeof req, "GET %s HTTP/1.0\r\nHost: " RAW_HOST "\r\nUser-Agent: OmegaInstaller/1.0 (http)\r\nConnection: close\r\n\r\n", path);
  if (send(s, req, (size_t)n, 0) != n) { close(s); return -1; }
  // intestazioni: fino alla riga vuota
  static char hb[8192]; size_t hn = 0; char *body = NULL;
  while (hn + 1 < sizeof hb) {
    ssize_t k = recv(s, hb + hn, sizeof hb - 1 - hn, 0);
    if (k <= 0) break;
    hn += (size_t)k; hb[hn] = 0;
    if ((body = strstr(hb, "\r\n\r\n"))) { body += 4; break; }
  }
  int status = -1;
  if (!body || sscanf(hb, "HTTP/%*s %d", &status) != 1) { close(s); return -1; }
  if (status == 200) {
    FILE *f = fopen(dest, "wb");
    if (!f) status = -2;
    else {
      size_t first = hn - (size_t)(body - hb);
      if (first && fwrite(body, 1, first, f) != first) status = -3;
      static char buf[64 * 1024]; ssize_t k;
      while (status == 200 && (k = recv(s, buf, sizeof buf, 0)) > 0)
        if (fwrite(buf, 1, (size_t)k, f) != (size_t)k) status = -3;
      fclose(f);
    }
  }
  close(s);
  return status;
}

// GET url → file: prima SceHttp (https), poi http semplice. Status HTTP o <0.
static int sce_download(const char *url, const char *dest);
static int download(const char *url, const char *dest) {
  int st = tmpl >= 0 ? sce_download(url, dest) : -1;
  if (st == 200) return st;
  int st2 = raw_get(url, dest);
  lg("GET %s in http semplice -> %d", url, st2);
  return st2 == 200 ? st2 : st;
}

static int sce_download(const char *url, const char *dest) {
  int conn = sceHttpCreateConnectionWithURL(tmpl, url, 0);
  if (conn < 0) { lg("GET %s: connessione 0x%x", url, (unsigned)conn); return conn; }
  int req = sceHttpCreateRequestWithURL(conn, 0, url, 0);
  if (req < 0) { lg("GET %s: richiesta 0x%x", url, (unsigned)req); sceHttpDeleteConnection(conn); return req; }
  int status = -1;
  int sent = sceHttpSendRequest(req, NULL, 0);
  if (sent < 0) status = sent;
  if (sent >= 0) {
    sceHttpGetStatusCode(req, &status);
    if (status == 200) {
      FILE *f = fopen(dest, "wb");
      if (!f) status = -2;
      else {
        static char buf[256 * 1024]; int k;
        while ((k = sceHttpReadData(req, buf, sizeof buf)) > 0)
          if (fwrite(buf, 1, (size_t)k, f) != (size_t)k) { status = -3; break; }
        if (k < 0) status = k;
        fclose(f);
      }
    }
  }
  sceHttpDeleteRequest(req); sceHttpDeleteConnection(conn);
  lg("GET %s -> %d (0x%x)", url, status, (unsigned)status);
  return status;
}

// ----------------------------------------------------------------- manifest --
static char *slurp(const char *path, size_t max, size_t *len) {
  FILE *f = fopen(path, "rb"); if (!f) return NULL;
  char *b = malloc(max + 1); size_t r = b ? fread(b, 1, max, f) : 0;
  fclose(f);
  if (!b) return NULL;
  b[r] = 0; if (len) *len = r;
  return b;
}

static int unhex(const char *h, unsigned char *out, size_t n) {
  for (size_t i = 0; i < n; i++) {
    unsigned v;
    if (sscanf(h + 2 * i, "%2x", &v) != 1) return -1;
    out[i] = (unsigned char)v;
  }
  return 0;
}

// Valore stringa di "key" dentro l'oggetto del componente con quell'id.
// Il manifest lo scriviamo noi (e la firma è già verificata): basta un parser minimo.
static int component_field(const char *m, const char *id, const char *key, char *out, size_t n) {
  char pat[64]; snprintf(pat, sizeof pat, "\"id\": \"%s\"", id);
  const char *o = strstr(m, pat);
  if (!o) return -1;
  const char *end = strchr(o, '}');
  snprintf(pat, sizeof pat, "\"%s\": \"", key);
  const char *v = strstr(o, pat);
  if (!v || (end && v > end)) return -1;
  v += strlen(pat);
  size_t i = 0;
  while (*v && *v != '"' && i + 1 < n) out[i++] = *v++;
  out[i] = 0;
  return 0;
}

static int mkdirs(const char *path) {
  char p[256]; snprintf(p, sizeof p, "%s", path);
  for (char *s = p + 1; *s; s++) if (*s == '/') { *s = 0; mkdir(p, 0777); *s = '/'; }
  return mkdir(p, 0777) == 0 || errno == EEXIST ? 0 : -1;
}

// Scarica il componente, controlla lo sha256 e lo mette al suo posto.
static int install_component(const char *m, const char *id, const char *dest, char *sha_out, char *ver_out) {
  char file[160], sha[80], url[400], tmp[300], got[65];
  if (component_field(m, id, "file", file, sizeof file) || component_field(m, id, "sha256", sha, sizeof sha)) {
    lg("componente %s assente nel manifest", id);
    return -1;
  }
  if (ver_out) component_field(m, id, "version", ver_out, 32);
  if (strchr(file, '/') || strstr(file, "..")) return -1;
  snprintf(url, sizeof url, "%s/%s", UPDATES, file);
  snprintf(tmp, sizeof tmp, "%s.part", dest);
  int st = download(url, tmp);
  if (st != 200) { lg("%s: download HTTP %d", id, st); unlink(tmp); return -1; }
  if (file_sha256(tmp, got) || strcmp(got, sha)) { lg("%s: sha256 diverso", id); unlink(tmp); return -1; }
  chmod(tmp, 0755);
  if (rename(tmp, dest)) { lg("%s: rename fallito (%d)", id, errno); unlink(tmp); return -1; }
  if (sha_out) snprintf(sha_out, 65, "%s", sha);
  return 0;
}

static void write_sidecar(const char *elf, const char *sha, const char *ver) {
  char p[300]; snprintf(p, sizeof p, "%s.json", elf);
  FILE *f = fopen(p, "w");
  if (!f) return;
  fprintf(f, "{\n  \"name\": \"OmegaRedirect\",\n  \"filename\": \"omega_redirect.elf\",\n"
             "  \"description\": \"Riapre Omega quando torni alla Home e porta le notifiche durante il gioco\",\n"
             "  \"version\": \"%s\",\n  \"checksum\": \"%s\",\n  \"category\": \"Utilities\",\n"
             "  \"source\": \"%s\",\n  \"install_source\": \"Omega Installer\"\n}\n", ver, sha, OMEGA_BASE_URL);
  fclose(f);
}

// GET /hbldr di websrv: avvia Omega come app in primo piano
static int launch_omega(void) {
  int s = socket(AF_INET, SOCK_STREAM, 0); if (s < 0) return -1;
  struct sockaddr_in a; memset(&a, 0, sizeof a);
  a.sin_family = AF_INET; a.sin_port = htons(8080); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (connect(s, (struct sockaddr *)&a, sizeof a)) { close(s); return -1; }
  const char *req = "GET /hbldr?pipe=0&daemon=0&path=" HB_DIR "/OmegaUI.elf&cwd=" HB_DIR " HTTP/1.1\r\n"
                    "Host: 127.0.0.1\r\nConnection: close\r\n\r\n";
  write(s, req, strlen(req));
  char sink[256]; read(s, sink, sizeof sink);
  close(s);
  return 0;
}

int main(void) {
  // stessa migrazione della UI e del demone
  if (access(DATA_DIR, F_OK) != 0 && access(DATA_DIR_LEGACY, F_OK) == 0) rename(DATA_DIR_LEGACY, DATA_DIR);
  mkdirs(DATA_DIR);
  i18n_init(sys_lang());
  notify(_("Omega: preparazione dell'installazione..."));
  if (http_init()) lg("https non disponibile: provo in http semplice");

  const char *mpath = DATA_DIR "/installer-manifest.json", *spath = DATA_DIR "/installer-manifest.sig";
  if (download(UPDATES "/manifest.json", mpath) != 200 || download(UPDATES "/manifest.sig", spath) != 200) {
    notify(_("Omega: server non raggiungibile, riprova più tardi"));
    return 1;
  }
  size_t mlen = 0;
  char *m = slurp(mpath, 256 * 1024, &mlen), *sig = slurp(spath, 1024, NULL);
  unsigned char sg[64], pk[32];
  int ok = m && sig && strlen(sig) >= 128 && !unhex(sig, sg, 64) && !unhex(PUBKEY_HEX, pk, 32)
           && crypto_ed25519_check(sg, pk, (const unsigned char *)m, mlen) == 0;
  free(sig);
  if (!ok) { notify(_("Omega: firma dell'aggiornamento non valida, installazione annullata")); free(m); return 1; }

  char ver[32] = "", sha[65] = "";
  notify(_("Omega: download in corso..."));
  if (mkdirs(HB_DIR) || install_component(m, "omega_ui", HB_DIR "/OmegaUI.elf", NULL, ver)) {
    char e[300]; snprintf(e, sizeof e, _("Omega: installazione non riuscita (vedi %s)"), LOG_FILE);
    notify(e);
    free(m);
    return 1;
  }
  mkdirs(HB_DIR "/sce_sys");
  install_component(m, "omega_ui_js", HB_DIR "/homebrew.js", NULL, NULL);
  install_component(m, "omega_ui_icon", HB_DIR "/sce_sys/icon0.png", NULL, NULL);

  // servizio e plugin pronti per la configurazione al primo avvio
  mkdirs(HB_DIR "/payloads");
  install_component(m, "omega_redirect", HB_DIR "/payloads/omega_redirect.elf", sha, NULL);
  install_component(m, "omega_onion", HB_DIR "/payloads/OMGA00001.elf", NULL, NULL);
  install_component(m, "patchdl", HB_DIR "/payloads/patchdl.elf", NULL, NULL);   // aggiornamenti dei giochi
  // chi aveva già il servizio in Payload Manager lo trova aggiornato
  struct stat pst;
  char rver[32] = "";
  if (stat(PLD_DIR "/omega_redirect.elf", &pst) == 0 && !install_component(m, "omega_redirect", PLD_DIR "/omega_redirect.elf", sha, rver))
    write_sidecar(PLD_DIR "/omega_redirect.elf", sha, rver);
  free(m);

  char msg[300];
  snprintf(msg, sizeof msg, _("Omega %s installato. Avvio in corso..."), ver);
  notify(msg);
  sleep(2);
  if (launch_omega()) notify(_("Omega è installato: aprilo da websrv (Homebrew)"));
  return 0;
}
