# Cómo compilar

## Escritorio (Linux): Core, pruebas, herramienta y reproductor

Requisitos: CMake ≥ 3.22, Ninja, un compilador C++17 (GCC 11+ o Clang 14+), `libglfw3-dev` y `libgles-dev`.

```bash
cmake -S . -B build/desktop -G Ninja -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build/desktop
```

```bash
./build/desktop/titan_tests
```

Resultado: `titan` (herramienta), `titan_player` (reproductor) y `titan_tests`.

## Crear y ejecutar un juego en escritorio

```bash
./build/desktop/titan demo mi_demo
```

```bash
./build/desktop/titan package mi_demo mi_demo_paquete
```

```bash
./build/desktop/titan_player mi_demo_paquete
```

Sin pantalla (servidores, CI), con captura:

```bash
TITAN_FRAMES=150 TITAN_SCREENSHOT=captura.png xvfb-run -a ./build/desktop/titan_player mi_demo_paquete
```

## Android (APK)

Requisitos: Android SDK con `platforms;android-34`, `build-tools;34.0.0`, `ndk;26.3.11579264` y `cmake;3.22.1`,
y JDK 17 o superior. Escribe la ruta del SDK en `android/local.properties` (`sdk.dir=...`) o en `ANDROID_HOME`.

1. Genera el paquete del juego (por defecto Gradle usa `samples/demo/package`):

```bash
./build/desktop/titan package samples/demo/project samples/demo/package
```

2. Compila:

```bash
cd android && ./gradlew assembleRelease
```

El APK queda en `android/app/build/outputs/apk/release/app-release.apk`.

Para otro juego:

```bash
cd android && ./gradlew assembleRelease -PtitanPackage=/ruta/paquete -PtitanAppId=com.estudio.juego -PtitanAppName="Mi juego" -PtitanVersionName=1.0 -PtitanVersionCode=1
```

`-PtitanAbis=arm64-v8a` limita las arquitecturas y acelera la compilación.

El APK se firma con la clave de depuración: sirve para instalarlo y probarlo, no para Google Play.

## Instalar en un teléfono

```bash
adb install -r android/app/build/outputs/apk/release/app-release.apk
```

```bash
adb logcat -s Titan
```

## Regenerar la fuente de depuración

```bash
python3 tools/make_assets.py
```

Necesita Pillow y la fuente DejaVu Sans Mono.
