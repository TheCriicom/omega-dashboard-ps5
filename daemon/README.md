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

## Musica

`source/player.c` decodifica con FFmpeg (radio, HLS, file locali e su USB,
server Subsonic), converte a 48 kHz stereo e suona con `sceAudioOut`, quindi la
musica continua anche dentro i giochi. Coda, volume e preferiti stanno in
`/data/Omega/music.json`; la UI comanda il lettore via HTTP su `127.0.0.1:9095`.

## Telecomando e La mia libreria

`source/ctl.c` serve su `:9095` la pagina `remote/remote.html` (incorporata in
`source/remote_page.c` da `node tools/embed-remote.mjs`, con le lingue di
`remote/i18n.json`). Dalla console le richieste sono fidate; dalla rete serve il
token che si ottiene con il PIN mostrato nella UI (5 tentativi sbagliati
bloccano l'abbinamento per un minuto). Dal telefono o dal PC si comanda la
musica, si mandano file audio in `/data/Omega/Music` e si gestisce La mia
libreria (`source/lib.c`, salvata in `/data/Omega/library.json`): giochi
aggiunti a mano, importati da un JSON o sincronizzati da un link.

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
