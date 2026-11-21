#include "titan/core/FileIO.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace titan::fileio {

bool readText(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

bool writeAtomic(const std::string& path, const std::string& data, bool keepBackup, std::string* error) {
    auto fail = [&](const std::string& msg) {
        if (error) *error = msg;
        return false;
    };
    std::error_code ec;
    const fs::path target(path);
    if (target.has_parent_path()) fs::create_directories(target.parent_path(), ec);

    const std::string tmp = path + ".tmp";
    std::FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return fail("no se puede escribir " + tmp + ": " + std::strerror(errno));
    const bool written = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    bool synced = std::fflush(f) == 0;
#if !defined(_WIN32)
    synced = synced && fsync(fileno(f)) == 0;
#endif
    std::fclose(f);
    if (!written || !synced) {
        fs::remove(tmp, ec);
        return fail("error al escribir " + tmp);
    }

    if (keepBackup && fs::exists(target, ec)) fs::copy_file(target, path + ".bak", fs::copy_options::overwrite_existing, ec);
    fs::rename(tmp, target, ec);
    if (ec) {
        fs::remove(tmp, ec);
        return fail("no se pudo reemplazar " + path + ": " + ec.message());
    }
    return true;
}

} // namespace titan::fileio
