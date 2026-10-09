// omega_redirect — voce del party quando la UI non è in primo piano (voice.c).
#pragma once
#include <stddef.h>
// base: server predefinito; tmpl_fn: template SceHttp pronto (o <0);
// session_fn: contenuto di session.json (>0 se c'è); ui_age_fn: secondi
// dall'ultimo ui-active (-1 se manca); notify_fn: notifica di sistema.
void voice_start(const char *base, int (*tmpl_fn)(void), int (*session_fn)(char *, size_t),
                 int (*ui_age_fn)(void), void (*notify_fn)(const char *), const char *(*lang_fn)(void));
int voice_command(const char *cmd);          // mute | unmute | toggle | leave
void voice_state_json(char *out, size_t n);  // stato per /v1/voice
void voice_on_crash(void);                  // dal gestore dei crash: ricorda dove si era
int foreground_user(void);                  // utente in primo piano (libSceUserService a runtime), -1 se non si sa
// main.c: un token che il server ha rifiutato (401) non si usa per un po'
void auth_rejected(const char *token);
int auth_blocked(const char *token);         // 1 = rifiutato di recente, non usarlo
