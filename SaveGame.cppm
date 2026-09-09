module;

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

export module Kairo.EngineCore.SaveGame;

import Kairo.Assets;
import Kairo.EngineCore.Entity;
import Kairo.EngineCore.LogicState;
import Kairo.EngineCore.Scene;
import Kairo.EngineCore.SceneSerialization;
import Kairo.EngineCore.TextValidation;
import Kairo.Foundation.Math.Vector;

export namespace kairo::engine
{
    namespace save_game_detail
    {
        constexpr std::array<std::byte, 8u> Magic{
            std::byte{'K'}, std::byte{'S'}, std::byte{'A'}, std::byte{'V'},
            std::byte{'E'}, std::byte{'0'}, std::byte{'0'}, std::byte{'1'} };
        constexpr std::uint32_t ArchiveVersion = 1u;
        constexpr std::size_t MaximumArchiveBytes = 512u * 1024u * 1024u;
        constexpr std::size_t MaximumChunkBytes = 256u * 1024u * 1024u;
        constexpr std::size_t MaximumChunks = 256u;
        constexpr std::size_t MaximumMetadataBytes = 4096u;
        constexpr std::size_t MaximumChunkNameBytes = 128u;
        constexpr std::size_t MaximumLogicStringBytes = 64u * 1024u;

        [[nodiscard]] inline std::uint32_t Crc32(std::span<const std::byte> bytes) noexcept
        {
            std::uint32_t crc = 0xffff'ffffu;
            for (const std::byte byte : bytes)
            {
                crc ^= std::to_integer<std::uint8_t>(byte);
                for (unsigned bit = 0u; bit < 8u; ++bit)
                {
                    const std::uint32_t mask =
                        0u - static_cast<std::uint32_t>(crc & 1u);
                    crc = (crc >> 1u) ^ (0xedb8'8320u & mask);
                }
            }
            return ~crc;
        }

        inline void ValidateMetadata(std::string_view value,
            std::string_view role, bool allowEmpty = false)
        {
            ValidateUtf8Text(value,
                { allowEmpty ? 0u : 1u, MaximumMetadataBytes, allowEmpty, false },
                role);
        }

        inline void ValidateChunkName(std::string_view value)
        {
            ValidateUtf8Text(value,
                { 1u, MaximumChunkNameBytes, false, false },
                "Save-game chunk name");
            for (const unsigned char character : value)
            {
                const bool allowed =
                    (character >= 'a' && character <= 'z') ||
                    (character >= 'A' && character <= 'Z') ||
                    (character >= '0' && character <= '9') ||
                    character == '.' || character == '-' || character == '_';
                if (!allowed)
                    throw std::invalid_argument(
                        "Save-game chunk names use only ASCII letters, digits, '.', '-', and '_'.");
            }
        }

        inline void WriteString(kairo::assets::BinaryWriter& writer,
            std::string_view value)
        {
            if (value.size() > std::numeric_limits<std::uint32_t>::max())
                throw std::length_error("Save-game text exceeds its binary length field.");
            writer.WriteU32(static_cast<std::uint32_t>(value.size()));
            writer.WriteText(value);
        }

        [[nodiscard]] inline std::string ReadString(
            kairo::assets::BinaryReader& reader, std::size_t maximumBytes,
            std::string_view role, bool allowEmpty = false)
        {
            const std::uint32_t bytes = reader.ReadU32();
            if (bytes > maximumBytes || (!allowEmpty && bytes == 0u))
                throw std::length_error(std::string(role) +
                    " exceeds its save-game byte limit.");
            return reader.ReadText(bytes);
        }

        inline void WriteF64(kairo::assets::BinaryWriter& writer, double value)
        {
            static_assert(sizeof(double) == sizeof(std::uint64_t));
            static_assert(std::numeric_limits<double>::is_iec559);
            if (!std::isfinite(value))
                throw std::invalid_argument("Save-game floating-point values must be finite.");
            writer.WriteU64(std::bit_cast<std::uint64_t>(value));
        }

        [[nodiscard]] inline double ReadF64(kairo::assets::BinaryReader& reader)
        {
            static_assert(sizeof(double) == sizeof(std::uint64_t));
            const double value = std::bit_cast<double>(reader.ReadU64());
            if (!std::isfinite(value))
                throw std::invalid_argument(
                    "Save-game contains a non-finite floating-point value.");
            return value;
        }
    }

