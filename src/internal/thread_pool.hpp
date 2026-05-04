#ifndef RIVULET_THREAD_POOL_HPP
#define RIVULET_THREAD_POOL_HPP

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <type_traits>
#include <vector>

namespace rivulet {

class ThreadPool {
public:
    explicit ThreadPool(std::size_t num_threads = 0)
    {
        if (num_threads == 0) {
            num_threads = std::thread::hardware_concurrency();
            if (num_threads == 0) num_threads = 4;
        }
        workers_.reserve(num_threads);
        for (std::size_t i = 0; i < num_threads; ++i) {
            workers_.emplace_back([this](std::stop_token st) { worker_loop(st); });
        }
    }

    ~ThreadPool() {
        {
            std::lock_guard lock(mutex_);
            stop_.store(true, std::memory_order_relaxed);
        }
        cv_.notify_all();
        for (auto& w : workers_) w.request_stop();
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    template <typename F, typename... Args>
    auto submit(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>
    {
        using R = std::invoke_result_t<F, Args...>;
        auto task = std::make_shared<std::packaged_task<R()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        auto future = task->get_future();
        {
            std::lock_guard lock(mutex_);
            if (stop_.load(std::memory_order_relaxed)) {
                throw std::runtime_error("submit on stopped ThreadPool");
            }
            tasks_.emplace([task]() { (*task)(); });
        }
        cv_.notify_one();
        return future;
    }

    template <typename F>
    void parallel_for(std::size_t count, F&& body) {
        if (count == 0) return;

        std::vector<std::future<void>> futures;
        futures.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            futures.push_back(submit([&body, i]() { body(i); }));
        }

        std::exception_ptr first_error;
        for (auto& f : futures) {
            try {
                f.get();
            } catch (...) {
                if (!first_error) first_error = std::current_exception();
            }
        }
        if (first_error) std::rethrow_exception(first_error);
    }

    std::size_t size() const { return workers_.size(); }

private:
    void worker_loop(std::stop_token st) {
        while (!st.stop_requested()) {
            std::function<void()> task;
            {
                std::unique_lock lock(mutex_);
                cv_.wait(lock, [&] {
                    return stop_.load(std::memory_order_relaxed) || !tasks_.empty();
                });
                if (tasks_.empty()) return;
                task = std::move(tasks_.front());
                tasks_.pop();
            }
            task();
        }
    }

    std::vector<std::jthread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> stop_{false};
};

} // namespace rivulet

#endif // RIVULET_THREAD_POOL_HPP
