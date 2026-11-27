// Núcleo del editor: ciclo de vida, entrada, proyectos, deshacer, modo Play.
#include <GLES3/gl3.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "EditorImpl.h"
#include "titan/core/TerrainTools.h"
#include "backends/imgui_impl_opengl3.h"
#include "imgui_internal.h"
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
#include "titan/core/FileIO.h"
#include "titan/core/ModelImport.h"
#include "titan/core/Templates.h"
#include "titan/samples/Samples.h"
#include "titan/core/Version.h"

namespace fs = std::filesystem;

namespace titan {

bool ProjectFiles::read(const std::string& path, std::vector<uint8_t>& out) {
    if (!root.empty()) {
        std::ifstream f(root + "/" + path, std::ios::binary);
        if (f) {
            out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
            return true;
        }
    }
    return engine && engine->read(path, out);
}

namespace {
EditorPlatform* gClipboardPlatform = nullptr;
std::string gClipboardText;
} // namespace

// ============================================================ Ciclo de vida

bool Editor::Impl::init() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // la distribución la decide el editor según el tamaño de pantalla
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    if (platform.isTouchScreen()) io.ConfigFlags |= ImGuiConfigFlags_IsTouchScreen;
    // PC sin escala del sistema (100 %): la interfaz por defecto un poco más grande. Si Windows ya escala (125 %,
    // 150 %...), no se multiplica otra vez: con 150 % × 125 % todo salía enorme.
    uiZoom = platform.isTouchScreen() || platform.uiScale() > 1.1f ? 1.0f : 1.25f;
    loadPreferences();
    scale = std::max(1.0f, platform.uiScale()) * uiZoom;
    loadFonts();
    applyStyle();
    if (!ImGui_ImplOpenGL3_Init("#version 300 es")) return false;

    gClipboardPlatform = &platform;
    ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
    pio.Platform_GetClipboardTextFn = [](ImGuiContext*) -> const char* {
        gClipboardText = gClipboardPlatform->clipboard();
        return gClipboardText.c_str();
    };
    pio.Platform_SetClipboardTextFn = [](ImGuiContext*, const char* text) { gClipboardPlatform->setClipboard(text); };

    files.engine = &platform.engineFiles();
    platformLogPath = platform.projectsDirectory() + "/titan-registro.txt";
    if (!Log::openFile(platformLogPath)) platformLogPath.clear();
    if (!renderer.init(files)) return false;
    editorQuality = QualitySettings::preset(recommendedQuality(renderer.caps()));
    renderer.setQuality(editorQuality);

    logSink = Log::addSink([this](const LogMessage& m) {
        logs.push_back(m);
        if (logs.size() > 2000) logs.pop_front();
        scrollConsole = true;
    });

    // Reabrir el último proyecto.
    std::string last;
    if (fileio::readText(projectsDir() + "/.ultimo_proyecto", last) && !last.empty() && fs::exists(last + "/project.titan"))
        openProject(last);
    initialized = true;
    TITAN_INFO("editor", "%s %s — editor listo", kEngineName, kEngineVersion);
    return true;
}

void Editor::Impl::loadPreferences() {
    std::string text;
    if (!fileio::readText(projectsDir() + "/.preferencias.json", text)) return;
    const Json j = Json::parse(text, nullptr, false);
    if (j.is_object() && j.contains("tamano_interfaz") && j["tamano_interfaz"].is_number())
        uiZoom = std::clamp(j["tamano_interfaz"].get<float>(), 0.75f, 2.5f);
}

void Editor::Impl::savePreferences() {
    Json j;
    j["tamano_interfaz"] = uiZoom;
    fileio::writeAtomic(projectsDir() + "/.preferencias.json", j.dump(2), false);
}

void Editor::Impl::setUiZoom(float zoom) {
    zoom = std::clamp(std::round(zoom * 20.0f) / 20.0f, 0.75f, 2.5f);
    if (zoom == uiZoom) return;
    uiZoom = zoom;
    fontsDirty = true;
    savePreferences();
    setStatus("Tamaño de la interfaz: " + std::to_string(int(std::lround(zoom * 100))) + " %");
}

void Editor::Impl::loadFonts() {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    ImFont* ui = nullptr;
    for (const auto& path : platform.uiFontCandidates())
        if (fs::exists(path) && (ui = io.Fonts->AddFontFromFileTTF(path.c_str(), 17.0f * scale))) break;
    if (!ui) {
        ImFontConfig cfg;
        cfg.SizePixels = 13.0f * scale;
        io.Fonts->AddFontDefault(&cfg);
    }
    monoFont = nullptr;
    for (const auto& path : platform.monoFontCandidates())
        if (fs::exists(path) && (monoFont = io.Fonts->AddFontFromFileTTF(path.c_str(), 16.0f * scale))) break;
}

