// Proyecto de ejemplo "Mundo de bloques" (Demo A del plan): un sandbox con mundo infinito por chunks, biomas,
// cuevas y árboles, en el que se rompen y colocan bloques y todo se guarda. Usa solo lo que trae el motor:
// VoxelTerrain, controlador de personaje, cámara en tercera persona, Lua (Voxel.*) e interfaz.
#include <filesystem>
#include <map>
#include <string>

#include "titan/core/FileIO.h"
#include "titan/core/Log.h"
#include "titan/core/Project.h"
#include "titan/core/Serialization.h"
#include "titan/samples/Samples.h"

using namespace titan;

namespace {

struct LuaFile {
    const char* name;
    const char* source;
};
const LuaFile kScripts[] = {
#include "DemoSandboxScripts.inc" // generado por CMake a partir de engine/samples/sandbox/*.lua
};

Entity shape(Scene& s, const char* name, MeshPrimitive mesh, Vec3 pos, Vec3 scale, Color color, Entity parent = {}) {
    Entity e = s.createEntity(name, parent);
    s.transform(e).setPosition(pos);
    s.transform(e).setScale(scale);
    MeshRenderer mr;
    mr.mesh = mesh;
    mr.color = color;
    s.world().add<MeshRenderer>(e, mr);
    return e;
}

Entity uiText(Scene& s, const char* name, Vec2 anchor, Vec2 pivot, Vec2 pos, Vec2 size, float textSize, int align) {
    Entity e = s.createEntity(name);
    UIRect r;
    r.anchorMin = r.anchorMax = anchor;
    r.pivot = pivot;
    r.position = pos;
    r.size = size;
    s.world().add<UIRect>(e, r);
    UIText t;
    t.size = textSize;
    t.align = align;
    s.world().add<UIText>(e, t);
    return e;
}

bool buildWorld(Project& p, const std::map<std::string, UUID>& scripts, UUID& sceneId) {
    Scene s("mundo");
    Entity env = s.createEntity("Cielo y niebla");
    Environment en;
    en.skyZenith = Color::hex(0x4A8FE0);
    en.skyHorizon = Color::hex(0xD6EAF8);
    en.fogColor = Color::hex(0xD6EAF8);
    en.fogDensity = 0.012f; // la niebla esconde el borde de los chunks cargados
    en.shadows = true;
    en.shadowDistance = 40;
    s.world().add<Environment>(env, en);
    Entity sun = s.createEntity("Sol");
    s.transform(sun).setEulerDegrees({-50, 30, 0});
    Light l;
    l.intensity = 1.1f;
    l.color = Color::hex(0xFFF4E0);
    s.world().add<Light>(sun, l);

    Entity world = s.createEntity("Mundo");
    VoxelTerrain vt;
    vt.viewDistance = 6;
    s.world().add<VoxelTerrain>(world, vt);

    Entity player = s.createEntity("Jugador");
    s.transform(player).setPosition({8.5f, 50, 8.5f}); // el script lo baja hasta el suelo al empezar
    CharacterController cc;
    cc.height = 1.7f;
    cc.radius = 0.3f;
    s.world().add<CharacterController>(player, cc);
    s.world().add<BlobShadow>(player);
    shape(s, "Cuerpo", MeshPrimitive::Capsule, {0, 0.9f, 0}, {0.6f, 1.7f, 0.6f}, Color::hex(0x3B7DD8), player);
    shape(s, "Cabeza", MeshPrimitive::Cube, {0, 1.55f, 0}, {0.4f, 0.4f, 0.4f}, Color::hex(0xF2C9A0), player);
    Script sc{scripts.at("constructor.lua"), true};
    s.world().add<Script>(player, sc);

    Entity cam = s.createEntity("Cámara");
    s.transform(cam).setPosition({8.5f, 54, 15});
    s.transform(cam).setEulerDegrees({-25, 0, 0});
    Camera camera;
    camera.projection = Camera::Projection::Perspective;
    camera.fieldOfView = 65;
    s.world().add<Camera>(cam, camera);
    CameraFollow follow;
    follow.target = s.info(player).id;
    follow.distance = 5;
    follow.pitch = 25;
    follow.height = 1.5f;
    s.world().add<CameraFollow>(cam, follow);

    // Marco del bloque apuntado: un cubo blanco semitransparente un poco más grande que un bloque.
    Entity frame = shape(s, "Bloque apuntado", MeshPrimitive::Cube, {0, -100, 0}, {1.04f, 1.04f, 1.04f}, Color{1, 1, 1, 0.28f});
    s.world().get<MeshRenderer>(frame)->castShadows = false;

    uiText(s, "Texto bloque", {0.5f, 1}, {0.5f, 1}, {0, -24}, {900, 44}, 30, 1);
    uiText(s, "Texto info", {0, 0}, {0, 0}, {20, 16}, {1100, 80}, 22, 0);
    uiText(s, "Ayuda", {1, 1}, {1, 1}, {-20, -16}, {560, 60}, 18, 2);
    s.world().get<UIText>(s.findByName("Ayuda"))->text = "WASD mover · Espacio saltar · J romper · E colocar\nArrastrar: girar la cámara";

    const std::string path = "scenes/mundo.scene";
    const ComponentRegistry reg = ComponentRegistry::createDefault();
    if (p.saveScene(s, reg, path).isNull()) return false;
    sceneId = p.assets().findByPath(path)->id;
    return true;
}

} // namespace

bool titan::createSandboxSample(const std::string& dir, const std::string& name, std::string* error) {
    auto fail = [&](const std::string& why) {
        if (error) *error = why;
        return false;
    };
    std::string err;
    auto p = Project::create(dir, name.empty() ? "Mundo de bloques" : name, ProjectDimension::ThreeD, &err);
    if (!p) return fail(err);
    std::filesystem::create_directories(p->assetsDir() + "/scripts");
    std::map<std::string, UUID> scripts;
    for (const LuaFile& f : kScripts) {
        const std::string rel = std::string("scripts/") + f.name;
        if (!fileio::writeAtomic(p->assetsDir() + "/" + rel, f.source, false)) return fail("no se pudo escribir " + rel);
        scripts[f.name] = p->assets().import(rel);
    }
    UUID scene;
    if (!buildWorld(*p, scripts, scene)) return fail("no se pudo guardar la escena");
    p->setStartScene(scene);
    if (!p->save()) return fail("no se pudo guardar el proyecto");
    return true;
}
