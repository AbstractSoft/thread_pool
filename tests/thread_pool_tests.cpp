/*
 * Simple thread pool library
 * Copyright (C) 2026 Eduard Ghergu, PhD <eduard.ghergu@professional-programmer.com>
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <atomic>
#include <chrono>
#include <cmath>
#include <future>
#include <latch>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

#include "gtest/gtest.h"

#include "thread_pool.hpp"

// ── Default Construction ───────────────────────────────────────────────────

TEST(ThreadPoolTest, DefaultConstruction) {
    thread_pool::ThreadPool pool{0};
    EXPECT_EQ(pool.active_tasks(), 0);
}

TEST(ThreadPoolTest, CustomThreadCount) {
    thread_pool::ThreadPool pool{4};
    EXPECT_EQ(pool.active_tasks(), 0);
}

// ── Submit ─────────────────────────────────────────────────────────────────

TEST(ThreadPoolTest, SubmitSimpleTask) {
    thread_pool::ThreadPool pool{2};
    std::atomic<int> counter{0};

    auto future = pool.submit([&counter] {
        ++counter;
    });

    future.get();
    EXPECT_EQ(counter, 1);
}

TEST(ThreadPoolTest, SubmitWithReturnValue) {
    thread_pool::ThreadPool pool{2};

    auto future = pool.submit([](int a, int b) {
        return a + b;
    }, 3, 4);

    EXPECT_EQ(future.get(), 7);
}

TEST(ThreadPoolTest, SubmitMoveOnly) {
    thread_pool::ThreadPool pool{1};

    auto future = pool.submit([](std::unique_ptr<int> ptr) {
        return *ptr;
    }, std::make_unique<int>(42));

    EXPECT_EQ(future.get(), 42);
}

TEST(ThreadPoolTest, MultipleSubmissions) {
    thread_pool::ThreadPool pool{2};
    std::atomic<int> counter{0};

    std::vector<std::future<void>> futures;
    for (int i = 0; i < 10; ++i) {
        futures.push_back(pool.submit([&counter] {
            ++counter;
        }));
    }

    for (auto& f : futures) {
        f.get();
    }

    EXPECT_EQ(counter, 10);
}

TEST(ThreadPoolTest, LambdaWithReferenceCapture) {
    thread_pool::ThreadPool pool{1};
    int shared_value = 0;

    auto future = pool.submit([&shared_value] {
        shared_value = 100;
    });

    future.get();
    EXPECT_EQ(shared_value, 100);
}

TEST(ThreadPoolTest, LambdaWithValueCapture) {
    thread_pool::ThreadPool pool{1};
    int captured = 42;

    auto future = pool.submit([captured] {
        return captured * 2;
    });

    EXPECT_EQ(future.get(), 84);
}

TEST(ThreadPoolTest, FutureReturnTypeVoid) {
    thread_pool::ThreadPool pool{1};

    std::atomic<int> value{0};
    auto future = pool.submit([&value] {
        value.store(42);
    });

    EXPECT_NO_THROW(future.get());
    EXPECT_EQ(value.load(), 42);
}

// ── Return Types ───────────────────────────────────────────────────────────

TEST(ThreadPoolTest, ReturnTypeInt) {
    thread_pool::ThreadPool pool{2};
    auto future = pool.submit([] { return 42; });
    EXPECT_EQ(future.get(), 42);
}

TEST(ThreadPoolTest, ReturnTypeString) {
    thread_pool::ThreadPool pool{2};
    auto future = pool.submit([] { return std::string("hello"); });
    EXPECT_EQ(future.get(), "hello");
}

TEST(ThreadPoolTest, ReturnTypeDouble) {
    thread_pool::ThreadPool pool{2};
    auto future = pool.submit([] { return 3.14; });
    EXPECT_NEAR(future.get(), 3.14, 1e-10);
}

// ── Wait ───────────────────────────────────────────────────────────────────

TEST(ThreadPoolTest, WaitAll) {
    thread_pool::ThreadPool pool{2};
    std::atomic<int> counter{0};

    for (int i = 0; i < 5; ++i) {
        pool.submit([&counter] {
            ++counter;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        });
    }

    pool.wait_all();
    EXPECT_EQ(counter, 5);
}

TEST(ThreadPoolTest, WaitAllWithTimeoutSuccess) {
    thread_pool::ThreadPool pool{2, std::chrono::seconds(5)};
    std::atomic<int> counter{0};

    for (int i = 0; i < 3; ++i) {
        pool.submit([&counter] {
            ++counter;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        });
    }

    pool.wait_all_with_timeout(std::chrono::seconds(2));
    EXPECT_EQ(counter, 3);
}

TEST(ThreadPoolTest, WaitAllWithTimeoutExpires) {
    thread_pool::ThreadPool pool{1, std::chrono::seconds(5)};

    pool.submit([] {
        std::this_thread::sleep_for(std::chrono::seconds(2));
    });

    auto start = std::chrono::steady_clock::now();
    pool.wait_all_with_timeout(std::chrono::seconds(1));
    auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_GE(elapsed, std::chrono::milliseconds(900));
    EXPECT_LT(elapsed, std::chrono::seconds(2));
}

TEST(ThreadPoolTest, NoTasksWaitAll) {
    thread_pool::ThreadPool pool{2};
    auto start = std::chrono::steady_clock::now();
    pool.wait_all_with_timeout(std::chrono::seconds(1));
    auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_LT(elapsed, std::chrono::seconds(1));
}

// ── Active Tasks ───────────────────────────────────────────────────────────

TEST(ThreadPoolTest, ActiveTasksDuringExecution) {
    // active_task_count_ is incremented in pop_task() before the task runs,
    // so by the time the lambda calls count_down() the counter is already 1.
    thread_pool::ThreadPool pool{1};
    std::latch started{1};

    auto future1 = pool.submit([&started] {
        started.count_down();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        return 1;
    });

    started.wait();
    EXPECT_EQ(pool.active_tasks(), 1);

    future1.get();
    pool.wait_all();
    EXPECT_EQ(pool.active_tasks(), 0);
}

// ── Exception Handling ─────────────────────────────────────────────────────

TEST(ThreadPoolTest, TaskWithException) {
    thread_pool::ThreadPool pool{2};

    auto future = pool.submit([] {
        throw std::runtime_error("test exception");
    });

    EXPECT_THROW(future.get(), std::runtime_error);
}

TEST(ThreadPoolTest, PoolContinuesAfterTaskException) {
    thread_pool::ThreadPool pool{2};

    auto future1 = pool.submit([] {
        throw std::runtime_error("task failure");
    });

    auto future2 = pool.submit([] { return 42; });

    EXPECT_THROW(future1.get(), std::runtime_error);
    EXPECT_EQ(future2.get(), 42);
    EXPECT_EQ(pool.active_tasks(), 0);

    auto future3 = pool.submit([] { return 100; });
    EXPECT_EQ(future3.get(), 100);
}

TEST(ThreadPoolTest, NestedSubmit) {
    // Requires pool size >= 2: the outer task blocks a worker on inner.get(),
    // and a second worker must be available to execute the inner task.
    thread_pool::ThreadPool pool{2};
    std::atomic<int> result{0};

    auto future = pool.submit([&pool, &result] {
        auto inner = pool.submit([] { return 7; });
        result = inner.get();
    });

    future.get();
    EXPECT_EQ(result, 7);
}

// ── Clear Pending ────────────────────────────────────────────────────────────

TEST(ThreadPoolTest, ClearPending) {
    thread_pool::ThreadPool pool{1};
    std::latch started{1};

    auto slow = pool.submit([&started] {
        started.count_down();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    });

    started.wait(); // slow task is running, worker is busy

    // These queue up because the single worker is blocked
    std::vector<std::future<void>> futures;
    for (int i = 0; i < 5; ++i) {
        futures.push_back(pool.submit([] { /* should be cleared */ }));
    }

    EXPECT_EQ(pool.clear_pending(), 5);

    slow.get();
    pool.wait_all(); // returns immediately — no pending tasks left

    // Pool should still accept new tasks after clearing
    auto result = pool.submit([] { return 42; });
    EXPECT_EQ(result.get(), 42);
}

