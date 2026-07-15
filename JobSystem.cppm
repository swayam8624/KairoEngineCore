module;

#include <condition_variable>
#include <concepts>
#include <cstddef>
#include <deque>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

export module Kairo.EngineCore.JobSystem;

export namespace kairo::engine
{
    /// Fixed worker-pool scheduler for short, independent engine jobs.
    /// Input: 1..256 worker threads; pass zero to use hardware concurrency
    /// with a safe one-thread fallback.
    /// Output: futures carrying task results/exceptions.
    /// Task: provide explicit background work without assigning ownership of
    /// renderer, physics, or scene objects. Call WaitIdle before destroying
    /// resources captured by enqueued jobs.
    class JobSystem final
    {
    public:
        explicit JobSystem(std::size_t workerCount = 0u)
        {
            if (workerCount == 0u)
            {
                const unsigned hardware = std::thread::hardware_concurrency();
                workerCount = hardware == 0u ? 1u : static_cast<std::size_t>(hardware);
            }
            if (workerCount > 256u) throw std::invalid_argument("JobSystem worker count must be in [1, 256].");
            m_Workers.reserve(workerCount);
            for (std::size_t index = 0u; index < workerCount; ++index) m_Workers.emplace_back([this] { WorkerLoop(); });
        }

        ~JobSystem()
        {
            {
                std::scoped_lock lock(m_Mutex);
                m_Stopping = true;
            }
            m_WorkAvailable.notify_all();
        }

        JobSystem(const JobSystem&) = delete;
        JobSystem& operator=(const JobSystem&) = delete;

        /// Input: callable taking no arguments. Output: a future for its
        /// result. Task: queue work exactly once; exceptions propagate through
        /// the future rather than terminating a worker thread.
        template<typename Callable>
            requires std::invocable<std::decay_t<Callable>&>
        [[nodiscard]] auto Submit(Callable&& callable) -> std::future<std::invoke_result_t<std::decay_t<Callable>&>>
        {
            using StoredCallable = std::decay_t<Callable>;
            using Result = std::invoke_result_t<StoredCallable&>;

            // Keep the queue copyable while accepting move-only jobs. This also
            // avoids routing an importer-defined lambda through packaged_task's
            // implementation detail, which is fragile with some C++ module and
            // libstdc++ combinations.
            auto function = std::make_shared<StoredCallable>(std::forward<Callable>(callable));
            auto promise = std::make_shared<std::promise<Result>>();
            std::future<Result> future = promise->get_future();
            {
                std::scoped_lock lock(m_Mutex);
                if (m_Stopping) throw std::runtime_error("Cannot submit work after JobSystem shutdown begins.");
                m_Tasks.emplace_back([function, promise]
                {
                    try
                    {
                        if constexpr (std::is_void_v<Result>)
                        {
                            std::invoke(*function);
                            promise->set_value();
                        }
                        else
                        {
                            promise->set_value(std::invoke(*function));
                        }
                    }
                    catch (...)
                    {
                        promise->set_exception(std::current_exception());
                    }
                });
            }
            m_WorkAvailable.notify_one();
            return future;
        }

        /// Task: wait until the pending queue and currently running jobs are
        /// both empty. It must not be called by a worker that owns unfinished
        /// work, because that would create an application-level deadlock.
        void WaitIdle()
        {
            std::unique_lock lock(m_Mutex);
            m_Idle.wait(lock, [this] { return m_Tasks.empty() && m_ActiveWorkers == 0u; });
        }

        [[nodiscard]] std::size_t WorkerCount() const noexcept { return m_Workers.size(); }

    private:
        mutable std::mutex m_Mutex;
        std::condition_variable m_WorkAvailable;
        std::condition_variable m_Idle;
        std::deque<std::function<void()>> m_Tasks;
        std::vector<std::jthread> m_Workers;
        std::size_t m_ActiveWorkers = 0u;
        bool m_Stopping = false;

        void WorkerLoop()
        {
            for (;;)
            {
                std::function<void()> task;
                {
                    std::unique_lock lock(m_Mutex);
                    m_WorkAvailable.wait(lock, [this] { return m_Stopping || !m_Tasks.empty(); });
                    if (m_Stopping && m_Tasks.empty()) return;
                    task = std::move(m_Tasks.front());
                    m_Tasks.pop_front();
                    ++m_ActiveWorkers;
                }
                task();
                {
                    std::scoped_lock lock(m_Mutex);
                    --m_ActiveWorkers;
                    if (m_Tasks.empty() && m_ActiveWorkers == 0u) m_Idle.notify_all();
                }
            }
        }
    };
}
