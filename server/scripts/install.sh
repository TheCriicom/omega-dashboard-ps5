#!/usr/bin/env bash
# Installa un server Omega con Docker Compose.
#
#   ./scripts/install.sh                      # chiede dominio e porta
#   OMEGA_DOMAIN=omega.example.org ./scripts/install.sh --yes
#
# Crea .env con segreti casuali (solo se non esiste già), costruisce le
# immagini e avvia i servizi. Con un dominio attiva anche HTTPS tramite Caddy.
set -euo pipefail

cd "$(dirname "$0")/.."
YES=0
[ "${1:-}" = "--yes" ] && YES=1

say()  { printf '\033[1;34m›\033[0m %s\n' "$*"; }
fail() { printf '\033[1;31m✗\033[0m %s\n' "$*" >&2; exit 1; }

command -v docker >/dev/null || fail "Docker non trovato: https://docs.docker.com/engine/install/"
docker compose version >/dev/null 2>&1 || fail "Serve Docker Compose v2 (docker compose)"
command -v openssl >/dev/null || fail "Serve openssl per generare i segreti"

ask() {  # ask <variabile> <domanda> <predefinito>
  local cur="${!1:-}"
  if [ -n "$cur" ] || [ "$YES" = 1 ]; then printf -v "$1" '%s' "${cur:-$3}"; return; fi
  read -r -p "$2 [$3]: " cur
  printf -v "$1" '%s' "${cur:-$3}"
}

if [ -f .env ]; then
  say ".env esiste già: lo lascio com'è (cambiare i segreti dopo il primo avvio disconnette tutti)"
  # shellcheck disable=SC1091
  set -a; . ./.env; set +a
else
  ask OMEGA_DOMAIN "Dominio per HTTPS (vuoto = solo http su una porta locale)" ""
  ask OMEGA_PORT "Porta locale del server" "8090"
  if [ -n "$OMEGA_DOMAIN" ]; then PUBLIC_BASE_URL="https://$OMEGA_DOMAIN"; else PUBLIC_BASE_URL="http://localhost:$OMEGA_PORT"; fi
  umask 077
  cat > .env <<EOF
PGUSER=omega
PGDATABASE=omega
PGPASSWORD=$(openssl rand -hex 24)
OMEGA_SESSION_SECRET=$(openssl rand -hex 32)
OMEGA_REGISTRATION_KEY=
OMEGA_DOMAIN=$OMEGA_DOMAIN
OMEGA_PORT=$OMEGA_PORT
PUBLIC_BASE_URL=$PUBLIC_BASE_URL

# titolare del servizio: compare in privacy e termini d'uso
LEGAL_CONTROLLER_NAME=
LEGAL_CONTACT_EMAIL=
LEGAL_CONTROLLER_VAT=
LEGAL_CONTROLLER_ADDRESS=
LEGAL_HOSTING_PROVIDER=

LOG_RETENTION_DAYS=30
STORE_AUTOHIDE_REPORTS=3
EOF
  say "Creato .env con segreti casuali"
fi

FILES=(-f compose.yml)
[ -n "${OMEGA_DOMAIN:-}" ] && FILES+=(-f compose.tls.yml)

say "Costruzione e avvio dei container (la prima volta richiede qualche minuto)..."
docker compose "${FILES[@]}" up -d --build --wait

say "Carico il catalogo dello Store"
docker compose exec -T api npm run --silent seed:store || say "Catalogo non caricato: riprova con  docker compose exec api npm run seed:store"

URL="${PUBLIC_BASE_URL:-http://localhost:${OMEGA_PORT:-8090}}"
cat <<EOF

  Server Omega pronto: $URL

  Sulla console: Impostazioni → Server → Aggiungi un server → $URL
  Primo amministratore (dopo esserti registrato dall'app):
    docker compose exec api npm run admin -- grant <tuo_online_id>
  Pannello di moderazione: $URL/admin

  Ricorda di compilare LEGAL_CONTROLLER_NAME e LEGAL_CONTACT_EMAIL nel .env
  (poi: docker compose up -d) se il server è aperto ad altre persone.

EOF
