# Pasillo Seguro — Datasheet de usuario

## 1. Qué es

Una aplicación local para Arduino UNO Q que recibe el video de un teléfono con
IP Webcam, detecta obstáculos/personas en un pasillo y muestra una página web con
un borde de color:

- verde: camino libre;
- ámbar: precaución;
- rojo: detenerse.

Todo corre en la UNO Q. No hay nube, no se guarda video y no se envía nada a
internet.

## 2. Hardware y software

| Elemento | Requisito |
|---|---|
| Placa | Arduino UNO Q (Debian aarch64) |
| Cámara | Teléfono Android con IP Webcam |
| Red | LAN/Wi-Fi local o USB/ADB opcional |
| Navegador | Chrome/Safari/Firefox actualizado |
| Puerto | 8020/tcp en la UNO Q |
| Almacenamiento | < 5 MB para el binario y la web; no se guardan videos |

La UNO Q debe estar en la misma red que el teléfono. En la feria se recomienda el
**hotspot del teléfono** o un router local sin internet.

## 3. Instalación

### 3.1 Desde la notebook

```bash
cd pasillo-seguro-unoq
./scripts/build_arm64.sh
BOARD=arduino@<IP_UNO_Q> SSH_PORT=2222 ./scripts/deploy_unoq.sh
```

### 3.2 Servicio

```bash
ssh arduino@<IP_UNO_Q>
cd ~/pasillo-seguro-unoq
./scripts/servicio.sh start
./scripts/servicio.sh status
```

El servicio no usa sudo. Para arranque automático:

```bash
./scripts/servicio.sh boot-enable
```

### 3.3 Página

Abrir en el teléfono o tablet:

```
http://<IP_UNO_Q>:8020/
```

## 4. Operación

1. **IP del teléfono**: escribir la dirección que muestra IP Webcam, por ejemplo
   `192.168.1.50:8080`.
2. **Probar conexión**: debe devolver la resolución de `/photo.jpg`.
3. **Sensibilidad**:
   - baja: menos falsos rojos;
   - media: equilibrio;
   - alta: se pone rojo antes.
4. **Iniciar**: comienza la captura y el procesamiento.
5. Caminar con el teléfono apuntando al frente, a la altura del pecho.
6. **Detener**: corta la captura; el servicio sigue activo para volver a iniciar.

### 4.1 Interpretación del borde

| Color | Qué significa | Qué hacer |
|---|---|---|
| Verde | No hay obstáculo relevante en el corredor | Avanzar |
| Ámbar | Objeto en el corredor o acercándose | Reducir velocidad, mirar |
| Rojo | Contacto probable en menos de ~1–2 s | Detenerse |

La transición a rojo es conservadora y tiene histéresis: no parpadea. Para volver
a verde hace falta que el corredor quede despejado varios cuadros.

## 5. Red local

### 5.1 Hotspot del teléfono

1. Activar **Zona Wi-Fi/hotspot** en el teléfono.
2. Conectar la UNO Q a esa red (Wi-Fi).
3. La IP del teléfono suele ser `192.168.43.1`; la UNO Q recibe algo como
   `192.168.43.x`.
4. Abrir `http://<IP_UNO_Q>:8020/` en el teléfono.

### 5.2 Wi-Fi existente

Ambos dispositivos en la misma red. No hace falta internet; solo que se vean
entre sí. Evitar redes con aislamiento de clientes.

### 5.3 USB/ADB (opcional)

Con cable de datos y depuración USB:

```bash
adb forward tcp:8080 tcp:8080
adb reverse tcp:8020 tcp:8020
```

La cámara queda en `127.0.0.1:8080` y la página en `127.0.0.1:8020`. La página
tiene el modo **USB/ADB** con los botones **Preparar USB** y **Desconectar USB**.

## 6. Configuración

`config.env` (copiar de `config.example`, no se versiona):

| Variable | Default | Descripción |
|---|---|---|
| `PS_PORT` | 8020 | Puerto HTTP. 8000/8010 prohibidos. |
| `PS_CAMERA_URL` | vacío | IP/URL por defecto del teléfono. |
| `PS_TRANSPORT` | lan | `lan` o `usb`. |
| `PS_ADB_PATH` | autodetectado | Ruta al binario `adb`. |

También se aceptan argumentos de línea de comandos:

```bash
./pasillo-seguro --port 8020 --camera 192.168.1.50:8080 --web web
```

## 7. Mantenimiento

- **Log**: `data/server.log`; `scripts/servicio.sh logs 80`.
- **PID**: `data/pasillo.pid`.
- **Reinicio**: `scripts/servicio.sh restart`.
- **Actualización**: `./scripts/deploy_unoq.sh` (preserva `data/`).
- **Arranque automático**: `boot-enable` / `boot-disable` / `boot-status`.
- **No hay video guardado**; el disco no crece con el uso.

## 8. Límites y advertencias

- No es un sistema de seguridad certificado.
- No detecta de forma fiable un obstáculo totalmente estático si la cámara no se
  mueve y el contraste es bajo.
- La cámara en movimiento, el contraluz y las sombras pueden dar falsos positivos.
- El borde es una ayuda visual; la persona sigue siendo responsable.
- No exponer el puerto 8020 a internet: no hay autenticación.
