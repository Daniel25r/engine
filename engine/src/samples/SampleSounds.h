#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace titan::samples {

// Nombre (sin extensión) y contenido WAV de cada sonido de la demo de plataformas.
std::vector<std::pair<std::string, std::vector<uint8_t>>> platformerSounds();

} // namespace titan::samples
