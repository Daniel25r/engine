# Arquitectura de Titán

Titán es un motor de videojuegos propio para Android, escrito en C++17. No se apoya en ningún otro motor.
Este documento describe la arquitectura **tal como está hoy** (v0.2.0) y las decisiones que la fijan.
Lo que aún no existe está marcado como tal.

## 1. Módulos

```
                    ┌────────────────────┐
 Plataforma         │ android/  (NDK)    │   escritorio/ (GLFW)       tools/titan (CLI)
 (ejecutables)      │ main_android.cpp   │   main_desktop.cpp         main.cpp
                    └─────────┬──────────┘          │                     │
                              ▼                     ▼                     │
                    ┌──────────────────────────────────────┐              │
                    │ titan_runtime  (bucle, RenderSystem) │              │
                    └───────┬──────────────┬───────────────┘              │
                            ▼              ▼                              ▼
                 ┌──────────────┐  ┌─────────────────┐                    │
                 │ titan_render │  │ titan_scripting │◄───────────────────┤
                 │ (GLES 3.0)   │  │ (Lua 5.4)       │                    │
                 └──────┬───────┘  └────────┬────────┘                    │
                        ▼                   ▼                             ▼
                 ┌───────────────────────────────────────────────────────────┐
                 │ titan_core: Math, Log, UUID, World (ECS), Events, Scene,   │
                 │ Transform, Serialization, Project, AssetDatabase, FileIO   │
                 └───────────────────────────────────────────────────────────┘
```

| Módulo | Depende de | No puede depender de | Contenido |
|---|---|---|---|
| `titan_core` | biblioteca estándar, nlohmann/json | GPU, plataforma, Lua | Todo lo que define un juego como datos |
| `titan_scripting` | core, Lua | GPU, plataforma | Intérprete aislado y API de scripts |
| `titan_render` | core, OpenGL ES 3.0, stb_image | escenas, entidades | Dibujo por lotes a partir de matrices |
| `titan_runtime` | core, render, scripting | plataforma, editor | Bucle de juego y sistemas |
| plataforma | runtime | — | Ventana, contexto GL, entrada, archivos |
| `tools/titan` | core | GPU | Crear, inspeccionar, modificar y empaquetar proyectos |

Regla: **el Core nunca conoce la GPU ni la plataforma**. Por eso las pruebas del Core corren sin ventana
y el futuro editor podrá manipular escenas sin dibujar nada.

Rutas: cabeceras públicas en `engine/include/titan/<módulo>/`, implementación en `engine/src/<módulo>/`.

## 2. Identidad de las entidades

Hay dos identificadores con funciones distintas:

| | `Entity` | `UUID` (en `EntityInfo.id`) |
|---|---|---|
| Qué es | índice + generación (32 + 32 bits) | número aleatorio de 64 bits, nunca 0 |
| Dónde vive | solo en memoria | en los archivos |
| Para qué | acceso rápido a componentes | referencias persistentes entre archivos |
| Al destruir | la generación sube y las copias antiguas dejan de valer | desaparece del índice de la escena |

`World::alive(e)` detecta referencias obsoletas. La API de Lua comprueba cada acceso y devuelve
"la entidad ya no existe" en lugar de corromper memoria.

## 3. ECS

`World` guarda un *sparse set* por tipo de componente: arreglo denso de datos + tabla dispersa índice→posición.
Recorrer un tipo es recorrer memoria contigua. Cualquier `struct` copiable puede ser componente.
`World` no sabe de escenas, nombres ni jerarquías: eso es responsabilidad de `Scene`.

Toda entidad creada por `Scene` tiene siempre `EntityInfo` (id, nombre, activa), `Hierarchy` y `Transform`.

## 4. Jerarquía y transformaciones

- `Hierarchy`: padre, primer hijo y hermanos anterior/siguiente (lista doble). Las raíces se enlazan igual,
  así el orden de raíces e hijos se conserva al guardar.
- `Transform` guarda posición, rotación (cuaternión) y escala **locales**. Sus setters marcan `dirty`.
- `TransformSystem::update` recorre la jerarquía y recalcula la matriz de mundo **solo** de las entidades
  modificadas y de sus descendientes. Devuelve cuántas recalculó (lo comprueban las pruebas).
