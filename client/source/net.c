// Omega UI — client HTTP(S) con SceHttp. I certificati non vengono verificati
// (callback che accetta tutto): gli aggiornamenti si affidano alla firma
// Ed25519 del manifest, non al TLS.
#include "omega.h"
#include "i18n.h"
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

void omega_log(const char *fmt, ...) {
  int fd = open(OMEGA_LOG, O_WRONLY | O_CREAT | O_APPEND, 0666);
  if (fd < 0) return;
  char l[1536];
  int n = snprintf(l, sizeof l, "[%lu] ", (unsigned long)time(NULL));
  va_list ap; va_start(ap, fmt); n += vsnprintf(l + n, sizeof l - n, fmt, ap); va_end(ap);
  if (n < (int)sizeof l - 1) l[n++] = '\n';
  write(fd, l, (size_t)n); close(fd);
}

int sceSysmoduleLoadModule(uint16_t id);
int sceNetInit(void);
int sceNetPoolCreate(const char *name, int size, int flags);
int sceSslInit(uint64_t poolSize);
int sceHttpInit(int netMemId, int sslCtxId, uint64_t poolSize);
int sceHttpCreateTemplate(int httpCtxId, const char *ua, int httpVer, int autoProxy);
int sceHttpsDisableOption(int id, uint32_t flags);
int sceHttpSetConnectTimeOut(int id, uint32_t usec);
int sceHttpSetRecvTimeOut(int id, uint32_t usec);
int sceHttpSetSendTimeOut(int id, uint32_t usec);
int sceHttpSetAutoRedirect(int id, int onOff);
typedef int (*omega_ssl_cb)(int, void *const[], int, void *);
int sceHttpsSetSslCallback(int id, omega_ssl_cb cb, void *arg);
int sceHttpCreateConnectionWithURL(int tmpl, const char *url, int keepAlive);
int sceHttpCreateRequestWithURL(int conn, int method, const char *url, uint64_t clen);
int sceHttpAddRequestHeader(int id, const char *name, const char *value, uint32_t mode);
int sceHttpSendRequest(int req, const void *data, size_t size);
int sceHttpGetStatusCode(int req, int *status);
int sceHttpGetResponseContentLength(int req, int *result, uint64_t *length);
int sceHttpSetResponseHeaderMaxSize(int id, size_t headerSize);
int sceHttpReadData(int req, void *data, size_t size);
int sceHttpDeleteRequest(int req);
int sceHttpDeleteConnection(int conn);

#define SEC(s) ((uint32_t)(s) * 1000 * 1000)   // i timeout di SceHttp sono in microsecondi
// github.com (link dello Store) risponde con header oltre i 5 KB: col limite
// predefinito la richiesta fallisce con 0x80431073 (header troppo grandi).
#define HEADER_MAX (64 * 1024)
#define DL_CHUNK   (256 * 1024)

static int g_tmpl = -1;
static int ssl_cb(int e, void *const c[], int n, void *u) { (void)e;(void)c;(void)n;(void)u; return 0; }

int net_setup(void) {
  sceSysmoduleLoadModule(0x0009); sceSysmoduleLoadModule(0x0045); sceSysmoduleLoadModule(0x000A);
  if (sceNetInit() < 0) { omega_log("sceNetInit fail"); }
  int pool = sceNetPoolCreate("omega-ui", 64 * 1024, 0); if (pool < 0) return -2;
  int ssl = sceSslInit(512 * 1024); if (ssl < 0) return -3;
  int http = sceHttpInit(pool, ssl, 1024 * 1024); if (http < 0) return -4;
  g_tmpl = sceHttpCreateTemplate(http, "OmegaUI/1.00", 2, 1); if (g_tmpl < 0) return -5;
  sceHttpSetConnectTimeOut(g_tmpl, SEC(10));
  sceHttpSetRecvTimeOut(g_tmpl, SEC(10));
  sceHttpSetAutoRedirect(g_tmpl, 0);
  sceHttpSetResponseHeaderMaxSize(g_tmpl, HEADER_MAX);
  sceHttpsDisableOption(g_tmpl, 0x01);
  sceHttpsSetSslCallback(g_tmpl, ssl_cb, NULL);
  return 0;
}

