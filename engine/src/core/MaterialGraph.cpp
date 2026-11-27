#include "titan/core/MaterialGraph.h"

#include <algorithm>
#include <functional>
#include <map>

#include "titan/core/Serialization.h"

namespace titan {

namespace {
struct TypeDef {
    const char* key; // nombre estable en el archivo
    MatNodeInfo info;
    Color def;       // valor por defecto
};
const TypeDef kTypes[] = {
    {"output", {"Salida", {"Color base", "Metálico", "Rugosidad", "Emisión"}, false}, {0, 0, 0, 0}},
    {"color", {"Color", {}, true}, {1, 1, 1, 1}},
    {"scalar", {"Número", {}, true}, {0.5f, 0, 0, 0}},
    {"texcoord", {"Coordenadas UV", {}, true}, {1, 1, 0, 0}},
    {"time", {"Tiempo", {}, true}, {0, 0, 0, 0}},
    {"worldpos", {"Posición en el mundo", {}, true}, {0, 0, 0, 0}},
    {"normal", {"Normal", {}, true}, {0, 0, 0, 0}},
    {"fresnel", {"Fresnel (borde)", {}, true}, {5, 0, 0, 0}},
    {"texture", {"Textura", {"UV"}, true}, {0, 0, 0, 0}},
    {"add", {"Sumar", {"A", "B"}, true}, {0, 0, 0, 0}},
    {"subtract", {"Restar", {"A", "B"}, true}, {0, 0, 0, 0}},
    {"multiply", {"Multiplicar", {"A", "B"}, true}, {0, 0, 0, 0}},
    {"lerp", {"Mezclar", {"A", "B", "Alfa"}, true}, {0, 0, 0, 0}},
    {"oneminus", {"1 - x", {"x"}, true}, {0, 0, 0, 0}},
    {"power", {"Potencia", {"x"}, true}, {2, 0, 0, 0}},
    {"sine", {"Seno", {"x"}, true}, {1, 0, 0, 0}},
    {"noise", {"Ruido", {"UV"}, true}, {4, 0, 0, 0}},
    {"panner", {"Desplazar UV", {"UV"}, true}, {0.1f, 0, 0, 0}},
    {"checker", {"Tablero", {"UV"}, true}, {8, 0, 0, 0}},
    {"clamp", {"Limitar 0..1", {"x"}, true}, {0, 0, 0, 0}},
};
static_assert(sizeof(kTypes) / sizeof(kTypes[0]) == size_t(MatNode::Count), "falta un tipo de nodo");

// Valor de una entrada sin conectar.
Color unlinkedDefault(MatNode t, int pin) {
    switch (t) {
    case MatNode::Multiply: return {1, 1, 1, 1};
    case MatNode::Lerp: return pin == 2 ? Color{0.5f, 0.5f, 0.5f, 0.5f} : Color{0, 0, 0, 1};
    default: return {0, 0, 0, 1};
    }
}

int opFor(MatNode t) {
    switch (t) {
    case MatNode::Color: case MatNode::Scalar: return kOpConst;
    case MatNode::TexCoord: return kOpTexCoord;
    case MatNode::Time: return kOpTime;
    case MatNode::WorldPos: return kOpWorldPos;
    case MatNode::Normal: return kOpNormal;
    case MatNode::Fresnel: return kOpFresnel;
    case MatNode::Texture: return kOpTexture;
    case MatNode::Add: return kOpAdd;
    case MatNode::Subtract: return kOpSub;
    case MatNode::Multiply: return kOpMul;
    case MatNode::Lerp: return kOpLerp;
    case MatNode::OneMinus: return kOpOneMinus;
    case MatNode::Power: return kOpPow;
    case MatNode::Sine: return kOpSin;
    case MatNode::Noise: return kOpNoise;
    case MatNode::Panner: return kOpPanner;
    case MatNode::Checker: return kOpChecker;
    case MatNode::Clamp: return kOpClamp;
    default: return -1;
    }
}
} // namespace

const MatNodeInfo& matNodeInfo(MatNode t) { return kTypes[std::min(size_t(t), size_t(MatNode::Count) - 1)].info; }

MaterialGraph MaterialGraph::makeDefault() {
    MaterialGraph g;
    const int out = g.add(MatNode::Output, {420, 120});
    const int col = g.add(MatNode::Color, {120, 100});
    g.find(col)->value = {0.8f, 0.8f, 0.8f, 1};
    g.connect(col, out, 0);
    return g;
}

const MatGraphProgram& MaterialGraph::program() const {
    if (dirty_) {
        cached_ = compile();
        dirty_ = false;
    }
    return cached_;
}

MatGraphNode* MaterialGraph::find(int id) {
    for (MatGraphNode& n : nodes)
        if (n.id == id) return &n;
    return nullptr;
}
const MatGraphNode* MaterialGraph::find(int id) const { return const_cast<MaterialGraph*>(this)->find(id); }

int MaterialGraph::add(MatNode type, Vec2 pos) {
    MatGraphNode n;
    n.id = nextId++;
    n.type = type;
    n.value = kTypes[size_t(type)].def;
    n.pos = pos;
    n.inputs.assign(matNodeInfo(type).inputs.size(), 0);
    nodes.push_back(n);
    dirty_ = true;
    return n.id;
}

void MaterialGraph::remove(int id) {
    nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [&](const MatGraphNode& n) { return n.id == id; }), nodes.end());
    for (MatGraphNode& n : nodes)
        for (int& in : n.inputs)
            if (in == id) in = 0;
    dirty_ = true;
}

