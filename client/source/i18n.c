// Omega UI — traduzioni: scelta della lingua e ricerca dei testi.
// _() si chiama a ogni fotogramma per ogni testo disegnato, quindi la ricerca
// è una tabella hash (FNV-1a sulla stringa) costruita una volta per lingua e
// poi mai più liberata: cambiare lingua è solo lo scambio di un puntatore, e i
// thread di rete che chiamano _() nel frattempo non leggono memoria liberata.
#include "omega.h"
#include "i18n.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#define LANG_FILE OMEGA_DIR "/lang.txt"

// Le 27 lingue, nell'ordine del menu (dopo "Automatica").
static const struct { const char *code, *name; } LANGS[] = {
  { "it", "Italiano" }, { "en", "English" }, { "ja", "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E" },
  { "fr", "Fran\xC3\xA7" "ais" }, { "es", "Espa\xC3\xB1ol" }, { "de", "Deutsch" }, { "nl", "Nederlands" },
  { "pt-PT", "Portugu\xC3\xAAs (Portugal)" }, { "pt-BR", "Portugu\xC3\xAAs (Brasil)" },
  { "ru", "\xD0\xA0\xD1\x83\xD1\x81\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9" }, { "ko", "\xED\x95\x9C\xEA\xB5\xAD\xEC\x96\xB4" },
  { "zh-Hans", "\xE7\xAE\x80\xE4\xBD\x93\xE4\xB8\xAD\xE6\x96\x87" }, { "zh-Hant", "\xE7\xB9\x81\xE9\xAB\x94\xE4\xB8\xAD\xE6\x96\x87" },
  { "fi", "Suomi" }, { "sv", "Svenska" }, { "da", "Dansk" }, { "nb", "Norsk bokm\xC3\xA5l" }, { "pl", "Polski" },
  { "tr", "T\xC3\xBCrk\xC3\xA7" "e" }, { "cs", "\xC4\x8C" "e\xC5\xA1tina" }, { "hu", "Magyar" },
  { "el", "\xCE\x95\xCE\xBB\xCE\xBB\xCE\xB7\xCE\xBD\xCE\xB9\xCE\xBA\xCE\xAC" }, { "ro", "Rom\xC3\xA2n\xC4\x83" },
  { "th", "\xE0\xB9\x84\xE0\xB8\x97\xE0\xB8\xA2" }, { "vi", "Ti\xE1\xBA\xBFng Vi\xE1\xBB\x87t" }, { "id", "Bahasa Indonesia" },
  { "uk", "\xD0\xA3\xD0\xBA\xD1\x80\xD0\xB0\xD1\x97\xD0\xBD\xD1\x81\xD1\x8C\xD0\xBA\xD0\xB0" },
};
#define NLANGS ((int)(sizeof LANGS / sizeof LANGS[0]))

int i18n_count(void) { return NLANGS; }
const char *i18n_lang_code(int i) { return i >= 0 && i < NLANGS ? LANGS[i].code : "en"; }
const char *i18n_lang_name(const char *code) {
  for (int i = 0; i < NLANGS; i++) if (!strcmp(LANGS[i].code, code)) return LANGS[i].name;
  return code;
}
static const char *known(const char *code) {      // codice canonico o NULL
  for (int i = 0; i < NLANGS; i++) if (!strcasecmp(LANGS[i].code, code)) return LANGS[i].code;
  return NULL;
}

// ----------------------------------------------------------- tabelle hash --
typedef struct { uint32_t h; const char *key, *val; } Slot;
typedef struct { Slot *slot; uint32_t mask; } Table;
static Table *built[NLANGS];          // costruite al primo uso, mai liberate
static Table *volatile cur;           // NULL = italiano (o lingua senza traduzioni)
static const char *cur_code = "it";
static char pref[16] = "auto";

static uint32_t fnv(const char *s) {
  uint32_t h = 2166136261u;
  while (*s) { h ^= (unsigned char)*s++; h *= 16777619u; }
  return h;
}

static Table *build(const char *code) {
  const I18nTable *t = NULL;
  for (int i = 0; i < i18n_ntables; i++) if (!strcmp(i18n_tables[i].code, code)) t = &i18n_tables[i];
  if (!t || t->n <= 0) return NULL;
  uint32_t size = 16; while (size < (uint32_t)t->n * 2) size <<= 1;
  Table *tb = malloc(sizeof *tb); Slot *sl = calloc(size, sizeof *sl);
  if (!tb || !sl) { free(tb); free(sl); return NULL; }
  tb->slot = sl; tb->mask = size - 1;
  for (int i = 0; i < t->n; i++) {
    const char *key = i18n_msgid_blob + i18n_msgid_off[t->pairs[i][0]];
    uint32_t h = fnv(key), k = h & tb->mask;
    while (sl[k].key) k = (k + 1) & tb->mask;
    sl[k].h = h; sl[k].key = key; sl[k].val = t->blob + t->pairs[i][1];
  }
  return tb;
}

const char *i18n_tr(const char *s) {
  Table *t = cur;
  if (!t || !s || !*s) return s;
  uint32_t h = fnv(s), k = h & t->mask;
  for (;;) {
    const Slot *e = &t->slot[k];
    if (!e->key) return s;
    if (e->h == h && !strcmp(e->key, s)) return e->val;
    k = (k + 1) & t->mask;
  }
}

