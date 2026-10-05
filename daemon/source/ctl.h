// Omega — server di controllo del demone (vedi ctl.c).
#pragma once
#define OMEGA_CTL_PORT 9095
#include <stddef.h>
int ctl_start(int port, const char *data_dir);   // data_dir: remote.json (PIN) e music.json (preferite)
// stato del sistema per /v1/system, scritto come oggetto JSON in out
typedef void (*ctl_system_fn)(char *out, size_t n);
void ctl_on_system(ctl_system_fn fn);
const char *ctl_last_request(void);              // per il log dei crash (metodo e percorso, senza query)
typedef void (*ctl_notify_fn)(const char *msg);   // notifica di sistema (avanzamento dei caricamenti)
void ctl_on_notify(ctl_notify_fn fn);
int omega_thread(void *(*fn)(void *), void *arg);   // thread staccato con 1 MB di stack nostro
// codice tecnico verso il server (main.c): in coda, non blocca mai chi chiama
void omega_diag(const char *comp, const char *ev, int ok, int rc, const char *detail);
