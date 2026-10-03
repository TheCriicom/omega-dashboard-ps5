// Omega UI — sostituti di net.c e ime.c per la build desktop (macOS): la rete
// passa da libcurl e la tastiera di sistema non c'è (si usa il comando di debug
// "text"). Serve a provare la UI senza console.
#include "../source/omega.h"
#include <curl/curl.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

void omega_log(const char *fmt, ...) {
  char l[1536];
  int n = snprintf(l, sizeof l, "[%lu] ", (unsigned long)time(NULL));
  va_list ap; va_start(ap, fmt); n += vsnprintf(l + n, sizeof l - n, fmt, ap); va_end(ap);
  fprintf(stderr, "%s\n", l);
  int fd = open(OMEGA_LOG, O_WRONLY | O_CREAT | O_APPEND, 0666);
  if (fd >= 0) { if (n < (int)sizeof l - 1) l[n++] = '\n'; write(fd, l, (size_t)n); close(fd); }
}

#define UA "OmegaUI/1.00"

int net_setup(void) { curl_global_init(CURL_GLOBAL_ALL); return 0; }

typedef struct { char *out; size_t len, cap; } Buf;
static size_t wr(char *p, size_t s, size_t n, void *u) {
  Buf *b = u; size_t k = s * n;
  if (b->len + k >= b->cap) k = b->cap - 1 - b->len;
  memcpy(b->out + b->len, p, k); b->len += k; b->out[b->len] = 0;
  return s * n;
}

int omega_http(int method, const char *path, const char *token, const char *body, char *out, size_t outlen) {
  char url[640]; snprintf(url, sizeof url, "%s%s", omega_base(), path);
  CURL *c = curl_easy_init(); if (!c) return -1;
  Buf b = { out, 0, outlen }; out[0] = 0;
  struct curl_slist *h = NULL;
  char auth[760];
  if (token) { snprintf(auth, sizeof auth, "Authorization: Bearer %s", token); h = curl_slist_append(h, auth); }
  if (body) h = curl_slist_append(h, "Content-Type: application/json");
  curl_easy_setopt(c, CURLOPT_URL, url);
  curl_easy_setopt(c, CURLOPT_USERAGENT, UA);
  curl_easy_setopt(c, CURLOPT_HTTPHEADER, h);
  curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, wr);
  curl_easy_setopt(c, CURLOPT_WRITEDATA, &b);
  curl_easy_setopt(c, CURLOPT_TIMEOUT, 10L);
  curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
  if (method == HTTP_POST) { curl_easy_setopt(c, CURLOPT_POST, 1L); curl_easy_setopt(c, CURLOPT_POSTFIELDS, body ? body : ""); }
  else if (method == HTTP_DELETE) curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, "DELETE");
  CURLcode rc = curl_easy_perform(c);
  long st = -1;
  if (rc == CURLE_OK) curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &st);
  curl_slist_free_all(h); curl_easy_cleanup(c);
  return rc == CURLE_OK ? (int)st : -1;
}

int ime_input(const char *title, const char *initial, int is_password, char *out, size_t outlen, void (*render_frame)(void)) {
  (void)title; (void)initial; (void)is_password; (void)out; (void)outlen; (void)render_frame;
  omega_log("(desktop) tastiera non disponibile: usa 'text <valore>' nel file comandi");
  return 0;
}

typedef struct { unsigned char *b; size_t len, cap, max; } BBuf;
static size_t wrb(char *p, size_t s, size_t n, void *u) {
  BBuf *b = u; size_t k = s * n;
  if (b->len + k > b->cap) { size_t nc = b->cap * 2; while (nc < b->len + k) nc *= 2; if (nc > b->max) return 0; unsigned char *x = realloc(b->b, nc); if (!x) return 0; b->b = x; b->cap = nc; }
  memcpy(b->b + b->len, p, k); b->len += k; return k;
}
int omega_http_bin(const char *path, const char *token, unsigned char **out, size_t *len, size_t max) {
  char url[640]; snprintf(url, sizeof url, "%s%s", omega_base(), path);
  CURL *c = curl_easy_init(); if (!c) return -1;
  BBuf b = { malloc(65536), 0, 65536, max };
  struct curl_slist *h = NULL; char auth[760];
  if (token) { snprintf(auth, sizeof auth, "Authorization: Bearer %s", token); h = curl_slist_append(h, auth); }
  curl_easy_setopt(c, CURLOPT_URL, url);
  curl_easy_setopt(c, CURLOPT_USERAGENT, UA); curl_easy_setopt(c, CURLOPT_HTTPHEADER, h);
  curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, wrb); curl_easy_setopt(c, CURLOPT_WRITEDATA, &b);
  curl_easy_setopt(c, CURLOPT_TIMEOUT, 15L); curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
  CURLcode rc = curl_easy_perform(c); long st = -1;
  if (rc == CURLE_OK) curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &st);
  curl_slist_free_all(h); curl_easy_cleanup(c);
  *out = b.b; *len = b.len;
  return rc == CURLE_OK ? (int)st : -1;
}

