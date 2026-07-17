module;

#include <array>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

export module Kairo.EngineCore.LogicBytecode;

import Kairo.EngineCore.Entity;
import Kairo.EngineCore.TextValidation;
import Kairo.Foundation.Math.Vector;

export namespace kairo::engine
{
    enum class LogicEventKind : std::uint8_t
    {
        BeginPlay, Tick, InputPressed, InputReleased, CollisionBegin, CollisionEnd
    };

    enum class LogicOpcode : std::uint8_t
    {
        Halt, Print, Jump, JumpIfFalse, LoadBoolean, LoadFloat, LoadVector3,
        LoadEntity, AddFloat, SetEntityPosition, ApplyEntityImpulse
    };

    /// Fixed-width instruction operands are interpreted by Opcode. Keeping the
    /// in-memory representation explicit avoids compiler-dependent packed
    /// structs in serialized artifacts.
    struct LogicInstruction final
    {
        LogicOpcode Opcode = LogicOpcode::Halt;
        std::uint32_t A = 0u;
        std::uint32_t B = 0u;
        std::uint32_t C = 0u;
        std::uint32_t D = 0u;
        friend constexpr bool operator==(const LogicInstruction&, const LogicInstruction&) noexcept = default;
    };

    struct LogicEntryPoint final
    {
        LogicEventKind Event = LogicEventKind::BeginPlay;
        std::string Action;
        std::uint32_t InstructionOffset = 0u;
        friend bool operator==(const LogicEntryPoint&, const LogicEntryPoint&) = default;
    };

    /// Runtime-only program. Registers 0, 1, and 2 are populated by dispatch
    /// with delta seconds, scalar action value, and other collision entity.
    /// Compilers may allocate user registers starting at ReservedRegisterCount.
    struct LogicProgram final
    {
        static constexpr std::uint32_t ReservedRegisterCount = 3u;
        static constexpr std::size_t MaximumInstructions = 1'000'000u;
        static constexpr std::size_t MaximumConstants = 1'000'000u;
        static constexpr std::size_t MaximumEntries = 100'000u;
        static constexpr std::uint32_t MaximumRegisters = 65'536u;

        std::uint32_t RegisterCount = ReservedRegisterCount;
        std::vector<std::string> Strings;
        std::vector<double> Floats;
        std::vector<kairo::foundation::math::Vec3d> Vectors;
        std::vector<Entity> Entities;
        std::vector<LogicInstruction> Instructions;
        std::vector<LogicEntryPoint> Entries;

