# Integración con Android

## Runtime del juego (lo que hay hoy)

- Actividad: `android.app.NativeActivity` (`android:hasCode="false"`): el juego exportado no lleva Java ni Kotlin.
  La biblioteca nativa es `libtitan_player.so` (`engine/platform/android/main_android.cpp` + `titan_runtime`).
- Requisitos: Android 7.0 (API 24) y OpenGL ES 3.0 (`<uses-feature android:glEsVersion="0x00030000">`).
- ABI: arm64-v8a, armeabi-v7a y x86_64 (la última para emuladores), configurables con `-PtitanAbis`.
- STL: `c++_static`. Excepciones activadas (Lua compilado como C++ las necesita).
- Archivos: el paquete del juego va dentro de `assets/` del APK y se lee con `AAssetManager`.
  Es de solo lectura; las partidas guardadas irán al almacenamiento privado de la app (pendiente).

### Ciclo de vida

| Evento de Android | Qué hace Titán |
|---|---|
| `APP_CMD_INIT_WINDOW` | Crea display y contexto EGL (ES 3) si no existen, y la superficie. La primera vez arranca el runtime |
| `APP_CMD_TERM_WINDOW` | Destruye solo la superficie. El contexto y las texturas se conservan |
| `APP_CMD_GAINED_FOCUS` | `Runtime::resume()`: reinicia el reloj para que no haya un `dt` gigante |
| `APP_CMD_LOST_FOCUS` | `Runtime::pause()`: cancela los toques activos y deja de dibujar; el hilo duerme hasta el siguiente evento |
| `eglSwapBuffers` → `EGL_CONTEXT_LOST` | `Renderer::onContextLost()`, nuevo contexto, `onContextRestored()`: recompila shaders y recarga texturas desde el APK |
| `destroyRequested` | `Runtime::shutdown()` (llama a `onDestroy` de los scripts) y libera EGL |

Con la app en segundo plano no se dibuja nada y el bucle se bloquea en `ALooper_pollOnce(-1)`, sin consumir CPU.
La pantalla se mantiene encendida mientras el juego está visible (`AWINDOW_FLAG_KEEP_SCREEN_ON`).

Rotación: la actividad declara `configChanges`, así que rotar no la recrea. El tamaño se consulta a EGL en cada fotograma.

Entrada: `AMotionEvent` → `Input` (DOWN/POINTER_DOWN, MOVE de todos los punteros, UP/POINTER_UP, CANCEL).

**Estado: compilado y validado como APK, pero sin probar en un dispositivo real.** Este entorno no tiene ni teléfono
ni emulador (no hay KVM). Hay que verificar en un teléfono: arranque, toques, pausa y reanudación, rotación,
y salir y volver tras mucho tiempo (pérdida de contexto).

## Exportación (lo que hay hoy)

Desde un ordenador con el SDK: `titan package` y luego `./gradlew assembleRelease -PtitanPackage=...`
(ver BUILD_INSTRUCTIONS.md). El APK se valida con `apksigner verify` y `aapt2 dump badging`.

Firma: hoy, con la clave de depuración. La firma de publicación leerá la ruta del keystore y las contraseñas
de variables de entorno o de `~/.gradle/gradle.properties`, nunca del repositorio (`.gitignore` excluye `*.jks` y `*.keystore`).

## Exportar desde el propio teléfono (análisis)

Compilar como hoy (Gradle + NDK + CMake) dentro de Android **no es viable de forma general**: Google no distribuye
el NDK para ARM ni para Android, Gradle necesita una JVM completa y varios GB, y Android no permite ejecutar
binarios descargados dentro del almacenamiento de la app a partir de `targetSdk` 29.

Plan técnicamente viable, sin compilar en el teléfono:
1. El editor lleva dentro un APK "runtime" ya compilado (este mismo `titan_player`, sin juego).
2. Al exportar, el editor genera el paquete del juego, lo añade a `assets/` del APK con un escritor ZIP,
   cambia el nombre de paquete y la etiqueta en el manifiesto binario, alinea el ZIP y firma con APK Signature
   Scheme v2/v3, todo en Kotlin o C++. Ninguna de estas operaciones necesita el NDK.
3. Para AAB o compilaciones con código nativo propio, servicio de compilación remoto opcional (nunca obligatorio).

Está previsto para la Generación 7 y hay que validarlo con un prototipo antes de comprometerlo.

## Editor (Generación 2, todavía no existe)

App Kotlin separada (módulo `editor/`) con `titan_core` vía JNI. Los proyectos irán en el almacenamiento privado
de la app; importar y exportar se hará con Storage Access Framework, sin pedir acceso libre al almacenamiento.
