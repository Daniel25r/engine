# Titán frente a Unity

Este documento recorre los sistemas de Unity 6 (el editor, el runtime y los paquetes oficiales más usados),
explica brevemente cómo funcionan y dice dónde está Titán. Es la hoja de ruta para llegar a su nivel.
Estado de Titán a fecha de la versión 0.11.0.

Leyenda: ✅ hecho · 🟡 parcial · ❌ pendiente

## 1. Núcleo y flujo de trabajo

| Unity | Cómo funciona en Unity | Titán |
|---|---|---|
| GameObject + Component | Cada objeto es un contenedor de componentes; los scripts heredan de MonoBehaviour | ✅ Entidades ECS con componentes; scripts por entidad |
| Escenas (.unity, YAML) | Escenas serializadas; carga aditiva y asíncrona | 🟡 Escenas JSON versionadas; falta carga aditiva y asíncrona |
| Prefabs y variantes | Plantillas reutilizables con anulaciones por instancia | 🟡 Crear, colocar e instanciar en juego; ❌ variantes y propagación de cambios a instancias |
| Asset Database + .meta | Ids (GUID) persistentes en .meta, reimportación automática | ✅ Ids de 64 bits en .meta, detección de duplicados; 🟡 sin reimportación al cambiar el archivo |
| Undo/Redo | Sistema de comandos en todo el editor | 🟡 Instantáneas de escena |
| Play Mode | Ejecuta la escena en el editor; los cambios se descartan al salir | ✅ Igual (Jugar/Pausa/Detener) |
| Build Settings | Compila para cada plataforma desde el editor | 🟡 Paquete del juego + plantilla Gradle (el APK se compila en PC) |

## 2. Scripting

| Unity | Cómo funciona | Titán |
|---|---|---|
| C# (Mono/IL2CPP) | Compilado; API muy amplia | 🟡 Lua 5.4 aislado, con límites de tiempo y memoria |
| Ciclo de vida (Awake, Start, Update, FixedUpdate, LateUpdate, OnDestroy, OnEnable/Disable) | Métodos llamados por el motor | ✅ onCreate, onStart, onUpdate, onFixedUpdate, onLateUpdate, onDestroy, onEnable/onDisable |
| OnCollisionEnter/Exit, OnTriggerEnter/Exit | Eventos de física | ✅ Desde v0.6 |
| Coroutines | Funciones que esperan | 🟡 Lua tiene corrutinas nativas; falta `wait(segundos)` integrado |
| Visual Scripting | Grafos de nodos | ❌ |
| Inspector de variables públicas ([SerializeField]) | Las variables del script se editan en el Inspector | ✅ Tabla `props` (números, booleanos, textos) |

## 3. Gráficos

| Unity (URP) | Cómo funciona | Titán |
|---|---|---|
| Render Pipeline (URP/HDRP) | Pipelines configurables, forward+ / deferred | 🟡 Forward propio en OpenGL ES 3.0 |
| Materiales Lit (PBR) | Metálico/rugosidad, mapas normal, oclusión, emisión | 🟡 Color, metálico, rugosidad, emisión, textura de color; ❌ mapas normal/ORM |
| Shader Graph | Editor de shaders por nodos | ❌ |
| Luces direccional, puntual, foco, área | Con sombras en tiempo real | 🟡 Direccional con sombras + 4 puntuales sin sombra; ❌ focos |
| Sombras en cascada | 1-4 cascadas | 🟡 1 cascada ajustada a la cámara |
| Iluminación global (lightmaps, light probes, APV) | Luz rebotada precalculada | ❌ |
| Skybox y niebla | Cielo procedural o cubemap | ✅ Cielo procedural, niebla exponencial |
| Post-procesado (Volume): bloom, tonemapping, color grading, SSAO, DOF | Pila de efectos | 🟡 Tonemapping ACES; ❌ resto (requiere HDR intermedio) |
| LOD, occlusion culling, GPU instancing, SRP Batcher | Optimización | 🟡 Lotes de sprites, culling por frustum (cámara y sombras); ❌ LOD, occlusion culling, instancing |
| Terreno (Terrain) | Mapa de alturas, pintura de capas, árboles, hierba | ❌ Prioridad alta para escenarios abiertos |
| Partículas (Shuriken / VFX Graph) | Emisores configurables | ❌ |

