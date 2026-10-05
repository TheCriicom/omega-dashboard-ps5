// Omega UI — preferenze di personalizzazione (Impostazioni › Personalizza).
// Ogni opzione è una riga della tabella PREFS: categoria, nome, spiegazione,
// valori. Il pannello (custom.c) si costruisce da qui, e ogni opzione è letta
// dal codice che disegna o suona (g_prefs.*). Si salvano in OMEGA_DIR/prefs.txt,
// una riga "chiave=valore": un valore sconosciuto o fuori scala torna al
// predefinito, così una versione vecchia o nuova non si rompe a vicenda.
#include "app.h"
#include <stdlib.h>

#define PREFS_FILE OMEGA_DIR "/prefs.txt"

Prefs g_prefs;
unsigned g_prefs_rev;        // cresce a ogni cambio: la scena congelata si rifà

static void apply_theme(void) { theme_apply(g_prefs.theme, 1); }
static void apply_bg(void) { bg_refresh(); }

#define V(...) { __VA_ARGS__, NULL }
static const PrefDef PREFS[] = {
  // ------------------------------------------------------------- aspetto --
  { PC_LOOK, "theme", N_("Tema"), N_("I colori di tutta Omega: sfondo, pannelli e luci."), &g_prefs.theme, 0,
    V(N_("Omega blu"), N_("Notte"), N_("Aurora"), N_("Tramonto"), N_("Ametista"), N_("Carbone"),
      N_("Neon"), N_("Smeraldo"), N_("Rubino"), N_("Oro"), N_("Ghiaccio"), N_("Mezzanotte")), apply_theme },
  { PC_LOOK, "accent", N_("Colore d'accento"), N_("Il colore di pulsanti, barre e selezioni. «Del tema» segue il tema."), &g_prefs.accent, 0,
    V(N_("Del tema"), N_("Azzurro"), N_("Viola"), N_("Rosa"), N_("Verde"), N_("Arancio"), N_("Oro"), N_("Bianco")), apply_theme },
  { PC_LOOK, "bg", N_("Sfondo"), N_("Cosa c'è dietro la home. «Immagine personale»: metti wallpaper.jpg o wallpaper.png in /data/Omega."), &g_prefs.bg_style, 0,
    V(N_("Copertina del gioco"), N_("Sfondo del tema"), N_("Nero puro"), N_("Immagine personale")), apply_bg },
  { PC_LOOK, "bg_dim", N_("Oscuramento dello sfondo"), N_("Più scuro, più leggibili i testi sopra le copertine."), &g_prefs.bg_dim, 1,
    V(N_("Nessuno"), N_("Leggero"), N_("Medio"), N_("Forte")), apply_bg },
  { PC_LOOK, "glass", N_("Pannelli"), N_("Quanto si vede lo sfondo attraverso schede e notifiche."), &g_prefs.glass, 1,
    V(N_("Vetro"), N_("Normali"), N_("Pieni")), NULL },
  { PC_LOOK, "corners", N_("Angoli"), N_("La forma di schede, pulsanti e icone."), &g_prefs.corners, 1,
    V(N_("Squadrati"), N_("Arrotondati"), N_("Molto arrotondati")), NULL },
  { PC_LOOK, "focus", N_("Selezione"), N_("Come si vede l'elemento scelto col controller."), &g_prefs.focus, 0,
    V(N_("Anello bianco"), N_("Anello colorato"), N_("Bagliore"), N_("Anello e bagliore")), NULL },
  // ---------------------------------------------------------------- home --
  { PC_HOME, "tiles", N_("Dimensione delle icone"), N_("Le icone dei giochi nella fila della home."), &g_prefs.tiles, 1,
    V(N_("Piccole"), N_("Medie"), N_("Grandi")), NULL },
  { PC_HOME, "labels", N_("Nomi sotto le icone"), N_("Il nome di ogni gioco sotto la sua icona, non solo di quello scelto."), &g_prefs.labels, 0,
    V(N_("No"), N_("Sì")), NULL },
  { PC_HOME, "sort", N_("Ordine dei giochi"), N_("Come si mettono in fila giochi e app."), &g_prefs.sort, 0,
    V(N_("Nome (A-Z)"), N_("Ultimi giocati"), N_("Prima i giochi, poi gli homebrew")), NULL },
  { PC_HOME, "show_hb", N_("Homebrew e payload in home"), N_("Mostra anche gli homebrew e i payload accanto ai giochi."), &g_prefs.show_hb, 1,
    V(N_("No"), N_("Sì")), NULL },
  { PC_HOME, "ext_games", N_("Giochi sui dischi esterni"), N_("I giochi trovati su HDD, SSD e chiavette collegati compaiono in home con l'icona del disco."), &g_prefs.ext_games, 1,
    V(N_("No"), N_("Sì")), NULL },
  { PC_HOME, "cards", N_("Sotto il gioco scelto"), N_("Le schede con amici che giocano, notizie e attività."), &g_prefs.cards, 1,
    V(N_("Niente"), N_("Amici e notizie")), NULL },
  { PC_HOME, "hints", N_("Barra dei comandi"), N_("I suggerimenti dei tasti in basso."), &g_prefs.hints, 1,
    V(N_("Mai"), N_("Per 10 secondi"), N_("Sempre")), NULL },
  // ------------------------------------------------------ orologio e barra --
  { PC_BAR, "clock", N_("Formato dell'ora"), N_("L'orologio in alto a destra."), &g_prefs.clock12, 0,
    V(N_("24 ore"), N_("12 ore")), NULL },
  { PC_BAR, "clock_sec", N_("Secondi"), N_("Mostra anche i secondi."), &g_prefs.clock_sec, 0,
    V(N_("No"), N_("Sì")), NULL },
  { PC_BAR, "clock_date", N_("Data"), N_("Il giorno accanto all'ora."), &g_prefs.clock_date, 0,
    V(N_("No"), N_("Sì")), NULL },
  { PC_BAR, "bar_temp", N_("Temperatura della console"), N_("La temperatura del processore accanto all'ora."), &g_prefs.bar_temp, 0,
    V(N_("No"), N_("Sì")), NULL },
  // ------------------------------------------------------------ movimento --
  { PC_MOTION, "particles", N_("Particelle sullo sfondo"), N_("Le luci che salgono piano dietro la home."), &g_prefs.particles, 2,
    V(N_("Nessuna"), N_("Poche"), N_("Normali"), N_("Tante")), NULL },
  { PC_MOTION, "particle_style", N_("Tipo di particelle"), N_("Che forma hanno e come si muovono."), &g_prefs.particle_style, 0,
    V(N_("Luci"), N_("Stelle"), N_("Neve"), N_("Lucciole"), N_("Bolle")), NULL },
  { PC_MOTION, "anim", N_("Velocità delle animazioni"), N_("«Istantanee» toglie quasi del tutto i movimenti."), &g_prefs.anim, 1,
    V(N_("Rilassate"), N_("Normali"), N_("Rapide"), N_("Istantanee")), NULL },
  { PC_MOTION, "fps", N_("Fluidità"), N_("«Automatica» passa a 30 fotogrammi quando la home è ferma: la console scalda meno."), &g_prefs.fps, 2,
    V(N_("60 fotogrammi"), N_("30 fotogrammi"), N_("Automatica")), NULL },
  // ----------------------------------------------------------------- suoni --
  { PC_SOUND, "sfx_pack", N_("Suoni dell'interfaccia"), N_("Il suono di spostamenti, scelte e notifiche."), &g_prefs.sfx_pack, 0,
    V(N_("Classici"), N_("Morbidi"), N_("Retrò 8 bit"), N_("Cristallo"), N_("Nessun suono")), NULL },
  { PC_SOUND, "music_mood", N_("Musica di sottofondo"), N_("La musica generata della home, quando non ascolti altro."), &g_prefs.music_mood, 0,
    V(N_("Serena"), N_("Notturna"), N_("Spaziale"), N_("Calda"), N_("Spenta")), NULL },
  // -------------------------------------------------------------- notifiche --
  { PC_NOTIF, "toast_pos", N_("Posizione delle notifiche"), N_("Dove compaiono messaggi, inviti e richieste."), &g_prefs.toast_pos, 0,
    V(N_("In alto a destra"), N_("In alto a sinistra"), N_("In basso a destra")), NULL },
  { PC_NOTIF, "toast_time", N_("Durata delle notifiche"), N_("Quanto restano a schermo."), &g_prefs.toast_time, 1,
    V(N_("3 secondi"), N_("5 secondi"), N_("8 secondi")), NULL },
  { PC_NOTIF, "dnd", N_("Non disturbare"), N_("«Silenziose»: senza suono. «Nascoste»: non compaiono (restano nel centro notifiche)."), &g_prefs.dnd, 0,
    V(N_("No"), N_("Silenziose"), N_("Nascoste")), NULL },
  // ---------------------------------------------------------- salvaschermo --
  { PC_SAVER, "saver", N_("Salvaschermo"), N_("Dopo quanto tempo senza toccare il controller."), &g_prefs.saver_min, 0,
    V(N_("Mai"), N_("2 minuti"), N_("5 minuti"), N_("10 minuti")), NULL },
  { PC_SAVER, "saver_style", N_("Stile del salvaschermo"), N_("Cosa si vede mentre è attivo. Basta un tasto per tornare."), &g_prefs.saver_style, 0,
    V(N_("Orologio grande"), N_("Copertine che scorrono"), N_("Schermo nero")), NULL },
  // ----------------------------------------------------------- prestazioni --
  { PC_PERF, "freeze", N_("Scena ferma dietro i pannelli"), N_("Con un pannello aperto la home sotto non si ridisegna: metà lavoro per la console."), &g_prefs.freeze, 1,
    V(N_("No"), N_("Sì")), NULL },
};
#define NPREFS (int)(sizeof PREFS / sizeof *PREFS)

