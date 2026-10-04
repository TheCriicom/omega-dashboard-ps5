// Omega per OnionHEN — una pagina "Omega" nel Toolbox di OnionHEN, raggiungibile
// anche durante il gioco con la scorciatoia del Toolbox (es. L2+R3):
//  · brano in corso del lettore di Omega, con riproduci/pausa, avanti, indietro
//    e volume;
//  · quanti amici sono online.
// I dati e i comandi passano dal demone omega_redirect (127.0.0.1:9095). La
// pagina di OnionHEN non si ridisegna mentre è aperta: si aggiorna riaprendola.
#include <onion/client.h>
#include <onion/ipc.h>
#include <onion/plugin.h>
#include <onion/status.h>
#include <onion/transport.h>
#include <onion/ui.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

ONION_PLUGIN_DEFINE("OMGA00001", "1.00", "Omega",
                    ONION_PLUGIN_CAP_IPC | ONION_PLUGIN_CAP_UI,
                    ONION_PLUGIN_FLAG_LONG_RUNNING | ONION_PLUGIN_FLAG_STOP_SUPPORTED);

#define LOG_FILE "/data/Omega/omega-onion.log"
#define CTL_PORT 9095

int sceSystemServiceParamGetInt(int paramId, int *value);
const char *i18n_tr(const char *msgid) __attribute__((format_arg(1)));
void i18n_init(int sys_lang);
#define _(s) i18n_tr(s)

static volatile sig_atomic_t running = 1;
static void stop(int s) { (void)s; running = 0; }

static void lg(const char *fmt, ...) {
  FILE *f = fopen(LOG_FILE, "a"); if (!f) return;
  fprintf(f, "[%ld] ", (long)time(NULL));
  va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
  fputc('\n', f); fclose(f);
}

// ------------------------------------------------- HTTP verso il demone --
// Una richiesta per connessione, corpo letto fino alla chiusura.
static int ctl(const char *method, const char *path, const char *body, char *out, size_t n) {
  out[0] = 0;
  int s = socket(AF_INET, SOCK_STREAM, 0); if (s < 0) return -1;
  struct timeval tv = { 2, 0 };
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv); setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
  struct sockaddr_in a; memset(&a, 0, sizeof a);
  a.sin_family = AF_INET; a.sin_port = htons(CTL_PORT); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (connect(s, (struct sockaddr *)&a, sizeof a) != 0) { close(s); return -2; }
  char req[512];
  int len = snprintf(req, sizeof req, "%s %s HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s",
                     method, path, body ? strlen(body) : 0, body ? body : "");
  if (write(s, req, (size_t)len) != len) { close(s); return -3; }
  size_t got = 0; ssize_t k;
  while (got + 1 < n && (k = read(s, out + got, n - 1 - got)) > 0) got += (size_t)k;
  out[got] = 0; close(s);
  char *b = strstr(out, "\r\n\r\n"); if (!b) return -4;
  int status = atoi(out + 9);
  memmove(out, b + 4, strlen(b + 4) + 1);
  return status;
}

// valore di "key" in un JSON piatto: stringa (senza escape complicati) o numero
static void jfield(const char *js, const char *key, char *out, size_t n) {
  char pat[48]; snprintf(pat, sizeof pat, "\"%s\":", key);
  const char *p = strstr(js, pat); out[0] = 0; if (!p) return;
  p += strlen(pat);
  size_t i = 0;
  if (*p == '"') { p++; while (*p && *p != '"' && i + 1 < n) { if (*p == '\\' && p[1]) p++; out[i++] = *p++; } }
  else while (*p && *p != ',' && *p != '}' && i + 1 < n) out[i++] = *p++;
  out[i] = 0;
}

// ---------------------------------------------------------------- pagina --
typedef struct { char line[220], sub[160], friends[80]; int volume, ok; } View;