void Editor::Impl::applyStyle() {
    ImGuiStyle& st = ImGui::GetStyle();
    st = ImGuiStyle();
    ImGui::StyleColorsDark(&st);
    st.WindowRounding = 0;
    st.ChildRounding = 2;
    st.FrameRounding = 3;
    st.PopupRounding = 3;
    st.GrabRounding = 3;
    st.TabRounding = 3;
    st.ScrollbarRounding = 3;
    st.WindowBorderSize = 1;
    st.FrameBorderSize = 1;
    st.PopupBorderSize = 1;
    st.TabBorderSize = 0;
    st.WindowPadding = ImVec2(6, 6);
    st.FramePadding = ImVec2(6, 4);
    st.ItemSpacing = ImVec2(5, 5);
    st.ItemInnerSpacing = ImVec2(5, 4);
    st.CellPadding = ImVec2(5, 3);
    st.ScrollbarSize = platform.isTouchScreen() ? 18 : 12;
    st.GrabMinSize = 12;
    st.IndentSpacing = 16;
    st.SeparatorTextBorderSize = 1;
    if (platform.isTouchScreen()) {
        st.FramePadding = ImVec2(8, 7);
        st.ItemSpacing = ImVec2(7, 7);
        st.TouchExtraPadding = ImVec2(4, 4);
    }
    st.ScaleAllSizes(scale);

    // Grises neutros oscuros, campos hundidos y acento azul para la selección.
    auto g = [](float v, float a = 1.0f) { return ImVec4(v, v, v, a); };
    const ImVec4 blue(0.0f, 0.44f, 0.88f, 1), blueSoft(0.0f, 0.44f, 0.88f, 0.55f);
    ImVec4* c = st.Colors;
    c[ImGuiCol_Text] = g(0.80f);
    c[ImGuiCol_TextDisabled] = g(0.50f);
    c[ImGuiCol_WindowBg] = g(0.141f);
    c[ImGuiCol_ChildBg] = g(0.118f);
    c[ImGuiCol_PopupBg] = g(0.10f, 0.98f);
    c[ImGuiCol_Border] = g(0.06f);
    c[ImGuiCol_BorderShadow] = g(0, 0);
    c[ImGuiCol_FrameBg] = g(0.059f);
    c[ImGuiCol_FrameBgHovered] = g(0.09f);
    c[ImGuiCol_FrameBgActive] = g(0.11f);
    c[ImGuiCol_TitleBg] = g(0.08f);
    c[ImGuiCol_TitleBgActive] = g(0.08f);
    c[ImGuiCol_MenuBarBg] = g(0.082f);
    c[ImGuiCol_ScrollbarBg] = g(0.10f);
    c[ImGuiCol_ScrollbarGrab] = g(0.30f);
    c[ImGuiCol_ScrollbarGrabHovered] = g(0.38f);
    c[ImGuiCol_ScrollbarGrabActive] = g(0.45f);
    c[ImGuiCol_CheckMark] = g(0.92f);
    c[ImGuiCol_SliderGrab] = blue;
    c[ImGuiCol_SliderGrabActive] = blue;
    c[ImGuiCol_Button] = g(0.22f);
    c[ImGuiCol_ButtonHovered] = g(0.30f);
    c[ImGuiCol_ButtonActive] = blueSoft;
    c[ImGuiCol_Header] = g(0.18f);
    c[ImGuiCol_HeaderHovered] = g(0.24f);
    c[ImGuiCol_HeaderActive] = blueSoft;
    c[ImGuiCol_Separator] = g(0.06f);
    c[ImGuiCol_ResizeGrip] = g(0, 0);
    c[ImGuiCol_Tab] = g(0.10f);
    c[ImGuiCol_TabHovered] = g(0.24f);
    c[ImGuiCol_TabSelected] = g(0.20f);
    c[ImGuiCol_TabSelectedOverline] = blue;
    c[ImGuiCol_TabDimmed] = g(0.10f);
    c[ImGuiCol_TabDimmedSelected] = g(0.16f);
    c[ImGuiCol_TableHeaderBg] = g(0.10f);
    c[ImGuiCol_TableBorderStrong] = g(0.06f);
    c[ImGuiCol_TableBorderLight] = g(0.08f);
    c[ImGuiCol_TableRowBg] = g(0, 0);
    c[ImGuiCol_TableRowBgAlt] = g(1, 0.025f);
    c[ImGuiCol_TextSelectedBg] = ImVec4(0.0f, 0.44f, 0.88f, 0.40f);
    c[ImGuiCol_DragDropTarget] = blue;
    c[ImGuiCol_NavCursor] = blue;
}

void Editor::Impl::frame(int w, int h, float dt) {
    updateSoundCheck(dt);
    fbWidth = std::max(w, 1);
    fbHeight = std::max(h, 1);
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(float(fbWidth), float(fbHeight));
    io.DeltaTime = std::max(dt, 1e-4f);
    if (statusTime > 0) statusTime -= dt;

    if (fontsDirty) { // tamaño de la interfaz cambiado: fuentes y estilo de nuevo, antes de empezar el fotograma
        fontsDirty = false;
        scale = std::max(1.0f, platform.uiScale()) * uiZoom;
        loadFonts();
        applyStyle();
        ImGui_ImplOpenGL3_DestroyFontsTexture();
        ImGui_ImplOpenGL3_CreateFontsTexture();
    }
    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
    touchScroll();
    handleShortcuts();
    noScrollWidgetActive = false;
    drawUI();
    ImGui::Render();

    static bool lastWantText = false;
    if (io.WantTextInput != lastWantText) {
        lastWantText = io.WantTextInput;
        platform.setTextInputActive(lastWantText);
    }

    glViewport(0, 0, fbWidth, fbHeight);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.141f, 0.141f, 0.141f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (vpVisible && project) renderViewport();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void Editor::Impl::setStatus(const std::string& s) {
    status = s;
    statusTime = 4;
}

std::string Editor::Impl::projectsDir() {
    const std::string d = platform.projectsDirectory();
    std::error_code ec;
    fs::create_directories(d, ec);
    return d;
}

// ============================================================ Proyecto

bool Editor::Impl::openProject(const std::string& path) {
    if (runtime) stop();
    if (project) save();
    std::string err;
    auto p = Project::open(path, &err);
    if (!p) {
        TITAN_ERROR("editor", "No se pudo abrir el proyecto: %s", err.c_str());
        setStatus("No se pudo abrir el proyecto");
        return false;
    }
    project = std::move(p);
    files.root = project->root();
    resources.clear();
    resources.bind(&renderer, &project->assets(), &files);
    undoStack.clear();
    redoStack.clear();
    codePath.clear();
    code.clear();
    codeDirty = false;
    if (!loadScene(project->startScene())) {
        scene.clear();
        sceneId = {};
    }
    // Encuadre inicial de la cámara del editor.
    cam2Center = {0, 0};
    cam2Size = 6;
    cam3Target = {0, 0, 0};
    cam3Yaw = 0.6f;
    cam3Pitch = 0.55f;
    cam3Dist = 14;
    showBrowser = false;
    codeTabActive = false;
    fileio::writeAtomic(projectsDir() + "/.ultimo_proyecto", project->root(), false);
    setStatus("Proyecto '" + project->name() + "' abierto");
    return true;
}

void Editor::Impl::closeProject() {
    if (runtime) stop();
    if (project) save();
    resources.clear();
    resources.bind(&renderer, nullptr); // la base de datos de recursos se va con el proyecto
    project.reset();
    scene.clear();
    sceneId = {};
    selected = {};
    showBrowser = true;
}

bool Editor::Impl::loadScene(UUID id) {
    if (!project) return false;
    const LoadReport rep = project->loadScene(id, scene, registry);
    if (!rep.ok()) {
        setStatus("No se pudo abrir la escena");
        return false;
    }
    sceneId = id;
    selected = {};
    dirty = false;
    undoStack.clear();
    redoStack.clear();
    TransformSystem::update(scene);
    return true;
}

bool Editor::Impl::save() {
    if (!project) return false;
    bool ok = true;
    std::string err;
    if (const AssetRecord* r = project->assets().find(sceneId)) {
        if (!saveSceneFile(scene, registry, project->assets().absolutePath(*r), &err)) {
            TITAN_ERROR("editor", "No se pudo guardar la escena: %s", err.c_str());
            ok = false;
        }
    }
    ok = saveCode() && ok;
    ok = project->save(&err) && ok;
    if (ok) {
        dirty = false;
        setStatus("Guardado");
    }
    return ok;
}

bool Editor::Impl::saveCode() {
    if (!project || codePath.empty() || !codeDirty) return true;
    std::string err;
    if (!fileio::writeAtomic(project->assetsDir() + "/" + codePath, code, true, &err)) {
        TITAN_ERROR("editor", "No se pudo guardar %s: %s", codePath.c_str(), err.c_str());
        return false;
    }
    codeDirty = false;
    propsCache.clear(); // las variables del script pueden haber cambiado
    return true;
}

void Editor::Impl::exportPackage() {
    if (!project) return;
    save();
    std::vector<std::string> errors, warnings;
    const std::string out = project->root() + "/build/paquete";
    if (project->package(out, registry, errors, warnings)) {
        TITAN_INFO("editor", "Paquete del juego listo en %s", out.c_str());
        TITAN_INFO("editor", "Para crear el APK: ./gradlew assembleRelease -PtitanPackage=%s (ver docs/BUILD_INSTRUCTIONS.md)", out.c_str());
        setStatus("Paquete exportado (ver Consola)");
    } else {
        for (auto& e : errors) TITAN_ERROR("editor", "Exportar: %s", e.c_str());
        setStatus("La exportación tiene errores (ver Consola)");
    }
    for (auto& w : warnings) TITAN_WARN("editor", "Exportar: %s", w.c_str());
}

// ============================================================ Selección

Entity Editor::Impl::selectedEntity() { return selected.isNull() ? Entity{} : scene.findById(selected); }

void Editor::Impl::select(Entity e) { selected = scene.isValid(e) ? scene.info(e).id : UUID{}; }

bool Editor::Impl::localBounds(Entity e, Vec3& min, Vec3& max) {
    const MeshRenderer* mr = scene.world().get<MeshRenderer>(e);
    if (!mr) return false;
    if (!mr->model.isNull() && renderer.modelBounds(editorModel(mr->model), min, max)) return true;
    min = {-0.5f, -0.5f, -0.5f};
    max = {0.5f, 0.5f, 0.5f};
    return true;
}

const std::vector<Editor::Impl::ScriptProp>& Editor::Impl::scriptProps(AssetRef script) {
    auto it = propsCache.find(script.value);
    if (it != propsCache.end()) return it->second;
    std::vector<ScriptProp>& out = propsCache[script.value];
    const AssetRecord* r = project ? project->assets().find(script) : nullptr;
    std::string code;
    if (!r || !fileio::readText(project->assetsDir() + "/" + r->path, code)) return out;
    // Intérprete aislado y con límite de instrucciones: un bucle infinito en el script no congela el editor.
    lua_State* L = luaL_newstate();
    luaL_requiref(L, LUA_GNAME, luaopen_base, 1);
    luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1);
    luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);
    lua_settop(L, 0);
    lua_sethook(L, [](lua_State* s, lua_Debug*) { luaL_error(s, "demasiado largo"); }, LUA_MASKCOUNT, 2000000);
    if (luaL_loadbufferx(L, code.data(), code.size(), "props", "t") == LUA_OK && lua_pcall(L, 0, 1, 0) == LUA_OK &&
        lua_istable(L, -1)) {
        lua_getfield(L, -1, "props");
        if (lua_istable(L, -1)) {
            lua_pushnil(L);
            while (lua_next(L, -2)) {
                if (lua_type(L, -2) == LUA_TSTRING) {
                    ScriptProp p;
                    p.name = lua_tostring(L, -2);
                    if (lua_type(L, -1) == LUA_TNUMBER) { p.type = 0; p.number = lua_tonumber(L, -1); out.push_back(p); }
                    else if (lua_type(L, -1) == LUA_TBOOLEAN) { p.type = 1; p.boolean = lua_toboolean(L, -1); out.push_back(p); }
                    else if (lua_type(L, -1) == LUA_TSTRING) { p.type = 2; p.text = lua_tostring(L, -1); out.push_back(p); }
                }
                lua_pop(L, 1);
            }
        }
    }
    lua_close(L);
    std::sort(out.begin(), out.end(), [](const ScriptProp& a, const ScriptProp& b) { return a.name < b.name; });
    return out;
}

