# TITÁN ENGINE — Estado real del desarrollo

Fecha: 2026-11-27 · Versión del código: 0.13.0 + mezclador de audio (0.14 en desarrollo) · ~20 000 líneas propias
(`engine/`, sin `third_party/`) · 129 pruebas automáticas, todas pasan.

**Cómo leer este documento.** Cada fila dice qué existe *de verdad en el código*, con la prueba que lo respalda.

| Etiqueta | Significado |
|---|---|
| ✅ Probado | Pruebas automáticas que miden el resultado, o ejecutado en el reproductor/editor de escritorio con captura |
| 🟡 Parcial | Existe y funciona, pero le faltan partes importantes (se dicen cuáles) |
| 🔨 Compilado | Escrito y compilado, sin prueba que lo ejerza |
| ❌ No existe | No hay código. Ninguna clase vacía ni botón que lo aparente |

**Plataforma principal: PC (Windows y Linux).** Desde 2026-11-27 el editor se orienta a ratón y teclado; Android
queda como objetivo secundario (el código se conserva y compila). **Nada se ha probado en un teléfono Android real** (en este entorno no hay dispositivo ni emulador con KVM). Todo
lo marcado ✅ se probó en Linux x86_64 (GCC, llvmpipe para OpenGL ES). Plan de validación: `docs/VALIDACION_ANDROID.md`.

## Resumen honesto frente a la especificación de 33 secciones

Titán es hoy un motor **forward en OpenGL ES 3.0** con editor táctil propio, ECS, física Jolt, Lua, audio miniaudio
con mezclador, animación esquelética glTF, partículas CPU, terreno, navegación en rejilla y exportación a APK.
Es suficiente para juegos 3D y 2D pequeños o medianos (la demo de plataformas lo demuestra). **No** tiene: Vulkan,
render graph, deferred/clustered, GI, Nanite-like, editor de shaders por nodos, programación visual, red, plugins,
mundos abiertos con streaming (salvo el mundo de bloques), física 2D, IK, secuenciador. Esas partes son trabajo de muchos meses y se abordan en el
orden de `docs/TITAN_ENGINE_ROADMAP.md`. Lo que sigue es el detalle.

## 1. Arquitectura central

| Sistema | Estado | Dónde / evidencia |
|---|---|---|
| Módulos separados (core, ui, scripting, physics, audio, render, runtime, editor, plataformas) | ✅ | `CMakeLists.txt`: 9 bibliotecas estáticas; core no depende de GPU ni de plataforma |
| Entity/Component System | ✅ | `core/World.h` (almacenes por tipo, generaciones), pruebas `ecs:` (100 000 entidades) |
| Escenas, jerarquía, transformaciones local/global | ✅ | `core/Scene.*`, pruebas `jerarquía:` |
| Eventos | ✅ | `core/Events.h` (EventBus con suscripciones RAII), pruebas `eventos:` |
| Serialización versionada + registro de componentes | ✅ | `core/Serialization.*`, pruebas `serialización:` (migraciones, campos desconocidos) |
| Sistema de archivos (FileSource: carpeta, APK, memoria) + escritura atómica | ✅ | `core/FileSource.h`, `core/FileIO.*` |
| Registro (log) a consola, logcat y archivo con rotación | ✅ | `core/Log.*` |
| Gestión de recursos (UUID, .meta, índice, caché compartida editor/juego) | ✅ | `core/Project.*`, `runtime/ResourceCache.*` |
| Reflexión / RTTI de componentes | 🟡 | Registro por nombre con serializar/leer/añadir/quitar; sin reflexión de campos genérica (el inspector está escrito a mano por componente) |
| Configuración del proyecto | 🟡 | `project.titan` (nombre, escena inicial, dimensión); sin ajustes por plataforma |
| Versionado | 🟡 | `core/Version.h`, versión de formato en cada archivo; sin gestor de versiones de proyecto |
| Gestión de errores | 🟡 | Errores con mensaje en registro; scripts aislados (un error no tumba el juego); sin informe de fallos nativos (crash reporter) |
| Sistema de trabajos (Job System): hilos de trabajo, 3 prioridades, contadores, `parallelFor`, espera que ayuda, tareas anidadas | ✅ | `core/JobSystem.*`; 6 pruebas (también con ThreadSanitizer, sin carreras); partículas en paralelo 2,9× más rápidas (medido) |
| Uso del Job System en el motor | 🟡 | Partículas y carga de texturas. Pendiente: modelos, sonidos, animación, culling, compartir hilos con Jolt |
| Carga asíncrona de texturas (lectura + decodificación en otros hilos, subida a la GPU con presupuesto por fotograma, precarga de toda la escena al activarla) | ✅ | 24 texturas 2048²: el hilo principal pasa de 3,4 s bloqueado a 0,22–0,27 s; todas listas en 1,3–1,5 s (antes 3,4–3,6 s). Prueba de decodificación en paralelo |
| Carga asíncrona de modelos glTF y sonidos | ❌ | Siguen siendo síncronos |
| Gestión de memoria (pools, asignadores por fotograma) | ❌ | Se usa el asignador estándar; el ECS reutiliza índices |
| Plugins / registro de módulos dinámico | ❌ | |
| Arranque/cierre ordenado de módulos | 🟡 | `Runtime::start/shutdown` en orden fijo; no hay registro genérico de módulos |

