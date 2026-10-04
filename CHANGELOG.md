# Changelog

## 2026.10.04

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
