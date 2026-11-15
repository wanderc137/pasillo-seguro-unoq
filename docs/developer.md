# Pasillo Seguro — Documentación de desarrollador

## 1. Arquitectura

```
IP Webcam (MJPEG) ──► camera.c ──► jpeg.c ──► vision.c ──► jpeg.c ──► hub ──► server.c ──► navegador
                                         │
                                         ├── risk.c (TTC, corredor, histéresis)
                                         └── adb.c (opcional, USB)
```

- **Un solo binario C** (`pasillo-seguro`) sirve la UI, el MJPEG y la API.
- **Hilo de captura**: abre la cámara, lee frames MJPEG, decodifica, procesa,
  dibuja, codifica y publica el último JPEG.
- **Hilo ADB** (opcional): cada 5 s consulta `adb devices`; si hay un dispositivo
  en estado `device`, ejecuta `forward`/`reverse` una vez.
- **Servidor HTTP**: un hilo por cliente; el MJPEG usa el último frame publicado
  y descarta frames si el cliente es lento.
- **Estado compartido**: `PsHub` (mutex + cond) para el frame/estado; `state_lock`
  para la URL de cámara y el estado ADB.

## 2. Módulos

### `src/main.c`

- `ps_load_config()`: defaults, variables de entorno y argumentos.
- `capture_thread()`: bucle de fuente real o sintética.
- `adb_thread()`: watchdog de ADB.
- `ps_hub_update()`: publica un JPEG y métricas.
- `make_fake_frame()`: fuente sintética para tests/demo.

### `src/camera.c`

- `ps_camera_open()`: parsea `http://host:puerto/video`, conecta, envía `GET` y
  consume cabeceras HTTP.
- `ps_camera_read()`: acumula bytes y extrae un JPEG buscando un `EOI` (`FF D9`) y
  el `SOI` (`FF D8`) más cercano hacia atrás. Ese orden permite resincronizar si
  un frame anterior llegó truncado.
- Reconexión: el hilo de captura cierra y reabre con espera de 1,2 s.

### `src/jpeg.c`

- `ps_jpeg_decode()`: `fmemopen` + `jpeg_stdio_src` + `JCS_RGB`.
- `ps_jpeg_encode()`: `open_memstream` + `jpeg_stdio_dest`.
- Usa la API de libjpeg 6.2 en ARM (headers v62) y v8 en el host (headers v8).
  La versión está en `jconfig.h`; no mezclar headers y runtime.
- Los warnings de libjpeg se silencian con `emit_message`; los errores se
  devuelven al llamador.

### `src/vision.c`

1. `downscale_gray()`: promedio de bloques RGB → gris, a la resolución de proceso.
2. Diferencia contra `prev` y blur 3×3.
3. Movimiento por bloques 8×8:
   - búsqueda ±3 px por bloque,
   - histograma de vectores para el movimiento global,
   - SAD residual en el offset global,
   - umbral adaptativo `mean + 1.6·std`,
   - máscara de bloques en movimiento.
4. Erosión/dilatación 3×3.
5. `find_components()`: flood fill 4-conectado; filtra área, tamaño y aspecto.
6. `track_update()`: matching por centroide, velocidad, crecimiento de área,
   `TTC = 1/growth`, misses y limpieza.
7. `ps_track_risk()`: corredor, cercanía, TTC, crecimiento, lateral, área.
8. `ps_risk_update()`: histéresis (`red_frames`, `green_frames`).
9. Dibujo: borde, barra de estado, texto 5×7, cajas de objetos.

### `src/risk.c`

Funciones puras y testeables. El riesgo por objeto es una suma ponderada:

```
risk = 0.28·corredor + 0.22·cercania + 0.24·ttc
     + 0.10·crecimiento + 0.08·lateral + 0.08·area
```

Se atenúa en los bordes laterales y para objetos muy pequeños. El riesgo global
es el máximo. La histéresis evita el parpadeo:

- `risk >= risk_red` durante `red_frames` → ROJO;
- `risk <= risk_amber` durante `green_frames` → VERDE;
- entre ambos → ÁMBAR (si ya estaba ROJO, se mantiene hasta que baja a ámbar).

### `src/server.c`

- `ThreadingHTTPServer` artesanal con `socket`/`pthread`.
- `GET /stream.mjpg`: multipart `--frame`, espera a `hub.seq` con
  `pthread_cond_timedwait`, copia el JPEG y lo envía; el cliente lento pierde
  frames.
- `GET /api/probe`: normaliza la URL, descarga `/photo.jpg`, decodifica para
  obtener dimensiones y responde JSON.
