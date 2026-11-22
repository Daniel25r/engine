# Registro de cambios

## 0.13.0 — 2026-11-22 · Gran actualización: render, materiales, partículas, controles, física, animación y terreno

Entregada de una vez (a petición del usuario), con nueve sistemas. Todo probado en PC; nada en un teléfono real.

Render
- Render target + posprocesado (`engine/src/render/PostProcess.*`): escala de resolución, FXAA (Lottes), bloom por
  "dual filtering" de 5 niveles, contraste, saturación, temperatura y viñeta. Sobre imagen LDR (RGBA8): funciona en
  cualquier GPU OpenGL ES 3.0. La UI y el texto se dibujan después, a resolución completa.
- Perfiles de calidad (`titan/core/Quality.h`): muy bajo, bajo, medio, alto, muy alto. Cambian de verdad la escala de
  render, las sombras (sí/no y 512–4096), FXAA, bloom y el número de luces. Perfil recomendado según capacidades
  medidas (versión de OpenGL ES, tamaño máximo de textura, memoria del sistema), nunca por el nombre del modelo.
  Menú Ventana › Calidad gráfica; `Quality.*` en Lua.
- Materiales v2: mapa normal (base tangente por píxel, sin tangentes precalculadas), metal/rugosidad (glTF),
  emisión, oclusión, repetición y desplazamiento, recorte alfa, doble cara; los modelos glTF usan ya todos sus mapas
  (antes solo el color) y `alphaMode: MASK`. Materiales compartidos `.mat` (Guardar como material en Detalles).
- Focos (luces spot) con apertura y borde suave; hasta 8 luces locales según el perfil.
- Sistema de partículas (`ParticleSystem`): simulación en CPU, cuadrados hacia la cámara, mezcla alfa o aditiva,
  hoja de animación, emisión por tiempo, por distancia y por ráfagas, culling. 7 efectos de partida.
- Terreno (`Terrain`): rejilla de alturas hasta 257×257, 4 capas pintadas, colisión, esculpido con el dedo (subir,
  bajar, suavizar, aplanar, pintar), generador de montañas, importación de mapas de alturas.

Juego
- Input Actions: teclado y mando (escritorio con GLFW; Android en el reproductor y en el editor) y táctil/UI con el
  mismo código: `Input.action`, `Input.axis`, `Input.define`. Acciones por defecto Mover, Mirar, Saltar, Atacar,
  Interactuar, Correr, Pausa.
- Física: controlador de personaje (Jolt CharacterVirtual: escalones, pendientes, salto), capas de colisión (16),
  sphereCast, overlapSphere, overlapBox, raycast con normal y máscara, uniones fija, bisagra y distancia/muelle.
- Animación: máquina de estados con parámetros, transiciones con condiciones y tiempo de salida, mezcla 1D (blend
  tree) sincronizada, eventos (`onAnimationEvent`); editor por formularios pensado para el tacto.
- Partida guardada: `Save.*` en JSON atómico (1 MB máximo), en el almacenamiento privado de la app.

Editor
- Crear › Efectos, Crear › Terreno, Crear › Física › Personaje, Crear › Luces › Foco.
- Enfocar selección (tecla F o Editar › Enfocar selección).
- La API de Lua se reparte en varios archivos (`ScriptInternal.h`, `ScriptApiWorld.cpp`); nuevos archivos del editor
  `EditorAnimation.cpp` y `EditorTerrain.cpp`.

Corregido
- `FieldReader::readRaw` no aceptaba listas JSON (encontrado por la prueba del terreno: los colores de las capas no
  se cargaban).

Pruebas: 87 automáticas (24 nuevas). Prueba de regresión de 1600 objetos: 950 draw calls con culling y 3206 sin él,
como en 0.11.

## 0.12.0 — 2026-11-22 · Jugar instantáneo

Cambiado
- El modo Jugar del editor usa el mismo `Renderer` y la misma caché de recursos que el editor
  (`titan/runtime/ResourceCache.h`, nueva):
  - ya no recompila los shaders ni vuelve a leer y subir texturas, modelos y fuentes;
  - cada recurso está una sola vez en la memoria de la GPU mientras se juega.
- El juego exportado sigue igual: su Runtime crea y cierra su propio Renderer.
- La caché de recursos era doble (una en el editor y otra en el runtime, con reglas distintas: el editor no respetaba
  la opción `mipmaps` del .meta). Ahora es una sola clase con una sola regla.
- El botón «Iluminado / Sin iluminación» de la vista ahora también funciona mientras se juega (antes no hacía nada en
  ese modo).
- `--play-at` y `--stop-at` del editor de escritorio admiten varios fotogramas (`--play-at 5,60`).

Añadido
- Registro «Jugar: primer fotograma en N ms».
- Auditoría: comparación técnica con Unity, Unreal y Godot, y tabla de mejoras realizadas.

