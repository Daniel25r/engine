# Dependencias

Regla: solo bibliotecas especializadas de bajo nivel, nunca un motor completo. Todas van incluidas
en `third_party/` con su versión fijada, así la compilación no depende de la red.

## En uso

### meshoptimizer 0.22

| | |
|---|---|
| Función | Simplificar mallas al cargar modelos (niveles de detalle automáticos) |
| Licencia | MIT (`third_party/meshoptimizer/LICENSE.md`) |
| Origen | `github.com/zeux/meshoptimizer`, etiqueta `v0.22` (solo `simplifier.cpp`, `allocator.cpp` y `meshoptimizer.h`) |
| Coste | ~80 KB de código; la simplificación se hace una vez al cargar cada modelo |

### ufbx 0.20.0

| | |
|---|---|
| Función | Leer FBX (Mixamo, Maya, 3ds Max, Prisma 3D) al importar; se convierte a GLB |
| Licencia | MIT o dominio público, a elegir (`third_party/ufbx/LICENSE`) |
| Origen | `github.com/ufbx/ufbx`, etiqueta `v0.20.0` (`ufbx.c`, `ufbx.h`) |
| Coste | ~1,2 MB de código fuente C; solo se usa al importar (no en los juegos exportados) |
| Alternativas | Autodesk FBX SDK (propietario, no redistribuible libremente), Assimp (más grande, BSD) |


### Lua 5.4.7
| | |
|---|---|
| Función | Lenguaje de scripting de los juegos (`titan_scripting`) |
| Licencia | MIT (texto al final de `third_party/lua/lua.h`) |
| Origen | Espejo oficial `github.com/lua/lua`, etiqueta `v5.4.7` (lua.org no era accesible desde el entorno de compilación) |
| Android | Sí. C puro, sin dependencias. Compilado aquí como C++ |
| Arquitecturas | Todas (arm64-v8a, armeabi-v7a, x86_64 comprobadas al compilar el APK) |
| Coste | ~600 KB de código máquina por ABI. Intérprete sin JIT: más lento que C++ en cálculo intensivo |
| Limitaciones | Sin tipos estáticos; los errores aparecen al ejecutar |
| Alternativas | LuaJIT (más rápido, pero estancado en 5.1 y con JIT restringido en algunos Android), QuickJS (JavaScript, más pesado), Wren, C# con Mono (mucho más pesado) |
| Motivo | Pequeño, estable, muy usado en juegos y fácil de aislar (memoria, tiempo, bibliotecas) |
| Archivos | 32 `.c` y 27 `.h` del núcleo y las bibliotecas estándar; sin `lua.c` ni `luac.c` |

### nlohmann/json 3.11.3
| | |
|---|---|
| Función | Leer y escribir los formatos JSON (escenas, proyectos, `.meta`, índice de recursos) |
| Licencia | MIT |
| Origen | `single_include/nlohmann/json.hpp`, etiqueta `v3.11.3`, SHA-256 `9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6` |
| Android | Sí (solo cabecera, C++11) |
| Coste | Aumenta el tiempo de compilación de los archivos que lo incluyen; usa memoria dinámica por nodo |
| Limitaciones | No apto para formatos binarios grandes; para eso habrá un formato "cocinado" propio |
| Alternativas | RapidJSON (más rápido, API más incómoda), un parser propio (más código que mantener) |
| Motivo | Muy probado; detecta errores de sintaxis; se usa con `parse(..., allow_exceptions=false)` |

### stb_image 2.34 y stb_image_write 1.19
| | |
|---|---|
| Función | Decodificar PNG y JPEG (render) y escribir PNG (capturas y herramienta) |
| Licencia | Dominio público o MIT, a elegir |
| Origen | `github.com/nothings/stb`, rama `master` (2026-11). SHA-256: stb_image.h `dfc2aadda6295f67f677587773f25e5bf8c8ad427ed0db7c518525c91e72e020`, stb_image_write.h `5d89edc8bffd848f3e138b2a1dbec67aadbed591894bde16ade7c83dc233af9b` |
| Android | Sí |
| Limitaciones | Sin WebP ni compresión de GPU (ETC2/ASTC); con stb_image solo se compilan PNG y JPEG |
| Alternativas | libpng + libjpeg-turbo (más rápidas, más pesadas), `AImageDecoder` (solo Android 11+) |

