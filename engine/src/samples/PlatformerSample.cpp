// Proyecto de ejemplo "Isla Estelar" (plantilla del editor y "titan demo-plataformas <carpeta>"): crea un juego de plataformas 3D completo (menú, nivel y victoria) hecho solo con
// lo que trae el motor: primitivas, scripts Lua, sonidos sintetizados, física de personaje, cámara en tercera persona,
// navegación, partículas, interfaz táctil y partida guardada. Los scripts están en tools/titan/demo/plataformas/.
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>
#include <string>

#include "SampleSounds.h"
#include "titan/samples/Samples.h"
#include "titan/audio/AudioBus.h"
#include "titan/core/FileIO.h"
#include "titan/core/Log.h"
#include "titan/core/Project.h"
#include "titan/core/Serialization.h"

using namespace titan;

namespace {

struct LuaFile {
    const char* name;
    const char* source;
};
const LuaFile kScripts[] = {
#include "DemoPlataformasScripts.inc" // generado por CMake a partir de los .lua
};

struct Ctx {
    Project* project = nullptr;
    ComponentRegistry reg = ComponentRegistry::createDefault();
    std::map<std::string, UUID> scripts, sounds;
};

// ---------------------------------------------------------------- ayudas para montar escenas
Entity shape(Scene& s, const char* name, MeshPrimitive mesh, Vec3 pos, Vec3 scale, uint32_t color, Entity parent = {}) {
    Entity e = s.createEntity(name, parent);
    s.transform(e).setPosition(pos);
    s.transform(e).setScale(scale);
    MeshRenderer mr;
    mr.mesh = mesh;
    mr.color = Color::hex(color);
    s.world().add<MeshRenderer>(e, mr);
    return e;
}

// Bloque sólido (suelo, plataforma, muro).
Entity block(Scene& s, const char* name, Vec3 pos, Vec3 size, uint32_t color, Entity parent = {}) {
    Entity e = shape(s, name, MeshPrimitive::Cube, pos, size, color, parent);
    s.world().add<Collider>(e);
    return e;
}

// Isla: césped arriba (con colisión) y tierra debajo (decorado).
Entity island(Scene& s, const char* name, Vec3 center, Vec3 size) {
    Entity top = block(s, name, center, size, 0x6DBE45);
    s.world().get<MeshRenderer>(top)->roughness = 0.85f;
    Entity soil = shape(s, "Tierra", MeshPrimitive::Cube, {0, -1.6f, 0}, {0.94f, 2.2f, 0.94f}, 0x8A5A3C, top);
    s.world().get<MeshRenderer>(soil)->roughness = 0.95f;
    return top;
}

void script(Ctx& c, Scene& s, Entity e, const std::string& file, const std::string& props = "{}") {
    Script sc{c.scripts.at(file), true};
    sc.props = props;
    s.world().add<Script>(e, sc);
}

Entity tree(Scene& s, Vec3 pos, float h) {
    Entity trunk = block(s, "Árbol", {pos.x, pos.y + h * 0.5f, pos.z}, {0.35f, h, 0.35f}, 0x7A4E2D);
    // La copa es hija del tronco: su escala local compensa la del tronco para medir 1,8 × 1,6 × 1,8 m.
    shape(s, "Copa", MeshPrimitive::Sphere, {0, 0.5f + 0.4f / h, 0}, {1.8f / 0.35f, 1.6f / h, 1.8f / 0.35f}, 0x3E9A3A, trunk);
    return trunk;
}

Entity coin(Ctx& c, Scene& s, Vec3 pos) {
    Entity e = shape(s, "Moneda", MeshPrimitive::Cylinder, pos, {0.6f, 0.08f, 0.6f}, 0xFFC83D);
    s.transform(e).setEulerDegrees({90, 0, 0});
    MeshRenderer& mr = *s.world().get<MeshRenderer>(e);
    mr.metallic = 1;
    mr.roughness = 0.3f;
    mr.emission = Color{0.35f, 0.25f, 0.02f, 1};
    Collider col;
    col.shape = Collider::Shape::Sphere;
    col.radius = 0.9f; // escalada por el Transform: ~0,55 m
    col.isTrigger = true;
    s.world().add<Collider>(e, col);
    script(c, s, e, "moneda.lua");
    return e;
}

Entity spikes(Ctx& c, Scene& s, Vec3 pos) {
    Entity base = shape(s, "Pinchos", MeshPrimitive::Cube, {pos.x, pos.y + 0.05f, pos.z}, {1.4f, 0.1f, 1.4f}, 0x555A66);
    for (int i = 0; i < 4; ++i)
        shape(s, "Pincho", MeshPrimitive::Cone, {(i % 2 - 0.5f) * 0.5f, 3.5f, (i / 2 - 0.5f) * 0.5f}, {0.3f, 6.0f, 0.3f},
              0xC8CCD6, base);
    script(c, s, base, "peligro.lua", R"({"radio": 0.7, "alto": 1.4})");
    return base;
}

Entity movingPlatform(Ctx& c, Scene& s, const char* name, Vec3 pos, Vec3 size, const std::string& props) {
    Entity e = block(s, name, pos, size, 0xE0A040);
    Rigidbody rb;
    rb.type = Rigidbody::Type::Kinematic;
    s.world().add<Rigidbody>(e, rb);
    script(c, s, e, "plataforma.lua", props);
    return e;
}

ParticleSystem burst(uint32_t start, uint32_t end, bool additive, float speed, float gravity) {
    ParticleSystem ps;
    ps.playOnStart = false;
    ps.rate = 0;
    ps.maxParticles = 200;
    ps.lifetimeMin = 0.4f;
    ps.lifetimeMax = 0.8f;
    ps.speedMin = speed * 0.5f;
    ps.speedMax = speed;
    ps.spread = 180;
    ps.acceleration = {0, -gravity, 0};
    ps.sizeStart = 0.25f;
    ps.sizeEnd = 0.05f;
    ps.colorStart = Color::hex(start);
    ps.colorEnd = Color::hex(end);
    ps.colorEnd.a = 0;
    ps.additive = additive;
    return ps;
}

// Muñeco del jugador hecho con primitivas; las caderas y los hombros son pivotes que el script hace girar.
void playerModel(Scene& s, Entity root) {
    Entity m = s.createEntity("Modelo del jugador", root);
    shape(s, "Cuerpo del jugador", MeshPrimitive::Capsule, {0, 0.95f, 0}, {1.1f, 0.8f, 0.9f}, 0x3B7DD8, m);
    shape(s, "Cabeza", MeshPrimitive::Sphere, {0, 1.55f, 0}, {0.45f, 0.45f, 0.45f}, 0xF2C9A0, m);
    shape(s, "Gorra", MeshPrimitive::Cylinder, {0, 1.73f, 0}, {0.47f, 0.12f, 0.47f}, 0xD93B3B, m);
    shape(s, "Visera", MeshPrimitive::Cube, {0, 1.69f, -0.24f}, {0.32f, 0.04f, 0.2f}, 0xD93B3B, m);
    for (int side = -1; side <= 1; side += 2) {
        shape(s, "Ojo", MeshPrimitive::Sphere, {side * 0.09f, 1.58f, -0.2f}, {0.07f, 0.09f, 0.05f}, 0x1A1A22, m);
        Entity hip = s.createEntity(side < 0 ? "Cadera izquierda" : "Cadera derecha", m);
        s.transform(hip).setPosition({side * 0.13f, 0.62f, 0});
        shape(s, "Pierna", MeshPrimitive::Cube, {0, -0.3f, 0}, {0.17f, 0.6f, 0.19f}, 0x2A3F6B, hip);
        shape(s, "Zapato", MeshPrimitive::Cube, {0, -0.58f, -0.05f}, {0.2f, 0.1f, 0.3f}, 0x5A3A22, hip);
        Entity shoulder = s.createEntity(side < 0 ? "Hombro izquierdo" : "Hombro derecho", m);
        s.transform(shoulder).setPosition({side * 0.33f, 1.25f, 0});
        shape(s, "Brazo", MeshPrimitive::Cube, {0, -0.24f, 0}, {0.12f, 0.48f, 0.12f}, 0x3B7DD8, shoulder);
        shape(s, "Mano", MeshPrimitive::Sphere, {0, -0.52f, 0}, {0.14f, 0.14f, 0.14f}, 0xF2C9A0, shoulder);
    }
}

void environment(Scene& s, bool shadows = true) {
    Entity env = s.createEntity("Cielo y niebla");
    Environment en;
    en.skyZenith = Color::hex(0x3C82D8);
    en.skyHorizon = Color::hex(0xCFE6F7);
    en.fogColor = Color::hex(0xCFE6F7);
    en.fogDensity = 0.006f;
    en.shadows = shadows;
    en.shadowDistance = 45;
    en.bloom = true;
    en.bloomIntensity = 0.5f;
    en.bloomThreshold = 0.8f;
    en.vignette = 0.2f;
    en.saturation = 1.1f;
    en.ssao = true;
    en.ssr = true; // el mar refleja las islas
    en.taa = true;
    s.world().add<Environment>(env, en);
    Entity sun = s.createEntity("Sol");
    s.transform(sun).setEulerDegrees({-55, 35, 0});
    Light l;
    l.intensity = 1.1f;
    l.color = Color::hex(0xFFF4E0);
    s.world().add<Light>(sun, l);
    Entity sea = shape(s, "Mar", MeshPrimitive::Plane, {0, -10, -30}, {400, 1, 400}, 0x2D7FB8);
    MeshRenderer& mr = *s.world().get<MeshRenderer>(sea);
    mr.roughness = 0.08f;
    mr.water = true; // olas animadas
    mr.waveScale = 2.0f;
    mr.castShadows = false;
}

void music(Ctx& c, Scene& s, float volume) {
    Entity e = s.createEntity("Música");
    AudioSource a;
    a.clip = c.sounds.at("musica");
    a.loop = true;
    a.volume = volume;
    a.bus = "Música";
    s.world().add<AudioSource>(e, a);
}

// ---------------------------------------------------------------- interfaz
Entity uiRect(Scene& s, const char* name, Vec2 anchor, Vec2 pivot, Vec2 pos, Vec2 size, Entity parent = {}) {
    Entity e = s.createEntity(name, parent);
    UIRect r;
    r.anchorMin = r.anchorMax = anchor;
    r.pivot = pivot;
    r.position = pos;
    r.size = size;
    s.world().add<UIRect>(e, r);
    return e;
}

Entity uiText(Scene& s, const char* name, const char* text, float size, Vec2 anchor, Vec2 pivot, Vec2 pos, Vec2 box,
              int align = 1, Entity parent = {}) {
    Entity e = uiRect(s, name, anchor, pivot, pos, box, parent);
    UIText t;
    t.text = text;
    t.size = size;
    t.bold = size >= 30;
    t.align = align;
    s.world().add<UIText>(e, t);
    return e;
}

Entity uiButton(Scene& s, const char* name, const char* label, Vec2 anchor, Vec2 pivot, Vec2 pos, Vec2 size,
                uint32_t color, float labelSize = 34) {
    Entity e = uiRect(s, name, anchor, pivot, pos, size);
    UIImage img;
    img.color = Color::hex(color);
    img.cornerRadius = std::fmin(size.x, size.y) * 0.5f;
    s.world().add<UIImage>(e, img);
    s.world().add<UIButton>(e);
    Entity t = uiRect(s, "Texto", {0.5f, 0.5f}, {0.5f, 0.5f}, {}, size, e);
    UIText txt;
    txt.text = label;
    txt.size = labelSize;
    txt.bold = true;
    s.world().add<UIText>(t, txt);
    return e;
}

Entity perspectiveCamera(Scene& s, Vec3 pos, Vec3 euler) {
    Entity cam = s.createEntity("Cámara");
    s.transform(cam).setPosition(pos);
    s.transform(cam).setEulerDegrees(euler);
    Camera cc;
    cc.projection = Camera::Projection::Perspective;
    cc.fieldOfView = 60;
    s.world().add<Camera>(cam, cc);
    return cam;
}

bool save(Ctx& c, Scene& s, const std::string& name, UUID* id = nullptr) {
    const std::string path = "scenes/" + name + ".scene";
    if (c.project->saveScene(s, c.reg, path).isNull()) {
        TITAN_ERROR("samples", "No se pudo guardar %s", path.c_str());
        return false;
    }
    if (id) *id = c.project->assets().findByPath(path)->id;
    return true;
}

// ---------------------------------------------------------------- nivel
bool buildLevel(Ctx& c) {
    Scene s("nivel1");
    environment(s);
    music(c, s, 0.3f);

    Entity juego = s.createEntity("Juego");
    script(c, s, juego, "juego.lua");

    // Jugador
    Entity player = s.createEntity("Jugador");
    s.transform(player).setPosition({0, 0.05f, 4});
    CharacterController cc;
    cc.height = 1.7f;
    cc.radius = 0.35f;
    cc.maxJumps = 2;
    cc.airControl = 0.65f;
    s.world().add<CharacterController>(player, cc);
    BlobShadow shadow;
    shadow.radius = 0.5f;
    s.world().add<BlobShadow>(player, shadow);
    playerModel(s, player);
    script(c, s, player, "jugador.lua");

    Entity cam = perspectiveCamera(s, {0, 3, 11}, {-20, 0, 0});
    CameraFollow follow;
    follow.target = s.info(player).id;
    follow.distance = 7;
    follow.pitch = 22;
    follow.height = 1.3f;
    s.world().add<CameraFollow>(cam, follow);

    // Efectos compartidos (los scripts los colocan donde hacen falta)
    Entity sparks = s.createEntity("Chispas");
    s.world().add<ParticleSystem>(sparks, burst(0xFFE680, 0xFF9020, true, 4, 6));
    Entity dust = s.createEntity("Polvo");
    ParticleSystem dps = burst(0xD8D2C4, 0xB0A898, false, 1.6f, 1);
    dps.sizeStart = 0.35f;
    dps.sizeEnd = 0.6f;
    dps.spread = 100;
    s.world().add<ParticleSystem>(dust, dps);

    // Zona 1: isla de salida, pinchos y escalones
    island(s, "Isla de salida", {0, -0.5f, 0}, {12, 1, 14});
    tree(s, {-4, 0, 5}, 2.6f);
    tree(s, {4.2f, 0, 3}, 3.0f);
    tree(s, {-4.5f, 0, -4}, 2.4f);
    for (float z : {1.0f, -1.0f, -3.0f}) coin(c, s, {0, 1, z});
    spikes(c, s, {3, 0, -3.5f});
    spikes(c, s, {-2, 0, -5.5f});
    island(s, "Escalón 1", {0, 0.5f, -10}, {3, 1, 3});
    island(s, "Escalón 2", {2.5f, 1.5f, -14}, {3, 1, 3});
    island(s, "Escalón 3", {0, 2.5f, -18}, {3, 1, 3});
    coin(c, s, {0, 2, -10});
    coin(c, s, {2.5f, 3, -14});
    coin(c, s, {0, 4, -18});

    // Plataforma móvil sobre el vacío
    movingPlatform(c, s, "Plataforma móvil", {0, 2.5f, -22.5f}, {3, 0.6f, 3}, R"({"dz": -6, "periodo": 5})");
    coin(c, s, {0, 4.3f, -25.5f});

    // Zona 2: isla con enemigos, bandera de control, barra giratoria y un bloque alto (doble salto)
    island(s, "Isla de los enemigos", {0, 2.5f, -38}, {16, 1, 15});
    tree(s, {-6.5f, 3, -31.5f}, 2.8f);
    tree(s, {6.5f, 3, -32}, 2.5f);
    {
        shape(s, "Mástil", MeshPrimitive::Cylinder, {-5, 4, -33}, {0.1f, 2, 0.1f}, 0xDDDDDD);
        // La tela es la raíz: tiene el trigger (agrandado para compensar su escala) y setColor la pinta de verde.
        Entity flag = shape(s, "Bandera", MeshPrimitive::Cube, {-4.6f, 4.65f, -33}, {0.7f, 0.45f, 0.04f}, 0xE04848);
        Collider trig;
        trig.size = {2 / 0.7f, 2.5f / 0.45f, 2 / 0.04f};
        trig.center = {-0.4f / 0.7f, -0.6f / 0.45f, 0};
        trig.isTrigger = true;
        s.world().add<Collider>(flag, trig);
        s.world().add<ParticleSystem>(flag, burst(0x80FF90, 0x20A040, true, 3, 2));
        script(c, s, flag, "meta.lua", R"({"tipo": "control"})");
    }
    for (Vec3 p : {Vec3{-5, 4, -37}, Vec3{5, 4, -36}, Vec3{0, 4, -40}, Vec3{-5, 4, -43}, Vec3{5, 4, -42}}) coin(c, s, p);
    block(s, "Bloque alto", {6, 5.25f, -44}, {2.5f, 4.5f, 2.5f}, 0xB9B2A8);
    coin(c, s, {6, 8.5f, -44});
    for (Vec3 p : {Vec3{-3, 3, -36}, Vec3{3, 3, -40}}) {
        Entity enemy = s.createEntity("Enemigo");
        s.transform(enemy).setPosition(p);
        CharacterController ec;
        ec.height = 1.1f;
        ec.radius = 0.4f;
        s.world().add<CharacterController>(enemy, ec);
        NavAgent ag;
        ag.speed = 2;
        s.world().add<NavAgent>(enemy, ag);
        BlobShadow es;
        es.radius = 0.5f;
        s.world().add<BlobShadow>(enemy, es);
        s.world().add<ParticleSystem>(enemy, burst(0xFF8080, 0x802020, false, 3, 6));
        shape(s, "Cuerpo del enemigo", MeshPrimitive::Sphere, {0, 0.5f, 0}, {0.9f, 0.9f, 0.9f}, 0xBF3340, enemy);
        for (int side = -1; side <= 1; side += 2) {
            shape(s, "Ojo", MeshPrimitive::Sphere, {side * 0.17f, 0.68f, -0.36f}, {0.18f, 0.22f, 0.1f}, 0xFFFFFF, enemy);
            shape(s, "Pupila", MeshPrimitive::Sphere, {side * 0.16f, 0.66f, -0.42f}, {0.08f, 0.1f, 0.05f}, 0x111111, enemy);
            shape(s, "Pie", MeshPrimitive::Sphere, {side * 0.22f, 0.08f, -0.05f}, {0.3f, 0.16f, 0.4f}, 0x4A2A1A, enemy);
        }
        script(c, s, enemy, "enemigo.lua", R"({"patrulla": 3})");
    }
    {
        Entity post = shape(s, "Eje de la barra", MeshPrimitive::Cylinder, {0, 3.6f, -45}, {0.4f, 1.2f, 0.4f}, 0x555A66);
        Entity bar = shape(s, "Barra giratoria", MeshPrimitive::Cube, {0, 3.55f, -45}, {6, 0.3f, 0.3f}, 0xE04040);
        s.world().get<MeshRenderer>(bar)->emission = Color{0.4f, 0.05f, 0.05f, 1};
        (void)post;
        script(c, s, bar, "peligro.lua", R"({"radio": 0.25, "largo": 3, "giro": 80, "alto": 1})");
    }

    // Ascensor hasta la zona 3
    movingPlatform(c, s, "Ascensor", {0, 2.8f, -47.5f}, {3, 0.6f, 3}, R"({"dy": 5, "periodo": 6, "retraso": 1})");

    // Zona 3: isla alta con plataforma redonda (colisión de malla), pinchos, enemigo y rampa (colisión de malla)
    island(s, "Isla alta", {0, 7.5f, -56}, {12, 1, 12});
    {
        Entity round = shape(s, "Plataforma redonda", MeshPrimitive::Cylinder, {-3, 8.5f, -55}, {3, 1, 3}, 0x9C8CD0);
        Collider mc;
        mc.shape = Collider::Shape::Mesh; // los triángulos del cilindro, no una caja
        s.world().add<Collider>(round, mc);
        for (Vec3 p : {Vec3{-3, 10, -54}, Vec3{-4, 10, -56}, Vec3{-2, 10, -56}}) coin(c, s, p);
    }
    spikes(c, s, {3, 8, -52});
    spikes(c, s, {3.5f, 8, -58.5f});
    {
        Entity enemy = s.createEntity("Enemigo");
        s.transform(enemy).setPosition({2, 8, -55});
        CharacterController ec;
        ec.height = 1.1f;
        ec.radius = 0.4f;
        s.world().add<CharacterController>(enemy, ec);
        NavAgent ag;
        s.world().add<NavAgent>(enemy, ag);
        BlobShadow es;
        s.world().add<BlobShadow>(enemy, es);
        s.world().add<ParticleSystem>(enemy, burst(0xFF8080, 0x802020, false, 3, 6));
        shape(s, "Cuerpo del enemigo", MeshPrimitive::Sphere, {0, 0.5f, 0}, {0.9f, 0.9f, 0.9f}, 0xBF3340, enemy);
        for (int side = -1; side <= 1; side += 2) {
            shape(s, "Ojo", MeshPrimitive::Sphere, {side * 0.17f, 0.68f, -0.36f}, {0.18f, 0.22f, 0.1f}, 0xFFFFFF, enemy);
            shape(s, "Pupila", MeshPrimitive::Sphere, {side * 0.16f, 0.66f, -0.42f}, {0.08f, 0.1f, 0.05f}, 0x111111, enemy);
        }
        script(c, s, enemy, "enemigo.lua", R"({"patrulla": 2, "rapido": 4.6})");
    }
    {
        const float angle = std::atan2(4.0f, 8.0f);
        Entity ramp = shape(s, "Rampa", MeshPrimitive::Cube, {0, 10 - 0.2f / std::cos(angle), -64},
                            {3, 0.4f, std::sqrt(80.0f)}, 0xC9A26B);
        s.transform(ramp).setEulerDegrees({angle * 57.2958f, 0, 0});
        Collider mc;
        mc.shape = Collider::Shape::Mesh;
        s.world().add<Collider>(ramp, mc);
        coin(c, s, {0, 10, -62});
        coin(c, s, {0, 11.5f, -65.5f});
    }

    // Meta
    island(s, "Isla de la meta", {0, 11.5f, -71}, {6, 1, 6});
    coin(c, s, {-1.6f, 13, -70});
    coin(c, s, {1.6f, 13, -70});
    {
        Entity star = shape(s, "Estrella", MeshPrimitive::Cube, {0, 13.4f, -72}, {0.9f, 0.9f, 0.25f}, 0xFFD84A);
        s.transform(star).setEulerDegrees({0, 0, 45});
        MeshRenderer& mr = *s.world().get<MeshRenderer>(star);
        mr.emission = Color{1, 0.8f, 0.2f, 1};
        mr.metallic = 1;
        mr.roughness = 0.25f;
        Entity tip = shape(s, "Punta", MeshPrimitive::Cube, {0, 0, 0}, {1, 1, 1}, 0xFFD84A, star);
        s.transform(tip).setEulerDegrees({0, 0, 45});
        s.world().get<MeshRenderer>(tip)->emission = Color{1, 0.8f, 0.2f, 1};
        Collider trig;
        trig.shape = Collider::Shape::Sphere;
        trig.radius = 1.4f;
        trig.isTrigger = true;
        s.world().add<Collider>(star, trig);
        ParticleSystem ps = burst(0xFFF0A0, 0xFF8000, true, 7, 3);
        ps.maxParticles = 300;
        ps.lifetimeMax = 1.4f;
        s.world().add<ParticleSystem>(star, ps);
        Entity glow = s.createEntity("Luz de la estrella", star);
        Light l;
        l.type = Light::Type::Point;
        l.color = Color::hex(0xFFD060);
        l.intensity = 3;
        l.range = 8;
        s.world().add<Light>(glow, l);
        script(c, s, star, "meta.lua", R"({"tipo": "meta"})");
    }

    // Interfaz: marcadores, mensaje central, joystick, salto y menú
    uiText(s, "Texto monedas", "Monedas 0 / 0", 34, {0, 0}, {0, 0}, {28, 20}, {360, 50}, 0);
    uiText(s, "Texto vidas", "Vidas 3", 34, {0, 0}, {0, 0}, {28, 66}, {300, 50}, 0);
    uiText(s, "Texto tiempo", "0:00", 34, {0.5f, 0}, {0.5f, 0}, {0, 20}, {200, 50});
    uiText(s, "Mensaje", "", 40, {0.5f, 0}, {0.5f, 0}, {0, 110}, {900, 60});
    Entity joy = uiRect(s, "Joystick", {0, 1}, {0, 1}, {40, -40}, {230, 230});
    s.world().add<UIJoystick>(joy);
    uiButton(s, "Saltar", "Saltar", {1, 1}, {1, 1}, {-50, -60}, {170, 170}, 0x3B7DD8);
    uiButton(s, "Menú", "Menú", {1, 0}, {1, 0}, {-24, 20}, {150, 64}, 0x2A3346, 28);

    return save(c, s, "nivel1");
}

// ---------------------------------------------------------------- menú y victoria
bool buildMenu(Ctx& c, UUID& id) {
    Scene s("menu");
    environment(s);
    music(c, s, 0.35f);
    perspectiveCamera(s, {0, 2.2f, 6}, {-10, 0, 0});
    island(s, "Isla", {0, -0.5f, 0}, {10, 1, 8});
    tree(s, {-3.5f, 0, -1.5f}, 2.8f);
    tree(s, {3.8f, 0, -2.2f}, 2.4f);
    Entity who = s.createEntity("Jugador del menú");
    s.transform(who).setPosition({-1.3f, 0, 1});
    s.transform(who).setEulerDegrees({0, 200, 0});
    playerModel(s, who);
    Entity big = shape(s, "Moneda del menú", MeshPrimitive::Cylinder, {1.3f, 1.4f, 0.5f}, {1.2f, 0.15f, 1.2f}, 0xFFC83D);
    s.transform(big).setEulerDegrees({90, 0, 0});
    s.world().get<MeshRenderer>(big)->metallic = 1;
    s.world().get<MeshRenderer>(big)->roughness = 0.3f;
    s.world().get<MeshRenderer>(big)->emission = Color{0.35f, 0.25f, 0.02f, 1};

    Entity menu = s.createEntity("Menú");
    script(c, s, menu, "menu.lua");
    uiText(s, "Título", "Isla Estelar", 92, {0.5f, 0}, {0.5f, 0}, {0, 50}, {900, 110});
    uiText(s, "Subtítulo", "Demo de plataformas 3D hecha con Titán", 28, {0.5f, 0}, {0.5f, 0}, {0, 160}, {900, 40});
    uiButton(s, "Jugar", "Jugar", {0.5f, 1}, {0.5f, 1}, {0, -170}, {320, 96}, 0x3B7DD8, 44);
    uiButton(s, "Borrar progreso", "Borrar progreso", {0.5f, 1}, {0.5f, 1}, {0, -90}, {280, 56}, 0x3A2A33, 24);
    uiText(s, "Mejor marca", "", 26, {0.5f, 1}, {0.5f, 1}, {0, -290}, {1000, 40});
    uiText(s, "Controles",
           "Joystick o WASD: mover  ·  Saltar o Espacio: salto y doble salto  ·  Arrastra la pantalla: cámara  ·  "
           "Pisa a los enemigos",
           20, {0.5f, 1}, {0.5f, 1}, {0, -20}, {1240, 40});
    return save(c, s, "menu", &id);
}

bool buildVictory(Ctx& c) {
    Scene s("victoria");
    environment(s, false);
    perspectiveCamera(s, {0, 2, 6}, {-6, 0, 0});
    island(s, "Isla", {0, -0.5f, 0}, {8, 1, 6});
    Entity star = shape(s, "Estrella de la victoria", MeshPrimitive::Cube, {0, 1.8f, 0}, {1.2f, 1.2f, 0.3f}, 0xFFD84A);
    s.transform(star).setEulerDegrees({0, 0, 45});
    s.world().get<MeshRenderer>(star)->emission = Color{1, 0.8f, 0.2f, 1};
    s.world().get<MeshRenderer>(star)->metallic = 1;
    Entity tip = shape(s, "Punta", MeshPrimitive::Cube, {0, 0, 0}, {1, 1, 1}, 0xFFD84A, star);
    s.transform(tip).setEulerDegrees({0, 0, 45});
    s.world().get<MeshRenderer>(tip)->emission = Color{1, 0.8f, 0.2f, 1};
    Entity fx = s.createEntity("Fuegos");
    ParticleSystem ps = burst(0xFFFFFF, 0xFF40C0, true, 6, 4);
    ps.maxParticles = 400;
    ps.lifetimeMax = 1.3f;
    s.world().add<ParticleSystem>(fx, ps);
    Entity v = s.createEntity("Victoria");
    script(c, s, v, "victoria.lua");
    uiText(s, "Título", "¡Victoria!", 96, {0.5f, 0}, {0.5f, 0}, {0, 40}, {900, 110});
    uiText(s, "Resultado", "", 38, {0.5f, 0}, {0.5f, 0}, {0, 160}, {1000, 50});
    uiText(s, "Récord", "", 30, {0.5f, 0}, {0.5f, 0}, {0, 214}, {1000, 44});
    uiButton(s, "Jugar otra vez", "Jugar otra vez", {0.5f, 1}, {1, 1}, {-15, -50}, {300, 90}, 0x3B7DD8, 34);
    uiButton(s, "Volver al menú", "Menú", {0.5f, 1}, {0, 1}, {15, -50}, {300, 90}, 0x2A3346, 34);
    return save(c, s, "victoria");
}

} // namespace

