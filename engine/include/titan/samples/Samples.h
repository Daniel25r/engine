#pragma once
// Proyectos de ejemplo completos (juegos que funcionan), para aprender y como punto de partida.
#include <string>

namespace titan {

// "Isla Estelar": plataformas 3D con menú, nivel (enemigos con IA, monedas, plataformas móviles, peligros, puntos
// de control, vidas), victoria, música y efectos con mezclador y récord guardado. Todo hecho con primitivas, Lua y
// sonidos sintetizados (sin recursos de terceros). Crea la carpeta dir; false y error si algo falla.
bool createPlatformerSample(const std::string& dir, const std::string& name, std::string* error = nullptr);

} // namespace titan
