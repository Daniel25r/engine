// Mundo de bloques: ruido, biomas, generación, mallas con fusión de caras, rayo DDA y guardado RLE.
#include "titan/core/Voxel.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "titan/core/FileIO.h"
#include "titan/core/Serialization.h"

namespace fs = std::filesystem;

namespace titan {

// ---------------------------------------------------------------- tipos de bloque
BlockRegistry::BlockRegistry() {
    types_.push_back({"Aire", {}, {}, false});
    auto c = [](uint32_t hex) { return Color::hex(hex); };
    add({"Hierba", c(0x5FA83A), c(0x7A6A3E), true});
    add({"Tierra", c(0x7A5A3A), c(0x7A5A3A), true});
    add({"Piedra", c(0x8A8D93), c(0x7E8187), true});
    add({"Arena", c(0xE3D29A), c(0xD9C68C), true});
    add({"Nieve", c(0xF4F7FA), c(0xDDE3EA), true});
    add({"Agua", c(0x2E6FB8), c(0x2A64A8), false});
    add({"Madera", c(0x9A7046), c(0x6E4A2A), true});
    add({"Hojas", c(0x3E8A34), c(0x367A2E), true});
    add({"Roca volcánica", c(0x3A3334), c(0x332D2E), true});
    add({"Lava", c(0xFF6A1A), c(0xE85A10), false});
    add({"Grava", c(0x8E8680), c(0x857D77), true});
    add({"Hielo", c(0xB8DDF2), c(0xA6D0EA), true});
    add({"Ladrillo", c(0xA84A3A), c(0x9A4232), true});
    add({"Tablones", c(0xC49A62), c(0xB48A54), true});
}

BlockId BlockRegistry::add(const BlockType& t) {
    if (types_.size() >= 256) return kAir;
    types_.push_back(t);
    return BlockId(types_.size() - 1);
}

BlockId BlockRegistry::find(const std::string& name) const {
    for (size_t i = 1; i < types_.size(); ++i)
        if (types_[i].name == name) return BlockId(i);
    return kAir;
}

const char* biomeName(Biome b) {
    static const char* n[] = {"Océano", "Playa", "Llanura", "Bosque", "Desierto", "Montaña", "Nieve", "Volcánico"};
    return n[int(b)];
}

BlockId VoxelChunk::get(int x, int y, int z) const {
    if (x < 0 || x >= kChunkSize || z < 0 || z >= kChunkSize || y < 0 || y >= kChunkHeight) return kAir;
    return blocks[size_t(index(x, y, z))];
}
void VoxelChunk::set(int x, int y, int z, BlockId b) {
    if (x < 0 || x >= kChunkSize || z < 0 || z >= kChunkSize || y < 0 || y >= kChunkHeight) return;
    blocks[size_t(index(x, y, z))] = b;
}

// ---------------------------------------------------------------- ruido
namespace {

uint32_t hash3(uint32_t seed, int x, int y, int z) {
    uint32_t h = seed * 0x9E3779B1u ^ uint32_t(x) * 0x85EBCA77u ^ uint32_t(y) * 0xC2B2AE3Du ^ uint32_t(z) * 0x27D4EB2Fu;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return h;
}
float fade(float t) { return t * t * t * (t * (t * 6 - 15) + 10); }
float lerp(float a, float b, float t) { return a + (b - a) * t; }
float grad2(uint32_t h, float x, float z) {
    const float a = float(h & 0xFFFF) / 65536.0f * 6.2831853f;
    return std::cos(a) * x + std::sin(a) * z;
}
float grad3(uint32_t h, float x, float y, float z) {
    switch (h % 12) { // las 12 direcciones de las aristas de un cubo
    case 0: return x + y;
    case 1: return -x + y;
    case 2: return x - y;
    case 3: return -x - y;
    case 4: return x + z;
    case 5: return -x + z;
    case 6: return x - z;
    case 7: return -x - z;
    case 8: return y + z;
    case 9: return -y + z;
    case 10: return y - z;
    default: return -y - z;
    }
}
int floorInt(float v) { return int(std::floor(v)); }
int floorDiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

} // namespace

float noise2(uint32_t seed, float x, float z) {
    const int x0 = floorInt(x), z0 = floorInt(z);
    const float fx = x - float(x0), fz = z - float(z0);
    const float n00 = grad2(hash3(seed, x0, 0, z0), fx, fz), n10 = grad2(hash3(seed, x0 + 1, 0, z0), fx - 1, fz);
    const float n01 = grad2(hash3(seed, x0, 0, z0 + 1), fx, fz - 1), n11 = grad2(hash3(seed, x0 + 1, 0, z0 + 1), fx - 1, fz - 1);
    const float u = fade(fx), v = fade(fz);
    return lerp(lerp(n00, n10, u), lerp(n01, n11, u), v) * 1.4142f;
}

float noise3(uint32_t seed, float x, float y, float z) {
    const int x0 = floorInt(x), y0 = floorInt(y), z0 = floorInt(z);
    const float fx = x - float(x0), fy = y - float(y0), fz = z - float(z0);
    float c[2][2][2];
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2; ++j)
            for (int k = 0; k < 2; ++k) c[i][j][k] = grad3(hash3(seed, x0 + i, y0 + j, z0 + k), fx - float(i), fy - float(j), fz - float(k));
    const float u = fade(fx), v = fade(fy), w = fade(fz);
    return lerp(lerp(lerp(c[0][0][0], c[1][0][0], u), lerp(c[0][1][0], c[1][1][0], u), v),
                lerp(lerp(c[0][0][1], c[1][0][1], u), lerp(c[0][1][1], c[1][1][1], u), v), w);
}

