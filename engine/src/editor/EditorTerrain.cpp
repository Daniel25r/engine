// Herramientas de terreno del editor: panel de Detalles, esculpido y pintura con el dedo, pincel en la vista.
#include <algorithm>
#include <cmath>

#include "EditorImpl.h"
#include "imgui.h"
#include "stb_image.h"
#include "titan/core/FileIO.h"
#include "titan/core/TerrainTools.h"

namespace titan {

namespace {
// Punto del terreno bajo la pantalla, en el espacio local del terreno.
bool brushHit(Editor::Impl& m, Entity e, const Terrain& t, ImVec2 screen, Vec3& local, Mat4& world) {
    Vec3 o, d;
    if (!m.screenRay(screen, o, d)) return false;
    world = m.scene.computeWorldMatrix(e);
    const Mat4 inv = world.inverted();
    return raycastTerrain(t, inv.transformPoint(o), inv.transformDirection(d).normalized(), 5000, local);
}
} // namespace

void Editor::Impl::drawTerrainPanel(Entity e, Terrain& t) {
    terrainEnsureData(t);
    auto label = [&](const char* l) { propLabel(l); };
    label("Herramienta");
    static const char* tools[] = {"Ninguna", "Subir", "Bajar", "Suavizar", "Aplanar", "Pintar"};
    int tool = terrainTool + 1;
    if (ImGui::Combo("##ttool", &tool, tools, 6)) terrainTool = tool - 1;
    if (terrainTool >= 0) {
        label("Radio");
        ImGui::SliderFloat("##trad", &brushRadius, 0.5f, std::max(2.0f, t.size * 0.25f), "%.1f m");
        label("Fuerza");
        ImGui::SliderFloat("##tstr", &brushStrength, 0.1f, 20, "%.1f");
        if (terrainTool == static_cast<int>(SculptTool::Paint)) {
            label("Capa");
            ImGui::SliderInt("##tlayer", &terrainLayer, 1, 4);
        }
        propLabel("");
        ImGui::TextDisabled("Arrastra un dedo sobre el terreno; con dos, mueves la cámara");
    }
    float size = t.size, maxH = t.maxHeight;
    label("Tamaño");
    if (ImGui::DragFloat("##tsize", &size, 0.5f, 4, 4096, "%.0f m")) { t.size = std::max(4.0f, size); ++t.revision; }
    track();
    label("Altura máx.");
    if (ImGui::DragFloat("##tmaxh", &maxH, 0.25f, 1, 2000, "%.1f m")) { t.maxHeight = std::max(1.0f, maxH); ++t.revision; }
    track();
    label("Resolución");
    static int newRes = 0;
    if (newRes == 0) newRes = t.resolution;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
    ImGui::SliderInt("##tres", &newRes, 17, 257);
    ImGui::SameLine();
    if (ImGui::Button("Aplicar") && newRes != t.resolution) {
        pushUndo();
        resizeTerrain(t, newRes);
    }
    label("Generar");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.35f);
    int seed = static_cast<int>(terrainSeed);
    if (ImGui::InputInt("##tseed", &seed)) terrainSeed = static_cast<uint32_t>(std::max(0, seed));
    ImGui::SameLine();
    if (ImGui::Button("Montañas")) {
        pushUndo();
        generateTerrain(t, terrainSeed, 0.7f);
    }
    ImGui::SameLine();
    if (ImGui::Button("Llano")) {
        pushUndo();
        std::fill(t.heights.begin(), t.heights.end(), uint16_t(0));
        ++t.revision;
    }
    label("Mapa de alturas");
    assetCombo("##thmap", AssetType::Texture, heightmapTexture);
    if (!heightmapTexture.isNull()) {
        propLabel("");
        if (ImGui::Button("Importar como relieve", ImVec2(-1, 0))) {
            const AssetRecord* r = project ? project->assets().find(heightmapTexture) : nullptr;
            std::vector<uint8_t> bytes;
            int w = 0, h = 0, ch = 0;
            stbi_uc* px = nullptr;
            std::string text;
            if (r && fileio::readText(project->assetsDir() + "/" + r->path, text))
                px = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(text.data()), static_cast<int>(text.size()), &w, &h, &ch, 1);
            if (px) {
                std::vector<float> gray(size_t(w) * h);
                for (size_t i = 0; i < gray.size(); ++i) gray[i] = px[i] / 255.0f;
                stbi_image_free(px);
                pushUndo();
                importHeightmap(t, gray, w, h);
                setStatus("Relieve importado de " + r->path);
            } else {
                setStatus("No se pudo leer la imagen del mapa de alturas");
            }
        }
    }
    label("Repetición capas");
    ImGui::DragFloat("##ttile", &t.layerTiling, 0.1f, 0.1f, 500, "%.1f m");
    track();
    for (int i = 0; i < 4; ++i) {
        ImGui::PushID(i);
        const std::string name = "Capa " + std::to_string(i + 1);
        propLabel(name.c_str());
        ImGui::ColorEdit3("##tcol", &t.layerColors[i].r, ImGuiColorEditFlags_NoInputs);
        track();
        ImGui::SameLine();
        assetCombo("##ttex", AssetType::Texture, t.layerTextures[i]);
        ImGui::PopID();
    }
    (void)e;
}

