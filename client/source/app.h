// Omega UI — stato dell'app, modello social, grafica e livelli di interfaccia.
#pragma once
#include "omega.h"
#include "json.h"
#include "i18n.h"
#include <SDL.h>
#include <SDL_ttf.h>
#include <stdio.h>
#include <string.h>

// ------------------------------------------------------------------ utilità --
char *file_read(const char *path, size_t max, size_t *len);     // malloc, terminato da '\0'
// Codifica percentuale: lascia intatti lettere, cifre e i caratteri in keep.
void  url_encode(char *dst, size_t n, const char *src, const char *keep);
int   file_sha256(const char *path, char out[65]);              // 0 = ok
uint32_t fnv1a(const char *s);
// Date tradotte (mesi 0..11, giorni della settimana 0 = domenica).
void tmpl_fill(char *out, size_t n, const char *tmpl, const char *const *kv, int nkv);   // {chiave} → valore
void date_long(char *out, size_t n, int wday, int day, int mon);    // "lunedì 3 ottobre"
void date_short(char *out, size_t n, int day, int mon, int year);   // "3 ott 2026"

// ----------------------------------------------------- lavoro in background --
// Coda di rete: le callback girano sul thread principale (netq_pump).
typedef void (*NetCb)(int status, JVal *json, const char *raw, void *ud);
extern int g_net_gen;                 // cambia al logout: le risposte vecchie si scartano
void netq_init(void);
void net_req(int method, const char *path, const char *body, NetCb cb, void *ud);
void netq_pump(void);

// Caricatore di immagini: decodifica in un thread, texture sul thread principale.
enum { LOAD_ICON = 1, LOAD_BG = 2, LOAD_URL = 3 };
typedef void (*LoadCb)(const char *key, SDL_Texture *tex, SDL_Color avg, void *ud);
void loader_init(void);
void load_req(int kind, const char *key, const char *path, int w, int h, int radius, SDL_Color avg, LoadCb cb, void *ud);
void loader_pump(void);
SDL_Surface *gen_ambient(SDL_Color base, int w, int h);

// ------------------------------------------------------------------ grafica --
extern SDL_Renderer *R;
extern float g_dt;                    // secondi dall'ultimo fotogramma
extern double g_time;                 // secondi dall'avvio
extern Uint32 g_frame;

enum { W_LIGHT, W_REG, W_MED, W_BOLD };
TTF_Font *font(int weight, int size);

typedef SDL_Color Col;
#define RGB(r, g, b) ((SDL_Color){ (r), (g), (b), 255 })
extern const Col C_WHITE, C_TXT, C_DIM, C_FAINT, C_OK, C_ERR, C_WARN, C_BLACK;
extern Col C_ACC, C_ACC2, C_PANEL;    // dipendono dal tema

#define N_THEMES 12
extern int g_theme;
extern Col g_theme_base;
void theme_tint(int alpha);
void bake_tint(SDL_Surface *s, Col c);
void bake_dim(SDL_Surface *s);           // oscuramento scelto in Personalizza
void bg_refresh(void);                   // rifà gli sfondi dopo un cambio di Sfondo o Oscuramento

// ------------------------------------------------ personalizzazione (prefs.c) --
typedef struct {
  int theme, accent, bg_style, bg_dim, glass, corners, focus;
  int tiles, labels, sort, show_hb, ext_games, cards, hints;
  int clock12, clock_sec, clock_date, bar_temp;
  int particles, particle_style, anim, fps;
  int sfx_pack, music_mood;
  int toast_pos, toast_time, dnd;
  int saver_min, saver_style;
  int freeze;
} Prefs;
enum { PC_LOOK, PC_HOME, PC_BAR, PC_MOTION, PC_SOUND, PC_NOTIF, PC_SAVER, PC_PERF, PC_COUNT };
#define PREF_MAXV 13
typedef struct { int cat; const char *key, *name, *desc; int *var; int def; const char *vals[PREF_MAXV]; void (*apply)(void); } PrefDef;
extern Prefs g_prefs; extern unsigned g_prefs_rev;
void prefs_load(void); void prefs_save(void);
const PrefDef *prefs_table(int *n); int pref_nvals(const PrefDef *d); void pref_set(const PrefDef *d, int v);
int presets_count(void); const char *preset_name(int i); const char *preset_desc(int i); void preset_apply(int p);
float pref_anim_k(void); float pref_corner_k(void); int pref_panel_alpha(int a); float pref_toast_life(void);
int sys_cpu_temp(void);   // system.c
void clock_text(char *out, size_t n); void date_text(char *out, size_t n);   // home.c, secondo Personalizza
void custom_open(void); void custom_draw(float t); void custom_input(int b);   // custom.c: il pannello   // velatura del tema dentro uno sfondo (una volta, non a ogni fotogramma)
const char *theme_name(int i);
void theme_apply(int i, int save);
void theme_load(void);

