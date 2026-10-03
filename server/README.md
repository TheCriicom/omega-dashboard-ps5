# Omega — server

Il server dell'app **Omega**: account, funzioni social (amici, presenza,
messaggi, party con chat e voce, bacheca, gruppi, tempo di gioco), lo **Store**
di homebrew, la **libreria personale** e il pannello di moderazione web.
Chiunque può ospitarne uno: sulla console basta aggiungerne l'indirizzo in
**Impostazioni → Server**.

Omega è un progetto indipendente: non è affiliato a Sony Interactive
Entertainment e non usa i suoi account né i suoi server.

## Installazione rapida

Su un server Linux con Docker e Docker Compose v2:

```sh
git clone https://github.com/CristianLaporta/omega-dashboard-ps5.git
cd omega-dashboard-ps5/server
./scripts/install.sh
```

Lo script chiede un dominio (facoltativo) e una porta, crea `.env` con segreti
casuali, costruisce le immagini, avvia i servizi e carica il catalogo dello
Store. Con un dominio che punta al server attiva l'HTTPS automatico tramite
Caddy (porte 80 e 443 libere).

Dopo l'avvio:

1. dalla console, **Impostazioni → Server → Aggiungi un server** con l'indirizzo
   mostrato a fine installazione, poi crea un account;
2. rendi amministratore il tuo account:
   `docker compose exec api npm run admin -- grant <online_id>`;
3. compila nel `.env` almeno `LEGAL_CONTROLLER_NAME` e `LEGAL_CONTACT_EMAIL`
   (compaiono in privacy e termini) e riavvia con `docker compose up -d`.

### Installazione a mano

```sh
cp .env.example .env     # compila PGPASSWORD e OMEGA_SESSION_SECRET (openssl rand -hex 32)
docker compose up -d --build --wait                                   # solo http, su 127.0.0.1:8090
docker compose -f compose.yml -f compose.tls.yml up -d --build --wait # con HTTPS (serve OMEGA_DOMAIN)
docker compose exec api npm run seed:store
```

Senza Caddy puoi mettere davanti il reverse proxy che usi già (nginx, Traefik…)
puntandolo a `127.0.0.1:8090`: va disattivato il buffering delle risposte per
la voce del party, che usa long-poll.

### Aggiornare

```sh
git pull
docker compose up -d --build --wait
```

Le migrazioni del database si applicano da sole all'avvio dell'api.

## Architettura

```
console ─► HTTPS (Caddy o il tuo reverse proxy) ─► 127.0.0.1:8090
                                          │
  ┌───────────────────────────────────────┼───────────────────────────────┐
  │ progetto docker compose  omega        ▼                               │
  │                                                                       │
  │  proxy   (omega-proxy)       unica porta pubblicata, solo su loopback │
  │    inoltra tutto all'api e scrive il registro delle richieste,        │
  │    con credenziali e token oscurati; i corpi non si registrano mai    │
  │          │                                                            │
  │          ▼                                                            │
  │  api     (omega-api)         solo rete interna                        │
  │    rotte di config/endpoints.json, sessioni, pannello /admin          │
  │          │                                                            │
  │          ▼                                                            │
  │  db      (omega-db)          PostgreSQL 17, nessuna porta pubblicata  │
  └───────────────────────────────────────────────────────────────────────┘
```

| Cartella | Contenuto |
|---|---|
| `api/` | api Node.js (`src/`), rotte (`config/endpoints.json`), migrazioni, catalogo dello Store, pannello admin, test |
| `proxy/` | proxy senza dipendenze npm |
| `compose.yml` | i tre servizi |
| `compose.tls.yml`, `Caddyfile` | HTTPS automatico con Caddy |
| `scripts/install.sh` | installazione guidata |

L'api applica all'avvio le migrazioni di `api/migrations/` non ancora
registrate; un file già applicato non si modifica mai, si aggiunge il successivo.

## Configurazione

Le variabili si leggono da `.env` (modello in `.env.example`).

