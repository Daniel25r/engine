#include <algorithm>
#include <cmath>

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

TEST("física: el personaje cae, se apoya, camina, sube un escalón y salta") {
    Scene s;
    floorOf(s);
    Entity step = s.createEntity("Escalón"); // 0,25 m de alto, de x = 3 a x = 7
    s.transform(step).setPosition({5, 0.125f, 0});
    s.transform(step).setScale({4, 0.25f, 4});
    s.world().add<Collider>(step);
    Entity hero = s.createEntity("Héroe");
    s.transform(hero).setPosition({0, 2, 0});
    s.world().add<CharacterController>(hero, {});
    PhysicsWorld w;
    w.build(s);
    CHECK(w.isCharacter(hero));
    simulate(w, s, 1.5f);
    CharacterController& cc = *s.world().get<CharacterController>(hero);
    CHECK(cc.grounded);
    CHECK_NEAR(s.transform(hero).position().y, 0, 0.05);
    cc.desiredVelocity = {4, 0, 0};
    simulate(w, s, 1.5f); // 6 m: pasa por encima del escalón
    CHECK(s.transform(hero).position().x > 4.5f);
    CHECK_NEAR(s.transform(hero).position().y, 0.25, 0.06);
    cc.desiredVelocity = {};
    cc.jumpSpeed = 5;
    simulate(w, s, 0.25f);
    CHECK(s.transform(hero).position().y > 0.8f); // en el aire
    CHECK(!cc.grounded);
    simulate(w, s, 2);
    CHECK(cc.grounded);
}

TEST("física: capas de colisión, sphereCast y overlaps") {
    Scene s;
    floorOf(s);
    Entity a = boxAt(s, "A", {0, 3, 0});
    s.world().get<Collider>(a)->layer = 3;
    Entity b = boxAt(s, "B", {0, 1, 0}); // capa 0, debajo de A
    PhysicsWorld w;
    w.setLayerCollision(3, 0, false); // A atraviesa la capa 0 (B y el suelo)
    CHECK(!w.layersCollide(0, 3) && w.layersCollide(0, 0));
    w.build(s);
    simulate(w, s, 2);
    CHECK(s.transform(a).position().y < -2.0f);    // cayó a través de todo
    CHECK_NEAR(s.transform(b).position().y, 0.5, 0.05); // B sí se apoya en el suelo

    RaycastHit hit;
    CHECK(w.sphereCast({-5, 0.5f, 0}, 0.25f, {1, 0, 0}, 20, hit));
    CHECK(hit.entity == b);
    CHECK_NEAR(hit.distance, 4.25, 0.05); // superficie de B en x = -0,5, menos el radio
    CHECK(!w.sphereCast({-5, 0.5f, 0}, 0.25f, {1, 0, 0}, 20, hit, 1u << 5)); // solo capa 5: nada
    const auto near = w.overlapSphere({0, 0.5f, 0}, 0.2f);
    CHECK(std::find(near.begin(), near.end(), b) != near.end());
    const auto inBox = w.overlapBox({0, 0.5f, 0}, {3, 0.2f, 3}, Quat{}, 1u << 0);
    CHECK(std::find(inBox.begin(), inBox.end(), b) != inBox.end());
    CHECK(std::find(inBox.begin(), inBox.end(), a) == inBox.end());
}

TEST("física: bisagra sujeta al mundo y cuerda entre dos cuerpos") {
    Scene s;
    Entity door = boxAt(s, "Puerta", {1, 5, 0}); // cuelga de una bisagra en (0, 5, 0) con eje Z
    Joint hinge;
    hinge.type = Joint::Type::Hinge;
    hinge.anchor = {-1, 0, 0};
    hinge.axis = {0, 0, 1};
    s.world().add<Joint>(door, hinge);
    Entity ball = boxAt(s, "Bola", {5, 5, 0});
    Entity anchorBox = s.createEntity("Ancla");
    s.transform(anchorBox).setPosition({5, 8, 0});
    s.world().add<Collider>(anchorBox); // estático
    Joint rope;
    rope.type = Joint::Type::Distance;
    rope.connected = s.info(anchorBox).id;
    rope.minDistance = 0;
    rope.maxDistance = 3;
    s.world().add<Joint>(ball, rope);
    PhysicsWorld w;
    w.build(s);
    simulate(w, s, 3);
    const Vec3 dp = s.transform(door).position();
    CHECK_NEAR((dp - Vec3{0, 5, 0}).length(), 1, 0.05); // gira alrededor de la bisagra a 1 m
    CHECK(dp.y < 4.5f);                                // y ha caído colgando
    const Vec3 bp = s.transform(ball).position();
    CHECK((bp - Vec3{5, 8, 0}).length() < 3.05f);      // la cuerda no deja que caiga más
    CHECK(bp.y < 5.5f && bp.y > 4.5f);
}

