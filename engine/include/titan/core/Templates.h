#pragma once
// Plantillas de proyecto: puntos de partida vacíos que el usuario modifica libremente.
#include <memory>
#include <string>

#include "titan/core/Project.h"

namespace titan {

enum class ProjectTemplate {
    Empty2D, // cámara ortográfica
    Empty3D, // nivel inicial: cámara, sol con sombras, cielo/niebla, suelo y un cubo de referencia
};

std::unique_ptr<Project> createProjectFromTemplate(const std::string& dir, const std::string& name, ProjectTemplate t,
                                                   std::string* error = nullptr);

// Código inicial de un script nuevo.
std::string newScriptSource(const std::string& className);

} // namespace titan
