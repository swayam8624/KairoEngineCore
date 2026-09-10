module;

#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

export module Kairo.EngineCore.SceneSerializationV5;

import Kairo.Assets;
import Kairo.EngineCore.AudioRuntime;
import Kairo.EngineCore.AudioSceneComponents;
import Kairo.EngineCore.Entity;
import Kairo.EngineCore.Scene;
import Kairo.EngineCore.SceneSerialization;

export namespace kairo::engine
{
    namespace scene_v5_detail
    {
        constexpr std::size_t MaxSceneBytes = 64u * 1024u * 1024u;

        struct Token final
        {
            std::string Text;
            std::size_t Column = 1u;
        };

        [[nodiscard]] inline std::vector<Token> Tokenize(
            std::string_view line, std::size_t lineNumber)
        {
            std::vector<Token> tokens;
            std::size_t index = 0u;
            while (index < line.size())
            {
                while (index < line.size() &&
                    (line[index] == ' ' || line[index] == '\t' || line[index] == '\r'))
                    ++index;
                if (index == line.size() || line[index] == '#') break;

                Token token;
                token.Column = index + 1u;
                if (line[index] != '"')
                {
                    while (index < line.size() && line[index] != ' ' &&
                        line[index] != '\t' && line[index] != '\r' &&
                        line[index] != '#')
                        token.Text.push_back(line[index++]);
                }
                else
                {
                    ++index;
                    bool closed = false;
                    while (index < line.size())
                    {
                        const char character = line[index++];
                        if (character == '"')
                        {
                            closed = true;
                            break;
                        }
                        if (character != '\\')
                        {
                            token.Text.push_back(character);
                            continue;
                        }
                        if (index == line.size())
                            throw SceneFormatError(
                                lineNumber, index, "unfinished escape sequence");
                        const char escaped = line[index++];
                        switch (escaped)
                        {
                            case '\\': token.Text.push_back('\\'); break;
                            case '"': token.Text.push_back('"'); break;
                            case 'n': token.Text.push_back('\n'); break;
                            case 't': token.Text.push_back('\t'); break;
                            default:
                                throw SceneFormatError(
                                    lineNumber, index, "unknown quoted-string escape");
                        }
                    }
                    if (!closed)
                        throw SceneFormatError(
                            lineNumber, token.Column, "unterminated quoted string");
                    if (index < line.size() && line[index] != ' ' &&
                        line[index] != '\t' && line[index] != '\r' &&
                        line[index] != '#')
                        throw SceneFormatError(lineNumber, index + 1u,
                            "quoted token must be followed by whitespace");
                }
                tokens.push_back(std::move(token));
            }
            return tokens;
        }

        inline void RequireCount(const std::vector<Token>& tokens,
            std::size_t expected, std::size_t line, std::string_view statement)
        {
            if (tokens.size() == expected) return;
            const std::size_t column = tokens.size() > expected
                ? tokens[expected].Column : 1u;
            throw SceneFormatError(line, column,
                std::string(statement) + " expects " +
                std::to_string(expected - 1u) + " argument(s)");
        }

        [[nodiscard]] inline bool ParseBool(
            const Token& token, std::size_t line)
        {
            if (token.Text == "true") return true;
            if (token.Text == "false") return false;
            throw SceneFormatError(
                line, token.Column, "boolean value must be true or false");
        }

        template <class Integer>
        [[nodiscard]] inline Integer ParseInteger(
            const Token& token, std::size_t line, std::string_view field)
        {
            Integer result{};
            const auto [end, error] = std::from_chars(
                token.Text.data(), token.Text.data() + token.Text.size(), result);
            if (error != std::errc{} ||
                end != token.Text.data() + token.Text.size())
                throw SceneFormatError(line, token.Column,
                    std::string(field) + " is not a valid integer");
            return result;
        }

        [[nodiscard]] inline double ParseDouble(
            const Token& token, std::size_t line, std::string_view field)
        {
            std::istringstream stream(token.Text);
            stream.imbue(std::locale::classic());
            double result = 0.0;
            stream >> result;
            if (stream.fail() || !std::isfinite(result))
                throw SceneFormatError(line, token.Column,
                    std::string(field) + " must be finite");
            stream >> std::ws;
            if (!stream.eof())
                throw SceneFormatError(line, token.Column,
                    std::string(field) + " must be finite");
            return result;
        }