TEST("física: collider de malla (rampa) y envolvente convexa dinámica") {
    Scene s;
    // Rampa: un único triángulo grande inclinado (sube hacia +X) y un suelo hecho también de malla.
    Entity ramp = s.createEntity("Rampa");
    Collider rc;
    rc.shape = Collider::Shape::Mesh;
    s.world().add<Collider>(ramp, rc);
    Entity ground = s.createEntity("Suelo");
    s.transform(ground).setPosition({0, 0, 0});
    s.world().add<Collider>(ground, rc);
    Entity rock = s.createEntity("Roca"); // envolvente convexa de un tetraedro, dinámica
    s.transform(rock).setPosition({-6, 3, 0});
    Collider hull;
    hull.shape = Collider::Shape::ConvexHull;
    s.world().add<Collider>(rock, hull);
    s.world().add<Rigidbody>(rock);
    PhysicsWorld w;
    w.setMeshProvider([&](Entity e, std::vector<Vec3>& v, std::vector<uint32_t>& idx) {
        if (e == ramp) {
            v = {{0, 0, -5}, {0, 0, 5}, {10, 5, 5}, {10, 5, -5}};
            idx = {0, 1, 2, 0, 2, 3}; // antihorario visto desde arriba
        } else if (e == ground) {
            v = {{-20, 0, -20}, {-20, 0, 20}, {20, 0, 20}, {20, 0, -20}};
            idx = {0, 1, 2, 0, 2, 3};
        } else if (e == rock) {
            v = {{-0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, -0.5f}, {0, -0.5f, 0.5f}, {0, 0.5f, 0}};
            idx = {0, 1, 2, 0, 1, 3, 1, 2, 3, 2, 0, 3};
        } else {
            return false;
        }
        return true;
    });
    w.build(s);
    CHECK_EQ(w.bodyCount(), 3u);
    RaycastHit hit;
    CHECK(w.raycast({5, 20, 0}, {0, -1, 0}, 50, hit)); // la rampa a mitad mide 2,5 m
    CHECK(hit.entity == ramp);
    CHECK_NEAR(hit.point.y, 2.5, 0.02);
    CHECK(hit.normal.x < -0.3f && hit.normal.y > 0.8f); // normal inclinada
    simulate(w, s, 3);
    CHECK_NEAR(s.transform(rock).position().y, 0, 0.6); // la roca descansa en el suelo de malla
    CHECK(s.transform(rock).position().y > -0.1f);      // y no lo atraviesa
}

#include "titan/core/CameraSystem.h"

TEST("cámara: sigue al objetivo detrás y arriba, gira al arrastrar y se acerca si hay una pared") {
    Scene s;
    Entity hero = s.createEntity("Héroe");
    s.transform(hero).setPosition({0, 0, 0});
    Entity cam = s.createEntity("Cámara");
    CameraFollow f;
    f.target = s.info(hero).id;
    f.distance = 6;
    f.pitch = 0;
    f.height = 1;
    f.smoothing = 0; // sin suavizado para comprobar la posición exacta
    s.world().add<CameraFollow>(cam, f);
    CameraSystem cams;
    Input in;
    cams.update(s, in, nullptr, nullptr, 1.0f / 60);
    CHECK_VEC3(s.transform(cam).position(), 0, 1, 6); // detrás (+Z) a 6 m, a la altura del punto de mira
    CHECK_VEC3(s.transform(cam).forward(), 0, 0, -1);  // mirando al objetivo
    // Arrastrar un dedo 100 px a la izquierda gira la cámara 25 grados.
    in.touchBegan(1, {500, 300});
    cams.update(s, in, nullptr, nullptr, 1.0f / 60);
    in.endFrame();
    in.touchMoved(1, {400, 300});
    cams.update(s, in, nullptr, nullptr, 1.0f / 60);
    CHECK_NEAR(s.world().get<CameraFollow>(cam)->yaw, 25, 1e-3);
    // Una pared entre el héroe y la cámara la acerca.
    s.world().get<CameraFollow>(cam)->yaw = 0;
    Entity wall = s.createEntity("Pared");
    s.transform(wall).setPosition({0, 1, 3});
    s.transform(wall).setScale({10, 5, 0.5f});
    s.world().add<Collider>(wall);
    PhysicsWorld w;
    w.build(s);
    in.endFrame();
    in.touchEnded(1, {400, 300});
    cams.update(s, in, nullptr, &w, 1.0f / 60);
    const float z = s.transform(cam).position().z;
    CHECK(z < 2.8f && z > 2.0f); // delante de la pared (cara en z = 2,75), no detrás
}

