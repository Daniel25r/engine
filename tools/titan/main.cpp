// titan: herramienta de línea de comandos para proyectos de Titán.
// Trabaja sobre los mismos datos y el mismo Core que usarán el editor y el runtime.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

#include <cmath>
#include <filesystem>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "titan/core/FileIO.h"
#include "titan/core/Log.h"
#include "titan/core/Project.h"
#include "titan/core/Version.h"

using namespace titan;

namespace {

int usage() {
    std::printf(
        "%s %s — herramienta de proyectos\n\n"
        "  titan new <carpeta> <nombre> [2d|3d]       crear un proyecto vacío con una escena y una cámara\n"
        "  titan demo <carpeta>                       crear el proyecto de demostración técnica\n"
        "  titan tree <carpeta>                       mostrar la jerarquía de la escena inicial\n"
        "  titan inspect <carpeta> <entidad>          mostrar los componentes de una entidad\n"
        "  titan set <carpeta> <entidad> <Comp.campo> <valores...>\n"
        "                                             cambiar un campo (p. ej. Transform.position 1 2 0)\n"
        "  titan add-entity <carpeta> <nombre> [padre]\n"
        "  titan add-component <carpeta> <entidad> <Componente>\n"
        "  titan assets <carpeta>                     listar los recursos y sus ids\n"
        "  titan check <carpeta>                      validar escenas y referencias\n"
        "  titan package <carpeta> <salida>           generar el paquete de juego para el runtime\n\n"
        "Opción: --scene <ruta dentro de assets/> para trabajar en otra escena.\n",
        kEngineName, kEngineVersion);
    return 2;
}

struct Context {
    std::unique_ptr<Project> project;
    ComponentRegistry registry = ComponentRegistry::createDefault();
    Scene scene;
    UUID sceneId;
};

bool openProject(Context& ctx, const std::string& dir, const std::string& scenePath) {
    std::string err;
    ctx.project = Project::open(dir, &err);
    if (!ctx.project) {
        std::fprintf(stderr, "Error: %s\n", err.c_str());
        return false;
    }
    if (!scenePath.empty()) {
        const AssetRecord* r = ctx.project->assets().findByPath(scenePath);
        if (!r) {
            std::fprintf(stderr, "Error: no existe la escena assets/%s\n", scenePath.c_str());
            return false;
        }
        ctx.sceneId = r->id;
    } else {
        ctx.sceneId = ctx.project->startScene();
    }
    const LoadReport rep = ctx.project->loadScene(ctx.sceneId, ctx.scene, ctx.registry);
    if (!rep.ok()) {
        for (auto& e : rep.errors) std::fprintf(stderr, "Error: %s\n", e.c_str());
        return false;
    }
    return true;
}

bool saveCurrentScene(Context& ctx) {
    const AssetRecord* r = ctx.project->assets().find(ctx.sceneId);
    std::string err;
    if (!saveSceneFile(ctx.scene, ctx.registry, ctx.project->assets().absolutePath(*r), &err)) {
        std::fprintf(stderr, "Error al guardar: %s\n", err.c_str());
        return false;
    }
    return true;
}

Entity findEntity(Context& ctx, const std::string& nameOrId) {
    UUID id;
    if (UUID::parse(nameOrId, id) && !ctx.scene.findById(id).isNull()) return ctx.scene.findById(id);
    Entity e = ctx.scene.findByName(nameOrId);
    if (e.isNull()) std::fprintf(stderr, "Error: no hay ninguna entidad '%s'\n", nameOrId.c_str());
    return e;
}

std::string componentList(Context& ctx, Entity e) {
    std::string out;
    for (auto& t : ctx.registry.types())
        if (t.has(ctx.scene, e)) out += (out.empty() ? "" : ", ") + t.name;
    if (auto* kept = ctx.scene.world().get<PreservedComponents>(e))
        for (auto it = kept->data.begin(); it != kept->data.end(); ++it) out += ", " + it.key() + " (desconocido)";
    return out;
}

void printTree(Context& ctx) {
    std::printf("Escena '%s' — %zu entidades\n", ctx.scene.name().c_str(), ctx.scene.entityCount());
    ctx.scene.traverse([&](Entity e, int depth) {
        const EntityInfo& info = ctx.scene.info(e);
        const Vec3 p = ctx.scene.transform(e).position();
        std::printf("%*s%s %s  [%s]  pos(%g, %g, %g)%s\n", depth * 2, "", depth ? "└─" : "■", info.name.c_str(),
                    componentList(ctx, e).c_str(), p.x, p.y, p.z, info.active ? "" : "  (inactiva)");
    });
}

// ------------------------------------------------------------------ demo

void writeShipTexture(const std::string& path) {
    const int n = 128;
    std::vector<uint8_t> px(n * n * 4, 0);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            // Flecha apuntando hacia arriba con borde suave (la imagen se guarda de arriba abajo).
            const float u = (x + 0.5f) / n - 0.5f, v = 1.0f - (y + 0.5f) / n;
            const float halfWidth = 0.42f * (1.0f - v);
            const bool notch = v < 0.22f && std::fabs(u) < 0.18f * (0.22f - v) / 0.22f;
            const float edge = halfWidth - std::fabs(u);
            if (edge > 0 && v > 0.04f && !notch) {
                uint8_t* p = &px[(y * n + x) * 4];
                const float shade = 0.75f + 0.25f * (1.0f - std::fabs(u) / 0.42f);
                p[0] = p[1] = p[2] = static_cast<uint8_t>(255 * shade);
                p[3] = static_cast<uint8_t>(255 * std::fmin(1.0f, edge * n * 0.8f));
            }
        }
    stbi_write_png(path.c_str(), n, n, 4, px.data(), n * 4);
}

