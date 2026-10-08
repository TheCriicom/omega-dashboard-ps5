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

## Personalizza

Impostazioni › Personalizza (`source/custom.c`, opzioni in `source/prefs.c`):
più di 30 opzioni in 8 categorie (aspetto, home, orologio e barra, movimento,
suoni, notifiche, salvaschermo, prestazioni) e 7 stili rapidi. Ogni opzione è
una riga della tabella `PREFS` con i suoi valori; il codice le legge da
`g_prefs`. Si salvano in `OMEGA_DIR/prefs.txt` (`chiave=valore`; un valore
sconosciuto torna al predefinito). Sfondo personale: `/data/Omega/wallpaper.jpg`
o `.png`.

## Giochi sui dischi esterni

`source/drives.c` guarda ogni 4 s `/mnt/usb0..7`, `/mnt/ext0` e `/mnt/ext1` e
cerca le cartelle di gioco (`sce_sys/param.json` o `param.sfo` più `eboot.bin`)
nella radice e in `homebrew/`, `etaHEN/games/`, `games/`, `PS5/`, `PS4/`. I
giochi compaiono in home con l'icona del disco e spariscono quando il disco si
scollega. All'avvio la cartella si monta in sola lettura (nullfs) su
`/system_ex/app/<TID>`, e la prima volta si registra il titolo; tornando in
Omega si smonta. Sul disco non si scrive e non si cancella mai niente. Serve
kstuff, come per ShadowMount e dump_runner. Installando un gioco a cartella o
zip con un disco collegato, Omega chiede dove metterlo (`<disco>/homebrew/<TID>`).

## Avvio degli homebrew

Gli homebrew in formato websrv partono con `GET /hbldr` di websrv
(`source/hblaunch.c`), che chiude l'app in primo piano e avvia l'ELF dentro
`FAKE00000`. Omega quindi **non si chiude da sola**: se lo facesse mentre websrv
la sta chiudendo, la chiamata fallirebbe (503) e non partirebbe niente. Prima
dell'avvio si controlla che websrv risponda (`/version`), si rendono
eseguibili gli ELF arrivati senza permessi, la cartella di lavoro è quella
dell'homebrew se lo script non ne indica una, e si scrive `OMEGA_DIR/hb-launching`:
per 30 s il demone non rilancia la UI, così non chiude l'homebrew appena partito.
Se websrv risponde 503 e `FAKE00000/sce_sys/param.json` è rovinato, lo si toglie
(websrv lo ricrea). Se websrv non risponde più (si blocca dopo un avvio fallito)
lo si dice dopo 20 s.

## Installare i PKG

`source/install.c` installa i pkg con `sceAppInstUtilInstallByPackage`, con le
strutture corrette (metadati di 0x38 byte con `slot` e `is_playgo_enabled`,
PlayGoInfo di 0x2700: con quelle vecchie i giochi base fallivano con
0x80B2116F), l'authid di ShellCore preso in prestito per la durata della
chiamata, `/data` riscritto in `/user/data` e, come ultimo ripiego, il Direct
Package Installer di etaHEN (porta 12800). L'avanzamento è quello vero del
sistema (`sceAppInstUtilGetInstallStatus` sul Content ID) e si vede **nella
fila della home**, come sulla PS4: icona del gioco (letta dal pkg), barra,
fase, errori. Le installazioni vanno in coda, una alla volta. Finita
l'installazione si cancellano il pkg scaricato e i file caricati dal telefono;
all'avvio si tolgono i file orfani (`OMEGA_DIR/dl` più vecchi di 2 ore,
registrazioni di giochi esterni non riuscite).

**Installa PKG** (`source/pkgs.c`, tessera nella home dopo Store e Browser):
i `.pkg` di chiavette e dischi, di `/data/pkg`, dei caricamenti dal telefono e
delle cartelle scelte, fino a 3 livelli sotto, con titolo, icona e Content ID
letti dal pkg. ✕ installa, △ installa tutti, □ elimina o aggiunge la cartella.
Dal gestore dei file un `.pkg` si installa con ✕.

## Cartelle dei giochi e montaggio automatico

Le cartelle scelte dall'utente stanno in `OMEGA_DIR/paths.txt` (`game <percorso>`
o `pkg <percorso>`); si scelgono da Installa PKG › Cartelle di giochi e PKG o da
Impostazioni › Home e giochi. `source/drives.c` le guarda insieme ai dischi e a
`/data/etaHEN/games`, `/data/games`. Con **Montaggio automatico** (Personalizza ›
Home, attivo di base) i giochi trovati si montano e si registrano subito, come
fa ShadowMount, e compaiono anche nella Home della console; se il disco
sparisce il mount si toglie. Lo stesso thread guarda `/user/appmeta`,
`/system_ex/app`, `/user/app` e le cartelle degli homebrew: un gioco montato da
ShadowMount, un pkg installato o un homebrew caricato compaiono in home senza
riavviare Omega.

## Archivio e spostamenti