## 2. Render

| Sistema | Estado | Evidencia |
|---|---|---|
| Backend OpenGL ES 3.0 (Android y escritorio) | ✅ | `render/Renderer.cpp`; capturas en `docs/` |
| Abstracción RHI, Vulkan, render graph, command buffers | ❌ | El render llama a GLES directamente |
| Forward con PBR (metal/rugosidad), hasta 8 luces locales + sol | ✅ | Capturas `materiales-casco.png`, `juego-3d.png` |
| Luces direccional, puntual y foco | ✅ | Prueba `luces: focos se guardan y cargan`; benchmark `luces` |
| Sombras: un mapa de sombras del sol con PCF | 🟡 | Sin cascadas, sin sombras de focos/puntuales |
| Sombra de gota (blob shadow) | ✅ | Demo de plataformas |
| Frustum culling | ✅ | Pruebas `culling:` (3) |
| GPU instancing automático | ✅ | Benchmark 1600 objetos: 950 → 8 draw calls |
| Posprocesado: bloom (dual Kawase), FXAA, gradación de color, viñeta, escala de render | ✅ | `render/PostProcess.cpp`; perfiles de calidad |
| Tonemapping ACES | 🟡 | En el sombreador, sobre color LDR (no hay búfer HDR intermedio) |
| Perfiles de calidad según dispositivo | ✅ | Pruebas `calidad:` (2) |
| Deferred, Forward+, clustered, indirect, occlusion/GPU culling, LOD, HLOD, streaming de mallas | ❌ | |
| Luz ambiental basada en imagen (IBL) del cielo procedural: "split-sum" de UE4/UE5 con la aproximación analítica de Karis, cielo prefiltrado por rugosidad, compensación de energía de dispersión múltiple, oclusión especular (Lagarde) y de horizonte | ✅ | Captura comparada antes/después con esferas de metal, pintura y oro de rugosidad 0 a 1 |
| SSAO (oclusión ambiental en pantalla, estilo Alchemy/SAO): media resolución, 12 muestras en espiral, suavizado que respeta bordes; perfiles alto y muy alto; activado en las plantillas nuevas (los proyectos anteriores no cambian) | ✅ | Prueba `calidad:` (perfiles y guardado); capturas de la escena de materiales, el mundo de bloques e Isla Estelar |
| Reflejos en pantalla (SSR): rayo en espacio de cámara con refinado binario y comprobación del impacto, suavizado por confianza, a media resolución; los opacos escriben su reflectancia en un segundo destino (MRT). Perfiles alto y muy alto; activado en las plantillas nuevas | ✅ | Prueba `calidad:`; captura de suelo pulido reflejando cilindro, cubo y esferas, sin rayas |
| Barniz (clear coat) en el material: capa GGX con F0 0,04 y su propia rugosidad, sobre la normal geométrica; atenúa la capa de abajo y también se refleja en el SSR | ✅ | Prueba `calidad:` (guardado); captura de esferas rojas con barniz |
| Sombras del sol en 4 cascadas (CSM) en un atlas 2×2: esferas estables por tramo, alineadas a texels, recorte por cascada, sesgo por la normal según el tamaño del texel y mezcla entre cascadas | ✅ | Capturas (escena de materiales, mundo de bloques): sombras nítidas cerca y hasta "Dist. sombras", sin costuras |
| **Materiales por nodos**: editor visual (Detalles › Material › "Material por nodos"): 19 tipos de nodo (color, número, UV, tiempo, posición, normal, Fresnel, textura, sumar, restar, multiplicar, mezclar, 1-x, potencia, seno, ruido, desplazar UV, tablero, limitar); salidas color base, metal, rugosidad y emisión. Se compila a instrucciones que ejecuta el sombreador en la GPU: los cambios se ven al momento. Límites: 32 operaciones, 2 texturas, solo en el material propio del objeto (no en .mat compartidos), sin salida de normal | ✅ | 4 pruebas `material por nodos:` (resultado numérico, ciclos, límites, guardado); capturas del render y del editor; conectar y desconectar con el ratón probado en el editor |
| Importar FBX con esqueleto, pesos, materiales, texturas y animaciones (ufbx); arrastrar archivos desde el explorador al editor | ✅ | 3 pruebas `fbx:`; personaje de 46 huesos animado en el reproductor; importación desde el editor |
| Antialiasing temporal (TAA): proyección desplazada con Halton (8 muestras), reproyección con la profundidad y la cámara anterior, historia limitada por varianza en YCoCg; sustituye al FXAA; perfiles alto y muy alto; activado en las plantillas nuevas | ✅ | Comparación con y sin TAA (bordes más limpios, sin el ruido del SSR); prueba en movimiento sin estelas. Sin vectores de movimiento por objeto: lo que se mueve rápido se ve algo más borroso |
| Escena en HDR (RGBA16F) con el tonemapping ACES al componer: emisión, sol y brillos por encima de 1 hacen bloom (neón). Cielo y sprites pasan por la inversa de ACES: con HDR la escena se ve igual que sin él (diferencia media medida: 0,14/255). Si la GPU no dibuja en RGBA16F, se sigue sin HDR | ✅ | Comparación medida sin/con HDR; escena de neón con bloom y reflejos |
| Sondas de reflexión con cubemaps, lightmaps, GI, volumétricas, exposición automática | ❌ | Ver `docs/TITAN_RUMBO_AAA.md` |
| TAA, DoF, motion blur, aberración cromática, grano, auto exposición | ❌ | |