- `POST /api/start`: normaliza, ajusta sensibilidad, reinicia el estado y marca
  `running=1`.
- `POST /api/stop`: `running=0` y estado `idle`.
- `/api/usb/*`: solo loopback; ejecuta ADB con `fork`/`execv`, sin shell.
- Archivos estáticos con allowlist; sin path traversal.

### `src/adb.c`

- `ps_adb_find()`: `PS_ADB_PATH`, rutas conocidas y `PATH`.
- `ps_adb_run()`: `fork` + `pipe` + `poll` + timeout; `execv`, sin shell.
- `ps_adb_status()`: parsea `adb devices -l` (`none`, `unauthorized`, `offline`,
  `device`).
- `ps_adb_setup()`: `forward tcp:8080 tcp:8080` y `reverse tcp:8020 tcp:8020`.
- `ps_adb_teardown()`: quita solo esos forwards.

### `src/util.c`

- Tiempo monotónico, logs, JSON escape, normalización de URL.
- `ps_http_get()` para el probe (HTTP simple, sin TLS).
- Lectura/escritura de archivos.

## 3. Flujo de datos

```
camera_read -> frame JPEG (bytes)
  -> ps_jpeg_decode -> RGB (w×h)
  -> ps_vision_process -> RGB anotado
  -> ps_jpeg_encode -> JPEG anotado
  -> ps_hub_update -> hub.jpeg + métricas
  -> /stream.mjpg -> cliente
```

El frame original se descarta; no se guarda video. La memoria está acotada por
el frame actual, el JPEG actual y los buffers de visión (unos pocos MB).

## 4. Compilación

### 4.1 Host (tests)

```bash
make native
make test
```

Usa los headers v8 de `third_party/libjpeg` y la libjpeg del sistema.

### 4.2 ARM64 (UNO Q)

```bash
./scripts/build_arm64.sh
```

Usa `aarch64-linux-gnu-gcc`, los headers v62 de `third_party/libjpeg62` y la
librería `third_party/libjpeg-arm64/libjpeg.so.62` copiada de la placa.

¿Por qué dos juegos de headers? La UNO Q tiene `libjpeg.so.62` (API 6.2). Si se
compila con los headers v8 del host, el runtime aborta con
`Wrong JPEG library version: library is 62, caller expects 80`. Los headers v62
se extraen del paquete Debian `libjpeg62-turbo-dev` y se versionan en
`third_party/libjpeg62/` (son headers, no binarios).

### 4.3 Dependencias

- `libc`, `libm`, `pthread`, `libjpeg.so.62`.
- No usa OpenCV, Python, GStreamer ni frameworks web.

## 5. Tests

- `tests/test_core.c`:
  - normalización de URL (IP pelada, puerto, `/video`, `/photo.jpg`, errores),
  - riesgo alto/bajo,
  - histéresis,
  - visión sintética con un rectángulo que crece.
- `tests/integration.py`:
  - arranca `./pasillo-seguro --fake`,
  - health/status/start,
  - snapshot JPEG,
  - MJPEG multipart,
  - probe contra un servidor de foto falso,
  - verificación estática de la UI.

```bash
make test
```

## 6. Decisiones y deuda técnica

- **C puro** para cumplir el requisito y evitar dependencias; la UNO Q no tiene
  compilador, así que se cross-compila en la notebook.
- **Sin modelo neuronal**: no hay runtime ni pesos en la placa y el proyecto no
  instala/descarga. La visión clásica es la vía honesta.
- **Resolución de proceso adaptativa** según el aspecto del stream.
- **JPEG parser** por SOI/EOI: simple y tolerante; los frames truncados se
  descartan. Se puede migrar a parser de boundary MJPEG si aparece una tasa alta
  de errores.
- **Un hilo por cliente**: suficiente para un teléfono/tablet; no es un servidor
  de internet.
- **Sin autenticación**: es una demo local; no exponer a internet.
- **ADB** implementado pero no probado sin cable; el modo LAN es el principal.

## 7. Cómo extender

- Agregar un detector neuronal: compilar un runtime C (TFLite/ONNX) para aarch64
  y un modelo; mantener la misma interfaz `PsDet`/`PsTrack`.
- Agregar sensor de distancia (LiDAR/ultrasonido) por UART/I2C y fusionarlo con
  el riesgo visual.
- Agregar grabación de eventos (snapshots) con límite de disco.
- Migrar el parser MJPEG a boundary + `Content-Length` para tolerancia máxima.
- Agregar autenticación/token si se despliega fuera de una red confiable.
