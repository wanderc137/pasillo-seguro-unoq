#!/bin/bash
# Compila el binario C para la UNO Q (aarch64) usando el cross-toolchain del host.
set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$DIR"

BOARD="${BOARD:-arduino@192.168.1.5}"
SSH_PORT="${SSH_PORT:-2222}"
KEY="${KEY:-$HOME/.ssh/unoq_key}"

if [ ! -f third_party/libjpeg-arm64/libjpeg.so.62 ]; then
    echo "Falta libjpeg arm64; copiando desde la placa..."
    mkdir -p third_party/libjpeg-arm64
    scp -i "$KEY" -P "$SSH_PORT" -o StrictHostKeyChecking=no \
        "$BOARD:/usr/lib/aarch64-linux-gnu/libjpeg.so.62.3.0" \
        third_party/libjpeg-arm64/libjpeg.so.62
fi

if ! command -v aarch64-linux-gnu-gcc >/dev/null 2>&1; then
    echo "Falta aarch64-linux-gnu-gcc en el host." >&2
    echo "Instalalo con: sudo apt-get install -y gcc-aarch64-linux-gnu" >&2
    exit 1
fi

make arm64
file pasillo-seguro-arm64
echo "OK: $DIR/pasillo-seguro-arm64"
