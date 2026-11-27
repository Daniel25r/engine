#pragma once
// Importación de modelos en otros formatos. Todo se convierte a GLB, que el motor ya sabe cargar, dibujar, animar,
// usar en la física y exportar.
//   OBJ (+ MTL y texturas PNG/JPG)  — Prisma 3D, Blender, ZBrush, la mayoría de programas
//   STL (ASCII y binario)          — impresión 3D y CAD; se pasa de Z arriba a Y arriba
//   PLY (ASCII y binario)          — escáneres y fotogrametría
//   .blend                         — con el Blender instalado en el ordenador (se ejecuta aparte, como hace Godot)
#include <string>
#include <vector>

#include "titan/core/MeshBuild.h"

namespace titan {

struct ImportReport {
    std::vector<std::string> warnings; // cosas que no se pudieron traer (texturas TGA, colores por vértice...)
};

bool loadObj(const std::string& path, MeshBuild& out, ImportReport* report, std::string* error);
bool loadStl(const std::string& path, MeshBuild& out, ImportReport* report, std::string* error);
bool loadPly(const std::string& path, MeshBuild& out, ImportReport* report, std::string* error);

// true si la extensión es un formato que se convierte a GLB al importar (.obj, .stl, .ply, .blend).
bool isConvertibleModel(const std::string& path);
// Convierte el archivo a un GLB en dstGlb. Para .blend busca Blender: variable TITAN_BLENDER, el PATH o las
// carpetas habituales de instalación.
bool convertModelToGlb(const std::string& srcPath, const std::string& dstGlb, ImportReport* report, std::string* error);
// Ruta del ejecutable de Blender encontrado, o vacía.
std::string findBlender();

} // namespace titan
