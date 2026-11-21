#include "titan/core/Uuid.h"

#include <cstdio>
#include <random>

namespace titan {

UUID UUID::generate() {
    thread_local std::mt19937_64 rng{[] {
        std::random_device rd;
        return (static_cast<uint64_t>(rd()) << 32) ^ rd();
    }()};
    uint64_t v = 0;
    while (v == 0) v = rng();
    return {v};
}

bool UUID::parse(const std::string& text, UUID& out) {
    if (text.size() != 16) return false;
    uint64_t v = 0;
    for (char c : text) {
        v <<= 4;
        if (c >= '0' && c <= '9') v |= static_cast<uint64_t>(c - '0');
        else if (c >= 'a' && c <= 'f') v |= static_cast<uint64_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= static_cast<uint64_t>(c - 'A' + 10);
        else return false;
    }
    out.value = v;
    return true;
}

std::string UUID::toString() const {
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(value));
    return buf;
}

} // namespace titan
