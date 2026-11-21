#include "Test.h"
#include "titan/core/World.h"

using namespace titan;

namespace {
struct Health { int value = 100; };
struct Speed { float value = 1; };
} // namespace

TEST("ecs: crear, añadir, leer y quitar componentes") {
    World w;
    Entity e = w.create();
    CHECK(w.alive(e));
    w.add<Health>(e, {42});
    CHECK(w.has<Health>(e));
    CHECK_EQ(w.get<Health>(e)->value, 42);
    CHECK(!w.has<Speed>(e));
    w.remove<Health>(e);
    CHECK(!w.has<Health>(e));
}

TEST("ecs: una entidad destruida invalida sus copias aunque el índice se reutilice") {
    World w;
    Entity a = w.create();
    w.add<Health>(a);
    w.destroy(a);
    CHECK(!w.alive(a));
    CHECK(w.get<Health>(a) == nullptr);
    Entity b = w.create();
    CHECK_EQ(b.index, a.index);  // índice reutilizado...
    CHECK(a != b);               // ...pero con otra generación
    CHECK(!w.alive(a));
    CHECK(w.alive(b));
}

TEST("ecs: each solo visita las entidades con todos los componentes") {
    World w;
    for (int i = 0; i < 10; ++i) {
        Entity e = w.create();
        w.add<Health>(e, {i});
        if (i % 2 == 0) w.add<Speed>(e);
    }
    int visited = 0, sum = 0;
    w.each<Health, Speed>([&](Entity, Health& h, Speed&) { ++visited; sum += h.value; });
    CHECK_EQ(visited, 5);
    CHECK_EQ(sum, 0 + 2 + 4 + 6 + 8);
}

TEST("ecs: destruir la entidad visitada dentro de each es seguro") {
    World w;
    for (int i = 0; i < 100; ++i) w.add<Health>(w.create(), {i});
    w.each<Health>([&](Entity e, Health& h) {
        if (h.value % 3 == 0) w.destroy(e);
    });
    CHECK_EQ(w.count(), 66u);
    int bad = 0;
    w.each<Health>([&](Entity, Health& h) { if (h.value % 3 == 0) ++bad; });
    CHECK_EQ(bad, 0);
}

TEST("ecs: 100 000 entidades se crean y destruyen sin fugas de índices") {
    World w;
    std::vector<Entity> v;
    for (int i = 0; i < 100000; ++i) {
        Entity e = w.create();
        w.add<Health>(e);
        v.push_back(e);
    }
    for (Entity e : v) w.destroy(e);
    CHECK_EQ(w.count(), 0u);
    CHECK_EQ(w.componentCount(), 0u);
    for (int i = 0; i < 100000; ++i) w.create();
    CHECK_EQ(w.count(), 100000u);
}
