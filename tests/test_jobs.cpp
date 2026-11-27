// Sistema de trabajos: corrección con varios hilos (cada elemento una sola vez, sin carreras), anidamiento,
// prioridades, un solo núcleo y uso real en las partículas.
#include <algorithm>
#include <atomic>
#include <mutex>
#include <chrono>
#include <thread>
#include <vector>

#include "Test.h"
#include "titan/core/JobSystem.h"
#include "titan/core/Particles.h"
#include "titan/core/Scene.h"

using namespace titan;

TEST("trabajos: parallelFor visita cada índice exactamente una vez (0, 1, 3 y 8 hilos)") {
    for (int workers : {0, 1, 3, 8}) {
        JobSystem jobs(workers);
        CHECK_EQ(jobs.workerCount(), workers);
        const size_t n = 200000;
        std::vector<std::atomic<int>> seen(n);
        std::atomic<uint64_t> sum{0};
        jobs.parallelFor(n, 100, [&](size_t a, size_t b) {
            uint64_t local = 0;
            for (size_t i = a; i < b; ++i) {
                seen[i].fetch_add(1, std::memory_order_relaxed);
                local += i;
            }
            sum += local;
        });
        bool once = true;
        for (auto& s : seen) once &= s.load() == 1;
        CHECK(once);
        CHECK_EQ(sum.load(), uint64_t(n) * (n - 1) / 2);
    }
}

TEST("trabajos: 20 000 tareas sueltas con contador; los hilos de trabajo participan") {
    JobSystem jobs(4);
    JobCounter c;
    std::atomic<int> done{0};
    std::vector<std::atomic<int>> byThread(64);
    std::mutex m;
    std::vector<std::thread::id> ids;
    for (int i = 0; i < 20000; ++i)
        jobs.submit([&] {
            done.fetch_add(1, std::memory_order_relaxed);
            std::lock_guard<std::mutex> lock(m);
            if (std::find(ids.begin(), ids.end(), std::this_thread::get_id()) == ids.end()) ids.push_back(std::this_thread::get_id());
        }, &c);
    jobs.wait(c);
    CHECK_EQ(done.load(), 20000);
    CHECK(c.done());
    CHECK(ids.size() >= 2); // no lo hizo todo un solo hilo
    CHECK(jobs.executedJobs() >= 20000u);
}

TEST("trabajos: tareas que lanzan y esperan otras (anidadas) no se bloquean") {
    JobSystem jobs(2);
    JobCounter outer;
    std::atomic<int> inner{0};
    for (int i = 0; i < 16; ++i)
        jobs.submit([&] {
            jobs.parallelFor(1000, 10, [&](size_t a, size_t b) { inner += int(b - a); });
            JobCounter child;
            for (int k = 0; k < 8; ++k) jobs.submit([&] { inner += 1; }, &child);
            jobs.wait(child);
        }, &outer);
    jobs.wait(outer);
    CHECK_EQ(inner.load(), 16 * (1000 + 8));
}

TEST("trabajos: la prioridad alta se ejecuta antes que la baja") {
    JobSystem jobs(1);
    std::atomic<bool> release{false}, started{false};
    JobCounter c;
    jobs.submit([&] { // ocupa el único hilo de trabajo
        started = true;
        while (!release) std::this_thread::yield();
    }, &c);
    while (!started) std::this_thread::yield();
    // Con el único hilo de trabajo ocupado, el hilo principal saca las tareas él solo: el orden en que las saca
    // es el de prioridad (con dos hilos libres terminarían en orden imprevisible aunque se sacaran en orden).
    std::vector<int> order;
    JobCounter queued;
    jobs.submit([&] { order.push_back(3); }, &queued, JobPriority::Low);
    jobs.submit([&] { order.push_back(2); }, &queued, JobPriority::Normal);
    jobs.submit([&] { order.push_back(1); }, &queued, JobPriority::High);
    jobs.wait(queued);
    release = true;
    jobs.wait(c);
    CHECK_EQ(order.size(), 3u);
    CHECK(order[0] == 1 && order[1] == 2 && order[2] == 3);
}

