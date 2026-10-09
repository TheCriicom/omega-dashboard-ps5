# Changelog

## 2026.10.09.4

- **Home order is yours**: from the options of a game, a folder or one of
  Omega's apps pick **Move** and bring it wherever you want with the arrows
  (L2/R2 send it to the start or the end). The picked tile lifts up and the
  others slide out of its way; ✕ keeps the new order, ◯ puts everything back.
  Works in every home layout. Saved in `home-layout.txt`, so it survives
  restarts; new games show up at the front. Settings › Home and games ›
  Home order goes back to the automatic order.
- Community, Store, Browser, Install PKG, Updates and Mobile app can now be
  moved and hidden like any other tile, and brought back from Hidden apps.
- Tiles in the home row slide to their new place whenever the row changes
  (reordering, hiding, folders) instead of jumping.
- Server: the proxy no longer reuses a connection to the API that the API is
  closing (keep-alive 65 s on the API, 30 s idle limit on the proxy): that
  race caused the occasional 502 "socket hang up".

## 2026.10.09.3

- **Installer**: if the HTTPS download through SceHttp fails at any step, it
  retries over plain HTTP with its own socket (the console's DNS resolver, the
  server IP as a last resort). `/updates` and `/download` are now served over
  plain HTTP too; integrity still comes from the Ed25519 signature and the
  SHA-256 of every file. The installer source is now in `installer/`.
- Game updates screen: no 64 KB buffer on the stack.

## 2026.10.09.2

- **Turning off, restarting and rest mode**: the daemon now watches the
  system power state (the `SceSystemStateMgrInfo` / `SceSystemStateMgrStatus`
  kernel flags, same method as ShadowMountPlus and OnionHEN) and, before the
  console goes down, stops music, party voice, the control server and PatchDL
  downloads and unmounts the games started from external drives (the one in
  the foreground stays mounted during rest mode). It resumes by itself a few
  seconds after waking up (`daemon/source/power.c`).
- Turn off / Restart / Rest Mode from Omega's menu refuse while an install,
  a copy or a cloud save job is running, unmount the external games and ask
  the daemon to quiesce first.
- `/system_ex` is no longer left mounted read-write after launching a game
  from an external drive.
- Daemon: audio output errors no longer spin the CPU; party and notification
  polling slow down when not needed and back off without network; music
  uploads and voice commands no longer block the control server; the UI only
  restarts the daemon when it is really stuck; crash handler on its own stack;
  SIGTERM handled; logs capped at 1 MB.
- Files written with fsync before rename (updates, loader autoload entries,
  library, player state, game copies); interrupted game copies are detected
  and cleaned up.
- Installer: every network step is written to `/data/Omega/omega-installer.log`,
  which is now part of the diagnostics.

## 2026.10.09.1

