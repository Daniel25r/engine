# Titán

Motor de videojuegos propio para Android, en C++17 sobre OpenGL ES 3.0, con scripts en Lua.
"Titán" es un nombre provisional; se cambia en `engine/include/titan/core/Version.h` y en `android/app/build.gradle.kts`.

**Estado: Generación 1 (Fundación), versión 0.2.0.** Funciona la cadena
proyecto → escena → entidades → componentes → render → scripts → ejecución → guardado,
probada en escritorio. El APK compila y se valida, pero todavía no se ha probado en un teléfono.
Detalle honesto en [docs/PROJECT_STATUS.md](docs/PROJECT_STATUS.md).

## En 1 minuto

```bash
cmake -S . -B build/desktop -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/desktop
./build/desktop/titan_tests
./build/desktop/titan demo mi_demo
./build/desktop/titan tree mi_demo
./build/desktop/titan set mi_demo Nave Transform.scale 2 2 1
./build/desktop/titan package mi_demo mi_paquete
./build/desktop/titan_player mi_paquete
```

## Documentación

| | |
|---|---|
| [PROJECT_STATUS](docs/PROJECT_STATUS.md) | Qué funciona y qué no, función por función |
| [ARCHITECTURE](docs/ARCHITECTURE.md) | Módulos, flujo del fotograma, decisiones |
| [ROADMAP](docs/ROADMAP.md) | Generaciones siguientes |
| [ENGINE_API](docs/ENGINE_API.md) | API de Lua, C++ y la herramienta `titan` |
| [FILE_FORMATS](docs/FILE_FORMATS.md) | Proyecto, escenas, .meta y paquetes |
| [ANDROID_PLATFORM](docs/ANDROID_PLATFORM.md) | Ciclo de vida, exportación, análisis de exportar desde el teléfono |
| [BUILD_INSTRUCTIONS](docs/BUILD_INSTRUCTIONS.md) | Compilar en escritorio y para Android |
| [TESTS](docs/TESTS.md) | Pruebas y resultados |
| [DEPENDENCIES](docs/DEPENDENCIES.md) | Bibliotecas, licencias y alternativas |
| [CHANGELOG](docs/CHANGELOG.md) | Cambios |

## Estructura

```
engine/include/titan/{core,render,scripting,runtime}   API pública
engine/src/{core,render,scripting,runtime}             implementación
engine/platform/{android,desktop}                      capa de plataforma
engine/assets/                                         recursos del motor (fuente de depuración)
third_party/{lua,nlohmann,stb}                         dependencias fijadas
tools/titan/                                           herramienta de línea de comandos
tests/                                                 pruebas
android/                                               plantilla de exportación (Gradle)
samples/demo/project/                                  proyecto de demostración
docs/                                                  documentación técnica
```
