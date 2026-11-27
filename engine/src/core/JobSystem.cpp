#include "titan/core/JobSystem.h"

#include <algorithm>

namespace titan {

JobSystem::JobSystem(int workers) {
    if (workers < 0) workers = std::max(0, static_cast<int>(std::thread::hardware_concurrency()) - 1);
    workers = std::min(workers, 16);
    threads_.reserve(static_cast<size_t>(workers));
    for (int i = 0; i < workers; ++i) threads_.emplace_back([this] { workerLoop(); });
}

JobSystem::~JobSystem() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    wake_.notify_all();
    for (std::thread& t : threads_) t.join();
    // Lo que quedara en cola se ejecuta aquí: nadie espera un contador que no llegue a 0.
    while (tryRunOne()) {}
}

void JobSystem::submit(std::function<void()> job, JobCounter* counter, JobPriority priority) {
    if (counter) counter->pending.fetch_add(1, std::memory_order_relaxed);
    if (threads_.empty()) { // un solo núcleo: en el acto
        job();
        executed_.fetch_add(1, std::memory_order_relaxed);
        if (counter) counter->pending.fetch_sub(1, std::memory_order_release);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queues_[static_cast<int>(priority)].push_back({std::move(job), counter});
    }
    wake_.notify_one();
}

bool JobSystem::tryRunOne() {
    Job job;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::deque<Job>* q = nullptr;
        for (auto& candidate : queues_)
            if (!candidate.empty()) {
                q = &candidate;
                break;
            }
        if (!q) return false;
        job = std::move(q->front());
        q->pop_front();
    }
    job.fn();
    executed_.fetch_add(1, std::memory_order_relaxed);
    if (job.counter) job.counter->pending.fetch_sub(1, std::memory_order_release);
    return true;
}

void JobSystem::workerLoop() {
    for (;;) {
        if (tryRunOne()) continue;
        std::unique_lock<std::mutex> lock(mutex_);
        wake_.wait(lock, [this] {
            return stopping_ || !queues_[0].empty() || !queues_[1].empty() || !queues_[2].empty();
        });
        if (stopping_ && queues_[0].empty() && queues_[1].empty() && queues_[2].empty()) return;
    }
}

void JobSystem::wait(JobCounter& counter) {
    while (!counter.done()) {
        if (!tryRunOne()) std::this_thread::yield(); // lo que falta lo está ejecutando otro hilo
    }
}

void JobSystem::parallelFor(size_t count, size_t minBatch, const std::function<void(size_t, size_t)>& fn) {
    if (count == 0) return;
    minBatch = std::max<size_t>(1, minBatch);
    const size_t threads = threads_.size() + 1; // el hilo que llama también trabaja
    // Unos 4 trozos por hilo: reparte bien aunque unos trozos tarden más que otros.
    size_t batch = std::max(minBatch, (count + threads * 4 - 1) / (threads * 4));
    if (threads == 1 || count <= batch) {
        fn(0, count);
        return;
    }
    JobCounter counter;
    size_t begin = batch; // el primer trozo lo hace el hilo que llama
    for (; begin < count; begin += batch) {
        const size_t end = std::min(count, begin + batch);
        submit([&fn, begin, end] { fn(begin, end); }, &counter, JobPriority::High);
    }
    fn(0, std::min(count, batch));
    wait(counter);
}

} // namespace titan