### stb_truetype 1.28
| | |
|---|---|
| Función | Leer fuentes TTF/OTF y generar los campos de distancia (SDF) de cada letra para el texto de los juegos |
| Licencia | Dominio público o MIT, a elegir |
| Origen | `github.com/nothings/stb`, rama `master` (2026-11). SHA-256 de stb_truetype.h `428cfe93520f2aed5e413a220b0db5bba5b17e2ebe5a64e4a8efa9eeeb67ec5f` |
| Android | Sí |
| Limitaciones | Sin "shaping" (no sirve para árabe, hindi...); el interletraje solo usa la tabla `kern` clásica |
| Alternativas | FreeType + HarfBuzz (completos, mucho más pesados), msdfgen (bordes aún más nítidos) |

### Fuente Outfit (normal y negrita)
| | |
|---|---|
| Función | Fuente por defecto del texto de los juegos (`engine/assets/engine/fonts/`) |
| Licencia | SIL Open Font License 1.1 (`engine/assets/engine/fonts/OFL.txt`): se puede incluir en juegos comerciales |
| Origen | Proyecto Outfit (github.com/Outfitio/Outfit-Fonts), 55 KB cada archivo |

### Dear ImGui 1.91.5
| | |
|---|---|
| Función | Interfaz del editor (no se usa en los juegos exportados) |
| Licencia | MIT (`third_party/imgui/LICENSE.txt`) |
| Origen | `github.com/ocornut/imgui`, etiqueta `v1.91.5`; backend `imgui_impl_opengl3` en modo ES 3 |
| Android | Sí (C++ y GLES 3). Desplazamiento táctil, teclado y portapapeles los implementa Titán |
| Coste | ~1 MB de código máquina por ABI en el APK del editor |
| Limitaciones | Pensada para ratón: Titán añade desplazamiento por arrastre, escalado por densidad y controles más grandes |
| Alternativas | Interfaz nativa Kotlin/Compose (no se puede probar sin dispositivo y duplica el editor), Nuklear, RmlUi |
| Motivo | Un solo editor para todas las plataformas, comprobable automáticamente |

### cgltf 1.14
| | |
|---|---|
| Función | Leer modelos glTF 2.0 / GLB |
| Licencia | MIT |
| Origen | `github.com/jkuhlmann/cgltf`, etiqueta `v1.14`, un solo archivo `third_party/cgltf/cgltf.h` |
| Android | Sí (C99) |
| Limitaciones | Titán aún no usa animaciones, esqueletos, morph targets ni texturas normal/metálico; los .gltf con archivos externos se rechazan (usar .glb) |
| Alternativas | Assimp (muy pesado, muchos formatos), tinygltf (C++, depende de json y stb) |
| Motivo | Pequeño, sin dependencias, muy usado |

### Jolt Physics 5.2.0
| | |
|---|---|
| Función | Física 3D (cuerpos rígidos, colisiones, sensores, consultas) |
| Licencia | MIT (`third_party/jolt/LICENSE`) |
| Origen | `github.com/jrouwe/JoltPhysics`, etiqueta `v5.2.0` (commit a63aa3b8e24cf95f3fab2613f9a3015b164ef62c), solo la carpeta `Jolt/` |
| Android | Sí: compila para ARM64/ARMv7 (NEON) y x86_64; se usa con 1-3 hilos |
| Coste | ~2 MB de código máquina por ABI; compilación lenta (138 archivos) |
| Limitaciones | Titán aún no expone colliders de malla, joints ni CharacterVirtual |
| Alternativas | Bullet (más antiguo y lento), PhysX (más pesado, licencia BSD-3), Box2D (solo 2D) |
| Motivo | Usado en juegos AAA (Horizon Forbidden West), multihilo, estable, licencia MIT |