    struct SaveGameChunk final
    {
        std::string Name;
        std::uint32_t SchemaVersion = 1u;
        std::vector<std::byte> Payload;

        friend bool operator==(const SaveGameChunk&, const SaveGameChunk&) = default;
    };

    /// Versioned, deterministic container for game-owned persistent state.
    /// EngineCore owns only the archive contract. Physics, AI, vehicles, quests,
    /// inventory, and future plugins each own their chunk payload/schema.
    class SaveGameArchive final
    {
    public:
        std::string ProjectName;
        std::string EngineVersion;
        std::string Label;
        std::uint64_t Sequence = 0u;

        void SetChunk(SaveGameChunk chunk)
        {
            ValidateChunk(chunk);
            if (!m_Chunks.contains(chunk.Name) && m_Chunks.size() >=
                save_game_detail::MaximumChunks)
                throw std::length_error("Save-game archive exceeds its chunk limit.");
            m_Chunks.insert_or_assign(chunk.Name, std::move(chunk));
        }

        bool RemoveChunk(std::string_view name)
        {
            return m_Chunks.erase(std::string(name)) != 0u;
        }

        [[nodiscard]] bool ContainsChunk(std::string_view name) const
        {
            return m_Chunks.contains(name);
        }

        [[nodiscard]] const SaveGameChunk& Chunk(std::string_view name) const
        {
            const auto found = m_Chunks.find(name);
            if (found == m_Chunks.end())
                throw std::out_of_range("Save-game chunk does not exist.");
            return found->second;
        }

        [[nodiscard]] std::vector<std::string> ChunkNames() const
        {
            std::vector<std::string> result;
            result.reserve(m_Chunks.size());
            for (const auto& [name, chunk] : m_Chunks)
                result.push_back(name);
            return result;
        }

        [[nodiscard]] std::size_t ChunkCount() const noexcept
        {
            return m_Chunks.size();
        }

        void Validate() const
        {
            using namespace save_game_detail;
            ValidateMetadata(ProjectName, "Save-game project name");
            ValidateMetadata(EngineVersion, "Save-game engine version");
            ValidateMetadata(Label, "Save-game label", true);
            if (m_Chunks.size() > MaximumChunks)
                throw std::length_error("Save-game archive exceeds its chunk limit.");
            std::size_t payloadBytes = 0u;
            for (const auto& [name, chunk] : m_Chunks)
            {
                if (name != chunk.Name)
                    throw std::invalid_argument(
                        "Save-game chunk index and embedded name disagree.");
                ValidateChunk(chunk);
                if (chunk.Payload.size() > MaximumArchiveBytes - payloadBytes)
                    throw std::length_error(
                        "Save-game payload exceeds its archive byte limit.");
                payloadBytes += chunk.Payload.size();
            }
        }

    private:
        std::map<std::string, SaveGameChunk, std::less<>> m_Chunks;

        static void ValidateChunk(const SaveGameChunk& chunk)
        {
            save_game_detail::ValidateChunkName(chunk.Name);
            if (chunk.SchemaVersion == 0u)
                throw std::invalid_argument(
                    "Save-game chunk schema version must be positive.");
            if (chunk.Payload.size() > save_game_detail::MaximumChunkBytes)
                throw std::length_error(
                    "Save-game chunk exceeds its payload byte limit.");
        }
    };

