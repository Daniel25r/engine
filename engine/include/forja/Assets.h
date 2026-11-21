#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace forja {

// Acceso de solo lectura a los recursos del juego (texturas, fuentes, datos).
// En Android lee del APK; en escritorio, de las carpetas de assets.
class Assets {
public:
    virtual ~Assets() = default;
    virtual bool read(const std::string& path, std::vector<uint8_t>& out) = 0;
};

} // namespace forja