### miniaudio 0.11.21
| | |
|---|---|
| Función | Salida de audio, decodificación (WAV, MP3, FLAC), mezcla y sonido 3D |
| Licencia | Dominio público o MIT-0, a elegir |
| Origen | `github.com/mackron/miniaudio`, etiqueta `0.11.21`, un solo archivo |
| Android | Sí: AAudio (Android 8+) u OpenSL ES, cargados en tiempo de ejecución |
| Limitaciones | OGG no está activado (se añadiría stb_vorbis); sin efectos (reverberación, filtros) en Titán todavía |
| Alternativas | Oboe (solo Android, solo salida), OpenAL Soft (LGPL), FMOD/Wwise (comerciales) |
| Motivo | Un archivo, sin dependencias, multiplataforma, licencia libre |

### Código adaptado de Godot Engine (efectos de audio)
| | |
|---|---|
| Función | Procesado de señal de los efectos del mezclador: reverberación, eco, filtros biquad, compresor, limitador y distorsión |
| Licencia | MIT. Copyright (c) 2014-present Godot Engine contributors; Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur. Texto completo abajo |
| Origen | `github.com/godotengine/godot` (rama `master`, versión 4.8-rc, copia de `github.com/Daniel25r/godot`), archivos `servers/audio/effects/reverb_filter.cpp`, `audio_effect_reverb.cpp`, `audio_effect_delay.cpp`, `audio_effect_filter.cpp`, `audio_effect_compressor.cpp`, `audio_effect_hard_limiter.cpp`, `audio_effect_distortion.cpp` y `servers/audio/audio_filter_sw.cpp` |
| Dónde está | `engine/src/audio/AudioEffects.cpp` (la cabecera `AudioEffects.h` lleva el aviso de copyright) |
| Qué se cambió | Sin `AudioServer`, `Ref<>` ni `ClassDB`: cada efecto es una clase con parámetros por nombre y rango; búferes estéreo intercalados en lugar de `AudioFrame`; `std::vector` en lugar de `memnew_arr`; frecuencia de muestreo pasada al crear el efecto. El cálculo de cada muestra es el del original. El filtro solo porta los modos paso bajo, paso alto y paso banda. El compresor no porta la entrada lateral (sidechain) |
| Qué NO es de Godot | Los buses (`AudioEngine.cpp`, nodos de miniaudio), `AudioBus.cpp` (formato `audio_buses.json`), el mezclador del editor y la API de Lua |
| Android | Sí: C++17 sin dependencias |
| Coste | Solo cuando un bus tiene efectos. La reverberación usa 8 filtros peine + 4 pasa-todo por canal. Coste de CPU aún sin medir (pendiente: benchmark de efectos en PC y teléfono) |
| Pruebas | `tests/test_audio_mixer.cpp`: miden la señal (atenuación de los filtros, cola de reverberación, tiempo y nivel del eco, techo del limitador, compresión y armónicos de la distorsión) |

Licencia de Godot Engine (MIT):

```
Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md).
Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

### Bibliotecas del sistema
- Android: `libandroid`, `libEGL`, `libGLESv3`, `liblog` (NDK r26.3, API mínima 24) y `native_app_glue` del NDK (Apache 2.0).
- Escritorio: GLFW 3.3 (zlib) y Mesa `libGLESv2` (MIT). Solo para desarrollo y pruebas; no van en el APK.

## Evaluadas para próximas generaciones (todavía no incluidas)

| Biblioteca | Para | Licencia | Por qué es candidata |
|---|---|---|---|
| Box2D v3 | Física 2D | MIT | Estándar del sector, C, determinista por plataforma, eventos de contacto y sensores |
| Jolt Physics | Física 3D | MIT | Moderna, multihilo, usada en producción, compila para ARM64 |
| miniaudio | Audio | MIT-0 o dominio público | Un archivo; usa AAudio y OpenSL ES en Android; decodifica WAV, MP3 y FLAC. OGG mediante stb_vorbis |
| cgltf | glTF/GLB | MIT | Parser en C de un archivo; no impone estructuras de motor |

Cada una se integrará detrás de una interfaz de Titán (`PhysicsWorld2D`, `AudioDevice`...) para poder cambiarla sin tocar los juegos.