## 3. Materiales y shaders

| Sistema | Estado | Evidencia |
|---|---|---|
| Material PBR: color, metal, rugosidad, emisión, mapas normal/oclusión/emisión, recorte alfa, transparencia | ✅ | Pruebas `materiales:` (2) |
| Materiales compartidos `.mat` | ✅ | Ídem |
| Instancias/variantes de material, flujo especular, clear coat, anisotropía, SSS, refracción | ❌ | |
| Editor de shaders por nodos, shaders propios, caché, recarga en caliente | ❌ | El sombreador PBR es fijo |

## 4. Terreno y mundos

| Sistema | Estado | Evidencia |
|---|---|---|
| Terreno por mapa de alturas: esculpir (subir, bajar, aplanar, suavizar), 4 capas de pintura, colisión | ✅ | Pruebas `terreno:` (4); captura `terreno-personaje.png` |
| Vegetación, agua, ríos, caminos/splines, texturas por pendiente/altura | ❌ | |
| **Mundo de bloques (voxel)**: chunks 16×64×16, 14 tipos de bloque, biomas (océano, playa, llanura, bosque, desierto, montaña, nieve, volcánico), cuevas, árboles, generación reproducible por semilla | ✅ | Pruebas `voxel:` (6): determinismo, ≥5 biomas, caras ocultas y fusión (greedy), rayo/colocar/romper, guardado RLE, una parte por chunk |
| Streaming de chunks del mundo de bloques alrededor de la cámara (generar/leer y mallar en los hilos del JobSystem, como mucho 4 subidas a la GPU por fotograma, descarga de lo lejano) | ✅ | Reproductor: 149 chunks, 73 llamadas de dibujo, el personaje anda sobre los bloques (colisión de malla estática de Jolt) |
| Guardado incremental del mundo (solo chunks modificados, RLE, escritura atómica) y recarga | ✅ | Reproductor: romper/colocar → `mundos/mundo/chunks/0_-1.chunk`; segunda ejecución lo carga |
| Plantilla "Mundo de bloques" (Demo A: explorar, romper, colocar, biomas, guardar) y `titan demo-bloques` | ✅ | Editor (`--template 4` + Jugar) y reproductor con teclas simuladas |
| World partition para escenas normales, origen flotante, HLOD | ❌ | El streaming existe solo para el mundo de bloques |
| Vista previa del mundo de bloques en el editor sin pulsar Jugar | ❌ | Se ve al jugar |
| Generación procedural (PCG) de terreno por mapa de alturas | ❌ | Solo "montañas" aleatorias con semilla en la herramienta de terreno |

