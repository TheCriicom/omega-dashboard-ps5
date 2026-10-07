<sub>**English** · [Italiano](README.it.md)</sub>

<p align="center">
  <picture><source media="(prefers-color-scheme: dark)" srcset="../docs/img/omega-mark.svg"><img src="../docs/img/omega-mark-light.svg" width="96" alt="Omega"></picture>
</p>

# Omega — server

The server for the **Omega** app: accounts, social features (friends, presence,
messages, party with chat and voice, wall, groups, playtime), the homebrew
**Store**, the **personal library** and the web moderation panel.
Anyone can host one: on the console, just add its address in
**Settings → Server**.

Omega is an independent project: it is not affiliated with Sony Interactive
Entertainment and does not use its accounts or its servers.

## Quick install

On a Linux server with Docker and Docker Compose v2:

```sh
git clone https://github.com/TheCriicom/omega-dashboard-ps5.git
cd omega-dashboard-ps5/server
./scripts/install.sh
```

The script asks for a domain (optional) and a port, creates `.env` with random
secrets, builds the images, starts the services and loads the Store catalog.
With a domain pointing to the server it enables automatic HTTPS through Caddy
(ports 80 and 443 must be free).

After startup:

1. on the console, **Settings → Server → Add a server** with the address
   shown at the end of the install, then create an account;
2. make your account an administrator:
   `docker compose exec api npm run admin -- grant <online_id>`;
3. fill in at least `LEGAL_CONTROLLER_NAME` and `LEGAL_CONTACT_EMAIL` in `.env`
   (they appear in the privacy policy and terms) and restart with
   `docker compose up -d`.

### Manual install

```sh
cp .env.example .env     # fill in PGPASSWORD and OMEGA_SESSION_SECRET (openssl rand -hex 32)
docker compose up -d --build --wait                                   # http only, on 127.0.0.1:8090
docker compose -f compose.yml -f compose.tls.yml up -d --build --wait # with HTTPS (requires OMEGA_DOMAIN)
docker compose exec api npm run seed:store
```

Without Caddy you can put the reverse proxy you already use (nginx, Traefik…)
in front, pointing it to `127.0.0.1:8090`: response buffering must be turned
off for party voice, which uses long-polling.

### Updating

```sh
git pull
docker compose up -d --build --wait
```

Database migrations are applied automatically when the api starts.

## Architecture

```
console ─► HTTPS (Caddy or your reverse proxy) ─► 127.0.0.1:8090
                                          │
  ┌───────────────────────────────────────┼───────────────────────────────┐
  │ docker compose project  omega         ▼                               │
  │                                                                       │
  │  proxy   (omega-proxy)       the only published port, loopback only   │
  │    forwards everything to the api and writes the request log,         │
  │    with credentials and tokens redacted; bodies are never logged      │
  │          │                                                            │
  │          ▼                                                            │
  │  api     (omega-api)         internal network only                    │
  │    routes from config/endpoints.json, sessions, /admin panel          │
  │          │                                                            │
  │          ▼                                                            │
  │  db      (omega-db)          PostgreSQL 17, no published port         │
  └───────────────────────────────────────────────────────────────────────┘
```

| Folder | Contents |
|---|---|
| `api/` | Node.js api (`src/`), routes (`config/endpoints.json`), migrations, Store catalog, admin panel, tests |
| `proxy/` | proxy with no npm dependencies |
| `compose.yml` | the three services |
| `compose.tls.yml`, `Caddyfile` | automatic HTTPS with Caddy |
| `scripts/install.sh` | guided install |

At startup the api applies the migrations in `api/migrations/` that are not yet
recorded; a file that has been applied is never modified, you add the next one.

## Configuration

Variables are read from `.env` (template in `.env.example`).

