module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

export module Kairo.EngineCore.LogicState;

import Kairo.EngineCore.Entity;
import Kairo.EngineCore.TextValidation;
import Kairo.Foundation.Math.Vector;

export namespace kairo::engine
{
    using LogicStateValue = std::variant<bool, double,
        kairo::foundation::math::Vec3d, Entity, std::string>;

    struct LogicStateEntry final
    {
        std::string Name;
        LogicStateValue Value;
        friend bool operator==(const LogicStateEntry&, const LogicStateEntry&) = default;
    };

    struct LogicTimer final
    {
        std::string Name;
        double RemainingSeconds = 0.0;
        double RepeatSeconds = 0.0;
        friend bool operator==(const LogicTimer&, const LogicTimer&) = default;
    };

    struct LogicStateSnapshot final
    {
        std::vector<LogicStateEntry> Variables;
        std::vector<LogicTimer> Timers;
        friend bool operator==(const LogicStateSnapshot&, const LogicStateSnapshot&) = default;
    };

    /// Persistent per-logic-instance state. Unlike VM registers, values survive
    /// event dispatches. Names and counts are bounded so authored graphs cannot
    /// grow memory indefinitely at runtime.
    class LogicState final
    {
    public:
        static constexpr std::size_t MaximumVariables = 4'096u;
        static constexpr std::size_t MaximumTimers = 1'024u;

        void Set(std::string name, LogicStateValue value)
        {
            ValidateName(name, "Logic variable");
            ValidateValue(value);
            if (!m_Variables.contains(name) && m_Variables.size() >= MaximumVariables)
                throw std::length_error("Logic state exceeds its variable safety limit.");
            m_Variables.insert_or_assign(std::move(name), std::move(value));
        }

        [[nodiscard]] bool Contains(std::string_view name) const
        {
            return m_Variables.contains(std::string(name));
        }

        [[nodiscard]] const LogicStateValue& At(std::string_view name) const
        {
            const auto found = m_Variables.find(std::string(name));
            if (found == m_Variables.end()) throw std::out_of_range("Logic variable does not exist.");
            return found->second;
        }

        template<class Value>
        [[nodiscard]] const Value& Get(std::string_view name) const
        {
            const LogicStateValue& value = At(name);
            const auto* typed = std::get_if<Value>(&value);
            if (typed == nullptr) throw std::runtime_error("Logic variable has the wrong runtime type.");
            return *typed;
        }

        bool Remove(std::string_view name)
        {
            return m_Variables.erase(std::string(name)) != 0u;
        }

        void StartTimer(std::string name, double durationSeconds,
            std::optional<double> repeatSeconds = std::nullopt)
        {
            ValidateName(name, "Logic timer");
            if (!std::isfinite(durationSeconds) || durationSeconds < 0.0)
                throw std::invalid_argument("Logic timer duration must be finite and non-negative.");
            const double repeat = repeatSeconds.value_or(0.0);
            if (!std::isfinite(repeat) || repeat < 0.0)
                throw std::invalid_argument("Logic timer repeat interval must be finite and non-negative.");
            if (!m_Timers.contains(name) && m_Timers.size() >= MaximumTimers)
                throw std::length_error("Logic state exceeds its timer safety limit.");
            m_Timers.insert_or_assign(std::move(name), LogicTimer{ {}, durationSeconds, repeat });
            auto& timer = m_Timers.begin()->second;
            (void)timer;
            // insert_or_assign moves the key, so restore the canonical name from
            // the map during snapshot/tick instead of duplicating it internally.
        }

        bool CancelTimer(std::string_view name)
        {
            return m_Timers.erase(std::string(name)) != 0u;
        }

        [[nodiscard]] bool HasTimer(std::string_view name) const
        {
            return m_Timers.contains(std::string(name));
        }

        /// Advances every timer and returns expired names in lexical order.
        /// Repeating timers preserve overshoot; one-shot timers are removed.
        [[nodiscard]] std::vector<std::string> TickTimers(double deltaSeconds)
        {
            if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0)
                throw std::invalid_argument("Logic timer delta must be finite and non-negative.");
            std::vector<std::string> expired;
            std::vector<std::string> remove;
            for (auto& [name, timer] : m_Timers)
            {
                timer.RemainingSeconds -= deltaSeconds;
                if (timer.RemainingSeconds > 0.0) continue;
                expired.push_back(name);
                if (timer.RepeatSeconds > 0.0)
                {
                    const double missed = std::floor((-timer.RemainingSeconds) / timer.RepeatSeconds);
                    timer.RemainingSeconds += (missed + 1.0) * timer.RepeatSeconds;
                }
                else
                {
                    remove.push_back(name);
                }
            }
            for (const std::string& name : remove) m_Timers.erase(name);
            std::ranges::sort(expired);
            return expired;
        }

        [[nodiscard]] LogicStateSnapshot Snapshot() const
        {
            LogicStateSnapshot snapshot;
            snapshot.Variables.reserve(m_Variables.size());
            for (const auto& [name, value] : m_Variables)
                snapshot.Variables.push_back({ name, value });
            snapshot.Timers.reserve(m_Timers.size());
            for (const auto& [name, timer] : m_Timers)
                snapshot.Timers.push_back({ name, timer.RemainingSeconds, timer.RepeatSeconds });
            std::ranges::sort(snapshot.Variables, {}, &LogicStateEntry::Name);
            std::ranges::sort(snapshot.Timers, {}, &LogicTimer::Name);
            return snapshot;
        }

        void Restore(LogicStateSnapshot snapshot)
        {
            if (snapshot.Variables.size() > MaximumVariables || snapshot.Timers.size() > MaximumTimers)
                throw std::length_error("Logic state snapshot exceeds its safety limits.");
            LogicState candidate;
            for (LogicStateEntry& entry : snapshot.Variables)
                candidate.Set(std::move(entry.Name), std::move(entry.Value));
            for (LogicTimer& timer : snapshot.Timers)
                candidate.StartTimer(std::move(timer.Name), timer.RemainingSeconds,
                    timer.RepeatSeconds > 0.0 ? std::optional<double>{ timer.RepeatSeconds } : std::nullopt);
            *this = std::move(candidate);
        }

    private:
        std::unordered_map<std::string, LogicStateValue> m_Variables;
        std::unordered_map<std::string, LogicTimer> m_Timers;

        static void ValidateName(std::string_view name, std::string_view role)
        {
            ValidateUtf8Text(name, { 1u, 128u, false, false }, role);
        }

        static void ValidateValue(const LogicStateValue& value)
        {
            std::visit([](const auto& typed) {
                using Value = std::decay_t<decltype(typed)>;
                if constexpr (std::is_same_v<Value, double>)
                {
                    if (!std::isfinite(typed))
                        throw std::invalid_argument("Logic floating-point state must be finite.");
                }
                else if constexpr (std::is_same_v<Value, kairo::foundation::math::Vec3d>)
                {
                    if (!std::isfinite(typed.x) || !std::isfinite(typed.y) || !std::isfinite(typed.z))
                        throw std::invalid_argument("Logic vector state must be finite.");
                }
                else if constexpr (std::is_same_v<Value, Entity>)
                {
                    if (!typed) throw std::invalid_argument("Logic entity state cannot be invalid.");
                }
                else if constexpr (std::is_same_v<Value, std::string>)
                {
                    ValidateUtf8Text(typed, { 0u, 64u * 1024u, true, true }, "Logic string state");
                }
            }, value);
        }
    };
}