Medida (PC, render por software): primer fotograma de Jugar con el proyecto de animación (2 modelos glTF, sonidos,
fuente) de 285–343 ms a 18–23 ms. La prueba de regresión de 1600 objetos no cambia (950 draw calls).

## 0.11.0 — 2026-11-22 · Auditoría y culling

Añadido
- `docs/AUDITORIA.md`: auditoría técnica completa (estado real de cada sistema, problemas con su causa en el código,
  madurez, arquitectura propuesta, hoja de ruta por dependencias). `docs/ROADMAP.md` reescrito a partir de ella.
- Culling por frustum (`titan/core/Bounds.h`: AABB, transformación exacta de cajas, frustum de Gribb-Hartmann):
  - pasada principal: solo se dibujan las mallas y sprites que tocan el campo de visión de la cámara;
  - sombras: solo los objetos dentro de la caja de la luz (lo de fuera tampoco aparecería en el mapa de sombras);
  - esqueletos: la pose solo se calcula para los personajes que se ven o dan sombra;
  - personajes animados: su caja se amplía un 50 % por lado porque la animación los saca de la pose de reposo.
- Estadísticas de visibilidad (`SceneRenderer::cullStats`): en pantalla durante el juego y en la vista del editor.
- Reproductor de escritorio: `TITAN_CULLING=0` (dibujar todo) y `TITAN_BENCH=1` (medias al terminar) para medir.
- 3 pruebas nuevas (cajas y frustum): 64 en total.

Corregido
- Los draw calls de la pasada de sombras no se contaban (se borraban al empezar la pasada principal). Ahora se suman
  y además se informa aparte (`RenderStats::shadowDrawCalls`).

Medida (PC, render por software llvmpipe, 1280×720, 1600 cubos alrededor de la cámara; no representa un teléfono):
draw calls de 3206 a 950, fotograma de 17,9–18,8 ms a 13,7–14,9 ms. Imagen idéntica píxel a píxel salvo el texto de
estadísticas.

## 0.10.0 — 2026-11-21 · Interfaz de juego

Añadido
- Módulo `titan_ui` (sin OpenGL): fuentes TrueType/OpenType convertidas en atlas de campo de distancia (SDF) con
  stb_truetype 1.28 (texto nítido a cualquier tamaño; ASCII, Latin-1 y Latin Extended-A: á é ñ ¿ ¡ ü ç €...),
  diseño de la interfaz con anclajes, pivote y estirado (como el RectTransform de Unity) escalado a una pantalla
  de referencia de 720 px de alto, y botones y joysticks virtuales con multitáctil.
- Componentes `UIRect`, `UIImage` (color o textura, esquinas redondeadas, relleno para barras), `UIText` (fuente del
  motor normal/negrita o una .ttf/.otf del proyecto, tamaño, alineación, sombra), `UIButton` y `UIJoystick`.
- Fuente del motor: Outfit (SIL OFL 1.1), normal y negrita. Sustituye a la fuente de mapa de bits solo ASCII:
  `Screen.text` y las estadísticas ya admiten acentos y eñe.
- Editor: menú Crear › Interfaz (Texto, Imagen, Panel, Botón, Barra de vida, Joystick); la interfaz se ve en la vista
  y se selecciona y arrastra con el dedo; Detalles con rejilla de anclajes de 9 posiciones y "Estirar";
  Ventana › Interfaz del juego para ocultarla.
- Lua: `onClick()`, `e:setText/getText`, `e:setFill/getFill`, `e:isPressed/wasClicked/setInteractable`,
  `e:getJoystick()`, `e:getUIPosition/setUIPosition/setUISize`; `setColor/getColor` sirven también para UIImage, UIText
  y MeshRenderer.
- Los dedos que empiezan sobre un botón o joystick no cuentan como toques del juego (`Input.touchCount`,
  `Input.touch`, `Input.tapped`).
- 8 pruebas nuevas (anclajes, jerarquía, botones, joystick, UTF-8, fuente, API Lua, toques de la UI): 61 en total.

Pendiente de comprobar: la interfaz en un teléfono real (probada en el editor de escritorio con toques simulados).

## 0.9.0 — 2026-11-21 · Sonido y animación mejorada

Añadido
- Audio con miniaudio 0.11.21 (módulo `titan_audio`): WAV, MP3 y FLAC decodificados a memoria, hasta 64 voces,
  volumen, tono, bucle, sonido 3D con atenuación por distancia (el oyente es la cámara activa), pausa con el juego.
- Componente `AudioSource` (sonido, volumen, tono, repetir, al empezar, 3D, distancias) en Detalles.
- Lua: `e:playSound([ruta, volumen])`, `e:stopSound()`, `Audio.play(nombre [, volumen])`, `Audio.playAt(nombre, x, y, z)`,
  `Audio.setVolume(v)`. Los sonidos se pueden pedir por ruta o solo por nombre ("disparo").