float fbm2(uint32_t seed, float x, float z, int octaves) {
    float sum = 0, amp = 1, norm = 0, f = 1;
    for (int o = 0; o < octaves; ++o) {
        sum += noise2(seed + uint32_t(o) * 101u, x * f, z * f) * amp;
        norm += amp;
        amp *= 0.5f;
        f *= 2;
    }
    return sum / norm;
}

// ---------------------------------------------------------------- generación
namespace {

struct Column {
    int height;
    Biome biome;
};

Column columnAt(const VoxelGenSettings& s, int wx, int wz) {
    const float x = float(wx), z = float(wz);
    const float continent = fbm2(s.seed, x * 0.004f, z * 0.004f, 4);
    const float mountain = std::max(0.0f, fbm2(s.seed + 11, x * 0.008f, z * 0.008f, 4));
    const float detail = fbm2(s.seed + 23, x * 0.04f, z * 0.04f, 3);
    const float temp = fbm2(s.seed + 37, x * 0.003f, z * 0.003f, 3);
    const float humid = fbm2(s.seed + 53, x * 0.003f, z * 0.003f, 3);
    float h = float(s.seaLevel) + 3 + continent * 14 + mountain * mountain * 70 * s.terrainScale + detail * 3;
    const int height = std::clamp(int(h), 2, kChunkHeight - 8);
    Biome b;
    if (height < s.seaLevel - 1) b = Biome::Ocean;
    else if (height <= s.seaLevel + 1) b = Biome::Beach;
    else if (height > s.seaLevel + 24) b = temp < 0.05f ? Biome::Snow : Biome::Mountains;
    else if (temp > 0.3f && humid < -0.05f) b = noise2(s.seed + 71, x * 0.02f, z * 0.02f) > 0.45f ? Biome::Volcanic : Biome::Desert;
    else if (temp < -0.3f) b = Biome::Snow;
    else if (humid > 0.05f) b = Biome::Forest;
    else b = Biome::Plains;
    return {height, b};
}

} // namespace

Biome biomeAt(const VoxelGenSettings& s, int wx, int wz) { return columnAt(s, wx, wz).biome; }