void Editor::Impl::savePrefab(Entity e) {
    if (!project || e.isNull()) return;
    std::string base;
    for (char c : scene.info(e).name) base += (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-') ? c : '_';
    if (base.empty()) base = "prefab";
    const std::string rel = "prefabs/" + base + ".prefab";
    std::string err;
    if (!fileio::writeAtomic(project->assetsDir() + "/" + rel, serializePrefab(scene, e, registry), true, &err)) {
        TITAN_ERROR("editor", "No se pudo guardar el prefab: %s", err.c_str());
        return;
    }
    project->assets().import(rel);
    TITAN_INFO("editor", "Prefab guardado: %s (úsalo con Scene.instantiate(\"%s\", x, y, z))", rel.c_str(), base.c_str());
    setStatus("Prefab guardado: " + rel);
}

void Editor::Impl::saveMaterial(Entity e) {
    MeshRenderer* m = project && !e.isNull() ? scene.world().get<MeshRenderer>(e) : nullptr;
    if (!m) return;
    std::string base;
    for (char c : scene.info(e).name) base += (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-') ? c : '_';
    if (base.empty()) base = "material";
    std::string rel = "materiales/" + base + ".mat";
    for (int i = 2; project->assets().findByPath(rel); ++i) rel = "materiales/" + base + "_" + std::to_string(i) + ".mat";
    std::string err;
    if (!fileio::writeAtomic(project->assetsDir() + "/" + rel, serializeMaterial(*m), true, &err)) {
        TITAN_ERROR("editor", "No se pudo guardar el material: %s", err.c_str());
        return;
    }
    project->assets().import(rel);
    if (const AssetRecord* r = project->assets().findByPath(rel)) {
        pushUndo();
        m->material = r->id;
    }
    setStatus("Material guardado: " + rel + " (asígnalo a otros objetos en Detalles)");
}

void Editor::Impl::instantiatePrefabAsset(const AssetRecord& r) {
    std::string text;
    if (!fileio::readText(project->assetsDir() + "/" + r.path, text)) return;
    pushUndo();
    const Entity e = instantiatePrefab(text, scene, {}, registry);
    if (e.isNull()) {
        setStatus("El prefab no se pudo leer (ver Registro)");
        return;
    }
    Vec3 p = spawnPoint();
    if (threeD()) p.y = scene.transform(e).position().y + 0.0f;
    scene.transform(e).setPosition(p);
    select(e);
    dirty = true;
}

void Editor::Impl::fitCollider(Entity e) {
    Collider* c = scene.world().get<Collider>(e);
    const MeshRenderer* mr = scene.world().get<MeshRenderer>(e);
    if (!c || !mr) return;
    if (mr->model.isNull() && mr->mesh == MeshPrimitive::Sphere) {
        *c = Collider{};
        c->shape = Collider::Shape::Sphere;
        c->radius = 0.5f;
        return;
    }
    if (mr->model.isNull() && mr->mesh == MeshPrimitive::Capsule) {
        *c = Collider{};
        c->shape = Collider::Shape::Capsule;
        c->radius = 0.25f;
        c->height = 1.0f;
        return;
    }
    Vec3 mn, mx;
    if (!localBounds(e, mn, mx)) return;
    c->shape = Collider::Shape::Box;
    c->size = {std::max(mx.x - mn.x, 0.02f), std::max(mx.y - mn.y, 0.02f), std::max(mx.z - mn.z, 0.02f)};
    c->center = (mn + mx) * 0.5f;
}

void Editor::Impl::addModelToScene(const AssetRecord& r) {
    const int m = editorModel(r.id);
    Vec3 mn, mx;
    if (m < 0 || !renderer.modelBounds(m, mn, mx)) {
        setStatus("No se pudo cargar el modelo (ver Registro)");
        return;
    }
    pushUndo();
    const std::string name = r.path.substr(r.path.find_last_of('/') + 1);
    Entity e = scene.createEntity(name.substr(0, name.rfind('.')));
    MeshRenderer mr;
    mr.model = r.id;
    scene.world().add<MeshRenderer>(e, mr);
    // Se escala para que mida unos 2 metros y se apoya sobre el suelo.
    const Vec3 size = mx - mn;
    const float big = std::max(size.x, std::max(size.y, size.z));
    const float k = big > 1e-5f ? 2.0f / big : 1.0f;
    Vec3 p = spawnPoint();
    if (threeD()) p.y = -mn.y * k;
    scene.transform(e).setPosition(p);
    scene.transform(e).setScale({k, k, k});
    if (renderer.modelAnimationCount(m) > 0) scene.world().add<Animator>(e); // con esqueleto y clips: lista para animar
    select(e);
    dirty = true;
    setStatus("Modelo añadido: " + name);
}

// ============================================================ Deshacer

void Editor::Impl::beginEdit() {
    if (pendingEdit.empty()) pendingEdit = serializeScene(scene, registry);
}

void Editor::Impl::commitEdit() {
    if (pendingEdit.empty()) return;
    undoStack.push_back({std::move(pendingEdit), selected});
    pendingEdit.clear();
    if (undoStack.size() > 100) undoStack.erase(undoStack.begin());
    redoStack.clear();
    dirty = true;
}

void Editor::Impl::pushUndo() {
    pendingEdit.clear();
    beginEdit();
    commitEdit();
}

void Editor::Impl::track() {
    if (ImGui::IsItemActivated()) beginEdit();
    if (ImGui::IsItemDeactivatedAfterEdit()) commitEdit();
    else if (ImGui::IsItemDeactivated()) pendingEdit.clear(); // se tocó pero no cambió nada
    if (ImGui::IsItemActive()) noScrollWidgetActive = true;
}

void Editor::Impl::restore(const Snapshot& s) {
    deserializeScene(s.scene, scene, registry);
    selected = scene.findById(s.selected).isNull() ? UUID{} : s.selected;
    TransformSystem::update(scene);
    dirty = true;
}

void Editor::Impl::undo() {
    if (undoStack.empty() || runtime) return;
    redoStack.push_back({serializeScene(scene, registry), selected});
    Snapshot s = std::move(undoStack.back());
    undoStack.pop_back();
    restore(s);
    setStatus("Deshecho");
}

void Editor::Impl::redo() {
    if (redoStack.empty() || runtime) return;
    undoStack.push_back({serializeScene(scene, registry), selected});
    Snapshot s = std::move(redoStack.back());
    redoStack.pop_back();
    restore(s);
    setStatus("Rehecho");
}

// ============================================================ Operaciones sobre entidades

Vec3 Editor::Impl::spawnPoint() const {
    if (threeD()) return {cam3Target.x, 0.5f, cam3Target.z};
    return {cam2Center.x, cam2Center.y, 0};
}

Entity Editor::Impl::createEntityOfKind(const std::string& kind) {
    pushUndo();
    Entity e = scene.createEntity(kind);
    scene.transform(e).setPosition(spawnPoint());
    auto mesh = [&](MeshPrimitive m) {
        MeshRenderer mr;
        mr.mesh = m;
        mr.color = Color::hex(0xD0D4DC);
        scene.world().add<MeshRenderer>(e, mr);
    };
    if (kind == "Sprite") {
        scene.world().add<SpriteRenderer>(e);
    } else if (kind == "Cubo") mesh(MeshPrimitive::Cube);
    else if (kind == "Esfera") mesh(MeshPrimitive::Sphere);
    else if (kind == "Plano") {
        mesh(MeshPrimitive::Plane);
        scene.transform(e).setPosition({spawnPoint().x, 0, spawnPoint().z});
        scene.transform(e).setScale({5, 1, 5});
    } else if (kind == "Cilindro") mesh(MeshPrimitive::Cylinder);
    else if (kind == "Cono") mesh(MeshPrimitive::Cone);
    else if (kind == "Cápsula") mesh(MeshPrimitive::Capsule);
    else if (kind == "Cámara") {
        Camera c;
        if (threeD()) {
            c.projection = Camera::Projection::Perspective;
            scene.transform(e).setPosition({0, 3, 8});
            scene.transform(e).setEulerDegrees({-15, 0, 0});
        } else {
            scene.transform(e).setPosition({spawnPoint().x, spawnPoint().y, 10});
        }
        scene.world().add<Camera>(e, c);
    } else if (kind == "Luz direccional") {
        scene.transform(e).setPosition({0, 5, 0});
        scene.transform(e).setEulerDegrees({-50, 30, 0});
        scene.world().add<Light>(e);
    } else if (kind == "Cubo con física" || kind == "Esfera con física") {
        mesh(kind[0] == 'C' ? MeshPrimitive::Cube : MeshPrimitive::Sphere);
        scene.transform(e).setPosition(spawnPoint() + Vec3{0, 3, 0});
        scene.world().add<Collider>(e);
        fitCollider(e);
        scene.world().add<Rigidbody>(e);
    } else if (kind == "Enemigo") {
        // Personaje rojo con agente de navegación; su script decide a quién perseguir (ver docs/TITAN_ENGINE_API.md).
        scene.transform(e).setPosition({spawnPoint().x + 3, 0, spawnPoint().z});
        scene.world().add<CharacterController>(e);
        scene.world().add<NavAgent>(e);
        scene.world().add<BlobShadow>(e);
        Entity body = scene.createEntity("Cuerpo", e);
        scene.transform(body).setPosition({0, 0.9f, 0});
        scene.transform(body).setScale({0.7f, 1.8f, 0.7f});
        MeshRenderer mr;
        mr.mesh = MeshPrimitive::Capsule;
        mr.color = Color::hex(0xD64545);
        scene.world().add<MeshRenderer>(body, mr);
    } else if (kind == "Cámara en tercera persona") {
        // Sigue al objeto seleccionado o, si no hay, al primer personaje de la escena.
        Entity target = selectedEntity();
        if (target.isNull())
            scene.world().each<CharacterController>([&](Entity o, CharacterController&) { if (target.isNull()) target = o; });
        Camera cam;
        cam.projection = Camera::Projection::Perspective;
        cam.priority = 10; // por encima de la cámara normal de la plantilla
        scene.world().add<Camera>(e, cam);
        CameraFollow f;
        if (!target.isNull()) f.target = scene.info(target).id;
        scene.world().add<CameraFollow>(e, f);
        scene.transform(e).setPosition(spawnPoint() + Vec3{0, 3, 6});
    } else if (kind == "Terreno") {
        Terrain t;
        terrainEnsureData(t);
        generateTerrain(t, 7, 0.25f); // colinas suaves para empezar; se esculpe con las herramientas de Detalles
        scene.transform(e).setPosition({0, 0, 0});
        scene.world().add<Terrain>(e, t);
    } else if (kind == "Mundo de bloques") {
        scene.transform(e).setPosition({0, 0, 0});
        scene.world().add<VoxelTerrain>(e);
    } else if (kind == "Personaje") {
        // Controlador de personaje con una cápsula visible de hijo (el origen de la entidad son los pies).
        scene.transform(e).setPosition({spawnPoint().x, 0, spawnPoint().z});
        scene.world().add<CharacterController>(e);
        scene.world().add<BlobShadow>(e);
        Entity body = scene.createEntity("Cuerpo", e);
        scene.transform(body).setPosition({0, 0.9f, 0});
        scene.transform(body).setScale({0.7f, 1.8f, 0.7f});
        MeshRenderer mr;
        mr.mesh = MeshPrimitive::Capsule;
        mr.color = Color::hex(0x3C8DE0);
        scene.world().add<MeshRenderer>(body, mr);
    } else if (kind == "Zona trigger") {
        Collider c;
        c.size = {3, 2, 3};
        c.isTrigger = true;
        scene.transform(e).setPosition(spawnPoint() + Vec3{0, 0.5f, 0});
        scene.world().add<Collider>(e, c);
    } else if (kind == "Cielo y niebla") {
        scene.world().add<Environment>(e);
    } else if (kind == "Texto" || kind == "Imagen" || kind == "Panel" || kind == "Botón" || kind == "Barra de vida" ||
               kind == "Joystick") {
        // Interfaz de juego. Medidas en píxeles de una pantalla de 720 de alto.
        World& w = scene.world();
        auto rect = [](Vec2 anchor, Vec2 pos, Vec2 size) {
            UIRect r;
            r.anchorMin = r.anchorMax = r.pivot = anchor;
            r.position = pos;
            r.size = size;
            return r;
        };
        auto stretch = [](Vec2 inset) {
            UIRect r;
            r.anchorMin = {0, 0};
            r.anchorMax = {1, 1};
            r.size = {-inset.x * 2, -inset.y * 2};
            return r;
        };
        if (kind == "Texto") {
            w.add<UIRect>(e, rect({0.5f, 0}, {0, 24}, {600, 60}));
            UIText t;
            t.text = "Puntos: 0";
            t.size = 40;
            t.bold = true;
            w.add<UIText>(e, t);
        } else if (kind == "Imagen") {
            w.add<UIRect>(e, rect({0.5f, 0.5f}, {0, 0}, {200, 200}));
            w.add<UIImage>(e);
        } else if (kind == "Panel") {
            w.add<UIRect>(e, rect({0.5f, 0.5f}, {0, 0}, {560, 360}));
            UIImage img;
            img.color = {0.07f, 0.09f, 0.13f, 0.88f};
            img.cornerRadius = 24;
            w.add<UIImage>(e, img);
        } else if (kind == "Botón") {
            w.add<UIRect>(e, rect({1, 1}, {-40, -40}, {220, 84}));
            UIImage img;
            img.color = Color::hex(0x2F7DF6);
            img.cornerRadius = 20;
            w.add<UIImage>(e, img);
            w.add<UIButton>(e);
            Entity label = scene.createEntity("Texto", e);
            w.add<UIRect>(label, stretch({0, 0}));
            UIText t;
            t.text = "Botón";
            t.size = 34;
            t.bold = true;
            w.add<UIText>(label, t);
        } else if (kind == "Barra de vida") {
            w.add<UIRect>(e, rect({0, 0}, {28, 28}, {340, 30}));
            UIImage back;
            back.color = {0, 0, 0, 0.55f};
            back.cornerRadius = 15;
            w.add<UIImage>(e, back);
            Entity fill = scene.createEntity("Relleno", e);
            w.add<UIRect>(fill, stretch({4, 4}));
            UIImage front;
            front.color = Color::hex(0x3DDC84);
            front.cornerRadius = 11;
            w.add<UIImage>(fill, front);
        } else {
            w.add<UIRect>(e, rect({0, 1}, {48, -48}, {230, 230}));
            w.add<UIJoystick>(e);
        }
    } else if (kind == "Fuego" || kind == "Humo" || kind == "Chispas" || kind == "Lluvia" || kind == "Nieve" ||
               kind == "Explosión" || kind == "Magia") {
        // Efectos de partida: valores pensados para verse bien a escala humana (1 unidad = 1 metro).
        ParticleSystem p;
        if (kind == "Fuego") {
            p.rate = 60; p.lifetimeMin = 0.5f; p.lifetimeMax = 1.1f; p.speedMin = 0.8f; p.speedMax = 1.8f; p.spread = 12;
            p.shape = 1; p.shapeSize = {0.25f, 0.25f, 0.25f}; p.sizeStart = 0.7f; p.sizeEnd = 0.15f;
            p.colorStart = {1, 0.5f, 0.12f, 0.55f}; p.colorEnd = {0.9f, 0.1f, 0.02f, 0}; p.additive = true;
            p.acceleration = {0, 1.0f, 0}; p.rotationSpeed = 90;
        } else if (kind == "Humo") {
            p.rate = 12; p.lifetimeMin = 3; p.lifetimeMax = 5; p.speedMin = 0.4f; p.speedMax = 0.9f; p.spread = 15;
            p.sizeStart = 0.6f; p.sizeEnd = 2.4f; p.colorStart = {0.55f, 0.55f, 0.58f, 0.55f}; p.colorEnd = {0.7f, 0.7f, 0.72f, 0};
            p.acceleration = {0.2f, 0.15f, 0}; p.rotationSpeed = 30; p.drag = 0.2f;
        } else if (kind == "Chispas") {
            p.rate = 40; p.lifetimeMin = 0.4f; p.lifetimeMax = 0.9f; p.speedMin = 3; p.speedMax = 6; p.spread = 40;
            p.sizeStart = 0.08f; p.sizeEnd = 0.02f; p.colorStart = {1, 0.85f, 0.4f, 1}; p.colorEnd = {1, 0.4f, 0.1f, 0};
            p.acceleration = {0, -9.8f, 0}; p.additive = true;
        } else if (kind == "Lluvia") {
            p.rate = 400; p.maxParticles = 1500; p.lifetimeMin = p.lifetimeMax = 1.2f; p.speedMin = 9; p.speedMax = 11;
            p.spread = 3; p.shape = 2; p.shapeSize = {20, 0.1f, 20}; p.sizeStart = p.sizeEnd = 0.05f;
            p.colorStart = p.colorEnd = {0.7f, 0.8f, 0.95f, 0.5f};
            scene.transform(e).setPosition(spawnPoint() + Vec3{0, 12, 0});
            scene.transform(e).setEulerDegrees({180, 0, 0}); // emite hacia abajo
        } else if (kind == "Nieve") {
            p.rate = 120; p.maxParticles = 1500; p.lifetimeMin = 6; p.lifetimeMax = 9; p.speedMin = 0.6f; p.speedMax = 1.2f;
            p.spread = 20; p.shape = 2; p.shapeSize = {20, 0.1f, 20}; p.sizeStart = p.sizeEnd = 0.1f;
            p.colorStart = {1, 1, 1, 0.9f}; p.colorEnd = {1, 1, 1, 0.6f}; p.acceleration = {0.3f, 0, 0};
            scene.transform(e).setPosition(spawnPoint() + Vec3{0, 10, 0});
            scene.transform(e).setEulerDegrees({180, 0, 0});
        } else if (kind == "Explosión") {
            p.rate = 0; p.burst = 120; p.duration = 2; p.lifetimeMin = 0.4f; p.lifetimeMax = 1.0f; p.speedMin = 4;
            p.speedMax = 9; p.spread = 180; p.sizeStart = 0.8f; p.sizeEnd = 0.1f; p.colorStart = {1, 0.75f, 0.3f, 1};
            p.colorEnd = {0.6f, 0.1f, 0.05f, 0}; p.drag = 3; p.additive = true;
        } else { // Magia
            p.rate = 50; p.lifetimeMin = 1; p.lifetimeMax = 2; p.speedMin = 0.2f; p.speedMax = 0.6f; p.spread = 180;
            p.shape = 1; p.shapeSize = {0.8f, 0.8f, 0.8f}; p.sizeStart = 0.15f; p.sizeEnd = 0.0f;
            p.colorStart = {0.4f, 0.7f, 1, 1}; p.colorEnd = {0.8f, 0.3f, 1, 0}; p.acceleration = {0, 0.6f, 0}; p.additive = true;
        }
        if (kind != "Lluvia" && kind != "Nieve") scene.transform(e).setPosition(spawnPoint() + Vec3{0, 0.5f, 0});
        scene.world().add<ParticleSystem>(e, p);
    } else if (kind == "Foco") {
        Light l;
        l.type = Light::Type::Spot;
        l.range = 15;
        l.intensity = 2;
        scene.transform(e).setPosition(spawnPoint() + Vec3{0, 4, 0});
        scene.transform(e).setEulerDegrees({-90, 0, 0}); // apuntando hacia abajo
        scene.world().add<Light>(e, l);
    } else if (kind == "Luz puntual") {
        Light l;
        l.type = Light::Type::Point;
        scene.transform(e).setPosition(spawnPoint() + Vec3{0, 1.5f, 0});
        scene.world().add<Light>(e, l);
    }
    select(e);
    dirty = true;
    return e;
}

void Editor::Impl::focusSelected() {
    Entity e = selectedEntity();
    if (e.isNull()) return;
    const Mat4 world = scene.computeWorldMatrix(e);
    Vec3 mn, mx;
    float radius = 1.0f;
    Vec3 center = world.translation();
    if (localBounds(e, mn, mx)) {
        const AABB box = transformAABB(AABB{mn, mx}, world);
        center = box.center();
        radius = std::max(0.25f, box.extents().length());
    } else if (const Terrain* t = scene.world().get<Terrain>(e)) {
        const AABB box = transformAABB(terrainBounds(*t), world);
        center = box.center();
        radius = box.extents().length();
    }
    if (threeD()) {
        cam3Target = center;
        cam3Dist = std::clamp(radius * 2.6f, 1.5f, 1500.0f); // que quepa en la vista con algo de margen
    } else {
        cam2Center = {center.x, center.y};
        cam2Size = std::clamp(radius * 1.4f, 0.5f, 500.0f);
    }
    setStatus("Enfocado: " + scene.info(e).name);
}

void Editor::Impl::deleteSelected() {
    Entity e = selectedEntity();
    if (e.isNull()) return;
    pushUndo();
    scene.destroyEntityImmediate(e);
    selected = {};
}

void Editor::Impl::duplicateSelected() {
    Entity src = selectedEntity();
    if (src.isNull()) return;
    pushUndo();
    Entity dup = copySubtree(scene, src, scene, scene.parent(src), registry);
    scene.info(dup).name += " (copia)";
    if (threeD()) scene.transform(dup).translate({1, 0, 0});
    else scene.transform(dup).translate({0.5f, -0.5f, 0});
    select(dup);
}

// ============================================================ Código y recursos

void Editor::Impl::openScriptFile(const std::string& rel) {
    if (!project) return;
    saveCode();
    std::string text;
    if (!fileio::readText(project->assetsDir() + "/" + rel, text)) {
        setStatus("No se pudo abrir " + rel);
        return;
    }
    codePath = rel;
    code = text;
    codeDirty = false;
    codeCheck.clear();
    codeErrorLine = 0;
    codeTabActive = true;
}

void Editor::Impl::importFile(const std::string& path, const std::string& name) {
    if (!project) return;
    std::string base = name.empty() ? fs::path(path).filename().string() : name;
    for (char& c : base)
        if (c == '/' || c == '\\' || c == ':') c = '_';
    // OBJ, STL, PLY, FBX y .blend se convierten a GLB (lo que carga el motor); se importa el GLB.
    const bool convert = isConvertibleModel(base);
    if (convert) base = fs::path(base).stem().string() + ".glb";
    std::string rel = "imported/" + base;
    for (int i = 2; fs::exists(project->assetsDir() + "/" + rel); ++i)
        rel = "imported/" + fs::path(base).stem().string() + "_" + std::to_string(i) + fs::path(base).extension().string();
    std::error_code ec;
    fs::create_directories(project->assetsDir() + "/imported", ec);
    if (convert) {
        ImportReport rep;
        std::string err;
        setStatus("Convirtiendo " + fs::path(path).filename().string() + "...");
        if (!convertModelToGlb(path, project->assetsDir() + "/" + rel, &rep, &err)) {
            TITAN_ERROR("editor", "No se pudo importar %s: %s", path.c_str(), err.c_str());
            setStatus("Error al importar: " + err);
            return;
        }
        for (const std::string& w : rep.warnings) TITAN_WARN("editor", "%s: %s", fs::path(path).filename().string().c_str(), w.c_str());
    } else {
        fs::copy_file(path, project->assetsDir() + "/" + rel, ec);
    }
    if (ec) {
        TITAN_ERROR("editor", "No se pudo importar %s: %s", path.c_str(), ec.message().c_str());
        setStatus("Error al importar");
        return;
    }
    const UUID id = project->assets().import(rel);
    const AssetRecord* r = project->assets().find(id);
    TITAN_INFO("editor", "Importado %s como %s", rel.c_str(), r ? assetTypeName(r->type) : "?");
    if (r && r->type == AssetType::Unknown)
        TITAN_WARN("editor", "%s: el motor todavía no sabe usar este tipo de archivo", rel.c_str());
    setStatus("Importado " + rel);
    panel = Panel::Content;
    bottomIsConsole = false;
}

// ============================================================ Play

void Editor::Impl::play() {
    if (!project || runtime) return;
    saveCode(); // los scripts se leen del disco
    playStart = std::chrono::steady_clock::now();
    playFirstFrame = true;
    runtime = std::make_unique<Runtime>();
    const std::string text = serializeScene(scene, registry);
    const AssetRecord* r = project->assets().find(sceneId);
    const size_t errorsBefore = logs.size();
    (void)errorsBefore;
    // La partida de prueba se guarda fuera de assets/ (no va en el paquete del juego).
    {
        std::error_code ec;
        std::filesystem::create_directories(project->root() + "/.titan", ec);
        runtime->setSavePath(project->root() + "/.titan/partida.json");
    }
    // Mismo Renderer y misma caché que el editor: Jugar no recarga nada de lo que ya se ve en la vista.
    if (!runtime->startEmbedded(files, project->assets(), text, r ? r->path : scene.name(), &renderer, &resources)) {
        runtime.reset();
        setStatus("No se pudo ejecutar la escena (ver Consola)");
        return;
    }
    runtime->showStats = false; // se activa desde el menú de la vista
    playTouches.clear();
    codeTabActive = false;
    playPaused = false;
    setStatus("Jugando: los cambios hechos durante el juego se descartan al detener");
}

void Editor::Impl::stop() {
    if (!runtime) return;
    runtime->shutdown();
    runtime.reset();
    renderer.setQuality(editorQuality); // por si el juego cambió la calidad con Quality.set
    setStatus("Juego detenido");
}

// ============================================================ Entrada

void Editor::Impl::handleShortcuts() {
    ImGuiIO& io = ImGui::GetIO();
    if (medit.active && !runtime) { // en modelado, las teclas son las del modelado (E, I, X, Tab...)
        meshEditKeys();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
            meshEditSave();
            save();
        }
        return;
    }
    // Tamaño de la interfaz: en cualquier pantalla (también el navegador de proyectos y escribiendo).
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Equal, false)) setUiZoom(uiZoom + 0.125f);
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Minus, false)) setUiZoom(uiZoom - 0.125f);
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_0, false)) setUiZoom(1.25f);
    if (io.WantTextInput) {
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) save();
        return;
    }
    if (!project) return;
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) save();
    if (!runtime && !io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Tab, false)) { // Tab: modelar el objeto seleccionado
        if (Entity sel = selectedEntity(); !sel.isNull() && scene.world().has<MeshRenderer>(sel)) enterMeshEdit(sel);
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) undo();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) redo();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) duplicateSelected();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_P, false)) runtime ? stop() : play();
    if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Delete, false)) deleteSelected();
    if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false) && !runtime) focusSelected();
    if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Q, false)) tool = Tool::Select;
    if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_W, false)) tool = Tool::Translate;
    if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_E, false)) tool = Tool::Rotate;
    if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_R, false)) tool = Tool::Scale;
}

