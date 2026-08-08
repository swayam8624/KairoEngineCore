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
#include <locale>
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

        [[nodiscard]] inline std::uint64_t ParseUInt64(
            const Token& token, std::size_t line, std::string_view field)
        {
            std::uint64_t result = 0u;
            const auto [end, error] = std::from_chars(
                token.Text.data(), token.Text.data() + token.Text.size(), result);
            if (error != std::errc{} || end != token.Text.data() + token.Text.size())
                throw SceneFormatError(line, token.Column,
                    std::string(field) + " must be an unsigned 64-bit integer");
            return result;
        }

        [[nodiscard]] inline std::int32_t ParseInt32(
            const Token& token, std::size_t line, std::string_view field)
        {
            std::int32_t result = 0;
            const auto [end, error] = std::from_chars(
                token.Text.data(), token.Text.data() + token.Text.size(), result);
            if (error != std::errc{} || end != token.Text.data() + token.Text.size())
                throw SceneFormatError(line, token.Column,
                    std::string(field) + " must be a signed 32-bit integer");
            return result;
        }

        [[nodiscard]] inline float ParseFloat(const Token& token, std::size_t line, std::string_view field)
        {
            std::istringstream stream(token.Text);
            stream.imbue(std::locale::classic());
            float result = 0.0f;
            stream >> result;
            if (stream.fail() || !std::isfinite(result))
                throw SceneFormatError(line, token.Column, std::string(field) + " must be a finite float");
            stream >> std::ws;
            if (!stream.eof())
                throw SceneFormatError(line, token.Column, std::string(field) + " must be a finite float");
            return result;
        }

        [[nodiscard]] inline bool ParseBool(const Token& token, std::size_t line)
        {
            if (token.Text == "true") return true;
            if (token.Text == "false") return false;
            throw SceneFormatError(line, token.Column, "boolean value must be true or false");
        }

        [[nodiscard]] inline CameraProjection ParseCameraProjection(
            const Token& token, std::size_t line)
        {
            if (token.Text == "perspective") return CameraProjection::Perspective;
            if (token.Text == "orthographic") return CameraProjection::Orthographic;
            throw SceneFormatError(line, token.Column,
                "camera projection must be perspective or orthographic");
        }

        [[nodiscard]] inline std::string_view CameraProjectionName(CameraProjection value)
        {
            switch (value)
            {
                case CameraProjection::Perspective: return "perspective";
                case CameraProjection::Orthographic: return "orthographic";
            }
            throw std::invalid_argument("Camera projection enum is invalid.");
        }

        [[nodiscard]] inline CameraClearMode ParseCameraClearMode(
            const Token& token, std::size_t line)
        {
            if (token.Text == "environment") return CameraClearMode::Environment;
            if (token.Text == "solid") return CameraClearMode::SolidColor;
            if (token.Text == "depth") return CameraClearMode::DepthOnly;
            if (token.Text == "nothing") return CameraClearMode::Nothing;
            throw SceneFormatError(line, token.Column,
                "camera clear mode must be environment, solid, depth, or nothing");
        }

        [[nodiscard]] inline std::string_view CameraClearModeName(CameraClearMode value)
        {
            switch (value)
            {
                case CameraClearMode::Environment: return "environment";
                case CameraClearMode::SolidColor: return "solid";
                case CameraClearMode::DepthOnly: return "depth";
                case CameraClearMode::Nothing: return "nothing";
            }
            throw std::invalid_argument("Camera clear mode enum is invalid.");
        }

        [[nodiscard]] inline LightType ParseLightType(const Token& token, std::size_t line)
        {
            if (token.Text == "directional") return LightType::Directional;
            if (token.Text == "point") return LightType::Point;
            if (token.Text == "spot") return LightType::Spot;
            if (token.Text == "rectangle") return LightType::RectangleArea;
            throw SceneFormatError(line, token.Column,
                "light type must be directional, point, spot, or rectangle");
        }

        [[nodiscard]] inline std::string_view LightTypeName(LightType value)
        {
            switch (value)
            {
                case LightType::Directional: return "directional";
                case LightType::Point: return "point";
                case LightType::Spot: return "spot";
                case LightType::RectangleArea: return "rectangle";
            }
            throw std::invalid_argument("Light type enum is invalid.");
        }

        [[nodiscard]] inline PhotometricUnit ParsePhotometricUnit(
            const Token& token, std::size_t line)
        {
            if (token.Text == "lux") return PhotometricUnit::Lux;
            if (token.Text == "candela") return PhotometricUnit::Candela;
            if (token.Text == "nit") return PhotometricUnit::Nit;
            throw SceneFormatError(line, token.Column,
                "photometric unit must be lux, candela, or nit");
        }

        [[nodiscard]] inline std::string_view PhotometricUnitName(PhotometricUnit value)
        {
            switch (value)
            {
                case PhotometricUnit::Lux: return "lux";
                case PhotometricUnit::Candela: return "candela";
                case PhotometricUnit::Nit: return "nit";
            }
            throw std::invalid_argument("Photometric unit enum is invalid.");
        }

        [[nodiscard]] inline ShadowPolicy ParseShadowPolicy(
            const Token& token, std::size_t line)
        {
            if (token.Text == "disabled") return ShadowPolicy::Disabled;
            if (token.Text == "hard") return ShadowPolicy::Hard;
            if (token.Text == "soft") return ShadowPolicy::Soft;
            throw SceneFormatError(line, token.Column,
                "shadow policy must be disabled, hard, or soft");
        }

        [[nodiscard]] inline std::string_view ShadowPolicyName(ShadowPolicy value)
        {
            switch (value)
            {
                case ShadowPolicy::Disabled: return "disabled";
                case ShadowPolicy::Hard: return "hard";
                case ShadowPolicy::Soft: return "soft";
            }
            throw std::invalid_argument("Shadow policy enum is invalid.");
        }

        [[nodiscard]] inline FogMode ParseFogMode(const Token& token, std::size_t line)
        {
            if (token.Text == "disabled") return FogMode::Disabled;
            if (token.Text == "linear") return FogMode::Linear;
            if (token.Text == "exponential") return FogMode::Exponential;
            throw SceneFormatError(line, token.Column,
                "fog mode must be disabled, linear, or exponential");
        }

        [[nodiscard]] inline std::string_view FogModeName(FogMode value)
        {
            switch (value)
            {
                case FogMode::Disabled: return "disabled";
                case FogMode::Linear: return "linear";
                case FogMode::Exponential: return "exponential";
            }
            throw std::invalid_argument("Fog mode enum is invalid.");
        }

        [[nodiscard]] inline ToneMapping ParseToneMapping(
            const Token& token, std::size_t line)
        {
            if (token.Text == "none") return ToneMapping::None;
            if (token.Text == "reinhard") return ToneMapping::Reinhard;
            if (token.Text == "aces") return ToneMapping::ACES;
            throw SceneFormatError(line, token.Column,
                "tone mapping must be none, reinhard, or aces");
        }

        [[nodiscard]] inline std::string_view ToneMappingName(ToneMapping value)
        {
            switch (value)
            {
                case ToneMapping::None: return "none";
                case ToneMapping::Reinhard: return "reinhard";
                case ToneMapping::ACES: return "aces";
            }
            throw std::invalid_argument("Tone mapping enum is invalid.");
        }

        [[nodiscard]] inline RigidBodyMotion ParseMotion(const Token& token, std::size_t line)
        {
            if (token.Text == "static") return RigidBodyMotion::Static;
            if (token.Text == "dynamic") return RigidBodyMotion::Dynamic;
            if (token.Text == "kinematic") return RigidBodyMotion::Kinematic;
            throw SceneFormatError(line, token.Column,
                "rigid body motion must be static, dynamic, or kinematic");
        }

        [[nodiscard]] inline std::string_view MotionName(RigidBodyMotion motion)
        {
            switch (motion)
            {
                case RigidBodyMotion::Static: return "static";
                case RigidBodyMotion::Dynamic: return "dynamic";
                case RigidBodyMotion::Kinematic: return "kinematic";
            }
            throw std::invalid_argument("Rigid body motion enum is invalid.");
        }

        [[nodiscard]] inline ColliderShape ParseShape(const Token& token, std::size_t line)
        {
            if (token.Text == "box") return ColliderShape::Box;
            if (token.Text == "sphere") return ColliderShape::Sphere;
            if (token.Text == "capsule") return ColliderShape::Capsule;
            throw SceneFormatError(line, token.Column,
                "collider shape must be box, sphere, or capsule");
        }

        [[nodiscard]] inline std::string_view ShapeName(ColliderShape shape)
        {
            switch (shape)
            {
                case ColliderShape::Box: return "box";
                case ColliderShape::Sphere: return "sphere";
                case ColliderShape::Capsule: return "capsule";
            }
            throw std::invalid_argument("Collider shape enum is invalid.");
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

    /// Input: a complete UTF-8 `kairo-scene 1|2|3|4` document and the project asset registry.
    /// Output: a new scene with restored IDs and validated typed asset references.
    /// Task: deserialize authored state with deterministic behavior and exact
    /// line/column diagnostics. Typed physics descriptors remain independent
    /// of process-local PhysicsWorld handles.
    [[nodiscard]] inline Scene ParseScene(std::string_view source, const kairo::assets::AssetRegistry& assets)
    {
        using namespace scene_format_detail;
        if (source.size() > MaxSceneBytes) throw std::length_error("Kairo scene exceeds the 64 MiB safety limit.");

        Scene scene;
        struct PendingParent final
        {
            Entity Child;
            Entity Parent;
            std::size_t Line = 1u;
            std::size_t Column = 1u;
        };
        std::vector<PendingParent> pendingParents;
        std::optional<Entity> primaryCamera;
        std::optional<Entity> current;
        bool headerSeen = false;
        std::uint32_t version = 0u;
        bool transformSeen = false;
        bool meshSeen = false;
        bool sceneInstanceSeen = false;
        bool cameraSeen = false;
        bool lightSeen = false;
        bool environmentSeen = false;
        bool logicSeen = false;
        bool rigidBodySeen = false;
        bool colliderSeen = false;
        bool parentSeen = false;
        bool enabledSeen = false;
        bool layerSeen = false;
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
                if (tokens[1].Text == "1") version = 1u;
                else if (tokens[1].Text == "2") version = 2u;
                else if (tokens[1].Text == "3") version = 3u;
                else if (tokens[1].Text == "4") version = 4u;
                else throw SceneFormatError(lineNumber, tokens[1].Column, "unsupported scene version");
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
                sceneInstanceSeen = false;
                cameraSeen = false;
                lightSeen = false;
                environmentSeen = false;
                logicSeen = false;
                rigidBodySeen = false;
                colliderSeen = false;
                parentSeen = false;
                enabledSeen = false;
                layerSeen = false;
                continue;
            }

            if (!current.has_value()) throw SceneFormatError(lineNumber, tokens[0].Column, "statement outside entity record");
            if (tokens[0].Text == "parent")
            {
                if (version < 2u) throw SceneFormatError(lineNumber, tokens[0].Column,
                    "parent requires kairo-scene 2");
                if (parentSeen) throw SceneFormatError(lineNumber, tokens[0].Column,
                    "duplicate parent relationship");
                RequireCount(tokens, 2u, lineNumber, "parent");
                pendingParents.push_back({ *current,
                    { ParseUInt32(tokens[1], lineNumber, "parent entity ID") },
                    lineNumber, tokens[1].Column });
                parentSeen = true;
            }
            else if (tokens[0].Text == "enabled")
            {
                if (version < 2u) throw SceneFormatError(lineNumber, tokens[0].Column,
                    "enabled requires kairo-scene 2");
                if (enabledSeen) throw SceneFormatError(lineNumber, tokens[0].Column,
                    "duplicate enabled state");
                RequireCount(tokens, 2u, lineNumber, "enabled");
                scene.SetEnabled(*current, ParseBool(tokens[1], lineNumber));
                enabledSeen = true;
            }
            else if (tokens[0].Text == "layer")
            {
                if (version < 2u) throw SceneFormatError(lineNumber, tokens[0].Column,
                    "layer requires kairo-scene 2");
                if (layerSeen) throw SceneFormatError(lineNumber, tokens[0].Column,
                    "duplicate layer state");
                RequireCount(tokens, 2u, lineNumber, "layer");
                try { scene.SetLayer(*current, ParseUInt32(tokens[1], lineNumber, "entity layer")); }
                catch (const std::exception& error)
                {
                    throw SceneFormatError(lineNumber, tokens[1].Column, error.what());
                }
                layerSeen = true;
            }
            else if (tokens[0].Text == "tag")
            {
                if (version < 2u) throw SceneFormatError(lineNumber, tokens[0].Column,
                    "tag requires kairo-scene 2");
                RequireCount(tokens, 2u, lineNumber, "tag");
                if (scene.HasTag(*current, tokens[1].Text))
                    throw SceneFormatError(lineNumber, tokens[1].Column, "duplicate entity tag");
                try { scene.AddTag(*current, tokens[1].Text); }
                catch (const std::exception& error)
                {
                    throw SceneFormatError(lineNumber, tokens[1].Column, error.what());
                }
            }
            else if (tokens[0].Text == "transform")
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
                if (version < 3u)
                {
                    RequireCount(tokens, 4u, lineNumber, "mesh-renderer");
                    const kairo::assets::MeshAssetHandle mesh{ ParseAssetID(tokens[1], lineNumber) };
                    const kairo::assets::MaterialAssetHandle material{ ParseAssetID(tokens[2], lineNumber) };
                    try { (void)assets.Resolve(mesh); }
                    catch (const std::exception& error) { throw SceneFormatError(lineNumber, tokens[1].Column, error.what()); }
                    try { (void)assets.Resolve(material); }
                    catch (const std::exception& error) { throw SceneFormatError(lineNumber, tokens[2].Column, error.what()); }
                    scene.SetMeshRenderer(*current, { mesh, material, ParseBool(tokens[3], lineNumber) });
                }
                else
                {
                    if (tokens.size() < 8u)
                        throw SceneFormatError(lineNumber, 1u,
                            "mesh-renderer expects mesh, visibility/shadow/layer settings, count, and materials");
                    const std::uint32_t materialCount = ParseUInt32(
                        tokens[6], lineNumber, "material slot count");
                    if (materialCount == 0u || materialCount > MaximumMaterialSlots)
                        throw SceneFormatError(lineNumber, tokens[6].Column,
                            "material slot count must be between 1 and 256");
                    RequireCount(tokens, 7u + materialCount, lineNumber, "mesh-renderer");
                    MeshRendererComponent component;
                    component.MeshAsset = { ParseAssetID(tokens[1], lineNumber) };
                    component.Visible = ParseBool(tokens[2], lineNumber);
                    component.CastShadows = ParseBool(tokens[3], lineNumber);
                    component.ReceiveShadows = ParseBool(tokens[4], lineNumber);
                    component.RenderLayers = ParseUInt64(tokens[5], lineNumber, "render layer mask");
                    component.MaterialAsset = { ParseAssetID(tokens[7], lineNumber) };
                    component.AdditionalMaterialSlots.reserve(materialCount - 1u);
                    for (std::uint32_t slot = 1u; slot < materialCount; ++slot)
                        component.AdditionalMaterialSlots.push_back({
                            ParseAssetID(tokens[7u + slot], lineNumber) });
                    try
                    {
                        (void)assets.Resolve(component.MeshAsset);
                        for (std::size_t slot = 0u; slot < component.MaterialSlotCount(); ++slot)
                            (void)assets.Resolve(component.MaterialForSlot(slot));
                        scene.SetMeshRenderer(*current, std::move(component));
                    }
                    catch (const std::exception& error)
                    {
                        throw SceneFormatError(lineNumber, tokens[1].Column, error.what());
                    }
                }
                meshSeen = true;
            }
            else if (tokens[0].Text == "scene-instance")
            {
                if (version < 4u) throw SceneFormatError(lineNumber, tokens[0].Column,
                    "scene-instance requires kairo-scene 4");
                if (sceneInstanceSeen) throw SceneFormatError(lineNumber, tokens[0].Column,
                    "duplicate scene-instance component");
                RequireCount(tokens, 6u, lineNumber, "scene-instance");
                SceneInstanceComponent component;
                component.SceneAsset = { ParseAssetID(tokens[1], lineNumber) };
                component.Visible = ParseBool(tokens[2], lineNumber);
                component.CastShadows = ParseBool(tokens[3], lineNumber);
                component.ReceiveShadows = ParseBool(tokens[4], lineNumber);
                component.RenderLayers = ParseUInt64(tokens[5], lineNumber,
                    "render layer mask");
                try
                {
                    (void)assets.Resolve(component.SceneAsset);
                    scene.SetSceneInstance(*current, component);
                }
                catch (const std::exception& error)
                {
                    throw SceneFormatError(lineNumber, tokens[1].Column, error.what());
                }
                sceneInstanceSeen = true;
            }
            else if (tokens[0].Text == "camera")
            {
                if (cameraSeen) throw SceneFormatError(lineNumber, tokens[0].Column, "duplicate camera component");
                CameraComponent camera;
                if (version < 3u)
                {
                    RequireCount(tokens, 5u, lineNumber, "camera");
                    camera.VerticalFovRadians = ParseFloat(tokens[1], lineNumber, "vertical FOV");
                    camera.NearPlane = ParseFloat(tokens[2], lineNumber, "near plane");
                    camera.FarPlane = ParseFloat(tokens[3], lineNumber, "far plane");
                    camera.Primary = ParseBool(tokens[4], lineNumber);
                }
                else
                {
                    RequireCount(tokens, 14u, lineNumber, "camera");
                    camera.Projection = ParseCameraProjection(tokens[1], lineNumber);
                    camera.VerticalFovRadians = ParseFloat(tokens[2], lineNumber, "vertical FOV");
                    camera.OrthographicSize = ParseFloat(tokens[3], lineNumber, "orthographic size");
                    camera.NearPlane = ParseFloat(tokens[4], lineNumber, "near plane");
                    camera.FarPlane = ParseFloat(tokens[5], lineNumber, "far plane");
                    camera.ExposureEV100 = ParseFloat(tokens[6], lineNumber, "camera exposure");
                    camera.Primary = ParseBool(tokens[7], lineNumber);
                    camera.ClearMode = ParseCameraClearMode(tokens[8], lineNumber);
                    camera.ClearColor = {
                        ParseFloat(tokens[9], lineNumber, "clear color red"),
                        ParseFloat(tokens[10], lineNumber, "clear color green"),
                        ParseFloat(tokens[11], lineNumber, "clear color blue"),
                        ParseFloat(tokens[12], lineNumber, "clear color alpha") };
                    camera.RenderLayers = ParseUInt64(tokens[13], lineNumber, "camera render layer mask");
                }
                if (camera.Primary)
                {
                    if (primaryCamera.has_value())
                        throw SceneFormatError(lineNumber, tokens[version < 3u ? 4u : 7u].Column,
                            "scene contains more than one primary camera");
                    primaryCamera = *current;
                }
                try { scene.SetCamera(*current, camera); }
                catch (const std::exception& error) { throw SceneFormatError(lineNumber, tokens[1].Column, error.what()); }
                cameraSeen = true;
            }
            else if (tokens[0].Text == "light")
            {
                if (version < 3u) throw SceneFormatError(lineNumber, tokens[0].Column,
                    "light requires kairo-scene 3");
                if (lightSeen) throw SceneFormatError(lineNumber, tokens[0].Column,
                    "duplicate light component");
                RequireCount(tokens, 16u, lineNumber, "light");
                LightComponent light;
                light.Type = ParseLightType(tokens[1], lineNumber);
                light.Color = {
                    ParseFloat(tokens[2], lineNumber, "light color red"),
                    ParseFloat(tokens[3], lineNumber, "light color green"),
                    ParseFloat(tokens[4], lineNumber, "light color blue") };
                light.Intensity = ParseFloat(tokens[5], lineNumber, "light intensity");
                light.Unit = ParsePhotometricUnit(tokens[6], lineNumber);
                light.Range = ParseFloat(tokens[7], lineNumber, "light range");
                light.InnerConeRadians = ParseFloat(tokens[8], lineNumber, "inner cone");
                light.OuterConeRadians = ParseFloat(tokens[9], lineNumber, "outer cone");
                light.AreaWidth = ParseFloat(tokens[10], lineNumber, "area width");
                light.AreaHeight = ParseFloat(tokens[11], lineNumber, "area height");
                light.Shadows = ParseShadowPolicy(tokens[12], lineNumber);
                light.ShadowBias = ParseFloat(tokens[13], lineNumber, "shadow bias");
                light.ShadowNormalBias = ParseFloat(tokens[14], lineNumber, "shadow normal bias");
                light.RenderLayers = ParseUInt64(tokens[15], lineNumber, "light render layer mask");
                try { scene.SetLight(*current, light); }
                catch (const std::exception& error)
                { throw SceneFormatError(lineNumber, tokens[1].Column, error.what()); }
                lightSeen = true;
            }
            else if (tokens[0].Text == "environment")
            {
                if (version < 3u) throw SceneFormatError(lineNumber, tokens[0].Column,
                    "environment requires kairo-scene 3");
                if (environmentSeen) throw SceneFormatError(lineNumber, tokens[0].Column,
                    "duplicate environment component");
                RequireCount(tokens, 19u, lineNumber, "environment");
                EnvironmentComponent environment;
                environment.Enabled = ParseBool(tokens[1], lineNumber);
                environment.Priority = ParseInt32(tokens[2], lineNumber, "environment priority");
                environment.BackgroundColor = {
                    ParseFloat(tokens[3], lineNumber, "background red"),
                    ParseFloat(tokens[4], lineNumber, "background green"),
                    ParseFloat(tokens[5], lineNumber, "background blue") };
                if (tokens[6].Text != "none")
                {
                    environment.EnvironmentTexture = kairo::assets::TextureAssetHandle{
                        ParseAssetID(tokens[6], lineNumber) };
                    try { (void)assets.Resolve(*environment.EnvironmentTexture); }
                    catch (const std::exception& error)
                    { throw SceneFormatError(lineNumber, tokens[6].Column, error.what()); }
                }
                environment.AmbientIntensity = ParseFloat(tokens[7], lineNumber, "ambient intensity");
                environment.EnvironmentIntensity = ParseFloat(tokens[8], lineNumber, "environment intensity");
                environment.Fog = ParseFogMode(tokens[9], lineNumber);
                environment.FogColor = {
                    ParseFloat(tokens[10], lineNumber, "fog red"),
                    ParseFloat(tokens[11], lineNumber, "fog green"),
                    ParseFloat(tokens[12], lineNumber, "fog blue") };
                environment.FogDensity = ParseFloat(tokens[13], lineNumber, "fog density");
                environment.FogNear = ParseFloat(tokens[14], lineNumber, "fog near");
                environment.FogFar = ParseFloat(tokens[15], lineNumber, "fog far");
                environment.ExposureEV100 = ParseFloat(tokens[16], lineNumber, "environment exposure");
                environment.ToneMap = ParseToneMapping(tokens[17], lineNumber);
                if (tokens[18].Text != "global")
                    throw SceneFormatError(lineNumber, tokens[18].Column,
                        "environment scope must be global in scene version 3");
                try { scene.SetEnvironment(*current, std::move(environment)); }
                catch (const std::exception& error)
                { throw SceneFormatError(lineNumber, tokens[1].Column, error.what()); }
                environmentSeen = true;
            }
            else if (tokens[0].Text == "logic")
            {
                if (version < 2u) throw SceneFormatError(lineNumber, tokens[0].Column,
                    "logic requires kairo-scene 2");
                if (logicSeen) throw SceneFormatError(lineNumber, tokens[0].Column,
                    "duplicate logic component");
                RequireCount(tokens, 3u, lineNumber, "logic");
                const kairo::assets::DocumentAssetHandle document{
                    ParseAssetID(tokens[1], lineNumber) };
                try { (void)assets.Resolve(document); }
                catch (const std::exception& error)
                { throw SceneFormatError(lineNumber, tokens[1].Column, error.what()); }
                scene.SetLogic(*current, { document, ParseBool(tokens[2], lineNumber) });
                logicSeen = true;
            }
            else if (tokens[0].Text == "rigid-body")
            {
                if (rigidBodySeen) throw SceneFormatError(lineNumber, tokens[0].Column, "duplicate rigid body component");
                if (version == 1u)
                {
                    RequireCount(tokens, 2u, lineNumber, "rigid-body");
                    (void)ParseUInt32(tokens[1], lineNumber, "legacy rigid body ID");
                    scene.SetRigidBody(*current, {});
                }
                else
                {
                    RequireCount(tokens, 6u, lineNumber, "rigid-body");
                    RigidBodyComponent body{
                        ParseMotion(tokens[1], lineNumber),
                        ParseFloat(tokens[2], lineNumber, "density"),
                        ParseFloat(tokens[3], lineNumber, "gravity scale"),
                        ParseFloat(tokens[4], lineNumber, "linear damping"),
                        ParseFloat(tokens[5], lineNumber, "angular damping")
                    };
                    try { scene.SetRigidBody(*current, body); }
                    catch (const std::exception& error)
                    { throw SceneFormatError(lineNumber, tokens[1].Column, error.what()); }
                }
                rigidBodySeen = true;
            }
            else if (tokens[0].Text == "collider")
            {
                if (colliderSeen) throw SceneFormatError(lineNumber, tokens[0].Column, "duplicate collider component");
                if (version == 1u)
                {
                    RequireCount(tokens, 2u, lineNumber, "collider");
                    (void)ParseUInt32(tokens[1], lineNumber, "legacy collider ID");
                    scene.SetCollider(*current, {});
                }
                else
                {
                    if (tokens.size() < 2u)
                        throw SceneFormatError(lineNumber, 1u, "collider expects a shape");
                    ColliderComponent collider;
                    collider.Shape = ParseShape(tokens[1], lineNumber);
                    if (collider.Shape == ColliderShape::Box)
                    {
                        RequireCount(tokens, 10u, lineNumber, "box collider");
                        collider.HalfExtents = {
                            ParseFloat(tokens[2], lineNumber, "box half extent x"),
                            ParseFloat(tokens[3], lineNumber, "box half extent y"),
                            ParseFloat(tokens[4], lineNumber, "box half extent z") };
                        collider.Friction = ParseFloat(tokens[5], lineNumber, "friction");
                        collider.Restitution = ParseFloat(tokens[6], lineNumber, "restitution");
                        collider.BelongsTo = ParseUInt32(tokens[7], lineNumber, "category mask");
                        collider.CollidesWith = ParseUInt32(tokens[8], lineNumber, "collision mask");
                        collider.IsTrigger = ParseBool(tokens[9], lineNumber);
                    }
                    else if (collider.Shape == ColliderShape::Sphere)
                    {
                        RequireCount(tokens, 8u, lineNumber, "sphere collider");
                        collider.Radius = ParseFloat(tokens[2], lineNumber, "sphere radius");
                        collider.Friction = ParseFloat(tokens[3], lineNumber, "friction");
                        collider.Restitution = ParseFloat(tokens[4], lineNumber, "restitution");
                        collider.BelongsTo = ParseUInt32(tokens[5], lineNumber, "category mask");
                        collider.CollidesWith = ParseUInt32(tokens[6], lineNumber, "collision mask");
                        collider.IsTrigger = ParseBool(tokens[7], lineNumber);
                    }
                    else
                    {
                        RequireCount(tokens, 9u, lineNumber, "capsule collider");
                        collider.Radius = ParseFloat(tokens[2], lineNumber, "capsule radius");
                        collider.HalfHeight = ParseFloat(tokens[3], lineNumber, "capsule half height");
                        collider.Friction = ParseFloat(tokens[4], lineNumber, "friction");
                        collider.Restitution = ParseFloat(tokens[5], lineNumber, "restitution");
                        collider.BelongsTo = ParseUInt32(tokens[6], lineNumber, "category mask");
                        collider.CollidesWith = ParseUInt32(tokens[7], lineNumber, "collision mask");
                        collider.IsTrigger = ParseBool(tokens[8], lineNumber);
                    }
                    try { scene.SetCollider(*current, collider); }
                    catch (const std::exception& error)
                    { throw SceneFormatError(lineNumber, tokens[1].Column, error.what()); }
                }
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
        for (const PendingParent& relationship : pendingParents)
        {
            if (!scene.Contains(relationship.Parent))
                throw SceneFormatError(relationship.Line, relationship.Column,
                    "parent entity ID does not exist");
            try { scene.SetParent(relationship.Child, relationship.Parent); }
            catch (const std::exception& error)
            {
                throw SceneFormatError(relationship.Line, relationship.Column, error.what());
            }
        }
        return scene;
    }

    /// Output: stable ID-ordered and diff-friendly `kairo-scene 4` text.
    /// Preconditions: transforms and public components must remain valid, and
    /// every mesh/material reference must resolve with its declared asset type.
    [[nodiscard]] inline std::string SerializeScene(const Scene& scene, const kairo::assets::AssetRegistry& assets)
    {
        using namespace scene_format_detail;
        std::ostringstream output;
        output << std::setprecision(std::numeric_limits<float>::max_digits10);
        output << "kairo-scene 4\n";
        (void)scene.PrimaryCamera();
        for (const Entity entity : scene.Entities())
        {
            const auto& transform = scene.Transform(entity).Local;
            if (scene.Name(entity).Value.size() > MaxNameBytes)
                throw std::length_error("Cannot serialize an entity name exceeding 4096 bytes.");
            if (!IsFinite(transform) || !kairo::foundation::math::IsValid(transform))
                throw std::invalid_argument("Cannot serialize an invalid entity transform.");
            output << "entity " << entity.Value << ' ' << Quote(scene.Name(entity).Value) << '\n';
            if (const auto parent = scene.Parent(entity); parent.has_value())
                output << "parent " << parent->Value << '\n';
            output << "enabled " << (scene.IsEnabled(entity) ? "true" : "false") << '\n';
            output << "layer " << scene.Layer(entity) << '\n';
            for (const std::string& tag : scene.Tags(entity))
                output << "tag " << Quote(tag) << '\n';
            output << "transform " << transform.Translation.x << ' ' << transform.Translation.y << ' '
                << transform.Translation.z << ' ' << transform.Rotation.x << ' ' << transform.Rotation.y << ' '
                << transform.Rotation.z << ' ' << transform.Rotation.w << ' ' << transform.Scale.x << ' '
                << transform.Scale.y << ' ' << transform.Scale.z << '\n';
            if (scene.HasMeshRenderer(entity))
            {
                const auto& mesh = scene.MeshRenderer(entity);
                mesh.Validate();
                (void)assets.Resolve(mesh.MeshAsset);
                for (std::size_t slot = 0u; slot < mesh.MaterialSlotCount(); ++slot)
                    (void)assets.Resolve(mesh.MaterialForSlot(slot));
                output << "mesh-renderer " << mesh.MeshAsset.ID.ToString() << ' '
                    << (mesh.Visible ? "true" : "false") << ' '
                    << (mesh.CastShadows ? "true" : "false") << ' '
                    << (mesh.ReceiveShadows ? "true" : "false") << ' '
                    << mesh.RenderLayers << ' ' << mesh.MaterialSlotCount();
                for (std::size_t slot = 0u; slot < mesh.MaterialSlotCount(); ++slot)
                    output << ' ' << mesh.MaterialForSlot(slot).ID.ToString();
                output << '\n';
            }
            if (scene.HasSceneInstance(entity))
            {
                const auto& instance = scene.SceneInstance(entity);
                instance.Validate();
                (void)assets.Resolve(instance.SceneAsset);
                output << "scene-instance " << instance.SceneAsset.ID.ToString() << ' '
                    << (instance.Visible ? "true" : "false") << ' '
                    << (instance.CastShadows ? "true" : "false") << ' '
                    << (instance.ReceiveShadows ? "true" : "false") << ' '
                    << instance.RenderLayers << '\n';
            }
            if (scene.HasCamera(entity))
            {
                const auto& camera = scene.Camera(entity);
                camera.Validate();
                output << "camera " << CameraProjectionName(camera.Projection) << ' '
                    << camera.VerticalFovRadians << ' ' << camera.OrthographicSize << ' '
                    << camera.NearPlane << ' ' << camera.FarPlane << ' '
                    << camera.ExposureEV100 << ' ' << (camera.Primary ? "true" : "false") << ' '
                    << CameraClearModeName(camera.ClearMode) << ' '
                    << camera.ClearColor.x << ' ' << camera.ClearColor.y << ' '
                    << camera.ClearColor.z << ' ' << camera.ClearColor.w << ' '
                    << camera.RenderLayers << '\n';
            }
            if (scene.HasLight(entity))
            {
                const auto& light = scene.Light(entity);
                light.Validate();
                output << "light " << LightTypeName(light.Type) << ' '
                    << light.Color.x << ' ' << light.Color.y << ' ' << light.Color.z << ' '
                    << light.Intensity << ' ' << PhotometricUnitName(light.Unit) << ' '
                    << light.Range << ' ' << light.InnerConeRadians << ' '
                    << light.OuterConeRadians << ' ' << light.AreaWidth << ' '
                    << light.AreaHeight << ' ' << ShadowPolicyName(light.Shadows) << ' '
                    << light.ShadowBias << ' ' << light.ShadowNormalBias << ' '
                    << light.RenderLayers << '\n';
            }
            if (scene.HasEnvironment(entity))
            {
                const auto& environment = scene.Environment(entity);
                environment.Validate();
                if (environment.EnvironmentTexture.has_value())
                    (void)assets.Resolve(*environment.EnvironmentTexture);
                output << "environment " << (environment.Enabled ? "true" : "false") << ' '
                    << environment.Priority << ' ' << environment.BackgroundColor.x << ' '
                    << environment.BackgroundColor.y << ' ' << environment.BackgroundColor.z << ' ';
                if (environment.EnvironmentTexture.has_value())
                    output << environment.EnvironmentTexture->ID.ToString();
                else output << "none";
                output << ' ' << environment.AmbientIntensity << ' '
                    << environment.EnvironmentIntensity << ' ' << FogModeName(environment.Fog) << ' '
                    << environment.FogColor.x << ' ' << environment.FogColor.y << ' '
                    << environment.FogColor.z << ' ' << environment.FogDensity << ' '
                    << environment.FogNear << ' ' << environment.FogFar << ' '
                    << environment.ExposureEV100 << ' ' << ToneMappingName(environment.ToneMap)
                    << " global\n";
            }
            if (scene.HasLogic(entity))
            {
                const auto& logic = scene.Logic(entity);
                logic.Validate();
                (void)assets.Resolve(logic.Document);
                output << "logic " << logic.Document.ID.ToString() << ' '
                    << (logic.Enabled ? "true" : "false") << '\n';
            }
            if (scene.HasRigidBody(entity))
            {
                const auto& body = scene.RigidBody(entity);
                body.Validate();
                output << "rigid-body " << MotionName(body.Motion) << ' ' << body.Density << ' '
                    << body.GravityScale << ' ' << body.LinearDamping << ' ' << body.AngularDamping
                    << '\n';
            }
            if (scene.HasCollider(entity))
            {
                const auto& collider = scene.Collider(entity);
                collider.Validate();
                output << "collider " << ShapeName(collider.Shape) << ' ';
                if (collider.Shape == ColliderShape::Box)
                    output << collider.HalfExtents.x << ' ' << collider.HalfExtents.y << ' '
                        << collider.HalfExtents.z << ' ';
                else if (collider.Shape == ColliderShape::Sphere)
                    output << collider.Radius << ' ';
                else
                    output << collider.Radius << ' ' << collider.HalfHeight << ' ';
                output << collider.Friction << ' ' << collider.Restitution << ' '
                    << collider.BelongsTo << ' ' << collider.CollidesWith << ' '
                    << (collider.IsTrigger ? "true" : "false") << '\n';
            }
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
