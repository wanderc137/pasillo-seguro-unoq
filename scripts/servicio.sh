#!/bin/bash
# Servicio local de pasillo-seguro (sin sudo, sin systemd).
# Uso: scripts/servicio.sh {start|stop|restart|status|logs [N]|boot-enable|boot-disable|boot-status|help}

set -u

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [ -f "$DIR/config.env" ]; then
    # shellcheck disable=SC1091
    set -a
    . "$DIR/config.env"
    set +a
fi

PORT="${PS_PORT:-8020}"
HOST="${PS_HOST:-0.0.0.0}"
DATA="${PS_DATA_DIR:-$DIR/data}"
BIN="$DIR/pasillo-seguro"
PIDFILE="$DATA/pasillo.pid"
LOG="$DATA/server.log"
LOCK="$DATA/servicio.lock"
CRON_TAG="pasillo-seguro-service"

die() { echo "ERROR: $*" >&2; exit 1; }

case "$PORT" in
    8000|8010) die "el puerto $PORT esta reservado (detector-tarjetas/superfoto); usa 8020" ;;
esac
case "$PORT" in
    ''|*[!0-9]*) die "PS_PORT invalido: $PORT" ;;
esac
if [ "$PORT" -lt 1 ] || [ "$PORT" -gt 65535 ]; then
    die "PS_PORT fuera de rango: $PORT"
fi

lan_ip() {
    ip -4 route get 1.1.1.1 2>/dev/null | awk '{for (i=1;i<=NF;i++) if ($i=="src") {print $(i+1); exit}}'
}

is_ours() {
    local pid="$1"
    [ -n "$pid" ] || return 1
    ps -p "$pid" -o args= 2>/dev/null | grep -q "pasillo-seguro"
}

health() {
    curl -fsS --max-time 2 "http://127.0.0.1:${PORT}/api/health" 2>/dev/null
}

wait_health() {
    local n="${1:-10}"
    local i=0
    while [ "$i" -lt "$n" ]; do
        if health >/dev/null 2>&1; then return 0; fi
        sleep 0.5
        i=$((i + 1))
    done
    return 1
}

start() {
    mkdir -p "$DATA" || die "no se pudo crear $DATA"
    if [ -f "$PIDFILE" ]; then
        local pid
        pid="$(cat "$PIDFILE" 2>/dev/null || true)"
        if is_ours "$pid"; then
            echo "pasillo-seguro ya esta corriendo (PID $pid) en http://$(lan_ip):${PORT}/"
            return 0
        fi
        rm -f "$PIDFILE"
    fi
    [ -x "$BIN" ] || die "no existe el binario $BIN; ejecuta make arm64"
    [ -d "$DIR/web" ] || die "no existe $DIR/web"

    exec 9>"$LOCK" || die "no se pudo abrir el lock"
    if ! flock -n 9; then
        die "otra operacion de servicio esta en curso"
    fi

    echo "Iniciando pasillo-seguro en el puerto ${PORT}..."
    nohup "$BIN" --port "$PORT" --web "$DIR/web" --data "$DATA" \
        >>"$LOG" 2>&1 9>&- &
    local pid=$!
    echo "$pid" >"$PIDFILE"
    if wait_health 20; then
        echo "OK. PID $pid. Pagina: http://$(lan_ip):${PORT}/"
        echo "Health: $(health)"
        return 0
    fi
    echo "No respondio el health; ultimas lineas del log:" >&2
    tail -n 20 "$LOG" >&2 || true
    return 1
}

stop() {
    local pid=""
    [ -f "$PIDFILE" ] && pid="$(cat "$PIDFILE" 2>/dev/null || true)"
    if ! is_ours "$pid"; then
        # fallback: find only our binary
        pid="$(pgrep -f "$DIR/pasillo-seguro" 2>/dev/null | head -1 || true)"
    fi
    if ! is_ours "$pid"; then
        echo "pasillo-seguro no esta corriendo."
        rm -f "$PIDFILE"
        return 0
    fi
    echo "Deteniendo PID $pid..."
    kill "$pid" 2>/dev/null || true
    local i=0
    while [ "$i" -lt 20 ] && kill -0 "$pid" 2>/dev/null; do
        sleep 0.25
        i=$((i + 1))
    done
    if kill -0 "$pid" 2>/dev/null; then
        echo "No termino con TERM; enviando KILL."
        kill -9 "$pid" 2>/dev/null || true
    fi
    rm -f "$PIDFILE"
    echo "Detenido."
}

status() {
    local pid=""
    [ -f "$PIDFILE" ] && pid="$(cat "$PIDFILE" 2>/dev/null || true)"
    if is_ours "$pid"; then
        echo "estado: corriendo"
        echo "pid: $pid"
        echo "url: http://$(lan_ip):${PORT}/"
        echo "datos: $DATA"
        echo "log: $LOG"
        local h
        h="$(health || true)"
        [ -n "$h" ] && echo "health: $h" || echo "health: sin respuesta"
    else
        echo "estado: detenido"
        echo "puerto: $PORT"
    fi
}

logs() {
    local n="${1:-50}"
    [ -f "$LOG" ] || die "no existe $LOG"
    tail -n "$n" "$LOG"
}

cron_line() {
    echo "@reboot $DIR/scripts/servicio.sh start >/dev/null 2>&1 # $CRON_TAG"
}

boot_enable() {
    local cur
    cur="$(crontab -l 2>/dev/null || true)"
    if echo "$cur" | grep -q "$CRON_TAG"; then
        echo "arranque automatico ya habilitado."
        return 0
    fi
    { echo "$cur"; cron_line; } | crontab - || die "no se pudo editar crontab"
    echo "arranque automatico habilitado (cron @reboot)."
}

boot_disable() {
    local cur
    cur="$(crontab -l 2>/dev/null || true)"
    if ! echo "$cur" | grep -q "$CRON_TAG"; then
        echo "arranque automatico no estaba habilitado."
        return 0
    fi
    echo "$cur" | grep -v "$CRON_TAG" | crontab - || die "no se pudo editar crontab"
    echo "arranque automatico deshabilitado."
}

boot_status() {
    if crontab -l 2>/dev/null | grep -q "$CRON_TAG"; then
        echo "arranque automatico: habilitado"
        crontab -l 2>/dev/null | grep "$CRON_TAG"
    else
        echo "arranque automatico: deshabilitado"
    fi
}

case "${1:-help}" in
    start) start ;;
    stop) stop ;;
    restart) stop; start ;;
    status) status ;;
    logs) logs "${2:-50}" ;;
    boot-enable) boot_enable ;;
    boot-disable) boot_disable ;;
    boot-status) boot_status ;;
    help|--help|-h)
        cat <<EOF
pasillo-seguro - servicio local sin sudo

Uso: scripts/servicio.sh <comando>

  start           inicia el servidor en el puerto ${PORT}
  stop            detiene el servidor
  restart         stop + start
  status          estado, PID, URL y health
  logs [N]        ultimas N lineas del log (default 50)
  boot-enable     agrega arranque automatico por cron @reboot
  boot-disable    quita el arranque automatico
  boot-status     muestra si el arranque automatico esta activo
  help            esta ayuda

Variables: PS_PORT (8020), PS_HOST (0.0.0.0), PS_DATA_DIR, PS_CAMERA_URL.
El puerto 8000 (detector-tarjetas) y 8010 (superfoto) estan prohibidos.
EOF
        ;;
    *) die "comando desconocido: $1 (usa help)" ;;
esac
