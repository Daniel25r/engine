# Auditoría técnica de Titán

Realizada sobre la 0.10.0 y actualizada en la 0.12.0. Fecha: 2026-11-22. Revisión hecha sobre el código del repositorio (no sobre la lista de funciones anunciadas).

**Cómo leer las marcas:**

- **Verificado**: tiene pruebas automáticas, o se ha ejecutado de verdad y se ha mirado el resultado (captura o registro).
- **Sin verificar**: compila, pero nadie lo ha ejecutado comprobando el resultado.
- **Teléfono**: probado en un Android real. Hoy **ninguna** función tiene esta marca: el motor nunca se ha ejecutado en un teléfono.

## 1. Datos generales

| | |
|---|---|
| Lenguajes | C++17 (motor, editor y herramientas), Kotlin (aplicación Android del editor), Lua 5.4.7 (scripts de los juegos), GLSL ES 3.00 (shaders) |
| Compilación | CMake 3.22 y Gradle con el NDK (arm64-v8a, armeabi-v7a, x86_64) |
| Código propio | unas 10 000 líneas de motor y editor; 1 400 de pruebas; 61 pruebas automáticas (todas pasan) |
| API gráfica | OpenGL ES 3.0, llamada directamente (no hay capa de abstracción, RHI) |
| Plataformas | Android: editor (GLSurfaceView + JNI) y reproductor (NativeActivity). Escritorio Linux: editor y reproductor sobre GLFW, solo para desarrollo y pruebas |

### Dependencias externas

Todas se compilan desde el código fuente incluido en el repositorio. Ninguna es de pago y ninguna obliga a publicar el código del juego.

| Biblioteca | Versión | Licencia | Uso |
|---|---|---|---|
| Lua | 5.4.7 | MIT | Scripts |
| Jolt Physics | 5.2.0 | MIT | Física 3D |
| miniaudio | 0.11.21 | Dominio público / MIT-0 | Audio |
| Dear ImGui | 1.91.5 | MIT | Interfaz del editor (no va en los juegos) |
| cgltf | 1.14 | MIT | glTF/GLB |
| nlohmann/json | 3.11 | MIT | Formatos JSON |
| stb_image, stb_image_write, stb_truetype | 2.34 / 1.19 / 1.28 | Dominio público / MIT | Imágenes y fuentes |
| Fuente Outfit | — | SIL OFL 1.1 | Texto de los juegos |
| GLFW | 3.3 | zlib | Solo en escritorio |
| android_native_app_glue | NDK | Apache 2.0 | Reproductor Android |

## 2. Mapa de la arquitectura actual

```
 Plataforma         android/editor (Kotlin+JNI)   android/app (NativeActivity)   escritorio (GLFW)   tools/titan (CLI)
                              │                           │                           │                  │
 Aplicaciones       titan_editor (ImGui) ──────────► titan_runtime ◄──────────────────┘                  │
                              │                     (bucle, escenas, UISystem, SceneRenderer, UIRenderer)   │
                              │                ┌──────────┼──────────┬────────────┬──────────┐            │
 Sistemas              titan_render      titan_scripting  titan_physics  titan_audio   titan_ui          │
                       (GLES3, cgltf,    (Lua aislado)    (Jolt)        (miniaudio)   (fuentes SDF,      │
                        stb)                                                         diseño, toques)     │
                              └──────────┴──────────┬───────┴────────────┴──────────┘                    │
 Núcleo                                        titan_core ◄──────────────────────────────────────────────┘
                     (matemáticas, ECS, Scene, EventBus, Serialización, Project/AssetDatabase, Log, FileIO, Input)
```

**Lo que está bien resuelto en esta arquitectura:**

- El runtime no depende del editor: el reproductor y el APK exportado no llevan ImGui ni código del editor.
- El editor y el juego usan el mismo SceneRenderer y el mismo Runtime, así que «Jugar» ejecuta exactamente lo mismo que el juego exportado.
- Física, audio, scripts y render solo ven el Core a través de interfaces (`PhysicsApi`, `ScriptHost`, `FileSource`).
- Los recursos se identifican por un id de 64 bits guardado en su archivo `.meta`, no por su ruta: mover o renombrar un archivo no rompe las referencias.
- Los formatos (escena, proyecto, índice de recursos) son JSON con número de versión. Las escenas tienen una cadena de migraciones.
- El guardado es atómico (archivo temporal y renombrado) y deja una copia `.bak`.

## 3. Sistemas existentes y su estado real