void generateChunk(const VoxelGenSettings& s, const BlockRegistry& reg, VoxelChunk& c) {
    std::fill(c.blocks.begin(), c.blocks.end(), kAir);
    const BlockId grass = reg.find("Hierba"), dirt = reg.find("Tierra"), stone = reg.find("Piedra"), sand = reg.find("Arena");
    const BlockId snow = reg.find("Nieve"), water = reg.find("Agua"), wood = reg.find("Madera"), leaves = reg.find("Hojas");
    const BlockId volcanic = reg.find("Roca volcánica"), lava = reg.find("Lava"), gravel = reg.find("Grava"), ice = reg.find("Hielo");
    Column cols[kChunkSize][kChunkSize];
    for (int z = 0; z < kChunkSize; ++z)
        for (int x = 0; x < kChunkSize; ++x) {
            const int wx = c.cx * kChunkSize + x, wz = c.cz * kChunkSize + z;
            const Column col = columnAt(s, wx, wz);
            cols[x][z] = col;
            for (int y = 0; y <= col.height; ++y) {
                const int depth = col.height - y;
                BlockId b = stone;
                switch (col.biome) {
                case Biome::Ocean: b = depth < 2 ? (y % 3 ? sand : gravel) : stone; break;
                case Biome::Beach:
                case Biome::Desert: b = depth < 4 ? sand : stone; break;
                case Biome::Mountains: b = (depth == 0 && col.height > s.seaLevel + 34) ? snow : stone; break;
                case Biome::Snow: b = depth == 0 ? snow : depth < 3 ? dirt : stone; break;
                case Biome::Volcanic: b = volcanic; break;
                default: b = depth == 0 ? grass : depth < 4 ? dirt : stone;
                }
                c.set(x, y, z, b);
            }
            for (int y = col.height + 1; y <= s.seaLevel; ++y) c.set(x, y, z, col.biome == Biome::Snow && y == s.seaLevel ? ice : water);
            if (col.biome == Biome::Volcanic && noise2(s.seed + 91, float(wx) * 0.15f, float(wz) * 0.15f) > 0.55f)
                c.set(x, col.height, z, lava); // pozas de lava
            // Cuevas: túneles donde el ruido 3D es alto, sin abrir agujeros bajo el agua.
            if (s.caves)
                for (int y = 2; y < col.height - 3; ++y)
                    if (noise3(s.seed + 7, float(wx) * 0.07f, float(y) * 0.11f, float(wz) * 0.07f) > 0.42f) c.set(x, y, z, kAir);
        }
    // Árboles: solo si caben enteros dentro del chunk (así cada chunk se genera sin mirar a sus vecinos).
    if (s.trees)
        for (int z = 2; z < kChunkSize - 2; ++z)
            for (int x = 2; x < kChunkSize - 2; ++x) {
                const Column& col = cols[x][z];
                const float density = col.biome == Biome::Forest ? 0.045f : col.biome == Biome::Plains ? 0.006f : 0.0f;
                const int wx = c.cx * kChunkSize + x, wz = c.cz * kChunkSize + z;
                if (density == 0 || float(hash3(s.seed + 5, wx, 0, wz) % 10000) / 10000.0f > density) continue;
                if (c.get(x, col.height, z) != grass || col.height + 8 >= kChunkHeight) continue;
                const int trunk = 4 + int(hash3(s.seed + 6, wx, 1, wz) % 2);
                for (int y = 1; y <= trunk; ++y) c.set(x, col.height + y, z, wood);
                const int top = col.height + trunk;
                for (int dy = -2; dy <= 1; ++dy)
                    for (int dz = -2; dz <= 2; ++dz)
                        for (int dx = -2; dx <= 2; ++dx) {
                            const int r = dy == 1 ? 1 : 2;
                            if (std::abs(dx) > r || std::abs(dz) > r || (std::abs(dx) == 2 && std::abs(dz) == 2)) continue;
                            if (c.get(x + dx, top + dy, z + dz) == kAir) c.set(x + dx, top + dy, z + dz, leaves);
                        }
            }
}

