// Omega UI — funzioni di servizio condivise: file, codifica degli URL, SHA-256.
#include "app.h"
#include <stdlib.h>

char *file_read(const char *path, size_t max, size_t *len) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  char *b = malloc(max + 1);
  size_t n = b ? fread(b, 1, max, f) : 0;
  fclose(f);
  if (!b) return NULL;
  b[n] = 0;
  if (len) *len = n;
  return b;
}

void url_encode(char *dst, size_t n, const char *src, const char *keep) {
  static const char HEX[] = "0123456789ABCDEF";
  size_t o = 0;
  for (const unsigned char *p = (const unsigned char *)src; *p && o + 4 < n; p++) {
    if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || (keep && strchr(keep, *p)))
      dst[o++] = (char)*p;
    else { dst[o++] = '%'; dst[o++] = HEX[*p >> 4]; dst[o++] = HEX[*p & 15]; }
  }
  dst[o] = 0;
}

// ------------------------------------------------------------------- date --
// Le date si compongono da modelli tradotti con segnaposto {nome}: ogni lingua
// mette giorno, mese e anno nell'ordine che vuole (es. "{month}{day}日").
void tmpl_fill(char *out, size_t n, const char *tmpl, const char *const *kv, int nkv) {
  size_t o = 0;
  for (const char *p = tmpl; *p && o + 1 < n;) {
    if (*p == '{') {
      const char *e = strchr(p, '}'); int done = 0;
      for (int i = 0; e && i + 1 < nkv * 2; i += 2)
        if (strlen(kv[i]) == (size_t)(e - p - 1) && !strncmp(p + 1, kv[i], (size_t)(e - p - 1))) {
          o += (size_t)snprintf(out + o, n - o, "%s", kv[i + 1]); if (o >= n) o = n - 1;
          p = e + 1; done = 1; break;
        }
      if (done) continue;
    }
    out[o++] = *p++;
  }
  out[o] = 0;
}

static const char *const MONTH[12] = { N_("gennaio"), N_("febbraio"), N_("marzo"), N_("aprile"), N_("maggio"), N_("giugno"),
                                       N_("luglio"), N_("agosto"), N_("settembre"), N_("ottobre"), N_("novembre"), N_("dicembre") };
static const char *const MONTH_ABBR[12] = { N_("gen"), N_("feb"), N_("mar"), N_("apr"), N_("mag"), N_("giu"),
                                            N_("lug"), N_("ago"), N_("set"), N_("ott"), N_("nov"), N_("dic") };
static const char *const WDAY[7] = { N_("domenica"), N_("luned\xC3\xAC"), N_("marted\xC3\xAC"), N_("mercoled\xC3\xAC"),
                                     N_("gioved\xC3\xAC"), N_("venerd\xC3\xAC"), N_("sabato") };

void date_long(char *out, size_t n, int wday, int day, int mon) {
  char d[8]; snprintf(d, sizeof d, "%d", day);
  const char *kv[] = { "weekday", _(WDAY[(wday % 7 + 7) % 7]), "day", d, "month", _(MONTH[(mon % 12 + 12) % 12]) };
  tmpl_fill(out, n, _("{weekday} {day} {month}"), kv, 3);
}

void date_short(char *out, size_t n, int day, int mon, int year) {
  char d[8], y[8]; snprintf(d, sizeof d, "%d", day); snprintf(y, sizeof y, "%d", year);
  const char *kv[] = { "day", d, "mon", _(MONTH_ABBR[(mon % 12 + 12) % 12]), "year", y };
  tmpl_fill(out, n, _("{day} {mon} {year}"), kv, 3);
}

uint32_t fnv1a(const char *s) {
  uint32_t h = 2166136261u;
  for (; *s; s++) h = (h ^ (unsigned char)*s) * 16777619u;
  return h;
}

// ------------------------------------------------------------------ SHA-256 --
typedef struct { uint32_t s[8]; uint64_t len; unsigned char buf[64]; size_t n; } Sha256;

static const uint32_t K256[64] = {
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
  for (int i = 16; i < 64; i++) {
    uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3), s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  uint32_t a = c->s[0], b = c->s[1], cc = c->s[2], d = c->s[3], e = c->s[4], f = c->s[5], g = c->s[6], h = c->s[7];
  for (int i = 0; i < 64; i++) {
    uint32_t t1 = h + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K256[i] + w[i];
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

int file_sha256(const char *path, char out[65]) {
  FILE *f = fopen(path, "rb"); if (!f) return -1;
  Sha256 c; sha_init(&c); unsigned char b[65536]; size_t k;
  while ((k = fread(b, 1, sizeof b, f)) > 0) sha_update(&c, b, k);
  fclose(f); sha_hex(&c, out); return 0;
}
