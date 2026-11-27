#include "titan/core/Templates.h"


namespace titan {

namespace {

Entity addMesh(Scene& s, const std::string& name, MeshPrimitive m, Vec3 pos, Vec3 scale, Color color, Entity parent = {}) {
    Entity e = s.createEntity(name, parent);
    s.transform(e).setPosition(pos);
    s.transform(e).setScale(scale);
    MeshRenderer mr;
    mr.mesh = m;
    mr.color = color;
    s.world().add<MeshRenderer>(e, mr);
    return e;
}

Entity addCamera(Scene& s, bool perspective, Vec3 pos, Vec3 euler, Color clear) {
    Entity cam = s.createEntity("Cámara");
    s.transform(cam).setPosition(pos);
    s.transform(cam).setEulerDegrees(euler);
    Camera c;
    c.projection = perspective ? Camera::Projection::Perspective : Camera::Projection::Orthographic;
    c.clearColor = clear;
    s.world().add<Camera>(cam, c);
    return cam;
}

void addSun(Scene& s) {
    Entity sun = s.createEntity("Sol");
    s.transform(sun).setPosition({0, 6, 0});
    s.transform(sun).setEulerDegrees({-50, 35, 0});
    Light l;
    l.color = Color::hex(0xFFF4E0);
    s.world().add<Light>(sun, l);
}

} // namespace

std::string newScriptSource(const std::string& className) {
    return "-- Script " + className + ". Añádelo a una entidad con el componente Script.\n"
           "local " + className + " = {}\n\n"
           "function " + className + ":onStart()\n"
           "    -- se ejecuta una vez, al empezar\n"
           "end\n\n"
           "function " + className + ":onUpdate(dt)\n"
           "    -- se ejecuta en cada fotograma; dt = segundos desde el anterior\n"
           "    -- ejemplo: self.entity:rotate(0, 90 * dt, 0)\n"
           "end\n\n"
           "return " + className + "\n";
}

std::unique_ptr<Project> createProjectFromTemplate(const std::string& dir, const std::string& name, ProjectTemplate t,
                                                   std::string* error) {
    const bool threeD = t == ProjectTemplate::Empty3D;
    auto p = Project::create(dir, name, threeD ? ProjectDimension::ThreeD : ProjectDimension::TwoD, error);
    if (!p) return nullptr;
    const ComponentRegistry reg = ComponentRegistry::createDefault();
    Scene s("principal");

    if (t == ProjectTemplate::Empty2D) {
        addCamera(s, false, {0, 0, 10}, {}, Color::hex(0x1E1E1E));
    } else {
        addCamera(s, true, {0, 3, 9}, {-15, 0, 0}, Color::hex(0x1E1E1E));
        Entity env = s.createEntity("Cielo y niebla");
        Environment en;
        en.ssao = true;
        en.ssr = true;
        en.taa = true;
        en.hdr = true; // sombras de contacto (solo en los perfiles alto y muy alto)
        s.world().add<Environment>(env, en);
        addSun(s);
        Entity floor = addMesh(s, "Suelo", MeshPrimitive::Plane, {0, 0, 0}, {60, 1, 60}, Color::hex(0x9A9A9A));
        s.world().get<MeshRenderer>(floor)->prototypeGrid = true;
        s.world().get<MeshRenderer>(floor)->roughness = 0.85f;
        Collider floorCol;
        floorCol.size = {1, 0.02f, 1};
        s.world().add<Collider>(floor, floorCol); // suelo sólido
        Entity cube = addMesh(s, "Cubo", MeshPrimitive::Cube, {0, 2.5f, 0}, {1, 1, 1}, Color::hex(0xD9D9D9));
        s.world().get<MeshRenderer>(cube)->prototypeGrid = true;
        s.world().add<Collider>(cube);
        s.world().add<Rigidbody>(cube); // al pulsar Jugar cae al suelo
    }

    const UUID scene = p->saveScene(s, reg, "scenes/principal.scene", error);
    if (scene.isNull()) return nullptr;
    p->setStartScene(scene);
    if (!p->save(error)) return nullptr;
    return p;
}

} // namespace titan