TEST(ThreadPoolTest, ClearPendingFuturesBroken) {
    thread_pool::ThreadPool pool{1};
    std::latch started{1};

    pool.submit([&started] {
        started.count_down();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    });
    started.wait();

    auto f = pool.submit([] { return 42; });
    pool.clear_pending();

    EXPECT_THROW(f.get(), std::future_error);
}

TEST(ThreadPoolTest, SubmitOnStoppedPool) {
    thread_pool::ThreadPool pool{1};
    pool.shutdown();
    EXPECT_THROW(pool.submit([] {}), std::runtime_error);
}

TEST(ThreadPoolTest, ActiveTasksNeverExceedsThreadCount) {
    constexpr std::size_t N = 4;
    thread_pool::ThreadPool pool{N};
    std::atomic<std::size_t> max_seen{0};

    std::vector<std::future<void>> futures;
    for (int i = 0; i < 50; ++i) {
        futures.push_back(pool.submit([&pool, &max_seen] {
            std::size_t cur = pool.active_tasks();
            std::size_t prev = max_seen.load();
            while (cur > prev && !max_seen.compare_exchange_weak(prev, cur)) {}
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }));
    }

    for (auto& f : futures) {
        f.get();
    }
    EXPECT_LE(max_seen.load(), N);
}

