#pragma once
// Lectura de archivos de solo lectura con rutas relativas a una raíz
// (el paquete del juego). En Android lee del APK; en escritorio, de carpetas.
#include <cstdint>
#include <map>
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

// Archivos en memoria (sonidos generados, pruebas, recursos creados por el propio motor).
class MemoryFileSource final : public FileSource {
public:
    void add(const std::string& path, std::vector<uint8_t> data) { files_[path] = std::move(data); }
    bool read(const std::string& path, std::vector<uint8_t>& out) override {
        auto it = files_.find(path);
        if (it == files_.end()) return false;
        out = it->second;
        return true;
    }

private:
    std::map<std::string, std::vector<uint8_t>> files_;
};

} // namespace titan
