# Estado del proyecto — Titán 0.8.0

Fecha: 2026-11-21 · Generaciones 1 (Fundación) y 2 (Editor) en curso; primeros pasos de la 5 (3D)

Etiquetas: [COMPLETADO Y PROBADO] · [IMPLEMENTADO Y COMPILADO] · [IMPLEMENTADO, PENDIENTE DE PRUEBA REAL] ·
[IMPLEMENTACIÓN PARCIAL] · [PROTOTIPO TÉCNICO] · [EN DESARROLLO] · [PENDIENTE] · [BLOQUEADO]

"Probado" significa: cubierto por pruebas automáticas que pasan en Linux, o ejecutado de verdad en el reproductor
de escritorio. **Nada se ha probado en un dispositivo Android real**: en este entorno no hay ni teléfono ni emulador
(no hay aceleración KVM).

## Hitos

| Hito | Estado | Evidencia |
|---|---|---|
| A — Motor básico real | **Cumplido en escritorio** | Prueba "proyecto: crear, guardar escena, cerrar y volver a abrir"; demostración con `titan` (ver TESTS.md) |
| B — Primer juego 2D | Parcial | Desde el editor: proyecto 2D, importar imagen, personaje, script, movimiento, cámara, escena, Jugar, texto en pantalla, guardar. Faltan colisionadores/física (las colisiones de la plantilla son por distancia en Lua) y sonido |
| C — Primer juego 3D | Parcial | Desde el editor: proyecto 3D, primitivas, material (color, textura, brillo), cámara, luces, personaje con script, Jugar. Faltan importar modelos y física 3D |
| D — Juego independiente | Parcial | `titan package` + plantilla Gradle generan un APK firmado y validado. Falta instalarlo en un dispositivo y exportar desde el propio Android |

## Editor (en la app)

Probado en escritorio con el mismo código que en Android, manejado de forma automática (toques simulados y capturas).
**No probado en un teléfono.**

| Función | Estado |
|---|---|
| Navegador de proyectos: crear con plantilla 2D/3D, abrir, borrar con confirmación, reabrir el último | [COMPLETADO Y PROBADO] (crear y abrir) |
| Vista de escena: cámara de edición 2D/3D, rejilla, gizmos de cámaras y luces, selección resaltada | [COMPLETADO Y PROBADO] |
| Seleccionar tocando y mover arrastrando (sobre el suelo en 3D, plano XY en 2D) con imán | [COMPLETADO Y PROBADO] (arrastre guardado y releído) |
| Gestos: girar, desplazar, pellizcar para zoom | [IMPLEMENTADO, PENDIENTE DE PRUEBA REAL] (multitáctil sin probar) |
| Jerarquía: crear (vacío, sprite, 6 primitivas, cámara, luces), duplicar, eliminar, arrastrar para cambiar padre | [IMPLEMENTADO Y COMPILADO] |
| Inspector: nombre, activo, Transform, Camera, SpriteRenderer, MeshRenderer, Light, Script; añadir y quitar componentes | [COMPLETADO Y PROBADO] (visual) |
| Editor de código: abrir, crear, guardar, comprobar sintaxis con línea, atajos táctiles | [IMPLEMENTACIÓN PARCIAL] (sin resaltado de sintaxis, autocompletado ni pestañas múltiples) |
| Recursos: importar (selector del sistema en Android, ruta en escritorio), asignar texturas y scripts, escenas | [IMPLEMENTADO Y COMPILADO] |
| Consola con filtros y errores de script con enlace al archivo | [COMPLETADO Y PROBADO] |
| Jugar / Detener dentro del editor (los cambios del juego se descartan al detener) | [COMPLETADO Y PROBADO] (2D y 3D) |
| Deshacer / rehacer | [IMPLEMENTACIÓN PARCIAL] (instantáneas de escena, no comandos) |
| Guardado automático al salir o pausar la app | [IMPLEMENTADO, PENDIENTE DE PRUEBA REAL] |
| Exportar desde el editor | [IMPLEMENTACIÓN PARCIAL] (genera el paquete del juego; el APK se compila en un PC) |
| Teclado en pantalla de Android para escribir código | [IMPLEMENTADO, PENDIENTE DE PRUEBA REAL] |

## 3D

| Función | Estado |
|---|---|
| Primitivas (cubo, esfera, plano, cilindro, cono, cápsula) con normales y caras correctas | [COMPLETADO Y PROBADO] |
| Luz ambiente + direccional + 4 puntuales (Blinn-Phong), profundidad, culling, transparencias | [COMPLETADO Y PROBADO] (visual) |
| PBR metálico/rugosidad, emisión, sombras del sol (PCF), cielo, niebla, tonemapping ACES | [COMPLETADO Y PROBADO] (visual, en escritorio con render por software) |
| Gizmo mover/rotar/escalar | [COMPLETADO Y PROBADO] (arrastres simulados: mover en X y rotar 71,8° en Y, guardados) |
| Importar modelos glTF/GLB (mallas, materiales PBR, texturas incrustadas) | [COMPLETADO Y PROBADO] (Duck, BoxTextured, Fox de Khronos) |
| Animación de esqueletos (skinning GPU, clips glTF, Animator, API Lua) | [COMPLETADO Y PROBADO] (Fox y CesiumMan, en editor y en juego) |
| Transiciones entre clips, máquinas de estados, mapas normales, OBJ/FBX | [PENDIENTE] |
| Física 3D (Jolt): Rigidbody, Collider caja/esfera/cápsula, triggers, raycast, fuerzas, eventos en scripts | [COMPLETADO Y PROBADO] (4 pruebas + nivel de prueba en el editor) |
| Colliders de malla, joints, controlador de personaje, física 2D | [PENDIENTE] |
| Terreno, vegetación, iluminación global, posprocesado (bloom, SSAO) | [PENDIENTE] |

