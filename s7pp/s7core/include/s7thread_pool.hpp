#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace s7 {

class ThreadPool
{
public:
    explicit ThreadPool(std::size_t worker_count = std::thread::hardware_concurrency());
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    [[nodiscard]] auto workerCount() const -> std::size_t { return workers_.size(); }

    template <typename Fn>
    auto submit(Fn&& fn) -> std::future<std::invoke_result_t<Fn>>
    {
        using result_t = std::invoke_result_t<Fn>;

        auto task = std::make_shared<std::packaged_task<result_t()>>(std::forward<Fn>(fn));
        auto future = task->get_future();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_)
            {
                throw std::runtime_error("ThreadPool is stopping; cannot submit new tasks");
            }
            tasks_.push([task]() { (*task)(); });
        }

        cv_.notify_one();
        return future;
    }

private:
    void workerLoop();

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stopping_ = false;
};

} // namespace s7
