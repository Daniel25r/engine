# Forja

Motor de videojuegos 2D nativo para Android, escrito en C++17 sobre OpenGL ES 3.0.

## Qué tiene (versión 0.1)

| Parte | Archivo | Qué hace |
|---|---|---|
| Bucle de juego | `engine/src/Engine.cpp` | Lógica a paso fijo de 60 Hz, render a la velocidad de la pantalla, FPS |
| ECS | `engine/include/forja/World.h` | Entidades con generación + componentes en arreglos densos (sparse sets) |
| Render | `engine/src/Renderer.cpp` | Sprites por lotes (hasta 8192 por llamada), texturas PNG con mipmaps, cámara 2D, texto de depuración |
| Entrada | `engine/include/forja/Input.h` | Multitáctil: eventos (empieza, se mueve, termina) y dedos activos |
| Android | `engine/platform/android/main_android.cpp` | NativeActivity, EGL, ciclo de vida, pérdida de contexto, assets del APK |
| Escritorio | `engine/platform/desktop/main_desktop.cpp` | Mismo OpenGL ES con GLFW, para desarrollar y probar sin el móvil |
| Ejemplo | `sample/src/SwarmGame.cpp` | Un enjambre de partículas que tus dedos atraen |

## Hacer un juego

Un juego es una clase que hereda de `forja::Game` más la función `createGame()`:

```cpp
#include "forja/Engine.h"
using namespace forja;

struct Velocity { Vec2 v; };            // cualquier struct es un componente

class MyGame : public Game {
    void onStart(Engine& e) override {
        TextureId tex = e.renderer().loadTexture("textures/orb.png");
        Entity ball = e.world().create();
        e.world().add<Transform>(ball, {{0, 0}});
        e.world().add<Sprite>(ball, {tex, {64, 64}});
        e.world().add<Velocity>(ball, {{100, 0}});
    }
    void onUpdate(Engine& e, float dt) override {        // 60 veces por segundo
        e.world().each<Velocity, Transform>([&](Entity, Velocity& vel, Transform& t) {
            t.position += vel.v * dt;
        });
    }
};

std::unique_ptr<Game> forja::createGame() { return std::make_unique<MyGame>(); }
```

Toda entidad con `Transform` + `Sprite` se dibuja sola. El mundo usa Y hacia arriba, y el centro de la pantalla es (0, 0).
`camera().viewSize` dice cuántas unidades caben en el lado corto de la pantalla (720 por defecto).

Para usar tu juego en lugar del ejemplo, cambia `GAME_SOURCES` en `CMakeLists.txt`.

## Compilar

**APK de Android** (necesita el SDK de Android con NDK 26.3 y CMake 3.22.1, o Android Studio):

```bash
cd android
./gradlew assembleRelease
```

El APK queda en `android/app/build/outputs/apk/release/app-release.apk`.
También puedes abrir la carpeta `android/` con Android Studio.

**Escritorio** (Linux, necesita `libglfw3-dev` y `libgles-dev`):

```bash
cmake -S . -B build/desktop -G Ninja
cmake --build build/desktop
./build/desktop/forja_desktop
```

El ratón hace de dedo. Para pruebas automáticas: `FORJA_FRAMES=180 FORJA_SCREENSHOT=captura.png ./build/desktop/forja_desktop`.

**Recursos**: `python3 tools/make_assets.py` regenera la fuente de depuración y la textura del ejemplo.

## Límites actuales

- Solo 2D. Todavía no hay físicas con colisiones entre cuerpos, ni sonido, ni escenas guardadas en archivo, ni editor visual.
- El APK va firmado con la clave de depuración. Para Google Play hace falta una clave propia.
