#include "Test.h"
#include "titan/core/AnimationGraph.h"
#include "titan/core/Serialization.h"

using namespace titan;

namespace {
// Idle -> Walk (velocidad > 0,1) -> Idle (velocidad < 0,1); Saltar (trigger) desde cualquiera; Jump sin bucle
// vuelve a Idle al terminar. Locomoción: mezcla 1D Walk (1) / Run (4).
Animator makeAnimator() {
    Animator a;
    AnimStateMachine& m = a.machine;
    m.params = {{"velocidad", AnimParam::Type::Float, 0}, {"saltar", AnimParam::Type::Trigger, 0}};
    AnimState idle{"Idle", "Idle", 1, true, "", {}, {}};
    AnimState move{"Mover", "", 1, true, "velocidad", {{1, "Walk"}, {4, "Run"}}, {{0.5f, "paso"}}};
    AnimState jump{"Saltar", "Jump", 1, false, "", {}, {}};
    m.states = {idle, move, jump};
    m.transitions = {
        {"Idle", "Mover", {{"velocidad", AnimCondition::Op::Greater, 0.1f}}, 0.2f, false, 1},
        {"Mover", "Idle", {{"velocidad", AnimCondition::Op::Less, 0.1f}}, 0.2f, false, 1},
        {"*", "Saltar", {{"saltar", AnimCondition::Op::IsTrue, 0}}, 0.1f, false, 1},
        {"Saltar", "Idle", {}, 0.2f, true, 1.0f},
    };
    return a;
}
float dur(const std::string& clip) { return clip == "Run" ? 0.5f : clip == "Jump" ? 0.8f : 1.0f; }
void run(Animator& a, float seconds) {
    for (int i = 0; i < int(seconds * 60); ++i) updateAnimator(a, 1.0f / 60, dur);
}
} // namespace

TEST("animación: estados, condiciones, trigger y salida al terminar el clip") {
    Animator a = makeAnimator();
    run(a, 0.1f);
    CHECK_EQ(a.machine.current, std::string("Idle"));
    CHECK_EQ(a.clip, std::string("Idle"));
    CHECK(setAnimParam(a, "velocidad", 2));
    run(a, 0.05f);
    CHECK_EQ(a.machine.current, std::string("Mover"));
    CHECK(a.fromWeight > 0 && a.fromClip == "Idle"); // transición suave desde Idle
    CHECK(setAnimTrigger(a, "saltar"));
    run(a, 0.05f);
    CHECK_EQ(a.machine.current, std::string("Saltar"));
    CHECK_NEAR(animParam(a, "saltar"), 0, 1e-6); // el trigger se consume
    setAnimParam(a, "velocidad", 0);                // al aterrizar estará quieto
    run(a, 0.5f);
    CHECK_EQ(a.machine.current, std::string("Saltar")); // aún no ha terminado (0,8 s)
    run(a, 0.5f);
    CHECK_EQ(a.machine.current, std::string("Idle"));   // terminó: sale por tiempo de salida
    CHECK(!setAnimParam(a, "noexiste", 1));
}

TEST("animación: mezcla 1D sincronizada y eventos") {
    Animator a = makeAnimator();
    setAnimParam(a, "velocidad", 2.5f); // a mitad entre Walk (1) y Run (4)
    run(a, 1.0f);
    CHECK_EQ(a.machine.current, std::string("Mover"));
    CHECK_EQ(a.clip, std::string("Run"));
    CHECK_EQ(a.blendClip, std::string("Walk"));
    CHECK_NEAR(a.blendWeight, 0.5, 1e-4);
    CHECK_NEAR(a.time / 0.5f, a.blendTime / 1.0f, 1e-4); // misma fase en los dos clips (los pasos coinciden)
    // Evento "paso" a mitad de ciclo: duración media 0,75 s → en 3 s unas 4 veces.
    int steps = 0;
    for (int i = 0; i < 180; ++i) {
        updateAnimator(a, 1.0f / 60, dur);
        for (auto& e : a.machine.firedEvents) steps += e == "paso";
    }
    CHECK(steps >= 3 && steps <= 5);
    setAnimParam(a, "velocidad", 10); // por encima del último punto: solo Run
    run(a, 0.1f);
    CHECK_NEAR(a.blendWeight, 0, 1e-6);
}

TEST("animación: la máquina se guarda y se carga; sin estados, el Animator funciona como antes") {
    Animator a = makeAnimator();
    Json j = machineToJson(a.machine);
    AnimStateMachine back;
    machineFromJson(j, back);
    CHECK_EQ(back.states.size(), 3u);
    CHECK_EQ(back.transitions.size(), 4u);
    CHECK(back.states[1].blend.size() == 2 && back.states[1].events.size() == 1);
    CHECK(back.transitions[2].from == "*" && back.transitions[2].conditions[0].op == AnimCondition::Op::IsTrue);
    CHECK(back.params[1].type == AnimParam::Type::Trigger);
    Animator plain;
    plain.clip = "Walk";
    updateAnimator(plain, 0.5f, dur);
    CHECK_NEAR(plain.time, 0.5, 1e-6);
    CHECK_EQ(plain.clip, std::string("Walk"));
}
