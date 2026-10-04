# omega-redirect — avvisi

Demone di supporto dell'app Omega — Copyright (C) 2026 TheCriicom (outlinedigital.it) e i contributori di Omega.

Software libero sotto GNU General Public License versione 3 o successive
(testo in `LICENSE`), SENZA ALCUNA GARANZIA. Sorgente:
`https://play.omegasuite.it/source`.

Collegato con ps5-payload-sdk (GPL-3.0-or-later) e con le librerie di sistema
della console, che non vengono ridistribuite.

Il lettore musicale incorpora (collegamento statico):

- **FFmpeg 7.0.1** (libavformat, libavcodec, libswresample, libavutil),
  https://ffmpeg.org — compilato da `tools/build-ffmpeg-audio.sh` con soli
  decoder audio, contenitori e protocolli di rete, `--enable-version3`, senza
  componenti GPL: licenza GNU LGPL versione 3 o successive. Il sorgente usato è
  quello pubblicato su ffmpeg.org (versione indicata nello script).
- **OpenSSL 3** (dai pacchetti di ps5-payload-dev, per https),
  https://www.openssl.org — Apache License 2.0.
- **zlib** — licenza zlib.

Il plugin per OnionHEN (`omega-onion-plugin-src`) usa l'SDK dei plugin di
OnionHEN, https://github.com/OnionBuddies/onionHEN-plugin-sdk — GPL-3.0.
La soglia della ventola usa lo stesso comando di etaHEN
(https://github.com/etaHEN/etaHEN, GPL-3.0).
