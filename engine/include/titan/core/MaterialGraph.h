#pragma once
// Materiales por nodos (como el editor de materiales de Unreal, a escala de Titán).
//
// Un grafo de nodos calcula el color base, el metal, la rugosidad y la emisión de un material. Se compila a una lista
// corta de instrucciones que el sombreador de mallas ejecuta en la GPU (una pequeña máquina virtual): cambiar el grafo
// se ve al momento, sin recompilar shaders, y el mismo programa sirve para todos los materiales.
// Límites: 32 instrucciones, 2 texturas por material, 15 registros (los compila el compilador; avisa si no cabe).
#include <memory>
#include <string>
#include <vector>

#include "titan/core/Math.h"
#include "titan/core/Uuid.h"

namespace titan {

enum class MatNode {
    Output,     // entradas: Color base, Metálico, Rugosidad, Emisión
    Color,      // constante (value)
    Scalar,     // constante (value.x)
    TexCoord,   // UV de la malla × (value.x, value.y)
    Time,       // segundos
    WorldPos,   // posición en el mundo
    Normal,     // normal en el mundo
    Fresnel,    // borde: (1 - n·v)^value.x
    Texture,    // textura del proyecto; entrada UV (si no, la UV de la malla)
    Add, Subtract, Multiply,
    Lerp,       // A, B, alfa
    OneMinus,
    Power,      // A ^ value.x
    Sine,       // sin(A × value.x)
    Noise,      // ruido suave de A.xy × value.x
    Panner,     // A.xy + tiempo × (value.x, value.y)
    Checker,    // tablero de A.xy × value.x
    Clamp,      // a 0..1
    Count
};

struct MatNodeInfo {
    const char* name;            // en el editor
    std::vector<const char*> inputs;
    bool hasOutput;
};
const MatNodeInfo& matNodeInfo(MatNode t);

struct MatGraphNode {
    int id = 0;
    MatNode type = MatNode::Color;
    Color value{1, 1, 1, 1};      // constante o parámetros (según el tipo)
    UUID texture;                // nodo Textura
    Vec2 pos;                    // en el lienzo del editor
    std::vector<int> inputs;     // id del nodo conectado a cada entrada (0 = sin conectar: valor por defecto)
};

// Programa compilado: 3 vec4 por instrucción (cabecera {op, destino, a, b}, {c, 0, 0, 0}, constante).
struct MatGraphProgram {
    std::vector<Color> code; // cada Color = un vec4 (r, g, b, a = x, y, z, w)
    int count = 0;                // instrucciones
    int outBase = -1, outMetal = -1, outRough = -1, outEmission = -1; // registros (-1 = lo del material)
    UUID textures[2];
    std::string error;            // vacío si compiló
    bool ok() const { return error.empty() && count > 0; }
};

class MaterialGraph {
public:
    std::vector<MatGraphNode> nodes;
    int nextId = 1;

    static MaterialGraph makeDefault(); // salida + color, listo para conectar
    MatGraphNode* find(int id);
    const MatGraphNode* find(int id) const;
    int add(MatNode type, Vec2 pos);    // devuelve el id
    void remove(int id);                // y sus conexiones
    // Conecta la salida de "from" a la entrada "pin" de "to". false si crearía un ciclo o no existe.
    bool connect(int from, int to, int pin);
    void disconnect(int to, int pin);
    MatGraphProgram compile() const;
    // Programa compilado guardado: se recompila solo tras un cambio (touch() o las funciones de edición).
    const MatGraphProgram& program() const;
    void touch() { dirty_ = true; }

    std::string toJson() const;
    bool fromJson(const std::string& text, std::string* error = nullptr);

private:
    mutable MatGraphProgram cached_;
    mutable bool dirty_ = true;
};

// Opcodes de la máquina virtual del sombreador (deben coincidir con Renderer.cpp).
enum MatOp {
    kOpConst = 0, kOpTexCoord, kOpTime, kOpWorldPos, kOpNormal, kOpFresnel, kOpTexture, kOpAdd, kOpSub, kOpMul,
    kOpLerp, kOpOneMinus, kOpPow, kOpSin, kOpNoise, kOpPanner, kOpChecker, kOpClamp
};
constexpr int kMatGraphMaxInstructions = 32;
constexpr int kMatGraphRegisters = 16; // el 0 es la UV de la malla

} // namespace titan
