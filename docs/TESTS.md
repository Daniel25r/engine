# Pruebas

## Cómo ejecutarlas

```bash
./build/desktop/titan_tests        # todas
./build/desktop/titan_tests escena # solo las que contienen "escena"
./build/desktop/titan_tests -v     # mostrando también el registro del motor
```

Arnés propio y mínimo (`tests/Test.h`), sin dependencias. Las pruebas que tocan disco usan carpetas temporales nuevas.

## Resultado actual (2026-11-22, Linux x86_64, GCC, Release)

```
[ OK ] math: Euler <-> cuaternión ida y vuelta
[ OK ] math: rotar 90 grados en Y lleva -Z a -X
[ OK ] math: TRS por su inversa es la identidad
[ OK ] math: descomponer recupera T, R y S
[ OK ] math: proyección ortográfica lleva los bordes a -1..1
[ OK ] ecs: crear, añadir, leer y quitar componentes
[ OK ] ecs: una entidad destruida invalida sus copias aunque el índice se reutilice
[ OK ] ecs: each solo visita las entidades con todos los componentes
[ OK ] ecs: destruir la entidad visitada dentro de each es seguro
[ OK ] ecs: 100 000 entidades se crean y destruyen sin fugas de índices
[ OK ] escena: crear entidades con id, nombre y transformación
[ OK ] escena: no se aceptan ids repetidos
[ OK ] jerarquía: la matriz de mundo combina las de los padres
[ OK ] jerarquía: solo se recalculan las ramas modificadas
[ OK ] jerarquía: cambiar de padre conserva la posición en el mundo
[ OK ] jerarquía: se rechazan los ciclos
[ OK ] jerarquía: el orden de los hijos se conserva
[ OK ] escena: destruir es diferido y arrastra a los hijos
[ OK ] escena: activa en la jerarquía depende de los antepasados
[ OK ] eventos: crear y destruir entidades emite eventos
[ OK ] eventos: la suscripción se da de baja al destruirse
[ OK ] eventos: darse de baja durante la emisión es seguro
[ OK ] eventos: una suscripción que sobrevive al bus no falla
[ OK ] serialización: guardar y cargar conserva entidades, jerarquía y componentes
[ OK ] serialización: los componentes desconocidos se conservan
[ OK ] serialización: datos incorrectos dan avisos, no se cuelga ni inventa
[ OK ] serialización: se rechazan archivos ajenos, rotos y de versiones futuras
[ OK ] archivos: guardado atómico con copia de seguridad y recuperación
[ OK ] prefabs: guardar un subárbol e instanciarlo con ids nuevos
[ OK ] proyecto: crear, guardar escena, cerrar y volver a abrir (demostración técnica)
[ OK ] recursos: los ids sobreviven a mover un archivo con su .meta
[ OK ] recursos: un archivo copiado con su .meta recibe un id nuevo
[ OK ] paquete: detecta referencias rotas y genera el índice si todo es correcto
[ OK ] proyecto: se rechaza un proyecto de una versión futura sin tocarlo
[ OK ] scripts: ciclo de vida onCreate, onStart, onUpdate, onFixedUpdate, onLateUpdate, onDestroy
[ OK ] scripts: mueven entidades reales de la escena
[ OK ] scripts: API de entidades, componentes y jerarquía
[ OK ] scripts: los errores indican archivo, línea y función, y no detienen a los demás
[ OK ] scripts: errores de sintaxis y scripts que no devuelven tabla
[ OK ] scripts: un bucle infinito se corta por tiempo y el sistema sigue funcionando
[ OK ] scripts: límite de memoria
[ OK ] scripts: Screen.text con y sin color
[ OK ] scripts: sin acceso al sistema (io, os, load, require)
[ OK ] scripts: usar una entidad destruida da un error claro
[ OK ] scripts: setActive llama onDisable/onEnable y detiene onUpdate
[ OK ] scripts: props del script con valores cambiados en el editor
[ OK ] scripts: interfaz de juego (texto, barra, botón, joystick, color) y onClick
[ OK ] scripts: los toques sobre la interfaz no cuentan como toques del juego
[ OK ] scripts: Input.action, Input.axis, Input.define y Input.key
[ OK ] scripts: Save guarda tablas, números y textos en un archivo y los recupera
[ OK ] geometría: primitivas cerradas, antihorarias y con normales hacia fuera
[ OK ] física: un cubo cae por gravedad y se apoya en el suelo
[ OK ] física: eventos de colisión al empezar y terminar
[ OK ] física: sensores (triggers) detectan sin frenar
[ OK ] física: raycast, impulsos y gravedad
[ OK ] física: el personaje cae, se apoya, camina, sube un escalón y salta
[ OK ] física: capas de colisión, sphereCast y overlaps
[ OK ] física: bisagra sujeta al mundo y cuerda entre dos cuerpos
[ OK ] audio: decodifica un WAV, lo mezcla y termina solo
[ OK ] audio: bucle, volumen y voces que siguen a su entidad
[ OK ] ui: anclajes, pivote, estirado y escala con la altura de pantalla
[ OK ] ui: los hijos se colocan dentro del padre y las entidades inactivas no aparecen
[ OK ] ui: un botón cuenta el clic solo si se suelta encima, y se queda con el dedo
[ OK ] ui: joystick virtual con dirección, límite y zona muerta
[ OK ] ui: UTF-8
[ OK ] ui: fuente TTF del motor con acentos, eñe y signos de apertura
[ OK ] culling: caja transformada con giro, escala y traslación
[ OK ] culling: frustum de perspectiva (delante, detrás, a los lados, lejos)
[ OK ] culling: frustum de una cámara movida y de una ortográfica (2D y sombras)
[ OK ] calidad: cada perfil cambia opciones reales y en orden creciente
[ OK ] calidad: nombres y recomendación según capacidades medibles
[ OK ] materiales: .mat ida y vuelta con mapas, repetición y recorte
[ OK ] materiales: archivos ajenos o futuros se rechazan; escenas antiguas cargan con valores por defecto
[ OK ] luces: focos se guardan y cargan
[ OK ] partículas: ritmo de emisión, vida y máximo
[ OK ] partículas: ráfaga, sin repetir, parar y emisión puntual
[ OK ] partículas: gravedad, dirección, espacio del mundo y emisión por distancia
[ OK ] entrada: teclas, nombres y bordes de pulsación
[ OK ] entrada: acciones por defecto con teclado, mando y diagonal normalizada
[ OK ] entrada: acciones con botones y joystick de la UI, y definiciones propias
[ OK ] animación: estados, condiciones, trigger y salida al terminar el clip
[ OK ] animación: mezcla 1D sincronizada y eventos
[ OK ] animación: la máquina se guarda y se carga; sin estados, el Animator funciona como antes
[ OK ] terreno: base64 ida y vuelta
[ OK ] terreno: esculpir sube, baja, aplana y suaviza con caída suave
[ OK ] terreno: pintura de capas, rayo, montañas, mapa de alturas y cambio de resolución
[ OK ] terreno: se guarda con la escena y tiene colisión

87 pruebas, 0 fallidas (697.8 ms)
```