- Animación: hasta 256 huesos (matrices de huesos en una textura RGBA32F; personajes de Mixamo) y transiciones suaves
  entre clips (`Animator.fadeDuration`, 0,25 s por defecto).
- 2 pruebas de audio (mezcla real sin tarjeta de sonido).

Pendiente de comprobar: escuchar el sonido en un dispositivo real (el entorno de pruebas no tiene salida de audio).

## 0.8.0 — 2026-11-21 · Animación de personajes

Añadido
- Animación esquelética de modelos glTF/GLB: se leen nodos, esqueleto (hasta 48 huesos), pesos por vértice y clips
  (traslación, rotación y escala; interpolación lineal, escalonada y la parte central de la cúbica).
  Skinning en la GPU, también en el mapa de sombras.
- Componente `Animator` (clip, reproducir, repetir, velocidad). Vista previa animada en el editor, selector de clips
  con su duración y barra de tiempo para recorrer la animación. Al añadir un modelo con clips se le pone un Animator.
- Lua: `playAnimation(nombre [, repetir])`, `stopAnimation()`, `getAnimation()`, `setAnimationSpeed(v)`.

Probado con Fox (Survey/Walk/Run) y CesiumMan de Khronos. Las animaciones se crean en Blender o Mixamo y se importan en GLB.

## 0.7.0 — 2026-11-21 · Variables de script y prefabs

Añadido
- Variables de script editables en Detalles (como los campos públicos de Unity): el script declara
  `local Clase = { props = { velocidad = 5, activo = true, nombre = "x" } }` y cada entidad puede cambiar los
  valores (se muestran en azul; clic derecho vuelve al valor del script). En el juego llegan como `self.velocidad`.
- Prefabs (`.prefab`): "Guardar como prefab" en Detalles, "Añadir a la escena" en el navegador de contenido,
  `Scene.instantiate("Nombre", x, y, z)` en Lua (crea la copia con sus scripts y su física) y `titan prefab` en la CLI.
- `copySubtree`: una sola implementación para Duplicar y prefabs.

Corregido
- Crear instancias de script mientras se recorren (por ejemplo, instanciar en onUpdate) ya no puede invalidar
  referencias internas (se usa `std::deque`).

## 0.6.0 — 2026-11-21 · Física

Añadido
- Física 3D con Jolt Physics 5.2.0 detrás del módulo `titan_physics`: componentes `Rigidbody` (estático, dinámico,
  cinemático; masa, fricción, rebote, amortiguación, escala de gravedad, bloquear rotación) y `Collider` (caja,
  esfera, cápsula, centro, trigger). Paso fijo de 60 Hz, cinemáticos que siguen a su Transform.
- Scripts: `onCollisionEnter/Exit(otro)`, `onTriggerEnter/Exit(otro)`, `addForce`, `addImpulse`, `addTorque`,
  `setVelocity`, `getVelocity`, `teleport`, `Physics.raycast` (con entidad ignorada), `Physics.setGravity/getGravity`.
- Editor: paneles de Rigidbody y Collider, colliders dibujados en verde (triggers en cian), "Ajustar al objeto",
  menú Crear > Física. La plantilla 3D trae suelo sólido y un cubo que cae al pulsar Jugar.
- `docs/UNITY_COMPARISON.md`: comparación sistema por sistema con Unity y plan para alcanzarlo.
- 4 pruebas de física (caída y reposo, eventos de colisión, triggers, raycast, impulsos, gravedad).

## 0.5.0 — 2026-11-21 · Modelos 3D y modo horizontal

Añadido
- Importación de modelos glTF 2.0 (.glb, o .gltf con datos incrustados) con cgltf: mallas de todos los nodos,
  normales (o calculadas si faltan), UV, índices de 32 bits, materiales PBR (color, metálico, rugosidad, emisión,
  doble cara) y texturas incrustadas. Proyectan sombras. Campo `model` en `MeshRenderer`.
- Editor: "Añadir a la escena" desde el navegador de contenido (escala el modelo a ~2 m y lo apoya en el suelo),
  selección y caja de selección ajustadas a los límites del modelo, campo Modelo y número de triángulos en Detalles.
- La app del editor funciona solo en horizontal; interfaz más compacta en teléfonos y panel inferior plegado en
  pantallas bajas.

Probado con los modelos oficiales de Khronos Duck, BoxTextured y Fox.

## 0.4.0 — 2026-11-21 · Interfaz profesional y render PBR