| Sistema | Estado | Comprobado con |
|---|---|---|
| ECS (sparse sets, generación de índices), jerarquía, eventos | Verificado | 18 pruebas |
| Escenas JSON versionadas con migraciones; componentes desconocidos conservados | Verificado | 4 pruebas |
| Proyecto, base de datos de recursos con .meta, paquete con validación de referencias | Verificado | 6 pruebas |
| Guardado atómico + .bak | Verificado | 1 prueba |
| Scripts Lua: ciclo de vida, aislamiento, límites de tiempo y memoria, errores con traza | Verificado | 14 pruebas |
| Física Jolt: cuerpos, colliders caja/esfera/cápsula, triggers, eventos, raycast | Verificado | 4 pruebas + juego en el editor |
| Audio miniaudio: mezcla, voces 3D | Verificado sin altavoces | 2 pruebas (mezcla en memoria). **Nunca se ha escuchado** |
| Render PBR, sombra del sol, cielo, niebla, ACES | Verificado en PC | Capturas (render por software llvmpipe) |
| glTF con esqueleto, 256 huesos, transiciones | Verificado en PC | Capturas (zorro, CesiumMan) |
| UI de juego: anclajes, texto SDF, botones, joystick | Verificado | 8 pruebas + juego en el editor |
| Editor: Outliner, Detalles, gizmos, deshacer, prefabs, Play | Verificado en PC | Toques simulados y capturas |
| APK del editor y del reproductor | Sin verificar | Se compilan y se firma; **nunca instalados en un teléfono** |
| Pérdida del contexto GL en Android | Sin verificar | Código escrito; nunca ejecutado |

**Riesgo número 1:** nada se ha ejecutado en un teléfono. Mientras no haya una prueba en Android real, todo lo que depende de la GPU, la pantalla táctil, el audio o el ciclo de vida de Android está sin confirmar.

## 4. Problemas detectados (con el código que los demuestra)

### Rendimiento

1. ~~**No hay culling.**~~ Resuelto en 0.11.0. `SceneRenderer::render` dibuja todas las mallas activas, tanto en la pasada principal como en la de sombras, aunque estén detrás de la cámara. También calcula la pose de todos los personajes animados aunque no se vean. Este problema impide los mundos grandes, el terreno y el LOD.
2. **Una llamada de dibujo por malla, con unas 20 `glUniform` en cada una.** No hay instancing ni agrupación de mallas. Mil cubos son mil draw calls en la pasada principal, y otros mil en la de sombras.
3. **`isActiveInHierarchy` recorre todos los padres** cada vez que se consulta, y se consulta para cada entidad en varios sistemas por fotograma. Coste O(n·profundidad).
4. ~~**Cada «Jugar» crea un Runtime y un Renderer nuevos.**~~ Resuelto en 0.12.0: Jugar usa el Renderer y la caché del editor. Se recompilan los shaders y se recargan desde el disco todas las texturas, modelos y fuentes. Solo el atlas de una fuente tarda unos 0,25 s en PC. El arranque de Play se alarga con el tamaño del proyecto.
5. **Toda la carga es síncrona** y se hace en el hilo de render: texturas, glTF y atlas de fuentes. No hay sistema de tareas propio (solo el pool de hilos interno de Jolt).
6. **Las texturas se suben sin comprimir** (RGBA8). Una de 2048×2048 con mipmaps ocupa unos 21 MB de memoria de GPU. No hay ETC2 ni ASTC.
7. **La interfaz se recalcula dos veces por fotograma**, con `std::function` recursiva y vectores temporales. El coste es pequeño, pero evitable.
8. `Entity.find(nombre)` en Lua recorre todas las entidades.

### Arquitectura

9. **`Renderer.cpp` es un bloque único**: shaders en línea, mallas, sprites, texto, sombras y cielo, todo con llamadas GL directas. Sin una capa RHI, añadir Vulkan exigiría reescribirlo.
10. **No hay render targets**: se dibuja directamente en pantalla y el tonemapping se hace dentro del shader de mallas. Por eso no son posibles el posprocesado (bloom, SSAO, FXAA), la escala de resolución ni el HDR.
11. **El deshacer guarda la escena entera** (JSON) en cada cambio, hasta 100 copias. Con escenas grandes, la memoria crece y cada cambio va más lento.
12. **No hay perfiles de calidad ni detección de capacidades** (límites de textura, extensiones, memoria). Los valores están fijos: sombra de 2048, 4 luces.
13. **No hay sistema de plugins, ni de tareas, ni configuración global** del motor.
14. **Las escenas no se pueden cargar de forma aditiva ni en segundo plano.** Solo hay una escena activa.

### Funciones incompletas

