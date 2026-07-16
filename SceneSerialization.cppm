module;

#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

export module Kairo.EngineCore.SceneSerialization;

import Kairo.Assets;
import Kairo.EngineCore.Entity;
import Kairo.EngineCore.RuntimeComponents;
import Kairo.EngineCore.Scene;
import Kairo.Foundation.Math;

export namespace kairo::engine
{
    /// Parse failure carrying an exact one-based source location.
    class SceneFormatError final : public std::runtime_error
    {
    public:
        SceneFormatError(std::size_t line, std::size_t column, std::string message)
            : std::runtime_error("Kairo scene " + std::to_string(line) + ":" +
                std::to_string(column) + ": " + message), Line(line), Column(column) {}

        std::size_t Line;
        std::size_t Column;
    };

    namespace scene_format_detail
    {
        constexpr std::size_t MaxSceneBytes = 64u * 1024u * 1024u;
        constexpr std::size_t MaxEntities = 1'000'000u;
        constexpr std::size_t MaxNameBytes = 4096u;

        struct Token final
        {
            std::string Text;
            std::size_t Column = 1u;
        };

        [[nodiscard]] inline std::vector<Token> Tokenize(std::string_view line, std::size_t lineNumber)
        {
            std::vector<Token> tokens;
            std::size_t index = 0u;
            while (index < line.size())
            {
                while (index < line.size() && (line[index] == ' ' || line[index] == '\t' || line[index] == '\r')) ++index;
                if (index == line.size() || line[index] == '#') break;

                Token token;
                token.Column = index + 1u;
                if (line[index] != '"')
                {
                    while (index < line.size() && line[index] != ' ' && line[index] != '\t' &&
                        line[index] != '\r' && line[index] != '#')
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
                        if (index == line.size()) throw SceneFormatError(lineNumber, index, "unfinished escape sequence");
                        const char escaped = line[index++];
                        switch (escaped)
                        {
                            case '\\': token.Text.push_back('\\'); break;
                            case '"': token.Text.push_back('"'); break;
                            case 'n': token.Text.push_back('\n'); break;
                            case 't': token.Text.push_back('\t'); break;
                            default: throw SceneFormatError(lineNumber, index, "unknown quoted-string escape");
                        }
                    }
                    if (!closed) throw SceneFormatError(lineNumber, token.Column, "unterminated quoted string");
                    if (index < line.size() && line[index] != ' ' && line[index] != '\t' && line[index] != '\r' && line[index] != '#')
                        throw SceneFormatError(lineNumber, index + 1u, "quoted token must be followed by whitespace");
                }
                tokens.push_back(std::move(token));
            }
            return tokens;
        }

        inline void RequireCount(const std::vector<Token>& tokens, std::size_t expected,
            std::size_t line, std::string_view statement)
        {
            if (tokens.size() == expected) return;
            const std::size_t column = tokens.size() > expected ? tokens[expected].Column : 1u;
            throw SceneFormatError(line, column, std::string(statement) + " expects " +
                std::to_string(expected - 1u) + " argument(s)");
        }

        [[nodiscard]] inline std::uint32_t ParseUInt32(const Token& token, std::size_t line, std::string_view field)
        {
            std::uint32_t result = 0u;
            const auto [end, error] = std::from_chars(token.Text.data(), token.Text.data() + token.Text.size(), result);
            if (error != std::errc{} || end != token.Text.data() + token.Text.size())
                throw SceneFormatError(line, token.Column, std::string(field) + " must be an unsigned 32-bit integer");
            return result;
        }

        [[nodiscard]] inline float ParseFloat(const Token& token, std::size_t line, std::string_view field)
        {
            float result = 0.0f;
            const auto [end, error] = std::from_chars(token.Text.data(), token.Text.data() + token.Text.size(), result);
            if (error != std::errc{} || end != token.Text.data() + token.Text.size() || !std::isfinite(result))
                throw SceneFormatError(line, token.Column, std::string(field) + " must be a finite float");
            return result;
        }

        [[nodiscard]] inline bool ParseBool(const Token& token, std::size_t line)
        {
            if (token.Text == "true") return true;
            if (token.Text == "false") return false;
            throw SceneFormatError(line, token.Column, "boolean value must be true or false");
        }

