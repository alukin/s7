#include "s7thread_pool.hpp"

namespace s7 {

ThreadPool::ThreadPool(std::size_t worker_count)
{
    if (worker_count == 0)
    {
        worker_count = 1;
    }

    workers_.reserve(worker_count);
    for (std::size_t i = 0; i < worker_count; ++i)
    {
        workers_.emplace_back([this] { workerLoop(); });
    }
}

ThreadPool::~ThreadPool()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    cv_.notify_all();

    for (auto& w : workers_)
    {
        if (w.joinable())
        {
            w.join();
        }
    }
}

void ThreadPool::workerLoop()
{
    while (true)
    {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });
            if (stopping_ && tasks_.empty())
            {
                return;
            }
            task = std::move(tasks_.front());
            tasks_.pop();
        }
        task();
    }
}

} // namespace s7