        [[nodiscard]] inline kairo::assets::AssetID ParseAssetID(
            const Token& token, std::size_t line)
        {
            try { return kairo::assets::AssetID::Parse(token.Text); }
            catch (const std::exception& error)
            {
                throw SceneFormatError(line, token.Column, error.what());
            }
        }

        [[nodiscard]] inline AudioDistanceModel ParseDistanceModel(
            const Token& token, std::size_t line)
        {
            if (token.Text == "linear") return AudioDistanceModel::Linear;
            if (token.Text == "inverse") return AudioDistanceModel::Inverse;
            throw SceneFormatError(line, token.Column,
                "audio distance model must be linear or inverse");
        }

        [[nodiscard]] inline std::string_view DistanceModelName(
            AudioDistanceModel model)
        {
            switch (model)
            {
                case AudioDistanceModel::Linear: return "linear";
                case AudioDistanceModel::Inverse: return "inverse";
            }
            throw std::invalid_argument("Audio distance model enum is invalid.");
        }

        [[nodiscard]] inline std::string Quote(std::string_view value)
        {
            std::string result = "\"";
            for (const char character : value)
            {
                switch (character)
                {
                    case '\\': result += "\\\\"; break;
                    case '"': result += "\\\""; break;
                    case '\n': result += "\\n"; break;
                    case '\t': result += "\\t"; break;
                    default: result.push_back(character); break;
                }
            }
            result.push_back('"');
            return result;
        }

        struct PendingEmitter final
        {
            Entity EntityID{};
            AudioEmitterComponent Component;
            std::size_t Line = 1u;
            std::size_t ClipColumn = 1u;
        };

        struct PendingListener final
        {
            Entity EntityID{};
            AudioListenerComponent Component;
            std::size_t Line = 1u;
            std::size_t Column = 1u;
        };

        [[nodiscard]] inline AudioEmitterComponent ParseEmitter(
            const std::vector<Token>& tokens,
            std::size_t line,
            const kairo::assets::AssetRegistry& assets)
        {
            RequireCount(tokens, 14u, line, "audio-emitter");
            AudioEmitterComponent component;
            component.Clip = { ParseAssetID(tokens[1], line) };
            component.Enabled = ParseBool(tokens[2], line);
            component.PlayOnStart = ParseBool(tokens[3], line);
            component.Loop = ParseBool(tokens[4], line);
            component.Spatial = ParseBool(tokens[5], line);
            component.Gain = ParseDouble(tokens[6], line, "audio gain");
            component.Pitch = ParseDouble(tokens[7], line, "audio pitch");
            component.Attenuation.Model = ParseDistanceModel(tokens[8], line);
            component.Attenuation.MinDistance =
                ParseDouble(tokens[9], line, "audio minimum distance");
            component.Attenuation.MaxDistance =
                ParseDouble(tokens[10], line, "audio maximum distance");
            component.Attenuation.Rolloff =
                ParseDouble(tokens[11], line, "audio rolloff");
            component.Bus = tokens[12].Text;
            component.Priority =
                ParseInteger<std::int32_t>(tokens[13], line, "audio priority");
            try
            {
                component.Validate();
                (void)assets.Resolve(component.Clip);
            }
            catch (const std::exception& error)
            {
                throw SceneFormatError(line, tokens[1].Column, error.what());
            }
            return component;
        }

        [[nodiscard]] inline AudioListenerComponent ParseListener(
            const std::vector<Token>& tokens, std::size_t line)
        {
            RequireCount(tokens, 3u, line, "audio-listener");
            AudioListenerComponent component;
            component.Enabled = ParseBool(tokens[1], line);
            component.Primary = ParseBool(tokens[2], line);
            component.Validate();
            return component;
        }
    }