## 5. Física (Jolt Physics 5.2.0)

| Sistema | Estado | Evidencia |
|---|---|---|
| Cuerpos estático/dinámico/cinemático, caja/esfera/cápsula/malla/envolvente convexa | ✅ | Pruebas `física:` |
| Capas (16) y matriz de colisión | ✅ | |
| Personaje (CharacterVirtual): pendientes, escalones, doble salto, salto variable, paredes, plataformas móviles | ✅ | Pruebas + demo |
| Raycast, sphereCast, overlap (esfera, caja) | ✅ | |
| Joints: fijo, bisagra (con límites), distancia (cuerda, barra, muelle) | ✅ | |
| Eventos de contacto y de trigger (onCollisionEnter/Exit, onTriggerEnter/Exit) | ✅ | Monedas de la demo (trigger con personaje) |
| Paso fijo 1/60 con acumulador | ✅ | |
| Fricción, rebote, amortiguación, escala de gravedad | ✅ | Por Rigidbody (no hay asset de material físico) |
| Física 2D, soft body, tela, cuerda, vehículos, destrucción, depurador visual de física | ❌ | Jolt ofrece soft body y vehículos: integrarlos es trabajo pendiente |

## 6. Animación

| Sistema | Estado | Evidencia |
|---|---|---|
| Esqueletos glTF, skinning en GPU, transiciones suaves | ✅ | Captura `juego-animacion.png` |
| Máquina de estados, mezcla 1D, parámetros, triggers, eventos | ✅ | Pruebas `animación:` (3); editor de la máquina |
| Mezcla 2D, capas, IK, root motion, retargeting, morph targets, timeline/curvas, animación 2D por huesos | ❌ | |
| Animación procedural por script | ✅ | Muñeco de la demo de plataformas (caderas y hombros girados desde Lua) |

## 7. Efectos visuales

| Sistema | Estado | Evidencia |
|---|---|---|
| Partículas CPU: ritmo, ráfagas, por distancia, formas, gravedad, arrastre, color/tamaño en el tiempo, hojas de sprites, aditivo/alfa, espacio local/mundo | ✅ | Pruebas `partículas:` (3) |
| Partículas GPU, colisiones, subemisores, editor por nodos, trails de malla | ❌ | |

## 8. Audio

| Sistema | Estado | Evidencia |
|---|---|---|
| Motor miniaudio: WAV/MP3/FLAC, 2D/3D con atenuación, bucles, tono, voces que siguen entidades | ✅ | `tests/test_audio.cpp` (análisis de señal) |
| **Mezclador: buses con volumen, silencio, bypass, envíos encadenados y medidores** | ✅ | `tests/test_audio_mixer.cpp` (9 pruebas que miden la señal) |
| **Efectos (de Godot, MIT): reverberación, eco, paso bajo/alto/banda, compresor, limitador, distorsión** | ✅ | Ídem: atenuación medida, cola, tiempos de eco, techo, compresión, armónicos |
| Mezclador en el editor (tiras, efectos, parámetros, medidores en vivo, `audio_buses.json`) | ✅ | Captura del panel durante Jugar (demo de plataformas) |
| API de Lua del mezclador | ✅ | Prueba `mezclador: API de Lua` |
| Prueba de sonido y diagnóstico de salida | ✅ | Detecta la salida nula; **no oído en un teléfono todavía** |
| Doppler, oclusión, zonas de reverberación, aleatorización, streaming, OGG, snapshots, eventos de sonido | ❌ | OGG: miniaudio lo admite con stb_vorbis, aún no incluido |
| Coste de CPU de los efectos | ❌ sin medir | Pendiente: benchmark de efectos |

## 9. Escenas y nodos

| Sistema | Estado | Evidencia |
|---|---|---|
| Árbol de escena con padres/hijos, activar/desactivar en jerarquía | ✅ | |
| Prefabs (crear e instanciar, también desde Lua) | ✅ | |
| Cambio de escena desde scripts, carga diferida al final del fotograma | ✅ | Demo: menú → nivel → victoria |
| Escenas anidadas/herencia, grupos, etiquetas, señales conectables, pooling, transiciones | ❌ | |
| Buscar en el árbol | 🟡 | Búsqueda por nombre y tipo en el Outliner |

## 10–11. Scripting

