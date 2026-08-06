module;

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

export module Kairo.EngineCore.GameplayVM;

import Kairo.Assets.BinaryFormat;
import Kairo.EngineCore.Entity;
import Kairo.EngineCore.TextValidation;
import Kairo.Foundation.Math.Vector;

export namespace kairo::engine
{
    enum class GameplayEventKind : std::uint8_t
    {
        BeginPlay = 1u,
        Tick = 2u,
        InputPressed = 3u,
        InputReleased = 4u,
        CollisionBegin = 5u,
        CollisionEnd = 6u,
        Custom = 7u
    };

    enum class GameplayOpcode : std::uint8_t
    {
        Halt = 0u,
        Print = 1u,
        Jump = 2u,
        JumpIfFalse = 3u,
        LoadBoolean = 4u,
        LoadFloat = 5u,
        LoadVector3 = 6u,
        LoadEntity = 7u,
        LoadOwner = 8u,
        Move = 9u,
        AddFloat = 10u,
        SubtractFloat = 11u,
        MultiplyFloat = 12u,
        DivideFloat = 13u,
        LessFloat = 14u,
        EqualFloat = 15u,
        EqualEntity = 16u,
        GetVariable = 17u,
        SetVariable = 18u,
        FindFirstWithTag = 19u,
        HasTag = 20u,
        GetEntityPosition = 21u,
        SetEntityPosition = 22u,
        SetEntityEnabled = 23u,
        SpawnEntity = 24u,
        DestroyEntity = 25u,
        AddTag = 26u,
        RemoveTag = 27u
    };

    using GameplayValue = std::variant<std::monostate, bool, double,
        kairo::foundation::math::Vec3d, Entity>;

    struct GameplayVariableDefinition final
    {
        std::string Name;
        GameplayValue DefaultValue;
        friend bool operator==(const GameplayVariableDefinition&,
            const GameplayVariableDefinition&) = default;
    };

    struct GameplayInstruction final
    {
        GameplayOpcode Opcode = GameplayOpcode::Halt;
        std::uint32_t A = 0u;
        std::uint32_t B = 0u;
        std::uint32_t C = 0u;
        std::uint32_t D = 0u;
        friend constexpr bool operator==(const GameplayInstruction&,
            const GameplayInstruction&) noexcept = default;
    };

    struct GameplayEntryPoint final
    {
        GameplayEventKind Event = GameplayEventKind::BeginPlay;
        std::string Action;
        std::uint32_t InstructionOffset = 0u;
        friend bool operator==(const GameplayEntryPoint&, const GameplayEntryPoint&) = default;
    };

    struct GameplayProgram final
    {
        static constexpr std::uint32_t ReservedRegisterCount = 3u;
        static constexpr std::uint32_t MaximumRegisters = 65'536u;
        static constexpr std::uint32_t MaximumVariables = 16'384u;
        static constexpr std::size_t MaximumConstants = 1'000'000u;
        static constexpr std::size_t MaximumInstructions = 1'000'000u;
        static constexpr std::size_t MaximumEntries = 100'000u;

        std::uint32_t RegisterCount = ReservedRegisterCount;
        std::vector<std::string> Strings;
        std::vector<double> Floats;
        std::vector<kairo::foundation::math::Vec3d> Vectors;
        std::vector<Entity> Entities;
        std::vector<GameplayVariableDefinition> Variables;
        std::vector<GameplayInstruction> Instructions;
        std::vector<GameplayEntryPoint> Entries;

        void Validate() const
        {
            if (RegisterCount < ReservedRegisterCount || RegisterCount > MaximumRegisters)
                throw std::invalid_argument("Gameplay register count is outside the supported range.");
            if (Strings.size() > MaximumConstants || Floats.size() > MaximumConstants ||
                Vectors.size() > MaximumConstants || Entities.size() > MaximumConstants)
                throw std::length_error("Gameplay constant pool exceeds its safety limit.");
            if (Variables.size() > MaximumVariables)
                throw std::length_error("Gameplay variable count exceeds its safety limit.");
            if (Instructions.empty() || Instructions.size() > MaximumInstructions)
                throw std::invalid_argument("Gameplay program requires a bounded instruction stream.");
            if (Entries.empty() || Entries.size() > MaximumEntries)
                throw std::invalid_argument("Gameplay program requires at least one entry point.");

            for (const std::string& value : Strings)
                ValidateUtf8Text(value, { 0u, 64u * 1024u, true, true },
                    "Gameplay string constant");
            for (double value : Floats)
                if (!std::isfinite(value))
                    throw std::invalid_argument("Gameplay float constants must be finite.");
            for (const auto& value : Vectors)
                if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z))
                    throw std::invalid_argument("Gameplay vector constants must be finite.");
            for (Entity value : Entities)
                if (!value) throw std::invalid_argument("Gameplay entity constants cannot be zero.");

