# Registro de cambios

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