const char* kShipScript = R"(-- Nave: orbita alrededor del centro. Cada toque en la pantalla
-- cambia su color; al tercer toque se carga la escena "nivel2".
local Nave = {}

function Nave:onStart()
    self.radio = 2.5
    self.angulo = 0
    self.toques = 0
    Debug.log("La nave arranca en", self.entity:getPosition())
end

function Nave:onUpdate(dt)
    self.angulo = self.angulo + dt * 0.8
    local x = math.cos(self.angulo) * self.radio
    local y = math.sin(self.angulo) * self.radio
    self.entity:setPosition(x, y, 0)
    -- La punta de la flecha mira en la dirección del movimiento.
    self.entity:setRotation(0, 0, math.deg(self.angulo))

    if Input.tapped() then
        self.toques = self.toques + 1
        local colores = { {0.37, 0.82, 0.78}, {0.91, 0.40, 0.42}, {0.95, 0.71, 0.29} }
        local c = colores[(self.toques - 1) % #colores + 1]
        self.entity:setColor(c[1], c[2], c[3])
        Debug.log("Toque número", self.toques)
        if self.toques >= 3 then Scene.load("nivel2") end
    end
end

return Nave
)";

const char* kSpinScript = R"(-- Gira la entidad sobre su eje Z.
local Girar = {}

function Girar:onUpdate(dt)
    self.entity:rotate(0, 0, 120 * dt)
end

return Girar
)";

const char* kReturnScript = R"(-- Muestra el nivel 2 durante 2 segundos y vuelve a la escena principal.
local Volver = {}

function Volver:onStart()
    self.inicio = Time.time()
    Debug.log("Nivel 2 cargado")
end

function Volver:onUpdate(dt)
    local escala = 1 + 0.2 * math.sin((Time.time() - self.inicio) * 6)
    self.entity:setScale(escala)
    if Time.time() - self.inicio > 2 then Scene.load("principal") end
end

return Volver
)";