// ---------------------------------------------------------------- mallas (greedy meshing)
MeshBuild meshChunk(const PaddedChunk& p, const BlockRegistry& reg) {
    MeshBuild out;
    const int dims[3] = {kChunkSize, kChunkHeight, kChunkSize};
    std::unordered_map<int, size_t> partOf; // bloque * 2 + (cara de arriba)
    auto blockAt = [&](const int q[3]) { return p.b[size_t(PaddedChunk::index(q[0], q[1], q[2]))]; };
    // ¿Se ve la cara del bloque a hacia el vecino b? Sólido junto a no sólido, o agua/lava junto a aire.
    auto visible = [&](BlockId a, BlockId b) {
        if (a == kAir) return false;
        if (reg.get(a).solid) return !reg.get(b).solid;
        return b == kAir;
    };
    const float ox = float(p.cx * kChunkSize), oz = float(p.cz * kChunkSize);
    std::vector<BlockId> mask;
    for (int d = 0; d < 3; ++d) {
        const int u = (d + 1) % 3, v = (d + 2) % 3;
        mask.assign(size_t(dims[u]) * size_t(dims[v]), kAir);
        for (int s = -1; s <= 1; s += 2) {
            for (int i = 0; i < dims[d]; ++i) {
                // Máscara de caras visibles en este corte.
                for (int b = 0; b < dims[v]; ++b)
                    for (int a = 0; a < dims[u]; ++a) {
                        int q[3], n[3];
                        q[d] = i;
                        q[u] = a;
                        q[v] = b;
                        n[0] = q[0];
                        n[1] = q[1];
                        n[2] = q[2];
                        n[d] += s;
                        const BlockId here = blockAt(q), there = blockAt(n);
                        mask[size_t(b) * size_t(dims[u]) + size_t(a)] = visible(here, there) ? here : kAir;
                    }
                // Fusión: rectángulos máximos del mismo bloque.
                for (int b = 0; b < dims[v]; ++b)
                    for (int a = 0; a < dims[u];) {
                        const BlockId id = mask[size_t(b) * size_t(dims[u]) + size_t(a)];
                        if (id == kAir) {
                            ++a;
                            continue;
                        }
                        int w = 1;
                        while (a + w < dims[u] && mask[size_t(b) * size_t(dims[u]) + size_t(a + w)] == id) ++w;
                        int h = 1; // crece hacia v mientras toda la fila siguiente sea del mismo bloque
                        while (b + h < dims[v]) {
                            bool rowOk = true;
                            for (int k = 0; k < w && rowOk; ++k)
                                rowOk = mask[size_t(b + h) * size_t(dims[u]) + size_t(a + k)] == id;
                            if (!rowOk) break;
                            ++h;
                        }
                        for (int hh = 0; hh < h; ++hh)
                            for (int k = 0; k < w; ++k) mask[size_t(b + hh) * size_t(dims[u]) + size_t(a + k)] = kAir;
                        // Cuadrilátero
                        const bool top = d == 1 && s > 0;
                        const int key = int(id) * 2 + (top ? 1 : 0);
                        auto it = partOf.find(key);
                        if (it == partOf.end()) {
                            it = partOf.emplace(key, out.parts.size()).first;
                            MeshBuildMaterial m;
                            m.name = reg.get(id).name + (top ? " (arriba)" : "");
                            m.color = top ? reg.get(id).top : reg.get(id).side;
                            m.roughness = 0.9f;
                            out.materials.push_back(m);
                            out.parts.push_back({});
                            out.parts.back().material = int(out.materials.size() - 1);
                        }
                        MeshBuildPart& part = out.parts[it->second];
                        float base[3] = {0, 0, 0}, du[3] = {0, 0, 0}, dv[3] = {0, 0, 0};
                        base[d] = float(i + (s > 0 ? 1 : 0));
                        base[u] = float(a);
                        base[v] = float(b);
                        du[u] = float(w);
                        dv[v] = float(h);
                        Vec3 nrm;
                        (d == 0 ? nrm.x : d == 1 ? nrm.y : nrm.z) = float(s);
                        auto vert = [&](float ku, float kv, Vec2 uv) {
                            MeshBuildVertex mv;
                            mv.position = {base[0] + du[0] * ku + dv[0] * kv + ox, base[1] + du[1] * ku + dv[1] * kv,
                                           base[2] + du[2] * ku + dv[2] * kv + oz};
                            mv.normal = nrm;
                            mv.uv = uv;
                            part.vertices.push_back(mv);
                        };
                        const uint32_t first = uint32_t(part.vertices.size());
                        vert(0, 0, {0, 0});
                        vert(1, 0, {float(w), 0});
                        vert(1, 1, {float(w), float(h)});
                        vert(0, 1, {0, float(h)});
                        if (s > 0) part.indices.insert(part.indices.end(), {first, first + 1, first + 2, first, first + 2, first + 3});
                        else part.indices.insert(part.indices.end(), {first, first + 2, first + 1, first, first + 3, first + 2});
                        a += w;
                    }
            }
        }
    }
    return out;
}