bool Editor::Impl::terrainPointerDown(ImVec2 p) {
    if (terrainTool < 0 || touches.size() != 1 || runtime) return false;
    Entity e = selectedEntity();
    Terrain* t = e.isNull() ? nullptr : scene.world().get<Terrain>(e);
    if (!t) return false;
    Vec3 local;
    Mat4 world;
    if (!brushHit(*this, e, *t, p, local, world)) return false;
    beginEdit();
    sculpting = true;
    brushScreen = p;
    flattenHeight = local.y; // "Aplanar" lleva todo a la altura donde empezó el trazo
    return true;
}

void Editor::Impl::applySculpt(float dt) {
    if (!sculpting) return;
    Entity e = selectedEntity();
    Terrain* t = e.isNull() ? nullptr : scene.world().get<Terrain>(e);
    if (!t) {
        sculpting = false;
        return;
    }
    Vec3 local;
    Mat4 world;
    if (!brushHit(*this, e, *t, brushScreen, local, world)) return;
    const SculptTool tool = static_cast<SculptTool>(std::clamp(terrainTool, 0, 4));
    const float strength = tool == SculptTool::Smooth || tool == SculptTool::Flatten || tool == SculptTool::Paint
                               ? brushStrength * 0.5f // fracción por segundo
                               : brushStrength;      // metros por segundo
    sculptTerrain(*t, tool, local.x, local.z, brushRadius, strength, std::min(dt, 0.05f), flattenHeight, terrainLayer - 1);
    dirty = true;
}

void Editor::Impl::drawBrushGizmo() {
    if (terrainTool < 0 || runtime) return;
    Entity e = selectedEntity();
    const Terrain* t = e.isNull() ? nullptr : scene.world().get<Terrain>(e);
    if (!t) return;
    // El pincel se dibuja donde está el dedo (o el ratón en escritorio).
    const ImVec2 at = sculpting ? brushScreen : ImGui::GetIO().MousePos;
    Vec3 local;
    Mat4 world;
    if (!brushHit(*this, e, *t, at, local, world)) return;
    const Color col = terrainTool == static_cast<int>(SculptTool::Lower) ? Color{1, 0.4f, 0.3f, 1} : Color{1, 0.8f, 0.2f, 1};
    Vec3 prev;
    for (int i = 0; i <= 48; ++i) {
        const float a = i * 6.2831853f / 48;
        const float x = local.x + std::cos(a) * brushRadius, z = local.z + std::sin(a) * brushRadius;
        const Vec3 p = world.transformPoint({x, terrainHeightAt(*t, x, z) + 0.05f, z});
        if (i > 0) renderer.drawLine(prev, p, col);
        prev = p;
    }
}

} // namespace titan
