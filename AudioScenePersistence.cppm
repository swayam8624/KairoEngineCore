module;

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module Kairo.EngineCore.AudioScenePersistence;

import Kairo.Assets;
import Kairo.EngineCore.AudioRuntime;
import Kairo.EngineCore.AudioSceneComponents;
import Kairo.EngineCore.Entity;
import Kairo.EngineCore.SaveGame;
import Kairo.EngineCore.Scene;

export namespace kairo::engine
{
    inline constexpr std::string_view AudioSceneSaveChunkName = "kairo.scene-audio";
    inline constexpr std::uint32_t AudioSceneSaveChunkSchema = 1u;

    namespace audio_scene_persistence_detail
    {
        constexpr std::string_view Magic = "KAUDIO01";
        constexpr std::size_t MaximumPayloadBytes = 64u * 1024u * 1024u;
        constexpr std::uint32_t MaximumRecords = 1'000'000u;

        inline void WriteString(kairo::assets::BinaryWriter& writer, std::string_view value)
        {
            if (value.size() > std::numeric_limits<std::uint32_t>::max())
                throw std::length_error("Audio-scene string exceeds its binary length field.");
            writer.WriteU32(static_cast<std::uint32_t>(value.size()));
            writer.WriteText(value);
        }

        [[nodiscard]] inline std::string ReadString(kairo::assets::BinaryReader& reader,
            std::size_t maximumBytes, std::string_view role)
        {
            const std::uint32_t bytes = reader.ReadU32();
            if (bytes == 0u || bytes > maximumBytes)
                throw std::length_error(std::string(role) + " exceeds its audio-scene byte limit.");
            return reader.ReadText(bytes);
        }

        inline void WriteF64(kairo::assets::BinaryWriter& writer, double value)
        {
            if (!std::isfinite(value))
                throw std::invalid_argument("Audio-scene floating-point value must be finite.");
            writer.WriteU64(std::bit_cast<std::uint64_t>(value));
        }

        [[nodiscard]] inline double ReadF64(kairo::assets::BinaryReader& reader)
        {
            const double value = std::bit_cast<double>(reader.ReadU64());
            if (!std::isfinite(value))
                throw std::invalid_argument("Audio-scene floating-point value must be finite.");
            return value;
        }

        [[nodiscard]] inline std::uint8_t BoolByte(bool value) noexcept
        {
            return value ? 1u : 0u;
        }

        [[nodiscard]] inline bool ReadBool(kairo::assets::BinaryReader& reader)
        {
            const std::uint8_t value = reader.ReadU8();
            if (value > 1u)
                throw std::invalid_argument("Audio-scene boolean field is invalid.");
            return value != 0u;
        }

        struct Record final
        {
            Entity Owner;
            bool HasEmitter = false;
            AudioEmitterComponent Emitter;
            bool HasListener = false;
            AudioListenerComponent Listener;
        };
    }

    [[nodiscard]] inline std::vector<std::byte> SerializeAudioSceneState(const Scene& scene)
    {
        using namespace audio_scene_persistence_detail;
        kairo::assets::BinaryWriter writer;
        writer.WriteText(Magic);
        writer.WriteU32(AudioSceneSaveChunkSchema);

        std::vector<Entity> owners;
        owners.reserve(scene.Size());
        for (const Entity entity : scene.Entities())
            if (scene.HasAudioEmitter(entity) || scene.HasAudioListener(entity))
                owners.push_back(entity);
        if (owners.size() > MaximumRecords)
            throw std::length_error("Audio-scene state exceeds its record limit.");
        writer.WriteU32(static_cast<std::uint32_t>(owners.size()));

        for (const Entity entity : owners)
        {
            writer.WriteU32(entity.Value);
            const bool hasEmitter = scene.HasAudioEmitter(entity);
            const bool hasListener = scene.HasAudioListener(entity);
            writer.WriteU8(static_cast<std::uint8_t>(
                (hasEmitter ? 0x01u : 0u) | (hasListener ? 0x02u : 0u)));

            if (hasEmitter)
            {
                const AudioEmitterComponent& emitter = scene.AudioEmitter(entity);
                emitter.Validate();
                WriteString(writer, emitter.Clip.ID.ToString());
                writer.WriteU8(BoolByte(emitter.Enabled));
                writer.WriteU8(BoolByte(emitter.PlayOnStart));
                writer.WriteU8(BoolByte(emitter.Loop));
                writer.WriteU8(BoolByte(emitter.Spatial));
                WriteF64(writer, emitter.Gain);
                WriteF64(writer, emitter.Pitch);
                writer.WriteU8(static_cast<std::uint8_t>(emitter.Attenuation.Model));
                WriteF64(writer, emitter.Attenuation.MinDistance);
                WriteF64(writer, emitter.Attenuation.MaxDistance);
                WriteF64(writer, emitter.Attenuation.Rolloff);
                WriteString(writer, emitter.Bus);
                writer.WriteU32(std::bit_cast<std::uint32_t>(emitter.Priority));
            }

            if (hasListener)
            {
                const AudioListenerComponent& listener = scene.AudioListenerComponentFor(entity);
                listener.Validate();
                writer.WriteU8(BoolByte(listener.Enabled));
                writer.WriteU8(BoolByte(listener.Primary));
                WriteF64(writer, listener.Gain);
            }
        }

        if (writer.Bytes().size() > MaximumPayloadBytes)
            throw std::length_error("Audio-scene state exceeds its 64 MiB payload limit.");
        return std::move(writer).TakeBytes();
    }

