#pragma once
// Editor de Titán: la aplicación donde se crean y programan los juegos.
// Es el mismo código en Android y en escritorio; la plataforma solo aporta la ventana,
// el contexto OpenGL, la entrada y unos pocos servicios (EditorPlatform).
//
// Todo lo que muestra y modifica son datos reales del Core: la escena que se edita es una
// titan::Scene, se guarda con la serialización del motor y "Jugar" la ejecuta con el mismo Runtime
// que va dentro de los juegos exportados.
#include <memory>
#include <string>
#include <vector>

namespace titan {

class FileSource;
class Input;

class EditorPlatform {
public:
    virtual ~EditorPlatform() = default;
    // Carpeta donde viven los proyectos (en Android: almacenamiento privado de la app).
    virtual std::string projectsDirectory() = 0;
    // Recursos del propio motor (engine/fonts/...).
    virtual FileSource& engineFiles() = 0;
    // Mostrar u ocultar el teclado en pantalla (Android). En escritorio no hace nada.
    virtual void setTextInputActive(bool active) = 0;
    // Pedir al sistema que el usuario elija un archivo; la respuesta llega con Editor::onFileImported.
    // Devuelve false si la plataforma no tiene selector (el editor pedirá una ruta).
    virtual bool requestImportFile() = 0;
    virtual std::string clipboard() = 0;
    virtual void setClipboard(const std::string& text) = 0;
    // Fuentes TTF candidatas (texto de interfaz y monoespaciada para el código), en orden de preferencia.
    virtual std::vector<std::string> uiFontCandidates() = 0;
    virtual std::vector<std::string> monoFontCandidates() = 0;
    // Píxeles por unidad de interfaz (densidad de pantalla).
    virtual float uiScale() = 0;
    // true en móviles: no hay "hover" y los paneles se desplazan arrastrando con el dedo.
    virtual bool isTouchScreen() = 0;
};

enum class EditorKey {
    Tab, Left, Right, Up, Down, PageUp, PageDown, Home, End, Delete, Backspace, Enter, Escape,
    Ctrl, Shift, Alt, A, C, V, X, Y, Z, S, D, F, P, N, Q, W, E, R,
    Plus, Minus, Num0, // Ctrl+ / Ctrl- / Ctrl+0: tamaño de la interfaz
    I, Num1, Num3,     // modelado: inset, modo vértices, modo caras
};

class Editor {
public:
    explicit Editor(EditorPlatform& platform);
    ~Editor();
    Editor(const Editor&) = delete;
    Editor& operator=(const Editor&) = delete;

    // Requiere un contexto OpenGL ES 3 activo.
    bool init();
    // Procesa la interfaz y dibuja todo el fotograma (framebuffer completo).
    void frame(int framebufferWidth, int framebufferHeight, float dt);
    void shutdown();

    // ---- Entrada (píxeles del framebuffer, origen arriba a la izquierda) ----
    void onPointerDown(int id, float x, float y);
    void onPointerMove(int id, float x, float y);
    void onPointerUp(int id, float x, float y);
    void onScroll(float x, float y, float amount);
    void onText(const std::string& utf8);
    void onKey(EditorKey key, bool down);

    // ---- Ciclo de vida y servicios ----
    void onFileImported(const std::string& path, const std::string& displayName);
    void onPause();  // guarda el proyecto (Android puede cerrar la app en segundo plano)
    void onResume();
    void onContextLost();
    void onContextRestored();
    bool onBack();   // botón atrás: true si el editor lo ha gestionado

    // ---- Automatización (pruebas y línea de comandos) ----
    bool openProject(const std::string& path);
    bool createProject(const std::string& name, bool threeD, bool playableTemplate);
    bool createSandboxProject(const std::string& name); // ejemplo "Mundo de bloques"
    void play();
    void stop();
    bool isPlaying() const;
    // Entrada del juego mientras se juega en el editor (teclado y mando físicos); nulo si no se está jugando.
    Input* playInput();
    void selectEntity(const std::string& name);
    // Crea un objeto como el menú Crear ("Cubo", "Botón", "Joystick"...).
    void createEntity(const std::string& kind);
    void showPanel(const std::string& name); // "escena", "jerarquia", "inspector", "codigo", "recursos", "consola"
    void openScript(const std::string& relativePath);
    size_t scriptErrorCount() const;

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace titan
