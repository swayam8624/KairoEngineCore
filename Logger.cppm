module;

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

export module Kairo.EngineCore.Logger;

export namespace kairo::engine
{
    /// Ordered severity used by Logger::SetMinimumSeverity. The enum is kept
    /// platform-neutral so host applications can forward records to their own
    /// console, file, or telemetry sink without importing a logging library.
    enum class LogSeverity : std::uint8_t { Trace, Debug, Info, Warning, Error, Critical };

    /// Immutable snapshot value returned by Logger. Sequence is monotonic for
    /// one logger instance; it is intentionally not a wall-clock timestamp so
    /// deterministic tests and offline tools do not inherit clock dependence.
    struct LogEntry final
    {
        std::uint64_t Sequence = 0u;
        LogSeverity Severity = LogSeverity::Info;
        std::string Category;
        std::string Message;
    };

    /// Thread-safe bounded in-memory log for engine/editor diagnostics.
    /// Input: a positive retained-entry capacity.
    /// Output: ordered snapshots that remain valid after subsequent writes.
    /// Task: preserve recent diagnostic records without imposing a concrete
    /// terminal/file backend on applications embedding EngineCore.
    class Logger final
    {
    public:
        explicit Logger(std::size_t capacity = 1024u) : m_Capacity(capacity)
        {
            if (capacity == 0u) throw std::invalid_argument("Logger capacity must be positive.");
        }

        /// Input: minimum severity to retain. Output: no value.
        /// Task: atomically select the record filter. Existing records remain
        /// available so a tooling UI can change its view without losing data.
        void SetMinimumSeverity(LogSeverity severity) noexcept
        {
            std::scoped_lock lock(m_Mutex);
            m_MinimumSeverity = severity;
        }

        [[nodiscard]] LogSeverity MinimumSeverity() const noexcept
        {
            std::scoped_lock lock(m_Mutex);
            return m_MinimumSeverity;
        }

        /// Input: non-empty category and message text; any severity.
        /// Output: no value; filtered records do not consume capacity.
        /// Task: append a deterministic record and evict only the oldest item
        /// when capacity is reached. Throws for empty diagnostic context.
        void Write(LogSeverity severity, std::string_view category, std::string_view message)
        {
            if (category.empty() || message.empty()) throw std::invalid_argument("Logger records require non-empty category and message.");
            std::scoped_lock lock(m_Mutex);
            if (severity < m_MinimumSeverity) return;
            if (m_Entries.size() == m_Capacity) m_Entries.erase(m_Entries.begin());
            m_Entries.push_back({ ++m_NextSequence, severity, std::string(category), std::string(message) });
        }

        /// Output: copy of retained entries in ascending sequence order.
        [[nodiscard]] std::vector<LogEntry> Snapshot() const
        {
            std::scoped_lock lock(m_Mutex);
            return m_Entries;
        }

        /// Task: discard retained records without resetting sequence identity.
        void Clear() noexcept
        {
            std::scoped_lock lock(m_Mutex);
            m_Entries.clear();
        }

    private:
        mutable std::mutex m_Mutex;
        std::size_t m_Capacity;
        std::uint64_t m_NextSequence = 0u;
        LogSeverity m_MinimumSeverity = LogSeverity::Trace;
        std::vector<LogEntry> m_Entries;
    };
}
