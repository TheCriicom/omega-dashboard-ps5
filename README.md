<p align="center">
  <picture><source media="(prefers-color-scheme: dark)" srcset="docs/img/omega-mark.svg"><img src="docs/img/omega-mark-light.svg" width="96" alt="Omega"></picture>
</p>

<h1 align="center">Omega</h1>

<p align="center">
  The open-source dashboard for homebrew-enabled PS5 consoles: games, Store, music, friends and party in one place.<br>
  Developed by <a href="https://outlinedigital.it"><b>TheCriicom</b></a>.
</p>

<p align="center"><sub><b>English</b> · <a href="README.it.md">Italiano</a> · <a href="README.ja.md">日本語</a> · <a href="README.fr.md">Français</a> · <a href="README.es.md">Español</a> · <a href="README.de.md">Deutsch</a> · <a href="README.nl.md">Nederlands</a> · <a href="README.pt-PT.md">Português (Portugal)</a> · <a href="README.pt-BR.md">Português (Brasil)</a> · <a href="README.ru.md">Русский</a> · <a href="README.ko.md">한국어</a> · <a href="README.zh-Hans.md">简体中文</a> · <a href="README.zh-Hant.md">繁體中文</a> · <a href="README.fi.md">Suomi</a> · <a href="README.sv.md">Svenska</a> · <a href="README.da.md">Dansk</a> · <a href="README.nb.md">Norsk bokmål</a> · <a href="README.pl.md">Polski</a> · <a href="README.tr.md">Türkçe</a> · <a href="README.cs.md">Čeština</a> · <a href="README.hu.md">Magyar</a> · <a href="README.el.md">Ελληνικά</a> · <a href="README.ro.md">Română</a> · <a href="README.th.md">ไทย</a> · <a href="README.vi.md">Tiếng Việt</a> · <a href="README.id.md">Bahasa Indonesia</a> · <a href="README.uk.md">Українська</a></sub></p>

<p align="center">
  <a href="https://play.omegasuite.it">Website</a> ·
  <a href="https://play.omegasuite.it/installa">Install</a> ·
  <a href="server/README.md">Host a server</a> ·
  <a href="client/README.md">Development</a>
</p>

![Omega Home](docs/screenshots/home.png)

Omega is a dashboard built for homebrew users, and your Home screen if you want
it to be (it asks on first launch): installed games and homebrew sit in the same
row, the Store installs with one button, music keeps playing while you game and
your friends are always one press away. It runs as homebrew (SDL2, software
renderer) and talks to a server that anyone can host.

## What's inside

- **Home** with games and homebrew together, dynamic backgrounds from the
  artwork, direct launch (games through LncUtil, homebrew through websrv, ELF
  payloads in the background).
- **Store** of open-source homebrew: search, category shelves, votes, ratings
  and comments. It detects `.pkg`, `.zip` and `.elf` on its own and installs
  each one in the right place.
- **My library**: your own game backups with a download link and a cover,
  added from the console or from your phone, or imported from a JSON file
  (once, or linked so it stays in sync). Stored only on the console.
- **Music**, also during games: internet radio (radio-browser), Navidrome and
  other Subsonic servers, USB files, any audio link. Playback runs in the
  background daemon (FFmpeg audio-only build), so it doesn't stop when you
  launch a game.
- **Phone remote**: the daemon serves a web page on port 9095. Scan the QR
  code shown on the console, type the PIN, and control the music, send audio
  files, and manage My library from any phone, tablet or PC browser.
- **HEN detection**: at startup Omega recognises OnionHEN, etaHEN, pldmgr or
  ps5_autoloader, tells you what's missing and, after you confirm, installs and
  enables its services in the right place. On OnionHEN it adds a page to the
  in-game menu (L2 + R3) with music controls.
- **System tools**: temperatures, fan threshold, storage and a file manager.
- **Community**: a wall with posts, likes and comments; group chats; people
  you may know; playtime and a friends leaderboard.
- **Party** with chat and voice (Opus), game invites, custom status (online,
  away, do not disturb, invisible).
- **Game Base**: friends, requests and messages.
- **Privacy**: blocking, reporting, who can message you, data export, account
  deletion.
- **Themes**, generated ambient music, a reading browser.
- **27 languages**: the app follows the console language automatically (and
  can be changed in Settings).
- **Choose your server**: add the address of any Omega server in Settings; the
  official one always stays available.

| | |
|---|---|
| ![Store](docs/screenshots/store.png) | ![Community](docs/screenshots/community.png) |
| ![Party](docs/screenshots/party.png) | ![Playtime](docs/screenshots/stats.png) |
| ![Phone remote on a PC](docs/screenshots/remote-music.png) | ![My library from a PC](docs/screenshots/remote-library.png) |

## Repository layout

| Folder | |
|---|---|
| [`client/`](client) | the console app (C, SDL2) and a desktop build to develop it on a Mac |
| [`daemon/`](daemon) | background payload: music player, phone remote web page, My library, notifications during games, and (only if you choose it) reopening Omega when you go back to the Home |
| [`onionhen-plugin/`](onionhen-plugin) | OnionHEN plugin: an Omega page in the in-game menu with music controls |
| [`server/`](server) | Node.js API, proxy, moderation panel and Docker Compose to host a server |
| [`docs/`](docs) | architecture and images |

## Installing Omega on the console

You need a jailbroken PS5 with homebrew support: a HEN such as OnionHEN or
etaHEN, or a launcher such as [websrv](https://github.com/ps5-payload-dev/websrv)
with a payload loader.
The easiest way is [play.omegasuite.it/installa](https://play.omegasuite.it/installa),
opened in the console's browser. Alternatively, download the package from the
website and copy `data/` to the console over FTP.

## Hosting a server

```sh
git clone https://github.com/TheCriicom/omega-dashboard-ps5.git
cd omega-dashboard-ps5/server
./scripts/install.sh
```

The script creates `.env` with random secrets and starts the containers. The
full guide, with automatic HTTPS through Caddy, is in
[`server/README.md`](server/README.md). Then, on the console:
**Settings → Server → Add a server**.

## Development

- App: [`client/README.md`](client/README.md) — PS5 build with
  [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) and a desktop build
  with Homebrew's SDL2, driven by a command file to test the UI without a console.
  Translations live in `client/i18n/` (one JSON per language).
- Server: [`server/README.md`](server/README.md) — Node.js ≥ 20 and PostgreSQL,
  end-to-end tests with `npm test`.
- Architecture: [`docs/architecture.md`](docs/architecture.md).

Contributions are welcome: see [CONTRIBUTING.md](CONTRIBUTING.md).

## Author

Omega is developed by **TheCriicom** — [outlinedigital.it](https://outlinedigital.it).

## License

Copyright © 2026 TheCriicom and the Omega contributors.
Omega is free software: [GNU GPL v3 or later](LICENSE). Third-party components
and their licenses are listed in
[`client/THIRD-PARTY-NOTICES.md`](client/THIRD-PARTY-NOTICES.md).

Omega is an independent project and is not affiliated with, endorsed or
sponsored by Sony Interactive Entertainment. "PlayStation" and "PS5" are
trademarks of their respective owners. Omega does not contain or distribute
games.
