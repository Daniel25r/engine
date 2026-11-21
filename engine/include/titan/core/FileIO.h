#pragma once
#include <string>

namespace titan::fileio {

bool readText(const std::string& path, std::string& out);

// Guardado atómico: escribe en "<ruta>.tmp", lo vuelca a disco y lo renombra encima del original.
// Si se corta la luz a mitad, queda el archivo antiguo o el nuevo, nunca uno a medias.
// Con keepBackup, la versión anterior se conserva como "<ruta>.bak".
bool writeAtomic(const std::string& path, const std::string& data, bool keepBackup = true, std::string* error = nullptr);

} // namespace titan::fileio