| Variable | Default | Purpose |
|---|---|---|
| `PGUSER`, `PGDATABASE`, `PGPASSWORD` | `omega`, `omega`, — | PostgreSQL credentials (`PGPASSWORD` is required) |
| `OMEGA_SESSION_SECRET` | — | signs session tokens and the admin cookie, at least 32 characters. Changing it signs everyone out |
| `OMEGA_SESSION_TTL_SECONDS` | `86400` | lifetime of app sessions |
| `OMEGA_REGISTRATION_KEY` | empty | key required at sign-up; empty = open registration |
| `PUBLIC_BASE_URL` | `http://localhost:8090` | public address, used in the legal texts |
| `LEGAL_CONTROLLER_NAME`, `LEGAL_CONTROLLER_VAT`, `LEGAL_CONTROLLER_ADDRESS`, `LEGAL_CONTACT_EMAIL`, `LEGAL_HOSTING_PROVIDER` | empty | service operator in the legal documents; if missing, they read "[da completare]" ("to be completed") |
| `LOG_RETENTION_DAYS` | `30` | days the proxy logs are kept (stated in the privacy policy) |
| `STORE_AUTOHIDE_REPORTS` | `3` | number of distinct reporters beyond which content is hidden pending review |
| `LOG_SKIP_PREFIXES` | `/api/v1/party/voice,/lab/v1/party/voice` | paths forwarded but not logged by the proxy |
| `OMEGA_PORT`, `OMEGA_BIND` | `8090`, `127.0.0.1` | port and address the server listens on |
| `OMEGA_DOMAIN` | empty | domain for automatic HTTPS (`compose.tls.yml`) |

Internal variables, already set by `compose.yml` or only useful in development:
`DATABASE_URL`, `API_PORT`, `MEDIA_DIR`, `SITE_DIR`, `PUBLIC_SOURCE_DIR`,
`STORE_ASSET_DIR`, `ADMIN_INSECURE_COOKIE=1` (admin cookie without `Secure`, to
try the panel over http) and, for the proxy, `PROXY_PORT`, `UPSTREAM_URL`,
`LOG_DIR`, `REDACT_HEADERS`, `REDACT_QUERY`.

## Running locally

You need Node.js ≥ 20 and PostgreSQL; ffmpeg and JxrDecApp only for uploading
avatars and covers.

```sh
createdb omega          # the schema is created by the migrations when the api starts
cd api && npm ci
DATABASE_URL=postgres://localhost/omega OMEGA_SESSION_SECRET=$(openssl rand -hex 32) \
  API_PORT=18080 ADMIN_INSECURE_COOKIE=1 PUBLIC_SOURCE_DIR=../.. npm start
```

The proxy, if you need it:

```sh
cd proxy && PROXY_PORT=19986 UPSTREAM_URL=http://127.0.0.1:18080 LOG_DIR=/tmp/omega-logs npm start
```

### Tests

The end-to-end tests run against an api that is already running; they also read
the database (`DATABASE_URL`) and start a temporary proxy from `proxy/`.

```sh
cd api
OMEGA_TEST_URL=http://127.0.0.1:18080 DATABASE_URL=postgres://localhost/omega npm test
```

`OMEGA_TEST_URL` defaults to `http://127.0.0.1:18080`. The tests create
accounts with unique names on every run: use a development database.

## Administration

**Admin role** — the first administrator is appointed from the command line;
the others from the panel as well:

```sh
docker compose exec api npm run admin -- grant <online_id>
docker compose exec api npm run admin -- revoke <online_id>
docker compose exec api npm run admin -- list
```

**Panel** — `/admin`, with an account that has the admin role: overview,
reports (Store and social), homebrew, comments, wall, users (bans, roles,
sign-out, deletion) and a log of every action. The session lives in an
HttpOnly, Secure, SameSite=Strict cookie valid for 8 hours, separate from the
app tokens; requests that change something require the `X-Omega-Admin: 1`
header.

**Curated Store catalog** — `api/config/store-catalog.json`: open-source
homebrew with direct links to the official releases. Icons and images are not
included in the repository (they belong to their respective projects): put
them in `api/store-assets/` with the names given in the catalog, otherwise the
Store shows a placeholder. Loading is idempotent (key `catalog_key`; votes,
comments and installs are kept):

```sh
docker compose exec api npm run seed:store              # load or update
docker compose exec api npm run seed:store -- --dry-run # show only
docker compose exec api npm run seed:store -- --prune   # remove entries no longer in the catalog
```

## Moderation

- Store homebrew and comments, wall posts and comments, and users can be
  reported from the app. One open report per user and item.
- Once `STORE_AUTOHIDE_REPORTS` distinct reporters is reached, homebrew,
  comments and posts are hidden pending review; users are not (bans are decided
  from the panel). Administrators get a notification for every report.