static void read_view(View *v) {
  static char st[8192], sy[1024];
  memset(v, 0, sizeof *v); v->volume = 75;
  if (ctl("GET", "/v1/state", NULL, st, sizeof st) != 200) {
    snprintf(v->line, sizeof v->line, "%s", _("Omega non risponde"));
    snprintf(v->sub, sizeof v->sub, "%s", _("Avvia Omega o riavvia la console"));
    return;
  }
  v->ok = 1;
  char state[16], title[256], artist[256], stream[256], vol[8];
  jfield(st, "state", state, sizeof state); jfield(st, "title", title, sizeof title);
  jfield(st, "artist", artist, sizeof artist); jfield(st, "stream_title", stream, sizeof stream); jfield(st, "volume", vol, sizeof vol);
  v->volume = atoi(vol);
  if (!title[0] || !strcmp(state, "stopped")) snprintf(v->line, sizeof v->line, "%s", _("Niente in riproduzione"));
  else if (stream[0]) snprintf(v->line, sizeof v->line, "%s", stream);
  else if (artist[0]) snprintf(v->line, sizeof v->line, "%s \xE2\x80\x94 %s", title, artist);
  else snprintf(v->line, sizeof v->line, "%s", title);
  const char *what = !strcmp(state, "playing") ? (stream[0] ? title : _("In riproduzione")) : !strcmp(state, "paused") ? _("In pausa")
                   : !strcmp(state, "loading") ? _("Caricamento...") : !strcmp(state, "error") ? _("Errore") : _("Scegli la musica nell'app Omega");
  snprintf(v->sub, sizeof v->sub, "%s", what);
  if (ctl("GET", "/v1/system", NULL, sy, sizeof sy) == 200) {
    char fo[16]; jfield(sy, "friends_online", fo, sizeof fo);
    int n = fo[0] ? atoi(fo) : -1;
    if (n >= 0) snprintf(v->friends, sizeof v->friends, n == 1 ? _("1 amico online") : _("%d amici online"), n);
  }
  if (!v->friends[0]) snprintf(v->friends, sizeof v->friends, "%s", _("Amici: apri Omega per aggiornare"));
}

static onion_ui_node_desc_v1 node(uint32_t kind, const char *id, const char *parent, const char *title) {
  onion_ui_node_desc_v1 n; memset(&n, 0, sizeof n);
  n.struct_size = sizeof n; n.abi_version = ONION_UI_ABI_VERSION; n.kind = kind;
  snprintf(n.id, sizeof n.id, "%s", id);
  snprintf(n.parent_id, sizeof n.parent_id, "%s", parent ? parent : "");
  snprintf(n.title, sizeof n.title, "%s", title);
  return n;
}
static onion_status add(onion_ui_document *d, onion_ui_node_desc_v1 *n) { return onion_ui_document_add_node(d, n); }

static onion_status action(onion_ui_document *d, const char *id, const char *parent, const char *title) {
  onion_ui_node_desc_v1 n = node(ONION_UI_NODE_ACTION, id, parent, title);
  n.binding = ONION_UI_BINDING_EVENT; snprintf(n.binding_key, sizeof n.binding_key, "%s", id);
  return add(d, &n);
}

static onion_status build(const View *v, onion_ui_document **out) {
  onion_ui_document_desc_v1 desc; memset(&desc, 0, sizeof desc);
  desc.struct_size = sizeof desc; desc.abi_version = ONION_UI_ABI_VERSION; desc.priority = 100;
  snprintf(desc.plugin_id, sizeof desc.plugin_id, "OMGA00001");
  snprintf(desc.contribution_id, sizeof desc.contribution_id, "omega_main");
  snprintf(desc.title, sizeof desc.title, "Omega");
  snprintf(desc.description, sizeof desc.description, "%s", _("Musica e amici di Omega, anche durante il gioco"));
  snprintf(desc.root_page_id, sizeof desc.root_page_id, "main");
  onion_status s = onion_ui_document_create(&desc, out);
  if (s != ONION_OK) return s;
  onion_ui_document *d = *out;
  onion_ui_node_desc_v1 n = node(ONION_UI_NODE_PAGE, "main", NULL, "Omega");
  if ((s = add(d, &n)) != ONION_OK) goto fail;
  n = node(ONION_UI_NODE_GROUP, "music", "main", _("Musica")); if ((s = add(d, &n)) != ONION_OK) goto fail;
  n = node(ONION_UI_NODE_LABEL, "now", "music", v->line);
  snprintf(n.description, sizeof n.description, "%s", v->sub);
  if ((s = add(d, &n)) != ONION_OK) goto fail;
  if (v->ok) {
    if ((s = action(d, "toggle", "music", _("Riproduci / pausa"))) != ONION_OK) goto fail;
    if ((s = action(d, "next", "music", _("Brano successivo"))) != ONION_OK) goto fail;
    if ((s = action(d, "prev", "music", _("Brano precedente"))) != ONION_OK) goto fail;
    n = node(ONION_UI_NODE_LIST, "volume", "music", _("Volume"));
    n.value_type = ONION_UI_VALUE_STRING; n.binding = ONION_UI_BINDING_EVENT;
    snprintf(n.binding_key, sizeof n.binding_key, "volume");
    int vr = (v->volume + 12) / 25 * 25; snprintf(n.value, sizeof n.value, "%d", vr);
    if ((s = add(d, &n)) != ONION_OK) goto fail;
    static const char *lv[] = { "0", "25", "50", "75", "100" };
    for (int i = 0; i < 5; i++) {
      char id[16], t[16]; snprintf(id, sizeof id, "vol_%s", lv[i]); snprintf(t, sizeof t, "%s%%", lv[i]);
      n = node(ONION_UI_NODE_LIST_ITEM, id, "volume", t); n.value_type = ONION_UI_VALUE_STRING; snprintf(n.value, sizeof n.value, "%s", lv[i]);
      if ((s = add(d, &n)) != ONION_OK) goto fail;
    }
  }
  n = node(ONION_UI_NODE_GROUP, "social", "main", _("Amici")); if ((s = add(d, &n)) != ONION_OK) goto fail;
  n = node(ONION_UI_NODE_LABEL, "friends", "social", v->friends);
  snprintf(n.description, sizeof n.description, "%s", _("La pagina si aggiorna quando la riapri"));
  if ((s = add(d, &n)) != ONION_OK) goto fail;
  if ((s = onion_ui_document_validate(d)) != ONION_OK) goto fail;
  return ONION_OK;
fail:
  onion_ui_document_destroy(d); *out = NULL;
  return s;
}