typedef struct { FILE *f; volatile long *done; volatile int *cancel; } DlCtx;
static size_t dlwr(char *p, size_t s, size_t n, void *u) {
  DlCtx *d = u; if (d->cancel && *d->cancel) return 0;
  size_t k = fwrite(p, 1, s * n, d->f); if (d->done) *d->done += (long)k; return k;
}
int omega_url_download(const char *url, const char *dest, volatile long *done, volatile long *total, volatile int *cancel) {
  if (done) *done = 0; if (total) *total = 0;
  FILE *f = fopen(dest, "wb"); if (!f) return -100;
  CURL *c = curl_easy_init(); if (!c) { fclose(f); return -1; }
  DlCtx d = { f, done, cancel };
  curl_easy_setopt(c, CURLOPT_URL, url);
  curl_easy_setopt(c, CURLOPT_USERAGENT, UA);
  curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, dlwr);
  curl_easy_setopt(c, CURLOPT_WRITEDATA, &d);
  curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
  CURLcode rc = curl_easy_perform(c);
  long st = -1; curl_off_t cl = 0;
  if (rc == CURLE_OK) { curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &st); curl_easy_getinfo(c, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &cl); if (total) *total = (long)cl; }
  curl_easy_cleanup(c); fclose(f);
  return rc == CURLE_OK ? (int)st : -1;
}

long omega_url_peek(const char *url, unsigned char *buf, size_t n) {
  BBuf b = { malloc(n + 16), 0, n + 16, n + 16 };
  CURL *c = curl_easy_init(); if (!c) { free(b.b); return -1; }
  char range[32]; snprintf(range, sizeof range, "0-%zu", n - 1);
  curl_easy_setopt(c, CURLOPT_URL, url);
  curl_easy_setopt(c, CURLOPT_USERAGENT, UA); curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(c, CURLOPT_RANGE, range);
  curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, wrb); curl_easy_setopt(c, CURLOPT_WRITEDATA, &b);
  curl_easy_setopt(c, CURLOPT_TIMEOUT, 15L); curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
  CURLcode rc = curl_easy_perform(c);
  long got = -1;
  if (rc == CURLE_OK) { got = (long)(b.len < n ? b.len : n); memcpy(buf, b.b, (size_t)got); }
  curl_easy_cleanup(c); free(b.b);
  return got;
}

int omega_http_upload(const char *path, const char *token, const void *data, size_t len, char *out, size_t outlen) {
  char url[640]; snprintf(url, sizeof url, "%s%s", omega_base(), path);
  CURL *c = curl_easy_init(); if (!c) return -1;
  Buf b = { out, 0, outlen }; out[0] = 0;
  struct curl_slist *h = NULL; char auth[760];
  if (token) { snprintf(auth, sizeof auth, "Authorization: Bearer %s", token); h = curl_slist_append(h, auth); }
  h = curl_slist_append(h, "Content-Type: application/octet-stream");
  curl_easy_setopt(c, CURLOPT_URL, url);
  curl_easy_setopt(c, CURLOPT_USERAGENT, UA); curl_easy_setopt(c, CURLOPT_HTTPHEADER, h);
  curl_easy_setopt(c, CURLOPT_POST, 1L); curl_easy_setopt(c, CURLOPT_POSTFIELDS, data); curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)len);
  curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, wr); curl_easy_setopt(c, CURLOPT_WRITEDATA, &b);
  curl_easy_setopt(c, CURLOPT_TIMEOUT, 180L); curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
  CURLcode rc = curl_easy_perform(c); long st = -1;
  if (rc == CURLE_OK) curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &st);
  curl_slist_free_all(h); curl_easy_cleanup(c);
  return rc == CURLE_OK ? (int)st : -1;
}