## 4. Física

| Unity | Cómo funciona | Titán |
|---|---|---|
| PhysX 3D: Rigidbody, colliders caja/esfera/cápsula/malla | Simulación a paso fijo | ✅ Jolt Physics (motor de nivel AAA): caja, esfera, cápsula, triggers; ❌ colliders de malla |
| Raycast, SphereCast, Overlap | Consultas | ✅ Raycast (con entidad ignorada); ❌ formas |
| Joints (bisagra, muelle, fijo) | Restricciones | ❌ |
| CharacterController | Movimiento de personajes con pendientes y escalones | ❌ (Jolt incluye CharacterVirtual, se expondrá) |
| Física 2D (Box2D) | Colliders 2D, efectores | ❌ (Box2D v3 previsto) |
| Capas y matriz de colisión | Qué capas chocan entre sí | 🟡 Estáticos/móviles; ❌ capas del usuario |

## 5. Animación, audio, UI, entrada

| Unity | Titán |
|---|---|
| Animator (máquinas de estados, blend trees), Timeline | 🟡 Esqueletos glTF, clips, Animator con velocidad y bucle, control desde scripts, transiciones suaves, hasta 256 huesos; ❌ máquinas de estados, Timeline |
| Importación de modelos FBX/glTF/OBJ con animaciones | 🟡 glTF/GLB con mallas, materiales, texturas, esqueletos y animaciones; ❌ FBX |
| Audio: AudioSource 2D/3D, mezclador | 🟡 AudioSource 2D/3D, bucles, volumen, tono, oyente en la cámara; ❌ mezclador con grupos y efectos |
| UI (uGUI / UI Toolkit): Canvas, botones, texto, layouts, anclajes | 🟡 Anclajes y estirado, imágenes, barras, texto TTF, botones, joystick; ❌ layouts automáticos, deslizadores, campos de texto, máscaras |
| Input System: acciones, mandos, táctil | 🟡 Táctil multipunto y joystick virtual; ❌ acciones, mandos |
| Navegación (NavMesh) | ❌ |
| Red (Netcode) | ❌ |

## 6. Herramientas

| Unity | Titán |
|---|---|
| Consola con pila de llamadas | ✅ Registro con filtros; errores de script con archivo, línea y traza |
| Profiler (CPU, GPU, memoria) | 🟡 FPS, ms por fase, draw calls, triángulos, memoria Lua, cuerpos físicos |
| Package Manager / Asset Store | ❌ |
| Gizmos y handles | ✅ Mover/rotar/escalar, colliders, cámaras, luces |
| Editor en el propio móvil | ✅ **Unity no lo tiene** |

## 7. Plan para alcanzar a Unity

En este orden, porque cada paso hace posibles juegos que antes no lo eran:

1. **Física** ✅ (v0.6)
2. **Variables de script editables en el Inspector** ✅ (v0.7)
3. **Prefabs** ✅ básicos (v0.7); faltan variantes y propagación
4. **Animación esquelética** ✅ (v0.8, transiciones y 256 huesos en v0.9); faltan máquinas de estados
5. **Audio** ✅ (v0.9)
6. **UI de juego** ✅ (v0.10): botones, imágenes, barras, texto TTF, joystick virtual
7. **Terreno** con pintura de capas y vegetación instanciada.
8. **Post-procesado** con HDR: bloom, SSAO, color grading.
9. **Partículas**.
10. **Controlador de personaje**, física 2D, joints.
11. Exportar el APK desde el propio teléfono.

Lo que Titán ya hace mejor que Unity: el editor completo funciona en el propio teléfono, el proyecto ocupa
unos pocos MB, y los formatos son JSON legibles y versionados.