Añadido
- Render 3D: materiales PBR (metálico/rugosidad, GGX), emisión, sombras del sol en tiempo real (mapa de 2048 con
  PCF, ajustado a la cámara y estable al moverla), cielo procedural con disco solar, luz ambiental de cielo,
  niebla exponencial, tonemapping ACES y corrección gamma, material de rejilla de prototipado alineada al mundo.
- Componente `Environment` (cielo, ambiente, niebla, exposición, sombras).
- Editor rediseñado: menú (Archivo, Editar, Crear, Ventana, Ayuda), barra con iconos (guardar, deshacer, Q/W/E/R,
  imán con valor, Jugar/Pausa/Detener), documentos en pestañas, Outliner en tabla con visibilidad y tipo, Detalles
  con Ubicación/Rotación/Escala en campos X/Y/Z de colores y botón de restablecer, navegador de contenido con
  carpetas y miniaturas, registro de salida, barra de estado, tema gris neutro.
- Gizmo de transformación en la vista: flechas para mover por eje, aros para rotar, asas para escalar, con imán.
- Vista: perspectiva, superior, frontal y derecha; modo sin iluminación; indicador de ejes; estadísticas de render.
- Pausa del juego en el editor.

Eliminado
- Las plantillas de juego "2D Plataformas" y "3D Exploración" y sus scripts. Quedan las plantillas vacías 2D y 3D
  (la 3D trae cielo, sol con sombras, niebla y suelo con rejilla).

## 0.3.0 — 2026-11-21 · Editor y 3D

Añadido
- **Editor de juegos dentro de la app** (Android y escritorio, mismo código): navegador de proyectos con plantillas,
  vista de escena con gestos táctiles, selección tocando, mover objetos arrastrando (con imán), jerarquía con
  arrastrar y soltar, inspector de todos los componentes, editor de código Lua con comprobación de sintaxis y
  atajos táctiles, recursos (importar, asignar, escenas), consola, deshacer/rehacer y botón Jugar/Detener que
  ejecuta el juego dentro del editor con el mismo Runtime que los juegos exportados.
- App Android del editor (`android/editor`, Kotlin + JNI): táctil multipunto, teclado en pantalla, teclado físico,
  portapapeles, importación con el selector del sistema y guardado automático al salir.
- 3D: `MeshRenderer` (cubo, esfera, plano, cilindro, cono, cápsula), `Light` (direccional y puntual),
  iluminación Blinn-Phong, búfer de profundidad, back-face culling, transparencias ordenadas.
- `SceneRenderer`: un único camino de render para el editor y el juego.
- Plantillas: 2D vacío, 3D vacío, 2D plataformas jugable, 3D exploración jugable.
- API de Lua: `Screen.text`. `Input.touchWorld` interseca el suelo (y = 0) con cámara en perspectiva.

Corregido
- `Screen.text` leía mal el color (lo detectó la prueba del editor en modo Play).

Cambiado
- Decisión D8: la interfaz del editor se dibuja en C++ con Dear ImGui en lugar de Kotlin (ver ARCHITECTURE.md).

## 0.2.0 — 2026-11-21 · Fundación de Titán

Reestructuración completa del prototipo "Forja 0.1" como motor modular.

Añadido
- `titan_core`: matemáticas 3D, registro con sinks, UUID, ECS con generaciones, bus de eventos,
  escenas con jerarquía y destrucción diferida, Transform con matrices cacheadas, serialización JSON versionada,
  guardado atómico con `.bak`, proyectos, base de datos de recursos con `.meta`, paquetes de juego.
- `titan_scripting`: Lua 5.4.7 aislado, con API Entity/Scene/Time/Input/Screen/Debug, ciclo de vida completo
  sin física y límites de tiempo y memoria.
- `titan_runtime`: bucle de paso fijo, cámara desde la escena, render de sprites con jerarquía, cambio de escena,
  perfilador en pantalla.
- Herramienta `titan` (CLI) y reproductor de escritorio con toques simulados y capturas.
- Plantilla de exportación Android parametrizable (`-PtitanPackage`, `-PtitanAppId`...).
- 43 pruebas automáticas y documentación técnica en `docs/`.

Cambiado
- El render ya no usa la clase `Engine` ni el `Game` en C++: ejecuta escenas de datos y scripts.
- Las coordenadas del mundo son unidades de mundo con cámara 3D (antes eran píxeles 2D).

Eliminado
- El ejemplo "enjambre" (`SwarmGame.cpp`). Era un juego escrito en C++ dentro del motor y ya no encaja en el modelo
  de proyecto + escenas + scripts. Su función de prueba de rendimiento la cubrirá una escena de estrés generada
  por la herramienta. Su código sigue en el historial de git (commit 88459e3).

## 0.1.0 — Forja

Primer prototipo: render 2D por lotes en OpenGL ES 3.0, ECS mínimo, multitáctil y APK con NativeActivity.