| Variabile | Predefinito | Uso |
|---|---|---|
| `PGUSER`, `PGDATABASE`, `PGPASSWORD` | `omega`, `omega`, — | credenziali di PostgreSQL (`PGPASSWORD` obbligatoria) |
| `OMEGA_SESSION_SECRET` | — | firma dei token di sessione e del cookie admin, almeno 32 caratteri. Cambiarlo disconnette tutti |
| `OMEGA_SESSION_TTL_SECONDS` | `86400` | durata delle sessioni dell'app |
| `OMEGA_REGISTRATION_KEY` | vuota | chiave richiesta alla registrazione; vuota = registrazione aperta |
| `PUBLIC_BASE_URL` | `http://localhost:8090` | indirizzo pubblico, usato nei testi legali |
| `LEGAL_CONTROLLER_NAME`, `LEGAL_CONTROLLER_VAT`, `LEGAL_CONTROLLER_ADDRESS`, `LEGAL_CONTACT_EMAIL`, `LEGAL_HOSTING_PROVIDER` | vuote | titolare del servizio nei documenti legali; se mancano si legge "[da completare]" |
| `LOG_RETENTION_DAYS` | `30` | giorni di conservazione dei registri del proxy (citati nell'informativa) |
| `STORE_AUTOHIDE_REPORTS` | `3` | segnalatori distinti oltre cui un contenuto si oscura in attesa di verifica |
| `LOG_SKIP_PREFIXES` | `/api/v1/party/voice,/lab/v1/party/voice` | percorsi inoltrati ma non registrati dal proxy |
| `OMEGA_PORT`, `OMEGA_BIND` | `8090`, `127.0.0.1` | porta e indirizzo su cui il server ascolta |
| `OMEGA_DOMAIN` | vuota | dominio per l'HTTPS automatico (`compose.tls.yml`) |

Variabili interne, già impostate da `compose.yml` o utili solo in sviluppo:
`DATABASE_URL`, `API_PORT`, `MEDIA_DIR`, `SITE_DIR`, `PUBLIC_SOURCE_DIR`,
`STORE_ASSET_DIR`, `ADMIN_INSECURE_COOKIE=1` (cookie admin senza `Secure`, per
provare il pannello su http) e, per il proxy, `PROXY_PORT`, `UPSTREAM_URL`,
`LOG_DIR`, `REDACT_HEADERS`, `REDACT_QUERY`.

## Avvio in locale

Servono Node.js ≥ 20 e un PostgreSQL; ffmpeg e JxrDecApp solo per caricare
avatar e copertine.

```sh
createdb omega          # lo schema lo creano le migrazioni all'avvio dell'api
cd api && npm ci
DATABASE_URL=postgres://localhost/omega OMEGA_SESSION_SECRET=$(openssl rand -hex 32) \
  API_PORT=18080 ADMIN_INSECURE_COOKIE=1 PUBLIC_SOURCE_DIR=../.. npm start
```

Il proxy, se serve:

```sh
cd proxy && PROXY_PORT=19986 UPSTREAM_URL=http://127.0.0.1:18080 LOG_DIR=/tmp/omega-logs npm start
```

### Test

I test end-to-end girano contro un'api già avviata; leggono anche il database
(`DATABASE_URL`) e avviano un proxy temporaneo da `proxy/`.

```sh
cd api
OMEGA_TEST_URL=http://127.0.0.1:18080 DATABASE_URL=postgres://localhost/omega npm test
```

`OMEGA_TEST_URL` vale `http://127.0.0.1:18080` se non indicata. I test creano
account con nomi unici a ogni esecuzione: usare un database di sviluppo.

## Amministrazione

**Ruolo admin** — il primo amministratore si nomina da riga di comando; gli
altri anche dal pannello:

```sh
docker compose exec api npm run admin -- grant <online_id>
docker compose exec api npm run admin -- revoke <online_id>
docker compose exec api npm run admin -- list
```

**Pannello** — `/admin`, con un account che ha il ruolo admin: panoramica,
segnalazioni (Store e social), homebrew, commenti, bacheca, utenti (ban, ruoli,
disconnessione, eliminazione) e registro di tutte le azioni. Sessione in un
cookie HttpOnly, Secure, SameSite=Strict di 8 ore, separata dai token dell'app;
le richieste che modificano qualcosa richiedono l'intestazione `X-Omega-Admin: 1`.

**Catalogo curato dello Store** — `api/config/store-catalog.json`: homebrew
open source con link diretti ai rilasci ufficiali. Le icone e le immagini non
sono incluse nel repository (appartengono ai rispettivi progetti): mettile in
`api/store-assets/` con i nomi indicati nel catalogo, altrimenti lo Store le
mostra con un segnaposto. Il caricamento è idempotente (chiave
`catalog_key`; voti, commenti e installazioni restano):

```sh
docker compose exec api npm run seed:store              # carica o aggiorna
docker compose exec api npm run seed:store -- --dry-run # mostra soltanto
docker compose exec api npm run seed:store -- --prune   # toglie le voci uscite dal catalogo
```

## Moderazione

- Homebrew e commenti dello Store, post e commenti della bacheca e utenti si
  possono segnalare dall'app. Una segnalazione aperta per utente e contenuto.
- Raggiunti `STORE_AUTOHIDE_REPORTS` segnalatori distinti, homebrew, commenti e
  post si oscurano in attesa di verifica; gli utenti no (il ban si decide dal
  pannello). Gli amministratori ricevono una notifica per ogni segnalazione.
- Dal pannello una segnalazione si archivia, oppure il contenuto si oscura o si
  elimina; tutte le segnalazioni aperte sullo stesso contenuto si chiudono insieme.
- Un ban revoca le sessioni, impedisce il login (con il motivo, mostrato solo a
  chi conosce la password) e nasconde gli homebrew dell'autore finché resta.

## Registri e conservazione

Il proxy scrive una riga JSON per richiesta su stdout e in
`requests-AAAA-MM-GG.jsonl` nel volume `logs` (`/var/log/omega` nel container):
`ts, request_id, client_ip, method, path, query, status, duration_ms,
resp_bytes, error, req_headers`. Le intestazioni e i parametri elencati in
`REDACT_HEADERS` e `REDACT_QUERY` e i segmenti di percorso lunghi o a forma di
JWT si oscurano; i corpi non si registrano mai. I file più vecchi di
`LOG_RETENTION_DAYS` si cancellano da soli.

```sh
docker exec omega-proxy sh -c 'tail -f /var/log/omega/requests-$(date -u +%F).jsonl'
```

L'api registra su stdout solo avvio, errori e cancellazioni di account. I log dei
container ruotano a 5 file da 10 MB.

## API

Il prefisso canonico è `/api/v1`. Le stesse rotte rispondono anche come
`/lab/v1/...`, il prefisso usato dalle versioni meno recenti dell'app. Un
percorso sconosciuto risponde `404 {"error":"not_found"}`, un metodo non
previsto `405` con `Allow`.

Autenticazione: `POST /api/v1/auth/login` rilascia un token da inviare come
`Authorization: Bearer <token>`. Gli errori hanno la forma
`{"error": "<codice>", "detail"?: "..."}`; i limiti di frequenza rispondono `429`.
L'elenco completo delle rotte è in `api/config/endpoints.json` e su
`GET /api/v1/endpoints`.

| Area | Rotte principali |
|---|---|
| Account | `POST auth/register` `{online_id, password, email?, registration_key?, accept_terms: true}` · `POST auth/login` · `POST auth/logout` · `POST auth/password` · `GET me` · `GET account/export` · `POST account/delete` `{password}` · `POST account/terms` `{version}` |
| Profilo e amici | `GET/POST profile` · `GET friends` · `POST friends/request\|accept\|decline` `{online_id}` · `DELETE friends/:onlineId` · `GET friends/suggestions` · `GET users/search?q=` · `GET users/:onlineId` |
| Presenza e home | `GET/POST presence` · `POST status` `{mode, message?}` · `GET sync?since=` (anche heartbeat) · `GET activity` · `GET/POST notifications…` |
| Privacy e blocchi | `GET/POST privacy` · `POST/DELETE users/:onlineId/block` · `GET blocks` · `POST users/:onlineId/report` |
| Messaggi | `GET messages` · `GET/POST messages/:onlineId` |
| Party | `GET/POST party` · `POST party/invite\|join\|decline\|leave\|mute` · `GET/POST party/messages` · `GET/POST party/voice` (audio binario, long-poll) |
| Bacheca | `POST posts` · `GET feed?before=&limit=` · `GET users/:onlineId/posts` · `GET/DELETE posts/:id` · `POST posts/:id/like` · `GET/POST posts/:id/comments` · `DELETE …/comments/:commentId` · `POST …/report` |
| Gruppi | `GET/POST groups` · `GET/POST groups/:id` · `GET/POST groups/:id/messages` · `POST groups/:id/members\|leave` · `DELETE groups/:id/members/:onlineId` |
| Tempo di gioco e inviti | `GET stats/me` · `GET stats/friends?game_id=&period=week\|all` · `POST invites` |
| Media | `POST media/upload?kind=avatar\|cover&ext=` (corpo = file) · `POST media/clear?kind=` · `GET media/:id/:frame` |
| Notizie e giochi | `GET news?game_id=` · `GET news/:newsId/image` · `GET games/:gameId` |
| Browser | `GET browse?url=` o `?q=` (pagina ridotta a blocchi) · `GET browse/img/:id` |
| Store | `GET/POST store/apps` · `GET/POST/DELETE store/apps/:id` · `GET store/apps/:id/cover\|icon\|shot/:n\|download` · `POST store/apps/:id/vote\|rate\|report` · `GET/POST store/apps/:id/comments` · `DELETE …/comments/:commentId` |
| Libreria personale | `GET/POST library/source` · `POST library/sync` · `GET library/items` · `GET library/items/:id[/cover\|/shot/:n\|/download]` |
| Documenti | `GET legal` (JSON per l'app) |

Fuori da `/api/v1`: `GET /` (descrizione dell'api, o il sito se `SITE_DIR`
contiene un `index.html`), `GET /healthz`, `/legal`,
`/legal/privacy|terms|licenses`, `/source`, `/source/omega-src.tar.gz`,
`/admin` e `/admin/api/...`.

### Store e libreria

Gli homebrew puntano a un link diretto (`.pkg`, `.zip` o `.elf`); le immagini su
siti terzi le scarica e le serve il server, mentre il file da installare lo
scarica la console. `GET store/apps/:id/download` risolve il link e restituisce
`{url, filename, kind, size, title_id, version, category, summary, homepage_url}`.

La libreria personale si riempie da un JSON dell'utente:

```json
{ "name": "La mia libreria", "games": [
  { "id": "g1", "title": "...", "platform": "PS5", "version": "1.00",
    "title_id": "CUSA12345", "cover": "https://.../c.jpg",
    "images": ["https://.../s1.jpg"], "url": "https://.../gioco.pkg", "type": "pkg" }
]}
```

Sono accettati anche `items`/`library` al posto di `games`,
`download_url`/`file`/`pkg` per `url`, `image`/`icon` per `cover`, `screenshots`
per `images`, `kind` per `type` (altrimenti si deduce dall'estensione).

### Voce nel party

Relay in memoria (ultimi 8 s per party). `POST party/voice?codec=opus|adpcm&seq=`
con un pezzo fino a 8 KB; `GET party/voice?after=<cursore>&wait=1` attende fino a
1,5 s e risponde in binario little-endian:
`"OVC1" u32 next_cursor u16 count`, poi per pezzo
`u8 oid_len, oid, u8 codec (1=opus, 2=adpcm), u32 seq, u16 len, data`.

### Aggiungere una rotta

1. una riga in `api/config/endpoints.json` (`method`, `path`, `handler`, `family`, `auth`);
2. la funzione in `api/src/endpoints/<modulo>.js`: riceve
   `{ req, res, url, params, auth, admin, clientIp }` e restituisce `{ status, body }`
   (oppure `{ sent: true }` se scrive la risposta da sé);
3. se servono tabelle o colonne, una nuova migrazione `api/migrations/NNN_nome.sql`.

## Documenti legali e sorgente

Informativa privacy, termini d'uso e licenze sono in `api/src/legal.js`, unica
fonte per l'app (`GET /api/v1/legal`) e per il web (`/legal/*`). Descrivono ciò
che il servizio fa davvero: se cambiano i dati raccolti si aggiornano i testi e
`TERMS_VERSION`, e l'app chiede di nuovo il consenso (`terms.needs_accept` in
`GET /api/v1/me`). I dati del titolare arrivano dalle variabili `LEGAL_*`.

Il codice sorgente dell'app console (GPL-3.0-or-later) è scaricabile da
`/source`: l'archivio si genera al momento da `client/` e `daemon/`,
montati in sola lettura nel container dell'api. Se modifichi l'app e la
distribuisci, la GPL ti chiede di pubblicarne il sorgente: questo lo fa per te.

## Licenza

GPL-3.0-or-later.