// Percorso del server in uso ("/api/v1/...") oppure URL completo: musica dal
// demone (127.0.0.1) o da un server musicale dell'utente. Il token di Omega va
// solo al server di Omega.
static const char *full_url(const char *path, const char **token, char *url, size_t n) {
  if (!strncmp(path, "http://", 7) || !strncmp(path, "https://", 8)) {
    size_t bl = strlen(omega_base());
    if (strncmp(path, omega_base(), bl) != 0) *token = NULL;
    snprintf(url, n, "%s", path);
  } else snprintf(url, n, "%s%s", omega_base(), path);
  return url;
}

int omega_http(int method, const char *path, const char *token,
               const char *body, char *out, size_t outlen) {
  char url[1400]; full_url(path, &token, url, sizeof url);
  int conn = sceHttpCreateConnectionWithURL(g_tmpl, url, 1);
  if (conn < 0) { omega_log("conn err 0x%x %s", conn, path); return conn; }
  uint64_t blen = body ? (uint64_t)strlen(body) : 0;
  int req = sceHttpCreateRequestWithURL(conn, method, url, blen);
  if (req < 0) { sceHttpDeleteConnection(conn); return req; }
  if (token) { char b[700]; snprintf(b, sizeof b, "Bearer %s", token);
    sceHttpAddRequestHeader(req, "Authorization", b, 1); }
  if (body) sceHttpAddRequestHeader(req, "Content-Type", "application/json", 1);
  sceHttpAddRequestHeader(req, "Accept", "application/json", 1);
  sceHttpAddRequestHeader(req, "Accept-Language", i18n_code(), 1);   // documenti, Store ed errori nella lingua dell'utente
  int rc = sceHttpSendRequest(req, body, (size_t)blen);
  int status = -1;
  if (rc >= 0) {
    status = 0; sceHttpGetStatusCode(req, &status);
    size_t total = 0; int k;
    while (total + 1 < outlen && (k = sceHttpReadData(req, out + total, outlen - 1 - total)) > 0) total += (size_t)k;
    out[total] = '\0';
  } else omega_log("send err 0x%x %s", rc, path);
  sceHttpDeleteRequest(req); sceHttpDeleteConnection(conn);
  return rc >= 0 ? status : rc;
}

int omega_http_bin(const char *path, const char *token, unsigned char **out, size_t *len, size_t max) {
  *out = NULL; *len = 0;
  char url[1400]; full_url(path, &token, url, sizeof url);
  int conn = sceHttpCreateConnectionWithURL(g_tmpl, url, 1);
  if (conn < 0) return conn;
  int req = sceHttpCreateRequestWithURL(conn, HTTP_GET, url, 0);
  if (req < 0) { sceHttpDeleteConnection(conn); return req; }
  if (token) { char b[700]; snprintf(b, sizeof b, "Bearer %s", token); sceHttpAddRequestHeader(req, "Authorization", b, 1); }
  sceHttpAddRequestHeader(req, "Accept-Language", i18n_code(), 1);
  int status = -1;
  if (sceHttpSendRequest(req, NULL, 0) >= 0) {
    sceHttpGetStatusCode(req, &status);
    size_t cap = 256 * 1024, total = 0; unsigned char *buf = malloc(cap); int k;
    while (buf && (k = sceHttpReadData(req, buf + total, cap - total)) > 0) {
      total += (size_t)k;
      if (total == cap) {
        if (cap >= max) break;
        unsigned char *nb = realloc(buf, cap * 2); if (!nb) break; buf = nb; cap *= 2;
      }
    }
    *out = buf; *len = total;
  }
  sceHttpDeleteRequest(req); sceHttpDeleteConnection(conn);
  return status;
}

