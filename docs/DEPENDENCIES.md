# Dependencias

Regla: solo bibliotecas especializadas de bajo nivel, nunca un motor completo. Todas van incluidas
en `third_party/` con su versión fijada, así la compilación no depende de la red.

## En uso

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
| stb_truetype | Fuentes TTF | Dominio público/MIT | Rasteriza glifos a un atlas propio |

Cada una se integrará detrás de una interfaz de Titán (`PhysicsWorld2D`, `AudioDevice`...) para poder cambiarla sin tocar los juegos.
