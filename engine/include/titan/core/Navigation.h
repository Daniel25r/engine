#pragma once
// Navegación para personajes no jugables. Es una rejilla de navegación horneada contra la física (no un NavMesh
// poligonal): cada celda es caminable si un rayo encuentra suelo con poca pendiente y cabe un personaje de pie.
// El camino se busca con A* en 8 direcciones y se suaviza quitando los puntos que se pueden saltar en línea recta.
#include <cstdint>
#include <vector>

#include "titan/core/PhysicsApi.h"
#include "titan/core/Scene.h"

namespace titan {

struct NavSettings {
    Vec3 center;               // centro de la zona a hornear
    Vec2 size{60, 60};         // lado en X y Z (metros)
    float cellSize = 0.5f;
    float agentRadius = 0.4f;
    float agentHeight = 1.8f;
    float maxSlope = 45.0f;    // grados
    float maxStep = 0.45f;     // diferencia de altura entre celdas vecinas que se puede subir
    float topY = 50.0f;        // desde dónde se lanzan los rayos hacia abajo
    float bottomY = -20.0f;
    uint32_t layerMask = 0xFFFF;
};

class NavGrid {
public:
    // Hornea la rejilla con rayos y comprobaciones de espacio libre. ignore: el agente (no es un obstáculo).
    void bake(const PhysicsApi& physics, const NavSettings& s, const std::vector<Entity>& ignore = {});
    bool baked() const { return !walkable_.empty(); }
    int width() const { return w_; }
    int depth() const { return d_; }
    int walkableCells() const;

    // Camino de "from" a "to" en el mundo (puntos sobre el suelo). false si no hay camino.
    bool findPath(const Vec3& from, const Vec3& to, std::vector<Vec3>& path) const;
    // Celda caminable más cercana a un punto (radio en celdas). false si no hay ninguna.
    bool nearestWalkable(const Vec3& p, Vec3& out, int searchRadius = 6) const;
    bool isWalkable(const Vec3& p) const;

    // Para pruebas y para construir la rejilla a mano.
    void setGrid(const NavSettings& s, int w, int d, std::vector<uint8_t> walkable, std::vector<float> heights);

private:
    bool cellOf(const Vec3& p, int& x, int& z) const;
    Vec3 cellCenter(int x, int z) const;
    bool lineWalkable(int x0, int z0, int x1, int z1) const;
    NavSettings s_;
    int w_ = 0, d_ = 0;
    std::vector<uint8_t> walkable_;
    std::vector<float> height_;
};

// Mueve a los NavAgent por sus caminos (con su CharacterController si lo tienen; si no, su Transform).
void updateNavAgents(Scene& scene, const NavGrid& grid, float dt);
// Zona que cubre todo lo que tiene colisión en la escena (para hornear automáticamente).
NavSettings navSettingsForScene(Scene& scene, float cellSize = 0.6f);

} // namespace titan