        [[nodiscard]] inline kairo::assets::AssetID ParseAssetID(const Token& token, std::size_t line)
        {
            try { return kairo::assets::AssetID::Parse(token.Text); }
            catch (const std::exception& error) { throw SceneFormatError(line, token.Column, error.what()); }
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

        [[nodiscard]] inline bool IsFinite(const kairo::foundation::math::Transformf& transform) noexcept
        {
            return std::isfinite(transform.Translation.x) && std::isfinite(transform.Translation.y) &&
                std::isfinite(transform.Translation.z) && std::isfinite(transform.Rotation.x) &&
                std::isfinite(transform.Rotation.y) && std::isfinite(transform.Rotation.z) &&
                std::isfinite(transform.Rotation.w) && std::isfinite(transform.Scale.x) &&
                std::isfinite(transform.Scale.y) && std::isfinite(transform.Scale.z);
        }
    }

    /// Input: a complete UTF-8 `kairo-scene 1` document and the project asset registry.
    /// Output: a new scene with restored IDs and validated typed asset references.
    /// Task: deserialize authored state with deterministic behavior and exact
    /// line/column diagnostics. Physics binding presence and its opaque
    /// authoring token round-trip; adapters may replace that token in a cloned
    /// runtime scene when entering play mode.
    [[nodiscard]] inline Scene ParseScene(std::string_view source, const kairo::assets::AssetRegistry& assets)
    {
        using namespace scene_format_detail;
        if (source.size() > MaxSceneBytes) throw std::length_error("Kairo scene exceeds the 64 MiB safety limit.");

        Scene scene;
        std::optional<Entity> current;
        bool headerSeen = false;
        bool transformSeen = false;
        bool meshSeen = false;
        bool cameraSeen = false;
        bool rigidBodySeen = false;
        bool colliderSeen = false;
        std::istringstream input{ std::string(source) };
        std::string lineText;
        std::size_t lineNumber = 0u;
        while (std::getline(input, lineText))
        {
            ++lineNumber;
            const std::vector<Token> tokens = Tokenize(lineText, lineNumber);
            if (tokens.empty()) continue;

            if (!headerSeen)
            {
                RequireCount(tokens, 2u, lineNumber, "kairo-scene header");
                if (tokens[0].Text != "kairo-scene") throw SceneFormatError(lineNumber, tokens[0].Column, "expected kairo-scene header");
                if (tokens[1].Text != "1") throw SceneFormatError(lineNumber, tokens[1].Column, "unsupported scene version");
                headerSeen = true;
                continue;
            }

            if (tokens[0].Text == "entity")
            {
                if (current.has_value()) throw SceneFormatError(lineNumber, tokens[0].Column, "nested entity record before end");
                RequireCount(tokens, 3u, lineNumber, "entity");
                if (scene.Size() >= MaxEntities) throw SceneFormatError(lineNumber, tokens[0].Column, "entity count exceeds safety limit");
                if (tokens[2].Text.size() > MaxNameBytes) throw SceneFormatError(lineNumber, tokens[2].Column, "entity name exceeds safety limit");
                try
                {
                    current = scene.CreateEntityWithID({ ParseUInt32(tokens[1], lineNumber, "entity ID") }, tokens[2].Text);
                }
                catch (const SceneFormatError&) { throw; }
                catch (const std::exception& error) { throw SceneFormatError(lineNumber, tokens[1].Column, error.what()); }
                transformSeen = false;
                meshSeen = false;
                cameraSeen = false;
                rigidBodySeen = false;
                colliderSeen = false;
                continue;
            }

            if (!current.has_value()) throw SceneFormatError(lineNumber, tokens[0].Column, "statement outside entity record");
            if (tokens[0].Text == "transform")
            {
                if (transformSeen) throw SceneFormatError(lineNumber, tokens[0].Column, "duplicate transform component");
                RequireCount(tokens, 11u, lineNumber, "transform");
                auto& transform = scene.Transform(*current).Local;
                transform.Translation = {
                    ParseFloat(tokens[1], lineNumber, "translation x"),
                    ParseFloat(tokens[2], lineNumber, "translation y"),
                    ParseFloat(tokens[3], lineNumber, "translation z")
                };
                transform.Rotation = {
                    ParseFloat(tokens[4], lineNumber, "rotation x"),
                    ParseFloat(tokens[5], lineNumber, "rotation y"),
                    ParseFloat(tokens[6], lineNumber, "rotation z"),
                    ParseFloat(tokens[7], lineNumber, "rotation w")
                };
                transform.Scale = {
                    ParseFloat(tokens[8], lineNumber, "scale x"),
                    ParseFloat(tokens[9], lineNumber, "scale y"),
                    ParseFloat(tokens[10], lineNumber, "scale z")
                };
                if (!kairo::foundation::math::IsValid(transform))
                    throw SceneFormatError(lineNumber, tokens[0].Column, "transform requires normalized rotation and non-zero scale");
                transformSeen = true;
            }
            else if (tokens[0].Text == "mesh-renderer")
            {
                if (meshSeen) throw SceneFormatError(lineNumber, tokens[0].Column, "duplicate mesh renderer component");
                RequireCount(tokens, 4u, lineNumber, "mesh-renderer");
                const kairo::assets::MeshAssetHandle mesh{ ParseAssetID(tokens[1], lineNumber) };
                const kairo::assets::MaterialAssetHandle material{ ParseAssetID(tokens[2], lineNumber) };
                try { (void)assets.Resolve(mesh); }
                catch (const std::exception& error) { throw SceneFormatError(lineNumber, tokens[1].Column, error.what()); }
                try { (void)assets.Resolve(material); }
                catch (const std::exception& error) { throw SceneFormatError(lineNumber, tokens[2].Column, error.what()); }
                scene.SetMeshRenderer(*current, { mesh, material, ParseBool(tokens[3], lineNumber) });
                meshSeen = true;
            }
            else if (tokens[0].Text == "camera")
            {
                if (cameraSeen) throw SceneFormatError(lineNumber, tokens[0].Column, "duplicate camera component");
                RequireCount(tokens, 5u, lineNumber, "camera");
                CameraComponent camera{
                    ParseFloat(tokens[1], lineNumber, "vertical FOV"),
                    ParseFloat(tokens[2], lineNumber, "near plane"),
                    ParseFloat(tokens[3], lineNumber, "far plane"),
                    ParseBool(tokens[4], lineNumber)
                };
                try { scene.SetCamera(*current, camera); }
                catch (const std::exception& error) { throw SceneFormatError(lineNumber, tokens[1].Column, error.what()); }
                cameraSeen = true;
            }
            else if (tokens[0].Text == "rigid-body")
            {
                if (rigidBodySeen) throw SceneFormatError(lineNumber, tokens[0].Column, "duplicate rigid body component");
                RequireCount(tokens, 2u, lineNumber, "rigid-body");
                scene.SetRigidBody(*current, { ParseUInt32(tokens[1], lineNumber, "rigid body ID") });
                rigidBodySeen = true;
            }
            else if (tokens[0].Text == "collider")
            {
                if (colliderSeen) throw SceneFormatError(lineNumber, tokens[0].Column, "duplicate collider component");
                RequireCount(tokens, 2u, lineNumber, "collider");
                scene.SetCollider(*current, { ParseUInt32(tokens[1], lineNumber, "collider ID") });
                colliderSeen = true;
            }
            else if (tokens[0].Text == "end")
            {
                RequireCount(tokens, 1u, lineNumber, "end");
                current.reset();
            }
            else
            {
                throw SceneFormatError(lineNumber, tokens[0].Column, "unknown statement '" + tokens[0].Text + "'");
            }
        }

        if (!headerSeen) throw SceneFormatError(1u, 1u, "missing kairo-scene header");
        if (current.has_value()) throw SceneFormatError(lineNumber + 1u, 1u, "entity record is missing end");
        return scene;
    }