bool titan::createPlatformerSample(const std::string& dir, const std::string& name, std::string* error) {
    auto fail = [&](const std::string& why) {
        if (error) *error = why;
        return false;
    };
    std::string err;
    auto p = Project::create(dir, name.empty() ? "Isla Estelar" : name, ProjectDimension::ThreeD, &err);
    if (!p) return fail(err);
    Ctx c;
    c.project = p.get();
    std::filesystem::create_directories(p->assetsDir() + "/scripts");
    std::filesystem::create_directories(p->assetsDir() + "/sounds");
    for (const LuaFile& f : kScripts) {
        const std::string rel = std::string("scripts/") + f.name;
        if (!fileio::writeAtomic(p->assetsDir() + "/" + rel, f.source, false)) return fail("no se pudo escribir " + rel);
        c.scripts[f.name] = p->assets().import(rel);
    }
    for (const auto& [name, data] : samples::platformerSounds()) {
        const std::string rel = "sounds/" + name + ".wav";
        if (!fileio::writeAtomic(p->assetsDir() + "/" + rel, std::string(data.begin(), data.end()), false))
            return fail("no se pudo escribir " + rel);
        c.sounds[name] = p->assets().import(rel);
    }
    // Mezclador: música y efectos por separado (el menú del juego podría bajarlos), un poco de sala en los efectos y
    // un limitador en Master para que muchas monedas a la vez no saturen.
    {
        AudioBusLayout mix = AudioBusLayout::defaultLayout();
        mix.buses[1].volumeDb = -3;
        AudioBusEffectSettings room;
        room.type = AudioEffectType::Reverb;
        room.params = {{"room_size", 0.35f}, {"wet", 0.18f}, {"predelay_ms", 30}};
        mix.buses[2].effects.push_back(room);
        AudioBusEffectSettings lim;
        lim.type = AudioEffectType::Limiter;
        mix.buses[0].effects.push_back(lim);
        if (!fileio::writeAtomic(p->assetsDir() + "/audio_buses.json", mix.toJson(), false))
            return fail("no se pudo escribir audio_buses.json");
        p->assets().import("audio_buses.json");
    }
    UUID menu;
    if (!buildMenu(c, menu) || !buildLevel(c) || !buildVictory(c)) return fail("no se pudieron guardar las escenas");
    p->setStartScene(menu);
    if (!p->save()) return fail("no se pudo guardar el proyecto");
    return true;
}
