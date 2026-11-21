#pragma once
// Dibuja la interfaz de juego (UIImage, UIText, UIJoystick) con los rectángulos que calculó UISystem.
// Lo usan el runtime y la vista del editor.
#include <functional>

#include "titan/render/Renderer.h"
#include "titan/ui/UISystem.h"

namespace titan {

using UITextureResolver = std::function<TextureHandle(AssetRef)>;
using UIFontResolver = std::function<int(AssetRef)>; // -1 = fuente del motor

void drawUI(Renderer& renderer, const Scene& scene, const UISystem& ui, const UITextureResolver& textures,
            const UIFontResolver& fonts);

} // namespace titan
