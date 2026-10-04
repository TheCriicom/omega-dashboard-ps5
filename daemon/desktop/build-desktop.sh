#!/usr/bin/env bash
# Build desktop del lettore del demone (macOS, ffmpeg e sdl2 da Homebrew).
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
S="$HERE/../source"
B="$(brew --prefix)"
cc -O2 -Wall -Wextra -Wno-unused-parameter -o "$HERE/omega-player-desktop" \
  "$HERE/player-desktop.c" "$S/player.c" "$S/ctl.c" "$S/json.c" "$S/remote_page.c" "$S/lib.c" \
  -I"$B/include" -L"$B/lib" -lavformat -lavcodec -lavutil -lswresample -lSDL2 -lpthread
echo "FATTO: $HERE/omega-player-desktop"
