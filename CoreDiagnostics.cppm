module;

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

export module Kairo.EngineCore.Diagnostics;

import Kairo.EngineCore.Logger;

export namespace kairo::engine
{
    /// Stable top-level ownership channel for diagnostics emitted by Kairo
    /// systems. Input: a source subsystem. Output: canonical category text.
    /// Task: make logs filterable by engine responsibility without requiring
    /// every renderer, asset importer, and tool to invent category strings.
    enum class DiagnosticChannel : std::uint8_t
    {
        Core,
        Assets,
        Scene,
        Renderer,
        Physics,
        Editor,
        Tooling
    };

    [[nodiscard]] constexpr std::string_view NameOf(DiagnosticChannel channel) noexcept
    {
        switch (channel)
        {
        case DiagnosticChannel::Core: return "Core";
        case DiagnosticChannel::Assets: return "Assets";
        case DiagnosticChannel::Scene: return "Scene";
        case DiagnosticChannel::Renderer: return "Renderer";
        case DiagnosticChannel::Physics: return "Physics";
        case DiagnosticChannel::Editor: return "Editor";
        case DiagnosticChannel::Tooling: return "Tooling";
        }
        return "Core";
    }

    /// Canonical, bounded diagnostic service for host applications and editor
    /// tools. It reuses Logger's locking, retention, filtering, and snapshot
    /// behavior instead of becoming a competing log store. Hosts can render
    /// Snapshot() in a console panel or forward it to a file/telemetry sink.
    class CoreDiagnostics final
    {
    public:
        explicit CoreDiagnostics(std::size_t capacity = 1024u) : m_Logger(capacity) {}

        /// Input: source channel, severity, and non-empty human-readable
        /// message. Output: one retained diagnostic when it passes filtering.
        /// Task: emit a cross-system record using a stable category name.
        void Emit(DiagnosticChannel channel, LogSeverity severity, std::string_view message)
        {
            m_Logger.Write(severity, NameOf(channel), message);
        }

        /// Input: a non-empty local scope such as `Import` or `Swapchain`.
        /// Output: one retained diagnostic with `Channel/Scope` category.
        /// Task: retain useful subsystem detail without losing top-level
        /// ownership. Empty scopes use the canonical channel category.
        void Emit(DiagnosticChannel channel, std::string_view scope, LogSeverity severity,
            std::string_view message)
        {
            if (scope.empty())
            {
                Emit(channel, severity, message);
                return;
            }
            std::string category(NameOf(channel));
            category.push_back('/');
            category.append(scope);
            m_Logger.Write(severity, category, message);
        }

        void SetMinimumSeverity(LogSeverity severity) noexcept { m_Logger.SetMinimumSeverity(severity); }
        [[nodiscard]] LogSeverity MinimumSeverity() const noexcept { return m_Logger.MinimumSeverity(); }
        [[nodiscard]] std::vector<LogEntry> Snapshot() const { return m_Logger.Snapshot(); }
        void Clear() noexcept { m_Logger.Clear(); }

    private:
        Logger m_Logger;
    };
}