TEST("personaje avanzado: aceleración, doble salto, salto variable, giro y paredes") {
    Scene s;
    floorOf(s);
    Entity wall = s.createEntity("Pared");
    s.transform(wall).setPosition({6, 2, 0});
    s.transform(wall).setScale({1, 4, 6});
    s.world().add<Collider>(wall);
    Entity hero = s.createEntity("Héroe");
    s.transform(hero).setPosition({0, 0.1f, 0});
    CharacterController cc;
    cc.acceleration = 20;
    cc.maxJumps = 2;
    s.world().add<CharacterController>(hero, cc);
    PhysicsWorld w;
    w.build(s);
    simulate(w, s, 0.5f);
    CharacterController& c = *s.world().get<CharacterController>(hero);
    c.desiredVelocity = {4, 0, 0};
    w.step(s, 1.0f / 60);
    CHECK(c.horizontal.x > 0.1f && c.horizontal.x < 1.0f); // acelera, no salta a 4 m/s de golpe
    simulate(w, s, 0.5f);
    CHECK_NEAR(c.horizontal.x, 4, 0.1);
    CHECK_NEAR(s.transform(hero).eulerDegrees().y, -90, 5); // mira hacia +X (su frente es -Z)
    simulate(w, s, 2);                                     // llega a la pared
    CHECK(c.touchingWall);
    CHECK(c.wallNormal.x < -0.9f);                         // la pared mira hacia -X
    CHECK(s.transform(hero).position().x < 5.5f);           // no la atraviesa
    // Doble salto: el segundo en el aire funciona; el tercero no.
    c.desiredVelocity = {};
    c.jumpSpeed = 5;
    simulate(w, s, 0.3f);
    const float y1 = s.transform(hero).position().y;
    c.jumpSpeed = 5;
    simulate(w, s, 0.3f);
    CHECK(s.transform(hero).position().y > y1 + 0.5f);
    CHECK_EQ(c.jumpsUsed, 2);
    const float vBefore = c.velocity.y;
    c.jumpSpeed = 5;
    w.step(s, 1.0f / 60);
    CHECK(c.velocity.y < vBefore); // sin saltos libres: sigue cayendo
    simulate(w, s, 3);
    CHECK(c.grounded && c.jumpsUsed == 0);
    // Salto variable: soltar pronto da un salto más bajo.
    auto peak = [&](bool cut) {
        s.transform(hero).setPosition({0, 0.05f, 0});
        simulate(w, s, 1);
        c.jumpSpeed = 6;
        w.step(s, 1.0f / 60);
        if (cut) c.cutJump = true;
        float top = 0;
        for (int i = 0; i < 90; ++i) {
            w.step(s, 1.0f / 60);
            top = std::max(top, s.transform(hero).position().y);
        }
        return top;
    };
    const float high = peak(false), low = peak(true);
    CHECK(low < high * 0.5f);
}

TEST("personaje: un rayo que ignora al personaje no se choca con su cuerpo interior") {
    Scene s;
    floorOf(s);
    Entity hero = s.createEntity("Héroe");
    s.transform(hero).setPosition({0, 3, 0});
    s.world().add<CharacterController>(hero, {});
    PhysicsWorld w;
    w.build(s);
    w.step(s, 1.0f / 60);
    RaycastHit hit;
    const Vec3 from = s.transform(hero).position() + Vec3{0, 0.1f, 0};
    CHECK(w.raycast(from, {0, -1, 0}, 20, hit, hero));
    CHECK(hit.entity != hero);                  // llega al suelo
    CHECK_NEAR(hit.point.y, 0, 0.02);
    CHECK(w.raycast({0, 10, 0}, {0, -1, 0}, 20, hit)); // sin ignorar: los rayos sí ven al personaje
    CHECK(hit.entity == hero);
}

#include "titan/core/Navigation.h"