enum { AL_L = 0, AL_C = 1, AL_R = 2 };
void gfx_init(void);
void gfx_frame(void);                 // scadenza della cache del testo
int  text_w(TTF_Font *f, const char *s);
int  draw_text(TTF_Font *f, const char *s, int x, int y, Col c, int alpha, int align);
int  draw_text_fit(TTF_Font *f, const char *s, int x, int y, int maxw, Col c, int alpha, int align);
int  draw_text_wrap(TTF_Font *f, const char *s, int x, int y, int maxw, int maxlines, int lineh, Col c, int alpha);
int  draw_text_wrap_al(TTF_Font *f, const char *s, int x, int y, int maxw, int maxlines, int lineh, Col c, int alpha, int align);
void fill_rect(int x, int y, int w, int h, Col c, int alpha);
typedef struct { int x, y, w, h; Col c; int a; } Veil;   // velo grande annotato (scena congelata, main.c)
extern int g_veil_skip, g_veil_rec, g_nveils; extern Veil g_veils[8];
void fill_rrect(int x, int y, int w, int h, int r, Col c, int alpha);
void stroke_rrect(int x, int y, int w, int h, int r, int t, Col c, int alpha);
void shadow_rrect(int x, int y, int w, int h, int r, int spread, int alpha);
void fill_circle(int cx, int cy, int rad, Col c, int alpha);
void ring(int cx, int cy, int rad, int t, Col c, int alpha);
void grad_v(int x, int y, int w, int h, Col top, int atop, Col bot, int abot);
void grad_h(int x, int y, int w, int h, Col l, int al, Col r, int ar);
void glow(int cx, int cy, int rad, Col c, int alpha);
void draw_tex(SDL_Texture *t, int x, int y, int w, int h, int alpha);
void draw_badge(int cx, int cy, int n, int alpha);
void draw_spinner(int cx, int cy, int rad, int alpha);
Col  avatar_col(int idx);
Col  mix(Col a, Col b, float t);
int  pill(int x, int y, int h, const char *label, int icon, int focused, float foc_anim, int alpha);   // ritorna la larghezza
void focus_ring(int x, int y, int w, int h, int r, float pulse, int alpha);

enum {
  IC_SEARCH, IC_GEAR, IC_BELL, IC_FRIENDS, IC_CHAT, IC_PARTY, IC_POWER, IC_PLUS, IC_CHECK,
  IC_CLOSE, IC_ADDUSER, IC_GAMEPAD, IC_NEWS, IC_MORE, IC_MIC, IC_MICOFF, IC_PLAY, IC_USER,
  IC_BTN_X, IC_BTN_O, IC_BTN_TRI, IC_BTN_SQ, IC_BTN_OPT, IC_ARROW_R, IC_SEND, IC_EXIT,
  IC_CLOCK, IC_STAR, IC_GLOBE, IC_BACK, IC_FWD, IC_RELOAD,
  IC_STORE, IC_DOWNLOAD, IC_LIKE, IC_DISLIKE,
  IC_MUSIC, IC_PAUSE, IC_NEXT, IC_PREV, IC_SHUFFLE, IC_REPEAT, IC_RADIO, IC_FOLDER, IC_VOLUME, IC_ALBUM, IC_DRIVE, IC_BRUSH, IC_COUNT
};
void draw_icon(int id, int cx, int cy, int size, Col c, int alpha);
void draw_logo(int cx, int cy, int size, int alpha);   // marchio di Omega (PNG incorporato)
// distanze con segno (coordinate 0..1) per icone e avatar disegnati a vettori
float sd_seg(float px, float py, float ax, float ay, float bx, float by);
float sd_box(float px, float py, float x, float y, float w, float h, float r);
float sd_tri(float px, float py, float x0, float y0, float x1, float y1, float x2, float y2);

