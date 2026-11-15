#!/bin/bash
# Despliega pasillo-seguro en la Arduino UNO Q por SSH/tar. Sin sudo.
set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$DIR"

BOARD="${BOARD:-arduino@192.168.1.5}"
SSH_PORT="${SSH_PORT:-2222}"
KEY="${KEY:-$HOME/.ssh/unoq_key}"
REMOTE="${REMOTE:-/home/arduino/pasillo-seguro-unoq}"
BOARD_HOST="${BOARD#*@}"

if [ ! -f pasillo-seguro-arm64 ]; then
    echo "Compilando para arm64..."
    ./scripts/build_arm64.sh
fi

echo "== Copiando binario y web a $REMOTE =="
ssh -i "$KEY" -p "$SSH_PORT" -o StrictHostKeyChecking=no "$BOARD" \
    "mkdir -p '$REMOTE/web' '$REMOTE/scripts' '$REMOTE/docs' '$REMOTE/data'"

scp -i "$KEY" -P "$SSH_PORT" -o StrictHostKeyChecking=no \
    pasillo-seguro-arm64 "$BOARD:$REMOTE/pasillo-seguro.new"

scp -i "$KEY" -P "$SSH_PORT" -o StrictHostKeyChecking=no \
    web/index.html web/app.js web/style.css web/favicon.svg \
    "$BOARD:$REMOTE/web/"

scp -i "$KEY" -P "$SSH_PORT" -o StrictHostKeyChecking=no \
    scripts/servicio.sh "$BOARD:$REMOTE/scripts/servicio.sh"

scp -i "$KEY" -P "$SSH_PORT" -o StrictHostKeyChecking=no \
    config.example README.md LICENSE "$BOARD:$REMOTE/"

scp -i "$KEY" -P "$SSH_PORT" -o StrictHostKeyChecking=no \
    docs/*.md "$BOARD:$REMOTE/docs/"

ssh -i "$KEY" -p "$SSH_PORT" -o StrictHostKeyChecking=no "$BOARD" \
    "cd '$REMOTE' && mv pasillo-seguro.new pasillo-seguro && chmod +x pasillo-seguro scripts/servicio.sh"

echo "== Reiniciando servicio =="
ssh -i "$KEY" -p "$SSH_PORT" -o StrictHostKeyChecking=no "$BOARD" \
    "cd '$REMOTE' && ./scripts/servicio.sh restart"

echo "== Verificacion =="
sleep 1
curl -fsS --max-time 4 "http://${BOARD_HOST}:8020/api/health" || true
echo
curl -fsS --max-time 4 "http://${BOARD_HOST}:8020/api/status" || true
echo
echo "Listo. Abri http://${BOARD_HOST}:8020/ en el telefono o en la notebook."
