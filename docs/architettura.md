<sub>[English](architecture.md) · **Italiano**</sub>

# Architettura

```
 console PS5                                     server
┌──────────────────────────────┐         ┌──────────────────────────────────┐
│ Omega (homebrew, SDL2)       │  HTTPS  │ Caddy / reverse proxy            │
│  home · store · community    │ ──────► │   └─ proxy  registro, oscuramento│
│  party (voce) · impostazioni │         │        └─ api   Node.js          │
│                              │         │              └─ PostgreSQL       │
│ omega_redirect (payload)     │ ──────► │                                  │
│  riapre Omega, notifiche     │         └──────────────────────────────────┘
└──────────────────────────────┘
```

## App (`client/`)

Un'unica app SDL2 a schermo intero, 1920×1080, con renderer software: è quello
che funziona nel contesto degli homebrew avviati da websrv.

- **Ciclo principale** (`main.c`): input da controller e tastiera con
  ripetizione, una pila di pannelli animati sopra la scena, toast e messaggi.
- **Rete in background** (`async.c`): due worker eseguono le richieste HTTPS
  (SceHttp) e il parsing JSON; le callback tornano sul thread principale. Un
  terzo worker decodifica le immagini. L'interfaccia non si blocca mai.
- **Server** (`servers.c`): l'indirizzo dell'api si sceglie a runtime; la
  sessione salvata vale solo per il server su cui è nata.
- **Avvio dei titoli** (`home.c`, `hblaunch.c`, `payload.c`): i giochi con
  `sceLncUtilLaunchApp`; gli homebrew in formato websrv eseguendo il loro
  `homebrew.js` con QuickJS, come farebbe il launcher di websrv; i payload ELF
  inviati al loader sulla porta 9021.
- **Installazione** (`install.c`): riconosce pkg, zip ed elf e li mette dove
  servono (AppInstUtil per i pkg, `/data/homebrew` per gli homebrew, Payload
  Manager per gli ELF).
- **Voce** (`voice.c`): microfono a 16 kHz da libSceAudioIn, rilevamento della
  voce, Opus a 16 kb/s, pacchetti da 200 ms inviati al server; la ricezione è un
  long-poll e la riproduzione passa per il mixer di `audio.c`.

## Server (`server/`)

- **proxy**: l'unico servizio esposto. Registra ogni richiesta (un file al
  giorno, conservazione configurabile) oscurando credenziali, token e cookie;
  i corpi non si registrano mai.
- **api**: rotte dichiarate in `config/endpoints.json` e implementate in
  `src/endpoints/`. Sessioni firmate con HMAC, migrazioni applicate all'avvio,
  pannello di moderazione su `/admin` con sessione separata.
- **PostgreSQL**: tutti i dati; la voce del party resta solo in memoria.

## Flussi principali

**Accesso** — `POST /api/v1/auth/login` restituisce un token; l'app lo salva in
`/data/Omega/session.json` e lo manda come `Authorization: Bearer`.

**Sincronizzazione** — ogni pochi secondi `GET /api/v1/sync` tiene viva la
presenza e porta contatori, amici, party e notifiche nuove.

**Store** — l'app chiede `GET store/apps/:id/download`; il server risolve i
redirect del link e restituisce l'URL finale, che la console scarica
direttamente dal sito dell'autore.

**Voce** — `POST party/voice` con un pacchetto binario; gli altri membri lo
ricevono da `GET party/voice?after=<cursore>&wait=1`, che resta in attesa fino a
1,5 s. Il formato è descritto in [`server/README.it.md`](../server/README.it.md#voce-nel-party).