// Avatar: foto/video caricati, personaggi illustrati (avatar.c) o colore + iniziale (gfx.c).
#define AV_ART_FIRST 16
#define AV_ART_COUNT 32
void draw_avatar(const char *oid, int avatar, int cx, int cy, int size, int alpha);
void draw_avatar_color(const char *oid, int avatar, int cx, int cy, int size, int alpha);
void media_note(const char *oid, const char *media, int frames);   // avatar personalizzato di un utente
void media_note_json(const char *oid, JVal *o);
const char *media_of(const char *oid, int *frames);
int  draw_media_frames(const char *media, int frames, int x, int y, int w, int h, int tw, int th, int radius, int animate, int alpha);

// Sfondo e scena
void bg_init(void);
void bg_set_default(void);
void bg_set_game(const char *tid, const char *art_path, Col avg);
void bg_draw(void);
int  bg_fading(void);
void particles_draw(int alpha);
extern int g_busy_motion;             // durante gli scorrimenti niente effetti costosi

// Animazioni
float approach(float cur, float target, float speed);   // smorzamento esponenziale
float ease_out(float t);
float ease_in_out(float t);
float clampf(float v, float a, float b);

// ------------------------------------------------------------- audio e voce --
enum { SFX_MOVE, SFX_SELECT, SFX_BACK, SFX_NOTIFY, SFX_LAUNCH, SFX_OPEN };
void audio_init(void);
void sfx_play(int kind);
void audio_pause(int p);
int  audio_music_on(void);
int  audio_sfx_on(void);
int  audio_music_level(void);
void audio_set(int music, int effects, int level);      // -1 = invariato
void audio_lock(int on);
void audio_external_music(int on);   // il lettore del demone suona: la musica d'ambiente tace

int  voice_fill(float *out48k, int frames);   // dalla callback audio: 1 se c'è voce da mescolare
void voice_tick(void);                        // avvia/ferma la voce insieme al party
void voice_shutdown(void);                    // chiusura dell'app: ferma e aspetta i thread
void system_shutdown(void);                   // ferma il thread di monitoraggio di Sistema
int  voice_state(void);                       // 0 spenta, 1 attiva, 2 solo ascolto
int  voice_speaking(const char *oid);
void mic_probe(void);                         // comando di debug "micprobe"

// ------------------------------------------------------------------ modello --
#define MAX_FRIENDS 128
#define MAX_NOTIF   64
#define MAX_REQ     64
#define MAX_MSG     80
#define MAX_NEWS    30
#define MAX_ACT     50
#define MAX_CONV    64
#define MAX_USERS   24
#define MAX_APPS    256
#define TID_COMMUNITY "OMEGACOMM"

typedef struct {
  char oid[32]; int avatar; char about[168];
  char status[12]; char game_id[24]; char game_name[100]; char started[32]; char last_seen[32];
  char status_msg[64];
} Friend;
typedef struct { char id[24]; char type[20]; char title[200]; char body[260]; char ref[64]; char actor[32]; int avatar; int read; char when[32]; } Notif;
typedef struct { char oid[32]; int avatar; char when[32]; char relation[16]; } UserRef;
typedef struct { char id[24]; char sender[32]; int avatar; char body[520]; int mine; char when[32]; int system; } Msg;
typedef struct { char oid[32]; int avatar; int owner; int muted; int talking; char status[12]; char game_name[100]; } PartyMember;
typedef struct {
  int active; char id[24]; char name[64]; int owner;
  int nmembers; PartyMember members[16];
  int ninvited; UserRef invited[16];
  char last_msg_id[24];
} Party;
typedef struct { char party_id[24]; char name[64]; char from[32]; int avatar; int members; } PartyInvite;
typedef struct { char id[24]; char title[220]; char body[620]; char tag[32]; char game_id[24]; int color; char when[32]; char link[300]; int has_image; } News;
typedef struct { char oid[32]; int avatar; char type[20]; char game_id[24]; char game_name[100]; char detail[100]; char when[32]; } Activity;
typedef struct { char oid[32]; int avatar; int unread; char last[220]; int from_me; char when[32]; } Conv;

typedef struct {
  char tid[16]; char name[96]; char icon[256]; char art[256];
  int hb; char dir[256]; char sub[96];        // homebrew websrv nella cartella dir (tid = "HB" + hash)
  int pld;                                    // payload ELF: dir è il percorso dell'elf (tid = "PL" + hash)
  int builtin;                                // tessera di Omega, non un'app: 1 = Community (sempre la prima)
  int ext; char src[300]; char drive[40];     // gioco su un disco esterno (drives.c): cartella e nome del disco
  long last_played;                           // per l'ordine "Ultimi giocati"
  SDL_Texture *tex; int tex_state; Col avg;   // tex_state: 0 nulla, 1 in caricamento, 2 pronta
  float appear;
} AppEntry;