- `Scene::setParent(hijo, padre, keepWorld)` rechaza ciclos y, si se pide, recalcula la transformación local
  para que el objeto no se mueva en el mundo.
- Convenciones: sistema diestro, Y arriba, delante = −Z, matrices por columnas, unidades de mundo (no píxeles).

Limitación conocida: una escala no uniforme en un padre rotado produce cizalla, que no se puede guardar como
TRS. `setParent` con `keepWorld` da en ese caso la mejor aproximación sin cizalla.

## 5. Eventos

`EventBus` es síncrono y tipado. `subscribe<E>()` devuelve una `Subscription` RAII: al destruirse se da de baja.
Es seguro darse de baja durante una emisión, y que la suscripción sobreviva al bus.
Eventos actuales: `EntityCreated`, `EntityDestroyed` (se emite antes de borrar los datos),
`SceneLoaded`, `SceneUnloaded`, `AppPaused` y `AppResumed`.

## 6. Destrucción diferida

`Scene::destroyEntity()` solo apunta la entidad. `flushDestroyed()` la destruye junto con sus hijos en
puntos seguros del fotograma. Así un script puede destruir cosas mientras otros sistemas recorren la escena.
`destroyEntityImmediate()` queda para herramientas que no están recorriendo nada.

## 7. Escenas, proyectos y recursos

- Escena: JSON versionado (`docs/FILE_FORMATS.md`). La serialización usa `ComponentRegistry`: cada tipo de
  componente se registra con un nombre estable y sus funciones de guardar y cargar.
- Los componentes de tipo desconocido se conservan intactos (`PreservedComponents`) y se vuelven a guardar.
- La lectura es tolerante: un campo con el tipo equivocado produce un aviso con su ruta y conserva el valor por defecto.
- Guardado atómico: `.tmp` + `fsync` + `rename`, y copia `.bak` de la versión anterior. Si una escena está dañada,
  `loadSceneFile` recupera la `.bak` y lo avisa.
- Proyecto: `project.titan` + carpeta `assets/`. Cada recurso tiene un `.meta` con su UUID: mover o renombrar
  un archivo junto a su `.meta` no rompe ninguna referencia. Si se copian juntos, el duplicado recibe un id nuevo.
- Paquete de juego: `Project::package()` valida las escenas y todas las referencias, copia los recursos sin
  `.meta` y genera `asset_index.json`. El runtime solo entiende paquetes.

## 8. Bucle del runtime

```
frame(ancho, alto):
  dt = min(tiempo real, 0.25 s)
  acumulador += dt
  mientras acumulador >= 1/60 y pasos < 5:      ← paso fijo con límite
      scripts.onFixedUpdate                     ← [física: pendiente]
      escena.flushDestroyed
  scripts.onUpdate, scripts.onLateUpdate
  escena.flushDestroyed
  TransformSystem.update
  render: cámara activa de mayor prioridad, sprites ordenados por sortingOrder y textura
  input.endFrame
  cambio de escena pendiente (Scene.load nunca cambia la escena a mitad de un script)
```

Si se alcanza el límite de 5 pasos fijos, el tiempo sobrante se descarta: tras un parón el juego se ralentiza
en lugar de entrar en una espiral de cálculos. La interpolación de render entre pasos fijos no está hecha:
no hará falta hasta que haya física.

## 9. Render

`Renderer` (OpenGL ES 3.0) solo recibe matrices y sprites: no conoce escenas.
`Runtime::render` hace de RenderSystem: busca la cámara, construye la lista de dibujo y la ordena.
Los sprites se agrupan en lotes de hasta 8192 cuadrados por llamada; solo un cambio de textura corta el lote.

Hoy **no existe** una interfaz de backend gráfico (RHI): `Renderer` llama a OpenGL directamente.
Se extraerá cuando llegue el render 3D, antes de considerar Vulkan.

## 10. Scripts

Lua 5.4.7 compilado como C++ (sus errores son excepciones, así los destructores de C++ se ejecutan bien).
Detalles y API en `docs/ENGINE_API.md`. Aislamiento:

