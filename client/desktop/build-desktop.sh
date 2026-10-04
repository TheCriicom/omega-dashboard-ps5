#!/usr/bin/env bash
# Build desktop della Omega UI (macOS, SDL2/SDL2_ttf/SDL2_image e curl da Homebrew).
#
#   OMEGA_URL=http://127.0.0.1:18080 OMEGA_DATA=/tmp/omega ./build-desktop.sh && ./omega-ui-desktop
#
# Facoltativi:
#   MINIZIP_SRC=<dir con minizip/unzip.c>   installazioni vere dello Store in $OMEGA_DATA/sysroot
#   QUICKJS_INC=<dir con quickjs/quickjs.h> QUICKJS_LIB=<libquickjs.a>   esegue gli homebrew.js
# I comandi di debug (tasti, testo, screenshot) si scrivono in $OMEGA_DATA/omega-ui.cmd.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$HERE/../source"
URL="${OMEGA_URL:-https://play.omegasuite.it}"
DATA="${OMEGA_DATA:-$HERE/data}"
mkdir -p "$DATA"

# tabelle delle traduzioni (source/i18n_data.c è già nel sorgente: Node serve solo per rigenerarle)
if command -v node >/dev/null 2>&1; then node "$HERE/../tools/i18n-gen.mjs" || { echo "traduzioni non valide"; exit 1; }; fi

# net.c e ime.c sono solo per la console (qui li sostituisce desktop.c);
# update.c c'è solo nella build ufficiale.
FILES=()
for f in "$SRC"/*.c; do
  case "$(basename "$f")" in net.c|ime.c|update.c) ;; *) FILES+=("$f") ;; esac
done

OPT=()
[ -f "$SRC/update.c" ] && OPT+=(-DOMEGA_UPDATES "$SRC/update.c")
if [ -n "${MINIZIP_SRC:-}" ]; then
  OPT+=(-DOMEGA_HAVE_MINIZIP -I"$MINIZIP_SRC" "$MINIZIP_SRC/minizip/unzip.c" "$MINIZIP_SRC/minizip/ioapi.c" -lz)
fi
if [ -n "${QUICKJS_LIB:-}" ]; then
  OPT+=(-DOMEGA_HAVE_QUICKJS -I"$QUICKJS_INC" "$QUICKJS_LIB" -lm)
fi

cc -O2 -Wall -std=gnu17 -DOMEGA_DESKTOP -DOMEGA_BASE_URL="\"$URL\"" -DOMEGA_DIR="\"$DATA\"" \
  $(sdl2-config --cflags) -I/opt/homebrew/include/SDL2 \
  "${FILES[@]}" "$HERE/desktop.c" ${OPT[@]+"${OPT[@]}"} \
  -o "$HERE/omega-ui-desktop" \
  $(sdl2-config --libs) -L/opt/homebrew/lib -lSDL2_ttf -lSDL2_image -lcurl
echo "FATTO: $HERE/omega-ui-desktop  (dati: $DATA, server: $URL)"