    [[nodiscard]] inline Scene ParseSceneV5(
        std::string_view source,
        const kairo::assets::AssetRegistry& assets)
    {
        using namespace scene_v5_detail;
        if (source.size() > MaxSceneBytes)
            throw std::length_error("Kairo scene exceeds the 64 MiB safety limit.");

        std::istringstream input{ std::string(source) };
        std::ostringstream legacy;
        std::string lineText;
        std::size_t lineNumber = 0u;
        bool headerSeen = false;
        bool versionFive = false;
        std::optional<Entity> current;
        std::unordered_set<std::uint32_t> emitterSeen;
        std::unordered_set<std::uint32_t> listenerSeen;
        std::vector<PendingEmitter> emitters;
        std::vector<PendingListener> listeners;

        while (std::getline(input, lineText))
        {
            ++lineNumber;
            const auto tokens = Tokenize(lineText, lineNumber);
            if (!headerSeen && !tokens.empty())
            {
                RequireCount(tokens, 2u, lineNumber, "kairo-scene header");
                if (tokens[0].Text != "kairo-scene")
                    throw SceneFormatError(lineNumber, tokens[0].Column,
                        "expected kairo-scene header");
                if (tokens[1].Text == "5")
                {
                    versionFive = true;
                    legacy << "kairo-scene 4\n";
                }
                else
                {
                    return ParseScene(source, assets);
                }
                headerSeen = true;
                continue;
            }

            if (!versionFive)
            {
                legacy << lineText << '\n';
                continue;
            }

            if (tokens.empty())
            {
                legacy << lineText << '\n';
                continue;
            }
            if (tokens[0].Text == "entity")
            {
                RequireCount(tokens, 3u, lineNumber, "entity");
                current = Entity{
                    ParseInteger<std::uint32_t>(
                        tokens[1], lineNumber, "entity ID")
                };
                legacy << lineText << '\n';
                continue;
            }
            if (tokens[0].Text == "end")
            {
                current.reset();
                legacy << lineText << '\n';
                continue;
            }
            if (tokens[0].Text == "audio-emitter")
            {
                if (!current.has_value())
                    throw SceneFormatError(lineNumber, tokens[0].Column,
                        "audio-emitter outside entity record");
                if (!emitterSeen.emplace(current->Value).second)
                    throw SceneFormatError(lineNumber, tokens[0].Column,
                        "duplicate audio emitter component");
                emitters.push_back({
                    *current,
                    ParseEmitter(tokens, lineNumber, assets),
                    lineNumber,
                    tokens.size() > 1u ? tokens[1].Column : tokens[0].Column
                });
                legacy << '\n';
                continue;
            }
            if (tokens[0].Text == "audio-listener")
            {
                if (!current.has_value())
                    throw SceneFormatError(lineNumber, tokens[0].Column,
                        "audio-listener outside entity record");
                if (!listenerSeen.emplace(current->Value).second)
                    throw SceneFormatError(lineNumber, tokens[0].Column,
                        "duplicate audio listener component");
                listeners.push_back({
                    *current,
                    ParseListener(tokens, lineNumber),
                    lineNumber,
                    tokens[0].Column
                });
                legacy << '\n';
                continue;
            }
            legacy << lineText << '\n';
        }

        if (!headerSeen)
            throw SceneFormatError(1u, 1u, "missing kairo-scene header");

        Scene scene = ParseScene(legacy.str(), assets);
        for (const auto& pending : emitters)
        {
            if (!scene.Contains(pending.EntityID))
                throw SceneFormatError(pending.Line, pending.ClipColumn,
                    "audio emitter entity ID does not exist");
            try { scene.SetAudioEmitter(pending.EntityID, pending.Component); }
            catch (const std::exception& error)
            {
                throw SceneFormatError(
                    pending.Line, pending.ClipColumn, error.what());
            }
        }
        for (const auto& pending : listeners)
        {
            if (!scene.Contains(pending.EntityID))
                throw SceneFormatError(pending.Line, pending.Column,
                    "audio listener entity ID does not exist");
            try { scene.SetAudioListener(pending.EntityID, pending.Component); }
            catch (const std::exception& error)
            {
                throw SceneFormatError(
                    pending.Line, pending.Column, error.what());
            }
        }
        return scene;
    }