void Editor::Impl::touchScroll() {
    // En pantallas táctiles, arrastrar un panel lo desplaza (salvo si se arrastra un control).
    if (!platform.isTouchScreen() || imguiPointer < 0) return;
    ImGuiContext& g = *ImGui::GetCurrentContext();
    const ImVec2 delta = ImGui::GetIO().MouseDelta;
    dragDistance += std::fabs(delta.x) + std::fabs(delta.y);
    if (dragDistance < 10.0f * scale || noScrollWidgetActive) return;
    ImGuiWindow* w = g.HoveredWindow;
    while (w && w->ScrollMax.y <= 0 && w->ScrollMax.x <= 0 && (w->Flags & ImGuiWindowFlags_ChildWindow)) w = w->ParentWindow;
    if (!w || (w->ScrollMax.y <= 0 && w->ScrollMax.x <= 0)) return;
    if (w->ScrollMax.y > 0) ImGui::SetScrollY(w, std::clamp(w->Scroll.y - delta.y, 0.0f, w->ScrollMax.y));
    if (w->ScrollMax.x > 0) ImGui::SetScrollX(w, std::clamp(w->Scroll.x - delta.x, 0.0f, w->ScrollMax.x));
    if (g.ActiveId != 0 && !noScrollWidgetActive) ImGui::ClearActiveID(); // no convertir el desplazamiento en un toque
}

