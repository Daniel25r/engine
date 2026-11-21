# Estado del proyecto — Titán 0.2.0

Fecha: 2026-11-21 · Generación actual: **1 (Fundación)**

Etiquetas: [COMPLETADO Y PROBADO] · [IMPLEMENTADO Y COMPILADO] · [IMPLEMENTADO, PENDIENTE DE PRUEBA REAL] ·
[IMPLEMENTACIÓN PARCIAL] · [PROTOTIPO TÉCNICO] · [EN DESARROLLO] · [PENDIENTE] · [BLOQUEADO]

"Probado" significa: cubierto por pruebas automáticas que pasan en Linux, o ejecutado de verdad en el reproductor
de escritorio. **Nada se ha probado en un dispositivo Android real**: en este entorno no hay ni teléfono ni emulador
(no hay aceleración KVM).

## Hitos

| Hito | Estado | Evidencia |
|---|---|---|
| A — Motor básico real | **Cumplido en escritorio** | Prueba "proyecto: crear, guardar escena, cerrar y volver a abrir"; demostración con `titan` (ver TESTS.md) |
| B — Primer juego 2D | Parcial | Hay proyecto, imagen, script, movimiento, cámara y ejecución. Faltan colisiones, física, interfaz y sonido |
| C — Primer juego 3D | Pendiente | — |
| D — Juego independiente | Parcial | `titan package` + plantilla Gradle generan un APK firmado y validado. Falta instalarlo en un dispositivo y exportar desde el propio Android |

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
| Prefabs | [PENDIENTE] |
| Deshacer / rehacer (sistema de comandos) | [PENDIENTE] (Generación 2, con el editor) |

## Runtime y render

| Función | Estado |
|---|---|
| Bucle con paso fijo de 60 Hz y límite de 5 pasos | [IMPLEMENTADO Y COMPILADO] (ejecutado en escritorio) |
| Cambio de escena desde scripts al final del fotograma | [COMPLETADO Y PROBADO] (ida y vuelta en el reproductor) |
| Cámara ortográfica y en perspectiva, con prioridad | [IMPLEMENTACIÓN PARCIAL] (la ortográfica se ha ejecutado; la perspectiva solo se ha compilado) |
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
| onCollisionEnter/Exit, onTriggerEnter/Exit | [PENDIENTE] (necesitan física) |
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
| Editor Android (Kotlin + JNI) | [PENDIENTE] — Generación 2 |

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