// Voce del party: una connessione nuova costa TCP + TLS (250-400 ms misurati),
// più del ritmo dei pacchetti. Qui la connessione si tiene e si riusa; se cade
// se ne apre un'altra, una volta per richiesta.
void omega_keep_close(OmegaKeep *k) { if (k->conn >= 0) sceHttpDeleteConnection(k->conn); k->conn = -1; }
int omega_keep_req(OmegaKeep *k, int method, const char *path, const char *token, const void *body, size_t blen,
                   const char *ctype, unsigned char **out, size_t *olen, size_t max) {
  if (out) { *out = NULL; *olen = 0; }
  char url[1400]; full_url(path, &token, url, sizeof url);
  for (int attempt = 0; attempt < 2; attempt++) {
    if (k->conn < 0) { k->conn = sceHttpCreateConnectionWithURL(g_tmpl, url, 1); if (k->conn < 0) return k->conn; }
    int req = sceHttpCreateRequestWithURL(k->conn, method == 1 ? HTTP_POST : HTTP_GET, url, (uint64_t)blen);
    if (req < 0) { omega_keep_close(k); continue; }
    sceHttpSetRecvTimeOut(req, SEC(6));
    if (token) { char b[700]; snprintf(b, sizeof b, "Bearer %s", token); sceHttpAddRequestHeader(req, "Authorization", b, 1); }
    if (ctype) sceHttpAddRequestHeader(req, "Content-Type", ctype, 1);
    sceHttpAddRequestHeader(req, "Accept-Language", i18n_code(), 1);
    int status = -1;
    int rc = sceHttpSendRequest(req, body, blen);
    if (rc >= 0) {
      sceHttpGetStatusCode(req, &status);
      size_t cap = 4096, total = 0; unsigned char *buf = out ? malloc(cap + 1) : NULL, sink[512]; int r;
      for (;;) {
        if (buf && total == cap) {
          if (cap >= max) break;
          unsigned char *nb = realloc(buf, cap * 2 + 1); if (!nb) break; buf = nb; cap *= 2;
        }
        r = buf ? sceHttpReadData(req, buf + total, cap - total) : sceHttpReadData(req, sink, sizeof sink);
        if (r <= 0) break;
        if (buf) total += (size_t)r;
      }
      if (buf) buf[total] = 0;
      if (out) { *out = buf; *olen = total; }
    }
    sceHttpDeleteRequest(req);
    if (rc >= 0) return status;
    omega_keep_close(k);
  }
  return -1;
}

// Redirect abilitati sulla connessione e non sul template, che è condiviso dai
// worker della coda di rete.
int omega_url_download(const char *url, const char *dest, volatile long *done, volatile long *total, volatile int *cancel) {
  if (done) *done = 0; if (total) *total = 0;
  int conn = sceHttpCreateConnectionWithURL(g_tmpl, url, 1);
  if (conn < 0) { omega_log("dl conn err 0x%x", conn); return conn; }
  sceHttpSetAutoRedirect(conn, 1);
  sceHttpSetResponseHeaderMaxSize(conn, HEADER_MAX);
  int req = sceHttpCreateRequestWithURL(conn, HTTP_GET, url, 0);
  if (req < 0) { sceHttpDeleteConnection(conn); return req; }
  sceHttpSetAutoRedirect(req, 1);
  sceHttpSetResponseHeaderMaxSize(req, HEADER_MAX);
  sceHttpSetRecvTimeOut(req, SEC(60));
  sceHttpSetConnectTimeOut(req, SEC(20));
  int status = -1;
  int rc = sceHttpSendRequest(req, NULL, 0);
  if (rc >= 0) {
    sceHttpGetStatusCode(req, &status);
    if (status != 200) omega_log("dl HTTP %d", status);
    if (status == 200) {
      int lenType = 0; uint64_t clen = 0;
      if (sceHttpGetResponseContentLength(req, &lenType, &clen) >= 0 && clen > 0 && total) *total = (long)clen;
      FILE *f = fopen(dest, "wb");
      if (!f) { omega_log("dl fopen %s fallito", dest); status = -100; }
      else {
        char *buf = malloc(DL_CHUNK); long got = 0; int k;
        while (buf && !(cancel && *cancel) && (k = sceHttpReadData(req, buf, DL_CHUNK)) > 0) {
          if (fwrite(buf, 1, (size_t)k, f) != (size_t)k) { status = -101; break; }
          got += k; if (done) *done = got;
        }
        if (cancel && *cancel) status = -102;
        // connessione caduta a metà: un file troncato non va installato
        else if (status == 200 && total && *total > 0 && got < *total) { omega_log("dl troncato: %ld di %ld byte", got, *total); status = -103; }
        free(buf); fclose(f);
        if (status == 200 && total && *total <= 0) *total = got;
      }
    }
  } else omega_log("dl send err 0x%x", rc);
  sceHttpDeleteRequest(req); sceHttpDeleteConnection(conn);
  return rc >= 0 ? status : rc;
}