const PrefDef *prefs_table(int *n) { *n = NPREFS; return PREFS; }
int pref_nvals(const PrefDef *d) { int n = 0; while (n < PREF_MAXV && d->vals[n]) n++; return n; }

static void defaults(void) { for (int i = 0; i < NPREFS; i++) *PREFS[i].var = PREFS[i].def; }

void prefs_save(void) {
  FILE *f = fopen(PREFS_FILE ".tmp", "w"); if (!f) return;
  for (int i = 0; i < NPREFS; i++) fprintf(f, "%s=%d\n", PREFS[i].key, *PREFS[i].var);
  fclose(f);
  rename(PREFS_FILE ".tmp", PREFS_FILE);
}

void prefs_load(void) {
  defaults();
  // tema scelto con le versioni di prima (theme.txt)
  FILE *t = fopen(OMEGA_DIR "/theme.txt", "r");
  if (t) { int v; if (fscanf(t, "%d", &v) == 1 && v >= 0 && v < N_THEMES) g_prefs.theme = v; fclose(t); }
  FILE *f = fopen(PREFS_FILE, "r");
  if (f) {
    char line[128];
    while (fgets(line, sizeof line, f)) {
      char *eq = strchr(line, '='); if (!eq) continue;
      *eq = 0; int v = atoi(eq + 1);
      for (int i = 0; i < NPREFS; i++) if (!strcmp(PREFS[i].key, line)) { if (v >= 0 && v < pref_nvals(&PREFS[i])) *PREFS[i].var = v; break; }
    }
    fclose(f);
  }
  g_prefs_rev++;
}