bool MaterialGraph::connect(int from, int to, int pin) {
    MatGraphNode* src = find(from);
    MatGraphNode* dst = find(to);
    if (!src || !dst || from == to || pin < 0 || pin >= int(dst->inputs.size()) || !matNodeInfo(src->type).hasOutput) return false;
    // ¿"to" ya alimenta a "from"? Entonces sería un ciclo.
    std::function<bool(int)> reaches = [&](int id) {
        if (id == to) return true;
        const MatGraphNode* n = find(id);
        if (!n) return false;
        for (int in : n->inputs)
            if (in && reaches(in)) return true;
        return false;
    };
    if (reaches(from)) return false;
    dst->inputs[size_t(pin)] = from;
    dirty_ = true;
    return true;
}

void MaterialGraph::disconnect(int to, int pin) {
    MatGraphNode* dst = find(to);
    if (dst && pin >= 0 && pin < int(dst->inputs.size())) dst->inputs[size_t(pin)] = 0;
    dirty_ = true;
}

MatGraphProgram MaterialGraph::compile() const {
    MatGraphProgram prog;
    const MatGraphNode* out = nullptr;
    for (const MatGraphNode& n : nodes)
        if (n.type == MatNode::Output) out = &n;
    if (!out) {
        prog.error = "falta el nodo Salida";
        return prog;
    }
    // 1. Instrucciones en orden (cada nodo una vez) con registros virtuales.
    struct Ins { int op, dst, a, b, c; Color k; };
    std::vector<Ins> list;
    std::map<int, int> regOf; // nodo -> registro virtual
    int nextVirtual = 1;
    int textureCount = 0;
    std::map<uint64_t, int> slotOf;
    std::function<int(int, int)> emit = [&](int nodeId, int depth) -> int {
        if (depth > 64) return -1;
        if (auto it = regOf.find(nodeId); it != regOf.end()) return it->second;
        const MatGraphNode* n = find(nodeId);
        if (!n || n->type == MatNode::Output) return -1;
        int ins[3] = {0, 0, 0};
        for (size_t p = 0; p < n->inputs.size() && p < 3; ++p) {
            if (n->inputs[p]) {
                ins[p] = emit(n->inputs[p], depth + 1);
                if (ins[p] < 0) return -1;
            } else if (n->type == MatNode::Texture || n->type == MatNode::Noise || n->type == MatNode::Panner ||
                       n->type == MatNode::Checker) {
                ins[p] = 0; // UV de la malla
            } else {
                const int r = nextVirtual++;
                list.push_back({kOpConst, r, 0, 0, 0, unlinkedDefault(n->type, int(p))});
                ins[p] = r;
            }
        }
        Color k = n->value;
        if (n->type == MatNode::Texture) {
            if (n->texture.isNull()) {
                k = {0, 0, 0, 0};
                prog.error = prog.error.empty() ? "un nodo Textura no tiene textura" : prog.error;
            }
            auto it = slotOf.find(n->texture.value);
            if (it == slotOf.end()) {
                if (textureCount >= 2) {
                    prog.error = "como máximo 2 texturas distintas por material";
                    return -1;
                }
                prog.textures[textureCount] = n->texture;
                it = slotOf.emplace(n->texture.value, textureCount++).first;
            }
            k.r = float(it->second);
        }
        if (n->type == MatNode::Scalar) k = {n->value.r, n->value.r, n->value.r, n->value.r};
        const int r = nextVirtual++;
        list.push_back({opFor(n->type), r, ins[0], ins[1], ins[2], k});
        regOf[nodeId] = r;
        return r;
    };
    int outs[4] = {-1, -1, -1, -1};
    for (size_t p = 0; p < 4 && p < out->inputs.size(); ++p)
        if (out->inputs[p]) {
            outs[p] = emit(out->inputs[p], 0);
            if (outs[p] < 0) {
                if (prog.error.empty()) prog.error = "el grafo tiene un ciclo o un nodo roto";
                return prog;
            }
        }
    if (!prog.error.empty()) return prog;
    if (list.empty()) {
        prog.error = "conecta algo a la Salida";
        return prog;
    }
    if (int(list.size()) > kMatGraphMaxInstructions) {
        prog.error = "demasiados nodos (máximo " + std::to_string(kMatGraphMaxInstructions) + " operaciones)";
        return prog;
    }
    // 2. Registros reales por vida útil: cada valor ocupa un registro solo hasta su último uso.
    std::map<int, int> lastUse;
    for (size_t i = 0; i < list.size(); ++i)
        for (int v : {list[i].a, list[i].b, list[i].c})
            if (v > 0) lastUse[v] = int(i);
    for (int o : outs)
        if (o > 0) lastUse[o] = 1 << 20;
    std::vector<int> freeRegs;
    for (int r = kMatGraphRegisters - 1; r >= 1; --r) freeRegs.push_back(r);
    std::map<int, int> phys;
    auto physOf = [&](int v) { return v <= 0 ? 0 : phys.at(v); };
    for (size_t i = 0; i < list.size(); ++i) {
        Ins& in = list[i];
        const int a = physOf(in.a), b = physOf(in.b), c = physOf(in.c);
        for (int v : {in.a, in.b, in.c}) // se liberan antes de asignar el destino (puede reutilizarse)
            if (v > 0 && lastUse[v] == int(i) && phys.count(v)) {
                freeRegs.push_back(phys[v]);
                phys.erase(v);
            }
        if (freeRegs.empty()) {
            prog.error = "el grafo necesita demasiados valores a la vez";
            return prog;
        }
        phys[in.dst] = freeRegs.back();
        freeRegs.pop_back();
        const int d = phys[in.dst];
        if (!lastUse.count(in.dst)) { // nadie lo usa: se libera ya
            freeRegs.push_back(d);
            phys.erase(in.dst);
        }
        prog.code.push_back({float(in.op), float(d), float(a), float(b)});
        prog.code.push_back({float(c), 0, 0, 0});
        prog.code.push_back(in.k);
    }
    prog.count = int(list.size());
    prog.outBase = outs[0] > 0 ? phys.at(outs[0]) : -1;
    prog.outMetal = outs[1] > 0 ? phys.at(outs[1]) : -1;
    prog.outRough = outs[2] > 0 ? phys.at(outs[2]) : -1;
    prog.outEmission = outs[3] > 0 ? phys.at(outs[3]) : -1;
    return prog;
}

