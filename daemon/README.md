# omega_redirect — demone di Omega

Payload che gira in background, avviato all'accensione dal loader dell'HEN
(OnionHEN, etaHEN, pldmgr o ps5_autoloader). Non serve configurarlo a mano: la
UI riconosce l'HEN, chiede conferma e mette il file nel posto giusto
(`omega-ui-src/source/hen.c`). Ne gira una sola copia: se la porta 9095 è già
occupata il demone esce.

Cosa fa:

- **Avvio** (solo se l'utente ha scelto Omega come Home, cioè
  `/data/Omega/home-mode.txt` vale `1`): dopo 20 s (il tempo di far partire websrv) chiede a websrv
  (`GET /hbldr` su `127.0.0.1:8080`) di avviare la UI
  `/data/homebrew/OmegaUI/OmegaUI.elf`, riprovando finché risponde.
- **Ritorno alla Home** (stessa condizione): quando l'app in primo piano torna a essere un'app di
  sistema (`NPXS…`), rilancia la UI, con una pausa minima di 10 s tra un
  rilancio e l'altro.
- **Presenza:** ogni 30 s comunica al server il gioco in primo piano
  (`POST /api/v1/presence`), anche se avviato dalla Home di sistema; il nome del
  gioco viene da `param.json` o `param.sfo`.
- **Notifiche:** durante il gioco controlla ogni 8 s le novità
  (`GET /api/v1/sync`) e le mostra come notifiche di sistema.

I testi delle notifiche sono tradotti con gli stessi cataloghi della UI
(`omega-ui-src/i18n/`): `source/i18n_data.c` è generato da
`omega-ui-src/tools/i18n-gen.mjs` e contiene solo i testi usati qui. La lingua è
quella scelta nella UI (`/data/Omega/lang.txt`) o, se manca, quella della
console (`sceSystemServiceParamGetInt(1)`); si rilegge ogni 30 s. Le richieste
al server portano `Accept-Language`, così anche i titoli delle notifiche
arrivano tradotti.

Quando la UI è in primo piano aggiorna `/data/Omega/ui-active` ogni 2 s: se il
file è recente (meno di 15 s) il demone lascia a lei presenza e notifiche. Il
token arriva da `/data/Omega/session.json`, scritto dalla UI; senza sessione
il demone non contatta il server. Non fa injection: usa solo lo stato del primo
piano e l'API di websrv.

## Voce del party in gioco

