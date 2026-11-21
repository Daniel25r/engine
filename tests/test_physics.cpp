#include "Test.h"
#include "titan/physics/PhysicsWorld.h"

using namespace titan;

namespace {
Entity floorOf(Scene& s) {
    Entity f = s.createEntity("Suelo");
    s.transform(f).setPosition({0, -0.5f, 0});
    s.transform(f).setScale({20, 1, 20});
    s.world().add<Collider>(f);
    return f;
}
Entity boxAt(Scene& s, const char* name, Vec3 p) {
    Entity e = s.createEntity(name);
    s.transform(e).setPosition(p);
    s.world().add<Collider>(e);
    s.world().add<Rigidbody>(e);
    return e;
}
void simulate(PhysicsWorld& w, Scene& s, float seconds) {
    for (int i = 0; i < static_cast<int>(seconds * 60); ++i) w.step(s, 1.0f / 60.0f);
}
} // namespace

TEST("física: un cubo cae por gravedad y se apoya en el suelo") {
    Scene s;
    floorOf(s);
    Entity box = boxAt(s, "Caja", {0, 5, 0});
    PhysicsWorld w;
    w.build(s);
    CHECK_EQ(w.bodyCount(), 2u);
    simulate(w, s, 0.5f);
    CHECK(s.transform(box).position().y < 4.0f); // ha caído (≈ 5 - ½·9,81·0,25 ≈ 3,8)
    simulate(w, s, 3.0f);
    CHECK_NEAR(s.transform(box).position().y, 0.5, 0.05); // descansa sobre el suelo
}

TEST("física: eventos de colisión al empezar y terminar") {
    Scene s;
    Entity floor = floorOf(s);
    Entity box = boxAt(s, "Caja", {0, 2, 0});
    PhysicsWorld w;
    w.build(s);
    int begins = 0;
    for (int i = 0; i < 120; ++i) {
        w.step(s, 1.0f / 60.0f);
        for (const ContactEvent& ev : w.takeEvents())
            if (ev.begin && !ev.trigger && ((ev.a == floor && ev.b == box) || (ev.a == box && ev.b == floor))) ++begins;
    }
    CHECK_EQ(begins, 1); // un solo "Enter" aunque haya varios puntos de contacto
    w.setVelocity(box, {0, 8, 0});
    bool ended = false;
    for (int i = 0; i < 30; ++i) {
        w.step(s, 1.0f / 60.0f);
        for (const ContactEvent& ev : w.takeEvents()) ended |= !ev.begin;
    }
    CHECK(ended);
}

TEST("física: sensores (triggers) detectan sin frenar") {
    Scene s;
    floorOf(s);
    Entity zone = s.createEntity("Zona");
    s.transform(zone).setPosition({0, 2, 0});
    Collider c;
    c.size = {3, 1, 3};
    c.isTrigger = true;
    s.world().add<Collider>(zone, c);
    boxAt(s, "Caja", {0, 6, 0});
    PhysicsWorld w;
    w.build(s);
    bool triggered = false;
    for (int i = 0; i < 180; ++i) {
        w.step(s, 1.0f / 60.0f);
        for (const ContactEvent& ev : w.takeEvents()) triggered |= ev.begin && ev.trigger;
    }
    CHECK(triggered);
    CHECK_NEAR(s.transform(s.findByName("Caja")).position().y, 0.5, 0.05); // atravesó la zona y llegó al suelo
}

TEST("física: raycast, impulsos y gravedad") {
    Scene s;
    floorOf(s);
    Entity box = boxAt(s, "Caja", {3, 0.5f, 0});
    PhysicsWorld w;
    w.build(s);
    RaycastHit hit;
    CHECK(w.raycast({-5, 0.5f, 0}, {1, 0, 0}, 50, hit));
    CHECK(hit.entity == box);
    CHECK_NEAR(hit.distance, 7.5, 0.05);
    CHECK_NEAR(hit.normal.x, -1, 0.01);
    CHECK(!w.raycast({-5, 5, 0}, {0, 1, 0}, 50, hit));
    // Desde dentro de la caja, ignorándola, el rayo hacia abajo encuentra el suelo.
    CHECK(w.raycast({3, 0.5f, 0}, {0, -1, 0}, 10, hit, box));
    CHECK(hit.entity == s.findByName("Suelo"));

    w.addForce(box, {0, 6, 0}, true); // impulso hacia arriba: v = 6 m/s con 1 kg
    w.step(s, 1.0f / 60.0f);
    CHECK(w.velocity(box).y > 5.0f);

    for (int i = 0; i < 9; ++i) w.step(s, 1.0f / 60.0f);
    w.setGravity({0, 0, 0});
    w.setVelocity(box, {0, 0, 0});
    const float y = s.transform(box).position().y;
    CHECK(y > 0.7f);
    simulate(w, s, 1.0f);
    CHECK_NEAR(s.transform(box).position().y, y, 0.01); // sin gravedad se queda flotando
}
