// Omega UI — traduzioni. Il msgid è la stringa italiana esatta (come gettext):
// _("Indietro") ritorna la traduzione nella lingua attiva o il msgid stesso.
// I cataloghi stanno in i18n/<codice>.json e finiscono nell'ELF tramite
// source/i18n_data.c, generato da tools/i18n-gen.mjs.
#pragma once
#include <stdint.h>

// format_arg: il compilatore controlla gli argomenti di printf sul msgid.
const char *i18n_tr(const char *msgid) __attribute__((format_arg(1)));
#define _(s)  i18n_tr(s)
#define N_(s) (s)                 // solo per l'estrazione: si traduce al momento dell'uso
// Con contesto, per le parole che in italiano valgono più cose ("Altro" = More
// o Other): il msgid è "contesto\004testo", e senza traduzione resta "testo".
const char *i18n_trc(const char *ctx_msgid) __attribute__((format_arg(1)));
#define P_(c, s) i18n_trc(c "\004" s)

void i18n_init(void);             // legge OMEGA_DIR/lang.txt, altrimenti la lingua della console
void i18n_set(const char *code);  // NULL, "" o "auto" = segui la console; applica e salva
const char *i18n_code(void);      // lingua attiva ("it", "en", "zh-Hans"...)
const char *i18n_locale(void);    // con la regione ("it-IT", "ja-JP"...): param.json, navigator.language
const char *i18n_pref(void);      // "auto" o il codice scelto dall'utente
const char *i18n_detect(void);    // lingua di sistema (console) o, sul desktop, OMEGA_LANG / LANG

// Lingue supportate, nell'ordine del menu; il nome è nella lingua stessa.
int i18n_count(void);
const char *i18n_lang_code(int i);
const char *i18n_lang_name(const char *code);

// Tabelle generate (i18n_data.c): msgid concatenati con '\0' e, per ogni
// lingua, le traduzioni presenti come coppie { indice del msgid, offset }.
typedef struct { const char *code; const char *blob; const uint32_t (*pairs)[2]; int n; } I18nTable;
extern const int i18n_nmsg;
extern const char i18n_msgid_blob[];
extern const uint32_t i18n_msgid_off[];
extern const I18nTable i18n_tables[];
extern const int i18n_ntables;
