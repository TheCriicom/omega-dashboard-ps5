// GENERATO da omega-ui-src/tools/i18n-gen.mjs a partire da omega-ui-src/i18n/*.json:
// non modificare a mano (rigenerare con `node tools/i18n-gen.mjs`).
// Testi di omega_redirect (notifiche di sistema) e scelta della lingua. Autonomo:
// main.c dichiara i18n_tr, i18n_init e i18n_code e passa a i18n_init il valore
// di sceSystemServiceParamGetInt(1 /* lingua */) o -1 se non disponibile.
#include <stdio.h>
#include <string.h>

#define I18N_LANG_FILE "/data/Omega/lang.txt"   // scelta fatta nella UI (Impostazioni → Lingua)
#define NMSG 2
#define NTR 4

static const char *const MSGID[NMSG] = {
  "Omega · %s",
  "Omega · %s: %s",
};
static const char *const CODE[NTR] = { "ja", "fr", "zh-Hans", "zh-Hant" };
static const char *const TR[NTR][NMSG] = {
  { // ja
    0,
    "Omega · %s：%s",
  },
  { // fr
    0,
    "Omega · %s : %s",
  },
  { // zh-Hans
    0,
    "Omega · %s：%s",
  },
  { // zh-Hant
    0,
    "Omega · %s：%s",
  },
};
static const char *const LANGS[] = { "it", "en", "ja", "fr", "es", "de", "nl", "pt-PT", "pt-BR", "ru", "ko", "zh-Hans", "zh-Hant", "fi", "sv", "da", "nb", "pl", "tr", "cs", "hu", "el", "ro", "th", "vi", "id", "uk" };
// valore di SCE_SYSTEM_SERVICE_PARAM_ID_LANG → codice (21 = arabo: inglese)
static const char *const PS5_LANG[] = {
  "ja", "en", "fr", "es", "de", "it", "nl", "pt-PT", "ru", "ko", "zh-Hant", "zh-Hans", "fi", "sv", "da", "nb",
  "pl", "pt-BR", "en", "tr", "es", "en", "fr", "cs", "hu", "el", "ro", "th", "vi", "id", "uk",
};

static int cur = -1;                 // riga di TR, -1 = msgid (italiano)
static const char *cur_code = "en";

const char *i18n_tr(const char *s) __attribute__((format_arg(1)));
const char *i18n_tr(const char *s) {
  if (cur < 0 || !s) return s;
  for (int i = 0; i < NMSG; i++) if (!strcmp(MSGID[i], s)) return TR[cur][i] ? TR[cur][i] : s;
  return s;
}

const char *i18n_code(void) { return cur_code; }

void i18n_init(int sys_lang) {
  char buf[16] = ""; const char *code = NULL;
  FILE *f = fopen(I18N_LANG_FILE, "r");
  if (f) { if (!fgets(buf, sizeof buf, f)) buf[0] = 0; fclose(f); buf[strcspn(buf, "\r\n \t")] = 0; }
  for (unsigned i = 0; buf[0] && i < sizeof LANGS / sizeof LANGS[0]; i++) if (!strcmp(LANGS[i], buf)) code = LANGS[i];
  if (!code) code = sys_lang >= 0 && sys_lang < (int)(sizeof PS5_LANG / sizeof PS5_LANG[0]) ? PS5_LANG[sys_lang] : "en";
  cur_code = code; cur = -1;
  for (int i = 0; i < NTR; i++) if (!strcmp(CODE[i], code)) cur = i;
}