| Sistema | Estado | Evidencia |
|---|---|---|
| Lua 5.4 aislado (sin io/os/require, límites de tiempo y memoria) | ✅ | Pruebas `scripts:` |
| Ciclo de vida completo, props editables, comunicación entre scripts | ✅ | |
| API: entidades, transformaciones, física, personaje, cámara, navegación, IA, audio y mezclador, animación, partículas, UI, entrada, guardado, calidad, depuración | ✅ | `docs/TITAN_ENGINE_API.md` |
| Errores con archivo y línea en la consola | ✅ | |
| Depurador (puntos de parada, pila), perfilador de scripts, recarga en caliente durante el juego, autocompletado | ❌ | |
| Programación visual | ❌ | |

## 12. Editor (ImGui, el mismo en Android y escritorio)

| Sistema | Estado | Evidencia |
|---|---|---|
| Proyectos, escenas, Outliner, Detalles, Contenido, Consola, editor de código | ✅ | Capturas en `docs/` |
| Gizmos mover/rotar/escalar con imán, cámara táctil (1 dedo gira, 2 mueven/zoom) | ✅ escritorio | Gestos multitáctiles sin probar en un teléfono |
| Jugar / Pausa / Detener dentro del editor | ✅ | |
| Deshacer/rehacer | 🟡 | Por instantáneas de escena |
| Editores: máquina de estados, terreno, mezclador de audio | ✅ | |
| Selección múltiple, docking, temas, atajos configurables, búsqueda global, editores de materiales/partículas/animación | ❌ | |

## 13. Importación

| Formato | Estado |
|---|---|
| glTF / GLB (mallas, materiales, esqueletos, animaciones) | ✅ (cgltf) |
| PNG, JPG, TGA, BMP | ✅ (stb_image) |
| WAV, MP3, FLAC | ✅ (miniaudio) |
| TTF/OTF | ✅ |
| OBJ (+MTL y texturas), STL, PLY → convertidos a GLB al importar | ✅ (ver `TITAN_STUDIO.md`) |
| .blend (con Blender instalado) | 🔨 sin probar |
| FBX, DAE, SMD/DMX, WEBP, HDR/EXR, OGG | ❌ |
| Compresión de texturas (ASTC/ETC2), optimización de mallas, LOD automático | ❌ (se detecta si el dispositivo admite ASTC) |

## 14. UI de juego

| Sistema | Estado |
|---|---|
| Rectángulos con anclas y pivote, imagen (esquinas redondeadas, relleno), texto (fuente SDF con acentos), botón, joystick táctil | ✅ |
| Sliders, casillas, campos de texto, scroll, layouts automáticos, texto enriquecido, temas, localización, navegación con mando, editor visual dedicado | ❌ |

## 15. Cámaras

| Sistema | Estado |
|---|---|
| Perspectiva/ortográfica, prioridad, cámara en tercera persona con colisión y temblor | ✅ |
| Mezcla entre cámaras, raíles, secuenciador/cinemáticas, varias vistas | ❌ |

## 16. IA

| Sistema | Estado |
|---|---|
| Rejilla de navegación horneada con física + A* (8 direcciones, suavizado), NavAgent, percepción por vista (`AI.canSee`) | ✅ |
| Navmesh poligonal (Recast), evitación entre agentes, árboles de comportamiento, blackboard, oído, depurador de IA | ❌ |

## 17. Red

❌ No existe nada de red.

## 18. Vegetación, agua y ambiente

| Sistema | Estado |
|---|---|
| Cielo de gradiente, niebla exponencial, ambiente hemisférico | ✅ |
| Vegetación instanciada, agua, nubes, ciclo día/noche, clima, viento | ❌ |

## 19. Depuración y perfilado

| Sistema | Estado |
|---|---|
| Estadísticas en juego (FPS, ms, draw calls, triángulos, visibles, scripts, memoria Lua, cuerpos) y `Debug.stats()` | ✅ |
| Benchmark reproducible (`titan benchmark`: 100–1600 objetos, luces, partículas, física, materiales) | ✅ en PC |
| Informe de diagnóstico copiable; registro en archivo | ✅ |
| Tiempos por sistema en las estadísticas: scripts, física (paso fijo), partículas, transformaciones, render | ✅ |
| Tiempo de GPU, VRAM, perfilador de memoria, depurador de render, captura de fotogramas | ❌ |

## 20. Optimización

Ver secciones 2 y 19: culling, instancing, perfiles de calidad y escala de render existen y están medidos en PC.
Multihilo propio, pools, streaming, carga asíncrona, LOD, oclusión: ❌.

## 21–23. Plataformas y exportación

