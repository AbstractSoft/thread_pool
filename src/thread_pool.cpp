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

#include "thread_pool.hpp"

namespace thread_pool
{
    namespace
    {
        constexpr std::size_t default_num_threads = 8;
    } // namespace

    ThreadPool::ThreadPool(std::size_t num_threads, std::chrono::seconds default_timeout)
        : default_timeout_{default_timeout}
    {
        if (num_threads == 0)
        {
            num_threads = default_num_threads;
        }
        workers_.reserve(num_threads);
        for (std::size_t idx = 0; idx < num_threads; ++idx)
        {
            workers_.emplace_back([this] { worker_loop(); });
        }
    }

    ThreadPool::~ThreadPool()
    {
        shutdown();
    }

    void ThreadPool::shutdown()
    {
        {
            std::lock_guard lock{queue_mutex_};
            if (stop_) { return; }
            stop_ = true;
        }
        cv_.notify_all();

        for (auto& worker : workers_)
        {
            if (worker.joinable())
            {
                worker.join();
            }
        }
    }

    std::optional<std::function<void()>> ThreadPool::pop_task()
    {
        std::unique_lock lock{queue_mutex_};
        cv_.wait(lock, [this] { return stop_ || !tasks_.empty(); });

        if (stop_ && tasks_.empty())
        {
            return std::nullopt;
        }

        auto task = std::move(tasks_.front());
        tasks_.pop();
        ++active_task_count_;
        return task;
    }

    void ThreadPool::worker_loop()
    {
        while (true)
        {
            auto opt = pop_task();
            if (!opt)
            {
                return;
            }

            try
            {
                (*opt)();
            }
            catch (...)
            {
                // packaged_task::operator() stores user exceptions in the
                // future and does not rethrow. However, system-level
                // exceptions (std::bad_alloc, std::future_error) can arise
                // from the wrapper infrastructure itself. The catch-all
                // prevents counter leaks and worker death in all cases.
            }

            {
                std::lock_guard lock{finished_mutex_};
                --active_task_count_;
                --pending_tasks_;
            }
            finished_cv_.notify_all();
        }
    }

    void ThreadPool::wait_all()
    {
        wait_all_with_timeout(default_timeout_);
    }

    std::size_t ThreadPool::clear_pending()
    {
        std::lock_guard lock_q{queue_mutex_};
        std::lock_guard lock_f{finished_mutex_};
        const std::size_t count = tasks_.size();
        tasks_ = {};
        pending_tasks_ -= count;
        finished_cv_.notify_all();
        return count;
    }

    std::size_t ThreadPool::active_tasks() const noexcept
    {
        return active_task_count_;
    }
} // namespace thread_pool
