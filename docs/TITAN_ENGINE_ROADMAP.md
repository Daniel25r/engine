# TITÁN ENGINE — Hoja de ruta

Ordenada por dependencias técnicas (las 7 etapas de la especificación). Cada bloque se cierra con código conectado al
motor, pruebas que miden el resultado, documentación y una línea en el registro de `TITAN_ENGINE_STATUS.md`.
Lo que no tiene prueba no se marca como hecho. Estado detallado: `docs/TITAN_ENGINE_STATUS.md`.

Leyenda: ✅ hecho y probado · ▶ siguiente · ○ pendiente

## Etapa 1 — Arquitectura central y audio

| Bloque | Estado | Criterio de "terminado" |
|---|---|---|
| Módulos, ECS, escenas, eventos, serialización, recursos con UUID | ✅ | Pruebas existentes |
| Mezclador de audio con buses y efectos (portados de Godot) + editor + Lua | ✅ | 9 pruebas de señal |
| Sistema de trabajos (Job System) usado por las partículas | ✅ | 6 pruebas + ThreadSanitizer; 2,9× en partículas |
| **Carga asíncrona de recursos (texturas, modelos, sonidos) sobre el Job System** | ▶ | Escena grande carga sin bloquear el hilo principal; medido |
| Asignadores (por fotograma, pools) donde el perfilado lo justifique | ○ | Menos asignaciones por fotograma, medido |
| Informe de fallos nativos (señales en Android/Linux) al registro | ○ | Fallo provocado queda registrado |
| Medir el coste de CPU de los efectos de audio | ○ | Tabla en TESTS |

## Etapa 2 — Escenas y editor

| Bloque | Estado |
|---|---|
| Selección múltiple, bloquear/ocultar, etiquetas y grupos | ○ |
| Deshacer por comandos (en vez de instantáneas) | ○ |
| Escenas anidadas (prefabs dentro de prefabs con cambios locales) | ○ |
| Importar OBJ y OGG; compresión ASTC/ETC2 al exportar | ○ |

## Etapa 3 — Render

| Bloque | Estado |
|---|---|
| Búfer HDR intermedio (RGBA16F donde haya `EXT_color_buffer_half_float`, RGB10A2 si no) + exposición | ○ |
| Sombras en cascada (2–4) del sol; sombras de focos | ○ |
| IBL: mapa de entorno con irradiancia y especular prefiltrado | ○ |
| SSAO (perfiles alto y muy alto) | ○ |
| LOD por distancia (manual y generado) | ○ |
| Capa RHI y backend Vulkan | ○ — solo cuando el render GLES esté completo; es un proyecto grande |
| Editor de materiales por nodos que genera GLSL, con caché | ○ |

## Etapa 4 — Física, animación, partículas, UI, scripting

| Bloque | Estado |
|---|---|
| Física 2D (evaluar Box2D v3, MIT) | ○ |
| Vehículos y soft body de Jolt | ○ |
| IK de dos huesos, mezcla 2D, root motion | ○ |
| UI: slider, casilla, scroll, layouts | ○ |
| Depurador de Lua (puntos de parada) y recarga de scripts durante el juego | ○ |
| Programación visual que se ejecuta en el runtime | ○ |

## Etapa 5 — Mundos, IA, cinemáticas

Vegetación instanciada, agua, ciclo día/noche, árboles de comportamiento, evitación entre agentes, secuenciador. ○

## Etapa 6 — Red y optimización avanzada

Red cliente/servidor con replicación (evaluar ENet o GameNetworkingSockets), streaming de mundo, oclusión. ○

## Etapa 7 — Plataformas y exportación

| Bloque | Estado |
|---|---|
| **Pruebas en teléfonos reales** (gama baja, media, alta) | ○ — requiere dispositivos; procedimiento en `VALIDACION_ANDROID.md` |
| Windows: compilación en GitHub Actions (CMake + vcpkg + ANGLE) | ○ — flujo escrito en local, sin subir ni ejecutar |
| Exportar desde el editor de Android sin PC | ○ |
