// Núcleo del editor: ciclo de vida, entrada, proyectos, deshacer, modo Play.
#include <GLES3/gl3.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "EditorImpl.h"
#include "backends/imgui_impl_opengl3.h"
#include "imgui_internal.h"
#include "titan/core/FileIO.h"
#include "titan/core/Templates.h"
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
    scale = std::max(1.0f, platform.uiScale());
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
    if (!renderer.init(files)) return false;

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
    fbWidth = std::max(w, 1);
    fbHeight = std::max(h, 1);
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(float(fbWidth), float(fbHeight));
    io.DeltaTime = std::max(dt, 1e-4f);
    if (statusTime > 0) statusTime -= dt;

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
    textureCache.clear();
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

TextureHandle Editor::Impl::editorTexture(AssetRef ref) {
    if (ref.isNull() || !project) return kWhiteTexture;
    auto it = textureCache.find(ref.value);
    if (it != textureCache.end()) return it->second;
    TextureHandle h = kWhiteTexture;
    if (const AssetRecord* r = project->assets().find(ref)) {
        TextureOptions opt;
        opt.nearest = r->importSettings.value("filter", "linear") == "nearest";
        h = renderer.loadTexture("assets/" + r->path, opt);
    }
    textureCache[ref.value] = h;
    return h;
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
    } else if (kind == "Cielo y niebla") {
        scene.world().add<Environment>(e);
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
    // Copia el subárbol completo con ids nuevos, a través del registro de componentes.
    std::function<Entity(Entity, Entity, bool)> copy = [&](Entity from, Entity parent, bool root) {
        Entity to = scene.createEntity(scene.info(from).name + (root ? " (copia)" : ""), parent);
        scene.info(to).active = scene.info(from).active;
        for (const auto& type : registry.types()) {
            if (!type.has(scene, from)) continue;
            const Json data = type.save(scene, from);
            LoadReport ignored;
            FieldReader reader(data, type.name, ignored);
            type.load(scene, to, reader);
        }
        if (auto* kept = scene.world().get<PreservedComponents>(from)) scene.world().add<PreservedComponents>(to, *kept);
        for (Entity c : scene.children(from)) copy(c, to, false);
        return to;
    };
    Entity dup = copy(src, scene.parent(src), true);
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
    std::string rel = "imported/" + base;
    for (int i = 2; fs::exists(project->assetsDir() + "/" + rel); ++i)
        rel = "imported/" + fs::path(base).stem().string() + "_" + std::to_string(i) + fs::path(base).extension().string();
    std::error_code ec;
    fs::create_directories(project->assetsDir() + "/imported", ec);
    fs::copy_file(path, project->assetsDir() + "/" + rel, ec);
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
    runtime = std::make_unique<Runtime>();
    const std::string text = serializeScene(scene, registry);
    const AssetRecord* r = project->assets().find(sceneId);
    const size_t errorsBefore = logs.size();
    (void)errorsBefore;
    if (!runtime->startEmbedded(files, project->assets(), text, r ? r->path : scene.name())) {
        runtime.reset();
        setStatus("No se pudo ejecutar la escena (ver Consola)");
        return;
    }
    playTouches.clear();
    codeTabActive = false;
    playPaused = false;
    setStatus("Jugando: los cambios hechos durante el juego se descartan al detener");
}

void Editor::Impl::stop() {
    if (!runtime) return;
    runtime->shutdown();
    runtime.reset();
    setStatus("Juego detenido");
}

// ============================================================ Entrada

void Editor::Impl::handleShortcuts() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) {
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) save();
        return;
    }
    if (!project) return;
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) save();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) undo();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) redo();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) duplicateSelected();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_P, false)) runtime ? stop() : play();
    if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Delete, false)) deleteSelected();
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
        ImGuiKey_W, ImGuiKey_E, ImGuiKey_R};
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
    if (m.runtime) m.runtime->renderer().onContextLost();
    // El backend de ImGui guarda objetos del contexto anterior: se descarta y se vuelve a crear.
    ImGui::GetIO().BackendRendererUserData = nullptr;
    ImGui::GetIO().BackendRendererName = nullptr;
}

void Editor::onContextRestored() {
    Impl& m = *impl_;
    ImGui_ImplOpenGL3_Init("#version 300 es");
    m.renderer.onContextRestored();
    if (m.runtime) m.runtime->renderer().onContextRestored();
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
}

void Editor::openScript(const std::string& rel) { impl_->openScriptFile(rel); }

size_t Editor::scriptErrorCount() const { return impl_->runtime ? impl_->runtime->scripts().errors().size() : 0; }

} // namespace titan
