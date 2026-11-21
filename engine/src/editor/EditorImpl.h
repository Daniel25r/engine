#pragma once
// Estado interno del editor, compartido por EditorCore.cpp, EditorPanels.cpp y EditorViewport.cpp.
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "imgui.h"
#include "titan/core/FileSource.h"
#include "titan/core/Log.h"
#include "titan/core/Project.h"
#include "titan/editor/Editor.h"
#include "titan/render/Renderer.h"
#include "titan/runtime/Runtime.h"
#include "titan/runtime/SceneRenderer.h"

namespace titan {

// Lee primero del proyecto y, si no está, de los recursos del motor (fuente de depuración).
class ProjectFiles final : public FileSource {
public:
    std::string root;
    FileSource* engine = nullptr;
    bool read(const std::string& path, std::vector<uint8_t>& out) override;
};

enum class Panel { Scene, Hierarchy, Inspector, Code, Assets, Console };
enum class Tool { Move, Camera };

struct Editor::Impl {
    explicit Impl(EditorPlatform& p) : platform(p), registry(ComponentRegistry::createDefault()) {}

    EditorPlatform& platform;
    bool initialized = false;
    float scale = 1.0f;
    ImFont* monoFont = nullptr;
    int fbWidth = 1, fbHeight = 1;

    // ---- Proyecto y escena en edición ----
    std::unique_ptr<Project> project;
    ComponentRegistry registry;
    Scene scene{"escena"};
    UUID sceneId;
    bool dirty = false;
    UUID selected; // por id persistente: sobrevive a deshacer/rehacer
    Renderer renderer;
    SceneRenderer sceneRenderer;
    ProjectFiles files;
    std::unordered_map<uint64_t, TextureHandle> textureCache;
    std::string status;   // mensaje breve en la barra superior
    float statusTime = 0;

    // ---- Deshacer / rehacer (instantáneas de la escena; ver ROADMAP: se sustituirán por comandos) ----
    struct Snapshot {
        std::string scene;
        UUID selected;
    };
    std::vector<Snapshot> undoStack, redoStack;
    std::string pendingEdit;

    // ---- Cámara del editor ----
    Vec2 cam2Center{0, 0};
    float cam2Size = 6;
    Vec3 cam3Target{0, 0, 0};
    float cam3Yaw = 0.6f, cam3Pitch = 0.55f, cam3Dist = 12;
    Mat4 editorView, editorProj;

    // ---- Vista de escena y gestos ----
    ImVec2 vpMin{0, 0}, vpMax{0, 0};
    bool vpVisible = false;
    Tool tool = Tool::Move;
    bool snap = false;
    struct Touch {
        int id;
        ImVec2 pos, start, last;
    };
    std::vector<Touch> touches;   // dedos en la vista de escena
    int imguiPointer = -1;        // dedo que maneja la interfaz
    bool gestureMoved = false;
    bool movingEntity = false;
    Vec3 moveStartWorld, moveStartHit, movePlaneNormal;
    float pinchStart = 0;
    bool noScrollWidgetActive = false; // un control de arrastre está activo: no desplazar el panel
    ImVec2 lastMouse{0, 0};
    bool mouseDown = false;
    float dragDistance = 0;

    // ---- Paneles ----
    Panel panel = Panel::Scene;
    bool showBrowser = true;
    Vec3 inspectorEuler;
    UUID inspectorEulerFor;

    // ---- Editor de código ----
    std::string codePath;     // relativa a assets/
    std::string code;
    bool codeDirty = false;
    int codeCursor = 0;
    int codeCursorLine = 1;
    int codeRequestCursor = -1;
    bool codeRequestFocus = false;
    std::string codeCheck;    // resultado de "Comprobar"
    int codeErrorLine = 0;
    std::string newScriptName;

    // ---- Consola ----
    std::deque<LogMessage> logs;
    int logSink = 0;
    bool showInfo = true, showWarnings = true, showErrors = true;
    bool scrollConsole = false;

    // ---- Modo Play ----
    std::unique_ptr<Runtime> runtime;
    std::unordered_map<int, bool> playTouches;

    // ---- Navegador de proyectos ----
    std::string newProjectName = "Mi juego";
    int newProjectTemplate = 2;
    std::string pendingDelete;
    std::string importPath; // escritorio sin selector de archivos

    // ================= EditorCore.cpp =================
    bool init();
    void frame(int w, int h, float dt);
    void loadFonts();
    void applyStyle();
    void setStatus(const std::string& s);
    std::string projectsDir();
    bool openProject(const std::string& path);
    void closeProject();
    bool loadScene(UUID id);
    bool save();
    bool saveCode();
    void play();
    void stop();
    void beginEdit();
    void commitEdit();
    void pushUndo();
    void undo();
    void redo();
    void restore(const Snapshot& s);
    Entity selectedEntity();
    void select(Entity e);
    TextureHandle editorTexture(AssetRef ref);
    void handleShortcuts();
    void touchScroll();
    void track(); // llamar tras cada control de edición continua (arrastres y texto)
    void deleteSelected();
    void duplicateSelected();
    Entity createEntityOfKind(const std::string& kind);
    void openScriptFile(const std::string& rel);
    void importFile(const std::string& path, const std::string& name);
    void exportPackage();

    // ================= EditorPanels.cpp =================
    void drawUI();
    void drawToolbar(const ImVec2& pos, const ImVec2& size);
    void drawBrowser();
    void drawHierarchy();
    void drawHierarchyNode(Entity e);
    void drawInspector();
    void drawComponentFields(const std::string& name, Entity e);
    bool assetCombo(const char* label, AssetType type, AssetRef& ref);
    void drawCode();
    void drawAssets();
    void drawConsole();
    void drawPanelTabs(bool wide);
    void drawPanel(Panel p);
    void checkCode();

    // ================= EditorViewport.cpp =================
    bool threeD() const;
    void updateEditorCamera(float aspect);
    void renderViewport();
    void drawGizmos();
    bool screenRay(ImVec2 p, Vec3& origin, Vec3& dir) const;
    Entity pick(ImVec2 p);
    void viewportPointerDown(int id, ImVec2 p);
    void viewportPointerMove(int id, ImVec2 p);
    void viewportPointerUp(int id, ImVec2 p);
    void viewportScroll(float amount);
    bool insideViewport(ImVec2 p) const {
        return vpVisible && p.x >= vpMin.x && p.y >= vpMin.y && p.x < vpMax.x && p.y < vpMax.y;
    }
    Vec3 spawnPoint() const;
};

} // namespace titan
