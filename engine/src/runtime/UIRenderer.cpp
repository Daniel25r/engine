#include "titan/runtime/UIRenderer.h"

#include <algorithm>

namespace titan {

namespace {
Color mul(Color a, Color b) { return {a.r * b.r, a.g * b.g, a.b * b.b, a.a * b.a}; }
} // namespace

void drawUI(Renderer& r, const Scene& scene, const UISystem& ui, const UITextureResolver& textures,
            const UIFontResolver& fonts) {
    if (ui.items().empty()) return;
    const World& w = scene.world();
    const float s = ui.scale();
    r.beginOverlay();
    for (const UIItem& item : ui.items()) {
        const Rect& rect = item.rect;
        const UIButton* button = w.get<UIButton>(item.entity);

        if (const UIImage* img = w.get<UIImage>(item.entity)) {
            Color c = img->color;
            if (button && button->pressed) c = mul(c, button->pressedTint);
            if (button && !button->interactable) c.a *= 0.5f;
            const float fill = std::clamp(img->fill, 0.0f, 1.0f);
            if (fill > 0 && c.a > 0) {
                Rect visible = rect;
                visible.w *= fill;
                if (!img->texture.isNull()) {
                    r.drawRect(textures(img->texture), visible, {0, 0, fill, 1}, c);
                } else {
                    r.drawRoundedRect(visible, img->cornerRadius * s, c);
                }
            }
        }

        if (const UIJoystick* j = w.get<UIJoystick>(item.entity)) {
            const float d = std::min(rect.w, rect.h);
            const Vec2 center{rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f};
            r.drawRect(r.discTexture(), {center.x - d * 0.5f, center.y - d * 0.5f, d, d}, {0, 0, 1, 1}, j->baseColor);
            const float hd = d * std::clamp(j->handleSize, 0.1f, 1.0f);
            const float travel = (d - hd) * 0.5f;
            const Vec2 hc{center.x + j->value.x * travel, center.y - j->value.y * travel};
            Color hcCol = j->handleColor;
            if (j->pointer >= 0) hcCol.a = std::min(1.0f, hcCol.a + 0.2f);
            r.drawRect(r.discTexture(), {hc.x - hd * 0.5f, hc.y - hd * 0.5f, hd, hd}, {0, 0, 1, 1}, hcCol);
        }

        if (const UIText* t = w.get<UIText>(item.entity); t && !t->text.empty()) {
            int font = t->font.isNull() ? -1 : fonts(t->font);
            if (font < 0) font = t->bold ? 1 : 0;
            const float size = std::max(1.0f, t->size * s);
            const Vec2 m = r.measureText(font, size, t->text);
            float y = rect.y;
            if (t->verticalAlign == 1) y += (rect.h - m.y) * 0.5f;
            else if (t->verticalAlign == 2) y += rect.h - m.y;
            Color c = t->color;
            if (button && !button->interactable) c.a *= 0.5f;
            if (t->shadow) {
                const float off = std::max(1.0f, size * 0.06f);
                r.drawTextNow(font, {rect.x + off, y + off}, size, t->text, Color{0, 0, 0, c.a * 0.45f}, t->align, rect.w);
            }
            r.drawTextNow(font, {rect.x, y}, size, t->text, c, t->align, rect.w);
        }
    }
    r.endOverlay();
}

} // namespace titan