void pref_set(const PrefDef *d, int v) {
  int n = pref_nvals(d);
  if (v < 0) v = n - 1; if (v >= n) v = 0;
  *d->var = v;
  g_prefs_rev++;
  if (d->apply) d->apply();
  prefs_save();
}

// ----------------------------------------------------------- stili rapidi --
typedef struct { const char *name, *desc; const char *set; } Preset;
static const Preset PRESETS[] = {
  { N_("Omega"), N_("Com'era all'inizio."), "" },
  { N_("Minimal"), N_("Niente particelle, pannelli pieni, angoli squadrati, nomi sotto le icone."),
    "particles=0 glass=2 corners=0 labels=1 cards=0 hints=0 sfx_pack=1 music_mood=4 focus=0" },
  { N_("Neon"), N_("Tema Neon, tante luci, bagliore sulla selezione, suoni cristallo."),
    "theme=6 accent=3 particles=3 particle_style=0 focus=3 glass=0 corners=2 sfx_pack=3 music_mood=2" },
  { N_("Zen"), N_("Toni notturni, neve lenta, animazioni rilassate, musica spaziale."),
    "theme=1 accent=0 particles=2 particle_style=2 anim=0 sfx_pack=1 music_mood=2 bg_dim=2 hints=0" },
  { N_("Retrò"), N_("Suoni 8 bit, angoli squadrati, colori caldi."),
    "theme=3 accent=6 corners=0 sfx_pack=2 music_mood=3 particle_style=4 focus=1" },
  { N_("Cinema"), N_("Nero puro, niente distrazioni, salvaschermo con le copertine."),
    "theme=11 bg=2 particles=0 glass=2 hints=0 cards=0 saver=2 saver_style=1 music_mood=4" },
  { N_("Prestazioni"), N_("Il minimo lavoro per la console: utile se scalda o rallenta."),
    "particles=0 fps=2 freeze=1 glass=2 anim=2" },
};
#define NPRESETS (int)(sizeof PRESETS / sizeof *PRESETS)
int presets_count(void) { return NPRESETS; }
const char *preset_name(int i) { return _(PRESETS[i].name); }
const char *preset_desc(int i) { return _(PRESETS[i].desc); }