    /// Output: stable ID-ordered and diff-friendly `kairo-scene 1` text.
    /// Preconditions: transforms and public components must remain valid, and
    /// every mesh/material reference must resolve with its declared asset type.
    [[nodiscard]] inline std::string SerializeScene(const Scene& scene, const kairo::assets::AssetRegistry& assets)
    {
        using namespace scene_format_detail;
        std::ostringstream output;
        output << std::setprecision(std::numeric_limits<float>::max_digits10);
        output << "kairo-scene 1\n";
        for (const Entity entity : scene.Entities())
        {
            const auto& transform = scene.Transform(entity).Local;
            if (scene.Name(entity).Value.size() > MaxNameBytes)
                throw std::length_error("Cannot serialize an entity name exceeding 4096 bytes.");
            if (!IsFinite(transform) || !kairo::foundation::math::IsValid(transform))
                throw std::invalid_argument("Cannot serialize an invalid entity transform.");
            output << "entity " << entity.Value << ' ' << Quote(scene.Name(entity).Value) << '\n';
            output << "transform " << transform.Translation.x << ' ' << transform.Translation.y << ' '
                << transform.Translation.z << ' ' << transform.Rotation.x << ' ' << transform.Rotation.y << ' '
                << transform.Rotation.z << ' ' << transform.Rotation.w << ' ' << transform.Scale.x << ' '
                << transform.Scale.y << ' ' << transform.Scale.z << '\n';
            if (scene.HasMeshRenderer(entity))
            {
                const auto& mesh = scene.MeshRenderer(entity);
                (void)assets.Resolve(mesh.MeshAsset);
                (void)assets.Resolve(mesh.MaterialAsset);
                output << "mesh-renderer " << mesh.MeshAsset.ID.ToString() << ' '
                    << mesh.MaterialAsset.ID.ToString() << ' ' << (mesh.Visible ? "true" : "false") << '\n';
            }
            if (scene.HasCamera(entity))
            {
                const auto& camera = scene.Camera(entity);
                camera.Validate();
                output << "camera " << camera.VerticalFovRadians << ' ' << camera.NearPlane << ' '
                    << camera.FarPlane << ' ' << (camera.Primary ? "true" : "false") << '\n';
            }
            if (scene.HasRigidBody(entity))
                output << "rigid-body " << scene.RigidBody(entity).Body << '\n';
            if (scene.HasCollider(entity))
                output << "collider " << scene.Collider(entity).Collider << '\n';
            output << "end\n";
        }
        return output.str();
    }

