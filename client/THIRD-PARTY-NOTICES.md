# Omega — avvisi sui componenti di terze parti

Omega (app console, `omega-ui-src`) — Copyright (C) 2026 TheCriicom (outlinedigital.it) e i contributori di Omega.

Questo programma è software libero: puoi ridistribuirlo e/o modificarlo secondo
i termini della GNU General Public License, versione 3 o (a tua scelta)
qualsiasi versione successiva, pubblicata dalla Free Software Foundation
(testo completo in `LICENSE`). È distribuito SENZA ALCUNA GARANZIA, nemmeno
implicita di COMMERCIABILITÀ o di IDONEITÀ A UNO SCOPO PARTICOLARE.

Il sorgente corrispondente al binario distribuito si scarica da
`https://play.omegasuite.it/source` (con le istruzioni di compilazione:
`build.sh`, `CMakeLists.txt`).

Omega è un progetto indipendente, non affiliato a Sony Interactive
Entertainment. "PlayStation" e "PS5" sono marchi dei rispettivi titolari.

## Componenti collegati nel binario

| Componente | Licenza |
|---|---|
| ps5-payload-sdk (crt, libkernel/libc stub, port SDL2) | GPL-3.0-or-later |
| SDL2 | zlib |
| SDL2_ttf | zlib |
| SDL2_image | zlib |
| FreeType | FreeType License (FTL) |
| HarfBuzz | MIT ("Old MIT") |
| libpng | libpng License |
| libjpeg | IJG License — "this software is based in part on the work of the Independent JPEG Group" |
| libwebp | BSD-3-Clause |
| zlib | zlib |
| minizip | zlib |
| bzip2 | bzip2 License (BSD-style) |
| QuickJS | MIT |

I testi delle licenze di ciascun componente si trovano nei rispettivi progetti
e nell'SDK (`ps5-payload-sdk`). Le librerie di sistema della console (`libSce*`,
font in `/preinst`, compresi quelli per giapponese, cinese, coreano e thai) non
sono incluse né ridistribuite: si usano quelle già presenti sulla console. La
build desktop di prova usa allo stesso modo i font di macOS.

Il marchio di Omega (`source/logo_png.c`, `sce_sys/icon0.png`) è un disegno
originale del progetto.

## Contenuti dello Store

Gli homebrew elencati nello Store non fanno parte di questo programma: sono
opere dei rispettivi autori, scaricate dai loro siti, ciascuna con la propria
licenza (indicata nella scheda).