void preset_apply(int p) {
  if (p < 0 || p >= NPRESETS) return;
  int theme0 = g_prefs.theme, accent0 = g_prefs.accent, bg0 = g_prefs.bg_style, dim0 = g_prefs.bg_dim;
  defaults();
  if (p != 0) { g_prefs.theme = theme0; g_prefs.accent = accent0; }   // gli stili non toccano il tema se non lo dicono
  char buf[256]; snprintf(buf, sizeof buf, "%s", PRESETS[p].set);
  for (char *tok = strtok(buf, " "); tok; tok = strtok(NULL, " ")) {
    char *eq = strchr(tok, '='); if (!eq) continue;
    *eq = 0; int v = atoi(eq + 1);
    for (int i = 0; i < NPREFS; i++) if (!strcmp(PREFS[i].key, tok)) { if (v >= 0 && v < pref_nvals(&PREFS[i])) *PREFS[i].var = v; break; }
  }
  g_prefs_rev++;
  theme_apply(g_prefs.theme, 1);
  if (g_prefs.bg_style != bg0 || g_prefs.bg_dim != dim0) bg_refresh();
  prefs_save();
}

// ---------------------------------------------------- valori già calcolati --
float pref_anim_k(void) { static const float K[4] = { 0.65f, 1.0f, 1.6f, 6.0f }; return K[g_prefs.anim & 3]; }
float pref_corner_k(void) { static const float K[3] = { 0.3f, 1.0f, 1.45f }; return K[g_prefs.corners % 3]; }
int pref_panel_alpha(int a) { static const int P[3] = { 70, 100, 118 }; int v = a * P[g_prefs.glass % 3] / 100; return v > 255 ? 255 : v; }
float pref_toast_life(void) { static const float T[3] = { 3.0f, 5.0f, 8.0f }; return T[g_prefs.toast_time % 3]; }