TEST("navegación: hornear, rodear una pared con A* y que un agente llegue caminando") {
    Scene s;
    floorOf(s); // 20 x 20 m
    Entity wall = s.createEntity("Pared"); // pared de x = -6 a x = 6 en z = 0, con huecos a los lados
    s.transform(wall).setPosition({0, 1, 0});
    s.transform(wall).setScale({12, 2, 1});
    s.world().add<Collider>(wall);
    Entity enemy = s.createEntity("Enemigo");
    s.transform(enemy).setPosition({0, 0.05f, 5});
    s.world().add<CharacterController>(enemy, {});
    NavAgent agentC;
    agentC.speed = 4;
    s.world().add<NavAgent>(enemy, agentC);
    PhysicsWorld w;
    w.build(s);
    NavSettings ns;
    ns.size = {20, 20};
    ns.cellSize = 0.5f;
    NavGrid grid;
    grid.bake(w, ns, {enemy});
    CHECK(grid.baked());
    // Lo alto de la pared es pisable (se podría estar encima), pero está 2 m más alto: el camino no puede subir.
    CHECK(grid.isWalkable({0, 0, 5}));
    CHECK(grid.isWalkable({8, 0, 0}));   // el hueco junto a la pared
    std::vector<Vec3> path;
    CHECK(grid.findPath({0, 0, 5}, {0, 0, -5}, path));
    CHECK(path.size() >= 3);             // no va en línea recta: rodea
    bool aroundTheSide = false;
    for (const Vec3& p : path) aroundTheSide = aroundTheSide || std::fabs(p.x) > 6.2f;
    CHECK(aroundTheSide);
    CHECK(!grid.findPath({0, 0, 5}, {40, 0, 40}, path) || !path.empty()); // fuera de la zona: el punto caminable más cercano o nada
    // El agente va de verdad hasta el otro lado.
    NavAgent& a = *s.world().get<NavAgent>(enemy);
    a.destination = {0, 0, -5};
    a.hasDestination = true;
    for (int i = 0; i < 60 * 10 && !a.reached; ++i) {
        updateNavAgents(s, grid, 1.0f / 60);
        w.step(s, 1.0f / 60);
    }
    CHECK(a.reached);
    const Vec3 p = s.transform(enemy).position();
    CHECK(p.z < -4.0f && std::fabs(p.x) < 1.5f);
}

namespace {
Entity carAt(Scene& s, Vec3 p) {
    Entity car = s.createEntity("Coche");
    s.transform(car).setPosition(p);
    Collider c;
    c.size = {1.8f, 0.6f, 4.2f};
    s.world().add<Collider>(car, c);
    Rigidbody rb;
    rb.mass = 1200;
    s.world().add<Rigidbody>(car, rb);
    s.world().add<Vehicle>(car);
    for (int i = 0; i < 4; ++i) { // ruedas visibles
        Entity w = s.createEntity("Rueda", car);
        s.world().get<Vehicle>(car)->wheels.push_back(s.info(w).id);
    }
    return car;
}
} // namespace

TEST("vehículo: acelera hacia delante (-Z), gira con el volante y no vuelca") {
    Scene s;
    Entity f = floorOf(s);
    s.transform(f).setScale({400, 1, 400});
    Entity car = carAt(s, {0, 1.2f, 0});
    PhysicsWorld w;
    w.build(s);
    CHECK(w.isVehicle(car));
    simulate(w, s, 1.0f); // se asienta sobre la suspensión
    const Vec3 start = s.transform(car).position();
    Vehicle& v = *s.world().get<Vehicle>(car);
    v.throttle = 1;
    simulate(w, s, 3.0f);
    const Vec3 p = s.transform(car).position();
    CHECK(p.z < start.z - 5.0f);           // avanzó hacia -Z
    CHECK(std::fabs(p.x - start.x) < 1.0f); // en línea recta
    CHECK(v.speedKmh > 40.0f); // sin patinar: ~60 km/h en 3 s
    CHECK(v.rpm > 500.0f && v.gear >= 2); // con agarre normal la caja automática sube de marcha
    const Vec3 up = s.computeWorldMatrix(car).transformDirection({0, 1, 0}).normalized();
    CHECK(up.y > 0.9f);                     // de pie
    // Las ruedas visibles siguen a las físicas: debajo del coche y por delante/detrás.
    const Entity w0 = s.findById(v.wheels[0]);
    CHECK(s.computeWorldMatrix(w0).translation().y < p.y);
    // Volante a la derecha: se desvía hacia +X.
    v.steer = 1;
    simulate(w, s, 2.0f);
    CHECK(s.transform(car).position().x > p.x + 1.0f);
    // Frenar: casi se para.
    v.throttle = 0;
    v.steer = 0;
    v.brake = 1;
    simulate(w, s, 4.0f);
    CHECK(std::fabs(v.speedKmh) < 3.0f);
}

TEST("vehículo: sin Rigidbody dinámico o sin caja no se crea (aviso, sin romper nada)") {
    Scene s;
    floorOf(s);
    Entity e = s.createEntity("Coche roto");
    s.world().add<Vehicle>(e);
    PhysicsWorld w;
    w.build(s);
    CHECK(!w.isVehicle(e));
    simulate(w, s, 0.2f);
}