15. **Materiales:** faltan mapas normales, ORM (oclusión, rugosidad y metal en una textura), alpha clip, tiling y offset, y materiales como recurso. Los modelos no tienen tangentes.
16. **Luces:** no hay focos. Las luces puntuales no proyectan sombras y la sombra del sol usa una sola cascada.
17. **Física:** faltan colliders de malla y de terreno (heightfield), capas, controlador de personaje y joints. Las consultas se limitan al raycast.
18. **Animación:** faltan máquinas de estados, blend trees, IK y root motion.
19. **Entrada:** solo hay táctil en los juegos. No hay acciones, mandos ni teclado.
20. **Audio:** faltan mezclador y efectos. El formato OGG está desactivado.
21. **Prefabs:** faltan variantes, overrides y la propagación de cambios a las instancias.
22. **Recursos:** no se reimportan al cambiar el archivo y no hay miniaturas de modelos.
23. **Exportación:** el APK necesita un PC. Faltan ajustes de compilación (id de aplicación, icono, orientación, versión).
24. **Scripts:** faltan `require` entre scripts, depurador, autocompletado y guardado de partidas.

### Errores conocidos

25. Al seleccionar un personaje animado con el dedo, el editor usa la caja de su pose de reposo, así que puede fallar.
26. En el editor, la barra de la vista tapa la esquina superior izquierda de la UI del juego cuando no se está jugando. Al jugar se aparta.

## 5. Madurez por área (escala: prototipo → alfa → beta → producción)

| Área | Nivel | Motivo |
|---|---|---|
| Núcleo (ECS, escenas, formatos) | Beta | Probado, versionado y con migraciones; falta el sistema de tareas |
| Scripts Lua | Beta | Seguro y probado; la API es amplia, pero faltan módulos y el depurador |
| Física | Alfa | Jolt es de producción, pero la integración solo cubre lo básico |
| Render | Alfa | Buen sombreado; sin culling, render targets, RHI ni compresión |
| Animación | Alfa | Esqueletos y transiciones; sin máquinas de estados |
| Audio | Alfa | Funciona en pruebas; nunca escuchado |
| UI de juego | Alfa | Base sólida; faltan los controles avanzados |
| Editor | Alfa | Completo para escenas pequeñas; deshacer costoso, sin selección múltiple |
| Android | Prototipo | Compila; nunca ejecutado en un dispositivo |
| Exportación | Prototipo | Paquete correcto; APK solo desde PC |

## 6. Arquitectura propuesta

Se mantiene la división actual, que funciona, y se añaden piezas por debajo y por encima.

- **Core**: añadir un **JobSystem** (pool de hilos con dependencias), un **ResourceManager** con caché compartida, carga asíncrona y recuento de referencias, y **Config** (perfiles de calidad y capacidades del dispositivo).
- **Render**:
  - Una capa **RHI** con la interfaz mínima (buffers, texturas, render targets, pipelines y comandos), con GLES 3.0 como primer backend. Vulkan llegaría más tarde, cuando se pueda probar en dispositivos reales.
  - Por encima, un **render graph** sencillo: sombras → opacos → cielo → transparentes → posproceso → UI.
  - **Culling** y **listas de dibujo** con instancing.
- **Runtime**: un planificador de sistemas con fases fijas (entrada, lógica, física, animación, audio, render) y escenas aditivas.
- **Editor**:
  - Comandos reversibles por operación, en lugar de copias completas de la escena.
  - Paneles como módulos registrados, para que un plugin pueda añadir el suyo.
- **Plugins**: un manifiesto JSON con id, versión, dependencias y permisos.
  - Primera generación en **Lua aislado**: menús, paneles y herramientas del editor, con los mismos límites que los scripts. En Android, cargar código nativo de terceros es un riesgo de seguridad y de estabilidad.
  - Un fallo desactiva el plugin, no el editor.
- **API estable**:
  - La API de Lua y los formatos llevan número de versión.
  - Las funciones obsoletas se mantienen con un aviso hasta la siguiente versión mayor.

## 7. Hoja de ruta ordenada por dependencias

1. **Visibilidad** (culling y cajas de los objetos). No depende de nada, y la necesitan el LOD, el terreno, la vegetación, las sombras en cascada y el instancing.
2. **Recursos compartidos entre el editor y Play**, con un sistema de tareas para cargar en segundo plano. Lo necesitan el streaming, los mundos grandes y las miniaturas.
3. **Render targets, HDR, posproceso y perfiles de calidad**, con detección de capacidades. Lo necesitan el bloom, el SSAO, el FXAA, la escala de resolución y el ajuste por gama de teléfono.
4. **Materiales v2**: tangentes, normal, ORM, emisión, alpha clip, tiling y materiales como recurso. Lo necesitan el terreno, el agua, el editor de materiales y los shaders personalizados.
5. **Input Actions**: táctil, teclado y mando con el mismo código. Lo necesitan el controlador de personaje, la UI navegable con mando y los juegos de PC.
6. **Física v2**: capas, controlador de personaje, consultas de formas, mallas, heightfield y joints. Necesita el 4 para el terreno.
7. **Animación v2**: máquinas de estados, blend trees, eventos, IK de dos huesos y root motion.
8. **Terreno**, con esculpido, capas y vegetación instanciada. Necesita el 1, el 3, el 4 y el 6.
9. **Partículas**. Necesitan el 1 y el 3.
10. **Editor v2**: selección múltiple, comandos de deshacer, bloqueo, capas y etiquetas, y vista de juego.
11. **Scripts v2**: `require`, guardado de partidas, depurador y autocompletado.
12. **2D completo**: tilemaps, física 2D con Box2D y luces 2D.
13. **Audio v2**: mezclador, grupos y efectos.
14. **Exportación profesional**: ajustes de compilación y APK reproducible desde PC.
15. **Navegación (NavMesh), IA, red y plugins.**