    [[nodiscard]] inline std::vector<std::byte> SerializeSaveGame(
        const SaveGameArchive& archive)
    {
        using namespace save_game_detail;
        archive.Validate();
        kairo::assets::BinaryWriter writer;
        writer.WriteBytes(Magic);
        writer.WriteU32(ArchiveVersion);
        WriteString(writer, archive.ProjectName);
        WriteString(writer, archive.EngineVersion);
        WriteString(writer, archive.Label);
        writer.WriteU64(archive.Sequence);
        writer.WriteU32(static_cast<std::uint32_t>(archive.ChunkCount()));
        for (const std::string& name : archive.ChunkNames())
        {
            const SaveGameChunk& chunk = archive.Chunk(name);
            WriteString(writer, chunk.Name);
            writer.WriteU32(chunk.SchemaVersion);
            writer.WriteU64(static_cast<std::uint64_t>(chunk.Payload.size()));
            writer.WriteU32(Crc32(chunk.Payload));
            writer.WriteBytes(chunk.Payload);
            if (writer.Bytes().size() > MaximumArchiveBytes)
                throw std::length_error(
                    "Serialized save-game exceeds its archive byte limit.");
        }
        return std::move(writer).TakeBytes();
    }

    [[nodiscard]] inline SaveGameArchive ParseSaveGame(
        std::span<const std::byte> bytes)
    {
        using namespace save_game_detail;
        if (bytes.size() > MaximumArchiveBytes)
            throw std::length_error("Save-game exceeds its archive byte limit.");
        kairo::assets::BinaryReader reader(bytes);
        if (!std::ranges::equal(reader.ReadBytes(Magic.size()), Magic))
            throw std::invalid_argument("Save-game magic is invalid.");
        if (reader.ReadU32() != ArchiveVersion)
            throw std::invalid_argument("Save-game archive version is unsupported.");

        SaveGameArchive archive;
        archive.ProjectName = ReadString(reader, MaximumMetadataBytes,
            "Save-game project name");
        archive.EngineVersion = ReadString(reader, MaximumMetadataBytes,
            "Save-game engine version");
        archive.Label = ReadString(reader, MaximumMetadataBytes,
            "Save-game label", true);
        archive.Sequence = reader.ReadU64();
        const std::uint32_t chunkCount = reader.ReadU32();
        if (chunkCount > MaximumChunks)
            throw std::length_error("Save-game chunk count exceeds its limit.");
        for (std::uint32_t index = 0u; index < chunkCount; ++index)
        {
            SaveGameChunk chunk;
            chunk.Name = ReadString(reader, MaximumChunkNameBytes,
                "Save-game chunk name");
            chunk.SchemaVersion = reader.ReadU32();
            const std::uint64_t payloadBytes = reader.ReadU64();
            const std::uint32_t expectedCrc = reader.ReadU32();
            if (payloadBytes > MaximumChunkBytes || payloadBytes > reader.Remaining())
                throw std::length_error(
                    "Save-game chunk payload length exceeds its limit or remaining input.");
            const auto payload = reader.ReadBytes(static_cast<std::size_t>(payloadBytes));
            if (Crc32(payload) != expectedCrc)
                throw std::invalid_argument(
                    "Save-game chunk checksum does not match its payload.");
            chunk.Payload.assign(payload.begin(), payload.end());
            if (archive.ContainsChunk(chunk.Name))
                throw std::invalid_argument("Save-game contains a duplicate chunk name.");
            archive.SetChunk(std::move(chunk));
        }
        reader.RequireEnd();
        archive.Validate();
        return archive;
    }

    inline void SaveGameToFile(const std::filesystem::path& path,
        const SaveGameArchive& archive)
    {
        if (path.empty())
            throw std::invalid_argument("Save-game path cannot be empty.");
        const auto bytes = SerializeSaveGame(archive);
        const std::filesystem::path parent = path.has_parent_path()
            ? path.parent_path() : std::filesystem::path(".");
        std::error_code error;
        std::filesystem::create_directories(parent, error);
        if (error)
            throw std::runtime_error(
                "Cannot create save-game directory: " + error.message());
        const std::filesystem::path temporary = path.string() + ".tmp-" +
            kairo::assets::GenerateAssetID().ToString();
        try
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output)
                throw std::runtime_error(
                    "Cannot open temporary save-game file for writing.");
            if (!bytes.empty())
                output.write(reinterpret_cast<const char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
            output.flush();
            if (!output)
                throw std::runtime_error(
                    "Cannot write complete temporary save-game file.");
            output.close();
            kairo::assets::ReplaceFileAtomically(temporary, path);
        }
        catch (...)
        {
            std::filesystem::remove(temporary, error);
            throw;
        }
    }