// ---------------------------------------------------------------- mundo
VoxelChunk* VoxelWorld::chunk(int cx, int cz) {
    auto it = chunks_.find(key(cx, cz));
    return it == chunks_.end() ? nullptr : it->second.get();
}
const VoxelChunk* VoxelWorld::chunk(int cx, int cz) const {
    auto it = chunks_.find(key(cx, cz));
    return it == chunks_.end() ? nullptr : it->second.get();
}

VoxelChunk& VoxelWorld::ensureChunk(int cx, int cz) {
    if (VoxelChunk* c = chunk(cx, cz)) return *c;
    auto c = std::make_unique<VoxelChunk>();
    c->cx = cx;
    c->cz = cz;
    if (!loadChunk(cx, cz, *c)) generateChunk(gen, blocks, *c);
    VoxelChunk& ref = *c;
    chunks_[key(cx, cz)] = std::move(c);
    return ref;
}

void VoxelWorld::insert(std::unique_ptr<VoxelChunk> c) {
    const int64_t k = key(c->cx, c->cz);
    chunks_[k] = std::move(c);
}

std::unique_ptr<VoxelChunk> VoxelWorld::remove(int cx, int cz) {
    auto it = chunks_.find(key(cx, cz));
    if (it == chunks_.end()) return nullptr;
    auto c = std::move(it->second);
    chunks_.erase(it);
    return c;
}

std::vector<std::pair<int, int>> VoxelWorld::chunkCoords() const {
    std::vector<std::pair<int, int>> out;
    for (const auto& [k, c] : chunks_) out.push_back({c->cx, c->cz});
    return out;
}

BlockId VoxelWorld::get(int x, int y, int z) const {
    if (y < 0 || y >= kChunkHeight) return kAir;
    const int cx = floorDiv(x, kChunkSize), cz = floorDiv(z, kChunkSize);
    const VoxelChunk* c = chunk(cx, cz);
    return c ? c->get(x - cx * kChunkSize, y, z - cz * kChunkSize) : kAir;
}

bool VoxelWorld::set(int x, int y, int z, BlockId b) {
    if (y < 0 || y >= kChunkHeight) return false;
    const int cx = floorDiv(x, kChunkSize), cz = floorDiv(z, kChunkSize);
    VoxelChunk* c = chunk(cx, cz);
    if (!c) return false;
    c->set(x - cx * kChunkSize, y, z - cz * kChunkSize, b);
    c->modified = true;
    return true;
}

PaddedChunk VoxelWorld::padded(int cx, int cz) const {
    PaddedChunk p;
    p.cx = cx;
    p.cz = cz;
    for (int y = 0; y < kChunkHeight; ++y)
        for (int z = -1; z <= kChunkSize; ++z)
            for (int x = -1; x <= kChunkSize; ++x)
                p.b[size_t(PaddedChunk::index(x, y, z))] = get(cx * kChunkSize + x, y, cz * kChunkSize + z);
    return p;
}

int VoxelWorld::surfaceHeight(int x, int z) const {
    for (int y = kChunkHeight - 1; y >= 0; --y)
        if (blocks.get(get(x, y, z)).solid) return y;
    return -1;
}

bool VoxelWorld::raycast(Vec3 o, Vec3 d, float maxDist, RayHit& hit) const {
    d = d.normalized();
    if (d.length() < 0.5f) return false;
    int x = floorInt(o.x), y = floorInt(o.y), z = floorInt(o.z);
    const int sx = d.x > 0 ? 1 : -1, sy = d.y > 0 ? 1 : -1, sz = d.z > 0 ? 1 : -1;
    auto boundary = [](float p, float dir) { // distancia a la primera pared de la rejilla
        if (std::fabs(dir) < 1e-9f) return 1e30f;
        const float next = dir > 0 ? std::floor(p) + 1 - p : p - std::floor(p);
        return next / std::fabs(dir);
    };
    float tx = boundary(o.x, d.x), ty = boundary(o.y, d.y), tz = boundary(o.z, d.z);
    const float dx = std::fabs(d.x) > 1e-9f ? 1 / std::fabs(d.x) : 1e30f, dy = std::fabs(d.y) > 1e-9f ? 1 / std::fabs(d.y) : 1e30f,
                dz = std::fabs(d.z) > 1e-9f ? 1 / std::fabs(d.z) : 1e30f;
    int nx = 0, ny = 0, nz = 0;
    float t = 0;
    while (t <= maxDist) {
        const BlockId b = get(x, y, z);
        if (b != kAir && blocks.get(b).solid) {
            hit = {x, y, z, nx, ny, nz, b, t};
            return true;
        }
        if (tx < ty && tx < tz) {
            x += sx;
            t = tx;
            tx += dx;
            nx = -sx, ny = 0, nz = 0;
        } else if (ty < tz) {
            y += sy;
            t = ty;
            ty += dy;
            nx = 0, ny = -sy, nz = 0;
        } else {
            z += sz;
            t = tz;
            tz += dz;
            nx = 0, ny = 0, nz = -sz;
        }
    }
    return false;
}