| Sistema | Estado |
|---|---|
| Android: editor (Kotlin + JNI, horizontal), reproductor NativeActivity, APK de 3 arquitecturas firmado y validado con `apksigner` | ✅ compilado · ❌ sin probar en un teléfono |
| Linux: editor y reproductor (GLFW) | ✅ |
| Windows x86_64 | 🟡 Compila con MSVC en GitHub Actions (CMake + vcpkg + ANGLE sobre Direct3D 11), pasan las pruebas automáticas y se publica en Releases (`windows-ultima`). **El editor abre en un PC con Windows real** (navegador de proyectos visto por el usuario, 2026-11-27). Falta comprobar escenas 3D, Jugar y audio en Windows |
| macOS | ❌ |
| `titan package` (paquete de juego) + plantilla Gradle para APK | ✅ en PC |
| Exportar APK/AAB desde el propio teléfono, perfiles de compilación, iconos/splash configurables | ❌ |

## 24. Plantillas y demos

| | Estado |
|---|---|
| Plantillas de proyecto 2D/3D vacías | ✅ |
| Ejemplo "Isla Estelar" (plantilla del editor y `titan demo-plataformas`): juego 3D completo (menú, nivel, enemigos con IA, monedas, plataformas móviles, peligros, puntos de control, vidas, música y efectos con mezclador, guardado de récord, victoria) | ✅ ejecutado en el reproductor de escritorio (scripts sin errores, monedas recogidas por trigger, 3 buses activos) |
| `titan benchmark` | ✅ |
| Resto de plantillas (top-down, primera persona, carreras, puzle, supervivencia, sandbox, multijugador) | ❌ |

## 25–26. Documentación y pruebas

- Documentos: este, `TITAN_ENGINE_ARCHITECTURE.md`, `TITAN_ENGINE_ROADMAP.md`, `TITAN_ENGINE_API.md`,
  `TITAN_ENGINE_DEPENDENCIES.md`, `TITAN_ENGINE_TESTS.md`, `FILE_FORMATS.md`, `VALIDACION_ANDROID.md`, `AUDITORIA.md`.
- 104 pruebas automáticas (unidad e integración) en `tests/`. Faltan: pruebas de render por comparación de imagen
  automatizada, pruebas de exportación automatizadas, pruebas en Android.

## Registro de trabajo

| Fecha | Bloque | Resultado |
|---|---|---|
| 2026-11-27 | Titán Studio: modos Modelar y Esculpir en el editor, exportar GLB/OBJ | 120/120 pruebas; extruir y esculpir probados en el editor con entrada simulada |
| 2026-11-27 | Titán Studio, fase 1: malla editable con operaciones de modelado, escritor GLB, importar OBJ/MTL, STL, PLY y .blend (con Blender instalado). Ver `docs/TITAN_STUDIO.md` | 118/118 pruebas; OBJ con textura y STL importados y vistos en el reproductor |
| 2026-11-27 | Nueva pantalla de inicio (lanzador): barra lateral, proyectos en tarjetas por fecha con búsqueda y borrado confirmado, galería de plantillas con ilustraciones y la plantilla de ejemplo "Isla Estelar" (juego completo) creada desde el editor | Probado con clics simulados: crear el ejemplo, abrirlo y Jugar |
| 2026-11-27 | Interfaz del editor para PC: escala de Windows, tamaño elegible (100–200 %, Ctrl + / Ctrl −, 125 % por defecto, se recuerda), calidad alta por defecto en PC, editor sin consola | 111/111 pruebas; capturas a 1920×1080 |
| 2026-11-27 | Compilación para Windows en GitHub Actions; el editor abre en un PC con Windows | 3 intentos: errores de MSVC (include, cabeceras GLES) y cuota de Artifacts → Releases |
| 2026-11-27 | Carga asíncrona de texturas con precarga de escena | 111/111 pruebas; arranque 3,4 s → 0,22 s (medido) |
| 2026-11-27 | Job System + partículas en paralelo + tiempos de física y partículas en el perfilador | 110/110 pruebas; TSan limpio; 1,10 → 0,38 ms en partículas |
| 2026-11-27 | Mezclador de audio (efectos de Godot), editor del mezclador, API Lua, demo de plataformas 3D | 104/104 pruebas; demo ejecutada; commit `37d14ca` |
| 2026-11-26 | Instancing GPU, navegación, cámara 3.ª persona, personaje v2, colisión de malla, sombra de gota, benchmark | Ver `docs/CHANGELOG.md` |