const char *i18n_trc(const char *s) {
  const char *t = i18n_tr(s), *sep;
  return t == s && (sep = strchr(s, '\004')) ? sep + 1 : t;
}

static void apply(const char *code) {
  const char *c = known(code); if (!c) c = "en";
  int idx = 0; for (int i = 0; i < NLANGS; i++) if (LANGS[i].code == c) idx = i;
  if (!built[idx]) built[idx] = build(c);
  cur = built[idx]; cur_code = c;
  omega_log("lingua: %s (%s)", c, pref);
}

const char *i18n_code(void) { return cur_code; }

// Lingua con la regione, come la usano param.json dei giochi e navigator.language.
const char *i18n_locale(void) {
  static const char *M[][2] = {
    { "it", "it-IT" }, { "en", "en-US" }, { "ja", "ja-JP" }, { "fr", "fr-FR" }, { "es", "es-ES" }, { "de", "de-DE" }, { "nl", "nl-NL" },
    { "pt-PT", "pt-PT" }, { "pt-BR", "pt-BR" }, { "ru", "ru-RU" }, { "ko", "ko-KR" }, { "zh-Hans", "zh-Hans" }, { "zh-Hant", "zh-Hant" },
    { "fi", "fi-FI" }, { "sv", "sv-SE" }, { "da", "da-DK" }, { "nb", "no-NO" }, { "pl", "pl-PL" }, { "tr", "tr-TR" }, { "cs", "cs-CZ" },
    { "hu", "hu-HU" }, { "el", "el-GR" }, { "ro", "ro-RO" }, { "th", "th-TH" }, { "vi", "vi-VN" }, { "id", "id-ID" }, { "uk", "uk-UA" },
  };
  for (unsigned i = 0; i < sizeof M / sizeof M[0]; i++) if (!strcmp(M[i][0], cur_code)) return M[i][1];
  return "en-US";
}
const char *i18n_pref(void) { return pref; }

// ---------------------------------------------------- lingua del sistema --
#ifdef PS5
int sceSystemServiceParamGetInt(int paramId, int *value);   // libSceSystemService
#define SCE_SYSTEM_SERVICE_PARAM_ID_LANG 1
// valore di SCE_SYSTEM_SERVICE_PARAM_ID_LANG → codice; arabo (21) non c'è:
// servirebbe un'impaginazione da destra a sinistra.
static const char *PS5_LANG[] = {
  "ja", "en", "fr", "es", "de", "it", "nl", "pt-PT", "ru", "ko", "zh-Hant", "zh-Hans", "fi", "sv", "da", "nb",
  "pl", "pt-BR", "en", "tr", "es", "en", "fr", "cs", "hu", "el", "ro", "th", "vi", "id", "uk",
};
#endif

const char *i18n_detect(void) {
#ifdef PS5
  int v = -1, rc = sceSystemServiceParamGetInt(SCE_SYSTEM_SERVICE_PARAM_ID_LANG, &v);
  omega_log("lingua di sistema: rc=0x%x valore=%d", rc, v);
  if (rc == 0 && v >= 0 && v < (int)(sizeof PS5_LANG / sizeof PS5_LANG[0])) return PS5_LANG[v];
  return "en";
#else
  // OMEGA_LANG (codice esatto, es. "zh-Hant"), altrimenti LANG ("it_IT.UTF-8")
  const char *e = getenv("OMEGA_LANG");
  if (e && *e) { char t[16]; snprintf(t, sizeof t, "%s", e); for (char *p = t; *p; p++) if (*p == '_') *p = '-';
    const char *c = known(t); if (c) return c; }
  if (!e || !*e) e = getenv("LANG");
  if (!e || !*e) return "en";
  char l[32]; snprintf(l, sizeof l, "%s", e);
  l[strcspn(l, ".@")] = 0;
  for (char *p = l; *p; p++) if (*p == '-') *p = '_';
  if (!strncasecmp(l, "pt", 2)) return strcasecmp(l, "pt_BR") ? "pt-PT" : "pt-BR";
  if (!strncasecmp(l, "zh", 2))
    return strcasestr(l, "TW") || strcasestr(l, "HK") || strcasestr(l, "MO") || strcasestr(l, "Hant") ? "zh-Hant" : "zh-Hans";
  if (!strncasecmp(l, "no", 2) || !strncasecmp(l, "nn", 2)) return "nb";
  l[strcspn(l, "_")] = 0;
  const char *c = known(l);
  return c ? c : "en";
#endif
}

// ------------------------------------------------- preferenza dell'utente --
void i18n_init(void) {
  char buf[16] = "";
  FILE *f = fopen(LANG_FILE, "r");
  if (f) { if (!fgets(buf, sizeof buf, f)) buf[0] = 0; fclose(f); }
  buf[strcspn(buf, "\r\n \t")] = 0;
  const char *c = buf[0] ? known(buf) : NULL;
  snprintf(pref, sizeof pref, "%s", c ? c : "auto");
  apply(c ? c : i18n_detect());
}

void i18n_set(const char *code) {
  const char *c = code && *code && strcmp(code, "auto") ? known(code) : NULL;
  snprintf(pref, sizeof pref, "%s", c ? c : "auto");
  if (c) { FILE *f = fopen(LANG_FILE, "w"); if (f) { fprintf(f, "%s\n", c); fclose(f); } }
  else unlink(LANG_FILE);
  apply(c ? c : i18n_detect());
}
