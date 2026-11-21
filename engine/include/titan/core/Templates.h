#pragma once
// Plantillas de proyecto. Son proyectos reales (escenas + scripts) que el usuario puede
// modificar libremente: no hay nada "cerrado" en ellas.
#include <memory>
#include <string>

#include "titan/core/Project.h"

namespace titan {

enum class ProjectTemplate {
    Empty2D,     // cámara ortográfica
    Empty3D,     // cámara en perspectiva, luz y suelo
    Platformer2D, // jugador que corre y salta y recoge monedas
    Explorer3D,  // jugador que va donde tocas y recoge gemas; la cámara le sigue
};

std::unique_ptr<Project> createProjectFromTemplate(const std::string& dir, const std::string& name, ProjectTemplate t,
                                                   std::string* error = nullptr);

// Código inicial de un script nuevo.
std::string newScriptSource(const std::string& className);

} // namespace titan
