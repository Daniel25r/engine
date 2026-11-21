#pragma once
// Identificador persistente de 64 bits para entidades y recursos.
// A diferencia de Entity (índice + generación, solo válido en memoria), un UUID
// se guarda en los archivos y sirve para resolver referencias entre ellos.
#include <cstdint>
#include <functional>
#include <string>

namespace titan {

struct UUID {
    uint64_t value = 0;

    static UUID generate(); // nunca devuelve 0
    static bool parse(const std::string& text, UUID& out);

    std::string toString() const; // 16 caracteres hexadecimales
    bool isNull() const { return value == 0; }
    explicit operator bool() const { return value != 0; }
    bool operator==(const UUID& o) const { return value == o.value; }
    bool operator!=(const UUID& o) const { return value != o.value; }
    bool operator<(const UUID& o) const { return value < o.value; }
};

} // namespace titan

template <>
struct std::hash<titan::UUID> {
    size_t operator()(const titan::UUID& id) const noexcept { return std::hash<uint64_t>{}(id.value); }
};
