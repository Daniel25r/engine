#include "Test.h"
#include "titan/core/Scene.h"

using namespace titan;

TEST("escena: crear entidades con id, nombre y transformación") {
    Scene s;
    Entity e = s.createEntity("Jugador");
    CHECK(s.isValid(e));
    CHECK_EQ(s.info(e).name, std::string("Jugador"));
    CHECK(!s.info(e).id.isNull());
    CHECK(s.findById(s.info(e).id) == e);
    CHECK(s.findByName("Jugador") == e);
    CHECK(s.world().has<Transform>(e));
}

TEST("escena: no se aceptan ids repetidos") {
    Scene s;
    const UUID id = UUID::generate();
    CHECK(!s.createEntity("A", {}, id).isNull());
    CHECK(s.createEntity("B", {}, id).isNull());
    CHECK_EQ(s.entityCount(), 1u);
}

TEST("jerarquía: la matriz de mundo combina las de los padres") {
    Scene s;
    Entity root = s.createEntity("Raíz");
    Entity child = s.createEntity("Hijo", root);
    s.transform(root).setPosition({10, 0, 0});
    s.transform(root).setEulerDegrees({0, 0, 90});
    s.transform(child).setPosition({1, 0, 0});
    TransformSystem::update(s);
    // El hijo está a 1 unidad en el eje X local del padre, que tras girar 90° en Z apunta a +Y.
    CHECK_VEC3(s.transform(child).worldPosition(), 10, 1, 0);
    CHECK_VEC3(s.transform(child).right(), 0, 1, 0);
}

TEST("jerarquía: solo se recalculan las ramas modificadas") {
    Scene s;
    Entity a = s.createEntity("A");
    Entity b = s.createEntity("B");
    s.createEntity("A1", a);
    s.createEntity("A2", a);
    CHECK_EQ(TransformSystem::update(s), 4u);
    CHECK_EQ(TransformSystem::update(s), 0u); // nada cambió
    s.transform(a).translate({1, 0, 0});
    CHECK_EQ(TransformSystem::update(s), 3u); // A y sus dos hijos
    s.transform(b).setScale({2, 2, 2});
    CHECK_EQ(TransformSystem::update(s), 1u);
}

TEST("jerarquía: cambiar de padre conserva la posición en el mundo") {
    Scene s;
    Entity p = s.createEntity("Padre");
    Entity c = s.createEntity("Hijo");
    s.transform(p).setPosition({5, 5, 0});
    s.transform(p).setScale({2, 2, 2});
    s.transform(c).setPosition({7, 1, 0});
    CHECK(s.setParent(c, p, true));
    TransformSystem::update(s);
    CHECK_VEC3(s.transform(c).worldPosition(), 7, 1, 0);
    CHECK_VEC3(s.transform(c).position(), 1, -2, 0); // (7-5)/2, (1-5)/2
    CHECK(s.parent(c) == p);
    CHECK_EQ(s.roots().size(), 1u);
}

TEST("jerarquía: se rechazan los ciclos") {
    Scene s;
    Entity a = s.createEntity("A");
    Entity b = s.createEntity("B", a);
    Entity c = s.createEntity("C", b);
    CHECK(!s.setParent(a, c));
    CHECK(!s.setParent(a, a));
    CHECK(s.parent(a).isNull());
}

TEST("jerarquía: el orden de los hijos se conserva") {
    Scene s;
    Entity p = s.createEntity("P");
    Entity c1 = s.createEntity("1", p), c2 = s.createEntity("2", p), c3 = s.createEntity("3", p);
    s.setParent(c2, {}, false);
    auto kids = s.children(p);
    CHECK_EQ(kids.size(), 2u);
    CHECK(kids[0] == c1 && kids[1] == c3);
    s.setParent(c2, p, false);
    kids = s.children(p);
    CHECK(kids[2] == c2);
}

TEST("escena: destruir es diferido y arrastra a los hijos") {
    Scene s;
    Entity p = s.createEntity("P");
    Entity c = s.createEntity("C", p);
    Entity other = s.createEntity("Otro");
    s.destroyEntity(p);
    CHECK(s.isValid(p)); // aún no
    s.flushDestroyed();
    CHECK(!s.isValid(p));
    CHECK(!s.isValid(c));
    CHECK(s.isValid(other));
    CHECK_EQ(s.entityCount(), 1u);
    CHECK_EQ(s.roots().size(), 1u);
}

TEST("escena: activa en la jerarquía depende de los antepasados") {
    Scene s;
    Entity p = s.createEntity("P");
    Entity c = s.createEntity("C", p);
    CHECK(s.isActiveInHierarchy(c));
    s.info(p).active = false;
    CHECK(!s.isActiveInHierarchy(c));
}
