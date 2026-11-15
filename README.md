# Pasillo Seguro — Arduino UNO Q

**Percepción local para caminar por un pasillo concurrido.** Un teléfono con
[IP Webcam](https://play.google.com/store/apps/details?id=com.pas.webcam) transmite
video por la red local; la Arduino UNO Q lo procesa **sin nube** y sirve una página
web con el video anotado. El borde de la imagen cambia de color según el riesgo de
choque:

- **Verde**: camino libre, se puede avanzar.
- **Ámbar**: hay algo en el corredor, precaución.
- **Rojo**: riesgo inminente, detenerse.

El backend está escrito en **C** (servidor HTTP, cliente MJPEG, decodificación
JPEG, visión y riesgo). La interfaz es HTML/CSS/JavaScript servida por el propio
binario. No se instala nada en la UNO Q para ejecutarlo: usa la libjpeg que ya
tiene el sistema.

```
 Teléfono (IP Webcam)                 Arduino UNO Q                     Teléfono / tablet
 ┌──────────────────┐   MJPEG/LAN   ┌─────────────────────────┐  HTTP   ┌──────────────────┐
 │ cámara + stream  │ ────────────► │ pasillo-seguro (C)      │ ──────► │ página web local │
 │ 192.168.x.x:8080 │               │ decode + visión + riesgo│         │ borde verde/rojo │
 └──────────────────┘               │ HTTP :8020 + MJPEG out  │         └──────────────────┘
                                    └─────────────────────────┘
```

## Estado del proyecto

- Binario C `pasillo-seguro`, puerto **8020**.
- Fuente de video: IP Webcam por **LAN/Wi-Fi** o, si hay cable, **USB/ADB**.
- Detección de objetos/personas en movimiento, corredor central, crecimiento
  (tiempo al contacto), tracking e histéresis.
- Sin nube, sin internet, sin guardar video.
- Servicio sin sudo con `scripts/servicio.sh` y arranque automático opcional por cron.
- Tests: `make test` (unitarios C + integración con fuente sintética).

### Rendimiento medido

| Escenario | Resolución | FPS procesados | Latencia |
|---|---|---|---|
| UNO Q + teléfono real (MJPEG LAN) | 1080×1920 | **12,3 fps** | **~79 ms** |
| Notebook (mismo binario C, x86_64) | 1080×1920 | 37,1 fps | ~26 ms |
| Fuente sintética en la UNO Q | 640×480 | ~12 fps (tope de salida) | — |

El teléfono de prueba estaba en 1080p vertical y la UNO Q sostuvo 12 fps con
procesamiento completo. La resolución de proceso se adapta al aspecto de la
entrada (108×192 para vertical, 192×144 para horizontal).

## Qué necesitás

- Arduino UNO Q con Debian, Python no es necesario para ejecutar el binario.
- Teléfono Android con **IP Webcam** y la cámara desbloqueada.
- Red local: hotspot del teléfono, Wi-Fi sin internet o cable USB (opcional).
- Para compilar: una notebook con `gcc` y `aarch64-linux-gnu-gcc`
  (`sudo apt-get install -y gcc-aarch64-linux-gnu`), más los headers de libjpeg.
  El binario se copia a la placa; **no hace falta compilar en la UNO Q**.

## Puesta en marcha rápida

### 1. Preparar IP Webcam

1. Abrí IP Webcam en el teléfono.
2. En **Video preferences**, elegí la resolución más alta que aguante el teléfono
   (1080p o 720p). En **Photo settings**, subí la calidad.
3. Iniciá el servidor. La app muestra una dirección tipo
   `http://192.168.1.50:8080`.
4. Anotá la IP; si cambia, se escribe de nuevo en la página.

### 2. Desplegar en la UNO Q

Desde la notebook, en la carpeta del proyecto:

```bash
./scripts/build_arm64.sh
./scripts/deploy_unoq.sh
```

`deploy_unoq.sh` compila, copia el binario, `web/`, el script de servicio y
reinicia en el puerto 8020. No usa sudo y preserva `data/`.

Si preferís hacerlo a mano:

```bash
scp pasillo-seguro-arm64 arduino@<IP_UNO_Q>:/home/arduino/pasillo-seguro-unoq/pasillo-seguro
scp -r web arduino@<IP_UNO_Q>:/home/arduino/pasillo-seguro-unoq/
ssh arduino@<IP_UNO_Q> 'cd ~/pasillo-seguro-unoq && ./scripts/servicio.sh start'
```

### 3. Abrir la página

En el teléfono (o en una tablet) abrí:

```
http://<IP_DE_LA_UNO_Q>:8020/
```

La UNO Q debe tener una IP fija o reservada en la red local. Se puede ver con
`hostname -I` en la placa. La página se sirve desde la UNO Q; no depende de
internet.

1. Escribí la IP del teléfono en **IP o URL del teléfono**.
2. Tocá **Probar conexión**. Debe responder con la resolución real.
3. Tocá **Iniciar**.
4. Caminá con el teléfono apuntando al frente. El borde cambia de color.

> El teléfono puede ser cámara y pantalla a la vez. Si Android corta la cámara al
> abrir el navegador, usá un segundo teléfono/tablet como visor.

## La página

- **Conexión**: IP/URL manual, botón de prueba, persistencia en `localStorage`.
- **Transporte**: Wi-Fi/LAN o USB/ADB.
- **Visor**: MJPEG en vivo con el borde de riesgo dibujado dentro del cuadro y
  también en el CSS.
- **Métricas**: riesgo, objetos, FPS, latencia y fuente.
- **Sensibilidad**: baja, media o alta. Alta es más conservadora (se pone rojo
  antes); baja tolera más.
- **Ayuda**: pasos para la feria.

### Estados

| Color | Significado | Acción |
|---|---|---|
| Verde | Sin obstáculos en el corredor | Avanzar |
| Ámbar | Objeto en el corredor o acercándose | Atención, reducir velocidad |
| Rojo | Tiempo al contacto corto / objeto muy cerca | Detenerse |

La histéresis evita parpadeos: para pasar a rojo hacen falta varios cuadros de
riesgo alto; para volver a verde, varios cuadros de riesgo bajo.

## Cómo se detecta el riesgo

El binario C hace visión clásica, sin redes neuronales:

1. **Downscale** del frame a 108×192 (vertical) o 192×144 (horizontal) y escala
   de grises.
2. **Diferencia** contra el frame anterior y suavizado.
3. **Movimiento por bloques** 8×8 con búsqueda de desplazamiento y compensación
   del movimiento global de la cámara (la persona que camina).
4. **Máscara de movimiento** + limpieza morfológica + componentes conexas.
5. **Tracking** por centroide con velocidad, área y crecimiento.
6. **Riesgo por objeto**:
   - intersección con el corredor central,
   - cercanía (posición inferior en el cuadro),
   - crecimiento del área (looming / tiempo al contacto),
   - velocidad lateral y tamaño.
7. **Riesgo global** = máximo; **histéresis** para el color final.
8. **Dibujo** del borde, cajas de objetos y texto (`AVANZA`, `PRECAUCION`,
   `STOP`) en el frame antes de comprimir a JPEG.

### Límites honestos

- No es un modelo neuronal (YOLO, etc.). La UNO Q no trae PyTorch/ONNX/TFLite y
  el proyecto no instala nada. Es un pipeline clásico de movimiento/looming.
- Detecta bien **objetos/personas que se mueven o se acercan**. Un obstáculo
  completamente estático y camuflado puede no detectarse si la cámara también
  está quieta.
- La cámara en movimiento agrega ruido; la compensación global ayuda, pero
  escenas muy cargadas o con cambios de luz pueden dar falsos positivos.
- El borde es **informativo**. No frena robots ni reemplaza la atención de la
  persona. Para uso crítico hace falta un sensor de distancia (LiDAR/ultrasonido)
  y un modelo entrenado.

## Compilar y probar

```bash
make native        # binario x86_64 para probar en la notebook
make test          # tests C + integración con fuente sintética
make arm64         # binario aarch64 para la UNO Q
```

Dependencias de compilación:

- `gcc`, `make`, `libjpeg` (headers v8 en el host) para `native`.
- `aarch64-linux-gnu-gcc` para `arm64`.
- `third_party/libjpeg62/`: headers de libjpeg 6.2 extraídos del paquete
  `libjpeg62-turbo-dev` de Debian. La placa tiene `libjpeg.so.62`; por eso el
  binario ARM se compila con esos headers, no con los v8 del host.
- `third_party/libjpeg-arm64/libjpeg.so.62`: copia de la librería de la placa,
  usada solo para enlazar el binario ARM. No se publica en el repo.

## Servicio sin sudo

```bash
scripts/servicio.sh start
scripts/servicio.sh stop
scripts/servicio.sh restart
scripts/servicio.sh status
scripts/servicio.sh logs 80
scripts/servicio.sh boot-enable      # cron @reboot
scripts/servicio.sh boot-disable
scripts/servicio.sh boot-status
```

- PID: `data/pasillo.pid`
- Log: `data/server.log`
- Lock: `data/servicio.lock`
- Puertos **8000** (`detector-tarjetas`) y **8010** (`superfoto`) están
  prohibidos por el script; el proyecto usa **8020**.
- `config.env` (opcional, ignorado por git) permite fijar `PS_PORT`,
  `PS_CAMERA_URL`, `PS_TRANSPORT` y `PS_ADB_PATH`.

## API local

| Método | Ruta | Descripción |
|---|---|---|
| GET | `/` | Página |
| GET | `/stream.mjpg` | MJPEG anotado |
| GET | `/snapshot.jpg` | Último frame JPEG |
| GET | `/api/health` | Estado básico |
| GET | `/api/status` | Riesgo, objetos, FPS, latencia, ADB |
| GET | `/api/probe?url=IP:8080` | Prueba `/photo.jpg` y devuelve resolución |
| POST | `/api/start?url=IP:8080&sensitivity=medium` | Inicia captura |
| POST | `/api/stop` | Detiene captura |
| GET | `/api/usb/status` | Estado de ADB (solo loopback) |
| POST | `/api/usb/setup` | `adb forward` + `adb reverse` (solo loopback) |
| POST | `/api/usb/teardown` | Quita los forwards (solo loopback) |

Ejemplo:

```bash
curl "http://<IP_UNO_Q>:8020/api/probe?url=192.168.1.50:8080"
curl -X POST "http://<IP_UNO_Q>:8020/api/start?url=192.168.1.50:8080&sensitivity=medium"
curl "http://<IP_UNO_Q>:8020/api/status"
```

## USB/ADB (opcional)

Si hay cable de datos y depuración USB activa:

1. Conectar el teléfono al puerto **USB-A host** de la UNO Q.
2. Activar **Opciones de desarrollador → Depuración USB** y aceptar el diálogo RSA.
3. En la página, elegir **USB/ADB** y **Preparar USB**.
4. La app ejecuta:
   ```bash
   adb forward tcp:8080 tcp:8080   # cámara en 127.0.0.1:8080
   adb reverse tcp:8020 tcp:8020   # página en 127.0.0.1:8020
   ```
5. Abrir `http://127.0.0.1:8020/` en el teléfono.

Sin cable, usá LAN/Wi-Fi. Los endpoints USB solo aceptan conexiones desde
`127.0.0.1`.

## Estructura del repositorio

```
src/
  main.c        configuración, hilo de captura, hilo ADB, fuente sintética
  server.c      HTTP, MJPEG, API, archivos estáticos
  camera.c      cliente MJPEG y reconexión
  jpeg.c        decode/encode con libjpeg
  vision.c      downscale, movimiento, componentes, tracking, dibujo
  risk.c        riesgo por objeto e histéresis
  adb.c         forward/reverse/status de ADB
  util.c        tiempo, JSON, normalización de URL, HTTP GET
web/            index.html, app.js, style.css, favicon.svg
scripts/        build_arm64.sh, deploy_unoq.sh, servicio.sh
tests/          test_core.c, integration.py
docs/           datasheet.md, developer.md, limites-y-alcance.md, servicio.md
third_party/    headers libjpeg v8/v62 (la .so ARM no se versiona)
```

## Solución de problemas

| Síntoma | Causa / solución |
|---|---|
| `Probar conexión` da 502 | IP/puerto mal, teléfono en otra red, IP Webcam detenido o firewall. |
| La imagen está negra | Cámara tapada, teléfono boca abajo o poca luz. |
| Borde rojo con el pasillo vacío | Bajar sensibilidad; cambiar de ángulo; evitar apuntar a una pared que se acerca. |
| Borde verde con una persona cerca | La persona está quieta o el contraste es bajo; subir sensibilidad y mejorar la luz. |
| FPS bajo | Bajar resolución del teléfono a 720p; cerrar apps; usar 5 GHz/hotspot. |
| `adb: none` | Cable de datos, depuración USB, aceptar RSA, puerto USB-A host. |
| El servicio no arranca | `scripts/servicio.sh logs 80`; verificar que el puerto 8020 esté libre. |
| Cambió la IP del teléfono | Escribirla de nuevo y **Probar conexión**. |

## Advertencias

- Esto es una **demostración** de percepción local; no es un sistema de seguridad
  certificado.
- No lo uses para controlar un robot o vehículo sin una capa de seguridad
  independiente.
- No expongas el puerto 8020 a internet. La página no tiene autenticación.
- No toca los servicios existentes de la UNO Q (`detector-tarjetas:8000`,
  `superfoto:8010`).

## Ficha de evaluación

**Madurez global: 3.7/5**

| Criterio | Nota | Evidencia |
|---|---:|---|
| Madurez | 3/5 | Una sola versión sin número declarado, con medidas reales de 12,3 fps y 79 ms en la UNO Q. No tiene bitácora, estado actual ni suite de pruebas de hardware. |
| Reproducibilidad | 3/5 | Incluye binarios x86_64 y ARM64 y objetivos make native, test y arm64. La compilación ARM exige toolchain aarch64 y cabeceras libjpeg62 provistas en third_party. |
| Documentación | 4/5 | README, datasheet, developer, límites y guía de servicio cubren pipeline, API, compilación y operación. Falta la bitácora de decisiones que sí tienen los otros repos. |
| Honestidad de límites | 5/5 | Explicita que no hay distancia métrica, que el TTC es relativo al crecimiento aparente, que la multitud fusiona componentes y que el borde no controla nada. |
| Valor demostrativo | 4/5 | Muestra visión embebida en C puro, sin OpenCV ni Python, con menos de 5 MB de almacenamiento y el mismo binario en placa y notebook. |
| Facilidad de despliegue | 3/5 | deploy_unoq.sh compila, copia y reinicia sin sudo, pero requiere compilador cruzado y bibliotecas de enlace. La variante ADB no está validada. |

Sirve a quien quiera un detector de proximidad mínimo en C sobre la UNO Q y aprender a cruzar compilación ARM, MJPEG y percepción clásica. La limitación que más pesa es la detección: al depender del movimiento, un obstáculo quieto puede pasar desapercibido y el sistema solo es informativo.

## Lo que no funcionó

### Compilación ARM · libjpeg — Desajuste de versión de libjpeg

El binario ARM compilado con cabeceras v8 abortaba al arrancar con «Wrong JPEG library version: library is 62, caller expects 80». Se compiló contra cabeceras v62 y se enlazó la libjpeg.so.62 que trae la placa.

### Parser MJPEG — Resincronización por SOI/EOI

El parser busca marcadores SOI/EOI: tolera cortes de red pero descarta el cuadro truncado, lo que puede bajar los fps con WiFi mala. Queda documentada la migración a un parser por boundary y Content-Length.

### Transporte USB/ADB — Ruta ADB sin validar

La opción USB/ADB con adb forward y reverse está implementada, pero no se probó sin cable en la placa. La vía recomendada y medida es IP Webcam por LAN.

### Detección — Obstáculos estáticos o camuflados

La detección se basa en diferencia entre cuadros: un obstáculo quieto o del color del fondo puede no aparecer, y una multitud densa fusiona componentes. Se declara como límite y no se promete seguridad certificada.

## Preguntas típicas

**¿Qué es Pasillo Seguro?**

Es un servidor en C que recibe el video de un teléfono con IP Webcam, estima el riesgo de avanzar y devuelve una página con el stream anotado y un borde verde, ámbar o rojo. Corre entero en la Arduino UNO Q.

**¿Qué no hace?**

No mide distancias en metros ni reemplaza un sensor de seguridad, no registra video y no controla un robot o una puerta. Tampoco detecta de forma garantizada obstáculos transparentes, pequeños o quietos.

**¿Cómo se ejecuta o despliega?**

Se compila con make native o make arm64 y se despliega con scripts/deploy_unoq.sh, que copia el binario y los estáticos y reinicia el puerto 8020. En local se prueba con make test o con el modo --fake.

**¿Qué se puede reutilizar?**

El servidor HTTP y MJPEG en C, el parser de URLs, la búsqueda de movimiento por bloques con compensación global y la máquina de histéresis son piezas reutilizables. También el esquema de servicio sin sudo con PID y lock.

## Licencia

MIT. Ver `LICENSE`.
