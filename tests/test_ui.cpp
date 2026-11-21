#include <fstream>
#include <iterator>

#include "Test.h"
#include "titan/ui/Font.h"
#include "titan/ui/UISystem.h"

using namespace titan;

namespace {
bool near(float a, float b) { return std::fabs(a - b) < 1e-3f; }
bool rectIs(const Rect& r, float x, float y, float w, float h) {
    return near(r.x, x) && near(r.y, y) && near(r.w, w) && near(r.h, h);
}
Entity uiEntity(Scene& s, const char* name, UIRect r, Entity parent = {}) {
    Entity e = s.createEntity(name, parent);
    s.world().add<UIRect>(e, r);
    return e;
}
} // namespace

TEST("ui: anclajes, pivote, estirado y escala con la altura de pantalla") {
    const Rect screen{0, 0, 1280, 720};
    UIRect c; // centrado
    c.size = {200, 100};
    CHECK(rectIs(UISystem::computeRect(c, screen, 1), 540, 310, 200, 100));
    UIRect tl; // arriba a la izquierda, con margen
    tl.anchorMin = tl.anchorMax = {0, 0};
    tl.pivot = {0, 0};
    tl.position = {20, 30};
    tl.size = {100, 50};
    CHECK(rectIs(UISystem::computeRect(tl, screen, 1), 20, 30, 100, 50));
    CHECK(rectIs(UISystem::computeRect(tl, {0, 0, 1920, 1080}, 1.5f), 30, 45, 150, 75));
    UIRect br; // abajo a la derecha
    br.anchorMin = br.anchorMax = {1, 1};
    br.pivot = {1, 1};
    br.position = {-10, -10};
    br.size = {100, 100};
    CHECK(rectIs(UISystem::computeRect(br, screen, 1), 1170, 610, 100, 100));
    UIRect bar; // estirada a lo ancho con 40 px de margen a cada lado, pegada arriba
    bar.anchorMin = {0, 0};
    bar.anchorMax = {1, 0};
    bar.pivot = {0.5f, 0};
    bar.size = {-80, 20};
    CHECK(rectIs(UISystem::computeRect(bar, screen, 1), 40, 0, 1200, 20));
    CHECK_NEAR(UISystem::scaleFor({2400, 1080}), 1.5, 1e-6);
}

TEST("ui: los hijos se colocan dentro del padre y las entidades inactivas no aparecen") {
    Scene s("UI");
    UIRect panelR;
    panelR.size = {400, 200};
    Entity panel = uiEntity(s, "Panel", panelR);
    UIRect childR;
    childR.anchorMin = childR.anchorMax = {0, 0};
    childR.pivot = {0, 0};
    childR.size = {50, 50};
    Entity child = uiEntity(s, "Hijo", childR, panel);
    Entity hidden = uiEntity(s, "Oculto", childR);
    s.info(hidden).active = false;
    UISystem ui;
    ui.layout(s, {1280, 720});
    CHECK_EQ(ui.items().size(), 2u);
    Rect r;
    CHECK(ui.rectOf(child, r));
    CHECK(rectIs(r, 440, 260, 50, 50));
    CHECK(!ui.rectOf(hidden, r));
    CHECK(ui.pick(s, {450, 270}, false) == child); // el hijo se dibuja encima
    CHECK(ui.pick(s, {600, 400}, false) == panel);
    CHECK(ui.pick(s, {600, 400}, true).isNull()); // el panel no es interactivo
}

