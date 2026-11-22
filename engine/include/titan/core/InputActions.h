#pragma once
// Acciones de entrada: el juego pregunta por "Saltar" o "Mover" y no por una tecla concreta, así el mismo
// código funciona con dedo (botones y joystick de la UI), teclado y mando.
//
// Enlaces (texto), por ejemplo:
//   "key:Space"     tecla              "pad:A"           botón del mando
//   "ui:Saltar"     UIButton con ese nombre              "touch"           cualquier toque del juego
//   "keys:WASD"     cuatro teclas (eje) "keys:Arrows"     flechas (eje)
//   "pad:LeftStick" stick (eje)         "uijoy:Joystick"  UIJoystick con ese nombre ("uijoy:*" = cualquiera)
//   "pad:LeftTrigger" gatillo como botón
#include <string>
#include <unordered_map>
#include <vector>

#include "titan/core/Input.h"
#include "titan/core/Scene.h"

namespace titan {

class InputActions {
public:
    enum class Type { Button, Axis2D };

    // Mover, Mirar, Saltar, Atacar, Interactuar, Correr, Pausa (ver docs/ENGINE_API.md).
    void addDefaults();
    // Define o redefine una acción. Devuelve false (y no cambia nada) si algún enlace no es válido; error lo explica.
    bool define(const std::string& name, Type type, const std::vector<std::string>& bindings, std::string* error = nullptr);
    bool has(const std::string& name) const { return actions_.count(name) > 0; }
    float deadZone = 0.15f; // para sticks del mando

    // Una vez por fotograma, después de que la UI haya leído los toques. scene puede ser nula.
    void update(const Input& input, const Scene* scene);

    bool held(const std::string& name) const;     // mantenida
    bool pressed(const std::string& name) const;  // empezó en este fotograma
    bool released(const std::string& name) const; // terminó en este fotograma
    Vec2 axis(const std::string& name) const;     // -1..1 (Y positiva = arriba); botones: (0/1, 0)

private:
    struct Binding {
        enum Kind { KeyB, PadButton, PadTrigger, UIButtonB, Touch, Keys4, PadStick, UIJoy } kind;
        Key key = Key::Unknown, k2 = Key::Unknown, k3 = Key::Unknown, k4 = Key::Unknown; // Keys4: arriba, izq., abajo, der.
        GamepadButton button = GamepadButton::A;
        GamepadAxis ax = GamepadAxis::LeftX, ay = GamepadAxis::LeftY;
        std::string entity; // UI
    };
    struct Action {
        Type type = Type::Button;
        std::vector<Binding> bindings;
        bool held = false, prevHeld = false;
        Vec2 value;
    };
    static bool parse(const std::string& text, Type type, Binding& out, std::string& error);
    std::unordered_map<std::string, Action> actions_;
};

} // namespace titan