    [[nodiscard]] inline std::string SerializeSceneV5(
        const Scene& scene,
        const kairo::assets::AssetRegistry& assets)
    {
        using namespace scene_v5_detail;
        const std::string legacySource = SerializeScene(scene, assets);
        std::istringstream input(legacySource);
        std::ostringstream output;
        output.imbue(std::locale::classic());
        output << std::setprecision(std::numeric_limits<double>::max_digits10);

        std::string lineText;
        bool header = true;
        std::size_t entityIndex = 0u;
        const auto entities = scene.Entities();
        while (std::getline(input, lineText))
        {
            if (header)
            {
                if (lineText != "kairo-scene 4")
                    throw std::logic_error(
                        "SceneSerialization v5 expected a v4 delegate document.");
                output << "kairo-scene 5\n";
                header = false;
                continue;
            }

            if (lineText == "end")
            {
                if (entityIndex >= entities.size())
                    throw std::logic_error(
                        "SceneSerialization v5 delegate emitted too many entity ends.");
                const Entity entity = entities[entityIndex++];
                if (scene.HasAudioEmitter(entity))
                {
                    const auto& emitter = scene.AudioEmitter(entity);
                    emitter.Validate();
                    (void)assets.Resolve(emitter.Clip);
                    output << "audio-emitter " << emitter.Clip.ID.ToString() << ' '
                        << (emitter.Enabled ? "true" : "false") << ' '
                        << (emitter.PlayOnStart ? "true" : "false") << ' '
                        << (emitter.Loop ? "true" : "false") << ' '
                        << (emitter.Spatial ? "true" : "false") << ' '
                        << emitter.Gain << ' ' << emitter.Pitch << ' '
                        << DistanceModelName(emitter.Attenuation.Model) << ' '
                        << emitter.Attenuation.MinDistance << ' '
                        << emitter.Attenuation.MaxDistance << ' '
                        << emitter.Attenuation.Rolloff << ' '
                        << Quote(emitter.Bus) << ' ' << emitter.Priority << '\n';
                }
                if (scene.HasAudioListener(entity))
                {
                    const auto& listener = scene.AudioListenerComponentFor(entity);
                    listener.Validate();
                    output << "audio-listener "
                        << (listener.Enabled ? "true" : "false") << ' '
                        << (listener.Primary ? "true" : "false") << '\n';
                }
                output << "end\n";
                continue;
            }
            output << lineText << '\n';
        }
        if (entityIndex != entities.size())
            throw std::logic_error(
                "SceneSerialization v5 delegate emitted too few entity ends.");
        return output.str();
    }

    inline void LoadSceneV5(
        const std::filesystem::path& path,
        const kairo::assets::AssetRegistry& assets,
        Scene& destination)
    {
        std::error_code error;
        const std::uintmax_t bytes = std::filesystem::file_size(path, error);
        if (error)
            throw std::runtime_error(
                "Cannot inspect Kairo scene: " + error.message());
        if (bytes > scene_v5_detail::MaxSceneBytes)
            throw std::length_error("Kairo scene exceeds the 64 MiB safety limit.");
        std::ifstream input(path, std::ios::binary);
        if (!input)
            throw std::runtime_error(
                "Cannot open Kairo scene for reading: " + path.string());
        std::string source(static_cast<std::size_t>(bytes), '\0');
        if (!source.empty() && !input.read(
                source.data(), static_cast<std::streamsize>(source.size())))
            throw std::runtime_error(
                "Cannot read complete Kairo scene: " + path.string());
        Scene candidate = ParseSceneV5(source, assets);
        destination = std::move(candidate);
    }

    inline void SaveSceneV5(
        const std::filesystem::path& path,
        const Scene& scene,
        const kairo::assets::AssetRegistry& assets)
    {
        const std::filesystem::path parent = path.has_parent_path()
            ? path.parent_path() : std::filesystem::path(".");
        std::error_code error;
        std::filesystem::create_directories(parent, error);
        if (error)
            throw std::runtime_error(
                "Cannot create scene directory: " + error.message());
        const std::filesystem::path temporary = path.string() +
            ".tmp-" + kairo::assets::GenerateAssetID().ToString();
        try
        {
            const std::string source = SerializeSceneV5(scene, assets);
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output)
                throw std::runtime_error(
                    "Cannot open temporary scene for writing.");
            output.write(source.data(),
                static_cast<std::streamsize>(source.size()));
            output.flush();
            if (!output)
                throw std::runtime_error(
                    "Cannot write complete temporary scene.");
            output.close();
            kairo::assets::ReplaceFileAtomically(temporary, path);
        }
        catch (...)
        {
            std::filesystem::remove(temporary, error);
            throw;
        }
    }
}