- From the panel a report can be dismissed, or the content hidden or deleted;
  all open reports on the same item are closed together.
- A ban revokes sessions, blocks login (with the reason, shown only to someone
  who knows the password) and hides the author's homebrew for as long as it
  lasts.

## Logs and retention

The proxy writes one JSON line per request to stdout and to
`requests-AAAA-MM-GG.jsonl` in the `logs` volume (`/var/log/omega` in the
container):
`ts, request_id, client_ip, method, path, query, status, duration_ms,
resp_bytes, error, req_headers`. The headers and parameters listed in
`REDACT_HEADERS` and `REDACT_QUERY`, and path segments that are long or shaped
like a JWT, are redacted; bodies are never logged. Files older than
`LOG_RETENTION_DAYS` are deleted automatically.

```sh
docker exec omega-proxy sh -c 'tail -f /var/log/omega/requests-$(date -u +%F).jsonl'
```

The api logs only startup, errors and account deletions to stdout. Container
logs rotate at 5 files of 10 MB.

## API

The canonical prefix is `/api/v1`. The same routes also answer as
`/lab/v1/...`, the prefix used by older versions of the app. An unknown path
returns `404 {"error":"not_found"}`, an unsupported method `405` with `Allow`.

Authentication: `POST /api/v1/auth/login` issues a token to send as
`Authorization: Bearer <token>`. Errors have the form
`{"error": "<codice>", "detail"?: "..."}`; rate limits return `429`.
The full list of routes is in `api/config/endpoints.json` and at
`GET /api/v1/endpoints`.

| Area | Main routes |
|---|---|
| Account | `POST auth/register` `{online_id, password, email?, registration_key?, accept_terms: true}` · `POST auth/login` · `POST auth/logout` · `POST auth/password` · `GET me` · `GET account/export` · `POST account/delete` `{password}` · `POST account/terms` `{version}` |
| Profile and friends | `GET/POST profile` · `GET friends` · `POST friends/request\|accept\|decline` `{online_id}` · `DELETE friends/:onlineId` · `GET friends/suggestions` · `GET users/search?q=` · `GET users/:onlineId` |
| Presence and home | `GET/POST presence` · `POST status` `{mode, message?}` · `GET sync?since=` (also a heartbeat) · `GET activity` · `GET/POST notifications…` |
| Privacy and blocking | `GET/POST privacy` · `POST/DELETE users/:onlineId/block` · `GET blocks` · `POST users/:onlineId/report` |
| Messages | `GET messages` · `GET/POST messages/:onlineId` |
| Party | `GET/POST party` · `POST party/invite\|join\|decline\|leave\|mute` · `GET/POST party/messages` · `GET/POST party/voice` (binary audio, long-poll) |
| Wall | `POST posts` · `GET feed?before=&limit=` · `GET users/:onlineId/posts` · `GET/DELETE posts/:id` · `POST posts/:id/like` · `GET/POST posts/:id/comments` · `DELETE …/comments/:commentId` · `POST …/report` |
| Groups | `GET/POST groups` · `GET/POST groups/:id` · `GET/POST groups/:id/messages` · `POST groups/:id/members\|leave` · `DELETE groups/:id/members/:onlineId` |
| Playtime and invites | `GET stats/me` · `GET stats/friends?game_id=&period=week\|all` · `POST invites` |
| Media | `POST media/upload?kind=avatar\|cover&ext=` (body = file) · `POST media/clear?kind=` · `GET media/:id/:frame` |
| News and games | `GET news?game_id=` · `GET news/:newsId/image` · `GET games/:gameId` |
| Browser | `GET browse?url=` or `?q=` (page reduced to blocks) · `GET browse/img/:id` |
| Store | `GET/POST store/apps` · `GET/POST/DELETE store/apps/:id` · `GET store/apps/:id/cover\|icon\|shot/:n\|download` · `POST store/apps/:id/vote\|rate\|report` · `GET/POST store/apps/:id/comments` · `DELETE …/comments/:commentId` |
| Personal library | `GET/POST library/source` · `POST library/sync` · `GET library/items` · `GET library/items/:id[/cover\|/shot/:n\|/download]` |
| Documents | `GET legal` (JSON for the app) |