## Pruebas de integración ejecutadas a mano en esta sesión

| Prueba | Cómo | Resultado |
|---|---|---|
| Demostración técnica: crear → modificar → cerrar → reabrir | `titan demo`, `titan set`, `add-entity`, `add-component`, y luego `tree` e `inspect` en procesos nuevos | Correcto: las entidades, la jerarquía, los componentes y las referencias se recuperan |
| Valor no válido en `titan set` | `titan set ... Transform.scale grande` | Rechazado con código 1; el archivo no cambia |
| Paquete y runtime | `titan package` + `titan_player` con Xvfb y captura | La escena se dibuja; los scripts mueven la nave; el hijo sigue al padre; 0 errores de script |
| Cambio de escena desde scripts | `TITAN_TAPS` con 3 toques simulados | `principal` → `nivel2` → `principal` a los 2 s, según el registro |
| APK | `gradlew assembleRelease`, `apksigner verify`, `aapt2 dump badging`, `llvm-nm`, `llvm-readelf` | Firmado; 3 ABI; paquete incluido; `ANativeActivity_onCreate` exportado; solo depende de bibliotecas del sistema |

## Pruebas del editor (sesión 0.3.0, ejecutadas con `titan_editor` y toques simulados)

| Prueba | Resultado |
|---|---|
| Crear proyecto "3D Exploración" desde el editor y abrirlo | Escena 3D con luces, sombreado e inspector con los componentes reales del Jugador (captura `docs/editor-3d.png`) |
| Jugar en el editor (3D) + 3 toques en el suelo | El jugador camina hasta el punto, recoge una gema ("Gemas: 1 / 5"), la cámara le sigue, 0 errores de script |
| La misma prueba antes de la corrección | Detectó el fallo de `Screen.text` (argumento 5): corregido y cubierto por una prueba nueva |
| Jugar en el editor (2D plataformas) con toques mantenidos | El jugador corre y salta, recoge una moneda, la cámara 2D le sigue |
| Arrastrar la Caja en la vista 3D, cerrar el editor y leerla con `titan inspect` | Posición de (3.5, 0.5, -1) a (3.40, 0.5, 4.10): se mueve sobre el suelo y queda guardada |
| Editor a 1080×2280, densidad 2,75, modo táctil | Distribución de teléfono legible (vista arriba y panel abajo, pestañas) |
| APK del editor | Firmado, 3 ABI, 11 funciones JNI exportadas, clases Kotlin en el dex |

## Interfaz de juego (sesión 0.10.0, editor de escritorio con toques simulados)

| Prueba | Resultado |
|---|---|
| Crear › Interfaz: Barra de vida, Texto, Joystick y Botón en un proyecto 3D | Se dibujan en la vista con su sitio por defecto; el botón seleccionado muestra su rectángulo y su ancla |
| Jugar con un script que lee el joystick y el botón | Arrastrar el joystick mueve el cubo; dos toques en "Saltar" dan "Saltos: 2" y el cubo salta; la barra baja con el tiempo; "¡Ñandú!" se ve con sus tildes |