typedef struct {
  char me[32]; int my_avatar; char my_about[168];
  char status_mode[12]; char status_msg[64]; int dnd;
  int unread_notif, unread_msg, in_req, out_req, unread_groups;
  int nfriends; Friend friends[MAX_FRIENDS];
  int nin; UserRef in[MAX_REQ];
  int nout; UserRef out[MAX_REQ];
  int nnotif; Notif notif[MAX_NOTIF];
  long last_notif_id; int synced_once;
  Party party; int ninv; PartyInvite inv[16];
  Msg pmsg[MAX_MSG]; int npmsg;
  int nnews; News news[MAX_NEWS];
  int nact; Activity act[MAX_ACT];
  int nconv; Conv conv[MAX_CONV];
  // scheda del gioco a fuoco
  char game_tid[16]; int game_loading;
  int ngame_now; UserRef game_now[16];
  int ngame_played; UserRef game_played[16];
  int game_players; int ngame_news; News game_news[8];
  int nusers; UserRef users[MAX_USERS]; int users_loading;
  long server_epoch; Uint32 server_ticks;     // ora del server all'ultimo sync
} Social;

extern Social S;
extern char g_token[700];
extern AppEntry apps[MAX_APPS];
extern int napps;
// dischi esterni (drives.c)
typedef struct { char mount[128], label[48]; double free_gb, total_gb; } Drive;
int drives_list(Drive *out, int max);                      // dischi collegati adesso
int drives_merge(AppEntry *apps, int n, int max);          // aggiunge i giochi trovati sui dischi
void drives_tick(void);                                    // avvia il controllo periodico
int drives_changed(void);                                  // 1 se i giochi esterni sono cambiati (disco collegato o tolto)
int drives_prepare_launch(const AppEntry *a, char *err, size_t en);   // monta e registra prima di avviare
void drives_after_game(void);                              // smonta i giochi esterni non più in esecuzione

long iso_epoch(const char *iso);
void rel_time(const char *iso, char *out, size_t n);       // "5 min fa"
void play_time(const char *iso, char *out, size_t n);      // "da 12 min"
int  friend_online(const Friend *f);
const Friend *friend_find(const char *oid);
int  friends_online_count(void);
int  notif_icon(const char *type);

// Azioni social (social.c): tutte asincrone.
void social_reset(void);
void social_sync_now(void);
void social_tick(void);
void social_load_friends(void);
void social_load_notifications(void);
void social_mark_notif_read(void);
void social_clear_notif(void);
void social_load_news(void);
void social_load_activity(void);
void social_load_conversations(void);
void social_load_game(const char *tid);
void social_search(const char *q);
void social_friend_request(const char *oid);
void social_friend_accept(const char *oid);
void social_friend_decline(const char *oid);
void social_friend_remove(const char *oid);
void social_party_create(void);
void social_party_invite(const char *oid);
void social_party_join(const char *party_id);
void social_party_decline(const char *party_id);
void social_party_leave(void);
void social_party_mute(int muted);
void social_party_send(const char *text);
void social_party_messages(void);
void social_presence(const char *status, const char *game_id, const char *game_name);
void social_profile_update(const char *about, int avatar);
void social_set_status(const char *mode, const char *message);
void social_block(const char *oid, int block);
void social_report_user(const char *oid, const char *reason);
void social_invite_game(const char *oid, const char *game_id, const char *game_name);
void status_menu(void);
void invite_to_game_menu(const char *game_id, const char *game_name);

typedef struct {
  int open; int party;                // chat del party o diretta
  char oid[32]; int avatar;
  Msg msg[MAX_MSG]; int nmsg; char last_id[24];
  int loading; float scroll, scroll_t; Uint32 last_poll;
} Chat;
extern Chat CH;
void chat_open(const char *oid, int avatar);
void chat_open_party(void);
void chat_poll(void);
void chat_send(const char *text);

typedef struct {
  int loading; int loaded;
  char oid[32]; int avatar; char about[168]; char relation[16]; int friends_count;
  char cover_media[20]; int cover_frames;
  char status[12]; char game_name[100]; char started[32]; char created[32];
  int nrecent; Activity recent[8];
  int ngames; struct { char game_id[24]; char game_name[100]; char last[32]; int sessions; } games[6];
  char status_msg[64]; int mutual_friends, nmutual; char mutual[5][32]; int blocked; long total_seconds;
  struct { int hidden, sets, p, g, s, b, points; } trophies;      // hidden: l'utente non li mostra a chi guarda
} Profile;
extern Profile PR;
void profile_open(const char *oid);