## 8. Las cinco mejoras iniciales de mayor impacto

| # | Mejora | Por qué primero | Cómo se verifica |
|---|---|---|---|
| 1 | Culling por frustum en las pasadas principal y de sombras, y cálculo de poses solo de lo visible | Base de los mundos grandes; reduce draw calls sin cambiar el aspecto | Pruebas de geometría del frustum; escena de prueba con miles de objetos, comparando draw calls y tiempo con y sin culling; capturas idénticas |
| 2 | Recursos compartidos entre el editor y Play | Play inmediato; menos memoria (hoy cada textura está dos veces en la GPU mientras se juega) | Medir el tiempo de arranque de Play y las texturas en memoria antes y después |
| 3 | Render targets, posproceso y perfiles de calidad | Desbloquea casi todos los efectos y la adaptación a cada gama de teléfono | Capturas por perfil; prueba de que cada perfil cambia de verdad la resolución de sombras, la escala y los efectos |
| 4 | Materiales v2 | Calidad visual de los modelos importados | Modelos glTF de referencia de Khronos con mapas normales; comparación visual |
| 5 | Input Actions | El mismo juego funciona con dedo, teclado y mando | Pruebas automáticas de los mapas de entrada; juego de prueba en escritorio con teclado |

## 9. Mejoras realizadas desde la auditoría

| Versión | Mejora | Resultado medido (PC, ver `docs/TESTS.md`) |
|---|---|---|
| 0.11.0 | Culling por frustum | 1600 objetos: draw calls de 3206 a 950; imagen idéntica |
| 0.12.0 | Jugar comparte el Renderer y la caché de recursos del editor | Primer fotograma de Jugar: de 285–343 ms a 18–23 ms; ninguna textura ni modelo duplicado en la GPU |

Pendiente de la mejora 2: sistema de tareas y carga en segundo plano (hoy la primera carga de cada recurso sigue
bloqueando un fotograma).

## 10. Comparación técnica con motores profesionales

No pretende decir quién es «mejor»: sitúa a Titán respecto a lo que esos motores ya resuelven. Unity, Unreal y Godot
llevan décadas, o al menos años, con equipos grandes; la distancia es enorme en casi todo.

| Área | Unity 6 (URP) | Unreal Engine 5 | Godot 4 | Titán 0.12 |
|---|---|---|---|---|
| API gráfica | Vulkan, GLES 3, Metal, DX12 | Vulkan, DX12, Metal (GLES solo en móvil) | Vulkan, GLES 3 (Compatibility) | GLES 3.0, sin capa de abstracción |
| Pipeline | Forward+, deferred, render graph | Deferred, Nanite, Lumen, TSR | Forward+, móvil, compatibilidad | Forward propio con culling por frustum |
| Iluminación global | Lightmaps, probes, APV | Lumen (tiempo real), lightmaps | SDFGI, VoxelGI, lightmaps | Ninguna (ambiente de cielo hemisférico) |
| Posprocesado | Pila completa | Pila completa | Pila completa | Solo ACES en el shader |
| Animación | Mecanim, Timeline, IK, retargeting | Control Rig, Motion Matching, IK | AnimationTree, IK | Esqueletos glTF, transiciones; sin máquina de estados |
| Física | PhysX, Box2D | Chaos | Jolt (desde 4.4), Godot Physics | Jolt, solo lo básico |
| Scripting | C# | C++, Blueprints | GDScript, C# | Lua 5.4 aislado |
| Editor en el teléfono | No | No | Sí (Android, desde 4.2) | Sí, diseñado para el tacto |
| Tamaño del proyecto | Cientos de MB | GB | Decenas de MB | Unos pocos MB, formatos JSON legibles |

Ventajas propias que se pueden defender con hechos:

- Editor pensado primero para el móvil, horizontal y táctil.
- Formatos legibles y versionados, con migraciones.
- Proyectos diminutos.
- Lua con aislamiento real (sin `io`, `os` ni `require`; límites de tiempo y memoria).

**Godot también tiene editor para Android**, así que la diferencia de Titán no puede ser solo «funciona en Android».
Tiene que ser una experiencia táctil mejor y un ciclo de prueba más rápido, y eso habrá que medirlo con usuarios y en
teléfonos reales.
