#pragma once
// Sistema de trabajos: un grupo fijo de hilos que ejecuta tareas cortas en paralelo.
//
//   JobSystem jobs;                                   // núcleos - 1 hilos de trabajo
//   JobCounter c;
//   jobs.submit([&] { cargarTextura(); }, &c);        // tarea suelta (prioridad normal)
//   jobs.parallelFor(n, 64, [&](size_t a, size_t b) { // trocea 0..n en lotes de 64 como mínimo
//       for (size_t i = a; i < b; ++i) simular(i);
//   });
//   jobs.wait(c);                                     // espera ayudando a ejecutar trabajos (no se queda bloqueado)
//
// Reglas: las tareas no deben bloquearse esperando a algo que no sea otra tarea (I/O de disco sí, pero en tareas
// de prioridad Low para no retrasar el fotograma). wait() y parallelFor() pueden llamarse desde dentro de una tarea.
// Con 0 hilos de trabajo (un solo núcleo) todo se ejecuta en el hilo que llama, en orden: mismo resultado.
// Primera versión: colas con un mutex (no lock-free). Suficiente para decenas de miles de tareas por segundo.
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace titan {

enum class JobPriority { High = 0, Normal = 1, Low = 2 };

// Cuenta las tareas pendientes de un grupo. No se puede copiar; debe vivir hasta que wait() vuelva.
struct JobCounter {
    std::atomic<int> pending{0};
    bool done() const { return pending.load(std::memory_order_acquire) == 0; }
};

class JobSystem {
public:
    // workers < 0: núcleos del aparato - 1 (el hilo principal también trabaja), con un máximo de 16.
    explicit JobSystem(int workers = -1);
    ~JobSystem();
    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    void submit(std::function<void()> job, JobCounter* counter = nullptr, JobPriority priority = JobPriority::Normal);
    // Espera a que el contador llegue a 0 ejecutando trabajos pendientes mientras tanto.
    void wait(JobCounter& counter);
    // Ejecuta fn(inicio, fin) sobre trozos de [0, count) en paralelo y vuelve cuando han terminado todos.
    // minBatch: elementos mínimos por trozo (evita repartir trabajo demasiado pequeño).
    void parallelFor(size_t count, size_t minBatch, const std::function<void(size_t, size_t)>& fn);

    int workerCount() const { return static_cast<int>(threads_.size()); }
    // Estadísticas (para el perfilador): tareas ejecutadas desde el principio, por cualquier hilo.
    uint64_t executedJobs() const { return executed_.load(std::memory_order_relaxed); }

private:
    struct Job {
        std::function<void()> fn;
        JobCounter* counter;
    };
    bool tryRunOne();
    void workerLoop();

    std::vector<std::thread> threads_;
    std::deque<Job> queues_[3];
    std::mutex mutex_;
    std::condition_variable wake_;
    bool stopping_ = false;
    std::atomic<uint64_t> executed_{0};
};

} // namespace titan