// ============================================================ Editor (interfaz pública)

Editor::Editor(EditorPlatform& platform) : impl_(std::make_unique<Impl>(platform)) {}

Editor::~Editor() { shutdown(); }

bool Editor::init() { return impl_->init(); }

void Editor::frame(int w, int h, float dt) {
    if (impl_->initialized) impl_->frame(w, h, dt);
}

void Editor::shutdown() {
    Impl& m = *impl_;
    if (!m.initialized) return;
    m.stop();
    if (m.project) m.save();
    Log::removeSink(m.logSink);
    m.renderer.shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext();
    m.initialized = false;
}

void Editor::onPointerDown(int id, float x, float y) {
    Impl& m = *impl_;
    if (!m.initialized) return;
    const ImVec2 p(x, y);
    const bool popup = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    const bool onViewportBar = p.x >= m.vpBarMin.x && p.y >= m.vpBarMin.y && p.x < m.vpBarMax.x && p.y < m.vpBarMax.y;
    if (m.insideViewport(p) && !popup && !onViewportBar && m.imguiPointer != id) {
        m.viewportPointerDown(id, p);
        return;
    }
    if (m.imguiPointer >= 0) return; // la interfaz atiende un dedo cada vez
    m.imguiPointer = id;
    m.dragDistance = 0;
    ImGuiIO& io = ImGui::GetIO();
    io.AddMouseSourceEvent(m.platform.isTouchScreen() ? ImGuiMouseSource_TouchScreen : ImGuiMouseSource_Mouse);
    io.AddMousePosEvent(x, y);
    io.AddMouseButtonEvent(0, true);
}