TEST("ui: un botón cuenta el clic solo si se suelta encima, y se queda con el dedo") {
    Scene s("UI");
    UIRect br;
    br.size = {200, 100};
    Entity btn = uiEntity(s, "Saltar", br);
    s.world().add<UIButton>(btn, {});
    UISystem ui;
    Input input;
    std::vector<Entity> clicked;
    auto frame = [&] {
        clicked.clear();
        ui.layout(s, {1280, 720});
        ui.update(s, input, &clicked);
    };

    input.touchBegan(1, {640, 360});
    frame();
    CHECK(s.world().get<UIButton>(btn)->pressed);
    CHECK(input.pointers()[0].ui);
    CHECK(input.events()[0].ui);
    input.endFrame();
    input.touchEnded(1, {650, 370});
    frame();
    CHECK(s.world().get<UIButton>(btn)->clicked);
    CHECK_EQ(clicked.size(), 1u);
    input.endFrame();
    frame();
    CHECK(!s.world().get<UIButton>(btn)->clicked); // solo dura un fotograma

    input.touchBegan(2, {640, 360});
    frame();
    input.endFrame();
    input.touchMoved(2, {100, 100});
    frame();
    CHECK(!s.world().get<UIButton>(btn)->pressed); // fuera del botón ya no se ve pulsado
    input.endFrame();
    input.touchEnded(2, {100, 100});
    frame();
    CHECK(clicked.empty());

    input.endFrame();
    input.touchBegan(3, {10, 10}); // fuera de la interfaz: es un toque del juego
    frame();
    CHECK(!input.pointers()[0].ui);

    s.world().get<UIButton>(btn)->interactable = false;
    input.endFrame();
    input.touchBegan(4, {640, 360});
    frame();
    CHECK(!s.world().get<UIButton>(btn)->pressed);
}

TEST("ui: joystick virtual con dirección, límite y zona muerta") {
    Scene s("UI");
    UIRect jr;
    jr.anchorMin = jr.anchorMax = {0, 1};
    jr.pivot = {0, 1};
    jr.size = {200, 200};
    Entity joy = uiEntity(s, "Joystick", jr); // centro en (100, 620), radio 100
    s.world().add<UIJoystick>(joy, {});
    UISystem ui;
    Input input;
    auto frame = [&] { ui.layout(s, {1280, 720}); ui.update(s, input); input.endFrame(); };
    input.touchBegan(1, {100, 620});
    frame();
    CHECK_NEAR(s.world().get<UIJoystick>(joy)->value.x, 0, 1e-6); // dentro de la zona muerta
    input.touchMoved(1, {150, 620});
    frame();
    CHECK_NEAR(s.world().get<UIJoystick>(joy)->value.x, 0.5, 1e-4);
    input.touchMoved(1, {100, 320}); // muy arriba: se limita a 1
    frame();
    CHECK_NEAR(s.world().get<UIJoystick>(joy)->value.x, 0, 1e-4);
    CHECK_NEAR(s.world().get<UIJoystick>(joy)->value.y, 1, 1e-4); // arriba = Y positiva
    input.touchEnded(1, {100, 320});
    frame();
    CHECK_NEAR(s.world().get<UIJoystick>(joy)->value.y, 0, 1e-6);
}

TEST("ui: UTF-8") {
    const std::string s = "aé€😀\xff";
    size_t i = 0;
    CHECK_EQ(decodeUtf8(s, i), 0x61u);
    CHECK_EQ(decodeUtf8(s, i), 0xE9u);
    CHECK_EQ(decodeUtf8(s, i), 0x20ACu);
    CHECK_EQ(decodeUtf8(s, i), 0x1F600u);
    CHECK_EQ(decodeUtf8(s, i), 0xFFFDu);
    CHECK_EQ(i, s.size());
}

TEST("ui: fuente TTF del motor con acentos, eñe y signos de apertura") {
    std::ifstream in(TITAN_ENGINE_ASSETS_DIR "/engine/fonts/Outfit-Regular.ttf", std::ios::binary);
    CHECK(in.good());
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    FontAtlas font;
    std::string error;
    CHECK(font.build(bytes, &error));
    CHECK(error.empty());
    for (const char* c : {"á", "é", "ñ", "Ñ", "¿", "¡", "ü", "€"}) {
        size_t i = 0;
        const uint32_t cp = decodeUtf8(c, i);
        const Glyph* g = font.glyph(cp);
        CHECK(g && g->visible && g->advance > 0);
    }
    CHECK(font.width() == 1024 && font.height() <= 1024);
    const Vec2 one = font.measure("Hola", 40), two = font.measure("Hola\nAdiós", 40);
    CHECK(one.x > 40 && one.x < 160);
    CHECK(two.y > one.y * 1.8f);
    std::vector<GlyphQuad> quads;
    font.layout("¿Sí?", 40, {0, 0}, quads, 1, 400);
    CHECK_EQ(quads.size(), 4u);
    CHECK(quads[0].rect.x > 100); // centrado en una caja de 400
    FontAtlas bad;
    CHECK(!bad.build({1, 2, 3}, &error));
    CHECK(!error.empty());
}