int cmdDemo(const std::string& dir) {
    std::string err;
    auto p = Project::create(dir, "Demostración técnica", ProjectDimension::TwoD, &err);
    if (!p) {
        std::fprintf(stderr, "Error: %s\n", err.c_str());
        return 1;
    }
    const ComponentRegistry reg = ComponentRegistry::createDefault();
    std::filesystem::create_directories(p->assetsDir() + "/textures");
    writeShipTexture(p->assetsDir() + "/textures/nave.png");
    fileio::writeAtomic(p->assetsDir() + "/scripts/nave.lua", kShipScript, false);
    fileio::writeAtomic(p->assetsDir() + "/scripts/girar.lua", kSpinScript, false);
    fileio::writeAtomic(p->assetsDir() + "/scripts/volver.lua", kReturnScript, false);
    const UUID tex = p->assets().import("textures/nave.png");
    const UUID shipScript = p->assets().import("scripts/nave.lua");
    const UUID spinScript = p->assets().import("scripts/girar.lua");
    const UUID returnScript = p->assets().import("scripts/volver.lua");

    // Escena principal: raíz, cámara, objeto visual con textura, hijo, decorado y scripts.
    Scene s("principal");
    Entity root = s.createEntity("Mundo");
    Entity cam = s.createEntity("Cámara", root);
    s.transform(cam).setPosition({0, 0, 10});
    Camera c;
    c.orthographicSize = 4.5f;
    c.clearColor = Color::hex(0x141B2B);
    s.world().add<Camera>(cam, c);

    Entity sun = s.createEntity("Núcleo", root);
    SpriteRenderer sunSprite;
    sunSprite.color = Color::hex(0xF2B54A);
    sunSprite.size = {1.2f, 1.2f};
    s.world().add<SpriteRenderer>(sun, sunSprite);
    s.world().add<Script>(sun, {spinScript, true});

    Entity ship = s.createEntity("Nave", root);
    s.transform(ship).setPosition({2.5f, 0, 0});
    SpriteRenderer shipSprite;
    shipSprite.texture = tex;
    shipSprite.color = Color::hex(0x5FD0C8);
    shipSprite.sortingOrder = 2;
    s.world().add<SpriteRenderer>(ship, shipSprite);
    s.world().add<Script>(ship, {shipScript, true});

    Entity shield = s.createEntity("Escudo", ship);
    s.transform(shield).setPosition({0, -0.75f, 0});
    SpriteRenderer shieldSprite;
    shieldSprite.color = Color{1, 1, 1, 0.35f};
    shieldSprite.size = {0.5f, 0.5f};
    shieldSprite.sortingOrder = 1;
    s.world().add<SpriteRenderer>(shield, shieldSprite);
    s.world().add<Script>(shield, {spinScript, true});

    for (int i = 0; i < 24; ++i) {
        Entity star = s.createEntity("Estrella", root);
        const float a = i * 2.39996f, r = 1.6f + 0.25f * i;
        s.transform(star).setPosition({std::cos(a) * r, std::sin(a) * r * 0.7f, 0});
        SpriteRenderer st;
        st.color = Color{0.8f, 0.85f, 1.0f, 0.5f};
        st.size = {0.06f, 0.06f};
        s.world().add<SpriteRenderer>(star, st);
    }
    const UUID mainScene = p->saveScene(s, reg, "scenes/principal.scene", &err);

    Scene s2("nivel2");
    Entity cam2 = s2.createEntity("Cámara");
    s2.transform(cam2).setPosition({0, 0, 10});
    Camera c2 = c;
    c2.clearColor = Color::hex(0x2A1630);
    s2.world().add<Camera>(cam2, c2);
    Entity big = s2.createEntity("Nave grande");
    s2.transform(big).setScale({3, 3, 1});
    SpriteRenderer bigSprite = shipSprite;
    bigSprite.color = Color::hex(0xE8676B);
    s2.world().add<SpriteRenderer>(big, bigSprite);
    s2.world().add<Script>(big, {returnScript, true});
    p->saveScene(s2, reg, "scenes/nivel2.scene", &err);

    p->setStartScene(mainScene);
    if (!p->save(&err)) {
        std::fprintf(stderr, "Error: %s\n", err.c_str());
        return 1;
    }
    std::printf("Proyecto de demostración creado en %s\n", p->root().c_str());
    return 0;
}

// ------------------------------------------------------------------ set