## Core

| Función | Estado |
|---|---|
| Matemáticas 3D (Vec2/3, Quat, Mat4, inversa, descomposición) | [COMPLETADO Y PROBADO] |
| Registro con niveles, categorías, marcas de tiempo y sinks | [IMPLEMENTADO Y COMPILADO] (sin pruebas propias) |
| UUID persistentes | [COMPLETADO Y PROBADO] |
| ECS con generaciones y referencias obsoletas | [COMPLETADO Y PROBADO] |
| Bus de eventos con suscripciones RAII | [COMPLETADO Y PROBADO] |
| Escena: crear, destruir (diferido y recursivo), buscar | [COMPLETADO Y PROBADO] |
| Jerarquía: padres, orden de hijos, ciclos, cambio de padre conservando el mundo | [COMPLETADO Y PROBADO] |
| Transform con matrices de mundo recalculadas solo si cambian | [COMPLETADO Y PROBADO] |
| Serialización JSON versionada con migraciones | [IMPLEMENTACIÓN PARCIAL] (el mecanismo existe; aún no hay ninguna migración porque solo existe la versión 1) |
| Conservación de componentes desconocidos | [COMPLETADO Y PROBADO] |
| Guardado atómico, copia .bak y recuperación | [COMPLETADO Y PROBADO] |
| Proyecto (crear, abrir, guardar) | [COMPLETADO Y PROBADO] |
| Base de datos de recursos con .meta, ids estables y detección de duplicados | [COMPLETADO Y PROBADO] |
| Paquete de juego con validación de referencias | [COMPLETADO Y PROBADO] |
| Carga aditiva, carga asíncrona, persistencia entre escenas | [PENDIENTE] |
| Prefabs (crear, colocar, instanciar en juego) | [COMPLETADO Y PROBADO] (prueba + cañón que dispara 46 balas en el editor); variantes [PENDIENTE] |
| Variables de script en Detalles (props) | [COMPLETADO Y PROBADO] |
| Deshacer / rehacer (sistema de comandos) | [PENDIENTE] (Generación 2, con el editor) |

## Runtime y render

| Función | Estado |
|---|---|
| Bucle con paso fijo de 60 Hz y límite de 5 pasos | [IMPLEMENTADO Y COMPILADO] (ejecutado en escritorio) |
| Cambio de escena desde scripts al final del fotograma | [COMPLETADO Y PROBADO] (ida y vuelta en el reproductor) |
| Cámara ortográfica y en perspectiva, con prioridad | [COMPLETADO Y PROBADO] |
| Sprites por lotes con textura, color, pivote, UV y orden | [COMPLETADO Y PROBADO] (captura del reproductor) |
| Filtro de textura desde el .meta (linear/nearest, mipmaps) | [IMPLEMENTADO Y COMPILADO] |
| Recuperación del contexto GL en Android | [IMPLEMENTADO, PENDIENTE DE PRUEBA REAL] |
| Perfilador en pantalla (FPS, ms por fase, entidades, componentes, draw calls, memoria de Lua) | [IMPLEMENTADO Y COMPILADO] |
| Interfaz de backend gráfico (RHI), mallas, 3D, luces, materiales | [PENDIENTE] |
| Interpolación de render entre pasos fijos | [PENDIENTE] (cuando haya física) |
| Culling | [PENDIENTE] (antes hay que medir) |

## Scripts (Lua 5.4.7)

| Función | Estado |
|---|---|
| onCreate, onStart, onUpdate, onFixedUpdate, onLateUpdate, onDestroy, onEnable, onDisable | [COMPLETADO Y PROBADO] |
| onCollisionEnter/Exit, onTriggerEnter/Exit | [COMPLETADO Y PROBADO] |
| API: Entity, Scene, Time, Input, Screen, Debug | [COMPLETADO Y PROBADO] |
| Errores con archivo, línea, función y traza | [COMPLETADO Y PROBADO] |
| Límite de tiempo y de memoria; aislamiento del sistema | [COMPLETADO Y PROBADO] |
| Acciones de entrada configurables, teclado, mando | [PENDIENTE] |
| `require` entre scripts del proyecto | [PENDIENTE] |
| Editor de código, programación visual | [PENDIENTE] |

## Herramientas

| Función | Estado |
|---|---|
| `titan` CLI: new, demo, tree, inspect, set, add-entity, add-component, assets, check, package | [COMPLETADO Y PROBADO] (ejecutado; ver TESTS.md) |
| Reproductor de escritorio con toques simulados y capturas | [COMPLETADO Y PROBADO] |
| APK del editor (Kotlin + JNI) | [IMPLEMENTADO Y COMPILADO] (firmado y validado; sin instalar en un teléfono) |

## Android

| Función | Estado |
|---|---|
| Compilación del APK (arm64-v8a, armeabi-v7a, x86_64) | [IMPLEMENTADO Y COMPILADO] |
| Firma y validación del APK (apksigner, aapt2) | [COMPLETADO Y PROBADO] (firma de depuración) |
| Arranque, render, táctil, pausa y reanudación en un dispositivo | [IMPLEMENTADO, PENDIENTE DE PRUEBA REAL] |
| Firma de publicación con clave propia | [PENDIENTE] |
| Exportar desde el propio teléfono | [PENDIENTE] (ver ANDROID_PLATFORM.md) |

## Sistemas no iniciados

Física 2D y 3D, audio, animación, partículas, UI de juego, tilemaps, importación de modelos, terrenos, navegación,
guardado de partidas, red, plugins: [PENDIENTE]. El orden está en ROADMAP.md.
