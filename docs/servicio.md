# Servicio local sin sudo

`scripts/servicio.sh` maneja el proceso `pasillo-seguro` en la UNO Q sin systemd
y sin sudo. Usa un PID, un lock y un health check.

## Comandos

```bash
./scripts/servicio.sh start
./scripts/servicio.sh stop
./scripts/servicio.sh restart
./scripts/servicio.sh status
./scripts/servicio.sh logs 80
./scripts/servicio.sh boot-enable
./scripts/servicio.sh boot-disable
./scripts/servicio.sh boot-status
./scripts/servicio.sh help
```

## Archivos de estado

| Archivo | Uso |
|---|---|
| `data/pasillo.pid` | PID del proceso |
| `data/server.log` | stdout/stderr del binario |
| `data/servicio.lock` | lock de `flock` para evitar operaciones simultáneas |

## Cómo arranca

```bash
nohup ./pasillo-seguro --port 8020 --web ./web --data ./data \
  >> data/server.log 2>&1 9>&- &
```

- `9>&-` cierra el descriptor del lock en el hijo; sin eso, el proceso hereda el
  `flock` y un `restart` posterior falla con “otra operacion en curso”.
- El script espera hasta 10 s a que `/api/health` responda.
- Si el puerto está ocupado por otro proceso, no lo mata.

## Puertos

- **8020**: este proyecto.
- **8000**: `detector-tarjetas`; prohibido.
- **8010**: `superfoto-unoq`; prohibido.

`start` rechaza los puertos 8000/8010 antes de tocar nada.

## Arranque automático

`boot-enable` agrega al crontab del usuario:

```cron
@reboot /home/arduino/pasillo-seguro-unoq/scripts/servicio.sh start >/dev/null 2>&1 # pasillo-seguro-service
```

Es idempotente. `boot-disable` quita esa línea. `boot-status` muestra si está
activa. No requiere sudo; depende de que `cron` esté corriendo.

## Configuración

El script carga `config.env` si existe (no se versiona):

```bash
PS_PORT=8020
PS_CAMERA_URL=
PS_TRANSPORT=lan
PS_ADB_PATH=
```

También se pueden pasar variables al invocar:

```bash
PS_PORT=8020 ./scripts/servicio.sh start
```

## Diagnóstico

```bash
./scripts/servicio.sh status
./scripts/servicio.sh logs 100
curl -sS http://127.0.0.1:8020/api/health
curl -sS http://127.0.0.1:8020/api/status
ss -ltn | grep 8020
```

Si `start` no responde:

1. Verificar que `pasillo-seguro` sea ejecutable y de arquitectura aarch64.
2. Verificar que `web/index.html` exista.
3. Revisar `data/server.log`.
4. Verificar que el puerto 8020 esté libre.

## systemd (opcional, requiere sudo)

El proyecto no instala una unidad systemd. Si se quisiera, el comando sería:

```ini
[Unit]
Description=Pasillo Seguro UNO Q
After=network.target

[Service]
WorkingDirectory=/home/arduino/pasillo-seguro-unoq
ExecStart=/home/arduino/pasillo-seguro-unoq/pasillo-seguro --port 8020 --web web --data data
Restart=on-failure
User=arduino

[Install]
WantedBy=multi-user.target
```

Pero instalarla requiere `sudo systemctl enable --now ...`; el esquema actual con
cron no lo necesita.