void Editor::onPointerMove(int id, float x, float y) {
    Impl& m = *impl_;
    if (!m.initialized) return;
    for (auto& t : m.touches)
        if (t.id == id) {
            m.viewportPointerMove(id, ImVec2(x, y));
            return;
        }
    if (m.imguiPointer == id || (m.imguiPointer < 0 && !m.platform.isTouchScreen())) ImGui::GetIO().AddMousePosEvent(x, y);
}

void Editor::onPointerUp(int id, float x, float y) {
    Impl& m = *impl_;
    if (!m.initialized) return;
    for (auto& t : m.touches)
        if (t.id == id) {
            m.viewportPointerUp(id, ImVec2(x, y));
            return;
        }
    if (m.imguiPointer != id) return;
    m.imguiPointer = -1;
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(x, y);
    io.AddMouseButtonEvent(0, false);
    // Sin ratón no hay "hover": se aparta el puntero para que nada quede resaltado.
    if (m.platform.isTouchScreen()) io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
}

void Editor::onScroll(float x, float y, float amount) {
    Impl& m = *impl_;
    if (!m.initialized) return;
    if (m.insideViewport(ImVec2(x, y)) && !m.runtime) m.viewportScroll(amount);
    else ImGui::GetIO().AddMouseWheelEvent(0, amount);
}

