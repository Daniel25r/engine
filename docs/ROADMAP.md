# Hoja de ruta

El orden sale de la auditoría (`docs/AUDITORIA.md`, sección 7): primero lo que necesitan los demás sistemas.
Cada paso se cierra con código, pruebas, documentación y un informe de lo que queda sin comprobar.

| Fase | Contenido | Estado |
|---|---|---|
| 0 | Auditoría completa | ✅ 0.11.0 (actualizada en 0.12.0) |
| 1 | Arquitectura y estabilidad: **culling** ✅; **recursos compartidos entre el editor y Play** ✅; sistema de tareas y carga asíncrona; deshacer por comandos | En curso |
| 2 | Render: **render targets, posproceso y perfiles de calidad** ✅; **materiales v2** ✅; **focos** ✅; HDR intermedio, sombras en cascada, instancing, LOD | En curso |
| 3 | Entrada: **Input Actions** (táctil, teclado, mando) ✅; reasignación desde un menú | Casi terminada |
| 4 | Física v2: **capas, personaje, consultas, joints** ✅; colliders de malla. Animación v2: **máquinas de estados, mezcla 1D, eventos** ✅; blend 2D, IK, root motion | En curso |
| 5 | Editor profesional: selección múltiple, bloqueo, capas y etiquetas, vista de juego, paso a paso en Play | Pendiente |
| 6 | Scripts: **guardado de partidas** ✅; `require`, depurador, autocompletado | En curso |
| 7 | Mundos: **terreno** ✅ y **partículas** ✅; vegetación instanciada, agua, ciclo día/noche | En curso |
| 8 | 2D completo: tilemaps, física 2D (Box2D), luces 2D | Pendiente |
| 9 | Audio v2: mezclador, grupos, efectos | Pendiente |
| 10 | Herramientas visuales: materiales por nodos, animación, partículas, programación visual | Pendiente |
| 11 | Exportación profesional (ajustes de compilación, APK reproducible desde PC) | Pendiente |
| 12 | Navegación e IA, red (prueba de dos jugadores), plugins | Pendiente |
| Siempre | **Pruebas en teléfonos reales** (gama baja, media y alta), con las condiciones de cada medida | Pendiente: requiere dispositivos |