// ── Default Timeout ─────────────────────────────────────────────────────────

TEST(ThreadPoolTest, DefaultTimeoutExpires) {
    // Zero-second default timeout — wait_all() should return immediately
    thread_pool::ThreadPool pool{1, std::chrono::seconds{0}};

    auto future = pool.submit([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        return 42;
    });

    auto start = std::chrono::steady_clock::now();
    pool.wait_all();
    auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_LT(elapsed, std::chrono::milliseconds(100));
    // Future not yet ready since wait_all returned early due to timeout
    EXPECT_EQ(future.wait_for(std::chrono::seconds(0)), std::future_status::timeout);

    // Task eventually completes on its own
    EXPECT_EQ(future.get(), 42);
}

TEST(ThreadPoolTest, MultipleWaitAllCalls) {
    thread_pool::ThreadPool pool{2};
    std::atomic<int> counter{0};

    for (int i = 0; i < 5; ++i) {
        pool.submit([&counter] {
            ++counter;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        });
    }

    pool.wait_all();
    EXPECT_EQ(counter, 5);

    // Second call should return immediately
    auto start = std::chrono::steady_clock::now();
    pool.wait_all();
    auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_LT(elapsed, std::chrono::milliseconds(100));
}

TEST(ThreadPoolTest, WaitAllWithMilliseconds) {
    thread_pool::ThreadPool pool{2, std::chrono::seconds(5)};
    std::atomic<int> counter{0};

    for (int i = 0; i < 3; ++i) {
        pool.submit([&counter] {
            ++counter;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        });
    }

    pool.wait_all_with_timeout(std::chrono::milliseconds(2000));
    EXPECT_EQ(counter, 3);
}

TEST(ThreadPoolTest, ClearPendingWithRunningTasks) {
    thread_pool::ThreadPool pool{2};
    std::latch started{1};

    auto slow = pool.submit([&started] {
        started.count_down();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    });
    started.wait();

    // Submit a batch; some may start, some queue
    auto q1 = pool.submit([] { return 1; });
    auto q2 = pool.submit([] { return 2; });
    pool.clear_pending();

    // slow task is still running
    EXPECT_GE(pool.active_tasks(), 1);

    // wait_all should only wait for the running task(s)
    auto start = std::chrono::steady_clock::now();
    pool.wait_all();
    auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_LT(elapsed, std::chrono::seconds(1));
    EXPECT_EQ(pool.active_tasks(), 0);
}

// ── Concurrency ────────────────────────────────────────────────────────────

TEST(ThreadPoolTest, ConcurrentSubmissions) {
    thread_pool::ThreadPool pool{4};
    std::atomic<int> counter{0};

    std::vector<std::future<void>> futures;
    for (int i = 0; i < 100; ++i) {
        futures.push_back(pool.submit([&counter] {
            ++counter;
        }));
    }

    for (auto& f : futures) {
        f.get();
    }

    EXPECT_EQ(counter, 100);
}

TEST(ThreadPoolTest, SequentialExecutionOrder) {
    // FIFO order with a single worker is a documented contract.
    thread_pool::ThreadPool pool{1};
    std::vector<int> order;
    std::mutex order_mutex;

    for (int i = 0; i < 5; ++i) {
        int value = i;
        pool.submit([&order, &order_mutex, value] {
            std::lock_guard<std::mutex> lock(order_mutex);
            order.push_back(value);
        });
    }

    pool.wait_all();
    ASSERT_EQ(order.size(), 5);
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(order[i], i);
    }
}

TEST(ThreadPoolTest, SumReduction) {
    thread_pool::ThreadPool pool{4};

    std::vector<int> numbers(1000);
    std::iota(numbers.begin(), numbers.end(), 1);

    std::atomic<int> total{0};

    for (int n : numbers) {
        pool.submit([&total, n] {
            total += n;
        });
    }

    pool.wait_all();
    int expected = 1000 * 1001 / 2;
    EXPECT_EQ(total.load(), expected);
}

// ── Custom Timeout ─────────────────────────────────────────────────────────

TEST(ThreadPoolTest, CustomTimeoutParameter) {
    thread_pool::ThreadPool pool{2, std::chrono::seconds(10)};

    auto future = pool.submit([] {
        return std::string("custom timeout test");
    });

    EXPECT_EQ(future.get(), "custom timeout test");
}