void Editor::onText(const std::string& utf8) {
    if (impl_->initialized) ImGui::GetIO().AddInputCharactersUTF8(utf8.c_str());
}

void Editor::onKey(EditorKey key, bool down) {
    if (!impl_->initialized) return;
    static const ImGuiKey map[] = {
        ImGuiKey_Tab, ImGuiKey_LeftArrow, ImGuiKey_RightArrow, ImGuiKey_UpArrow, ImGuiKey_DownArrow, ImGuiKey_PageUp,
        ImGuiKey_PageDown, ImGuiKey_Home, ImGuiKey_End, ImGuiKey_Delete, ImGuiKey_Backspace, ImGuiKey_Enter,
        ImGuiKey_Escape, ImGuiKey_LeftCtrl, ImGuiKey_LeftShift, ImGuiKey_LeftAlt, ImGuiKey_A, ImGuiKey_C, ImGuiKey_V,
        ImGuiKey_X, ImGuiKey_Y, ImGuiKey_Z, ImGuiKey_S, ImGuiKey_D, ImGuiKey_F, ImGuiKey_P, ImGuiKey_N, ImGuiKey_Q,
        ImGuiKey_W, ImGuiKey_E, ImGuiKey_R, ImGuiKey_Equal, ImGuiKey_Minus, ImGuiKey_0,
        ImGuiKey_I, ImGuiKey_1, ImGuiKey_3};
    ImGuiIO& io = ImGui::GetIO();
    const ImGuiKey k = map[static_cast<int>(key)];
    if (key == EditorKey::Ctrl) io.AddKeyEvent(ImGuiMod_Ctrl, down);
    if (key == EditorKey::Shift) io.AddKeyEvent(ImGuiMod_Shift, down);
    if (key == EditorKey::Alt) io.AddKeyEvent(ImGuiMod_Alt, down);
    io.AddKeyEvent(k, down);
}

