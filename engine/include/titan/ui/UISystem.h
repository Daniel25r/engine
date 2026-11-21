#pragma once
// Interfaz de juego: calcula el rectángulo en pantalla de cada entidad con UIRect y gestiona
// botones y joysticks virtuales con la entrada táctil. No dibuja (eso lo hace UIRenderer, en runtime).
#include <vector>

#include "titan/core/Input.h"
#include "titan/core/Scene.h"

namespace titan {

struct UIItem {
    Entity entity;
    Rect rect; // píxeles, origen arriba a la izquierda
};

class UISystem {
public:
    static constexpr float kReferenceHeight = 720.0f;
    // Las medidas de la UI están pensadas para 720 px de alto; en un móvil de 1080 se multiplican por 1,5.
    static float scaleFor(Vec2 screen) { return screen.y / kReferenceHeight; }
    static Rect computeRect(const UIRect& r, const Rect& parent, float scale);

    // Rectángulos de todas las entidades UI activas, en orden de dibujo (padres antes que hijos).
    void layout(const Scene& scene, Vec2 screen);
    const std::vector<UIItem>& items() const { return items_; }
    float scale() const { return scale_; }
    bool rectOf(Entity e, Rect& out) const;

    // Botones y joysticks. Los dedos que empiezan sobre ellos quedan marcados en Input como "de la UI".
    // clicked recibe los botones que se han pulsado y soltado encima en este fotograma.
    void update(Scene& scene, Input& input, std::vector<Entity>* clicked = nullptr);

    // Entidad UI más alta bajo el punto (la última dibujada). interactiveOnly: solo botones y joysticks.
    Entity pick(const Scene& scene, Vec2 p, bool interactiveOnly) const;

private:
    std::vector<UIItem> items_;
    float scale_ = 1;
};

} // namespace titan
