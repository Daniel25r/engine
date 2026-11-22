#include "Test.h"
#include "titan/core/InputActions.h"

using namespace titan;

TEST("entrada: teclas, nombres y bordes de pulsación") {
    CHECK(keyFromName("Space") == Key::Space && keyFromName("w") == Key::W && keyFromName("espacio") == Key::Space);
    CHECK(keyFromName("nada") == Key::Unknown);
    CHECK_EQ(std::string(keyName(Key::LeftShift)), std::string("LeftShift"));
    Input in;
    in.setKey(Key::Space, true);
    CHECK(in.keyDown(Key::Space) && in.keyPressed(Key::Space));
    in.endFrame();
    CHECK(in.keyDown(Key::Space) && !in.keyPressed(Key::Space));
    in.setKey(Key::Space, false);
    CHECK(in.keyReleased(Key::Space));
    in.setKey(Key::A, true);
    in.cancelAll(); // pausa de la app: nada se queda pulsado
    CHECK(!in.keyDown(Key::A));
}

TEST("entrada: acciones por defecto con teclado, mando y diagonal normalizada") {
    InputActions a;
    a.addDefaults();
    Input in;
    in.setKey(Key::D, true);
    in.setKey(Key::W, true);
    a.update(in, nullptr);
    Vec2 v = a.axis("Mover");
    CHECK_NEAR(v.x, 0.7071, 1e-3);
    CHECK_NEAR(v.y, 0.7071, 1e-3);
    CHECK(a.held("Mover"));
    in.setKey(Key::D, false);
    in.setKey(Key::W, false);
    in.setGamepadAxis(GamepadAxis::LeftX, 0.1f); // dentro de la zona muerta
    a.update(in, nullptr);
    CHECK(!a.held("Mover"));
    in.setGamepadAxis(GamepadAxis::LeftX, -1.0f);
    a.update(in, nullptr);
    CHECK_NEAR(a.axis("Mover").x, -1, 1e-4);
    in.setGamepadButton(GamepadButton::A, true);
    a.update(in, nullptr);
    CHECK(a.pressed("Saltar") && a.held("Saltar"));
    a.update(in, nullptr);
    CHECK(!a.pressed("Saltar") && a.held("Saltar"));
    in.setGamepadButton(GamepadButton::A, false);
    a.update(in, nullptr);
    CHECK(a.released("Saltar"));
}

TEST("entrada: acciones con botones y joystick de la UI, y definiciones propias") {
    Scene s("UI");
    Entity jump = s.createEntity("Saltar");
    s.world().add<UIButton>(jump, {});
    Entity joy = s.createEntity("Joystick");
    s.world().add<UIJoystick>(joy, {});
    InputActions a;
    a.addDefaults();
    Input in;
    s.world().get<UIButton>(jump)->pressed = true;
    s.world().get<UIJoystick>(joy)->value = {0, 1};
    a.update(in, &s);
    CHECK(a.held("Saltar"));
    CHECK_NEAR(a.axis("Mover").y, 1, 1e-5);
    std::string err;
    CHECK(a.define("Disparar", InputActions::Type::Button, {"key:K", "pad:RightTrigger"}, &err));
    CHECK(!a.define("Mal", InputActions::Type::Button, {"key:NoExiste"}, &err));
    CHECK(err.find("NoExiste") != std::string::npos);
    CHECK(!a.define("Mal2", InputActions::Type::Axis2D, {"key:K"}, &err)); // una tecla no es un eje
    CHECK(!a.has("Mal") && !a.has("Mal2"));
    in.setGamepadAxis(GamepadAxis::RightTrigger, 0.9f);
    a.update(in, &s);
    CHECK(a.held("Disparar"));
}