`source/storage.c`: dove sta ogni gioco e quanto occupa; i giochi in cartella
(con `eboot.bin`) si spostano o si copiano tra memoria interna e dischi
esterni, con controllo dello spazio, avanzamento nella fila della home e
verifica della dimensione prima di togliere l'originale. Verso un disco il
gioco si disinstalla dalla memoria interna e il montaggio automatico lo
registra dal disco; verso la memoria interna si smonta e si registra da
`/user/app`. I giochi installati dal sistema come pacchetto cifrato non si
leggono da un disco: per quelli si indica lo spostamento della console. Anche i
`.pkg` si spostano o si copiano su un disco (Installa PKG › □).

## Il tuo riepilogo

`source/wrap.c`, come il Wrap-Up di PlayStation: una settimana, un mese o un
anno di gioco a schede che scorrono da sole (X avanti, L1/R1, Quadrato per il
periodo): tempo giocato con il confronto col periodo prima, il gioco
preferito, i primi cinque, il ritmo (ore e giorni), i record, i trofei, gli
amici con cui hai giocato, il confronto anonimo con la community e il profilo
finale da condividere sulla bacheca. Si apre da Community › Tempo di gioco con
Triangolo; una volta a settimana, poco dopo l'accesso, una notifica avvisa che
il riepilogo della settimana prima è pronto (se c'è almeno mezz'ora di gioco;
la settimana già annunciata sta in `OMEGA_DIR/wrap-seen-<utente>`). I numeri li
calcola il server (`GET /api/v1/wrap`) nel fuso della console.

## Salvataggi online

`source/savesync.c`, come su PS Plus. Il salvataggio di un gioco si legge da una
**copia** dell'immagine (`OMEGA_DIR/saves/work`): la chiave sigillata (a 0x800
nell'immagine per i giochi PS5, in `<dir>.bin` per quelli PS4) si apre con
`/dev/pfsmgr` (ioctl `0xc0845302`) e la copia si monta con `sceFsMountSaveData`
(authid `0x4800000000000010`), come garlic-savemgr e PS5 Save Mounter.
L'originale non si tocca. I file diventano un archivio, cifrato qui con
XChaCha20-Poly1305 a pezzi da 1 MB (Monocypher) e spedito a pezzi da 8 MB.
La chiave (32 byte casuali) resta in `OMEGA_DIR/saves/key-<utente>`; al server
va solo cifrata con la parola d'ordine (Argon2id 32 MB × 4).

Ripristino: solo su un salvataggio che sulla console c'è già (lo crea il
gioco). Si verifica il MAC di ogni pezzo, si controllano tutti i percorsi
dell'archivio, si monta una copia, si sostituiscono i file tranne `sce_sys`
(dove `param.sfo` lega il salvataggio all'utente) e la copia prende il posto
dell'originale, che resta in `OMEGA_DIR/saves/undo` per «Annulla l'ultimo
ripristino». Con il caricamento automatico, all'avvio di Omega (cioè dopo una
partita) e ogni 10 minuti si caricano i giochi con salvataggi più nuovi
dell'ultimo caricamento. Sul Mac il «contenitore» è una cartella.

## App mobile e notifiche

La tessera **App mobile** mostra il QR della web app (`<server>/app/`);
`mobile_link_card()` è il riquadro con QR e indirizzo usato ovunque si parla
del telefono. «Installa sulla PS5» dal telefono arriva da `GET
/api/v1/console/queue` (ogni 15 s) e passa da `store_install_remote()`.
Impostazioni › Account › Notifiche (`source/notifprefs.c`) regola le
notifiche per tipo e per amico; quelle `silent` non fanno comparire il toast.

## Modalità del menu

`source/homestyles.c`: oltre alla home di Omega, Classica PS4, XMB (PS3),
Griglia, Carosello e Cinema (Impostazioni › Aspetto › Modalità del menu). Usano
la stessa selezione della home (sfondo, scheda, avvio, opzioni con R1).

## Prestazioni

Il renderer è software: ogni sfumatura e ogni velo a tutto schermo costano
CPU. La velatura del tema è dentro le immagini di sfondo (`bake_tint`, una
volta per immagine); con un pannello aperto e fermo la scena sotto si
fotografa una volta già scurita e non si ridisegna (`render_frame`, «scena
congelata»); con la home ferma si scende a 30 fotogrammi. Misure sul Mac col
renderer software (05/10/2026): home da 6,6 a 3,5 ms, Game Base da 9,7 a 2,6,
Centro di controllo da 11,3 a 3,5.

## Novità dopo un aggiornamento

Al primo avvio di una versione nuova (`OMEGA_VERSION` diversa da
`OMEGA_DIR/version-seen.txt`) la UI mostra la finestra «Novità»
(`source/whatsnew.c`). L'elenco `ITEMS` si riscrive a ogni rilascio, insieme
alla versione; chi installa da zero non la vede.

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
- `saves/` — chiave dei salvataggi online (`key-<utente>`, 0600), ultimo caricamento per gioco, copie per annullare un ripristino (`undo/`)

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
| `savesync.c` | salvataggi online: lettura da una copia montata, cifratura, caricamento, ripristino e annullamento |
| `wrap.c` | il tuo riepilogo: le schede stile Wrap-Up, la condivisione sulla bacheca e la notifica settimanale |
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
