# omega_redirect — demone di Omega

Piccolo payload che gira in background, avviato all'accensione da Payload
Manager (`/data/pldmgr/payloads/OmegaRedirect/omega_redirect.elf`). Fa di Omega
la shell della console:

- **Avvio:** dopo 20 s (il tempo di far partire websrv) chiede a websrv
  (`GET /hbldr` su `127.0.0.1:8080`) di avviare la UI
  `/data/homebrew/OmegaUI/OmegaUI.elf`, riprovando finché risponde.
- **Ritorno alla Home:** quando l'app in primo piano torna a essere un'app di
  sistema (`NPXS…`), rilancia la UI, con una pausa minima di 10 s tra un
  rilancio e l'altro.
- **Presenza:** ogni 30 s comunica al server il gioco in primo piano
  (`POST /api/v1/presence`), anche se avviato dalla Home di sistema; il nome del
  gioco viene da `param.json` o `param.sfo`.
- **Notifiche:** durante il gioco controlla ogni 8 s le novità
  (`GET /api/v1/sync`) e le mostra come notifiche di sistema.

Quando la UI è in primo piano aggiorna `/data/Omega/ui-active` ogni 2 s: se il
file è recente (meno di 15 s) il demone lascia a lei presenza e notifiche. Il
token arriva da `/data/Omega/session.json`, scritto dalla UI; senza sessione
il demone non contatta il server. Non fa injection: usa solo lo stato del primo
piano e l'API di websrv.

All'avvio, se esiste la vecchia cartella dati `/data/OmegaPSNLab` e
`/data/Omega` no, la rinomina (lo stesso controllo lo fa la UI).

## Compilare

```sh
export LLVM_CONFIG=$(brew --prefix llvm@18)/bin/llvm-config
$HOME/ps5-payload-sdk/bin/prospero-cmake -S omega-redirect-src -B omega-redirect-src/build
cmake --build omega-redirect-src/build
```

Il risultato è `bin/omega_redirect.elf`; `omega-ui-src/install-ps5.sh` lo copia
sulla console. Il nuovo demone parte al riavvio successivo.

Macro: `OMEGA_BASE_URL` (predefinito `https://play.omegasuite.it`) e
`OMEGA_API` (predefinito `/api/v1`). Log in `/data/Omega/omega-redirect.log`.

Licenza GPL-3.0-or-later (`LICENSE`, `THIRD-PARTY-NOTICES.md`).