`source/voice.c`: quando l'utente è in un party e la UI non è in primo piano
(si chiude da sola quando parte un gioco, e toglie `ui-active` uscendo) il
servizio porta avanti la voce: microfono con `libSceAudioIn` caricata a runtime
(prima l'utente in primo piano, poi 255; tipo 0, poi 1), Opus 16 kHz a frame da
20 ms, invio e ricezione su **una connessione keep-alive per thread** (una
connessione nuova a pacchetto costava 250-400 ms e la voce accumulava secondi
di ritardo), buffer per persona che si allarga da 80 a 400 ms se la rete
singhiozza, uscita su `sceAudioOut` (porta MAIN, mono). Controlla il party ogni
4 s (`GET /api/v1/party`); appena la UI torna in primo piano (ui-active più
giovane di 5 s) chiude il microfono e lascia la voce a lei.

Comandi: `GET /v1/voice` (stato: party, microfono `ok|muted|silent|none`, chi
parla) e `POST /v1/voice {"cmd":"mute|unmute|toggle|leave"}`. Li usano la
pagina Omega del Toolbox di OnionHEN (in gioco: L2+R3 › Plugin › Omega) e il
Telecomando dal telefono (scheda Party vocale).

Non si può provare sul Mac con la console vera: c'è un banco di prova che
compila `voice.c` con SceHttp su libcurl, microfono e altoparlante su file e
ritardi di rete simulati (misure del 05/10/2026:
~0,5 s di ritardo a 80 ms di RTT, ~0,7 s a 150 ms).

## Musica

`source/player.c` decodifica con FFmpeg (radio, HLS, file locali e su USB,
server Subsonic) e converte a 48 kHz stereo. **Sulla PS5 un payload in
background non ha una sessione audio**: `sceAudioOut` si apre ma non si sente
niente (05/10/2026: nessun homebrew noto suona così; suonano le app lanciate
come "bigapp", come la UI). Quindi, quando la UI è aperta, il lettore le manda i
blocchi già decodificati su `127.0.0.1:9096` (PCM s16 stereo, al ritmo del
tempo reale con 150 ms di anticipo) e li suona lei (`omega-ui-src/source/pcmlink.c`).
Senza UI ripiega su `sceAudioOut`; se la porta viene rifiutata lo stato diventa
`error` con `no_audio`. Durante i giochi la musica quindi di norma non si sente. Coda, volume e preferiti stanno in
`/data/Omega/music.json`; la UI comanda il lettore via HTTP su `127.0.0.1:9095`.

## Diagnostica

Il servizio manda da solo a `/api/v1/diag/event` i codici tecnici dell'uscita
audio, dei flussi e del microfono (niente URL né nomi), in coda dal ciclo
principale. Sul server finiscono in `MEDIA_DIR/diag/events-AAAA-MM-GG.jsonl`
(volume `media-data`); i resoconti completi che l'utente manda da Impostazioni ›
Sistema e strumenti › Invia diagnostica sono `report-*.json` nella stessa cartella.

## Telecomando e La mia libreria

`source/ctl.c` serve su `:9095` la pagina `remote/remote.html` (incorporata in
`source/remote_page.c` da `node tools/embed-remote.mjs`, con le lingue di
`remote/i18n.json`). Dalla console le richieste sono fidate; dalla rete serve il
token che si ottiene con il PIN mostrato nella UI (5 tentativi sbagliati
bloccano l'abbinamento per un minuto). Dal telefono o dal PC si comanda la
musica, si mandano file audio in `/data/Omega/Music` e si gestisce La mia
libreria (`source/lib.c`, salvata in `/data/Omega/library.json`): giochi
aggiunti a mano, importati da un JSON o sincronizzati da un link.

Dalla stessa pagina si possono caricare i giochi direttamente sulla console: un
file `.pkg`, `.zip` o `.elf`, oppure una cartella intera (anche trascinata dal
PC). I file arrivano in `/data/Omega/uploads/<lotto>/`, ognuno in un thread suo,
così musica e comandi restano liberi. A fine lotto il demone guarda cosa è
arrivato: una cartella con `homebrew.js` o `eboot.elf` va subito in
`/data/homebrew`; un gioco (`sce_sys/param.json` per PS5, `param.sfo` per PS4)
o un file diventa una voce di La mia libreria con link `file://`, che la UI
installa senza scaricare niente (le cartelle in `/user/app/<Title ID>`).
Togliendo la voce si cancellano anche i file caricati.

La UI chiede ogni pochi secondi `GET /v1/console/jobs` (solo da 127.0.0.1):
il caricamento in corso, per la tessera che avanza nella home, e i giochi che il
telefono o il PC hanno chiesto di installare subito (`POST /v1/library/install
{"id"}` o `"install": true` in `upload/done`); la coda si svuota a ogni lettura.
I lotti caricati che non sono finiti in libreria si cancellano dopo un giorno.

Per provarlo sul Mac: `desktop/build-desktop.sh` (FFmpeg e SDL2 di Homebrew),
poi `desktop/omega-player-desktop /tmp/omega/player.json` e apri
`http://127.0.0.1:9095`.

All'avvio, se esiste la vecchia cartella dati `/data/OmegaPSNLab` e
`/data/Omega` no, la rinomina (lo stesso controllo lo fa la UI).

## Compilare

Una volta sola, FFmpeg ridotto al solo audio (senza, si usa quello completo
dell'SDK e il demone pesa ~30 MB):

```sh
PS5_PAYLOAD_SDK=$HOME/ps5-payload-sdk omega-redirect-src/tools/build-ffmpeg-audio.sh
```

Poi:

```sh
export LLVM_CONFIG=$(brew --prefix llvm@18)/bin/llvm-config
$HOME/ps5-payload-sdk/bin/prospero-cmake -S omega-redirect-src -B omega-redirect-src/build
cmake --build omega-redirect-src/build
```

Il risultato è `bin/omega_redirect.elf`; `omega-ui-src/install-ps5.sh` lo copia
sulla console. Il nuovo demone parte al riavvio successivo. Dopo aver cambiato
`remote/remote.html` rilancia `node tools/embed-remote.mjs`.

Macro: `OMEGA_BASE_URL` (predefinito `https://play.omegasuite.it`) e
`OMEGA_API` (predefinito `/api/v1`). Log in `/data/Omega/omega-redirect.log`.

## Crediti

Omega è sviluppato da **TheCriicom** ([outlinedigital.it](https://outlinedigital.it)).
Licenza GPL-3.0-or-later (`LICENSE`, `THIRD-PARTY-NOTICES.md`).