static void on_event(const onion_ui_event_v1 *e) {
  char out[2048], body[96];
  if (!strcmp(e->node_id, "toggle") || !strcmp(e->node_id, "next") || !strcmp(e->node_id, "prev")) {
    snprintf(body, sizeof body, "{\"cmd\":\"%s\"}", e->node_id);
    ctl("POST", "/v1/cmd", body, out, sizeof out);
  } else if (!strcmp(e->node_id, "volume") || !strncmp(e->node_id, "vol_", 4)) {
    int v = atoi(e->value[0] ? e->value : e->node_id + 4);
    snprintf(body, sizeof body, "{\"cmd\":\"volume\",\"value\":%d}", v < 0 ? 0 : v > 100 ? 100 : v);
    ctl("POST", "/v1/cmd", body, out, sizeof out);
  }
  lg("evento %s = %s", e->node_id, e->value);
}

int main(void) {
  signal(SIGINT, stop); signal(SIGTERM, stop);
  signal(SIGPIPE, SIG_IGN);   // il servizio può chiudere la connessione: non deve chiudere il plugin
  int lang = -1; if (sceSystemServiceParamGetInt(1, &lang) != 0) lang = -1;
  i18n_init(lang);
  lg("==== Omega per OnionHEN avvio ====");

  onion_transport transport = { 0 };
  onion_socket_transport sock = { 0 };
  onion_client client = ONION_CLIENT_INITIALIZER;
  onion_host_services_v1 host;
  onion_ui_handle handle = 0;
  onion_ui_document *doc = NULL;
  View view, last;
  read_view(&view); last = view;

  onion_status s = onion_socket_transport_connect(&transport, &sock, ONION_PLUGIN_IPC_SOCKET_PATH);
  if (s == ONION_OK) s = onion_client_init(&client, &transport);
  if (s == ONION_OK) s = onion_client_open_session(&client, &onion_plugin_descriptor);
  if (s == ONION_OK) s = onion_client_make_services(&client, &host);
  if (s == ONION_OK) s = build(&view, &doc);
  if (s == ONION_OK) s = onion_ui_register(&host, doc, &handle);
  if (s != ONION_OK) { lg("avvio non riuscito: %s", onion_status_string(s)); goto out; }
  lg("pagina registrata");

  time_t refreshed = time(NULL);
  while (running) {
    onion_ui_event_v1 ev;
    s = onion_client_poll_ui_event(&client, &ev);
    if (s == ONION_OK) { on_event(&ev); refreshed = 0; continue; }
    if (s != ONION_E_NOT_FOUND) { lg("eventi interrotti: %s", onion_status_string(s)); break; }
    usleep(100 * 1000);
    // ogni 2 s (o subito dopo un comando) si rilegge lo stato; se è cambiato
    // si registra di nuovo la pagina con lo stesso id, che la sostituisce
    if (time(NULL) - refreshed < 2) continue;
    refreshed = time(NULL);
    read_view(&view);
    if (!memcmp(&view, &last, sizeof view)) continue;
    onion_ui_document *nd = NULL;
    if (build(&view, &nd) == ONION_OK) {
      onion_ui_handle h2 = handle;
      if (onion_ui_register(&host, nd, &h2) == ONION_OK) { onion_ui_document_destroy(doc); doc = nd; handle = h2; last = view; }
      else onion_ui_document_destroy(nd);
    }
  }
out:
  if (handle) (void)onion_ui_unregister(&host, handle);
  onion_ui_document_destroy(doc);
  onion_client_deinit(&client);
  onion_socket_transport_deinit(&transport);
  lg("fermato");
  return 0;
}
