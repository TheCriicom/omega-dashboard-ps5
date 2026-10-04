// Omega UI — configurazione e servizi di base (log, rete, sessione, tastiera).
#pragma once
#include <stddef.h>
#include <stdint.h>

// Server predefinito; quello in uso lo dà omega_base() (servers.c).
#ifndef OMEGA_BASE_URL
#define OMEGA_BASE_URL "https://play.omegasuite.it"
#endif
#ifndef OMEGA_API
#define OMEGA_API "/api/v1"
#endif
// versione dell'app: la usano gli aggiornamenti, va alzata a ogni rilascio.
#ifndef OMEGA_VERSION
#define OMEGA_VERSION "2026.10.04.15"
#endif

// Dati dell'app. Il demone omega_redirect legge session.json e ui-active da
// qui, quindi i due programmi devono concordare sul percorso.
#ifndef OMEGA_DIR
#define OMEGA_DIR        "/data/Omega"
#define OMEGA_DIR_LEGACY "/data/OmegaPSNLab"   // nome delle prime versioni, migrato all'avvio
#endif
#define OMEGA_SESSION    OMEGA_DIR "/session.json"
#define OMEGA_UI_ACTIVE  OMEGA_DIR "/ui-active"
#define OMEGA_LOG        OMEGA_DIR "/omega-ui.log"
#define OMEGA_CMD        OMEGA_DIR "/omega-ui.cmd"
#define OMEGA_SHOT       OMEGA_DIR "/omega-ui-shot.png"

// Filesystem della console; sul desktop un albero finto dentro OMEGA_DIR.
#ifdef PS5
#define OMEGA_SYSROOT ""
#else
#define OMEGA_SYSROOT OMEGA_DIR "/sysroot"
#endif
#define OMEGA_HB_ROOT  OMEGA_SYSROOT "/data/homebrew"           // homebrew in formato websrv
#define OMEGA_PLD_ROOT OMEGA_SYSROOT "/data/pldmgr/payloads"    // payload di Payload Manager
#define WEBSRV_URL     "http://127.0.0.1:8080"                  // websrv sulla console: avvia gli homebrew (/hbldr)

#define SCREEN_W 1920
#define SCREEN_H 1080
#define RESP_MAX 8192
#define NET_BIG  (640 * 1024)

// metodi SceHttp
#define HTTP_GET    0
#define HTTP_POST   1
#define HTTP_DELETE 5

void omega_log(const char *fmt, ...);
const char *omega_base(void);

// -------------------- rete (net.c; desktop/desktop.c sul Mac). Thread-safe. --
int  net_setup(void);   // 0 = ok
// Richiesta verso il server in uso. token e body facoltativi. Status HTTP o rc < 0.
int  omega_http(int method, const char *path, const char *token,
                const char *body, char *out, size_t outlen);
// GET binario: *out allocato con malloc (al massimo max byte).
int  omega_http_bin(const char *path, const char *token, unsigned char **out, size_t *len, size_t max);
// POST di un corpo binario (application/octet-stream).
int  omega_http_upload(const char *path, const char *token, const void *data, size_t len, char *out, size_t outlen);
// Scarica un URL completo (anche di terzi) in dest seguendo i redirect.
// Aggiorna *done/*total e si ferma se *cancel diventa 1. Status HTTP o rc < 0.
int  omega_url_download(const char *url, const char *dest, volatile long *done, volatile long *total, volatile int *cancel);
// Primi n byte di un URL (per riconoscere il tipo di file). Byte letti o < 0.
long omega_url_peek(const char *url, unsigned char *buf, size_t n);

// ----------------------------------------------------- sessione (session.c) --
int  json_str(const char *js, const char *key, char *out, size_t n);   // "key":"valore" in un JSON piatto
void session_load(char *token, size_t ntok, char *online_id, size_t noid);
void session_save(const char *token, const char *online_id, const char *account_id, const char *expires_at);
void session_clear(void);

// ---------------------------------------------- tastiera di sistema (ime.c) --
// Apre la tastiera e attende la conferma; render_frame ridisegna lo sfondo.
// Ritorna 1 se l'utente ha confermato.
int  ime_input(const char *title, const char *initial, int is_password,
               char *out, size_t outlen, void (*render_frame)(void));
