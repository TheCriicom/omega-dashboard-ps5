// Omega UI — server a cui si collega l'app. "Omega" (OMEGA_BASE_URL) c'è sempre
// e non si toglie; gli altri li aggiunge l'utente. Il predefinito si salva come
// "omega" e non come indirizzo: se l'URL cambia nella build, chi l'aveva scelto
// lo segue.
#include "app.h"
#include "servers.h"
#include <ctype.h>
#include <stdlib.h>
#include <sys/stat.h>

#define SRV_FILE OMEGA_DIR "/servers.json"

static Server list[SRV_MAX];
static int n = 0, cur = 0, loaded = 0;
static char base[200] = OMEGA_BASE_URL;

static void builtin(void) {
  n = 1; cur = 0;
  snprintf(list[0].name, sizeof list[0].name, "Omega");
  snprintf(list[0].url, sizeof list[0].url, "%s", OMEGA_BASE_URL);
}

static void save(void) {
  mkdir(OMEGA_DIR, 0777);
  FILE *f = fopen(SRV_FILE ".tmp", "w");
  if (!f) return;
  char e1[160], e2[420];
  if (cur == 0) fprintf(f, "{\"current\":\"omega\",\"servers\":[");
  else { json_escape(e2, sizeof e2, list[cur].url); fprintf(f, "{\"current\":\"%s\",\"servers\":[", e2); }
  for (int i = 1; i < n; i++) {
    json_escape(e1, sizeof e1, list[i].name); json_escape(e2, sizeof e2, list[i].url);
    fprintf(f, "%s{\"name\":\"%s\",\"url\":\"%s\"}", i > 1 ? "," : "", e1, e2);
  }
  fprintf(f, "]}\n");
  fclose(f);
  rename(SRV_FILE ".tmp", SRV_FILE);
}

void srv_load(void) {
  loaded = 1;
  builtin();
  char *buf = file_read(SRV_FILE, 8191, NULL);
  if (buf) {
    JVal *j = json_parse(buf);
    free(buf);
    JFOR(s, jget(j, "servers")) {
      if (n >= SRV_MAX) break;
      const char *u = jstr(s, "url", "");
      if (!u[0]) continue;
      snprintf(list[n].name, sizeof list[n].name, "%s", jstr(s, "name", u));
      snprintf(list[n].url, sizeof list[n].url, "%s", u);
      n++;
    }
    const char *c = jstr(j, "current", "omega");
    for (int i = 1; i < n; i++) if (!strcmp(list[i].url, c)) cur = i;
    json_free(j);
  }
  snprintf(base, sizeof base, "%s", list[cur].url);
}

static void ensure(void) { if (!loaded) srv_load(); }

const char *omega_base(void) { ensure(); return base; }
int srv_count(void) { ensure(); return n; }
int srv_current(void) { ensure(); return cur; }
const Server *srv_get(int i) { ensure(); return i >= 0 && i < n ? &list[i] : NULL; }
const char *srv_key(void) { ensure(); return cur == 0 ? "omega" : list[cur].url; }

void srv_select(int i) {
  ensure();
  if (i < 0 || i >= n) return;
  cur = i;
  snprintf(base, sizeof base, "%s", list[cur].url);
  save();
  omega_log("server: %s (%s)", list[cur].name, base);
}

// Normalizza quello che scrive l'utente: niente spazi, schema https:// se manca,
// niente "/" finale. Accetta solo host fatti di caratteri da URL.
int srv_normalize(const char *in, char *out, size_t on) {
  char t[200]; size_t k = 0;
  for (const char *p = in; *p && k + 1 < sizeof t; p++) if (!isspace((unsigned char)*p)) t[k++] = *p;
  t[k] = 0;
  if (!t[0]) return -1;
  if (strncmp(t, "http://", 7) && strncmp(t, "https://", 8)) snprintf(out, on, "https://%s", t);
  else snprintf(out, on, "%s", t);
  size_t l = strlen(out);
  while (l > 0 && out[l - 1] == '/') out[--l] = 0;
  const char *host = strstr(out, "://") + 3;
  if (!host[0] || host[0] == '/' || host[0] == ':') return -1;
  for (const char *p = host; *p; p++)
    if (!isalnum((unsigned char)*p) && !strchr(".-_:/[]%~", *p)) return -1;
  return 0;
}

int srv_add(const char *name, const char *url) {
  ensure();
  char u[200];
  if (srv_normalize(url, u, sizeof u) < 0) return -1;
  for (int i = 0; i < n; i++) if (!strcmp(list[i].url, u)) return i;
  if (n >= SRV_MAX) return -2;
  const char *host = strstr(u, "://") + 3;
  snprintf(list[n].name, sizeof list[n].name, "%s", name && name[0] ? name : host);
  snprintf(list[n].url, sizeof list[n].url, "%s", u);
  n++;
  save();
  return n - 1;
}

// Togliendo il server in uso si torna a Omega (indice 0, che non si toglie).
int srv_remove(int i) {
  ensure();
  if (i <= 0 || i >= n) return -1;
  for (int k = i; k < n - 1; k++) list[k] = list[k + 1];
  n--;
  if (cur == i) { cur = 0; snprintf(base, sizeof base, "%s", list[0].url); }
  else if (cur > i) cur--;
  save();
  return 0;
}

const char *srv_host(const Server *s) {
  const char *h = s ? strstr(s->url, "://") : NULL;
  return h ? h + 3 : (s ? s->url : "");
}