    inline void LoadScene(const std::filesystem::path& path, const kairo::assets::AssetRegistry& assets, Scene& destination)
    {
        std::error_code error;
        const std::uintmax_t bytes = std::filesystem::file_size(path, error);
        if (error) throw std::runtime_error("Cannot inspect Kairo scene: " + error.message());
        if (bytes > scene_format_detail::MaxSceneBytes) throw std::length_error("Kairo scene exceeds the 64 MiB safety limit.");
        std::ifstream input(path, std::ios::binary);
        if (!input) throw std::runtime_error("Cannot open Kairo scene for reading: " + path.string());
        std::string source(static_cast<std::size_t>(bytes), '\0');
        if (!source.empty() && !input.read(source.data(), static_cast<std::streamsize>(source.size())))
            throw std::runtime_error("Cannot read complete Kairo scene: " + path.string());
        Scene candidate = ParseScene(source, assets);
        destination = std::move(candidate);
    }

    /// Task: preserve the prior scene on write failure by flushing a same-directory
    /// temporary and replacing the destination with one host atomic rename.
    inline void SaveScene(const std::filesystem::path& path, const Scene& scene,
        const kairo::assets::AssetRegistry& assets)
    {
        const std::filesystem::path parent = path.has_parent_path() ? path.parent_path() : std::filesystem::path(".");
        std::error_code error;
        std::filesystem::create_directories(parent, error);
        if (error) throw std::runtime_error("Cannot create scene directory: " + error.message());
        const std::filesystem::path temporary = path.string() + ".tmp-" + kairo::assets::GenerateAssetID().ToString();
        try
        {
            const std::string source = SerializeScene(scene, assets);
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output) throw std::runtime_error("Cannot open temporary scene for writing.");
            output.write(source.data(), static_cast<std::streamsize>(source.size()));
            output.flush();
            if (!output) throw std::runtime_error("Cannot write complete temporary scene.");
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