// ------------------------------------------------------------ livelli di UI --
enum { B_UP, B_DOWN, B_LEFT, B_RIGHT, B_X, B_O, B_TRI, B_SQ, B_OPT, B_L1, B_R1, B_L2, B_R2, B_COUNT };

typedef enum { SC_SPLASH, SC_USERS, SC_LOGIN, SC_REGISTER, SC_HOME } Scene;
extern Scene g_scene;
extern int g_ui_fg; extern Uint32 g_ui_fg_since;   // UI in primo piano (main.c)
void scene_set(Scene s);

// Pannelli sopra la home, gestiti come una pila.
typedef enum {
  OV_NONE, OV_CC, OV_GAMEBASE, OV_NOTIF, OV_PROFILE, OV_CHAT, OV_SEARCH, OV_MENU, OV_CONFIRM, OV_AVATAR,
  OV_SETTINGS, OV_NEWS, OV_BROWSER, OV_GALLERY, OV_STORE, OV_DOC, OV_COMMUNITY, OV_ABOUT, OV_MUSIC, OV_SYSTEM, OV_FILES, OV_REMOTE, OV_SETUP, OV_WHATSNEW, OV_CUSTOM, OV_TROPHIES
} Overlay;
void ov_push(Overlay o);
void ov_pop(void);
Overlay ov_top(void);
int  ov_depth(void);
void ov_clear(void);

typedef void (*MenuFn)(int idx, void *ud);
void menu_open(const char *title, const char **items, int n, MenuFn fn, void *ud);   // al massimo 32 voci
void menu_select(int i);              // voce a fuoco del menu appena aperto
void confirm_open(const char *msg, const char *yes, MenuFn fn, void *ud);
void toast(int icon, const char *actor, int avatar, const char *title, const char *body);
void hints(const int *icons, const char **labels, int n, int alpha);   // barra dei comandi in basso

// Scene
extern int g_boot_state;              // 0 verifica della sessione, 1 sessione valida, 2 serve l'accesso
void splash_enter(void);
void splash_update(void); void splash_draw(void);
void users_draw(void); void users_input(int b);
void login_draw(void); void login_input(int b);
void register_draw(void); void register_input(int b);
void home_enter(void);
void home_update(void); void home_draw(void); void home_input(int b);

// Pannelli (t = avanzamento dell'animazione di apertura, 0..1)
void cc_draw(float t); void cc_input(int b);
void gb_open(int tab); void gb_draw(float t); void gb_input(int b);
void notif_draw(float t); void notif_input(int b);
void profile_draw(float t); void profile_input(int b);
void chat_draw(float t); void chat_input(int b);
void search_open(void); void search_draw(float t); void search_input(int b);
void menu_draw(float t); void menu_input(int b);
void confirm_draw(float t); void confirm_input(int b);
void avatar_draw(float t); void avatar_input(int b);
void settings_draw(float t); void settings_input(int b);
void about_draw(float t); void about_input(int b);   // Informazioni su Omega
void news_open(const News *n); void news_draw(float t); void news_input(int b);
void news_art(const News *n, int x, int y, int w, int h, int radius, int alpha);
void gallery_open(int kind); void gallery_draw(float t); void gallery_input(int b);
void gallery_tick(void); void upload_overlay(void);
void browser_open(const char *url); void browser_draw(float t); void browser_input(int b);
void store_open(void); void store_draw(float t); void store_input(int b);
// Musica (music.c): la suona il demone, anche durante i giochi
void music_open(void); void music_draw(float t); void music_input(int b);
void music_tick(void);
int  music_playing(void);
const char *music_now_line(void);     // "Titolo · Artista" del brano in corso, NULL se fermo
void music_toggle(void);
void music_mini(int x, int y, int alpha);
// Sistema e gestore dei file (system.c)
void system_open(void); void system_draw(float t); void system_input(int b);
void files_open(const char *start); void files_draw(float t); void files_input(int b);
void files_tick(void); void files_overlay(void);
// Telecomando dal telefono (remote.c)
void remote_open(void); void remote_draw(float t); void remote_input(int b);
int  console_ip(char *out, size_t n);
// Prima configurazione (setup.c) e jailbreak/componenti (hen.c)
int  home_mode(void);                 // 1 Omega come Home, 0 solo app, -1 non ancora scelto
void home_mode_set(int on);
void home_mode_menu(void);
void setup_tick(void);
void hen_check(int ask);              // rileva il jailbreak e propone i componenti mancanti
const char *hen_name(void);           // "OnionHEN", "etaHEN"... per Sistema
int  hen_daemon_path(char *out, size_t n);   // servizio attivo (1 = trovato)
int  hen_payload_dir(char *out, size_t n);   // cartella piatta dei payload per l'HEN (0 = Payload Manager)
void setup_draw(float t); void setup_input(int b);
void hen_ask_home(void);              // domanda "Omega come Home?" (stessa finestra)
void community_open(int tab); void community_open_post(const char *post_id);
void community_draw(float t); void community_input(int b);
// misure e pezzi comuni alle schede della Community (community.c, records.c)
#define CM_X   140
#define CM_W   (SCREEN_W - 2 * CM_X)
#define CM_TOP 200
void cm_card(int x, int y, int w, int h, int foc, int a);
void cm_chip(int x, int y, const char *label, Col c, int a);
void cm_hours(long secs, char *out, size_t n);             // "45 min", "3.5 h", "120 h"
// Record e Trofei pubblici (records.c): trophies = 0 scheda Record, 1 scheda Trofei
void records_enter(int trophies); void records_load(int trophies); void records_draw(int trophies, int a);
int  records_input(int trophies, int b);                   // 1 = tasto gestito
int  records_hints(int trophies, int *ic, const char **lb);
int  trophy_counts(int x, int y, int p, int g, int s, int b, int a);   // quattro coppe con i conteggi; ritorna la larghezza
void trophies_open(const char *oid); void trophies_draw(float t); void trophies_input(int b);
// Giochi e trofei della console verso il server (consync.c)
void consync_start(int force); void consync_stop(void); int consync_busy(void); int consync_rev(void);
void doc_open(const char *kind); void doc_draw(float t); void doc_input(int b);   // "privacy" | "terms" | "licenses"