            std::unordered_set<std::string> names;
            for (const GameplayVariableDefinition& variable : Variables)
            {
                ValidateUtf8Text(variable.Name, { 1u, 64u, false, false },
                    "Gameplay variable name");
                if (!names.insert(variable.Name).second)
                    throw std::invalid_argument("Gameplay variable names must be unique.");
                ValidateValue(variable.DefaultValue, "Gameplay variable default");
            }

            for (const GameplayEntryPoint& entry : Entries)
            {
                switch (entry.Event)
                {
                    case GameplayEventKind::BeginPlay:
                    case GameplayEventKind::Tick:
                    case GameplayEventKind::InputPressed:
                    case GameplayEventKind::InputReleased:
                    case GameplayEventKind::CollisionBegin:
                    case GameplayEventKind::CollisionEnd:
                    case GameplayEventKind::Custom: break;
                    default: throw std::invalid_argument("Gameplay entry event is invalid.");
                }
                if (entry.InstructionOffset >= Instructions.size())
                    throw std::invalid_argument("Gameplay entry point is outside the instruction stream.");
                const bool named = entry.Event == GameplayEventKind::InputPressed ||
                    entry.Event == GameplayEventKind::InputReleased ||
                    entry.Event == GameplayEventKind::Custom;
                if (named)
                    ValidateUtf8Text(entry.Action, { 1u, 64u, false, false },
                        "Gameplay event action");
                else if (!entry.Action.empty())
                    throw std::invalid_argument("Only input and custom events may name an action.");
            }
            for (const GameplayInstruction& instruction : Instructions)
                ValidateInstruction(instruction);
        }

        [[nodiscard]] std::size_t VariableIndex(std::string_view name) const
        {
            const auto found = std::ranges::find(Variables, name,
                [](const GameplayVariableDefinition& variable) -> std::string_view
                { return variable.Name; });
            if (found == Variables.end())
                throw std::out_of_range("Gameplay variable is not declared.");
            return static_cast<std::size_t>(found - Variables.begin());
        }

    private:
        static void ValidateValue(const GameplayValue& value, const char* role)
        {
            if (std::holds_alternative<std::monostate>(value))
                throw std::invalid_argument(std::string(role) + " cannot be empty.");
            if (const auto* number = std::get_if<double>(&value); number != nullptr &&
                !std::isfinite(*number))
                throw std::invalid_argument(std::string(role) + " must be finite.");
            if (const auto* vector =
                std::get_if<kairo::foundation::math::Vec3d>(&value); vector != nullptr &&
                (!std::isfinite(vector->x) || !std::isfinite(vector->y) ||
                    !std::isfinite(vector->z)))
                throw std::invalid_argument(std::string(role) + " must be finite.");
        }

        void ValidateInstruction(const GameplayInstruction& instruction) const
        {
            const auto reg = [this](std::uint32_t index)
            {
                if (index >= RegisterCount)
                    throw std::invalid_argument("Gameplay instruction references an invalid register.");
            };
            const auto target = [this](std::uint32_t index)
            {
                if (index >= Instructions.size())
                    throw std::invalid_argument("Gameplay instruction jumps outside the program.");
            };
            const auto string = [this](std::uint32_t index)
            {
                if (index >= Strings.size())
                    throw std::invalid_argument("Gameplay instruction references an invalid string.");
            };
            const auto variable = [this](std::uint32_t index)
            {
                if (index >= Variables.size())
                    throw std::invalid_argument("Gameplay instruction references an invalid variable.");
            };

            switch (instruction.Opcode)
            {
                case GameplayOpcode::Halt: break;
                case GameplayOpcode::Print: string(instruction.A); break;
                case GameplayOpcode::Jump: target(instruction.A); break;
                case GameplayOpcode::JumpIfFalse: reg(instruction.A); target(instruction.B); break;
                case GameplayOpcode::LoadBoolean: reg(instruction.A);
                    if (instruction.B > 1u)
                        throw std::invalid_argument("Gameplay boolean constant is invalid.");
                    break;
                case GameplayOpcode::LoadFloat: reg(instruction.A);
                    if (instruction.B >= Floats.size())
                        throw std::invalid_argument("Gameplay float constant is invalid.");
                    break;
                case GameplayOpcode::LoadVector3: reg(instruction.A);
                    if (instruction.B >= Vectors.size())
                        throw std::invalid_argument("Gameplay vector constant is invalid.");
                    break;
                case GameplayOpcode::LoadEntity: reg(instruction.A);
                    if (instruction.B >= Entities.size())
                        throw std::invalid_argument("Gameplay entity constant is invalid.");
                    break;
                case GameplayOpcode::LoadOwner: reg(instruction.A); break;
                case GameplayOpcode::Move: reg(instruction.A); reg(instruction.B); break;
                case GameplayOpcode::AddFloat:
                case GameplayOpcode::SubtractFloat:
                case GameplayOpcode::MultiplyFloat:
                case GameplayOpcode::DivideFloat:
                case GameplayOpcode::LessFloat:
                case GameplayOpcode::EqualFloat:
                case GameplayOpcode::EqualEntity:
                    reg(instruction.A); reg(instruction.B); reg(instruction.C); break;
                case GameplayOpcode::GetVariable: reg(instruction.A); variable(instruction.B); break;
                case GameplayOpcode::SetVariable: variable(instruction.A); reg(instruction.B); break;
                case GameplayOpcode::FindFirstWithTag:
                    reg(instruction.A); string(instruction.B); break;
                case GameplayOpcode::HasTag:
                    reg(instruction.A); reg(instruction.B); string(instruction.C); break;
                case GameplayOpcode::GetEntityPosition:
                case GameplayOpcode::SetEntityPosition:
                case GameplayOpcode::SetEntityEnabled:
                    reg(instruction.A); reg(instruction.B); break;
                case GameplayOpcode::SpawnEntity:
                    reg(instruction.A); string(instruction.B); reg(instruction.C); break;
                case GameplayOpcode::DestroyEntity: reg(instruction.A); break;
                case GameplayOpcode::AddTag:
                case GameplayOpcode::RemoveTag:
                    reg(instruction.A); string(instruction.B); break;
                default: throw std::invalid_argument("Gameplay instruction opcode is invalid.");
            }
        }
    };

    struct GameplayDispatch final
    {
        GameplayEventKind Event = GameplayEventKind::BeginPlay;
        std::string_view Action;
        double DeltaSeconds = 0.0;
        double ActionValue = 0.0;
        Entity OtherEntity;
    };

    class GameplayHost
    {
    public:
        virtual ~GameplayHost() = default;
        virtual void Print(Entity owner, std::string_view message) = 0;
        [[nodiscard]] virtual Entity FindFirstWithTag(std::string_view tag) = 0;
        [[nodiscard]] virtual bool HasTag(Entity entity, std::string_view tag) = 0;
        [[nodiscard]] virtual kairo::foundation::math::Vec3d GetEntityPosition(Entity entity) = 0;
        virtual void SetEntityPosition(Entity entity,
            const kairo::foundation::math::Vec3d& position) = 0;
        virtual void SetEntityEnabled(Entity entity, bool enabled) = 0;
        [[nodiscard]] virtual Entity SpawnEntity(std::string_view name,
            const kairo::foundation::math::Vec3d& position) = 0;
        virtual void DestroyEntity(Entity entity) = 0;
        virtual void AddTag(Entity entity, std::string_view tag) = 0;
        virtual void RemoveTag(Entity entity, std::string_view tag) = 0;
    };

    class GameplayInstance final
    {
    public:
        static constexpr std::size_t DefaultInstructionBudget = 100'000u;

        explicit GameplayInstance(GameplayProgram program) : m_Program(std::move(program))
        {
            m_Program.Validate();
            m_Registers.resize(m_Program.RegisterCount);
            ResetState();
        }

        void ResetState()
        {
            m_Variables.clear();
            m_Variables.reserve(m_Program.Variables.size());
            for (const GameplayVariableDefinition& variable : m_Program.Variables)
                m_Variables.push_back(variable.DefaultValue);
        }

        [[nodiscard]] const GameplayValue& Variable(std::string_view name) const
        {
            return m_Variables.at(m_Program.VariableIndex(name));
        }

        [[nodiscard]] std::size_t Dispatch(Entity owner, const GameplayDispatch& dispatch,
            GameplayHost& host, std::size_t instructionBudget = DefaultInstructionBudget)
        {
            if (!owner) throw std::invalid_argument("Gameplay dispatch owner cannot be zero.");
            if (!std::isfinite(dispatch.DeltaSeconds) || dispatch.DeltaSeconds < 0.0 ||
                !std::isfinite(dispatch.ActionValue))
                throw std::invalid_argument("Gameplay dispatch values must be finite.");
            if (instructionBudget == 0u)
                throw std::invalid_argument("Gameplay instruction budget must be positive.");

            std::size_t executed = 0u;
            for (const GameplayEntryPoint& entry : m_Program.Entries)
            {
                if (entry.Event != dispatch.Event) continue;
                const bool named = entry.Event == GameplayEventKind::InputPressed ||
                    entry.Event == GameplayEventKind::InputReleased ||
                    entry.Event == GameplayEventKind::Custom;
                if (named && entry.Action != dispatch.Action) continue;
                std::fill(m_Registers.begin(), m_Registers.end(), GameplayValue{});
                m_Registers[0] = dispatch.DeltaSeconds;
                m_Registers[1] = dispatch.ActionValue;
                m_Registers[2] = dispatch.OtherEntity;
                Execute(owner, entry.InstructionOffset, host, instructionBudget, executed);
            }
            return executed;
        }

    private:
        GameplayProgram m_Program;
        std::vector<GameplayValue> m_Registers;
        std::vector<GameplayValue> m_Variables;

        template<class Value>
        [[nodiscard]] const Value& Register(std::uint32_t index, const char* role) const
        {
            const auto* value = std::get_if<Value>(&m_Registers.at(index));
            if (value == nullptr)
                throw std::runtime_error(std::string("Gameplay ") + role +
                    " register has the wrong runtime type.");
            return *value;
        }

        void Execute(Entity owner, std::uint32_t start, GameplayHost& host,
            std::size_t budget, std::size_t& executed)
        {
            std::uint32_t pc = start;
            while (true)
            {
                if (executed >= budget)
                    throw std::runtime_error("Gameplay dispatch exceeded its instruction budget.");
                const GameplayInstruction instruction = m_Program.Instructions.at(pc++);
                ++executed;
                switch (instruction.Opcode)
                {
                    case GameplayOpcode::Halt: return;
                    case GameplayOpcode::Print:
                        host.Print(owner, m_Program.Strings.at(instruction.A)); break;
                    case GameplayOpcode::Jump: pc = instruction.A; break;
                    case GameplayOpcode::JumpIfFalse:
                        if (!Register<bool>(instruction.A, "branch")) pc = instruction.B;
                        break;
                    case GameplayOpcode::LoadBoolean:
                        m_Registers[instruction.A] = instruction.B != 0u; break;
                    case GameplayOpcode::LoadFloat:
                        m_Registers[instruction.A] = m_Program.Floats.at(instruction.B); break;
                    case GameplayOpcode::LoadVector3:
                        m_Registers[instruction.A] = m_Program.Vectors.at(instruction.B); break;
                    case GameplayOpcode::LoadEntity:
                        m_Registers[instruction.A] = m_Program.Entities.at(instruction.B); break;
                    case GameplayOpcode::LoadOwner:
                        m_Registers[instruction.A] = owner; break;
                    case GameplayOpcode::Move:
                        m_Registers[instruction.A] = m_Registers.at(instruction.B); break;
                    case GameplayOpcode::AddFloat:
                        m_Registers[instruction.A] = Register<double>(instruction.B, "addition") +
                            Register<double>(instruction.C, "addition"); break;
                    case GameplayOpcode::SubtractFloat:
                        m_Registers[instruction.A] = Register<double>(instruction.B, "subtraction") -
                            Register<double>(instruction.C, "subtraction"); break;
                    case GameplayOpcode::MultiplyFloat:
                        m_Registers[instruction.A] = Register<double>(instruction.B, "multiplication") *
                            Register<double>(instruction.C, "multiplication"); break;
                    case GameplayOpcode::DivideFloat:
                    {
                        const double divisor = Register<double>(instruction.C, "division divisor");
                        if (divisor == 0.0)
                            throw std::runtime_error("Gameplay division by zero.");
                        m_Registers[instruction.A] =
                            Register<double>(instruction.B, "division dividend") / divisor;
                        break;
                    }
                    case GameplayOpcode::LessFloat:
                        m_Registers[instruction.A] = Register<double>(instruction.B, "comparison") <
                            Register<double>(instruction.C, "comparison"); break;
                    case GameplayOpcode::EqualFloat:
                        m_Registers[instruction.A] = Register<double>(instruction.B, "comparison") ==
                            Register<double>(instruction.C, "comparison"); break;
                    case GameplayOpcode::EqualEntity:
                        m_Registers[instruction.A] = Register<Entity>(instruction.B, "comparison") ==
                            Register<Entity>(instruction.C, "comparison"); break;
                    case GameplayOpcode::GetVariable:
                        m_Registers[instruction.A] = m_Variables.at(instruction.B); break;
                    case GameplayOpcode::SetVariable:
                    {
                        const GameplayValue& source = m_Registers.at(instruction.B);
                        GameplayValue& destination = m_Variables.at(instruction.A);
                        if (std::holds_alternative<std::monostate>(source) ||
                            source.index() != destination.index())
                            throw std::runtime_error("Gameplay variable assignment changes its declared type.");
                        destination = source;
                        break;
                    }
                    case GameplayOpcode::FindFirstWithTag:
                        m_Registers[instruction.A] =
                            host.FindFirstWithTag(m_Program.Strings.at(instruction.B)); break;
                    case GameplayOpcode::HasTag:
                        m_Registers[instruction.A] = host.HasTag(
                            Register<Entity>(instruction.B, "tag entity"),
                            m_Program.Strings.at(instruction.C)); break;
                    case GameplayOpcode::GetEntityPosition:
                        m_Registers[instruction.A] = host.GetEntityPosition(
                            Register<Entity>(instruction.B, "position entity")); break;
                    case GameplayOpcode::SetEntityPosition:
                        host.SetEntityPosition(Register<Entity>(instruction.A, "position entity"),
                            Register<kairo::foundation::math::Vec3d>(instruction.B, "position")); break;
                    case GameplayOpcode::SetEntityEnabled:
                        host.SetEntityEnabled(Register<Entity>(instruction.A, "enabled entity"),
                            Register<bool>(instruction.B, "enabled state")); break;
                    case GameplayOpcode::SpawnEntity:
                        m_Registers[instruction.A] = host.SpawnEntity(
                            m_Program.Strings.at(instruction.B),
                            Register<kairo::foundation::math::Vec3d>(instruction.C, "spawn position")); break;
                    case GameplayOpcode::DestroyEntity:
                        host.DestroyEntity(Register<Entity>(instruction.A, "destroy entity")); break;
                    case GameplayOpcode::AddTag:
                        host.AddTag(Register<Entity>(instruction.A, "tag entity"),
                            m_Program.Strings.at(instruction.B)); break;
                    case GameplayOpcode::RemoveTag:
                        host.RemoveTag(Register<Entity>(instruction.A, "tag entity"),
                            m_Program.Strings.at(instruction.B)); break;
                }
                if (pc >= m_Program.Instructions.size())
                    throw std::runtime_error("Gameplay execution reached the end without Halt.");
            }
        }
    };

    namespace gameplay_vm_detail
    {
        constexpr std::array<std::byte, 8u> Magic{
            std::byte{'K'}, std::byte{'G'}, std::byte{'V'}, std::byte{'M'},
            std::byte{1}, std::byte{0}, std::byte{0}, std::byte{0} };
        constexpr std::size_t MaximumArtifactBytes = 64u * 1024u * 1024u;

        inline void WriteString(kairo::assets::BinaryWriter& writer, std::string_view value)
        {
            if (value.size() > std::numeric_limits<std::uint32_t>::max())
                throw std::length_error("Gameplay string is too large to serialize.");
            writer.WriteU32(static_cast<std::uint32_t>(value.size()));
            writer.WriteText(value);
        }

        [[nodiscard]] inline std::string ReadString(kairo::assets::BinaryReader& reader)
        {
            const std::uint32_t size = reader.ReadU32();
            if (size > reader.Remaining())
                throw std::invalid_argument("Gameplay string is truncated.");
            return reader.ReadText(size);
        }

        inline void WriteValue(kairo::assets::BinaryWriter& writer,
            const GameplayValue& value)
        {
            writer.WriteU8(static_cast<std::uint8_t>(value.index()));
            if (const auto* boolean = std::get_if<bool>(&value))
                writer.WriteU8(*boolean ? 1u : 0u);
            else if (const auto* number = std::get_if<double>(&value))
                writer.WriteU64(std::bit_cast<std::uint64_t>(*number));
            else if (const auto* vector =
                std::get_if<kairo::foundation::math::Vec3d>(&value))
            {
                writer.WriteU64(std::bit_cast<std::uint64_t>(vector->x));
                writer.WriteU64(std::bit_cast<std::uint64_t>(vector->y));
                writer.WriteU64(std::bit_cast<std::uint64_t>(vector->z));
            }
            else if (const auto* entity = std::get_if<Entity>(&value))
                writer.WriteU32(entity->Value);
            else throw std::invalid_argument("Gameplay variable value cannot be empty.");
        }

        [[nodiscard]] inline GameplayValue ReadValue(kairo::assets::BinaryReader& reader)
        {
            switch (reader.ReadU8())
            {
                case 1u:
                {
                    const std::uint8_t value = reader.ReadU8();
                    if (value > 1u)
                        throw std::invalid_argument("Gameplay serialized boolean is invalid.");
                    return value != 0u;
                }
                case 2u: return std::bit_cast<double>(reader.ReadU64());
                case 3u:
                {
                    const double x = std::bit_cast<double>(reader.ReadU64());
                    const double y = std::bit_cast<double>(reader.ReadU64());
                    const double z = std::bit_cast<double>(reader.ReadU64());
                    return kairo::foundation::math::Vec3d{ x, y, z };
                }
                case 4u: return Entity{ reader.ReadU32() };
                default: throw std::invalid_argument("Gameplay serialized value type is invalid.");
            }
        }
    }

    [[nodiscard]] inline std::vector<std::byte> SerializeGameplayProgram(
        const GameplayProgram& program)
    {
        using namespace gameplay_vm_detail;
        program.Validate();
        kairo::assets::BinaryWriter writer;
        writer.WriteBytes(Magic);
        writer.WriteU32(program.RegisterCount);
        writer.WriteU32(static_cast<std::uint32_t>(program.Strings.size()));
        writer.WriteU32(static_cast<std::uint32_t>(program.Floats.size()));
        writer.WriteU32(static_cast<std::uint32_t>(program.Vectors.size()));
        writer.WriteU32(static_cast<std::uint32_t>(program.Entities.size()));
        writer.WriteU32(static_cast<std::uint32_t>(program.Variables.size()));
        writer.WriteU32(static_cast<std::uint32_t>(program.Instructions.size()));
        writer.WriteU32(static_cast<std::uint32_t>(program.Entries.size()));
        for (const std::string& value : program.Strings) WriteString(writer, value);
        for (double value : program.Floats)
            writer.WriteU64(std::bit_cast<std::uint64_t>(value));
        for (const auto& value : program.Vectors)
        {
            writer.WriteU64(std::bit_cast<std::uint64_t>(value.x));
            writer.WriteU64(std::bit_cast<std::uint64_t>(value.y));
            writer.WriteU64(std::bit_cast<std::uint64_t>(value.z));
        }
        for (Entity value : program.Entities) writer.WriteU32(value.Value);
        for (const GameplayVariableDefinition& variable : program.Variables)
        {
            WriteString(writer, variable.Name);
            WriteValue(writer, variable.DefaultValue);
        }
        for (const GameplayInstruction& instruction : program.Instructions)
        {
            writer.WriteU8(static_cast<std::uint8_t>(instruction.Opcode));
            writer.WriteU32(instruction.A);
            writer.WriteU32(instruction.B);
            writer.WriteU32(instruction.C);
            writer.WriteU32(instruction.D);
        }
        for (const GameplayEntryPoint& entry : program.Entries)
        {
            writer.WriteU8(static_cast<std::uint8_t>(entry.Event));
            WriteString(writer, entry.Action);
            writer.WriteU32(entry.InstructionOffset);
        }
        if (writer.Bytes().size() > MaximumArtifactBytes)
            throw std::length_error("Gameplay artifact exceeds the 64 MiB safety limit.");
        return std::move(writer).TakeBytes();
    }

    [[nodiscard]] inline GameplayProgram ParseGameplayProgram(
        std::span<const std::byte> bytes)
    {
        using namespace gameplay_vm_detail;
        if (bytes.size() > MaximumArtifactBytes)
            throw std::length_error("Gameplay artifact exceeds the 64 MiB safety limit.");
        kairo::assets::BinaryReader reader(bytes);
        const auto header = reader.ReadBytes(Magic.size());
        if (!std::equal(Magic.begin(), Magic.end(), header.begin()))
            throw std::invalid_argument("Gameplay artifact header is invalid.");

        GameplayProgram program;
        program.RegisterCount = reader.ReadU32();
        const std::uint32_t stringCount = reader.ReadU32();
        const std::uint32_t floatCount = reader.ReadU32();
        const std::uint32_t vectorCount = reader.ReadU32();
        const std::uint32_t entityCount = reader.ReadU32();
        const std::uint32_t variableCount = reader.ReadU32();
        const std::uint32_t instructionCount = reader.ReadU32();
        const std::uint32_t entryCount = reader.ReadU32();
        if (stringCount > GameplayProgram::MaximumConstants ||
            floatCount > GameplayProgram::MaximumConstants ||
            vectorCount > GameplayProgram::MaximumConstants ||
            entityCount > GameplayProgram::MaximumConstants ||
            variableCount > GameplayProgram::MaximumVariables ||
            instructionCount > GameplayProgram::MaximumInstructions ||
            entryCount > GameplayProgram::MaximumEntries)
            throw std::length_error("Gameplay artifact declares unsafe record counts.");

        program.Strings.reserve(stringCount);
        program.Floats.reserve(floatCount);
        program.Vectors.reserve(vectorCount);
        program.Entities.reserve(entityCount);
        program.Variables.reserve(variableCount);
        program.Instructions.reserve(instructionCount);
        program.Entries.reserve(entryCount);
        for (std::uint32_t index = 0u; index < stringCount; ++index)
            program.Strings.push_back(ReadString(reader));
        for (std::uint32_t index = 0u; index < floatCount; ++index)
            program.Floats.push_back(std::bit_cast<double>(reader.ReadU64()));
        for (std::uint32_t index = 0u; index < vectorCount; ++index)
        {
            const double x = std::bit_cast<double>(reader.ReadU64());
            const double y = std::bit_cast<double>(reader.ReadU64());
            const double z = std::bit_cast<double>(reader.ReadU64());
            program.Vectors.emplace_back(x, y, z);
        }
        for (std::uint32_t index = 0u; index < entityCount; ++index)
            program.Entities.push_back(Entity{ reader.ReadU32() });
        for (std::uint32_t index = 0u; index < variableCount; ++index)
        {
            GameplayVariableDefinition variable;
            variable.Name = ReadString(reader);
            variable.DefaultValue = ReadValue(reader);
            program.Variables.push_back(std::move(variable));
        }
        for (std::uint32_t index = 0u; index < instructionCount; ++index)
        {
            GameplayInstruction instruction;
            instruction.Opcode = static_cast<GameplayOpcode>(reader.ReadU8());
            instruction.A = reader.ReadU32();
            instruction.B = reader.ReadU32();
            instruction.C = reader.ReadU32();
            instruction.D = reader.ReadU32();
            program.Instructions.push_back(instruction);
        }
        for (std::uint32_t index = 0u; index < entryCount; ++index)
        {
            GameplayEntryPoint entry;
            entry.Event = static_cast<GameplayEventKind>(reader.ReadU8());
            entry.Action = ReadString(reader);
            entry.InstructionOffset = reader.ReadU32();
            program.Entries.push_back(std::move(entry));
        }
        reader.RequireEnd();
        program.Validate();
        return program;
    }
}
