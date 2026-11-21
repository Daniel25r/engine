#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>

#include "Test.h"
#include "titan/core/Log.h"

namespace titan_test {

std::vector<Case>& registry() {
    static std::vector<Case> r;
    return r;
}

static int gFailures = 0;

void fail(const char* file, int line, const std::string& msg) {
    ++gFailures;
    std::printf("    FALLO %s:%d: %s\n", file, line, msg.c_str());
}

std::string tempDir(const std::string& name) {
    namespace fs = std::filesystem;
    const fs::path p = fs::temp_directory_path() / "titan_tests" / name;
    fs::remove_all(p);
    fs::create_directories(p);
    return p.generic_string();
}

} // namespace titan_test

int main(int argc, char** argv) {
    // Los avisos esperados de las pruebas no ensucian la salida; pon -v para verlos.
    const bool verbose = argc > 1 && std::strcmp(argv[1], "-v") == 0;
    titan::Log::setPlatformOutput(verbose);
    const char* filter = (argc > 1 && !verbose) ? argv[1] : nullptr;

    int run = 0, failedCases = 0;
    const auto start = std::chrono::steady_clock::now();
    for (auto& c : titan_test::registry()) {
        if (filter && !std::strstr(c.name, filter)) continue;
        const int before = titan_test::gFailures;
        c.fn();
        ++run;
        const bool ok = titan_test::gFailures == before;
        if (!ok) ++failedCases;
        std::printf("[%s] %s\n", ok ? " OK " : "FALLA", c.name);
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::printf("\n%d pruebas, %d fallidas (%.1f ms)\n", run, failedCases, ms);
    return failedCases == 0 ? 0 : 1;
}
