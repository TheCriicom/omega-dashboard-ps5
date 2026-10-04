// Prova del lettore del demone sul Mac: stesso player.c e ctl.c, uscita SDL.
//   ./build-desktop.sh && ./omega-player-desktop [file di stato]
#include "../source/ctl.h"
#include "../source/player.h"
#include "../source/lib.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

void player_log(const char *fmt, ...) {
  va_list ap; va_start(ap, fmt);
  printf("[%ld] ", (long)time(NULL)); vprintf(fmt, ap); printf("\n"); fflush(stdout);
  va_end(ap);
}

// sul Mac il JSON della libreria si scarica con curl
static long curl_fetch(const char *url, char *buf, size_t max) {
  char cmd[1400]; snprintf(cmd, sizeof cmd, "curl -sfL --max-time 20 '%s'", url);
  FILE *p = popen(cmd, "r"); if (!p) return -1;
  size_t got = fread(buf, 1, max, p);
  int rc = pclose(p);
  return rc == 0 ? (long)got : -1;
}
static void fake_system(char *out, size_t n) {
  const char *lang = getenv("OMEGA_LANG"), *game = getenv("OMEGA_GAME");   // console finta
  snprintf(out, n, "{\"game\":\"%s\",\"title_id\":\"PPSA01325\",\"cpu_t\":58,\"fan\":40,\"friends_online\":2,\"lang\":\"%s\"}", game && *game ? game : "Astro Bot (prova)", lang && *lang ? lang : "it");
}
static void track(const char *t, const char *a) { player_log("ora suona: %s%s%s", t, a[0] ? " \xE2\x80\x94 " : "", a); }

int main(int argc, char **argv) {
  player_init(argc > 1 ? argv[1] : "/tmp/omega-player.json");
  player_on_track(track);
  static char dir[300]; snprintf(dir, sizeof dir, "%s", argc > 1 ? argv[1] : "/tmp/omega-player.json");
  char *sl = strrchr(dir, '/'); if (sl) *sl = 0;
  ctl_on_system(fake_system);
  char lib[400]; snprintf(lib, sizeof lib, "%s/library.json", dir);
  lib_init(lib, curl_fetch);
  if (ctl_start(OMEGA_CTL_PORT, dir) != 0) { fprintf(stderr, "porta %d occupata\n", OMEGA_CTL_PORT); return 1; }
  for (;;) sleep(60);
}
