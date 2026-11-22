#include "titan/core/TerrainTools.h"

#include <algorithm>
#include <cmath>

namespace titan {

namespace {
int res(const Terrain& t) { return std::clamp(t.resolution, 2, 1025); }
float cell(const Terrain& t) { return t.size / float(res(t) - 1); }
float hAt(const Terrain& t, int i, int j) {
    const int n = res(t);
    i = std::clamp(i, 0, n - 1);
    j = std::clamp(j, 0, n - 1);
    return t.heights.empty() ? 0.0f : t.heights[size_t(j) * n + i] / 65535.0f * t.maxHeight;
}
void setH(Terrain& t, int i, int j, float h) {
    const int n = res(t);
    t.heights[size_t(j) * n + i] = static_cast<uint16_t>(std::clamp(h / std::max(t.maxHeight, 1e-4f), 0.0f, 1.0f) * 65535.0f + 0.5f);
}
// Ruido de valor 2D suave (para generar montañas).
float hash(int x, int y, uint32_t seed) {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return ((h ^ (h >> 16)) & 0xFFFF) / 65535.0f;
}
float valueNoise(float x, float y, uint32_t seed) {
    const int xi = static_cast<int>(std::floor(x)), yi = static_cast<int>(std::floor(y));
    const float fx = x - xi, fy = y - yi;
    const float u = fx * fx * (3 - 2 * fx), v = fy * fy * (3 - 2 * fy);
    const float a = hash(xi, yi, seed), b = hash(xi + 1, yi, seed), c = hash(xi, yi + 1, seed), d = hash(xi + 1, yi + 1, seed);
    return (a + (b - a) * u) + ((c + (d - c) * u) - (a + (b - a) * u)) * v;
}
} // namespace

void terrainEnsureData(Terrain& t) {
    t.resolution = std::clamp(t.resolution, 17, 257);
    const size_t n = size_t(t.resolution) * t.resolution;
    if (t.heights.size() != n) t.heights.assign(n, 0);
    if (t.splat.size() != n * 4) {
        t.splat.assign(n * 4, 0);
        for (size_t i = 0; i < n; ++i) t.splat[i * 4] = 255;
    }
}

float terrainHeightAt(const Terrain& t, float x, float z) {
    const float c = cell(t);
    const float gx = (x + t.size * 0.5f) / c, gz = (z + t.size * 0.5f) / c;
    const int i = static_cast<int>(std::floor(gx)), j = static_cast<int>(std::floor(gz));
    const float fx = gx - i, fz = gz - j;
    const float h00 = hAt(t, i, j), h10 = hAt(t, i + 1, j), h01 = hAt(t, i, j + 1), h11 = hAt(t, i + 1, j + 1);
    return (h00 + (h10 - h00) * fx) + ((h01 + (h11 - h01) * fx) - (h00 + (h10 - h00) * fx)) * fz;
}

Vec3 terrainNormalAt(const Terrain& t, float x, float z) {
    const float e = cell(t);
    const float dx = terrainHeightAt(t, x + e, z) - terrainHeightAt(t, x - e, z);
    const float dz = terrainHeightAt(t, x, z + e) - terrainHeightAt(t, x, z - e);
    return Vec3{-dx, 2 * e, -dz}.normalized();
}

AABB terrainBounds(const Terrain& t) {
    float top = 0;
    for (uint16_t h : t.heights) top = std::max(top, h / 65535.0f * t.maxHeight);
    return {{-t.size * 0.5f, 0, -t.size * 0.5f}, {t.size * 0.5f, std::max(top, 0.01f), t.size * 0.5f}};
}

TerrainMesh buildTerrainMesh(const Terrain& t) {
    TerrainMesh m;
    const int n = res(t);
    const float c = cell(t), half = t.size * 0.5f;
    m.vertices.resize(size_t(n) * n);
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            TerrainVertex& v = m.vertices[size_t(j) * n + i];
            const float x = -half + i * c, z = -half + j * c;
            v.position = {x, hAt(t, i, j), z};
            const float dx = hAt(t, i + 1, j) - hAt(t, i - 1, j), dz = hAt(t, i, j + 1) - hAt(t, i, j - 1);
            v.normal = Vec3{-dx, 2 * c, -dz}.normalized();
            v.uv = {float(i) / (n - 1), float(j) / (n - 1)};
            const size_t s = (size_t(j) * n + i) * 4;
            for (int k = 0; k < 4; ++k) v.splat[k] = t.splat.size() > s + 3 ? t.splat[s + k] : (k == 0 ? 255 : 0);
        }
    m.indices.reserve(size_t(n - 1) * (n - 1) * 6);
    for (int j = 0; j + 1 < n; ++j)
        for (int i = 0; i + 1 < n; ++i) {
            const uint32_t a = uint32_t(j * n + i), b = a + 1, cc = a + uint32_t(n), d = cc + 1;
            // Antihorario visto desde arriba (+Y).
            m.indices.insert(m.indices.end(), {a, cc, b, b, cc, d});
        }
    return m;
}