- Sin `io`, `os`, `package`, `debug`, `load`, `loadfile`, `dofile` ni `string.dump`. Solo se carga código de texto, nunca bytecode.
- Límite de memoria del intérprete (asignador propio; 256 MB por defecto).
- Límite de tiempo por llamada (0,25 s por defecto) mediante un *hook* cada 10 000 instrucciones.
- Una instancia que falla se desactiva y su error queda registrado con archivo, línea, función y traza.

## 11. Decisiones de arquitectura

| # | Decisión | Motivo | Alternativas descartadas |
|---|---|---|---|
| D1 | Núcleo en C++17 con CMake | Mismo código en Android (NDK) y escritorio; control de memoria | Kotlin/JVM para el núcleo (GC, rendimiento) |
| D2 | Primer backend: OpenGL ES 3.0 | Lo admiten casi todos los Android desde la versión 4.3; depuración más simple | Vulkan primero (más trabajo y peor soporte en gama baja) |
| D3 | Lua 5.4 para scripts | Pequeño (~30 000 líneas), MIT, fácil de aislar, probado en juegos | Lenguaje propio (sin justificación), JS/QuickJS (más pesado), C# (Mono, pesado en Android) |
| D4 | JSON versionado para escenas | Legible, fácil de migrar y de combinar en git | Binario propio (difícil de migrar); se puede añadir un formato binario "cocinado" para paquetes |
| D5 | UUID de 64 bits en `.meta` | Referencias que sobreviven a renombrar o mover | Rutas como referencia (se rompen al reorganizar) |
| D6 | Runtime separado del editor | El APK exportado no lleva herramientas | Un único binario con modo editor |
| D7 | Runtime de Android con NativeActivity | Sin Java en el juego exportado | GameActivity (se evaluará al añadir teclado y mando) |
| D8 | Editor en C++ con Dear ImGui sobre el renderizador propio; Kotlin solo para la actividad, el teclado y los archivos | Un único editor para Android y escritorio que se puede ejecutar y probar de forma automática; la vista de escena y el modo Play comparten contexto GL con la interfaz | Interfaz Kotlin/Compose + JNI (decisión anterior: duplicaba la interfaz por plataforma y no se podía probar sin dispositivo) |

## 11b. Pipeline gráfico 3D (v0.4)

1. Sombras: pasada de profundidad del sol a un mapa de 2048×2048 (ortográfico, centrado delante de la cámara y
   alineado a texels).
2. Pasada principal: cielo procedural (triángulo a pantalla completa) → mallas opacas de delante a atrás →
   transparentes de atrás a delante → sprites → líneas del editor → texto.
3. Sombreado por píxel: PBR (GGX + Smith + Schlick) con sol y hasta 4 luces puntuales, ambiente hemisférico de
   cielo, emisión, ACES + gamma y niebla exponencial. Las entradas de color son sRGB y se convierten a lineal.

Límites conocidos: sin HDR intermedio ni posprocesado (el tonemapping se hace en cada malla), una sola cascada de
sombras, sin iluminación global ni oclusión ambiental. Son los siguientes pasos hacia escenas de gran calidad.

## 12. Editor

`titan_editor` (C++, Dear ImGui) depende de runtime, render y core. La plataforma implementa `EditorPlatform`:
carpeta de proyectos, teclado en pantalla, selector de archivos, portapapeles, fuentes y densidad.
- Escritorio: `engine/platform/desktop/editor_desktop.cpp` (GLFW), con opciones de automatización para pruebas.
- Android: `android/editor` (Kotlin: `GLSurfaceView`, `InputConnection`, SAF) + `editor_jni.cpp`.

La escena que se edita es una `titan::Scene`. Jugar serializa la escena en memoria y la ejecuta con un `Runtime`
aparte; al detener se descarta y la escena de edición queda como estaba. Deshacer y rehacer guardan, de momento,
instantáneas de la escena serializada (pendiente: comandos por operación).

## 13. Límites actuales

Ver `docs/PROJECT_STATUS.md`. Resumen: sin física, audio, animación, prefabs, importación de modelos ni UI de juego (solo `Screen.text`).
Nada se ha probado aún en un dispositivo Android real.
