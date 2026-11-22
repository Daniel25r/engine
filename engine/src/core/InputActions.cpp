#include "titan/core/InputActions.h"

#include <cctype>
#include <cmath>

namespace titan {

namespace {
const char* kKeyNames[] = {"Unknown", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q",
                           "R", "S", "T", "U", "V", "W", "X", "Y", "Z", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
                           "Space", "Enter", "Escape", "Tab", "Backspace", "Left", "Right", "Up", "Down", "LeftShift",
                           "RightShift", "LeftCtrl", "RightCtrl", "LeftAlt", "RightAlt", "F1", "F2", "F3", "F4", "F5",
                           "F6", "F7", "F8", "F9", "F10", "F11", "F12"};
static_assert(sizeof(kKeyNames) / sizeof(kKeyNames[0]) == static_cast<size_t>(Key::Count), "faltan nombres de teclas");
const char* kPadNames[] = {"A", "B", "X", "Y", "LB", "RB", "Back", "Start", "LeftStickButton", "RightStickButton",
                           "DpadUp", "DpadDown", "DpadLeft", "DpadRight"};
static_assert(sizeof(kPadNames) / sizeof(kPadNames[0]) == static_cast<size_t>(GamepadButton::Count), "faltan botones");

bool iequals(const std::string& a, const char* b) {
    size_t i = 0;
    for (; i < a.size() && b[i]; ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return i == a.size() && !b[i];
}
} // namespace

const char* keyName(Key k) { return k < Key::Count ? kKeyNames[static_cast<size_t>(k)] : "Unknown"; }

Key keyFromName(const std::string& name) {
    for (size_t i = 1; i < static_cast<size_t>(Key::Count); ++i)
        if (iequals(name, kKeyNames[i])) return static_cast<Key>(i);
    if (iequals(name, "Espacio")) return Key::Space;
    if (iequals(name, "Shift")) return Key::LeftShift;
    if (iequals(name, "Ctrl")) return Key::LeftCtrl;
    if (iequals(name, "Esc")) return Key::Escape;
    return Key::Unknown;
}

const char* gamepadButtonName(GamepadButton b) { return b < GamepadButton::Count ? kPadNames[static_cast<size_t>(b)] : "?"; }

bool gamepadButtonFromName(const std::string& name, GamepadButton& out) {
    for (size_t i = 0; i < static_cast<size_t>(GamepadButton::Count); ++i)
        if (iequals(name, kPadNames[i])) { out = static_cast<GamepadButton>(i); return true; }
    return false;
}

bool InputActions::parse(const std::string& text, Type type, Binding& b, std::string& error) {
    const size_t colon = text.find(':');
    const std::string kind = colon == std::string::npos ? text : text.substr(0, colon);
    const std::string arg = colon == std::string::npos ? "" : text.substr(colon + 1);
    const bool axis = type == Type::Axis2D;
    auto fail = [&](const std::string& why) { error = "'" + text + "': " + why; return false; };
    if (kind == "touch") { b.kind = Binding::Touch; return !axis || fail("un toque no sirve para un eje"); }
    if (kind == "key") {
        b.kind = Binding::KeyB;
        b.key = keyFromName(arg);
        if (b.key == Key::Unknown) return fail("tecla desconocida");
        return !axis || fail("una tecla sola no sirve para un eje (usa keys:WASD o keys:Arrows)");
    }
    if (kind == "keys") {
        b.kind = Binding::Keys4;
        if (iequals(arg, "WASD")) { b.key = Key::W; b.k2 = Key::A; b.k3 = Key::S; b.k4 = Key::D; }
        else if (iequals(arg, "Arrows") || iequals(arg, "Flechas")) { b.key = Key::Up; b.k2 = Key::Left; b.k3 = Key::Down; b.k4 = Key::Right; }
        else return fail("usa keys:WASD o keys:Arrows");
        return axis || fail("keys: es para ejes");
    }
    if (kind == "pad") {
        if (iequals(arg, "LeftStick") || iequals(arg, "RightStick")) {
            b.kind = Binding::PadStick;
            const bool left = iequals(arg, "LeftStick");
            b.ax = left ? GamepadAxis::LeftX : GamepadAxis::RightX;
            b.ay = left ? GamepadAxis::LeftY : GamepadAxis::RightY;
            return axis || fail("un stick es un eje");
        }
        if (iequals(arg, "LeftTrigger") || iequals(arg, "RightTrigger")) {
            b.kind = Binding::PadTrigger;
            b.ax = iequals(arg, "LeftTrigger") ? GamepadAxis::LeftTrigger : GamepadAxis::RightTrigger;
            return !axis || fail("un gatillo es un botón");
        }
        b.kind = Binding::PadButton;
        if (!gamepadButtonFromName(arg, b.button)) return fail("botón de mando desconocido");
        return !axis || fail("un botón no sirve para un eje");
    }
    if (kind == "ui") {
        b.kind = Binding::UIButtonB;
        b.entity = arg;
        if (arg.empty()) return fail("falta el nombre del botón");
        return !axis || fail("un botón de la UI no sirve para un eje (usa uijoy:)");
    }
    if (kind == "uijoy") {
        b.kind = Binding::UIJoy;
        b.entity = arg.empty() ? "*" : arg;
        return axis || fail("un joystick es un eje");
    }
    return fail("tipo de enlace desconocido (key, keys, pad, ui, uijoy, touch)");
}

bool InputActions::define(const std::string& name, Type type, const std::vector<std::string>& bindings, std::string* error) {
    Action a;
    a.type = type;
    for (const std::string& t : bindings) {
        Binding b{};
        std::string err;
        if (!parse(t, type, b, err)) {
            if (error) *error = err;
            return false;
        }
        a.bindings.push_back(b);
    }
    actions_[name] = std::move(a);
    return true;
}

void InputActions::addDefaults() {
    define("Mover", Type::Axis2D, {"keys:WASD", "keys:Arrows", "pad:LeftStick", "uijoy:*"});
    define("Mirar", Type::Axis2D, {"pad:RightStick"});
    define("Saltar", Type::Button, {"key:Space", "pad:A", "ui:Saltar"});
    define("Atacar", Type::Button, {"key:J", "pad:X", "pad:RightTrigger", "ui:Atacar"});
    define("Interactuar", Type::Button, {"key:E", "pad:Y", "ui:Interactuar"});
    define("Correr", Type::Button, {"key:LeftShift", "pad:LB", "ui:Correr"});
    define("Pausa", Type::Button, {"key:Escape", "pad:Start", "ui:Pausa"});
}

void InputActions::update(const Input& in, const Scene* scene) {
    bool anyTouch = false;
    for (const TouchEvent& ev : in.events()) anyTouch = anyTouch || (ev.phase == TouchPhase::Began && !ev.ui);
    for (auto& [name, a] : actions_) {
        a.prevHeld = a.held;
        a.held = false;
        a.value = {};
        for (const Binding& b : a.bindings) {
            Vec2 v;
            bool down = false;
            switch (b.kind) {
            case Binding::KeyB: down = in.keyDown(b.key); break;
            case Binding::PadButton: down = in.buttonDown(b.button); break;
            case Binding::PadTrigger: down = in.axis(b.ax) > 0.5f; break;
            case Binding::Touch: down = anyTouch; break;
            case Binding::UIButtonB:
                if (scene) {
                    const Entity e = scene->findByName(b.entity);
                    const UIButton* u = e.isNull() ? nullptr : scene->world().get<UIButton>(e);
                    down = u && (u->pressed || u->clicked);
                }
                break;
            case Binding::Keys4:
                v = {(in.keyDown(b.k4) ? 1.0f : 0.0f) - (in.keyDown(b.k2) ? 1.0f : 0.0f),
                     (in.keyDown(b.key) ? 1.0f : 0.0f) - (in.keyDown(b.k3) ? 1.0f : 0.0f)};
                break;
            case Binding::PadStick: {
                v = {in.axis(b.ax), in.axis(b.ay)};
                const float len = std::sqrt(v.x * v.x + v.y * v.y);
                if (len < deadZone) v = {};
                else v = v * ((std::min(len, 1.0f) - deadZone) / (1.0f - deadZone) / len); // zona muerta radial reescalada
                break;
            }
            case Binding::UIJoy:
                if (scene) {
                    const World& w = scene->world();
                    if (b.entity == "*") {
                        const_cast<World&>(w).each<UIJoystick>([&](Entity, UIJoystick& j) {
                            if (j.value.x * j.value.x + j.value.y * j.value.y > v.x * v.x + v.y * v.y) v = j.value;
                        });
                    } else {
                        const Entity e = scene->findByName(b.entity);
                        if (const UIJoystick* j = e.isNull() ? nullptr : w.get<UIJoystick>(e)) v = j->value;
                    }
                }
                break;
            }
            if (a.type == Type::Button) {
                a.held = a.held || down;
            } else if (v.x * v.x + v.y * v.y > a.value.x * a.value.x + a.value.y * a.value.y) {
                a.value = v; // gana el dispositivo que más se está usando
            }
        }
        if (a.type == Type::Axis2D) {
            const float len = std::sqrt(a.value.x * a.value.x + a.value.y * a.value.y);
            if (len > 1) a.value = a.value * (1.0f / len); // diagonal del teclado: no más rápida que una dirección
            a.held = len > 0.01f;
        } else {
            a.value = {a.held ? 1.0f : 0.0f, 0};
        }
    }
}

bool InputActions::held(const std::string& n) const { auto it = actions_.find(n); return it != actions_.end() && it->second.held; }
bool InputActions::pressed(const std::string& n) const {
    auto it = actions_.find(n);
    return it != actions_.end() && it->second.held && !it->second.prevHeld;
}
bool InputActions::released(const std::string& n) const {
    auto it = actions_.find(n);
    return it != actions_.end() && !it->second.held && it->second.prevHeld;
}
Vec2 InputActions::axis(const std::string& n) const { auto it = actions_.find(n); return it == actions_.end() ? Vec2{} : it->second.value; }

} // namespace titan
