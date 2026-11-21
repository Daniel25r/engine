# Hoja de ruta

Cada generación entrega una versión que compila, pasa las pruebas y se puede ejecutar.
El orden sigue las prioridades del documento maestro: arquitectura → Core → editor → escenas → render →
recursos → runtime → física → scripts → 2D → 3D → animación → audio → UI → depuración → optimización →
exportación → herramientas avanzadas → extensiones.

## Generación 1 — Fundación (en curso, v0.2)

Hecho: Core (ECS, escenas, jerarquía, transformaciones, eventos, serialización, proyectos, recursos),
runtime con render 2D y scripts Lua, herramienta `titan`, paquete de juego, APK.

Falta para cerrarla:
1. Probar el APK en un teléfono real: arranque, toques, pausa y reanudación, rotación, pérdida de contexto.
2. Pruebas propias del registro (`Log`) y del bucle del runtime con un reloj simulado.

## Generación 2 — Editor Android

1. Módulo `editor/` de Kotlin + Jetpack Compose, con biblioteca JNI `titan_editor` sobre `titan_core`.
2. Gestor de proyectos en el almacenamiento privado de la app; importar y exportar con Storage Access Framework.
3. Vista de escena con el mismo `Renderer` en un `SurfaceView`, más gizmos táctiles de mover, rotar y escalar.
4. Jerarquía (arrastrar para cambiar de padre) e Inspector generado desde `ComponentRegistry`.
   Para eso cada componente describirá sus campos (reflexión).
5. Sistema de comandos para deshacer y rehacer (comandos por campo y por estructura, no capturas de escena).
6. Modo Play dentro del editor: la escena se copia en memoria y al detener se restaura la de edición.
7. Consola conectada a los sinks de `Log`.
8. Autoguardado y detección de cierre anormal.

## Generación 3 — Motor 2D

Física 2D con Box2D v3 detrás de una interfaz propia (`Rigidbody2D`, `Collider2D`, eventos de contacto,
raycasts, capas). Interpolación de render. Hojas de sprites y animación de sprites. Tilemaps con editor.
Audio con miniaudio (sobre AAudio/OpenSL ES), con `AudioSource` y `AudioListener`. UI de juego
(Canvas, anclajes, botones, texto con fuentes TTF). Prefabs.

## Generación 4 — Programación

Editor de código con resaltado, autocompletado desde la API y diagnósticos. `require` entre scripts.
Acciones de entrada configurables y controles táctiles (joystick y botones). Programación visual por nodos
que se ejecuta sobre la misma API.

## Generación 5 — Motor 3D

Interfaz de backend gráfico (RHI). Mallas, buffer de profundidad, materiales y shaders propios, luces y sombras.
Importación glTF/GLB con cgltf y OBJ. Física 3D con Jolt. Controlador de personaje.

## Generación 6 — Herramientas profesionales

Animación con línea de tiempo, curvas y esqueletos. Partículas. Terreno. Perfilador ampliado.
Carga asíncrona de recursos.

## Generación 7 — Exportación

Exportación desde el editor: validación, procesamiento de recursos y compilación. Primero con una toolchain de
escritorio o remota, porque compilar con NDK y Gradle dentro del teléfono no es viable de forma general
(ver ANDROID_PLATFORM.md). Alternativa en estudio: un APK runtime precompilado al que se añade el paquete
del juego y que se vuelve a firmar en el dispositivo, sin compilar código nativo.

## Generación 8 — Expansión

Red (cliente/servidor con autoridad del servidor), plugins con versionado y permisos, formatos nuevos y Vulkan.