- **Game updates**: Omega finds the latest official update for every installed
  game that runs on your firmware, downloads it from Sony's servers and
  installs it. Home tile and Settings › Home and games. Downloads resume after
  a reboot and keep going with Omega closed; the daemon starts the install when
  a download finishes. The work is done by [PatchDL](https://github.com/knutwurst/ps5-patchdl)
  by Knutwurst (GPL-3.0), shipped as a payload from `patchdl/`.
- **Power options**: rest mode, restart and turn off from the power menu.
- **Fan**: the threshold is now written as a read-modify-write of the whole
  28-byte controller config and verified (it used to send 10 bytes, mostly
  zeros); the daemon puts it back when a game resets it to 91 °C. Up to 85 °C.
  Method from [fan_target](https://github.com/drakmor/fan_target).
- **System**: fan speed, hottest of the 16 SoC sensors, CPU frequency, console
  model, uptime and the kernel's real firmware version (readings as in
  [ps5-exporter](https://github.com/Marice/ps5-exporter)).
- **Interface sounds** redone: FM bells, filtered air and a small reverb.
- **Sessions** no longer expire 24 hours after login: they expire after 90 days
  without use and renew on every request (migration 019 brings back sessions
  that had expired in the last 30 days). The daemon stops retrying a token the
  server rejects instead of polling every few seconds.

## 2026.10.08.1

- **Your recap**, like PlayStation's Wrap-Up: a week, a month or a year of play
  as story cards that play by themselves. Time played compared with the period
  before, your game, your top 5, your rhythm (hours and weekdays), records
  (longest session, days in a row, new games), trophies with the rarest one,
  the friends you played with (same game at the same time), an anonymous
  comparison with the community and, at the end, your player profile (Night
  owl, Marathoner, Explorer, Trophy hunter…) to share on the feed.
- On the console: Community › Play time › Triangle; once a week a notification
  says last week's recap is ready. In the web app: Profile › Your recap, with a
  shareable image drawn in the browser.
- Server: every play session is now logged with start and end (migration 018,
  kept 400 days) for rhythm, records and "played together"; `GET
  /api/v1/wrap`; 38 new checks.

## 2026.10.07.3

- **Cloud saves**, like PS Plus: after every session the saves that changed go
  online by themselves; restore them on the same or another console from
  Settings › Home and games › Cloud saves or from a game's options. The last 3
  versions per game are kept; 2 GB per account.
- **End-to-end encrypted**: saves are read from a mounted *copy* of the save
  image (the original is never touched), packed and encrypted on the console
  with XChaCha20-Poly1305 under a random 32-byte key. The server only stores
  that key wrapped with the user's passphrase (Argon2id on the console), so it
  can't read saves; any tampering fails the per-chunk MAC and nothing is
  written.
- **Safe restores**: only into a save the game already created on this console,
  keeping its `sce_sys/param.sfo` (bound to the local user); the previous save
  is kept so "Undo last restore" puts it back. Archive paths are validated
  before anything is written.
- **Hardened server side**: the server never opens, decompresses or parses
  uploads; exact-length 8 MB chunks with SHA-256, a fixed header check,
  per-account quota and concurrency limits, a server-wide cap and minimum free
  disk, random server-chosen file names, ownership checks on every request,
  downloads served as `octet-stream` with `nosniff` and a `sandbox` CSP, and
  unfinished uploads purged after 2 hours. 47 new end-to-end checks.
- Web app: a **Cloud saves** page (list, storage used, delete a version).

## 2026.10.07.2

- **Mobile app** (`/app` on the server): a real app-like web app for phones
  and PCs (installable, works offline-first), signed in with the Omega account.
  Friends online, chat, parties, community feed and groups, leaderboards,
  records, trophies, user profiles, the full Store with wish list, recommend
  and **"Install on PS5"** (queued on the server, the console picks it up and
  installs it), notifications and notification settings, all working with the
  console off.
- **The console on the home network**: the Omega service announces its LAN
  address every 30 s; with the console on, the web app opens the console's own
  page in one tap (paired automatically) to upload games from the PC and manage
  My library and the games JSON. Files go straight to the console over Wi-Fi,
  never through the server.
- On the console: a new **Mobile app** tile after Install PKG shows the QR code
  of the web app; every screen that used to talk about the phone remote now
  shows the web app link. The Wi-Fi remote with PIN stays in System as an
  advanced option.
- **Notifications your way**: per type (everyone, favourites only, nobody),
  favourite or muted friends, quiet hours and what to show while playing, from
  Settings › Account › Notifications or the web app. "A friend is online" and
  "A friend started playing" now come only from favourites by default. Quiet or
  in-game notifications stay in the list without a popup.
- **Storage & moves**: see where each game is and how big it is, move or copy
  folder games between internal storage and external drives (space check,
  progress on the Home row, verification before the original is removed), move
  or copy `.pkg` files to a drive; "Move to another drive" in each game's
  options.
- Server: migrations 015 (console link, install queue) and 016 (notification
  preferences), new routes, tests.

## 2026.10.07.1

- **Homebrew launch fixed**: Omega no longer quits on its own after asking
  websrv to start a homebrew (websrv must close the foreground app itself; when
  both happened at once the launch failed with 503). Omega checks that websrv
  answers, makes ELFs executable, uses the homebrew folder as working
  directory, repairs a broken `FAKE00000` and tells the daemon not to relaunch
  the UI for 30 s, so it no longer kills the homebrew that just started.
- **Text that sometimes became huge everywhere** is fixed: the font cache had
  48 slots for more than 60 sizes; past the limit a new font was opened every
  frame until memory ran out and the fallback was the first (large) font.
- **Install PKG**: a new tile lists the `.pkg` files on USB sticks and drives,
  in `/data/pkg`, in uploads and in folders you choose, with title, icon and
  Content ID read from the package. Installs show on the Home row like on PS4:
  game icon, real progress from the system installer, phase and errors, with a
  queue. Correct `sceAppInstUtilInstallByPackage` structures (base games used
  to fail with 0x80B2116F), ShellCore authid during the call, etaHEN DPI as a
  fallback. Downloaded and uploaded files are removed once installed; orphan
  files are cleaned up.
- **Built-in ShadowMount**: choose your game and PKG folders; with automatic
  mounting, games on a drive are mounted and registered as soon as it's
  plugged in, and show up on the console Home too. Games mounted by
  ShadowMount, freshly installed pkgs and uploaded homebrew appear on the Home
  without restarting Omega.
- **Phone/PC remote**: upload games and install them right away ("install as
  soon as it arrives", "Install on console" on every library game), a guide to
  the games JSON with a downloadable example, and the same guide on the console
  (Store › Library › How the JSON works).
- **Six menu styles**: Omega, Classic PS4, XMB (PS3), Grid, Carousel, Cinema.
- **Store**: popular with your friends, trending, wish list with update
  notifications, recommend to a friend, featured creators, friends' avatars on
  the tiles, a richer banner. Store, Browser and Install PKG are now apps on
  the Home row after Community; the top bar is lighter.
- **Browser**: opens the full PS5 web browser (system WebKit) for modern sites,
  full-page or reader mode, quick sites on the start page.
- **Settings in sections** (Account, Look, Home & games, System, Help), "Why
  Omega", and **report a bug or request a feature** (with the log attached if
  you want); admins read them in the panel under "Bugs & requests".
- 15 new icons; the OnionHEN plugin reconnects by itself after rest mode or
  when OnionHEN starts late; the daemon reopens its port after rest mode.

## 2026.10.05.8

- **Folders on the Home**: game options › Move to a folder (existing folder or
  a new one). A folder is a tile with a 2×2 mosaic of its first icons; it opens
  a panel with its games (X plays, Triangle for the app's options, Square to
  rename or delete the folder). Deleting a folder puts its apps back on the
  Home; nothing is uninstalled.
- **Hide apps**: game options › Hide from Home. Hidden apps come back from
  Settings › Hidden apps. Hidden and foldered apps stay installed and keep
  working everywhere else (profiles, records, notifications).
- The layout is stored on the console (`/data/Omega/home-layout.txt`).

## 2026.10.05.7

- **16 new illustrated avatars** (32 in total): dragon, unicorn, shark,
  octopus, bunny, tiger, wolf, zombie, pirate, wizard, gamer with headset,
  hacker, oni mask, flying saucer, slime and cyborg. The avatar picker now shows
  the 32 characters in four rows and the 16 colours in a single row. Avatar
  indices go up to 47; older clients show the new ones as a coloured initial.

## 2026.10.05.6

- **Community on the Home**: a Community tile sits before the games, so the
  feed, groups, people and play time are one press away instead of hidden in
  the Control Center.
- **Records** (Community › Records): play time across all members, not just
  friends. Players, most played games (with each game's record holder and a
  per-game leaderboard) and marathons (one player on one game), for this week
  or all time. Game totals count everyone, without names.
- **Trophies**: the app reads the trophies earned on the console
  (`TRPTITLE.DAT` per user, definitions and icon from the set's UCP archive)
  and sends them to the server. They show up in profiles (your own from the
  Trophies button) and in a public leaderboard by points. The state file format
  is undocumented: the server recognises the record table instead of assuming
  a layout, and keeps files it cannot read yet (`parsed=false`) to re-read them
  later without another client release.
- **Privacy** (Settings › Privacy) now holds every choice in one menu: status,
  who can message you, friend requests, activity shown to friends, appearing in
  the public Records, who sees your trophies (everyone, friends, nobody),
  trophy import (turning it off deletes the imported ones), blocked users,
  documents, data export and account deletion.
- Games show their real name and icon to people who don't own them: consoles
  send the name and icon of installed games the server doesn't know yet.
- Presence turns itself off: a console that stops sending signals is marked
  offline after three minutes and comes back online on its next sync, without
  inheriting the game it was playing before.
- Privacy notice updated (version 2026-10-05.2): play time, game names and
  icons, trophies, and who can see them.

## 2026.10.05

Versions 2026.10.05.3, .4 and .5, released on the same day.

- **Customize** (Settings › Customize): more than 30 options in 8 groups and
  7 quick styles. 12 themes, 8 accent colours, background (game art, theme,
  pure black or `/data/Omega/wallpaper.jpg`), dimming, glass or solid panels,
  corners, selection style; icon size, labels, sort order (name, last played,
  games first), what the Home shows; 12/24 h clock, seconds, date, console
  temperature; particles (lights, stars, snow, fireflies, bubbles), animation
  speed, 60/30/auto fps; interface sound sets and background music moods;
  notification position, duration and Do Not Disturb; screensaver.
- **Games on external drives**: game folders on USB drives, extended storage
  and M.2 show up on the Home with a drive icon and disappear when the drive is
  unplugged. They start read-only from the drive (nullfs, like ShadowMount and
  dump_runner); kstuff is required. Installing a game folder or .zip with a
  drive plugged in asks where to put it.
- **Faster dashboard** with the software renderer: the theme tint is baked into
  the backgrounds, the scene behind an open panel is frozen instead of redrawn,
  and the Home drops to 30 fps when idle. 2 to 4 times less work per frame.
- **Music is heard again**: a background payload gets no audio output on PS5,
  so the daemon now streams the decoded audio to the app (127.0.0.1:9096),
  which plays it. During games music and party voice are not heard.
- **Party voice** rewritten: kept-alive HTTPS connections, 20 ms Opus frames
  sent in batches, adaptive jitter buffer. About 0.5 s of delay instead of
  several seconds piling up.
- **My library** reads FPKGi, lists with `pkg_url`/`content_id`, sizes like
  "45 GB" and files with a BOM, up to 2000 games; GitHub/Dropbox/Drive links are
  turned into direct links; a failed sync no longer empties the library and its
  result is shown on the console and on the phone.
- **Installs**: if the console refuses a .pkg link, Omega downloads it and
  installs the file; system installer errors are explained; truncated
  downloads are rejected; reinstalling an uploaded game folder no longer
  removes the installed game.
- News on the Home in the user's language (18 languages with their own
  sources, English for the rest); party and group system messages translated.
- "What's new" window after each update; "Send diagnostics" in Settings ›
  System and tools; the daemon reports technical error codes (no content).

## 2026.10.04

- Upload games straight from the phone or PC page: a .pkg, .zip or .elf file,
  or a whole game or homebrew folder (drag and drop on PC). Homebrew folders go
  to the Home at once; games land in My library and install from the console
  without downloading anything.

- Music, also during games: internet radio, Navidrome/Subsonic, USB files and
  audio links, played by the daemon in the background.
- Phone remote: a web page served by the console (QR code + PIN) to control the
  music, send audio files and manage My library from a phone, tablet or PC.
- My library: add games by hand with a link and a cover, from the console or the
  phone; JSON import (file, pasted text, or a linked URL kept in sync).
- HEN detection (OnionHEN, etaHEN, pldmgr, ps5_autoloader): Omega checks its
  services at startup and, after you confirm, installs and enables them.
- OnionHEN plugin with an Omega page in the in-game menu.
- Using Omega as the Home screen is now optional: it asks on first launch.
- System panel (temperatures, fan, storage) and a file manager.
- Store: ELF payloads go to the HEN payload folder when there is one.

## 2026.10.03

First public release.

- Home with games, websrv homebrew and ELF payloads in the same row.
- Homebrew Store: installs pkg, zip and elf; votes, ratings, comments,
  reports; personal library from JSON.
- Community: wall, groups, people you may know, playtime and friends
  leaderboard.
- Party with chat and voice (Opus), game invites, custom status.
- Privacy: blocking, reporting, who can message you, account export and
  deletion.
- Server selection from Settings.
- Server with Docker Compose, optional HTTPS through Caddy and a web
  moderation panel.