bool raycastTerrain(const Terrain& t, const Vec3& o, const Vec3& dirIn, float maxDistance, Vec3& hit) {
    const Vec3 dir = dirIn.normalized();
    const float stepLen = cell(t) * 0.5f;
    float prev = 0;
    float prevDiff = o.y - terrainHeightAt(t, o.x, o.z);
    const float half = t.size * 0.5f;
    for (float d = stepLen; d <= maxDistance; d += stepLen) {
        const Vec3 p = o + dir * d;
        const bool inside = p.x >= -half && p.x <= half && p.z >= -half && p.z <= half;
        const float diff = p.y - terrainHeightAt(t, p.x, p.z);
        if (inside && prevDiff > 0 && diff <= 0) {
            // Bisección entre el último punto por encima y este (por debajo).
            float lo = prev, hi = d;
            for (int k = 0; k < 16; ++k) {
                const float mid = (lo + hi) * 0.5f;
                const Vec3 q = o + dir * mid;
                (q.y - terrainHeightAt(t, q.x, q.z) > 0 ? lo : hi) = mid;
            }
            hit = o + dir * hi;
            return true;
        }
        prev = d;
        prevDiff = diff;
    }
    return false;
}

void sculptTerrain(Terrain& t, SculptTool tool, float x, float z, float radius, float strength, float dt,
                   float flattenHeight, int paintLayer) {
    terrainEnsureData(t);
    const int n = t.resolution;
    const float c = cell(t), half = t.size * 0.5f;
    radius = std::max(radius, c * 0.5f);
    const int i0 = std::max(0, static_cast<int>(std::floor((x - radius + half) / c)));
    const int i1 = std::min(n - 1, static_cast<int>(std::ceil((x + radius + half) / c)));
    const int j0 = std::max(0, static_cast<int>(std::floor((z - radius + half) / c)));
    const int j1 = std::min(n - 1, static_cast<int>(std::ceil((z + radius + half) / c)));
    if (i0 > i1 || j0 > j1) return;
    std::vector<float> before;
    if (tool == SculptTool::Smooth) { // promedio con los vecinos (leyendo una copia)
        before.resize(size_t(n) * n);
        for (int j = 0; j < n; ++j)
            for (int i = 0; i < n; ++i) before[size_t(j) * n + i] = hAt(t, i, j);
    }
    for (int j = j0; j <= j1; ++j)
        for (int i = i0; i <= i1; ++i) {
            const float px = -half + i * c, pz = -half + j * c;
            const float d = std::sqrt((px - x) * (px - x) + (pz - z) * (pz - z));
            if (d > radius) continue;
            const float f = 0.5f + 0.5f * std::cos(d / radius * 3.14159265f); // caída suave hasta el borde
            const float amount = strength * dt * f;
            const float h = hAt(t, i, j);
            switch (tool) {
            case SculptTool::Raise: setH(t, i, j, h + amount); break;
            case SculptTool::Lower: setH(t, i, j, h - amount); break;
            case SculptTool::Flatten: setH(t, i, j, h + (flattenHeight - h) * std::min(1.0f, amount)); break;
            case SculptTool::Smooth: {
                float sum = 0;
                int cnt = 0;
                for (int dj = -1; dj <= 1; ++dj)
                    for (int di = -1; di <= 1; ++di) {
                        const int a = std::clamp(i + di, 0, n - 1), b = std::clamp(j + dj, 0, n - 1);
                        sum += before[size_t(b) * n + a];
                        ++cnt;
                    }
                setH(t, i, j, h + (sum / cnt - h) * std::min(1.0f, amount));
                break;
            }
            case SculptTool::Paint: {
                uint8_t* w = &t.splat[(size_t(j) * n + i) * 4];
                const float k = std::min(1.0f, amount);
                float total = 0;
                float v[4];
                for (int l = 0; l < 4; ++l) {
                    v[l] = w[l] / 255.0f * (1 - k) + (l == std::clamp(paintLayer, 0, 3) ? k : 0.0f);
                    total += v[l];
                }
                for (int l = 0; l < 4; ++l) w[l] = static_cast<uint8_t>(std::clamp(v[l] / std::max(total, 1e-6f), 0.0f, 1.0f) * 255 + 0.5f);
                break;
            }
            }
        }
    ++t.revision;
}

