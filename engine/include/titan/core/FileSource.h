#pragma once
// Lectura de archivos de solo lectura con rutas relativas a una raíz
// (el paquete del juego). En Android lee del APK; en escritorio, de carpetas.
#include <cstdint>
#include <string>
#include <vector>

namespace titan {

class FileSource {
public:
    virtual ~FileSource() = default;
    virtual bool read(const std::string& path, std::vector<uint8_t>& out) = 0;

    bool readText(const std::string& path, std::string& out) {
        std::vector<uint8_t> bytes;
        if (!read(path, bytes)) return false;
        out.assign(bytes.begin(), bytes.end());
        return true;
    }
};

} // namespace titan
