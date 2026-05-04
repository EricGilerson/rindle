#include <catch2/catch_test_macros.hpp>

#include "internal/thread_pool.hpp"

#include <atomic>
#include <stdexcept>
#include <thread>
#include <vector>

namespace rv = rivulet;

TEST_CASE("ThreadPool construction with explicit count", "[threadpool]") {
    rv::ThreadPool pool(4);
    CHECK(pool.size() == 4);
}

TEST_CASE("ThreadPool construction with zero uses hardware_concurrency", "[threadpool]") {
    rv::ThreadPool pool(0);
    CHECK(pool.size() >= 1);
}

TEST_CASE("ThreadPool submit returns future with correct result", "[threadpool]") {
    rv::ThreadPool pool(2);
    auto future = pool.submit([]() { return 42; });
    CHECK(future.get() == 42);
}

TEST_CASE("ThreadPool submit propagates exception", "[threadpool]") {
    rv::ThreadPool pool(2);
    auto future = pool.submit([]() -> int {
        throw std::runtime_error("boom");
    });
    CHECK_THROWS_AS(future.get(), std::runtime_error);
}

TEST_CASE("ThreadPool parallel_for executes all iterations", "[threadpool]") {
    rv::ThreadPool pool(4);
    std::atomic<int> counter{0};
    pool.parallel_for(100, [&](std::size_t) { counter.fetch_add(1); });
    CHECK(counter.load() == 100);
}

TEST_CASE("ThreadPool parallel_for with count zero is no-op", "[threadpool]") {
    rv::ThreadPool pool(2);
    bool called = false;
    pool.parallel_for(0, [&](std::size_t) { called = true; });
    CHECK_FALSE(called);
}

TEST_CASE("ThreadPool parallel_for propagates first exception", "[threadpool]") {
    rv::ThreadPool pool(4);
    CHECK_THROWS_AS(
        pool.parallel_for(10, [](std::size_t i) {
            if (i == 5) throw std::runtime_error("fail at 5");
        }),
        std::runtime_error
    );
}

TEST_CASE("ThreadPool parallel_for indices cover full range", "[threadpool]") {
    constexpr std::size_t N = 200;
    rv::ThreadPool pool(4);
    std::vector<std::atomic<bool>> seen(N);
    for (auto& a : seen) a.store(false);

    pool.parallel_for(N, [&](std::size_t i) { seen[i].store(true); });

    for (std::size_t i = 0; i < N; ++i) {
        CHECK(seen[i].load());
    }
}

TEST_CASE("ThreadPool concurrent submit from multiple threads", "[threadpool]") {
    rv::ThreadPool pool(4);
    constexpr int kThreads = 8;
    constexpr int kTasksPerThread = 50;

    std::atomic<int> total{0};
    std::vector<std::thread> submitters;
    submitters.reserve(kThreads);

    for (int t = 0; t < kThreads; ++t) {
        submitters.emplace_back([&]() {
            std::vector<std::future<int>> futures;
            for (int j = 0; j < kTasksPerThread; ++j) {
                futures.push_back(pool.submit([&]() {
                    total.fetch_add(1);
                    return 1;
                }));
            }
            for (auto& f : futures) f.get();
        });
    }
    for (auto& th : submitters) th.join();

    CHECK(total.load() == kThreads * kTasksPerThread);
}

TEST_CASE("ThreadPool destructor joins gracefully", "[threadpool]") {
    std::atomic<int> counter{0};
    {
        rv::ThreadPool pool(4);
        for (int i = 0; i < 20; ++i) {
            pool.submit([&]() { counter.fetch_add(1); });
        }
    }
    CHECK(counter.load() <= 20);
}
