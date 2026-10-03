// Omega UI — elenco dei server (servers.c). omega_base() è in omega.h.
#pragma once
#include <stddef.h>

#define SRV_MAX 12
typedef struct { char name[48]; char url[200]; } Server;

void srv_load(void);
int  srv_count(void);
int  srv_current(void);
const Server *srv_get(int i);
const char *srv_key(void);                  // "omega" o l'URL: lega la sessione salvata al suo server
const char *srv_host(const Server *s);      // host senza schema, da mostrare
void srv_select(int i);
int  srv_normalize(const char *in, char *out, size_t on);
int  srv_add(const char *name, const char *url);   // indice, -1 URL non valido, -2 elenco pieno
int  srv_remove(int i);