    [[nodiscard]] inline SaveGameArchive LoadGameFromFile(
        const std::filesystem::path& path)
    {
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        if (error)
            throw std::runtime_error(
                "Cannot inspect save-game file: " + error.message());
        if (size > save_game_detail::MaximumArchiveBytes)
            throw std::length_error("Save-game file exceeds its archive byte limit.");
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input)
            throw std::runtime_error("Cannot open save-game file for reading.");
        const auto end = input.tellg();
        if (end < 0)
            throw std::runtime_error("Cannot measure save-game file.");
        std::vector<std::byte> bytes(static_cast<std::size_t>(end));
        input.seekg(0);
        if (!bytes.empty() && !input.read(
            reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size())))
            throw std::runtime_error("Cannot read complete save-game file.");
        return ParseSaveGame(bytes);
    }

    inline constexpr std::string_view SceneSaveChunkName = "kairo.scene";
    inline constexpr std::uint32_t SceneSaveChunkSchema = 4u;
    inline constexpr std::string_view LogicStateSaveChunkName = "kairo.logic-state";
    inline constexpr std::uint32_t LogicStateSaveChunkSchema = 1u;

    [[nodiscard]] inline SaveGameChunk MakeSceneSaveChunk(
        const Scene& scene, const kairo::assets::AssetRegistry& assets)
    {
        const std::string text = SerializeScene(scene, assets);
        std::vector<std::byte> payload(text.size());
        for (std::size_t index = 0u; index < text.size(); ++index)
            payload[index] = std::byte{ static_cast<unsigned char>(text[index]) };
        return { std::string(SceneSaveChunkName), SceneSaveChunkSchema,
            std::move(payload) };
    }

    [[nodiscard]] inline Scene ParseSceneSaveChunk(const SaveGameChunk& chunk,
        const kairo::assets::AssetRegistry& assets)
    {
        if (chunk.Name != SceneSaveChunkName ||
            chunk.SchemaVersion != SceneSaveChunkSchema)
            throw std::invalid_argument(
                "Save-game chunk is not a supported Kairo scene snapshot.");
        std::string text(chunk.Payload.size(), '\0');
        for (std::size_t index = 0u; index < chunk.Payload.size(); ++index)
            text[index] = static_cast<char>(
                std::to_integer<unsigned char>(chunk.Payload[index]));
        return ParseScene(text, assets);
    }

    [[nodiscard]] inline std::vector<std::byte> SerializeLogicStateSnapshot(
        const LogicStateSnapshot& snapshot)
    {
        using namespace save_game_detail;
        LogicState validator;
        validator.Restore(snapshot);
        const LogicStateSnapshot canonical = validator.Snapshot();
        kairo::assets::BinaryWriter writer;
        writer.WriteU32(static_cast<std::uint32_t>(canonical.Variables.size()));
        for (const LogicStateEntry& entry : canonical.Variables)
        {
            WriteString(writer, entry.Name);
            writer.WriteU8(static_cast<std::uint8_t>(entry.Value.index()));
            switch (entry.Value.index())
            {
                case 0u:
                    writer.WriteU8(std::get<bool>(entry.Value) ? 1u : 0u);
                    break;
                case 1u:
                    WriteF64(writer, std::get<double>(entry.Value));
                    break;
                case 2u:
                {
                    const auto& value =
                        std::get<kairo::foundation::math::Vec3d>(entry.Value);
                    WriteF64(writer, value.x);
                    WriteF64(writer, value.y);
                    WriteF64(writer, value.z);
                    break;
                }
                case 3u:
                    writer.WriteU32(std::get<Entity>(entry.Value).Value);
                    break;
                case 4u:
                {
                    const std::string& value = std::get<std::string>(entry.Value);
                    if (value.size() > MaximumLogicStringBytes)
                        throw std::length_error(
                            "Logic string exceeds save-game byte limit.");
                    WriteString(writer, value);
                    break;
                }
                default:
                    throw std::invalid_argument(
                        "Logic state contains an unsupported value type.");
            }
        }
        writer.WriteU32(static_cast<std::uint32_t>(canonical.Timers.size()));
        for (const LogicTimer& timer : canonical.Timers)
        {
            WriteString(writer, timer.Name);
            WriteF64(writer, timer.RemainingSeconds);
            WriteF64(writer, timer.RepeatSeconds);
        }
        if (writer.Bytes().size() > MaximumChunkBytes)
            throw std::length_error("Logic state save chunk exceeds its byte limit.");
        return std::move(writer).TakeBytes();
    }

    [[nodiscard]] inline LogicStateSnapshot ParseLogicStateSnapshot(
        std::span<const std::byte> payload)
    {
        using namespace save_game_detail;
        if (payload.size() > MaximumChunkBytes)
            throw std::length_error("Logic state save chunk exceeds its byte limit.");
        kairo::assets::BinaryReader reader(payload);
        LogicStateSnapshot snapshot;
        const std::uint32_t variableCount = reader.ReadU32();
        if (variableCount > LogicState::MaximumVariables)
            throw std::length_error(
                "Logic state save variable count exceeds runtime limit.");
        snapshot.Variables.reserve(variableCount);
        for (std::uint32_t index = 0u; index < variableCount; ++index)
        {
            LogicStateEntry entry;
            entry.Name = ReadString(reader, 128u, "Logic variable name");
            const std::uint8_t tag = reader.ReadU8();
            switch (tag)
            {
                case 0u:
                {
                    const std::uint8_t value = reader.ReadU8();
                    if (value > 1u)
                        throw std::invalid_argument(
                            "Logic boolean save value is invalid.");
                    entry.Value = value != 0u;
                    break;
                }
                case 1u:
                    entry.Value = ReadF64(reader);
                    break;
                case 2u:
                {
                    // BinaryReader advances mutable state on every read. Keep
                    // component consumption explicitly sequenced instead of
                    // depending on compiler argument-evaluation order.
                    const double x = ReadF64(reader);
                    const double y = ReadF64(reader);
                    const double z = ReadF64(reader);
                    entry.Value = kairo::foundation::math::Vec3d{ x, y, z };
                    break;
                }
                case 3u:
                {
                    const Entity value{ reader.ReadU32() };
                    if (!value)
                        throw std::invalid_argument(
                            "Logic entity save value cannot be invalid.");
                    entry.Value = value;
                    break;
                }
                case 4u:
                    entry.Value = ReadString(reader, MaximumLogicStringBytes,
                        "Logic string", true);
                    break;
                default:
                    throw std::invalid_argument(
                        "Logic state save contains an unknown value tag.");
            }
            snapshot.Variables.push_back(std::move(entry));
        }
        const std::uint32_t timerCount = reader.ReadU32();
        if (timerCount > LogicState::MaximumTimers)
            throw std::length_error(
                "Logic state save timer count exceeds runtime limit.");
        snapshot.Timers.reserve(timerCount);
        for (std::uint32_t index = 0u; index < timerCount; ++index)
        {
            LogicTimer timer;
            timer.Name = ReadString(reader, 128u, "Logic timer name");
            timer.RemainingSeconds = ReadF64(reader);
            timer.RepeatSeconds = ReadF64(reader);
            snapshot.Timers.push_back(std::move(timer));
        }
        reader.RequireEnd();
        LogicState validator;
        validator.Restore(snapshot);
        return validator.Snapshot();
    }

    [[nodiscard]] inline SaveGameChunk MakeLogicStateSaveChunk(
        const LogicState& state)
    {
        return { std::string(LogicStateSaveChunkName), LogicStateSaveChunkSchema,
            SerializeLogicStateSnapshot(state.Snapshot()) };
    }

    [[nodiscard]] inline LogicState ParseLogicStateSaveChunk(
        const SaveGameChunk& chunk)
    {
        if (chunk.Name != LogicStateSaveChunkName ||
            chunk.SchemaVersion != LogicStateSaveChunkSchema)
            throw std::invalid_argument(
                "Save-game chunk is not a supported Kairo logic-state snapshot.");
        LogicState state;
        state.Restore(ParseLogicStateSnapshot(chunk.Payload));
        return state;
    }
}
