#pragma once
// Esqueleto (rig), pesos y animación por fotogramas clave de una malla editable.
//   - Huesos con cabeza y cola en el espacio de la malla y jerarquía (como las armaduras de Blender).
//   - Pesos por vértice (hasta 4 huesos), calculados automáticamente por distancia a los huesos.
//   - Clips con claves de rotación y traslación por hueso, relativas a la pose de reposo.
// Se guarda dentro del .tmesh y se escribe en el GLB como skin + animaciones glTF: el Animator del juego lo
// reproduce como cualquier personaje importado.
#include <array>
#include <string>
#include <vector>

#include "titan/core/Math.h"

namespace titan {

struct RigBone {
    std::string name;
    int parent = -1;
    Vec3 head, tail; // pose de reposo, espacio de la malla
};

struct RigKey {
    float time = 0;
    Quat rotation;    // giro alrededor de la cabeza del hueso, respecto al reposo
    Vec3 translation; // desplazamiento respecto al reposo (normalmente solo en la raíz)
};

struct RigTrack {
    int bone = -1;
    std::vector<RigKey> keys; // ordenadas por tiempo
};

struct RigClip {
    std::string name = "Animación";
    float duration = 1.0f;
    bool loop = true;
    std::vector<RigTrack> tracks;

    // Pone o sustituye la clave del hueso en ese tiempo (con 1/1000 s de tolerancia).
    void setKey(int bone, const RigKey& key);
    bool removeKey(int bone, float time);
    // Pose del hueso en t (interpolación lineal de posición y esférica de rotación entre claves).
    bool sample(int bone, float t, Quat& rotation, Vec3& translation) const;
    std::vector<float> keyTimes() const; // todas las claves (para la línea de tiempo)
};

struct Rig {
    std::vector<RigBone> bones;
    std::vector<std::array<int, 4>> joints;    // por vértice de la malla
    std::vector<std::array<float, 4>> weights; // suman 1
    std::vector<RigClip> clips;

    bool empty() const { return bones.empty(); }
    int find(const std::string& name) const;
    // Esqueleto humano (19 huesos) ajustado a la caja de la malla, en pose T, con Y arriba y mirando a +Z.
    static Rig humanoid(Vec3 boxMin, Vec3 boxMax);
    // Pesos automáticos: cada vértice, de los 4 huesos más cercanos (inverso de la distancia al segmento del hueso).
    void autoWeights(const std::vector<Vec3>& verts);
    // Matrices de cada hueso para la pose de un clip en t (pose de reposo si clip < 0): skin[i] = mundo · inversa
    // del reposo. Las mismas que calcula el motor al reproducir el glTF.
    std::vector<Mat4> skinMatrices(int clip, float t) const;
    // Vértices deformados por la pose (vista previa en el editor).
    std::vector<Vec3> deform(const std::vector<Vec3>& verts, const std::vector<Mat4>& skin) const;
};

} // namespace titan