## Culling (sesión 0.11.0, reproductor de escritorio)

Escena: plantilla 3D con 1600 cubos creados por un script en una rejilla de 120 × 120 m alrededor de la cámara.
`TITAN_SIZE=1280x720 TITAN_FRAMES=150 TITAN_BENCH=1 TITAN_CULLING=0|1 titan_player paquete` (medias de los fotogramas
30 a 150). Render por software (llvmpipe) en un contenedor sin GPU: **las cifras de tiempo no representan a un
teléfono**; los draw calls sí son los mismos en cualquier dispositivo.

| | Sin culling | Con culling |
|---|---|---|
| Mallas dibujadas | 1602 / 1602 | 561 / 1602 |
| Objetos en la pasada de sombras | 1602 | 387 |
| Draw calls (sombras incluidas) | 3206 | 950 |
| Triángulos (pasada principal) | 20 022 | 7 510 |
| Fotograma (dos ejecuciones) | 17,9 y 18,8 ms | 13,7 y 14,9 ms |

Las dos capturas son idénticas píxel a píxel salvo el recuadro de estadísticas. El zorro y CesiumMan del proyecto de
animación siguen viéndose animados y con sombra.

## Jugar con recursos compartidos (sesión 0.12.0, editor de escritorio)

`titan_editor --open Canon --play-at 30 --stop-at 60` (proyecto con CesiumMan.glb, Fox.glb, 2 sonidos y UI de texto),
tres ejecuciones; se mide desde que se pulsa Jugar hasta que termina el primer fotograma del juego.

| | Antes (0.11.0) | Ahora (0.12.0) |
|---|---|---|
| Primer fotograma de Jugar | 324, 285 y 343 ms | 22, 22 y 23 ms |
| Recursos recargados al pulsar Jugar | fuente + 2 modelos (y sus texturas) | ninguno |

Comprobado también:
- Jugar, detener y volver a jugar (`--play-at 5,60 --stop-at 40`) con el HUD: joystick, «Saltos: 2» y texto con tildes.
- Tras detener, la vista del editor muestra la escena, los gizmos y los modelos animados.
- El reproductor independiente da los mismos 950 draw calls en la prueba de 1600 objetos.

## Versión 0.13.0 (PC, editor y reproductor de escritorio; nada en un teléfono)

| Prueba | Resultado |
|---|---|
| Perfiles de calidad con la escena de 1600 objetos | ver tabla siguiente; capturas de "muy bajo" (sin sombras, 60 % de resolución) y "alto" (sombras, FXAA) |
| Bloom y viñeta en el editor jugando | El cubo emisivo brilla; la UI se ve nítida encima. Primer intento con 5 niveles sin normalizar: cielo blanco; corregido |
| Modelo DamagedHelmet (Khronos, CC-BY 4.0) | Se ven el relieve del mapa normal, rayas, metal y la emisión; un foco ilumina el suelo |
| Fuego, humo, chispas y magia en el editor | Los 4 efectos se simulan y se dibujan |
| Teclado simulado (`TITAN_KEYS="D@10-70;Space@20"`) | El cubo se mueve a x = 10,8 con `Input.axis("Mover")` |
| Personaje con teclado | Camina de x = −3 a x = 5,1 y salta (y = 1,1 en el aire) |
| Máquina de estados del zorro | El editor muestra parámetros, estados (mezcla Walk/Run) y transiciones; estado actual "Mover" |
| Terreno con personaje | El personaje cae y camina por las colinas a la altura exacta del terreno (y = 1,54 = altura del terreno) |

Escena de 1600 objetos, 1280×720, render por software (llvmpipe), máquina sin otras tareas. **Los tiempos no
representan a un teléfono**; los draw calls sí.

| Perfil | Resolución de la escena | Sombras | Draw calls | Fotograma |
|---|---|---|---|---|
| Muy bajo | 768×432 | no | 563 | 10,3 ms |
| Bajo | 960×540 | 1024 | 950 | 21,0 ms |
| Medio | 1152×648 | 1024 | 950 | 24,6 ms |
| Alto | 1280×720 | 2048 | 950 | 32,2 ms |
| Muy alto | 1280×720 | 4096 | 951 | 48,7 ms |
| Alto sin culling | 1280×720 | 2048 | 3206 | 37,6 ms |

## Mediciones

| Medida | Valor | Condiciones |
|---|---|---|
| Crear y destruir 100 000 entidades con un componente, y volver a crearlas | incluido en ~30 ms del total de la prueba | Linux x86_64, Release |
| Fotograma de la demo (30 entidades, 146 sprites incluido el texto) | ~1,6 ms CPU, ~290 FPS | **Render por software (llvmpipe) en un contenedor sin GPU: no representa a un teléfono** |

No se publicarán cifras de rendimiento en Android hasta medirlas en dispositivos reales.

## Qué NO está probado

- Nada se ha ejecutado aún en un dispositivo o emulador Android.
- La cámara en perspectiva solo se ha compilado.
- La recuperación tras perder el contexto GL no se puede provocar en escritorio.
- El módulo `Log` no tiene pruebas propias (se usa en todas las demás).
