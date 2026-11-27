// Pantalla de inicio del editor (lanzador): proyectos recientes y galería de plantillas, al estilo de los
// lanzadores de los motores profesionales. Todo lo que muestra funciona: abrir, crear (vacío 3D/2D o el juego de
// ejemplo Isla Estelar), buscar, borrar con confirmación y el tamaño de la interfaz.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>

#include "EditorImpl.h"
#include "imgui.h"
#include "imgui_stdlib.h"
#include "titan/core/FileIO.h"
#include "titan/core/Templates.h"
#include "titan/core/Version.h"
#include "titan/samples/Samples.h"

namespace fs = std::filesystem;

namespace titan {

namespace {

const ImU32 kBg = IM_COL32(30, 31, 34, 255);
const ImU32 kSide = IM_COL32(22, 23, 26, 255);
const ImU32 kCard = IM_COL32(40, 42, 46, 255);
const ImU32 kCardHover = IM_COL32(48, 51, 56, 255);
const ImU32 kAccent = IM_COL32(0, 120, 232, 255);
const ImU32 kText = IM_COL32(236, 238, 242, 255);
const ImU32 kDimText = IM_COL32(150, 155, 165, 255);

struct TemplateInfo {
    const char* title;
    const char* tag;
    const char* description;
    const char* includes;
};
const TemplateInfo kTemplates[] = {
    {"3D vacío", "3D",
     "Un nivel inicial listo para construir: cámara en perspectiva, sol con sombras, cielo, niebla, suelo de "
     "prototipado y un cubo de referencia.",
     "Cámara · Sol con sombras · Cielo y niebla · Suelo · Cubo"},
    {"2D vacío", "2D",
     "Cámara ortográfica para juegos en plano: plataformas 2D, puzles, juegos de vista cenital.",
     "Cámara ortográfica · Escena vacía"},
    {"Isla Estelar", "Ejemplo 3D",
     "Un juego de plataformas 3D completo para aprender cómo se hace: personaje con doble salto y salto de pared, "
     "cámara en tercera persona, enemigos que patrullan y persiguen, monedas, plataformas móviles, peligros, puntos "
     "de control, vidas, menú, pantalla de victoria, récord guardado y música y efectos con el mezclador.",
     "3 escenas · 9 scripts Lua · 10 sonidos · Mezclador · Navegación e IA"},
    {"Mundo de bloques", "Ejemplo sandbox",
     "Un sandbox de construcción: mundo infinito por chunks con biomas (llanura, bosque, desierto, montaña, nieve, "
     "océano, volcánico), cuevas y árboles. Rompe y coloca bloques; el mundo se guarda solo y se carga al volver.",
     "Mundo de bloques · Personaje · Cámara 3ª persona · 1 script Lua (Voxel.*)"},
    {"Ciudad de neón", "Escaparate del render",
     "Una calle de noche para ver de qué es capaz el render de Titán: HDR con bloom de neón, asfalto mojado con "
     "reflejos, ventanas generadas con materiales por nodos, coche con barniz, farolas, sombras en cascada, SSAO y "
     "antialiasing temporal. Una cámara recorre la calle. Todo hecho con primitivas (sin recursos de terceros).",
     "1 escena · materiales por nodos · 4 luces · cámara en Lua"},
};

std::string relativeTime(fs::file_time_type t) {
    using namespace std::chrono;
    const auto now = fs::file_time_type::clock::now();
    const long long s = duration_cast<seconds>(now - t).count();
    if (s < 60) return "hace un momento";
    if (s < 3600) return "hace " + std::to_string(s / 60) + " min";
    if (s < 86400) return "hace " + std::to_string(s / 3600) + " h";
    if (s < 2 * 86400) return "ayer";
    if (s < 60 * 86400) return "hace " + std::to_string(s / 86400) + " días";
    return "hace " + std::to_string(s / (30 * 86400)) + " meses";
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Ilustraciones de las tarjetas, dibujadas con primitivas (sin imágenes que distribuir).
void gradient(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 top, ImU32 bottom) {
    dl->AddRectFilledMultiColor(a, b, top, top, bottom, bottom);
}

void drawPreview3D(ImDrawList* dl, ImVec2 a, ImVec2 b, float s) {
    gradient(dl, a, b, IM_COL32(52, 110, 190, 255), IM_COL32(170, 205, 235, 255));
    const float hy = a.y + (b.y - a.y) * 0.58f, cx = (a.x + b.x) * 0.5f;
    dl->AddRectFilled(ImVec2(a.x, hy), b, IM_COL32(110, 116, 126, 255));
    for (int i = -6; i <= 6; ++i) // suelo en perspectiva
        dl->AddLine(ImVec2(cx + i * 10 * s, hy), ImVec2(cx + i * 60 * s, b.y), IM_COL32(150, 156, 166, 255), 1);
    for (int j = 1; j <= 4; ++j) {
        const float y = hy + (b.y - hy) * (j * j) / 16.0f;
        dl->AddLine(ImVec2(a.x, y), ImVec2(b.x, y), IM_COL32(150, 156, 166, 255), 1);
    }
    const float c = 22 * s; // cubo
    const ImVec2 o(cx, hy + 4 * s);
    const ImVec2 top[4] = {{o.x, o.y - 2 * c}, {o.x + c, o.y - 1.5f * c}, {o.x, o.y - c}, {o.x - c, o.y - 1.5f * c}};
    const ImVec2 left[4] = {{o.x - c, o.y - 1.5f * c}, {o.x, o.y - c}, {o.x, o.y}, {o.x - c, o.y - 0.5f * c}};
    const ImVec2 right[4] = {{o.x, o.y - c}, {o.x + c, o.y - 1.5f * c}, {o.x + c, o.y - 0.5f * c}, {o.x, o.y}};
    dl->AddEllipseFilled(ImVec2(o.x - 6 * s, o.y + 2 * s), ImVec2(c * 1.2f, c * 0.3f), IM_COL32(40, 44, 52, 120));
    dl->AddConvexPolyFilled(top, 4, IM_COL32(235, 238, 242, 255));
    dl->AddConvexPolyFilled(left, 4, IM_COL32(190, 195, 204, 255));
    dl->AddConvexPolyFilled(right, 4, IM_COL32(150, 156, 166, 255));
    dl->AddCircleFilled(ImVec2(b.x - 28 * s, a.y + 22 * s), 10 * s, IM_COL32(255, 236, 170, 255));
}

void drawPreview2D(ImDrawList* dl, ImVec2 a, ImVec2 b, float s) {
    gradient(dl, a, b, IM_COL32(36, 30, 70, 255), IM_COL32(120, 80, 140, 255));
    for (int i = 0; i < 14; ++i) // estrellas
        dl->AddCircleFilled(ImVec2(a.x + std::fmod(i * 53.0f * s, b.x - a.x), a.y + std::fmod(i * 23.0f * s, (b.y - a.y) * 0.5f)),
                            1.3f * s, IM_COL32(255, 255, 255, 180));
    const float t = 16 * s, gy = b.y - t * 1.6f;
    for (float x = a.x; x < b.x; x += t) { // fila de bloques
        dl->AddRectFilled(ImVec2(x + 1, gy), ImVec2(x + t - 1, b.y), IM_COL32(86, 160, 80, 255));
        dl->AddRectFilled(ImVec2(x + 1, gy + t * 0.35f), ImVec2(x + t - 1, b.y), IM_COL32(120, 80, 50, 255));
    }
    for (int i = 0; i < 3; ++i) // plataforma flotante
        dl->AddRectFilled(ImVec2(a.x + (b.x - a.x) * 0.55f + i * t, gy - 3.2f * t),
                          ImVec2(a.x + (b.x - a.x) * 0.55f + (i + 1) * t - 1, gy - 2.4f * t), IM_COL32(200, 140, 70, 255));
    const ImVec2 hero(a.x + (b.x - a.x) * 0.28f, gy);
    dl->AddRectFilled(ImVec2(hero.x, hero.y - 1.6f * t), ImVec2(hero.x + t, hero.y), IM_COL32(230, 90, 80, 255), 3 * s);
    dl->AddRectFilled(ImVec2(hero.x + t * 0.55f, hero.y - 1.3f * t), ImVec2(hero.x + t * 0.85f, hero.y - 1.0f * t),
                      IM_COL32(255, 255, 255, 255));
}

void drawPreviewSandbox(ImDrawList* dl, ImVec2 a, ImVec2 b, float s) {
    gradient(dl, a, b, IM_COL32(74, 143, 224, 255), IM_COL32(214, 234, 248, 255));
    const float t = 12 * s, W = b.x - a.x;
    // Relieve escalonado de bloques: hierba arriba, tierra y piedra debajo, agua en lo bajo.
    const int heights[] = {3, 3, 4, 5, 5, 6, 7, 7, 6, 5, 4, 3, 2, 2, 2, 3, 4, 4, 5, 6};
    const float floorY = b.y;
    for (int i = 0; i * t < W; ++i) {
        const int h = heights[i % 20];
        const float x = a.x + i * t;
        for (int k = 0; k < h; ++k) {
            const float y = floorY - (k + 1) * t;
            const ImU32 c = k == h - 1 ? IM_COL32(95, 168, 58, 255) : k >= h - 3 ? IM_COL32(122, 90, 58, 255) : IM_COL32(138, 141, 147, 255);
            dl->AddRectFilled(ImVec2(x, y), ImVec2(x + t - 1, y + t - 1), c);
        }
        for (int k = h; k < 3; ++k) {
            const float y = floorY - (k + 1) * t;
            dl->AddRectFilled(ImVec2(x, y), ImVec2(x + t - 1, y + t - 1), IM_COL32(46, 111, 184, 220));
        }
    }
    // Árbol de bloques.
    const float tx = a.x + 6 * t, ty = floorY - 7 * t;
    for (int k = 0; k < 3; ++k) dl->AddRectFilled(ImVec2(tx, ty - (k + 1) * t), ImVec2(tx + t - 1, ty - k * t - 1), IM_COL32(110, 74, 42, 255));
    dl->AddRectFilled(ImVec2(tx - t, ty - 5 * t), ImVec2(tx + 2 * t - 1, ty - 3 * t - 1), IM_COL32(62, 138, 52, 255));
    // Bloque que se está colocando (marco blanco).
    const float bx = a.x + 9 * t, by = floorY - 8 * t;
    dl->AddRect(ImVec2(bx, by), ImVec2(bx + t, by + t), IM_COL32(255, 255, 255, 230), 0, 0, 2 * s);
}

void drawPreviewNeon(ImDrawList* dl, ImVec2 a, ImVec2 b, float s) {
    gradient(dl, a, b, IM_COL32(8, 6, 20, 255), IM_COL32(40, 22, 60, 255));
    const float W = b.x - a.x, H = b.y - a.y;
    // Edificios con ventanas encendidas.
    for (int i = 0; i < 9; ++i) {
        const float x0 = a.x + W * (i / 9.0f), x1 = x0 + W / 9.0f - 3 * s, top = a.y + H * (0.15f + 0.08f * float((i * 5) % 4));
        dl->AddRectFilled(ImVec2(x0, top), ImVec2(x1, a.y + H * 0.72f), IM_COL32(24, 24, 34, 255));
        for (float y = top + 5 * s; y < a.y + H * 0.68f; y += 8 * s)
            for (float x = x0 + 3 * s; x < x1 - 4 * s; x += 7 * s)
                if ((int(x * 13 + y * 7) / 5) % 3 == 0) dl->AddRectFilled(ImVec2(x, y), ImVec2(x + 3 * s, y + 4 * s), IM_COL32(255, 200, 120, 220));
    }
    // Neones y su reflejo en el suelo mojado.
    const ImU32 cols[] = {IM_COL32(255, 46, 154, 255), IM_COL32(35, 230, 255, 255), IM_COL32(255, 224, 58, 255)};
    for (int i = 0; i < 3; ++i) {
        const float x = a.x + W * (0.2f + 0.3f * i), y = a.y + H * 0.42f;
        dl->AddRectFilled(ImVec2(x - 2 * s, y), ImVec2(x + 2 * s, y + H * 0.22f), cols[i], 2 * s);
        dl->AddRectFilled(ImVec2(x - 2 * s, a.y + H * 0.76f), ImVec2(x + 2 * s, a.y + H * 0.95f), (cols[i] & 0x00FFFFFF) | 0x50000000);
    }
    dl->AddRectFilled(ImVec2(a.x, a.y + H * 0.72f), b, IM_COL32(12, 12, 18, 160));
    // Coche rojo.
    dl->AddRectFilled(ImVec2(a.x + W * 0.38f, a.y + H * 0.66f), ImVec2(a.x + W * 0.62f, a.y + H * 0.76f), IM_COL32(176, 16, 26, 255), 4 * s);
}

void drawPreviewPlatformer(ImDrawList* dl, ImVec2 a, ImVec2 b, float s) {
    gradient(dl, a, b, IM_COL32(60, 130, 216, 255), IM_COL32(200, 226, 246, 255));
    dl->AddRectFilled(ImVec2(a.x, b.y - 14 * s), b, IM_COL32(45, 127, 184, 255));
    auto island = [&](float x, float y, float w) {
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + 7 * s), IM_COL32(109, 190, 69, 255), 2 * s);
        dl->AddTriangleFilled(ImVec2(x + 3 * s, y + 7 * s), ImVec2(x + w - 3 * s, y + 7 * s), ImVec2(x + w * 0.5f, y + 26 * s),
                              IM_COL32(138, 90, 60, 255));
    };
    const float W = b.x - a.x, H = b.y - a.y;
    island(a.x + W * 0.06f, a.y + H * 0.62f, W * 0.3f);
    island(a.x + W * 0.45f, a.y + H * 0.46f, W * 0.2f);
    island(a.x + W * 0.72f, a.y + H * 0.30f, W * 0.22f);
    for (int i = 0; i < 3; ++i) dl->AddCircleFilled(ImVec2(a.x + W * (0.48f + i * 0.06f), a.y + H * 0.36f), 3.5f * s, IM_COL32(255, 200, 61, 255));
    const ImVec2 hero(a.x + W * 0.18f, a.y + H * 0.62f); // personaje: gorra roja, cuerpo azul
    dl->AddRectFilled(ImVec2(hero.x, hero.y - 16 * s), ImVec2(hero.x + 8 * s, hero.y), IM_COL32(59, 125, 216, 255), 2 * s);
    dl->AddCircleFilled(ImVec2(hero.x + 4 * s, hero.y - 19 * s), 4 * s, IM_COL32(242, 201, 160, 255));
    dl->AddRectFilled(ImVec2(hero.x - 1 * s, hero.y - 24 * s), ImVec2(hero.x + 9 * s, hero.y - 21 * s), IM_COL32(217, 59, 59, 255));
    dl->AddCircleFilled(ImVec2(a.x + W * 0.55f, a.y + H * 0.46f - 5 * s), 5 * s, IM_COL32(191, 51, 64, 255)); // enemigo
    // Estrella de la meta
    const ImVec2 st(a.x + W * 0.83f, a.y + H * 0.30f - 12 * s);
    ImVec2 pts[10];
    for (int i = 0; i < 10; ++i) {
        const float r = (i % 2 ? 3.5f : 8.0f) * s, ang = -1.5708f + i * 0.6283f;
        pts[i] = ImVec2(st.x + std::cos(ang) * r, st.y + std::sin(ang) * r);
    }
    for (int i = 0; i < 10; ++i) dl->AddTriangleFilled(st, pts[i], pts[(i + 1) % 10], IM_COL32(255, 216, 74, 255));
}

void drawLogo(ImDrawList* dl, ImVec2 c, float r) {
    ImVec2 hex[6];
    for (int i = 0; i < 6; ++i) hex[i] = ImVec2(c.x + std::cos(0.5236f + i * 1.0472f) * r, c.y + std::sin(0.5236f + i * 1.0472f) * r);
    dl->AddConvexPolyFilled(hex, 6, IM_COL32(0, 102, 214, 255));
    dl->AddPolyline(hex, 6, IM_COL32(90, 170, 255, 255), ImDrawFlags_Closed, r * 0.08f);
    const float t = r * 0.16f; // una "T"
    dl->AddRectFilled(ImVec2(c.x - r * 0.48f, c.y - r * 0.45f), ImVec2(c.x + r * 0.48f, c.y - r * 0.45f + t * 1.6f), IM_COL32(255, 255, 255, 255));
    dl->AddRectFilled(ImVec2(c.x - t, c.y - r * 0.3f), ImVec2(c.x + t, c.y + r * 0.55f), IM_COL32(255, 255, 255, 255));
}

// Botón de navegación de la barra lateral.
bool navItem(const char* label, bool active, float w, float h, float s) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(label, ImVec2(w, h));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (active || ImGui::IsItemHovered())
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), active ? IM_COL32(0, 120, 232, 60) : IM_COL32(255, 255, 255, 14), 4 * s);
    if (active) dl->AddRectFilled(p, ImVec2(p.x + 3 * s, p.y + h), kAccent, 2 * s);
    dl->AddText(ImVec2(p.x + 14 * s, p.y + (h - ImGui::GetFontSize()) * 0.5f), active ? kText : kDimText, label);
    return clicked;
}