void Editor::onFileImported(const std::string& path, const std::string& name) { impl_->importFile(path, name); }

void Editor::onPause() {
    Impl& m = *impl_;
    if (!m.initialized) return;
    if (m.runtime) m.runtime->pause();
    if (m.project) m.save();
}

void Editor::onResume() {
    if (impl_->runtime) impl_->runtime->resume();
}

void Editor::onContextLost() {
    Impl& m = *impl_;
    m.renderer.onContextLost();
    if (m.runtime && !m.runtime->usesSharedRenderer()) m.runtime->renderer().onContextLost();
    // El backend de ImGui guarda objetos del contexto anterior: se descarta y se vuelve a crear.
    ImGui::GetIO().BackendRendererUserData = nullptr;
    ImGui::GetIO().BackendRendererName = nullptr;
}

void Editor::onContextRestored() {
    Impl& m = *impl_;
    ImGui_ImplOpenGL3_Init("#version 300 es");
    m.renderer.onContextRestored();
    if (m.runtime && !m.runtime->usesSharedRenderer()) m.runtime->renderer().onContextRestored();
}

bool Editor::onBack() {
    Impl& m = *impl_;
    if (m.runtime) {
        m.stop();
        return true;
    }
    if (m.project && m.codeTabActive) {
        m.codeTabActive = false;
        return true;
    }
    if (m.project) {
        m.closeProject();
        return true;
    }
    return false;
}

bool Editor::openProject(const std::string& path) { return impl_->openProject(path); }

int Editor::importFiles(const std::vector<std::string>& paths) {
    if (!impl_->project) return 0;
    int n = 0;
    for (const std::string& p : paths) {
        std::error_code ec;
        if (!fs::is_regular_file(p, ec)) continue;
        impl_->importFile(p, "");
        ++n;
    }
    return n;
}

bool Editor::createSandboxProject(const std::string& name, int sample) {
    std::string err;
    const std::string dir = impl_->projectsDir() + "/" + name;
    if (!(sample == 1 ? createNeonSample(dir, name, &err) : createSandboxSample(dir, name, &err))) {
        TITAN_ERROR("editor", "No se pudo crear el proyecto: %s", err.c_str());
        return false;
    }
    return impl_->openProject(dir);
}

bool Editor::createProject(const std::string& name, bool threeD, bool) {
    std::string err;
    const std::string dir = impl_->projectsDir() + "/" + name;
    auto p = createProjectFromTemplate(dir, name, threeD ? ProjectTemplate::Empty3D : ProjectTemplate::Empty2D, &err);
    if (!p) {
        TITAN_ERROR("editor", "No se pudo crear el proyecto: %s", err.c_str());
        return false;
    }
    return impl_->openProject(dir);
}

void Editor::play() { impl_->play(); }
void Editor::stop() { impl_->stop(); }
bool Editor::isPlaying() const { return impl_->runtime != nullptr; }
Input* Editor::playInput() { return impl_->runtime ? &impl_->runtime->input() : nullptr; }

void Editor::createEntity(const std::string& kind) {
    // "Modelo:Cubo", "Modelo:Esfera"...: modelo editable (como Crear › Modelo editable), para automatizar pruebas.
    if (kind.rfind("Modelo:", 0) == 0) impl_->createEditableModel(kind.substr(7).c_str());
    else impl_->createEntityOfKind(kind);
}

void Editor::selectEntity(const std::string& name) { impl_->select(impl_->scene.findByName(name)); }

void Editor::showPanel(const std::string& name) {
    Impl& m = *impl_;
    if (name == "codigo") m.codeTabActive = !m.codePath.empty();
    else if (name == "escena") m.codeTabActive = false;
    else if (name == "outliner") m.panel = Panel::Outliner;
    else if (name == "detalles" || name == "inspector") m.panel = Panel::Details;
    else if (name == "contenido" || name == "recursos") { m.panel = Panel::Content; m.bottomIsConsole = false; }
    else if (name == "consola") { m.panel = Panel::Console; m.bottomIsConsole = true; }
    else if (name == "proyectos") m.showBrowser = true;
    else if (name == "rotar") m.tool = Tool::Rotate;
    else if (name == "escalar") m.tool = Tool::Scale;
    else if (name == "esculpir") m.terrainTool = 0;
    else if (name == "sonido") m.startSoundCheck();
    else if (name == "diagnostico") m.showDiagnostics = true;
    else if (name == "mezclador") m.showMixer = true;
    else if (name == "pintar") m.terrainTool = 4;
    else if (name == "maquina" && !m.selected.isNull()) { m.machineEditorFor = m.selected; m.showMachineEditor = true; }
    else if (name == "nodos" && !m.selected.isNull()) m.openMaterialGraph(m.scene.findById(m.selected));
}

void Editor::openScript(const std::string& rel) { impl_->openScriptFile(rel); }

size_t Editor::scriptErrorCount() const { return impl_->runtime ? impl_->runtime->scripts().errors().size() : 0; }

} // namespace titan
