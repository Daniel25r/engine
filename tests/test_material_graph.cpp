// Materiales por nodos: el compilador produce el programa correcto y se ejecuta igual que en el sombreador.
#include <algorithm>
#include <cmath>

#include "Test.h"
#include "titan/core/MaterialGraph.h"

using namespace titan;

namespace {
// Máquina virtual de referencia (la misma semántica que el sombreador, sin texturas ni ruido).
struct V4 { float x, y, z, w; };
V4 run(const MatGraphProgram& p, int outReg, V4 uv, float time) {
    V4 R[kMatGraphRegisters] = {};
    R[0] = uv;
    for (int i = 0; i < p.count; ++i) {
        const Color h = p.code[size_t(i * 3)], c2 = p.code[size_t(i * 3 + 1)], k = p.code[size_t(i * 3 + 2)];
        const int op = int(h.r), d = int(h.g);
        const V4 A = R[int(h.b)], B = R[int(h.a)], C = R[int(c2.r)];
        V4 r{k.r, k.g, k.b, k.a};
        auto f = [](V4 a, V4 b, auto g) { return V4{g(a.x, b.x), g(a.y, b.y), g(a.z, b.z), g(a.w, b.w)}; };
        switch (op) {
        case kOpConst: break;
        case kOpTexCoord: r = {uv.x * k.r, uv.y * k.g, 0, 1}; break;
        case kOpTime: r = {time, time, time, time}; break;
        case kOpAdd: r = f(A, B, [](float a, float b) { return a + b; }); break;
        case kOpSub: r = f(A, B, [](float a, float b) { return a - b; }); break;
        case kOpMul: r = f(A, B, [](float a, float b) { return a * b; }); break;
        case kOpLerp: r = f(A, B, [&](float a, float b) { return a + (b - a) * C.x; }); break;
        case kOpOneMinus: r = f(A, A, [](float a, float) { return 1 - a; }); break;
        case kOpPow: r = f(A, A, [&](float a, float) { return std::pow(std::max(a, 0.0f), k.r); }); break;
        case kOpSin: r = f(A, A, [&](float a, float) { return std::sin(a * k.r); }); break;
        case kOpPanner: r = {A.x + time * k.r, A.y + time * k.g, 0, 1}; break;
        case kOpClamp: r = f(A, A, [](float a, float) { return std::clamp(a, 0.0f, 1.0f); }); break;
        default: break;
        }
        R[d] = r;
    }
    return R[outReg];
}
} // namespace

TEST("material por nodos: mezclar dos colores con la UV da el resultado esperado") {
    MaterialGraph g = MaterialGraph::makeDefault();
    const int out = g.nodes[0].id;
    const int red = g.add(MatNode::Color, {}), blue = g.add(MatNode::Color, {});
    g.find(red)->value = {1, 0, 0, 1};
    g.find(blue)->value = {0, 0, 1, 1};
    const int uv = g.add(MatNode::TexCoord, {});
    const int mix = g.add(MatNode::Lerp, {});
    CHECK(g.connect(red, mix, 0));
    CHECK(g.connect(blue, mix, 1));
    CHECK(g.connect(uv, mix, 2)); // alfa = u
    CHECK(g.connect(mix, out, 0));
    const MatGraphProgram p = g.compile();
    CHECK(p.ok());
    CHECK(p.outBase >= 1 && p.outMetal == -1 && p.outRough == -1);
    const V4 c = run(p, p.outBase, {0.25f, 0.5f, 0, 1}, 0);
    CHECK_NEAR(c.x, 0.75f, 1e-5f);
    CHECK_NEAR(c.z, 0.25f, 1e-5f);
}

TEST("material por nodos: rugosidad animada con tiempo y seno, entradas sin conectar con su valor por defecto") {
    MaterialGraph g = MaterialGraph::makeDefault();
    const int out = g.nodes[0].id;
    const int t = g.add(MatNode::Time, {}), s = g.add(MatNode::Sine, {}), cl = g.add(MatNode::Clamp, {});
    g.find(s)->value.r = 2.0f;
    CHECK(g.connect(t, s, 0) && g.connect(s, cl, 0) && g.connect(cl, out, 2));
    const int m = g.add(MatNode::Multiply, {}); // B sin conectar = 1
    const int k = g.add(MatNode::Scalar, {});
    g.find(k)->value.r = 0.3f;
    CHECK(g.connect(k, m, 0) && g.connect(m, out, 1));
    const MatGraphProgram p = g.compile();
    CHECK(p.ok());
    CHECK_NEAR(run(p, p.outRough, {0, 0, 0, 1}, 0.5f).x, std::clamp(std::sin(1.0f), 0.0f, 1.0f), 1e-5f);
    CHECK_NEAR(run(p, p.outMetal, {0, 0, 0, 1}, 0.0f).x, 0.3f, 1e-5f);
}

TEST("material por nodos: ciclos rechazados, límites y errores claros") {
    MaterialGraph g = MaterialGraph::makeDefault();
    const int a = g.add(MatNode::Add, {}), b = g.add(MatNode::Add, {});
    CHECK(g.connect(a, b, 0));
    CHECK(!g.connect(b, a, 0)); // a <- b <- a: ciclo
    CHECK(!g.connect(a, a, 1));
    // Muchas operaciones encadenadas: los registros se reutilizan (no se agotan los 15).
    MaterialGraph chain = MaterialGraph::makeDefault();
    int prev = chain.add(MatNode::Scalar, {});
    for (int i = 0; i < 20; ++i) {
        const int n = chain.add(MatNode::OneMinus, {});
        CHECK(chain.connect(prev, n, 0));
        prev = n;
    }
    CHECK(chain.connect(prev, chain.nodes[0].id, 2));
    CHECK(chain.compile().ok());
    for (int i = 0; i < 20; ++i) {
        const int n = chain.add(MatNode::OneMinus, {});
        chain.connect(prev, n, 0);
        prev = n;
    }
    chain.connect(prev, chain.nodes[0].id, 2);
    CHECK(!chain.compile().ok()); // 41 operaciones: más de 32
    MaterialGraph tex = MaterialGraph::makeDefault();
    const int tn = tex.add(MatNode::Texture, {});
    tex.connect(tn, tex.nodes[0].id, 0);
    CHECK(tex.compile().error.find("textura") != std::string::npos); // sin textura asignada
}

TEST("material por nodos: se guarda y se carga igual") {
    MaterialGraph g = MaterialGraph::makeDefault();
    const int n = g.add(MatNode::Checker, {10, 20});
    g.find(n)->value.r = 12;
    g.connect(n, g.nodes[0].id, 0);
    MaterialGraph back;
    CHECK(back.fromJson(g.toJson()));
    CHECK_EQ(back.nodes.size(), g.nodes.size());
    CHECK(back.find(n) && back.find(n)->type == MatNode::Checker && back.find(n)->value.r == 12.0f);
    CHECK(back.compile().ok());
    std::string err;
    CHECK(!back.fromJson("{\"format\":\"otra cosa\"}", &err));
    CHECK(!err.empty());
}