std::string MaterialGraph::toJson() const {
    Json arr = Json::array();
    for (const MatGraphNode& n : nodes)
        arr.push_back(Json{{"id", n.id}, {"type", kTypes[size_t(n.type)].key}, {"value", titan::toJson(n.value)},
                           {"texture", assetToJson(n.texture)}, {"pos", titan::toJson(n.pos)}, {"inputs", n.inputs}});
    return Json{{"format", "titan-material-graph"}, {"version", 1}, {"nextId", nextId}, {"nodes", arr}}.dump(2);
}

bool MaterialGraph::fromJson(const std::string& text, std::string* error) {
    auto fail = [&](const std::string& why) {
        if (error) *error = why;
        return false;
    };
    const Json j = Json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object() || j.value("format", "") != "titan-material-graph") return fail("no es un grafo de material");
    if (j.value("version", 0) > 1) return fail("grafo de una versión más nueva de Titán");
    MaterialGraph g;
    g.nextId = j.value("nextId", 1);
    for (const Json& jn : j.value("nodes", Json::array())) {
        MatGraphNode n;
        n.id = jn.value("id", 0);
        const std::string key = jn.value("type", "");
        size_t t = 0;
        while (t < size_t(MatNode::Count) && key != kTypes[t].key) ++t;
        if (t == size_t(MatNode::Count) || n.id <= 0) return fail("nodo desconocido: " + key);
        n.type = MatNode(t);
        if (jn.contains("value") && jn["value"].is_array() && jn["value"].size() == 4)
            n.value = {jn["value"][0].get<float>(), jn["value"][1].get<float>(), jn["value"][2].get<float>(), jn["value"][3].get<float>()};
        if (jn.contains("texture") && jn["texture"].is_string()) UUID::parse(jn["texture"].get<std::string>(), n.texture);
        if (jn.contains("pos") && jn["pos"].is_array() && jn["pos"].size() == 2) n.pos = {jn["pos"][0].get<float>(), jn["pos"][1].get<float>()};
        n.inputs.assign(matNodeInfo(n.type).inputs.size(), 0);
        if (jn.contains("inputs") && jn["inputs"].is_array())
            for (size_t p = 0; p < n.inputs.size() && p < jn["inputs"].size(); ++p) n.inputs[p] = jn["inputs"][p].get<int>();
        g.nextId = std::max(g.nextId, n.id + 1);
        g.nodes.push_back(n);
    }
    nodes = std::move(g.nodes);
    nextId = g.nextId;
    dirty_ = true;
    return true;
}

} // namespace titan
