// Omega — parser JSON minimo (copia di omega-ui-src/source/json.c: tenerle uguali).
// Basta per le risposte del nostro server e per i comandi del lettore.
#include "json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { const char *p; int depth; } P;

static void ws(P *s) { while (*s->p == ' ' || *s->p == '\n' || *s->p == '\r' || *s->p == '\t') s->p++; }

static JVal *node(JType t) { JVal *v = calloc(1, sizeof *v); if (v) v->t = t; return v; }

static void put_utf8(char **o, unsigned cp) {
  char *d = *o;
  if (cp < 0x80) *d++ = (char)cp;
  else if (cp < 0x800) { *d++ = (char)(0xC0 | (cp >> 6)); *d++ = (char)(0x80 | (cp & 0x3F)); }
  else if (cp < 0x10000) { *d++ = (char)(0xE0 | (cp >> 12)); *d++ = (char)(0x80 | ((cp >> 6) & 0x3F)); *d++ = (char)(0x80 | (cp & 0x3F)); }
  else { *d++ = (char)(0xF0 | (cp >> 18)); *d++ = (char)(0x80 | ((cp >> 12) & 0x3F)); *d++ = (char)(0x80 | ((cp >> 6) & 0x3F)); *d++ = (char)(0x80 | (cp & 0x3F)); }
  *o = d;
}

static unsigned hex4(const char *p) {
  unsigned v = 0;
  for (int i = 0; i < 4; i++) {
    char c = p[i]; v <<= 4;
    if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
    else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
    else return 0xFFFFFFFF;
  }
  return v;
}

static char *str(P *s) {
  if (*s->p != '"') return NULL;
  s->p++;
  const char *q = s->p; size_t n = 0;
  while (*q && *q != '"') { if (*q == '\\' && q[1]) q++; q++; n++; }
  if (*q != '"') return NULL;
  char *out = malloc(n * 4 + 1), *o = out;
  if (!out) return NULL;
  while (*s->p && *s->p != '"') {
    char c = *s->p++;
    if (c != '\\') { *o++ = c; continue; }
    c = *s->p++;
    switch (c) {
      case 'n': *o++ = '\n'; break; case 't': *o++ = '\t'; break;
      case 'r': *o++ = '\r'; break; case 'b': *o++ = '\b'; break;
      case 'f': *o++ = '\f'; break;
      case 'u': {
        unsigned cp = hex4(s->p);
        if (cp == 0xFFFFFFFF) { free(out); return NULL; }
        s->p += 4;
        if (cp >= 0xD800 && cp < 0xDC00 && s->p[0] == '\\' && s->p[1] == 'u') {
          unsigned lo = hex4(s->p + 2);
          if (lo >= 0xDC00 && lo < 0xE000) { cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); s->p += 6; }
        }
        put_utf8(&o, cp); break;
      }
      default: *o++ = c;
    }
  }
  *o = '\0'; s->p++;
  return out;
}

static JVal *val(P *s);

static JVal *arr_or_obj(P *s, int obj) {
  JVal *v = node(obj ? J_OBJ : J_ARR), *last = NULL;
  if (!v) return NULL;
  s->p++; ws(s);
  if (*s->p == (obj ? '}' : ']')) { s->p++; return v; }
  for (;;) {
    char *key = NULL;
    ws(s);
    if (obj) {
      key = str(s); if (!key) goto bad;
      ws(s); if (*s->p != ':') { free(key); goto bad; } s->p++;
    }
    JVal *c = val(s);
    if (!c) { free(key); goto bad; }
    c->key = key;
    if (last) last->next = c; else v->child = c;
    last = c; v->len++;
    ws(s);
    if (*s->p == ',') { s->p++; continue; }
    if (*s->p == (obj ? '}' : ']')) { s->p++; return v; }
    goto bad;
  }
bad:
  json_free(v);
  return NULL;
}

static JVal *val(P *s) {
  if (++s->depth > 64) return NULL;
  ws(s);
  JVal *v = NULL;
  char c = *s->p;
  if (c == '{' || c == '[') v = arr_or_obj(s, c == '{');
  else if (c == '"') { char *x = str(s); if (x) { v = node(J_STR); if (v) v->s = x; else free(x); } }
  else if (!strncmp(s->p, "true", 4)) { v = node(J_BOOL); if (v) v->n = 1; s->p += 4; }
  else if (!strncmp(s->p, "false", 5)) { v = node(J_BOOL); s->p += 5; }
  else if (!strncmp(s->p, "null", 4)) { v = node(J_NULL); s->p += 4; }
  else if (c == '-' || (c >= '0' && c <= '9')) {
    char *end; double d = strtod(s->p, &end);
    if (end != s->p) { v = node(J_NUM); if (v) v->n = d; s->p = end; }
  }
  s->depth--;
  return v;
}

JVal *json_parse(const char *src) {
  if (!src) return NULL;
  P s = { src, 0 };
  return val(&s);
}

void json_free(JVal *v) {
  while (v) {
    JVal *n = v->next;
    json_free(v->child);
    free(v->s); free(v->key); free(v);
    v = n;
  }
}

JVal *jget(JVal *o, const char *key) {
  if (!o || o->t != J_OBJ) return NULL;
  for (JVal *c = o->child; c; c = c->next) if (c->key && !strcmp(c->key, key)) return c;
  return NULL;
}

const char *jstr(JVal *o, const char *key, const char *def) {
  JVal *v = key ? jget(o, key) : o;
  return (v && v->t == J_STR) ? v->s : def;
}

double jnum(JVal *o, const char *key, double def) {
  JVal *v = key ? jget(o, key) : o;
  if (!v) return def;
  if (v->t == J_NUM || v->t == J_BOOL) return v->n;
  if (v->t == J_STR) return atof(v->s);
  return def;
}

int jbool(JVal *o, const char *key) {
  JVal *v = jget(o, key);
  return v && (v->t == J_BOOL || v->t == J_NUM) && v->n != 0;
}

int jlen(JVal *a) { return (a && (a->t == J_ARR || a->t == J_OBJ)) ? a->len : 0; }

void jcpy(char *dst, size_t n, JVal *o, const char *key) {
  const char *s = jstr(o, key, NULL);
  if (!s) {
    JVal *v = jget(o, key);
    if (v && v->t == J_NUM) { snprintf(dst, n, "%.0f", v->n); return; }
    s = "";
  }
  snprintf(dst, n, "%s", s);
}

void json_escape(char *dst, size_t n, const char *src) {
  size_t o = 0;
  for (const unsigned char *p = (const unsigned char *)src; *p && o + 7 < n; p++) {
    if (*p == '"' || *p == '\\') { dst[o++] = '\\'; dst[o++] = (char)*p; }
    else if (*p == '\n') { dst[o++] = '\\'; dst[o++] = 'n'; }
    else if (*p < 0x20) { o += (size_t)snprintf(dst + o, n - o, "\\u%04x", *p); }
    else dst[o++] = (char)*p;
  }
  dst[o] = '\0';
}