long omega_url_peek(const char *url, unsigned char *buf, size_t n) {
  int conn = sceHttpCreateConnectionWithURL(g_tmpl, url, 1);
  if (conn < 0) return conn;
  sceHttpSetAutoRedirect(conn, 1);
  sceHttpSetResponseHeaderMaxSize(conn, HEADER_MAX);
  int req = sceHttpCreateRequestWithURL(conn, HTTP_GET, url, 0);
  if (req < 0) { sceHttpDeleteConnection(conn); return req; }
  sceHttpSetAutoRedirect(req, 1);
  sceHttpSetResponseHeaderMaxSize(req, HEADER_MAX);
  sceHttpSetRecvTimeOut(req, SEC(15));
  long got = -1; int status = -1;
  if (sceHttpSendRequest(req, NULL, 0) >= 0) {
    sceHttpGetStatusCode(req, &status);
    if (status == 200) {
      got = 0; int k;
      while ((size_t)got < n && (k = sceHttpReadData(req, buf + got, n - got)) > 0) got += k;
    }
  }
  sceHttpDeleteRequest(req); sceHttpDeleteConnection(conn);
  return got;
}

// Tempi lunghi: il server converte i video prima di rispondere.
int omega_http_upload(const char *path, const char *token, const void *data, size_t len, char *out, size_t outlen) {
  char url[640]; snprintf(url, sizeof url, "%s%s", omega_base(), path);
  int conn = sceHttpCreateConnectionWithURL(g_tmpl, url, 1);
  if (conn < 0) return conn;
  int req = sceHttpCreateRequestWithURL(conn, HTTP_POST, url, (uint64_t)len);
  if (req < 0) { sceHttpDeleteConnection(conn); return req; }
  sceHttpSetSendTimeOut(req, SEC(180));
  sceHttpSetRecvTimeOut(req, SEC(180));
  if (token) { char b[700]; snprintf(b, sizeof b, "Bearer %s", token); sceHttpAddRequestHeader(req, "Authorization", b, 1); }
  sceHttpAddRequestHeader(req, "Content-Type", "application/octet-stream", 1);
  sceHttpAddRequestHeader(req, "Accept-Language", i18n_code(), 1);
  int status = -1;
  int rc = sceHttpSendRequest(req, data, len);
  if (rc >= 0) {
    sceHttpGetStatusCode(req, &status);
    size_t total = 0; int k;
    while (total + 1 < outlen && (k = sceHttpReadData(req, out + total, outlen - 1 - total)) > 0) total += (size_t)k;
    out[total] = 0;
  } else omega_log("upload send err 0x%x", rc);
  sceHttpDeleteRequest(req); sceHttpDeleteConnection(conn);
  return rc >= 0 ? status : rc;
}
