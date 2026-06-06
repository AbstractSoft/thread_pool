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

#ifndef THREAD_POOL_HPP
#define THREAD_POOL_HPP

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <type_traits>
#include <vector>

namespace thread_pool
{
    /// A fixed-size thread pool that executes tasks in the background.
    ///
    /// Tasks are submitted via `submit()` which accepts any callable with any
    /// arguments. The pool forwards arguments perfectly and supports move-only
    /// types.
    ///
    /// `wait_all()` blocks until all submitted tasks complete (up to a configurable
    /// timeout). `active_tasks()` returns the number of currently running tasks.
    ///
    /// The pool is non-copyable and non-movable. Tasks submitted after `shutdown()`
    /// (via destructor) throw `std::runtime_error`.
    class ThreadPool
    {
    public:
        /// Construct a thread pool with the given number of worker threads.
        ///
        /// @param num_threads  Number of worker threads. If 0, defaults to 8.
        /// @param default_timeout  Default timeout for `wait_all()`.
        explicit ThreadPool(std::size_t num_threads = 0,
                            std::chrono::seconds default_timeout = std::chrono::hours(1));

        ~ThreadPool();

        /// Non-copyable, non-movable.
        ThreadPool(const ThreadPool&) = delete;
        ThreadPool& operator=(const ThreadPool&) = delete;
        ThreadPool(ThreadPool&&) = delete;
        ThreadPool& operator=(ThreadPool&&) = delete;

        /// Submit a task to the pool.
        ///
        /// Accepts any callable `F` with arguments `Args...`. Arguments are perfectly
        /// forwarded. The return type is deduced via `std::invoke_result_t`.
        ///
        /// @return A `std::future` that will hold the result when the task completes.
        /// @throws std::runtime_error if the pool has been shut down.
        template <typename F, typename... Args>
        auto submit(F&& func, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>>;

        /// Block until all tasks complete. Uses the default timeout from construction.
        void wait_all();

        /// Block until all tasks complete or the timeout expires.
        ///
        /// @param timeout  Maximum time to wait.
        void wait_all_with_timeout(std::chrono::seconds timeout);

        /// Return the number of tasks currently being executed by worker threads.
        ///
        /// This does not include tasks that are queued but not yet picked up.
        /// This is an atomic load — no mutex required.
        std::size_t active_tasks() const;

    private:
        void worker_loop();

        std::vector<std::thread> workers_;
        std::queue<std::function<void()>> tasks_;
        std::mutex queue_mutex_;
        std::condition_variable cv_;
        std::condition_variable finished_cv_;
        std::atomic<bool> stop_{false};
        std::atomic<std::size_t> active_task_count_{0};
        std::atomic<std::size_t> pending_tasks_{0};
        mutable std::mutex finished_mutex_;
        std::chrono::seconds default_timeout_;
    };

    // ── Template implementation (must be in header) ────────────────────────────

    template <typename F, typename... Args>
    auto ThreadPool::submit(F&& func, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>
    {
        using return_type = std::invoke_result_t<F, Args...>;

        auto args_tuple = std::make_tuple(std::forward<Args>(args)...);
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            [func = std::forward<F>(func),
                args_tuple = std::move(args_tuple)]() mutable
            {
                return std::apply(std::move(func), std::move(args_tuple));
            }
        );

        std::future<return_type> result = task->get_future();

        {
            std::lock_guard<std::mutex> lock{queue_mutex_};
            if (stop_)
            {
                throw std::runtime_error("Submit on stopped ThreadPool");
            }
            ++pending_tasks_;
            tasks_.emplace([task]() { (*task)(); });
        }
        cv_.notify_one();

        return result;
    }
} // namespace thread_pool

#endif // THREAD_POOL_HPP
