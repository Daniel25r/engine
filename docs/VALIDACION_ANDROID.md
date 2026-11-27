# Validación en Android real

Titán **todavía no se ha ejecutado en un teléfono**. Este documento explica cómo hacer la primera prueba completa y
dónde anotar lo que pase. No hace falta saber programar.

## 1. Instalación

1. Pasa `Titan-Editor-<versión>.apk` al teléfono (cable, Drive, Telegram...).
2. Ábrelo. Android pedirá permiso para "instalar aplicaciones de origen desconocido" para la app con la que lo abres.
3. Si el teléfono tiene una versión anterior de Titán, Android la actualiza conservando los proyectos.

Con un PC y `adb` (opcional, da más información):

```bash
adb install -r Titan-Editor-0.14.0.apk
```

```bash
adb logcat -s Titan
```

## 2. Recoger un informe

- Dentro del editor: **Ayuda › Diagnóstico del dispositivo › Copiar informe**. Copia el modelo de GPU, la versión de
  OpenGL ES, la memoria, la salida de audio y los últimos 200 mensajes. Pégalo en un mensaje.
- El registro completo se guarda en `titan-registro.txt` en la carpeta de proyectos de la app (el anterior queda como
  `.anterior`). Los avisos y errores se escriben al momento, así que sobreviven a un cierre inesperado.
- Si la app se cierra sola, al abrirla otra vez el informe del paso anterior conserva la sesión que falló.

## 3. Prueba de sonido (prioridad 1)

**Ayuda › Prueba de sonido.** Dura 7 segundos:

| Segundo | Deberías oír |
|---|---|
| 0 – 1 | Un tono continuo (un "la") |
| 1,2 – 3,6 | Una melodía de 8 notas que sube |
| 3,8 – 7 | 6 golpes que van de izquierda a derecha (con auriculares se nota mejor) |

La ventana muestra la **salida** que abrió el motor. En Android debería poner `AAudio` o `OpenSL|ES`.

- Si pone **Null** (en rojo), el sistema no dejó abrir el altavoz. Anótalo con el modelo del teléfono.
- Si pone AAudio u OpenSL y no se oye nada, sube el **volumen multimedia** (no el del timbre) y quita el modo silencio.

Referencia para comparar: `prueba-de-sonido.wav` es lo que mezcla el motor durante esta prueba, grabado sin altavoces
con `titan audiotest`. Si suena bien en tu móvil y la prueba dentro de la app no, el fallo está en la salida de audio
del dispositivo, no en la mezcla.

## 4. Lista de comprobación

Marca ✅, ❌ o ⚠️ y añade una nota. Hazlo con el proyecto de benchmark y con un proyecto propio.

| # | Qué probar | Cómo | Resultado |
|---|---|---|---|
| 1 | Inicio | Abrir la app; aparece la lista de proyectos en horizontal | |
| 2 | Proyectos | Crear un proyecto 3D, cerrarlo y volver a abrirlo | |
| 3 | Escenas | Crear › Cubo, Esfera, Luz; guardar; reabrir | |
| 4 | Cámara | Un dedo gira, dos dedos mueven y hacen zoom | |
| 5 | Selección | Tocar un objeto lo selecciona; tocar el vacío lo deselecciona | |
| 6 | Gizmos | Mover, rotar y escalar con las flechas | |
| 7 | Importar modelos | Contenido › Importar un .glb | |
| 8 | PBR | Un material metálico y otro rugoso se ven distintos | |
| 9 | Luces | Luz puntual y foco iluminan el suelo | |
| 10 | Sombras | El cubo proyecta sombra; Ventana › Calidad › Muy bajo las quita | |
| 11 | Posprocesado | Cielo y niebla › Bloom y Viñeta | |
| 12 | Partículas | Crear › Efectos › Fuego | |
| 13 | Terreno | Crear › Terreno; herramienta Subir; arrastrar el dedo | |
| 14 | Física | Crear › Física › Cubo con física; Jugar: cae | |
| 15 | Personaje | Crear › Física › Personaje + script con `Input.axis("Mover")` | |
| 16 | Animaciones | Modelo con animaciones + Animator | |
| 17 | Input Actions | Joystick de la UI, teclado Bluetooth o mando | |
| 18 | Interfaz | Crear › Interfaz › Botón y Joystick; jugar | |
| 19 | Lua | Un script con un error muestra archivo y línea en el Registro | |
| 20 | Guardado | `Save.set` en un juego; salir; volver a jugar | |
| 21 | Audio | Sección 3 | |
| 22 | Modo Play | Jugar, Pausa, Detener: la escena vuelve a como estaba | |
| 23 | Exportación | Paquete del juego + APK compilado en el PC; instalar y abrir | |

## 5. Rendimiento

1. En un PC: `titan benchmark Benchmark` crea el proyecto. Cópialo a la carpeta de proyectos, o expórtalo como juego.
2. Ábrelo y pulsa **Jugar**. Recorre 8 escenas (100, 500, 1000 y 1600 objetos, luces, partículas, física y
   materiales) y mide 8 segundos cada una.
3. Al final aparece la tabla de resultados. También queda en el registro, en las líneas `BENCHMARK ...`.
4. Repite con dos niveles de calidad (Ventana › Calidad gráfica).

Plantilla para anotar:

| Teléfono | GPU | Memoria | Calidad | 100 | 500 | 1000 | 1600 | Luces | Partículas | Física | Materiales |
|---|---|---|---|---|---|---|---|---|---|---|---|
| | | | | | | | | | | | |

Referencia (no es un teléfono): PC con render por software llvmpipe, calidad media, 1280×720. Ver `docs/TITAN_ENGINE_TESTS.md`.

## 6. Qué se ha comprobado sin teléfono

- Que el APK se compila, se firma y contiene las bibliotecas de las 3 arquitecturas y las funciones JNI.
- La mezcla de audio: frecuencia correcta en WAV, MP3 y FLAC, volumen, tono, izquierda/derecha y distancia. Lo
  comprueban pruebas automáticas que analizan la señal.
- Todo lo demás, en el editor y el reproductor de escritorio (Linux), con la misma base de código.

No comprobado: comportamiento táctil real, rendimiento de GPU móviles, AAudio/OpenSL, pérdida del contexto
OpenGL al cambiar de app, teclado y mando Bluetooth, y el almacenamiento de Android.