        void Validate() const
        {
            if (RegisterCount < ReservedRegisterCount || RegisterCount > MaximumRegisters)
                throw std::invalid_argument("Logic program register count is outside the supported range.");
            if (Instructions.empty() || Instructions.size() > MaximumInstructions)
                throw std::invalid_argument("Logic program requires between 1 and 1,000,000 instructions.");
            if (Strings.size() > MaximumConstants || Floats.size() > MaximumConstants ||
                Vectors.size() > MaximumConstants || Entities.size() > MaximumConstants)
                throw std::length_error("Logic program constant pool exceeds its safety limit.");
            if (Entries.empty() || Entries.size() > MaximumEntries)
                throw std::invalid_argument("Logic program requires at least one bounded entry point.");
            for (const auto& value : Strings)
                ValidateUtf8Text(value, { 0u, 64u * 1024u, true, true }, "Logic string constant");
            for (const auto& value : Floats)
                if (!std::isfinite(value)) throw std::invalid_argument("Logic float constant must be finite.");
            for (const auto& value : Vectors)
                if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z))
                    throw std::invalid_argument("Logic vector constant must be finite.");
            for (const Entity entity : Entities)
                if (!entity) throw std::invalid_argument("Logic entity constants cannot be zero.");
            for (const auto& entry : Entries)
            {
                switch (entry.Event)
                {
                    case LogicEventKind::BeginPlay:
                    case LogicEventKind::Tick:
                    case LogicEventKind::InputPressed:
                    case LogicEventKind::InputReleased:
                    case LogicEventKind::CollisionBegin:
                    case LogicEventKind::CollisionEnd: break;
                    default: throw std::invalid_argument("Logic entry point has an unknown event kind.");
                }
                if (entry.InstructionOffset >= Instructions.size())
                    throw std::invalid_argument("Logic entry point targets an invalid instruction.");
                const bool input = entry.Event == LogicEventKind::InputPressed ||
                    entry.Event == LogicEventKind::InputReleased;
                if (input) ValidateUtf8Text(entry.Action, { 1u, 64u, false, false }, "Logic input action");
                else if (!entry.Action.empty())
                    throw std::invalid_argument("Only input event entries may name an action.");
            }
            for (const auto& instruction : Instructions) ValidateInstruction(instruction);
        }

    private:
        void ValidateInstruction(const LogicInstruction& instruction) const
        {
            const auto reg = [this](std::uint32_t value) {
                if (value >= RegisterCount) throw std::invalid_argument("Logic instruction references an invalid register.");
            };
            const auto target = [this](std::uint32_t value) {
                if (value >= Instructions.size()) throw std::invalid_argument("Logic instruction jumps outside the program.");
            };
            switch (instruction.Opcode)
            {
                case LogicOpcode::Halt: break;
                case LogicOpcode::Print:
                    if (instruction.A >= Strings.size()) throw std::invalid_argument("Logic print string is invalid.");
                    break;
                case LogicOpcode::Jump: target(instruction.A); break;
                case LogicOpcode::JumpIfFalse: reg(instruction.A); target(instruction.B); break;
                case LogicOpcode::LoadBoolean: reg(instruction.A);
                    if (instruction.B > 1u) throw std::invalid_argument("Logic boolean constant must be zero or one.");
                    break;
                case LogicOpcode::LoadFloat: reg(instruction.A);
                    if (instruction.B >= Floats.size()) throw std::invalid_argument("Logic float constant is invalid.");
                    break;
                case LogicOpcode::LoadVector3: reg(instruction.A);
                    if (instruction.B >= Vectors.size()) throw std::invalid_argument("Logic vector constant is invalid.");
                    break;
                case LogicOpcode::LoadEntity: reg(instruction.A);
                    if (instruction.B >= Entities.size()) throw std::invalid_argument("Logic entity constant is invalid.");
                    break;
                case LogicOpcode::AddFloat: reg(instruction.A); reg(instruction.B); reg(instruction.C); break;
                case LogicOpcode::SetEntityPosition:
                case LogicOpcode::ApplyEntityImpulse: reg(instruction.A); reg(instruction.B); break;
                default: throw std::invalid_argument("Logic program contains an unknown opcode.");
            }
        }
    };

    struct LogicDispatch final
    {
        LogicEventKind Event = LogicEventKind::BeginPlay;
        std::string_view Action;
        double DeltaSeconds = 0.0;
        double ActionValue = 0.0;
        Entity OtherEntity;
    };

    /// Runtime host boundary shared by visual graphs and future C++ gameplay.
    /// Implementations own scene/physics mutation and validate entity lifetime.
    class LogicHost
    {
    public:
        virtual ~LogicHost() = default;
        virtual void Print(Entity owner, std::string_view message) = 0;
        virtual void SetEntityPosition(Entity entity,
            const kairo::foundation::math::Vec3d& position) = 0;
        virtual void ApplyEntityImpulse(Entity entity,
            const kairo::foundation::math::Vec3d& impulse) = 0;
    };

    class LogicInstance final
    {
    public:
        static constexpr std::size_t DefaultInstructionBudget = 100'000u;

        explicit LogicInstance(LogicProgram program) : m_Program(std::move(program))
        {
            m_Program.Validate();
            m_Registers.resize(m_Program.RegisterCount);
        }

        /// Executes every matching entry in deterministic declaration order.
        /// Registers are reset per entry so one event cannot observe temporary
        /// values from another. A shared dispatch budget bounds loops across all
        /// matching entries and turns runaway graphs into an explicit failure.
        [[nodiscard]] std::size_t Dispatch(Entity owner, const LogicDispatch& dispatch,
            LogicHost& host, std::size_t instructionBudget = DefaultInstructionBudget)
        {
            if (!owner) throw std::invalid_argument("Logic dispatch owner cannot be zero.");
            if (!std::isfinite(dispatch.DeltaSeconds) || dispatch.DeltaSeconds < 0.0 ||
                !std::isfinite(dispatch.ActionValue))
                throw std::invalid_argument("Logic dispatch values must be finite and delta time non-negative.");
            if (instructionBudget == 0u)
                throw std::invalid_argument("Logic dispatch instruction budget must be positive.");
            std::size_t executed = 0u;
            for (const auto& entry : m_Program.Entries)
            {
                if (entry.Event != dispatch.Event ||
                    ((entry.Event == LogicEventKind::InputPressed ||
                      entry.Event == LogicEventKind::InputReleased) && entry.Action != dispatch.Action))
                    continue;
                std::fill(m_Registers.begin(), m_Registers.end(), LogicValue{});
                m_Registers[0] = dispatch.DeltaSeconds;
                m_Registers[1] = dispatch.ActionValue;
                m_Registers[2] = dispatch.OtherEntity;
                Execute(owner, entry.InstructionOffset, host, instructionBudget, executed);
            }
            return executed;
        }

    private:
        using LogicValue = std::variant<std::monostate, bool, double,
            kairo::foundation::math::Vec3d, Entity>;
        LogicProgram m_Program;
        std::vector<LogicValue> m_Registers;

        template<class T>
        [[nodiscard]] const T& Register(std::uint32_t index, std::string_view role) const
        {
            const auto* value = std::get_if<T>(&m_Registers.at(index));
            if (value == nullptr) throw std::runtime_error("Logic " + std::string(role) + " register has the wrong runtime type.");
            return *value;
        }

        void Execute(Entity owner, std::uint32_t start, LogicHost& host,
            std::size_t budget, std::size_t& executed)
        {
            std::uint32_t pc = start;
            while (true)
            {
                if (executed >= budget)
                    throw std::runtime_error("Logic dispatch exceeded its instruction budget.");
                const LogicInstruction instruction = m_Program.Instructions.at(pc++);
                ++executed;
                switch (instruction.Opcode)
                {
                    case LogicOpcode::Halt: return;
                    case LogicOpcode::Print: host.Print(owner, m_Program.Strings.at(instruction.A)); break;
                    case LogicOpcode::Jump: pc = instruction.A; break;
                    case LogicOpcode::JumpIfFalse:
                        if (!Register<bool>(instruction.A, "branch")) pc = instruction.B;
                        break;
                    case LogicOpcode::LoadBoolean: m_Registers[instruction.A] = instruction.B != 0u; break;
                    case LogicOpcode::LoadFloat: m_Registers[instruction.A] = m_Program.Floats.at(instruction.B); break;
                    case LogicOpcode::LoadVector3: m_Registers[instruction.A] = m_Program.Vectors.at(instruction.B); break;
                    case LogicOpcode::LoadEntity: m_Registers[instruction.A] = m_Program.Entities.at(instruction.B); break;
                    case LogicOpcode::AddFloat:
                        m_Registers[instruction.A] = Register<double>(instruction.B, "addition input") +
                            Register<double>(instruction.C, "addition input");
                        break;
                    case LogicOpcode::SetEntityPosition:
                        host.SetEntityPosition(Register<Entity>(instruction.A, "entity"),
                            Register<kairo::foundation::math::Vec3d>(instruction.B, "position"));
                        break;
                    case LogicOpcode::ApplyEntityImpulse:
                        host.ApplyEntityImpulse(Register<Entity>(instruction.A, "entity"),
                            Register<kairo::foundation::math::Vec3d>(instruction.B, "impulse"));
                        break;
                }
                if (pc >= m_Program.Instructions.size())
                    throw std::runtime_error("Logic execution reached the end without Halt.");
            }
        }
    };

    namespace logic_bytecode_detail
    {
        constexpr std::array<std::byte, 8u> Magic{
            std::byte{'K'}, std::byte{'L'}, std::byte{'O'}, std::byte{'G'},
            std::byte{'I'}, std::byte{'C'}, std::byte{1}, std::byte{0} };
        constexpr std::size_t MaximumArtifactBytes = 64u * 1024u * 1024u;

        class Writer final
        {
        public:
            std::vector<std::byte> Bytes;
            void U8(std::uint8_t value) { Bytes.push_back(static_cast<std::byte>(value)); }
            void U32(std::uint32_t value)
            {
                for (unsigned shift = 0u; shift < 32u; shift += 8u)
                    U8(static_cast<std::uint8_t>(value >> shift));
            }
            void U64(std::uint64_t value)
            {
                for (unsigned shift = 0u; shift < 64u; shift += 8u)
                    U8(static_cast<std::uint8_t>(value >> shift));
            }
            void String(std::string_view value)
            {
                if (value.size() > std::numeric_limits<std::uint32_t>::max())
                    throw std::length_error("Logic string is too large to serialize.");
                U32(static_cast<std::uint32_t>(value.size()));
                const auto bytes = std::as_bytes(std::span(value.data(), value.size()));
                Bytes.insert(Bytes.end(), bytes.begin(), bytes.end());
            }
        };

        class Reader final
        {
        public:
            explicit Reader(std::span<const std::byte> bytes) : Bytes(bytes) {}
            std::uint8_t U8()
            {
                if (Position >= Bytes.size()) throw std::invalid_argument("Logic artifact is truncated.");
                return std::to_integer<std::uint8_t>(Bytes[Position++]);
            }
            std::uint32_t U32()
            {
                std::uint32_t value = 0u;
                for (unsigned shift = 0u; shift < 32u; shift += 8u) value |= std::uint32_t(U8()) << shift;
                return value;
            }
            std::uint64_t U64()
            {
                std::uint64_t value = 0u;
                for (unsigned shift = 0u; shift < 64u; shift += 8u) value |= std::uint64_t(U8()) << shift;
                return value;
            }
            std::string String()
            {
                const std::uint32_t count = U32();
                if (count > Bytes.size() - Position) throw std::invalid_argument("Logic artifact string is truncated.");
                std::string value(count, '\0');
                if (count != 0u) std::memcpy(value.data(), Bytes.data() + Position, count);
                Position += count;
                return value;
            }
            [[nodiscard]] bool Finished() const noexcept { return Position == Bytes.size(); }
            [[nodiscard]] std::size_t Remaining() const noexcept { return Bytes.size() - Position; }
        private:
            std::span<const std::byte> Bytes;
            std::size_t Position = 0u;
        };
    }

    [[nodiscard]] inline std::vector<std::byte> SerializeLogicProgram(const LogicProgram& program)
    {
        using namespace logic_bytecode_detail;
        program.Validate();
        Writer output;
        output.Bytes.insert(output.Bytes.end(), Magic.begin(), Magic.end());
        output.U32(program.RegisterCount);
        output.U32(static_cast<std::uint32_t>(program.Strings.size()));
        output.U32(static_cast<std::uint32_t>(program.Floats.size()));
        output.U32(static_cast<std::uint32_t>(program.Vectors.size()));
        output.U32(static_cast<std::uint32_t>(program.Entities.size()));
        output.U32(static_cast<std::uint32_t>(program.Instructions.size()));
        output.U32(static_cast<std::uint32_t>(program.Entries.size()));
        for (const auto& value : program.Strings) output.String(value);
        for (const double value : program.Floats) output.U64(std::bit_cast<std::uint64_t>(value));
        for (const auto& value : program.Vectors)
        {
            output.U64(std::bit_cast<std::uint64_t>(value.x));
            output.U64(std::bit_cast<std::uint64_t>(value.y));
            output.U64(std::bit_cast<std::uint64_t>(value.z));
        }
        for (const Entity value : program.Entities) output.U32(value.Value);
        for (const auto& value : program.Instructions)
        {
            output.U8(static_cast<std::uint8_t>(value.Opcode));
            output.U32(value.A); output.U32(value.B); output.U32(value.C); output.U32(value.D);
        }
        for (const auto& value : program.Entries)
        {
            output.U8(static_cast<std::uint8_t>(value.Event));
            output.String(value.Action);
            output.U32(value.InstructionOffset);
        }
        if (output.Bytes.size() > MaximumArtifactBytes)
            throw std::length_error("Logic artifact exceeds the 64 MiB runtime limit.");
        return output.Bytes;
    }

    [[nodiscard]] inline LogicProgram ParseLogicProgram(std::span<const std::byte> bytes)
    {
        using namespace logic_bytecode_detail;
        if (bytes.size() > MaximumArtifactBytes) throw std::length_error("Logic artifact exceeds the 64 MiB runtime limit.");
        if (bytes.size() < Magic.size() || !std::equal(Magic.begin(), Magic.end(), bytes.begin()))
            throw std::invalid_argument("Logic artifact has an unsupported header.");
        Reader input(bytes.subspan(Magic.size()));
        LogicProgram result;
        result.RegisterCount = input.U32();
        const auto strings = input.U32();
        const auto floats = input.U32();
        const auto vectors = input.U32();
        const auto entities = input.U32();
        const auto instructions = input.U32();
        const auto entries = input.U32();
        if (strings > LogicProgram::MaximumConstants || floats > LogicProgram::MaximumConstants ||
            vectors > LogicProgram::MaximumConstants || entities > LogicProgram::MaximumConstants ||
            instructions > LogicProgram::MaximumInstructions || entries > LogicProgram::MaximumEntries)
            throw std::length_error("Logic artifact declares a count above its safety limit.");
        const std::uint64_t minimumPayload = std::uint64_t(strings) * 4u +
            std::uint64_t(floats) * 8u + std::uint64_t(vectors) * 24u +
            std::uint64_t(entities) * 4u + std::uint64_t(instructions) * 17u +
            std::uint64_t(entries) * 9u;
        if (minimumPayload > input.Remaining())
            throw std::invalid_argument("Logic artifact declared pools cannot fit in the remaining bytes.");
        result.Strings.reserve(strings); result.Floats.reserve(floats); result.Vectors.reserve(vectors);
        result.Entities.reserve(entities); result.Instructions.reserve(instructions); result.Entries.reserve(entries);
        for (std::uint32_t i = 0; i < strings; ++i) result.Strings.push_back(input.String());
        for (std::uint32_t i = 0; i < floats; ++i) result.Floats.push_back(std::bit_cast<double>(input.U64()));
        for (std::uint32_t i = 0; i < vectors; ++i) result.Vectors.push_back({
            std::bit_cast<double>(input.U64()), std::bit_cast<double>(input.U64()),
            std::bit_cast<double>(input.U64()) });
        for (std::uint32_t i = 0; i < entities; ++i) result.Entities.push_back({ input.U32() });
        for (std::uint32_t i = 0; i < instructions; ++i) result.Instructions.push_back({
            static_cast<LogicOpcode>(input.U8()), input.U32(), input.U32(), input.U32(), input.U32() });
        for (std::uint32_t i = 0; i < entries; ++i) result.Entries.push_back({
            static_cast<LogicEventKind>(input.U8()), input.String(), input.U32() });
        if (!input.Finished()) throw std::invalid_argument("Logic artifact contains trailing bytes.");
        result.Validate();
        return result;
    }
}
