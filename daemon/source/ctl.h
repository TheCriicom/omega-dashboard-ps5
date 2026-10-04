// Omega — server di controllo del demone (vedi ctl.c).
#pragma once
#define OMEGA_CTL_PORT 9095
#include <stddef.h>
int ctl_start(int port, const char *data_dir);   // data_dir: remote.json (PIN) e music.json (preferite)
// stato del sistema per /v1/system, scritto come oggetto JSON in out
typedef void (*ctl_system_fn)(char *out, size_t n);
void ctl_on_system(ctl_system_fn fn);