void generateTerrain(Terrain& t, uint32_t seed, float amount, float scale) {
    terrainEnsureData(t);
    const int n = t.resolution;
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            const float x = float(i) / (n - 1) * 4 * scale, z = float(j) / (n - 1) * 4 * scale;
            float h = 0, amp = 1, freq = 1, norm = 0;
            for (int o = 0; o < 5; ++o) { // fBm: 5 octavas
                h += valueNoise(x * freq, z * freq, seed + o * 101) * amp;
                norm += amp;
                amp *= 0.5f;
                freq *= 2.0f;
            }
            h /= norm;
            h = std::pow(h, 1.6f); // valles más anchos, cimas más marcadas
            setH(t, i, j, h * std::clamp(amount, 0.0f, 1.0f) * t.maxHeight);
        }
    ++t.revision;
}

void importHeightmap(Terrain& t, const std::vector<float>& gray, int w, int h) {
    terrainEnsureData(t);
    if (w < 1 || h < 1 || gray.size() < size_t(w) * h) return;
    const int n = t.resolution;
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            const float sx = float(i) / (n - 1) * (w - 1), sy = float(j) / (n - 1) * (h - 1);
            const int x0 = static_cast<int>(sx), y0 = static_cast<int>(sy);
            const int x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
            const float fx = sx - x0, fy = sy - y0;
            auto g = [&](int x, int y) { return gray[size_t(y) * w + x]; };
            const float v = (g(x0, y0) * (1 - fx) + g(x1, y0) * fx) * (1 - fy) + (g(x0, y1) * (1 - fx) + g(x1, y1) * fx) * fy;
            setH(t, i, j, std::clamp(v, 0.0f, 1.0f) * t.maxHeight);
        }
    ++t.revision;
}

void resizeTerrain(Terrain& t, int resolution) {
    terrainEnsureData(t);
    resolution = std::clamp(resolution, 17, 257);
    if (resolution == t.resolution) return;
    Terrain out = t;
    out.resolution = resolution;
    out.heights.assign(size_t(resolution) * resolution, 0);
    out.splat.assign(size_t(resolution) * resolution * 4, 0);
    const float half = t.size * 0.5f, c = t.size / (resolution - 1);
    const int n = t.resolution;
    for (int j = 0; j < resolution; ++j)
        for (int i = 0; i < resolution; ++i) {
            const float x = -half + i * c, z = -half + j * c;
            setH(out, i, j, terrainHeightAt(t, x, z));
            const int si = std::clamp(static_cast<int>(std::round(float(i) / (resolution - 1) * (n - 1))), 0, n - 1);
            const int sj = std::clamp(static_cast<int>(std::round(float(j) / (resolution - 1) * (n - 1))), 0, n - 1);
            for (int k = 0; k < 4; ++k) out.splat[(size_t(j) * resolution + i) * 4 + k] = t.splat[(size_t(sj) * n + si) * 4 + k];
        }
    out.revision = t.revision + 1;
    t = std::move(out);
}

std::string base64Encode(const uint8_t* d, size_t n) {
    static const char* k = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((n + 2) / 3 * 4);
    for (size_t i = 0; i < n; i += 3) {
        const uint32_t v = (uint32_t(d[i]) << 16) | (i + 1 < n ? uint32_t(d[i + 1]) << 8 : 0) | (i + 2 < n ? d[i + 2] : 0);
        out += k[(v >> 18) & 63];
        out += k[(v >> 12) & 63];
        out += i + 1 < n ? k[(v >> 6) & 63] : '=';
        out += i + 2 < n ? k[v & 63] : '=';
    }
    return out;
}

bool base64Decode(const std::string& s, std::vector<uint8_t>& out) {
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    out.clear();
    if (s.size() % 4 != 0) return false;
    out.reserve(s.size() / 4 * 3);
    for (size_t i = 0; i < s.size(); i += 4) {
        int v[4];
        for (int k = 0; k < 4; ++k) {
            v[k] = s[i + k] == '=' ? 0 : val(s[i + k]);
            if (v[k] < 0) return false;
        }
        const uint32_t x = (uint32_t(v[0]) << 18) | (uint32_t(v[1]) << 12) | (uint32_t(v[2]) << 6) | uint32_t(v[3]);
        out.push_back(static_cast<uint8_t>(x >> 16));
        if (s[i + 2] != '=') out.push_back(static_cast<uint8_t>(x >> 8));
        if (s[i + 3] != '=') out.push_back(static_cast<uint8_t>(x));
    }
    return true;
}

} // namespace titan
