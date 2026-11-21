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

// Pestañas de la hoja inferior en teléfonos.
enum class Panel { Outliner, Details, Content, Console };
// Herramientas de la vista (como Q/W/E/R en los editores profesionales).
enum class Tool { Select, Translate, Rotate, Scale };
enum class ViewAngle { Perspective, Top, Front, Right };

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
    std::unordered_map<uint64_t, int> modelCache;
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
    Tool tool = Tool::Translate;
    bool snap = false;
    float snapMove = 0.5f, snapRotate = 15.0f, snapScale = 0.25f;
    ViewAngle viewAngle = ViewAngle::Perspective;
    bool viewUnlit = false, showGrid = true, showGizmos = true, showStats = false;
    ImVec2 vpBarMin{0, 0}, vpBarMax{0, 0}; // barra flotante de la vista: sus toques van a la interfaz
    // Gizmo de transformación
    int gizmoAxis = -1;   // 0=X 1=Y 2=Z 3=centro
    bool gizmoDragging = false;
    ImVec2 gizmoGrab{0, 0};
    Vec3 gizmoStartWorld, gizmoStartScale;
    Quat gizmoStartWorldRot;
    bool playPaused = false;
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
    Panel panel = Panel::Outliner;   // hoja inferior (teléfono)
    bool codeTabActive = false;       // documento activo: escena o script
    bool bottomIsConsole = false;     // panel inferior (pantalla ancha): contenido o consola
    bool showBottom = true;
    std::string outlinerFilter;
    std::string contentFolder;        // carpeta mostrada en el navegador de contenido
    UUID contentSelected;
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
    int editorModel(AssetRef ref);
    // Caja local del objeto (cubo unidad para primitivas, límites del modelo si lo tiene).
    bool localBounds(Entity e, Vec3& min, Vec3& max);
    void addModelToScene(const AssetRecord& r);
    void fitCollider(Entity e); // ajusta forma y tamaño del Collider a la malla o el modelo
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
    void drawMenuBar();
    void drawToolbar(const ImVec2& pos, const ImVec2& size);
    void drawDocumentTabs();
    void drawViewportBar();
    void drawStatusBar(const ImVec2& pos, const ImVec2& size);
    void drawBrowser();
    void drawOutliner();
    void drawOutlinerNode(Entity e);
    void drawDetails();
    void drawComponentFields(const std::string& name, Entity e);
    bool assetCombo(const char* label, AssetType type, AssetRef& ref);
    void drawCode();
    void drawContent();
    void drawConsole();
    void drawCreateMenu();
    void checkCode();
    const char* typeLabel(Entity e);

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
    bool worldToScreen(const Vec3& p, ImVec2& out) const;
    float gizmoLength() const;
    int gizmoHitTest(ImVec2 p);
    void gizmoDrag(ImVec2 p);
    void drawGizmo(ImDrawList* dl);
    void drawAxisIndicator(ImDrawList* dl);
};

} // namespace titan