bool primaryButton(const char* label, ImVec2 size) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.47f, 0.91f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.1f, 0.55f, 1.0f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.0f, 0.38f, 0.78f, 1));
    const bool r = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return r;
}

} // namespace

void Editor::Impl::drawBrowser() {
    const float W = float(fbWidth), H = float(fbHeight), s = scale;
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(W, H));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::ColorConvertU32ToFloat4(kBg));
    const bool open = ImGui::Begin("##lanzador", nullptr,
                                   ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                                       ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    if (!open) {
        ImGui::End();
        return;
    }

    // ---- Proyectos de la carpeta (el más reciente primero)
    struct Entry {
        std::string path, name;
        bool threeD = false, readable = true;
        fs::file_time_type modified;
    };
    std::vector<Entry> projects;
    std::error_code ec;
    for (auto& d : fs::directory_iterator(projectsDir(), ec)) {
        const fs::path file = d.path() / "project.titan";
        if (!d.is_directory() || !fs::exists(file)) continue;
        Entry e;
        e.path = d.path().generic_string();
        e.modified = fs::last_write_time(file, ec);
        for (const char* sub : {"assets/scenes", "assets/scripts", "assets"}) { // lo último guardado de verdad
            for (auto& f : fs::directory_iterator(d.path() / sub, ec))
                if (f.is_regular_file()) e.modified = std::max(e.modified, f.last_write_time(ec));
        }
        std::string text;
        ProjectDimension dim = ProjectDimension::TwoD;
        UUID start;
        if (fileio::readText(file.string(), text) && Project::parseProjectFile(text, e.name, dim, start, nullptr)) {
            e.threeD = dim == ProjectDimension::ThreeD;
        } else {
            e.name = d.path().filename().string();
            e.readable = false;
        }
        projects.push_back(e);
    }
    std::sort(projects.begin(), projects.end(), [](const Entry& a, const Entry& b) { return a.modified > b.modified; });
    if (launcherView == 0 && projects.empty()) launcherView = 1; // sin proyectos: directamente a crear uno

    // ---- Barra lateral
    const float sideW = 250 * s;
    ImDrawList* bg = ImGui::GetWindowDrawList();
    bg->AddRectFilled(ImVec2(0, 0), ImVec2(sideW, H), kSide);
    bg->AddLine(ImVec2(sideW, 0), ImVec2(sideW, H), IM_COL32(255, 255, 255, 18));
    ImGui::SetCursorScreenPos(ImVec2(18 * s, 22 * s));
    ImGui::BeginGroup();
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        drawLogo(bg, ImVec2(p.x + 20 * s, p.y + 20 * s), 20 * s);
        bg->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 1.45f, ImVec2(p.x + 50 * s, p.y + 2 * s), kText, "TITÁN");
        bg->AddText(ImVec2(p.x + 51 * s, p.y + 24 * s), kDimText, (std::string("Engine ") + kEngineVersion).c_str());
        ImGui::Dummy(ImVec2(sideW - 36 * s, 56 * s));
    }
    const float navW = sideW - 36 * s, navH = ImGui::GetFontSize() + 18 * s;
    const std::string projLabel = "Proyectos  (" + std::to_string(projects.size()) + ")";
    if (navItem(projLabel.c_str(), launcherView == 0, navW, navH, s)) launcherView = 0;
    if (navItem("Nuevo proyecto", launcherView == 1, navW, navH, s)) launcherView = 1;
    if (project) {
        ImGui::Dummy(ImVec2(0, 10 * s));
        if (navItem(("Volver a " + project->name()).c_str(), false, navW, navH, s)) showBrowser = false;
    }
    ImGui::EndGroup();

    // Pie de la barra lateral: tamaño de la interfaz y carpeta.
    ImGui::SetCursorScreenPos(ImVec2(18 * s, H - 96 * s));
    ImGui::BeginGroup();
    ImGui::TextDisabled("Tamaño de la interfaz");
    if (ImGui::Button(" - ", ImVec2(36 * s, 0))) setUiZoom(uiZoom - 0.125f);
    ImGui::SameLine();
    ImGui::Text("%d %%", int(std::lround(uiZoom * 100)));
    ImGui::SameLine();
    if (ImGui::Button(" + ", ImVec2(36 * s, 0))) setUiZoom(uiZoom + 0.125f);
    ImGui::Spacing();
    { // Carpeta de proyectos: solo el final de la ruta; la ruta completa al pasar el ratón.
        const fs::path dirPath(projectsDir());
        ImGui::TextDisabled("Carpeta: %s", dirPath.filename().generic_string().c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", projectsDir().c_str());
    }
    ImGui::EndGroup();

    // ---- Zona principal
    const float mainX = sideW + 36 * s, mainW = W - sideW - 72 * s;
    ImGui::SetCursorScreenPos(ImVec2(mainX, 26 * s));
    ImGui::BeginChild("##principal", ImVec2(mainW, H - 52 * s), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    auto title = [&](const char* t, const char* sub) {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 1.6f, p, kText, t);
        dl->AddText(ImVec2(p.x, p.y + ImGui::GetFontSize() * 1.9f), kDimText, sub);
        ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 3.3f));
    };

    if (launcherView == 0) {
        title("Proyectos", "Abre un proyecto o crea uno nuevo. El más reciente aparece primero.");
        ImGui::SetNextItemWidth(std::min(mainW * 0.45f, 420 * s));
        ImGui::InputTextWithHint("##buscar", "Buscar proyecto...", &projectSearch);
        ImGui::SameLine(mainW - 190 * s);
        if (primaryButton("+  Nuevo proyecto", ImVec2(190 * s, 0))) launcherView = 1;
        ImGui::Dummy(ImVec2(0, 10 * s));

        const float gap = 16 * s;
        const int cols = std::max(1, int((mainW + gap) / (240 * s + gap)));
        const float cardW = (mainW - 8 * s - gap * (cols - 1)) / cols;
        const float prevH = std::clamp(cardW * 0.5f, 90 * s, 150 * s), cardH = prevH + 80 * s;
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        int shown = 0;
        const std::string q = lower(projectSearch);
        for (const Entry& e : projects) {
            if (!q.empty() && lower(e.name).find(q) == std::string::npos) continue;
            const ImVec2 a(origin.x + (shown % cols) * (cardW + gap), origin.y + (shown / cols) * (cardH + gap));
            const ImVec2 b(a.x + cardW, a.y + cardH);
            ++shown;
            ImGui::PushID(e.path.c_str());
            ImGui::SetCursorScreenPos(a);
            ImGui::SetNextItemAllowOverlap(); // el botón Eliminar va encima de la tarjeta
            const bool clicked = ImGui::InvisibleButton("##tarjeta", ImVec2(cardW, cardH));
            const bool hover = ImGui::IsMouseHoveringRect(a, b);
            dl->AddRectFilled(a, b, hover ? kCardHover : kCard, 8 * s);
            const ImVec2 pb(b.x, a.y + prevH);
            dl->PushClipRect(a, pb, true);
            if (e.threeD) drawPreview3D(dl, a, pb, s);
            else drawPreview2D(dl, a, pb, s);
            dl->PopClipRect();
            dl->AddRectFilled(ImVec2(a.x + 10 * s, a.y + 10 * s), ImVec2(a.x + 46 * s, a.y + 34 * s), IM_COL32(0, 0, 0, 140), 4 * s);
            dl->AddText(ImVec2(a.x + 17 * s, a.y + 12 * s), kText, e.threeD ? "3D" : "2D");
            dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 1.15f, ImVec2(a.x + 14 * s, pb.y + 12 * s), kText, e.name.c_str());
            dl->AddText(ImVec2(a.x + 14 * s, pb.y + 42 * s), kDimText,
                        e.readable ? ("Modificado " + relativeTime(e.modified)).c_str() : "No se puede leer project.titan");
            if (hover || pendingDelete == e.path) dl->AddRect(a, b, kAccent, 8 * s, 0, 2 * s);
            bool consumed = false;
            if (pendingDelete == e.path) { // confirmación dentro de la tarjeta
                ImGui::SetCursorScreenPos(ImVec2(a.x + 12 * s, b.y - 40 * s));
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.22f, 0.22f, 1));
                if (ImGui::Button("Borrar del disco", ImVec2(0, 0))) {
                    consumed = true;
                    if (project && project->root() == e.path) closeProject();
                    fs::remove_all(e.path, ec);
                    pendingDelete.clear();
                }
                ImGui::PopStyleColor();
                ImGui::SameLine();
                if (ImGui::Button("Cancelar")) {
                    consumed = true;
                    pendingDelete.clear();
                }
            } else if (hover) {
                ImGui::SetCursorScreenPos(ImVec2(b.x - 86 * s, b.y - 34 * s));
                if (ImGui::SmallButton("Eliminar")) {
                    consumed = true;
                    pendingDelete = e.path;
                }
            }
            if (clicked && !consumed && pendingDelete != e.path && e.readable) openProject(e.path);
            ImGui::PopID();
        }
        if (shown > 0) {
            const int rows = (shown + cols - 1) / cols;
            ImGui::SetCursorScreenPos(origin);
            ImGui::Dummy(ImVec2(mainW - 8 * s, rows * (cardH + gap)));
        }
        if (shown == 0) ImGui::TextDisabled(projects.empty() ? "Aún no hay proyectos." : "Ningún proyecto coincide con la búsqueda.");
    } else {
        title("Nuevo proyecto", "Elige una plantilla. Los ejemplos son juegos completos que puedes jugar y modificar.");
        const float detailW = std::min(380 * s, mainW * 0.34f), gap = 16 * s;
        const float galleryW = mainW - detailW - 24 * s;
        // Al menos 2 columnas si caben tarjetas de 170 px: una sola tarjeta a todo lo ancho se veía desproporcionada.
        int cols = std::max(1, int((galleryW + gap) / (220 * s + gap)));
        if (cols == 1 && galleryW >= 2 * 170 * s + gap) cols = 2;
        const float cardW = (galleryW - gap * (cols - 1)) / cols;
        const float prevH = std::clamp(cardW * 0.5f, 80 * s, 150 * s), cardH = prevH + 64 * s;
        ImGui::BeginChild("##galeria", ImVec2(galleryW, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
        ImDrawList* gl = ImGui::GetWindowDrawList();
        for (int i = 0; i < 5; ++i) {
            if (i % cols) ImGui::SameLine(0, gap);
            ImGui::PushID(i);
            const ImVec2 a = ImGui::GetCursorScreenPos(), b(a.x + cardW, a.y + cardH);
            if (ImGui::InvisibleButton("##plantilla", ImVec2(cardW, cardH))) {
                newProjectTemplate = i;
                // Nombre sugerido: el del ejemplo, salvo que el usuario ya haya escrito uno propio.
                const bool suggested = newProjectName == "Mi juego" || newProjectName == "Isla Estelar" || newProjectName == "Mundo de bloques" ||
                                       newProjectName == "Ciudad de neón";
                if (suggested)
                    newProjectName = i == 2 ? "Isla Estelar" : i == 3 ? "Mundo de bloques" : i == 4 ? "Ciudad de neón" : "Mi juego";
            }
            const bool hover = ImGui::IsItemHovered(), sel = newProjectTemplate == i;
            gl->AddRectFilled(a, b, hover ? kCardHover : kCard, 8 * s);
            const ImVec2 pb(b.x, a.y + prevH);
            gl->PushClipRect(a, pb, true);
            if (i == 0) drawPreview3D(gl, a, pb, s);
            else if (i == 1) drawPreview2D(gl, a, pb, s);
            else if (i == 2) drawPreviewPlatformer(gl, a, pb, s);
            else if (i == 3) drawPreviewSandbox(gl, a, pb, s);
            else drawPreviewNeon(gl, a, pb, s);
            gl->PopClipRect();
            gl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 1.1f, ImVec2(a.x + 14 * s, pb.y + 12 * s), kText, kTemplates[i].title);
            gl->AddText(ImVec2(a.x + 14 * s, pb.y + 36 * s), i >= 2 ? IM_COL32(255, 200, 61, 255) : kDimText, kTemplates[i].tag);
            if (sel) gl->AddRect(a, b, kAccent, 8 * s, 0, 3 * s);
            ImGui::PopID();
        }
        ImGui::EndChild();
        // Detalles y creación
        ImGui::SameLine(0, 24 * s);
        ImGui::BeginChild("##detalle", ImVec2(detailW, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
        const TemplateInfo& t = kTemplates[std::clamp(newProjectTemplate, 0, 4)];
        ImDrawList* d2 = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        d2->AddRectFilled(p, ImVec2(p.x + detailW, p.y + 2 * s), kAccent);
        ImGui::Dummy(ImVec2(0, 12 * s));
        d2->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 1.35f, ImGui::GetCursorScreenPos(), kText, t.title);
        ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 1.6f));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + detailW);
        ImGui::TextWrapped("%s", t.description);
        ImGui::Spacing();
        ImGui::TextDisabled("Incluye: %s", t.includes);
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, 16 * s));
        ImGui::TextUnformatted("Nombre del proyecto");
        ImGui::SetNextItemWidth(detailW);
        ImGui::InputText("##nombre", &newProjectName);
        std::string safeName;
        for (char c : newProjectName) safeName += (c == '/' || c == '\\' || c == ':' || c == '.') ? '_' : c;
        const std::string dir = projectsDir() + "/" + safeName;
        const bool exists = !safeName.empty() && fs::exists(dir);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + detailW);
        if (exists) ImGui::TextColored(ImVec4(1, 0.6f, 0.35f, 1), "Ya hay un proyecto con ese nombre.");
        else ImGui::TextDisabled("Se creará en %s", dir.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, 8 * s));
        ImGui::BeginDisabled(safeName.empty() || exists);
        if (primaryButton("Crear proyecto", ImVec2(detailW, 44 * s))) {
            std::string err;
            bool ok = false;
            if (newProjectTemplate == 2) ok = createPlatformerSample(dir, newProjectName, &err);
            else if (newProjectTemplate == 3) ok = createSandboxSample(dir, newProjectName, &err);
            else if (newProjectTemplate == 4) ok = createNeonSample(dir, newProjectName, &err);
            else ok = createProjectFromTemplate(dir, newProjectName,
                                                newProjectTemplate == 0 ? ProjectTemplate::Empty3D : ProjectTemplate::Empty2D, &err) != nullptr;
            if (ok) {
                openProject(dir);
                launcherView = 0;
            } else {
                setStatus("No se pudo crear: " + err);
                TITAN_ERROR("editor", "No se pudo crear el proyecto: %s", err.c_str());
            }
        }
        ImGui::EndDisabled();
        ImGui::EndChild();
    }
    ImGui::EndChild();
    ImGui::End();
}

} // namespace titan
