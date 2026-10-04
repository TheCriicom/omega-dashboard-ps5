<sub>**English** · [Italiano](architettura.md)</sub>

# Architecture

```
 PS5 console                                     server
┌──────────────────────────────┐         ┌──────────────────────────────────┐
│ Omega (homebrew, SDL2)       │  HTTPS  │ Caddy / reverse proxy            │
│  home · store · community    │ ──────► │   └─ proxy   logging, redaction  │
│  party (voice) · settings    │         │        └─ api   Node.js          │
│                              │         │              └─ PostgreSQL       │
│ omega_redirect (payload)     │ ──────► │                                  │
│  reopens Omega, notifications│         └──────────────────────────────────┘
└──────────────────────────────┘
```

## App (`client/`)

A single full-screen SDL2 app, 1920×1080, with a software renderer: that is
what works in the context of homebrew launched by websrv.

- **Main loop** (`main.c`): controller and keyboard input with key repeat, a
  stack of animated panels above the scene, toasts and messages.
- **Background networking** (`async.c`): two workers run the HTTPS requests
  (SceHttp) and the JSON parsing; callbacks come back on the main thread. A
  third worker decodes images. The interface never blocks.
- **Server** (`servers.c`): the api address is chosen at runtime; a saved
  session is valid only for the server it was created on.
- **Launching titles** (`home.c`, `hblaunch.c`, `payload.c`): games with
  `sceLncUtilLaunchApp`; websrv-format homebrew by running their
  `homebrew.js` with QuickJS, as the websrv launcher would; ELF payloads sent
  to the loader on port 9021.
- **Installation** (`install.c`): recognises pkg, zip and elf and puts them
  where they belong (AppInstUtil for pkg, `/data/homebrew` for homebrew,
  Payload Manager for ELF).
- **Voice** (`voice.c`): 16 kHz microphone from libSceAudioIn, voice activity
  detection, Opus at 16 kb/s, 200 ms packets sent to the server; receiving is
  a long-poll and playback goes through the mixer in `audio.c`.

## Server (`server/`)

- **proxy**: the only exposed service. Logs every request (one file per day,
  configurable retention), redacting credentials, tokens and cookies; bodies
  are never logged.
- **api**: routes declared in `config/endpoints.json` and implemented in
  `src/endpoints/`. HMAC-signed sessions, migrations applied at startup,
  moderation panel at `/admin` with a separate session.
- **PostgreSQL**: all the data; party voice stays in memory only.

## Main flows

**Sign-in** — `POST /api/v1/auth/login` returns a token; the app saves it in
`/data/Omega/session.json` and sends it as `Authorization: Bearer`.

**Sync** — every few seconds `GET /api/v1/sync` keeps presence alive and
brings counters, friends, party and new notifications.

**Store** — the app requests `GET store/apps/:id/download`; the server
resolves the link's redirects and returns the final URL, which the console
downloads directly from the author's site.

**Voice** — `POST party/voice` with a binary packet; the other members
receive it from `GET party/voice?after=<cursor>&wait=1`, which waits up to
1.5 s. The format is described in
[`server/README.md`](../server/README.md#party-voice).
