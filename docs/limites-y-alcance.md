# Límites y alcance

## Qué demuestra

- La UNO Q puede recibir un stream MJPEG de un teléfono, decodificarlo,
  procesarlo y volver a servirlo anotado, todo localmente y en C.
- Un pipeline clásico de visión (movimiento + compensación global + tracking +
  tiempo al contacto) puede generar una señal de riesgo en tiempo real a ~12 fps
  con 1080×1920 en la placa.
- La página puede operarse desde el teléfono que hace de cámara o desde otro
  dispositivo de la red local, sin nube.

## Qué NO es

- No es un modelo de deep learning. No hay YOLO, SSD, Detectron ni similar.
- No es un sistema de seguridad certificado ni un control de robot.
- No mide distancia métrica: el “tiempo al contacto” es una estimación relativa
  basada en el crecimiento aparente del objeto.
- No garantiza detección de obstáculos estáticos, transparentes, muy pequeños,
  camuflados o en contraluz.
- No funciona bien con la cámara totalmente tapada o sin luz.
- No guarda evidencia ni video; solo el frame actual en memoria.

## Escenarios donde rinde mejor

- Pasillo con personas que caminan o se cruzan lateralmente.
- Cámara a la altura del pecho, apuntando al frente, con luz razonable.
- Fondo con algo de textura (paredes, stands, piso).
- Sensibilidad media; ajustar a alta si el público se mueve rápido.

## Escenarios donde puede fallar

- Persona parada totalmente quieta en el corredor con cámara quieta.
- Pared o cartel que ocupa el cuadro y crece al caminar (falso positivo).
- Multitud muy densa: los objetos se fusionan en una sola componente.
- Cambios bruscos de iluminación o reflejos.
- Teléfono en 4K: la placa puede bajar los fps; usar 1080p o 720p.
- Red saturada o IP Webcam inestable: cortes y reconexión.

## Métricas de referencia

| Equipo | Resolución | FPS | Latencia |
|---|---|---|---|
| UNO Q | 1080×1920 | 12,3 | 79 ms |
| Notebook x86_64 | 1080×1920 | 37,1 | 26 ms |

La cifra de FPS es el procesamiento completo (decode + visión + encode), no la
tasa del stream original. El servidor limita la salida a `stream_fps` (12 por
defecto). Si la placa se queda corta, bajar la resolución del teléfono o el
`--fps`.

## Seguridad

- La página no tiene autenticación. Solo para red local confiable.
- No exponer el puerto 8020 a internet.
- Los endpoints `/api/usb/*` solo aceptan loopback.
- El binario no ejecuta shell; ADB se invoca con `execv` y argumentos fijos.
- No se guardan credenciales ni claves.

## Relación con los otros proyectos de la placa

- `detector-tarjetas` usa el puerto **8000**.
- `superfoto-unoq` usa el puerto **8010**.
- Este proyecto usa **8020** y no toca los anteriores.
