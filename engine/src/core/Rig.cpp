// Esqueleto, pesos automáticos, clips de animación y deformación de la malla (skinning en la CPU para la vista
// previa; en el juego lo hace la GPU con las mismas matrices).
#include "titan/core/Rig.h"

#include <algorithm>
#include <cmath>

namespace titan {

namespace {

Quat slerp(Quat a, Quat b, float t) {
    float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (d < 0) { // el camino corto
        b = {-b.x, -b.y, -b.z, -b.w};
        d = -d;
    }
    if (d > 0.9995f) {
        Quat r{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t};
        return r.normalized();
    }
    const float th = std::acos(d), s = std::sin(th);
    const float wa = std::sin((1 - t) * th) / s, wb = std::sin(t * th) / s;
    return Quat{a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb};
}

float segmentDistance(Vec3 p, Vec3 a, Vec3 b) {
    const Vec3 ab = b - a;
    const float l2 = dot(ab, ab);
    const float t = l2 > 1e-12f ? std::clamp(dot(p - a, ab) / l2, 0.0f, 1.0f) : 0.0f;
    return (p - (a + ab * t)).length();
}

} // namespace

// ---------------------------------------------------------------- clips
void RigClip::setKey(int bone, const RigKey& key) {
    RigTrack* track = nullptr;
    for (RigTrack& t : tracks)
        if (t.bone == bone) track = &t;
    if (!track) {
        tracks.push_back({bone, {}});
        track = &tracks.back();
    }
    for (RigKey& k : track->keys)
        if (std::fabs(k.time - key.time) < 1e-3f) {
            k = key;
            return;
        }
    track->keys.push_back(key);
    std::sort(track->keys.begin(), track->keys.end(), [](const RigKey& a, const RigKey& b) { return a.time < b.time; });
    duration = std::max(duration, key.time);
}

bool RigClip::removeKey(int bone, float time) {
    for (RigTrack& t : tracks)
        if (t.bone == bone)
            for (size_t i = 0; i < t.keys.size(); ++i)
                if (std::fabs(t.keys[i].time - time) < 1e-3f) {
                    t.keys.erase(t.keys.begin() + long(i));
                    return true;
                }
    return false;
}

bool RigClip::sample(int bone, float t, Quat& r, Vec3& tr) const {
    for (const RigTrack& track : tracks) {
        if (track.bone != bone || track.keys.empty()) continue;
        const std::vector<RigKey>& k = track.keys;
        if (t <= k.front().time) {
            r = k.front().rotation;
            tr = k.front().translation;
        } else if (t >= k.back().time) {
            r = k.back().rotation;
            tr = k.back().translation;
        } else {
            size_t i = 1;
            while (i < k.size() && k[i].time < t) ++i;
            const RigKey &a = k[i - 1], &b = k[i];
            const float u = (t - a.time) / std::max(1e-6f, b.time - a.time);
            r = slerp(a.rotation, b.rotation, u);
            tr = a.translation + (b.translation - a.translation) * u;
        }
        return true;
    }
    return false;
}

std::vector<float> RigClip::keyTimes() const {
    std::vector<float> out;
    for (const RigTrack& t : tracks)
        for (const RigKey& k : t.keys)
            if (std::none_of(out.begin(), out.end(), [&](float x) { return std::fabs(x - k.time) < 1e-3f; })) out.push_back(k.time);
    std::sort(out.begin(), out.end());
    return out;
}

// ---------------------------------------------------------------- esqueleto
int Rig::find(const std::string& name) const {
    for (size_t i = 0; i < bones.size(); ++i)
        if (bones[i].name == name) return int(i);
    return -1;
}

Rig Rig::humanoid(Vec3 mn, Vec3 mx) {
    Rig r;
    const float H = std::max(1e-3f, mx.y - mn.y), y0 = mn.y;
    const float cx = (mn.x + mx.x) * 0.5f, cz = (mn.z + mx.z) * 0.5f;
    // Si el modelo es ancho (pose T), los brazos llegan hasta los lados de la caja; si no, proporciones estándar.
    const float halfW = (mx.x - mn.x) * 0.5f;
    const float armEnd = halfW > 0.3f * H ? halfW * 0.97f : 0.45f * H;
    auto P = [&](float x, float yFrac, float z = 0) { return Vec3{cx + x, y0 + yFrac * H, cz + z}; };
    auto add = [&](const char* name, int parent, Vec3 head, Vec3 tail) {
        r.bones.push_back({name, parent, head, tail});
        return int(r.bones.size() - 1);
    };
    const int hips = add("Cadera", -1, P(0, 0.50f), P(0, 0.58f));
    const int spine = add("Columna", hips, P(0, 0.58f), P(0, 0.68f));
    const int chest = add("Pecho", spine, P(0, 0.68f), P(0, 0.80f));
    const int neck = add("Cuello", chest, P(0, 0.80f), P(0, 0.86f));
    add("Cabeza", neck, P(0, 0.86f), P(0, 1.0f));
    for (int side = -1; side <= 1; side += 2) {
        const bool left = side > 0; // la izquierda del personaje está en +X (mirando a +Z)
        const float s = float(side);
        const int sh = add(left ? "Hombro.I" : "Hombro.D", chest, P(s * 0.03f * H, 0.79f), P(s * 0.11f * H, 0.80f));
        const float shoulderX = 0.11f * H, reach = armEnd - shoulderX;
        const int up = add(left ? "Brazo.I" : "Brazo.D", sh, P(s * shoulderX, 0.80f), P(s * (shoulderX + reach * 0.45f), 0.80f));
        const int fore = add(left ? "Antebrazo.I" : "Antebrazo.D", up, P(s * (shoulderX + reach * 0.45f), 0.80f),
                             P(s * (shoulderX + reach * 0.85f), 0.80f));
        add(left ? "Mano.I" : "Mano.D", fore, P(s * (shoulderX + reach * 0.85f), 0.80f), P(s * armEnd, 0.80f));
        const int thigh = add(left ? "Muslo.I" : "Muslo.D", hips, P(s * 0.09f * H, 0.50f), P(s * 0.09f * H, 0.27f));
        const int shin = add(left ? "Pierna.I" : "Pierna.D", thigh, P(s * 0.09f * H, 0.27f), P(s * 0.09f * H, 0.05f));
        add(left ? "Pie.I" : "Pie.D", shin, P(s * 0.09f * H, 0.05f), P(s * 0.09f * H, 0.0f, 0.12f * H));
    }
    return r;
}

void Rig::autoWeights(const std::vector<Vec3>& verts) {
    joints.assign(verts.size(), {0, 0, 0, 0});
    weights.assign(verts.size(), {1, 0, 0, 0});
    if (bones.empty()) return;
    std::vector<std::pair<float, int>> d(bones.size());
    for (size_t v = 0; v < verts.size(); ++v) {
        for (size_t b = 0; b < bones.size(); ++b) d[b] = {segmentDistance(verts[v], bones[b].head, bones[b].tail), int(b)};
        const size_t n = std::min<size_t>(4, d.size());
        std::partial_sort(d.begin(), d.begin() + long(n), d.end());
        // Inverso de la distancia a la cuarta: el hueso más cercano domina y las uniones se mezclan suavemente.
        float sum = 0;
        std::array<float, 4> w{0, 0, 0, 0};
        for (size_t k = 0; k < n; ++k) {
            const float x = d[k].first + 1e-4f;
            w[k] = 1.0f / (x * x * x * x);
            sum += w[k];
        }
        for (size_t k = 0; k < 4; ++k) {
            joints[v][k] = k < n ? d[k].second : 0;
            weights[v][k] = k < n ? w[k] / sum : 0;
        }
    }
}

std::vector<Mat4> Rig::skinMatrices(int clip, float t) const {
    std::vector<Mat4> world(bones.size()), skin(bones.size());
    for (size_t i = 0; i < bones.size(); ++i) { // los padres van antes que los hijos (se crean en ese orden)
        const RigBone& b = bones[i];
        const Vec3 restLocal = b.parent >= 0 ? b.head - bones[size_t(b.parent)].head : b.head;
        Quat r;
        Vec3 tr;
        if (clip >= 0 && size_t(clip) < clips.size()) clips[size_t(clip)].sample(int(i), t, r, tr);
        const Mat4 local = Mat4::translate(restLocal + tr) * Mat4::rotate(r);
        world[i] = b.parent >= 0 ? world[size_t(b.parent)] * local : local;
        skin[i] = world[i] * Mat4::translate(b.head * -1.0f); // inversa del reposo: los huesos no giran en reposo
    }
    return skin;
}

std::vector<Vec3> Rig::deform(const std::vector<Vec3>& verts, const std::vector<Mat4>& skin) const {
    if (skin.empty() || joints.size() != verts.size()) return verts;
    std::vector<Vec3> out(verts.size());
    for (size_t v = 0; v < verts.size(); ++v) {
        Vec3 p;
        for (int k = 0; k < 4; ++k)
            if (weights[v][size_t(k)] > 0) p += skin[size_t(joints[v][size_t(k)])].transformPoint(verts[v]) * weights[v][size_t(k)];
        out[v] = p;
    }
    return out;
}

} // namespace titan
