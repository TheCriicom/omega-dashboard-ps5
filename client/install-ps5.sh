#!/usr/bin/env bash
# Installa la Omega UI e il demone omega_redirect sulla PS5 via FTP (ftpsrv, porta 2121).
#
#   ./install-ps5.sh                 # cerca la console sulla rete locale
#   ./install-ps5.sh 192.168.1.5     # IP esplicito
#   ./install-ps5.sh --launch        # installa e avvia subito la UI (serve websrv, porta 8080)
#
#   bin/omega_ui.elf       → /data/homebrew/OmegaUI/OmegaUI.elf (con homebrew.js e icona)
#   bin/omega_redirect.elf → /data/pldmgr/payloads/OmegaRedirect/omega_redirect.elf
# Il demone nuovo entra in funzione al prossimo avvio della console.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN="$HERE/../bin"
IP=""; LAUNCH=0
for a in "$@"; do
  case "$a" in
    --launch) LAUNCH=1 ;;
    *) IP="$a" ;;
  esac
done

[ -f "$BIN/omega_ui.elf" ] || { echo "manca $BIN/omega_ui.elf: lancia prima ./build.sh"; exit 1; }

if [ -z "$IP" ]; then
  NET="$(ipconfig getifaddr en0 2>/dev/null || ipconfig getifaddr en1 2>/dev/null || true)"
  [ -n "$NET" ] || { echo "non trovo la rete locale: passa l'IP della console"; exit 1; }
  PREFIX="${NET%.*}"
  echo "Cerco la PS5 su $PREFIX.0/24 (porta FTP 2121)..."
  for i in $(seq 1 254); do
    (nc -z -G 1 "$PREFIX.$i" 2121 >/dev/null 2>&1 && echo "$PREFIX.$i") &
  done | head -1 > /tmp/omega-ps5-ip.$$ || true
  wait 2>/dev/null || true
  IP="$(cat /tmp/omega-ps5-ip.$$ 2>/dev/null || true)"; rm -f /tmp/omega-ps5-ip.$$
  [ -n "$IP" ] || { echo "PS5 non trovata: è accesa con ftpsrv attivo?"; exit 1; }
fi
FTP="ftp://$IP:2121"
echo "Console: $IP"

up() { curl -s --ftp-create-dirs -T "$1" "$FTP$2" && echo "  ✓ $2" || { echo "  ✗ $2"; exit 1; }; }

up "$BIN/omega_ui.elf"            /data/homebrew/OmegaUI/OmegaUI.elf
up "$HERE/homebrew.js"            /data/homebrew/OmegaUI/homebrew.js
[ -f "$HERE/sce_sys/icon0.png" ] && up "$HERE/sce_sys/icon0.png" /data/homebrew/OmegaUI/sce_sys/icon0.png
if [ -f "$BIN/omega_redirect.elf" ]; then
  up "$BIN/omega_redirect.elf"    /data/pldmgr/payloads/OmegaRedirect/omega_redirect.elf
fi

if [ "$LAUNCH" = 1 ]; then
  echo "Avvio Omega via websrv..."
  curl -s --max-time 8 "http://$IP:8080/hbldr?pipe=0&daemon=0&path=/data/homebrew/OmegaUI/OmegaUI.elf&cwd=/data/homebrew/OmegaUI" >/dev/null \
    && echo "  ✓ avviata" || echo "  ✗ websrv non risponde su :8080"
fi
echo "Log sulla console: $FTP/data/Omega/omega-ui.log"