// Privacy, termini e server (account.c)
void privacy_menu(void);
void server_menu(void);
const char *server_label(void);
void terms_check(JVal *me);           // dalla risposta di GET /me
void terms_refresh(void);
void terms_tick(void);
void pcmlink_init(void); int pcmlink_mix(float *l, float *r, int frames); int pcmlink_active(void);   // pcmlink.c: musica del servizio
void whatsnew_tick(void); void whatsnew_draw(float t); void whatsnew_input(int b);   // whatsnew.c

// ---------------------------------------------------------- launcher (home) --
void scan_apps(void);
void launch_app(int idx);
// homebrew in formato websrv (hblaunch.c)
int  hb_meta(const char *dir, char *name, size_t n, char *sub, size_t sn);
int  hb_launch(const char *dir, int choice, int dry, char *err, size_t en, char (*names)[64], int maxn, int *nn);
int  hb_remove(const char *dir, const char *root);
int  hb_icon(const char *dir, char *out, size_t n);
// payload ELF (payload.c)
int  payload_scan(AppEntry *list, int n, int max);
int  payload_run(const char *elf, char *err, size_t en);
int  payload_run_service(const char *elf, char *err, size_t en);   // solo elfldr (vedi payload.c)
int  payload_remove(const char *elf);

// ----------------------------------------------------- store: installazione --
// kind: 0 = dedotto dal file, 1 = pkg, 2 = zip, 3 = elf. url = link diretto.
typedef struct {
  int kind; char url[2048]; char filename[160]; char name[96]; char title_id[16];
  char version[24]; char category[24]; char desc[200]; char source[300]; long size;
  char dest_mount[128], dest_label[48];   // gioco su disco esterno (scelto all'installazione); vuoto = memoria interna
} InstallReq;
void install_begin(const InstallReq *r);
void install_tick(void);
void install_overlay(void);
int  install_busy(void);
int  store_uninstall(const char *title_id);   // 0 = ok

// Aggiornamenti automatici (update.c, solo nella build ufficiale)
void update_init(void);
void update_tick(void);

// ------------------------------------------------------- sessione e account --
extern char f_user[64], f_pass[128], f_email[96], f_confirm[128];
extern char g_host_tid[64];
void after_login(JVal *j);
void boot_session(void);
void do_logout(void);
void app_quit(void);
void app_quit_later(Uint32 ms);
void set_msg(const char *m, int err);
int  edit_text(const char *title, char *buf, size_t n, int pw);
void render_frame(void);