// ---------------------------------------------------------------- persistencia
std::vector<uint8_t> encodeChunk(const VoxelChunk& c) {
    std::vector<uint8_t> out = {'T', 'V', 'C', '1'};
    for (size_t i = 0; i < c.blocks.size();) {
        const BlockId b = c.blocks[i];
        size_t n = 1;
        while (i + n < c.blocks.size() && c.blocks[i + n] == b && n < 255) ++n;
        out.push_back(uint8_t(n));
        out.push_back(b);
        i += n;
    }
    return out;
}

bool decodeChunk(const std::vector<uint8_t>& d, VoxelChunk& c) {
    if (d.size() < 4 || std::memcmp(d.data(), "TVC1", 4) != 0 || (d.size() - 4) % 2) return false;
    std::vector<BlockId> blocks;
    blocks.reserve(kChunkVolume);
    for (size_t i = 4; i + 1 < d.size(); i += 2) {
        if (d[i] == 0 || blocks.size() + d[i] > size_t(kChunkVolume)) return false;
        blocks.insert(blocks.end(), d[i], d[i + 1]);
    }
    if (blocks.size() != size_t(kChunkVolume)) return false; // archivo dañado: mejor regenerar que cargar basura
    c.blocks = std::move(blocks);
    return true;
}

bool VoxelWorld::saveSettings(std::string* error) const {
    if (folder.empty()) return false;
    fs::create_directories(folder + "/chunks");
    Json j{{"format", "titan-voxel-world"}, {"version", 1}, {"seed", gen.seed}, {"seaLevel", gen.seaLevel},
           {"terrainScale", gen.terrainScale}, {"caves", gen.caves}, {"trees", gen.trees}};
    return fileio::writeAtomic(folder + "/world.json", j.dump(2), true, error);
}

bool VoxelWorld::loadSettings(std::string* error) {
    std::string text;
    if (folder.empty() || !fileio::readText(folder + "/world.json", text)) return false;
    const Json j = Json::parse(text, nullptr, false);
    if (!j.is_object() || j.value("format", std::string()) != "titan-voxel-world") {
        if (error) *error = "world.json no es un mundo de Titán";
        return false;
    }
    if (j.value("version", 0) > 1) {
        if (error) *error = "el mundo es de una versión más nueva de Titán";
        return false;
    }
    gen.seed = j.value("seed", gen.seed);
    gen.seaLevel = j.value("seaLevel", gen.seaLevel);
    gen.terrainScale = j.value("terrainScale", gen.terrainScale);
    gen.caves = j.value("caves", gen.caves);
    gen.trees = j.value("trees", gen.trees);
    return true;
}

bool VoxelWorld::saveChunk(const VoxelChunk& c, std::string* error) const {
    if (folder.empty()) return false;
    fs::create_directories(folder + "/chunks");
    const std::vector<uint8_t> data = encodeChunk(c);
    return fileio::writeAtomic(folder + "/chunks/" + std::to_string(c.cx) + "_" + std::to_string(c.cz) + ".chunk",
                               std::string(data.begin(), data.end()), false, error);
}

bool VoxelWorld::loadChunk(int cx, int cz, VoxelChunk& out) const {
    if (folder.empty()) return false;
    std::ifstream f(folder + "/chunks/" + std::to_string(cx) + "_" + std::to_string(cz) + ".chunk", std::ios::binary);
    if (!f) return false;
    const std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (!decodeChunk(data, out)) return false;
    out.modified = false;
    return true;
}

int VoxelWorld::saveModified() {
    int n = 0;
    for (auto& [k, c] : chunks_)
        if (c->modified && saveChunk(*c)) {
            c->modified = false;
            ++n;
        }
    return n;
}

} // namespace titan
