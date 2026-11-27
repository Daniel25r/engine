#pragma once
// Carpeta de recursos del motor (fuentes, sombreadores, iconos) en escritorio. Orden de búsqueda:
//   1. la variable de entorno TITAN_ENGINE_ASSETS;
//   2. "engine_assets" junto al ejecutable (así se distribuye en Windows: se puede mover la carpeta a otro PC);
//   3. la ruta del código fuente con la que se compiló (para desarrollar sin copiar nada).
#include <cstdlib>
#include <filesystem>
#include <string>

namespace titan::desktop {

inline std::string engineAssetsDir(const char* argv0, const char* compiledPath) {
    if (const char* env = std::getenv("TITAN_ENGINE_ASSETS"); env && *env) return env;
    std::error_code ec;
    const std::filesystem::path exe = std::filesystem::absolute(argv0 ? argv0 : "", ec);
    if (!ec) {
        const std::filesystem::path next = exe.parent_path() / "engine_assets";
        if (std::filesystem::is_directory(next, ec)) return next.generic_string();
    }
    return compiledPath;
}

} // namespace titan::desktop