Outside `/api/v1`: `GET /` (api description, or the website if `SITE_DIR`
contains an `index.html`), `GET /healthz`, `/legal`,
`/legal/privacy|terms|licenses`, `/source`, `/source/omega-src.tar.gz`,
`/admin` and `/admin/api/...`.

### Store and library

Homebrew entries point to a direct link (`.pkg`, `.zip` or `.elf`); images on
third-party sites are downloaded and served by the server, while the file to
install is downloaded by the console. `GET store/apps/:id/download` resolves the
link and returns
`{url, filename, kind, size, title_id, version, category, summary, homepage_url}`.

The personal library is filled from a JSON file provided by the user:

```json
{ "name": "La mia libreria", "games": [
  { "id": "g1", "title": "...", "platform": "PS5", "version": "1.00",
    "title_id": "CUSA12345", "cover": "https://.../c.jpg",
    "images": ["https://.../s1.jpg"], "url": "https://.../gioco.pkg", "type": "pkg" }
]}
```

Also accepted: `items`/`library` instead of `games`,
`download_url`/`file`/`pkg` for `url`, `image`/`icon` for `cover`, `screenshots`
for `images`, `kind` for `type` (otherwise it is inferred from the extension).

### Store social features and feedback

`lab_store_install` records who installed what (from `…/download`):
`GET store/apps?sort=friends|trending` and the `friends`, `friends_count` and
`wished` fields of every card feed the "Popular with your friends" and
"Trending" shelves. `POST store/apps/:id/wish {on}` is the wish list: when the
author publishes a new version, everyone who wished for or installed the app
gets a `store_update` notification. `POST store/apps/:id/recommend {online_id}`
recommends a homebrew to a friend (`store_recommend` notification) and
`GET store/creators` returns the featured creators. `POST feedback {kind:
bug|idea, text, log?}` stores bug reports and feature requests sent from the app
(`lab_feedback`, admins are notified); read them in the panel under "Bugs &
requests".

### Web app, linked console and notifications

`GET /app` serves the Omega web app (`api/webapp/`, a single installable page,
local files only). The console announces itself with `POST console/announce
{lan_ip, port, token}` every 30 s (the `omega_redirect` service); `GET console`
tells whether it is on (announced in the last 90 s) and returns a direct link
`http://<ip>:9095/#k=<token>` to its page on the home network, only to the same
account: files from the phone or PC go there, never to the server. `POST
console/queue {app_id}` is "Install on PS5" from the Store on the phone; the
console reads the queue with `GET console/queue`. Notification preferences
(`GET/POST notifications/prefs`, `POST notifications/prefs/friend`) apply when
a notification is created: a type switched off, "favourites only" or a muted
friend means it isn't created; quiet hours or a game in progress keep it in the
list with `silent`.

### Party voice

In-memory relay (last 8 s per party). `POST party/voice?codec=opus|adpcm&seq=`
with a chunk of up to 8 KB; `GET party/voice?after=<cursore>&wait=1` waits up to
1.5 s and replies in little-endian binary:
`"OVC1" u32 next_cursor u16 count`, then for each chunk
`u8 oid_len, oid, u8 codec (1=opus, 2=adpcm), u32 seq, u16 len, data`.

### Adding a route

1. a line in `api/config/endpoints.json` (`method`, `path`, `handler`, `family`, `auth`);
2. the function in `api/src/endpoints/<modulo>.js`: it receives
   `{ req, res, url, params, auth, admin, clientIp }` and returns `{ status, body }`
   (or `{ sent: true }` if it writes the response itself);
3. if you need tables or columns, a new migration `api/migrations/NNN_nome.sql`.

## Legal documents and source code

The privacy policy, terms of use and licenses are in `api/src/legal.js`, the
single source for the app (`GET /api/v1/legal`) and for the web (`/legal/*`).
They describe what the service actually does: if the data collected changes,
update the texts and `TERMS_VERSION`, and the app asks for consent again
(`terms.needs_accept` in `GET /api/v1/me`). The operator's details come from
the `LEGAL_*` variables.

The source code of the console app (GPL-3.0-or-later) can be downloaded from
`/source`: the archive is generated on the fly from `client/` and `daemon/`,
mounted read-only in the api container. If you modify the app and distribute
it, the GPL requires you to publish its source: this does it for you.

## License

GPL-3.0-or-later.
