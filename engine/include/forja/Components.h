#pragma once
#include "forja/Math.h"
#include "forja/Renderer.h"

namespace forja {

// Componentes que el motor entiende. Tus juegos pueden definir los suyos
// (cualquier struct sirve como componente).

struct Transform {
    Vec2 position;
    Vec2 scale{1.0f, 1.0f};
    float rotation = 0.0f; // radianes
};

// Toda entidad con Transform + Sprite se dibuja automáticamente.
struct Sprite {
    TextureId texture = kWhiteTexture;
    Vec2 size{64.0f, 64.0f};   // en unidades de mundo
    Color color;               // se multiplica con la textura
    int layer = 0;             // capas mayores se dibujan encima
    Rect uv{0.0f, 0.0f, 1.0f, 1.0f};
    Vec2 pivot{0.5f, 0.5f};
};

} // namespace forja