Json parseValue(Context& ctx, const std::vector<std::string>& vals) {
    auto number = [](const std::string& s, double& out) {
        char* end = nullptr;
        out = std::strtod(s.c_str(), &end);
        return end && *end == '\0' && !s.empty();
    };
    if (vals.size() > 1) {
        Json arr = Json::array();
        for (auto& v : vals) {
            double d;
            if (!number(v, d)) return Json(nullptr);
            arr.push_back(d);
        }
        return arr;
    }
    const std::string& v = vals[0];
    double d;
    if (number(v, d)) return (v.find('.') == std::string::npos && v.find('e') == std::string::npos) ? Json(static_cast<long>(d)) : Json(d);
    if (v == "true" || v == "false") return Json(v == "true");
    if (v == "null" || v == "ninguno") return Json(nullptr);
    if (const AssetRecord* r = ctx.project->assets().findByPath(v)) return Json(r->id.toString()); // ruta -> id
    return Json(v);
}

int cmdSet(Context& ctx, Entity e, const std::string& target, const std::vector<std::string>& vals) {
    const size_t dot = target.find('.');
    if (dot == std::string::npos) {
        std::fprintf(stderr, "Error: usa Componente.campo (p. ej. Transform.position)\n");
        return 1;
    }
    const std::string compName = target.substr(0, dot), field = target.substr(dot + 1);
    if (compName == "Entity") {
        if (field == "name") ctx.scene.info(e).name = vals[0];
        else if (field == "active") ctx.scene.info(e).active = vals[0] == "true";
        else { std::fprintf(stderr, "Error: Entity solo tiene name y active\n"); return 1; }
        return saveCurrentScene(ctx) ? 0 : 1;
    }
    const ComponentType* type = ctx.registry.find(compName);
    if (!type) { std::fprintf(stderr, "Error: no existe el componente %s\n", compName.c_str()); return 1; }
    if (!type->has(ctx.scene, e)) { std::fprintf(stderr, "Error: la entidad no tiene %s (usa add-component)\n", compName.c_str()); return 1; }
    Json data = type->save(ctx.scene, e);
    if (!data.contains(field)) { std::fprintf(stderr, "Error: %s no tiene el campo '%s'. Campos: %s\n", compName.c_str(), field.c_str(), data.dump().c_str()); return 1; }
    data[field] = parseValue(ctx, vals);
    LoadReport rep;
    FieldReader reader(data, compName, rep);
    type->load(ctx.scene, e, reader);
    if (!rep.warnings.empty()) {
        for (auto& w : rep.warnings) std::fprintf(stderr, "Error: %s\n", w.c_str());
        return 1; // no se guarda nada si el valor no es válido
    }
    for (UUID ref : rep.assetRefs)
        if (!ctx.project->assets().find(ref)) { std::fprintf(stderr, "Error: el recurso %s no existe\n", ref.toString().c_str()); return 1; }
    if (!saveCurrentScene(ctx)) return 1;
    std::printf("%s.%s = %s\n", compName.c_str(), field.c_str(), type->save(ctx.scene, e)[field].dump().c_str());
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    Log::setPlatformOutput(std::getenv("TITAN_VERBOSE") != nullptr);
    std::vector<std::string> args;
    std::string scenePath;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--scene") == 0 && i + 1 < argc) scenePath = argv[++i];
        else args.emplace_back(argv[i]);
    }
    if (args.size() < 2) return usage();
    const std::string& cmd = args[0];
    const std::string& dir = args[1];

    if (cmd == "new") {
        if (args.size() < 3) return usage();
        std::string err;
        const ProjectDimension dim = (args.size() > 3 && args[3] == "3d") ? ProjectDimension::ThreeD : ProjectDimension::TwoD;
        auto p = Project::create(dir, args[2], dim, &err);
        if (!p) { std::fprintf(stderr, "Error: %s\n", err.c_str()); return 1; }
        Scene s("principal");
        Entity cam = s.createEntity("Cámara");
        s.transform(cam).setPosition({0, dim == ProjectDimension::ThreeD ? 1.0f : 0.0f, 10});
        Camera c;
        if (dim == ProjectDimension::ThreeD) c.projection = Camera::Projection::Perspective;
        s.world().add<Camera>(cam, c);
        p->setStartScene(p->saveScene(s, ComponentRegistry::createDefault(), "scenes/principal.scene", &err));
        if (!p->save(&err)) { std::fprintf(stderr, "Error: %s\n", err.c_str()); return 1; }
        std::printf("Proyecto '%s' creado en %s\n", args[2].c_str(), p->root().c_str());
        return 0;
    }
    if (cmd == "demo") return cmdDemo(dir);

    Context ctx;
    if (!openProject(ctx, dir, scenePath)) return 1;

    if (cmd == "tree") {
        std::printf("Proyecto '%s' (%s)\n", ctx.project->name().c_str(),
                    ctx.project->dimension() == ProjectDimension::ThreeD ? "3D" : "2D");
        printTree(ctx);
        return 0;
    }
    if (cmd == "assets") {
        for (const AssetRecord* r : ctx.project->assets().all())
            std::printf("%s  %-8s %s\n", r->id.toString().c_str(), assetTypeName(r->type), r->path.c_str());
        return 0;
    }
    if (cmd == "inspect" && args.size() >= 3) {
        Entity e = findEntity(ctx, args[2]);
        if (e.isNull()) return 1;
        const EntityInfo& info = ctx.scene.info(e);
        const Entity parent = ctx.scene.parent(e);
        Json out{{"id", info.id.toString()}, {"name", info.name}, {"active", info.active},
                 {"parent", parent.isNull() ? Json(nullptr) : Json(ctx.scene.info(parent).name)}};
        for (auto& t : ctx.registry.types())
            if (t.has(ctx.scene, e)) out["components"][t.name] = t.save(ctx.scene, e);
        std::printf("%s\n", out.dump(2).c_str());
        return 0;
    }
    if (cmd == "set" && args.size() >= 5) {
        Entity e = findEntity(ctx, args[2]);
        if (e.isNull()) return 1;
        return cmdSet(ctx, e, args[3], std::vector<std::string>(args.begin() + 4, args.end()));
    }
    if (cmd == "add-entity" && args.size() >= 3) {
        Entity parent;
        if (args.size() >= 4 && (parent = findEntity(ctx, args[3])).isNull()) return 1;
        Entity e = ctx.scene.createEntity(args[2], parent);
        if (!saveCurrentScene(ctx)) return 1;
        std::printf("Entidad '%s' creada (id %s)\n", args[2].c_str(), ctx.scene.info(e).id.toString().c_str());
        return 0;
    }
    if (cmd == "add-component" && args.size() >= 4) {
        Entity e = findEntity(ctx, args[2]);
        if (e.isNull()) return 1;
        const ComponentType* type = ctx.registry.find(args[3]);
        if (!type) { std::fprintf(stderr, "Error: no existe el componente %s\n", args[3].c_str()); return 1; }
        LoadReport rep;
        const Json empty = Json::object();
        FieldReader reader(empty, args[3], rep);
        type->load(ctx.scene, e, reader);
        if (!saveCurrentScene(ctx)) return 1;
        std::printf("%s añadido a '%s'\n", args[3].c_str(), ctx.scene.info(e).name.c_str());
        return 0;
    }
    if (cmd == "check" || (cmd == "package" && args.size() >= 3)) {
        std::vector<std::string> errors, warnings;
        const std::string out = cmd == "package" ? args[2] : ctx.project->root() + "/.titan/check";
        const bool ok = ctx.project->package(out, ctx.registry, errors, warnings);
        for (auto& w : warnings) std::printf("Aviso: %s\n", w.c_str());
        for (auto& e : errors) std::fprintf(stderr, "Error: %s\n", e.c_str());
        if (ok) std::printf(cmd == "package" ? "Paquete generado en %s\n" : "Proyecto correcto (%s)\n", out.c_str());
        return ok ? 0 : 1;
    }
    return usage();
}
