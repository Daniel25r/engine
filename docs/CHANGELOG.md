# Registro de cambios

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
