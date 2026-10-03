// Omega UI — sessione locale sicura (solo token, mai password in chiaro).
#include "omega.h"
#include "servers.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int json_str(const char *js, const char *key, char *out, size_t n) {
  char pat[64]; snprintf(pat, sizeof pat, "\"%s\":\"", key);
  const char *p = strstr(js, pat);
  out[0] = '\0'; if (!p) return -1;
  p += strlen(pat); size_t i = 0;
  while (*p && *p != '"' && i + 1 < n) out[i++] = *p++;
  out[i] = '\0'; return 0;
}

void session_load(char *token, size_t ntok, char *online_id, size_t noid) {
  token[0] = '\0'; if (online_id) online_id[0] = '\0';
  int fd = open(OMEGA_SESSION, O_RDONLY); if (fd < 0) return;
  char buf[RESP_MAX]; int r = (int)read(fd, buf, sizeof buf - 1); close(fd);
  if (r <= 0) return; buf[r] = '\0';
  // una sessione vale solo per il server su cui è nata
  char srv[220]; json_str(buf, "server", srv, sizeof srv);
  if (strcmp(srv[0] ? srv : "omega", srv_key())) return;
  json_str(buf, "token", token, ntok);
  if (online_id) json_str(buf, "online_id", online_id, noid);
}

void session_save(const char *token, const char *online_id, const char *account_id, const char *expires_at) {
  mkdir(OMEGA_DIR, 0777);
  char out[1400];
  snprintf(out, sizeof out,
    "{\"token\":\"%s\",\"online_id\":\"%s\",\"account_id\":\"%s\",\"expires_at\":\"%s\",\"server\":\"%s\"}\n",
    token, online_id ? online_id : "", account_id ? account_id : "", expires_at ? expires_at : "", srv_key());
  int fd = open(OMEGA_SESSION, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (fd < 0) return; write(fd, out, strlen(out)); close(fd);
}

void session_clear(void) { unlink(OMEGA_SESSION); }
