#include "titan/audio/AudioBus.h"

#include <set>

#include "titan/core/Serialization.h"

namespace titan {

AudioBusLayout AudioBusLayout::defaultLayout() {
    AudioBusLayout l;
    l.buses.push_back({});
    AudioBusSettings music;
    music.name = "Música";
    l.buses.push_back(music);
    AudioBusSettings sfx;
    sfx.name = "Efectos";
    l.buses.push_back(sfx);
    return l;
}

int AudioBusLayout::find(const std::string& name) const {
    for (size_t i = 0; i < buses.size(); ++i)
        if (buses[i].name == name) return static_cast<int>(i);
    return -1;
}

void AudioBusLayout::normalize() {
    // Master el primero (si no está, se crea).
    int m = find("Master");
    if (m < 0) {
        buses.insert(buses.begin(), AudioBusSettings{});
    } else if (m > 0) {
        AudioBusSettings master = buses[static_cast<size_t>(m)];
        buses.erase(buses.begin() + m);
        buses.insert(buses.begin(), master);
    }
    buses[0].send = "Master";
    // Nombres únicos y no vacíos.
    std::set<std::string> used;
    for (AudioBusSettings& b : buses) {
        if (b.name.empty()) b.name = "Bus";
        std::string base = b.name, name = base;
        for (int n = 2; used.count(name); ++n) name = base + " " + std::to_string(n);
        b.name = name;
        used.insert(name);
    }
    // Destinos válidos y sin ciclos: si al seguir los envíos no se llega a Master, va a Master.
    for (size_t i = 1; i < buses.size(); ++i) {
        if (find(buses[i].send) < 0 || buses[i].send == buses[i].name) buses[i].send = "Master";
        std::set<std::string> seen{buses[i].name};
        std::string at = buses[i].send;
        while (at != "Master") {
            if (!seen.insert(at).second) {
                buses[i].send = "Master";
                break;
            }
            at = buses[static_cast<size_t>(find(at))].send;
        }
    }
}

std::string AudioBusLayout::toJson() const {
    Json root;
    root["version"] = 1;
    Json list = Json::array();
    for (const AudioBusSettings& b : buses) {
        Json jb;
        jb["name"] = b.name;
        jb["send"] = b.send;
        jb["volume_db"] = b.volumeDb;
        jb["mute"] = b.mute;
        jb["bypass_effects"] = b.bypassEffects;
        Json fx = Json::array();
        for (const AudioBusEffectSettings& e : b.effects) {
            Json je;
            je["type"] = audioEffectName(e.type);
            je["enabled"] = e.enabled;
            Json params = Json::object();
            for (const auto& [k, v] : e.params) params[k] = v;
            je["params"] = params;
            fx.push_back(je);
        }
        jb["effects"] = fx;
        list.push_back(jb);
    }
    root["buses"] = list;
    return root.dump(2);
}

bool AudioBusLayout::fromJson(const std::string& text, AudioBusLayout& out, std::string* error) {
    Json root = Json::parse(text, nullptr, false);
    auto fail = [&](const char* why) {
        if (error) *error = why;
        return false;
    };
    if (root.is_discarded() || !root.is_object()) return fail("no es JSON válido");
    if (!root.contains("buses") || !root["buses"].is_array()) return fail("falta la lista 'buses'");
    AudioBusLayout l;
    for (const Json& jb : root["buses"]) {
        if (!jb.is_object()) continue;
        AudioBusSettings b;
        b.name = jb.value("name", std::string("Bus"));
        b.send = jb.value("send", std::string("Master"));
        b.volumeDb = jb.value("volume_db", 0.0f);
        b.mute = jb.value("mute", false);
        b.bypassEffects = jb.value("bypass_effects", false);
        if (jb.contains("effects") && jb["effects"].is_array())
            for (const Json& je : jb["effects"]) {
                AudioBusEffectSettings e;
                if (!je.is_object() || !audioEffectFromName(je.value("type", std::string()), e.type)) continue; // tipo desconocido: se ignora
                e.enabled = je.value("enabled", true);
                if (je.contains("params") && je["params"].is_object())
                    for (auto it = je["params"].begin(); it != je["params"].end(); ++it)
                        if (it->is_number()) e.params.emplace_back(it.key(), it->get<float>());
                b.effects.push_back(e);
            }
        l.buses.push_back(b);
    }
    l.normalize();
    out = l;
    return true;
}

} // namespace titan
