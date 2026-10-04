# Omega — app per la console

Omega è una dashboard social per PS5 con jailbreak: home in stile console, Store
di homebrew, community (bacheca, gruppi, classifiche), party con chat vocale,
messaggi e browser. È un'app SDL2 a schermo intero, scritta in C, che parla con
un server Omega via HTTP(S).

Sviluppato da **TheCriicom** ([outlinedigital.it](https://outlinedigital.it)).
Licenza GPL-3.0-or-later (`LICENSE`); componenti di terze parti in
`THIRD-PARTY-NOTICES.md`. Crediti, versione e sorgente si vedono anche
nell'app, in Impostazioni → Informazioni su Omega.

## Compilare per PS5

Serve [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) con i pacchetti
homebrew (SDL2, SDL2_ttf, SDL2_image, freetype, minizip, quickjs, opus) e LLVM 18.

```sh
PS5_PAYLOAD_SDK=$HOME/ps5-payload-sdk ./build.sh
```

Il risultato è `../bin/omega_ui.elf`. `install-ps5.sh` lo copia sulla console via
FTP (ftpsrv, porta 2121) in `/data/homebrew/OmegaUI/` insieme a `homebrew.js` e
all'icona, così websrv lo mostra tra gli homebrew; con `--launch` lo avvia
subito. Copia anche il demone `omega_redirect` (vedi `../omega-redirect-src`).

## Build desktop di prova (macOS)

Per lavorare sulla UI senza console. Servono SDL2, SDL2_ttf, SDL2_image e curl
di Homebrew.

```sh
cd desktop
OMEGA_URL=http://127.0.0.1:18080 OMEGA_DATA=/tmp/omega ./build-desktop.sh
./omega-ui-desktop                    # OMEGA_HIDDEN=1 per non aprire la finestra
```

Facoltativi: `MINIZIP_SRC=<cartella con minizip/unzip.c>` per provare davvero le
installazioni dello Store, `QUICKJS_INC=<dir> QUICKJS_LIB=<libquickjs.a>` per
eseguire gli `homebrew.js`. Sul desktop la rete passa da libcurl
(`desktop/desktop.c` sostituisce `net.c` e `ime.c`), il filesystem della console è
simulato in `$OMEGA_DATA/sysroot` e i giochi si leggono da `$OMEGA_DATA/apps`.

### Comandi di debug

L'app legge `OMEGA_DIR/omega-ui.cmd` (sul desktop `$OMEGA_DATA/omega-ui.cmd`), un
comando per riga, e cancella il file dopo averlo letto:

| comando | effetto |
|---|---|
| `key x\|o\|tri\|sq\|opt\|up\|down\|left\|right\|l1\|r1` | preme un tasto |
| `text <valore>` | testo da restituire alla prossima tastiera (coda di 4) |
| `shot [file.png]` | screenshot (predefinito `OMEGA_DIR/omega-ui-shot.png`) |
| `wait <ms>` | pausa prima del comando successivo |
| `micprobe` | apre il microfono per 2 s e scrive il picco nel log |
| `quit` | chiude l'app |

Funziona anche sulla console: si carica il file via FTP.

## Lingue

L'app è tradotta in 27 lingue: `it en ja fr es de nl pt-PT pt-BR ru ko zh-Hans
zh-Hant fi sv da nb pl tr cs hu el ro th vi id uk` (gli stessi codici valgono per
i file, per le impostazioni e per l'header `Accept-Language`).

- **Testi.** Nel sorgente ogni testo visibile è l'italiano esatto dentro `_()`
  (`N_()` per gli elenchi statici, tradotti quando si disegnano; `P_("contesto",
  "testo")` quando la stessa parola italiana ha due significati). `_()` ritorna la
  traduzione o, se manca, l'italiano. I log (`omega_log`), le chiavi JSON e i
  valori del server non si traducono.
- **Cataloghi.** `i18n/<codice>.json` è un oggetto piatto `{ "msgid": "traduzione" }`;
  `i18n/it.json` è l'elenco dei msgid e `i18n/_context.json` dice per ognuno
  dove compare, che cos'è (pulsante, voce di menu, messaggio...) e quanto può
  essere lungo. Una traduzione vuota o assente lascia l'italiano.
- **Script** (Node, senza dipendenze):
  - `node tools/i18n-extract.mjs` rilegge i sorgenti (UI, `omega-redirect-src`,
    `omega-installer-src`), aggiorna `it.json` e `_context.json` e crea `{}` per
    le lingue nuove (`--prune` toglie le chiavi non più usate);
  - `node tools/i18n-gen.mjs` scrive `source/i18n_data.c` e gli `i18n_data.c`
    del demone e dell'installer. Si ferma se un JSON non è valido o se una
    traduzione non ha gli stessi specificatori printf del msgid (`%s %d %ld
    %.1f %%`..., stesso ordine e tipo). `build.sh` e `desktop/build-desktop.sh`
    lo eseguono se c'è Node; altrimenti si usa il file già generato.
- **Scelta.** Impostazioni → Lingua: "Automatica (console)" segue la lingua di
  sistema (`sceSystemServiceParamGetInt(1)`, il valore grezzo finisce nel log);
  le altre voci valgono subito e si salvano in `OMEGA_DIR/lang.txt`. Sul
  desktop la lingua automatica viene da `OMEGA_LANG` (es. `zh-Hant`) o da `LANG`.
- **Font.** All'avvio si elencano i font di `/preinst/common/font/`: le stringhe
  con caratteri che i font SST non hanno (giapponese, cinese, coreano, thai...)
  si disegnano e si misurano col primo font che li ha; per il thai si imposta
  lo script di HarfBuzz. Il cinese e il giapponese vanno a capo anche tra un
  ideogramma e l'altro. Sul desktop si usano i font di macOS.

## Configurazione

Macro da passare al compilatore (`-D`):

| macro | predefinito | uso |
|---|---|---|
| `OMEGA_BASE_URL` | `https://play.omegasuite.it` | server predefinito ("Omega") |
| `OMEGA_API` | `/api/v1` | prefisso delle API |
| `OMEGA_DIR` | `/data/Omega` | cartella dati; se la si ridefinisce non c'è migrazione |
| `OMEGA_VERSION` | in `source/omega.h` | versione dell'app |
| `OMEGA_REG_KEY` | in `source/login.c` | chiave che il server chiede per registrarsi |

Le prime versioni usavano `/data/OmegaPSNLab`: all'avvio, se esiste e
`/data/Omega` no, la cartella viene rinominata. Il demone fa lo stesso
controllo, perché legge `session.json` e `ui-active` dalla stessa cartella.

## Server

"Omega" (`OMEGA_BASE_URL`) è sempre nell'elenco e non si può togliere.
Da Impostazioni → Server, o dalla riga "Server" della schermata di accesso, se ne
aggiungono altri inserendo l'indirizzo (`https://` è implicito). Elenco e
scelta stanno in `OMEGA_DIR/servers.json`; la sessione salvata vale solo per il
server su cui è nata, e cambiando server si esce dall'account. Gli
aggiornamenti automatici arrivano sempre da `OMEGA_BASE_URL`.

## File e log

In `OMEGA_DIR` (sulla console `/data/Omega`):

- `omega-ui.log` — log dell'app (anche i tempi dei fotogrammi ogni 15 s)
- `omega-redirect.log` — log del demone
- `session.json` — token della sessione (mai la password)
- `ui-active` — aggiornato ogni 2 s mentre la UI è in primo piano
- `servers.json`, `theme.txt`, `audio.txt`, `lang.txt`, `browser-*.txt` — preferenze
- `update/`, `update.json` — stato degli aggiornamenti (solo build ufficiale)

## Struttura del sorgente

| file | contenuto |
|---|---|
| `omega.h` | configurazione, percorsi, rete, sessione, tastiera |
| `app.h` | stato dell'app, modello social, grafica, pannelli |
| `main.c` | avvio, ciclo principale, input, pila dei pannelli, toast, comandi di debug |
| `net.c` | client HTTP(S) con SceHttp |
| `async.c` | coda di rete e caricatore di immagini in background |
| `json.c`, `json.h` | parser JSON minimo |
| `i18n.c`, `i18n.h` | traduzioni: `_()`, scelta e rilevamento della lingua |
| `i18n_data.c` | tabelle delle traduzioni, generate da `tools/i18n-gen.mjs` |
| `util.c` | lettura file, codifica URL, FNV-1a, SHA-256 |
| `session.c` | sessione salvata |
| `servers.c`, `servers.h` | elenco dei server |
| `ime.c` | tastiera di sistema (sceImeDialog) |
| `gfx.c` | renderer: testo (con i font di ripiego), forme, icone SDF, marchio, temi, sfondi, particelle |
| `logo_png.c` | marchio di Omega (PNG incorporato) |
| `avatar.c` | avatar personalizzati e illustrati |
| `audio.c` | musica generativa ed effetti sintetizzati |
| `voice.c` | voce del party (Opus su PS5, ADPCM sul desktop) |
| `social.c` | amici, notifiche, party, chat, profilo, presenza |
| `login.c` | splash, scelta dell'utente, accesso, registrazione |
| `home.c` | home: giochi, homebrew, payload, notizie, attività |
| `panels.c` | Centro di controllo, Game Base, notifiche, profilo, chat, ricerca, impostazioni, informazioni |
| `community.c` | bacheca, gruppi, persone, tempo di gioco |
| `store.c` | Store: homebrew, libreria, dettaglio, pubblicazione |
| `install.c` | installazione di pkg, zip ed elf |
| `hblaunch.c` | avvio degli homebrew websrv eseguendo `homebrew.js` in QuickJS |
| `payload.c` | payload di Payload Manager (elfldr o websrv) |
| `gallery.c` | catture e chiavette USB, caricamento di avatar e copertine |
| `browser.c` | browser a blocchi impaginati dal server |
| `account.c` | documenti legali, privacy, dati, scelta del server |
| `update.c` | aggiornamenti automatici firmati (solo nella build ufficiale) |
| `monocypher*.c/h` | Monocypher (terze parti), verifica Ed25519 degli aggiornamenti |

`update.c` e `publish-update.sh` non fanno parte del sorgente pubblico: se
`update.c` manca, CMake e `build-desktop.sh` compilano senza `OMEGA_UPDATES`.
