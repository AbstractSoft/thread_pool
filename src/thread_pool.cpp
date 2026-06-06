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
#include <iostream>

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
        for (std::size_t i = 0; i < num_threads; ++i)
        {
            workers_.emplace_back([this] { worker_loop(); });
        }
    }

    ThreadPool::~ThreadPool()
    {
        {
            std::lock_guard<std::mutex> lock{queue_mutex_};
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

    void ThreadPool::worker_loop()
    {
        while (true)
        {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock{queue_mutex_};
                cv_.wait(lock, [this] { return stop_ || !tasks_.empty(); });

                if (stop_ && tasks_.empty())
                {
                    return;
                }

                task = std::move(tasks_.front());
                tasks_.pop();
                ++active_task_count_;
            }

            task();

            {
                std::lock_guard<std::mutex> lock{finished_mutex_};
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

    void ThreadPool::wait_all_with_timeout(std::chrono::seconds timeout)
    {
        std::unique_lock<std::mutex> lock{finished_mutex_};
        bool completed = finished_cv_.wait_for(lock, timeout, [this]
        {
            return active_task_count_ == 0 && pending_tasks_ == 0;
        });

        if (!completed)
        {
#ifndef THREAD_POOL_SILENT
            // Note: the two atomic loads below are not a single atomic snapshot.
            // A task could finish between them, making the count transiently
            // inconsistent — acceptable for a diagnostic message.
            std::cerr << "ThreadPool::wait_all timed out after " << timeout.count()
                << " seconds — " << active_task_count_.load() << " tasks still active, "
                << (pending_tasks_.load() - active_task_count_.load()) << " queued\n";
#endif
        }
    }

    std::size_t ThreadPool::active_tasks() const
    {
        return active_task_count_; // atomic load — no mutex needed
    }
} // namespace thread_pool