    inline void ApplyAudioSceneState(std::span<const std::byte> payload,
        Scene& scene, const kairo::assets::AssetRegistry& assets)
    {
        using namespace audio_scene_persistence_detail;
        if (payload.size() > MaximumPayloadBytes)
            throw std::length_error("Audio-scene state exceeds its 64 MiB payload limit.");

        kairo::assets::BinaryReader reader(payload);
        if (reader.ReadText(Magic.size()) != Magic)
            throw std::invalid_argument("Audio-scene payload magic is invalid.");
        if (reader.ReadU32() != AudioSceneSaveChunkSchema)
            throw std::invalid_argument("Audio-scene payload schema is unsupported.");
        const std::uint32_t count = reader.ReadU32();
        if (count > MaximumRecords)
            throw std::length_error("Audio-scene state exceeds its record limit.");

        std::vector<Record> records;
        records.reserve(count);
        std::uint32_t previousEntity = 0u;
        bool primaryListenerSeen = false;
        for (std::uint32_t index = 0u; index < count; ++index)
        {
            Record record;
            record.Owner = Entity{ reader.ReadU32() };
            if (!record.Owner || !scene.Contains(record.Owner))
                throw std::invalid_argument("Audio-scene state references an unknown entity.");
            if (record.Owner.Value <= previousEntity)
                throw std::invalid_argument("Audio-scene records must use unique ascending entity IDs.");
            previousEntity = record.Owner.Value;

            const std::uint8_t flags = reader.ReadU8();
            if (flags == 0u || (flags & ~0x03u) != 0u)
                throw std::invalid_argument("Audio-scene record flags are invalid.");
            record.HasEmitter = (flags & 0x01u) != 0u;
            record.HasListener = (flags & 0x02u) != 0u;

            if (record.HasEmitter)
            {
                record.Emitter.Clip = { kairo::assets::AssetID::Parse(
                    ReadString(reader, 36u, "Audio clip ID")) };
                (void)assets.Resolve(record.Emitter.Clip);
                record.Emitter.Enabled = ReadBool(reader);
                record.Emitter.PlayOnStart = ReadBool(reader);
                record.Emitter.Loop = ReadBool(reader);
                record.Emitter.Spatial = ReadBool(reader);
                record.Emitter.Gain = ReadF64(reader);
                record.Emitter.Pitch = ReadF64(reader);
                const std::uint8_t distanceModel = reader.ReadU8();
                if (distanceModel > static_cast<std::uint8_t>(AudioDistanceModel::Inverse))
                    throw std::invalid_argument("Audio-scene attenuation model is invalid.");
                record.Emitter.Attenuation.Model =
                    static_cast<AudioDistanceModel>(distanceModel);
                record.Emitter.Attenuation.MinDistance = ReadF64(reader);
                record.Emitter.Attenuation.MaxDistance = ReadF64(reader);
                record.Emitter.Attenuation.Rolloff = ReadF64(reader);
                record.Emitter.Bus = ReadString(reader, 128u, "Audio bus name");
                record.Emitter.Priority = std::bit_cast<std::int32_t>(reader.ReadU32());
                record.Emitter.Validate();
            }

            if (record.HasListener)
            {
                record.Listener.Enabled = ReadBool(reader);
                record.Listener.Primary = ReadBool(reader);
                record.Listener.Gain = ReadF64(reader);
                record.Listener.Validate();
                if (record.Listener.Primary)
                {
                    if (primaryListenerSeen)
                        throw std::invalid_argument("Audio-scene state contains multiple primary listeners.");
                    primaryListenerSeen = true;
                }
            }
            records.push_back(std::move(record));
        }
        reader.RequireEnd();

        // Transaction boundary: every record, asset reference, and component has
        // been validated above. Only now replace the Scene-owned audio state.
        for (const Entity entity : scene.Entities())
        {
            (void)scene.RemoveAudioEmitter(entity);
            (void)scene.RemoveAudioListener(entity);
        }
        for (const Record& record : records)
        {
            if (record.HasEmitter) scene.SetAudioEmitter(record.Owner, record.Emitter);
            if (record.HasListener) scene.SetAudioListener(record.Owner, record.Listener);
        }
    }

    [[nodiscard]] inline SaveGameChunk MakeAudioSceneSaveChunk(const Scene& scene)
    {
        return { std::string(AudioSceneSaveChunkName), AudioSceneSaveChunkSchema,
            SerializeAudioSceneState(scene) };
    }

    inline void ApplyAudioSceneSaveChunk(const SaveGameChunk& chunk,
        Scene& scene, const kairo::assets::AssetRegistry& assets)
    {
        if (chunk.Name != AudioSceneSaveChunkName ||
            chunk.SchemaVersion != AudioSceneSaveChunkSchema)
            throw std::invalid_argument("Save-game chunk is not a supported Kairo audio-scene snapshot.");
        ApplyAudioSceneState(chunk.Payload, scene, assets);
    }
}
