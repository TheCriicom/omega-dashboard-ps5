# Changelog

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
