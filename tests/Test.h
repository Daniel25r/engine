#pragma once
// Arnés de pruebas mínimo (sin dependencias). Cada TEST se registra solo.
#include <cmath>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace titan_test {

struct Case {
    const char* name;
    std::function<void()> fn;
};
std::vector<Case>& registry();
void fail(const char* file, int line, const std::string& msg);
std::string tempDir(const std::string& name); // carpeta vacía y nueva para la prueba

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

} // namespace titan_test

#define TT_CONCAT2(a, b) a##b
#define TT_CONCAT(a, b) TT_CONCAT2(a, b)
#define TEST(name)                                                                              \
    static void TT_CONCAT(test_fn_, __LINE__)();                                                \
    static titan_test::Registrar TT_CONCAT(test_reg_, __LINE__)(name, TT_CONCAT(test_fn_, __LINE__)); \
    static void TT_CONCAT(test_fn_, __LINE__)()

#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (!(cond)) titan_test::fail(__FILE__, __LINE__, "CHECK(" #cond ")");      \
    } while (0)

#define CHECK_EQ(a, b)                                                                                  \
    do {                                                                                                \
        auto&& va_ = (a);                                                                               \
        auto&& vb_ = (b);                                                                               \
        if (!(va_ == vb_)) {                                                                            \
            std::ostringstream os_;                                                                     \
            os_ << "CHECK_EQ(" #a ", " #b ") -> " << va_ << " != " << vb_;                              \
            titan_test::fail(__FILE__, __LINE__, os_.str());                                            \
        }                                                                                               \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                                           \
    do {                                                                                                \
        const double va_ = (a), vb_ = (b);                                                              \
        if (std::fabs(va_ - vb_) > (eps)) {                                                             \
            std::ostringstream os_;                                                                     \
            os_ << "CHECK_NEAR(" #a ", " #b ") -> " << va_ << " vs " << vb_;                            \
            titan_test::fail(__FILE__, __LINE__, os_.str());                                            \
        }                                                                                               \
    } while (0)

#define CHECK_VEC3(v, ex, ey, ez)            \
    do {                                     \
        CHECK_NEAR((v).x, ex, 1e-4);         \
        CHECK_NEAR((v).y, ey, 1e-4);         \
        CHECK_NEAR((v).z, ez, 1e-4);         \
    } while (0)
