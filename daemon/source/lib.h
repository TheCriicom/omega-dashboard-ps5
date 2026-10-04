// Omega — "La mia libreria" (vedi lib.c).
#pragma once
#include <stddef.h>
#include "json.h"

// scarica un URL qualsiasi (http/https, con redirect) in buf; byte letti o < 0
typedef long (*lib_fetch_fn)(const char *url, char *buf, size_t max);

void   lib_init(const char *file, lib_fetch_fn fetch);
size_t lib_list_json(char *out, size_t cap);
int    lib_add(JVal *item, char *err, size_t en);          // nuovo, o modifica se "id" esiste
int    lib_remove(const char *id, char *url_out, size_t un);   // url_out: il file locale, se era un gioco caricato
int    lib_import(JVal *body, char *err, size_t en);       // giochi aggiunti o < 0
int    lib_set_source(const char *url, char *err, size_t en);
int    lib_sync(char *err, size_t en);
int    lib_add_upload(const char *title, const char *url, const char *kind, const char *cover,
                      const char *title_id, const char *version, const char *platform, char *id_out, size_t idn);