TEST("trabajos: al destruirse ejecuta lo que quedaba en cola") {
    std::atomic<int> n{0};
    {
        JobSystem jobs(2);
        for (int i = 0; i < 500; ++i) jobs.submit([&] { n++; });
    }
    CHECK_EQ(n.load(), 500);
}

TEST("trabajos: partículas en paralelo dan el mismo resultado que en serie") {
    auto build = [](Scene& s) {
        for (int i = 0; i < 24; ++i) {
            Entity e = s.createEntity("Emisor");
            s.transform(e).setPosition({float(i), 0, 0});
            ParticleSystem ps;
            ps.rate = 400;
            ps.maxParticles = 2000;
            ps.acceleration = {0, -9.8f, 0};
            s.world().add<ParticleSystem>(e, ps);
        }
        TransformSystem::update(s);
    };
    Scene serial("serie"), parallel("paralelo");
    build(serial);
    build(parallel);
    JobSystem jobs(3);
    for (int f = 0; f < 60; ++f) {
        updateParticles(serial, 1.0f / 60);
        updateParticles(parallel, 1.0f / 60, &jobs);
    }
    // Cada emisor tiene su propio generador: los dos caminos deben coincidir partícula a partícula en número.
    std::vector<size_t> a, b;
    serial.world().each<ParticleSystem>([&](Entity, ParticleSystem& ps) { a.push_back(aliveParticles(ps)); });
    parallel.world().each<ParticleSystem>([&](Entity, ParticleSystem& ps) { b.push_back(aliveParticles(ps)); });
    CHECK_EQ(a.size(), 24u);
    CHECK(a == b);
    CHECK(a[0] > 300u);
}

// ---- Carga asíncrona: decodificar imágenes en varios hilos a la vez (lo que hace ResourceCache con un JobSystem).
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"
#include "titan/core/FileSource.h"
#include "titan/render/Renderer.h"

TEST("trabajos: decodificar 32 PNG en paralelo da los mismos píxeles que en serie; los errores son por hilo") {
    MemoryFileSource files;
    for (int i = 0; i < 32; ++i) {
        const int n = 128 + i * 8;
        std::vector<uint8_t> px(size_t(n) * n * 4);
        for (size_t k = 0; k < px.size(); ++k) px[k] = uint8_t((k * 31 + size_t(i) * 7) & 0xFF);
        std::vector<uint8_t> png;
        stbi_write_png_to_func([](void* ctx, void* data, int size) {
            auto* out = static_cast<std::vector<uint8_t>*>(ctx);
            out->insert(out->end(), static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
        }, &png, n, n, 4, px.data(), n * 4);
        files.add("img" + std::to_string(i) + ".png", png);
    }
    files.add("roto.png", std::vector<uint8_t>{0x89, 'P', 'N', 'G', 1, 2, 3});
    std::vector<DecodedImage> serial(33), parallel(33);
    auto name = [](size_t i) { return i < 32 ? "img" + std::to_string(i) + ".png" : std::string("roto.png"); };
    for (size_t i = 0; i < 33; ++i) decodeImage(files, name(i), serial[i]);
    JobSystem jobs(4);
    jobs.parallelFor(33, 1, [&](size_t a, size_t b) {
        for (size_t i = a; i < b; ++i) decodeImage(files, name(i), parallel[i]);
    });
    bool same = true;
    for (size_t i = 0; i < 32; ++i)
        same &= parallel[i].width == int(128 + i * 8) && parallel[i].rgba == serial[i].rgba && parallel[i].error.empty();
    CHECK(same);
    CHECK(parallel[32].rgba.empty());
    CHECK(!parallel[32].error.empty()); // el motivo del fallo llega al hilo que lo pidió
    DecodedImage missing;
    CHECK(!decodeImage(files, "no_existe.png", missing));
    CHECK_EQ(missing.error, std::string("no se encontró el archivo"));
}
