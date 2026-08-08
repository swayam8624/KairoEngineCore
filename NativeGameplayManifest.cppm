module;

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

export module Kairo.EngineCore.NativeGameplayManifest;

import Kairo.EngineCore.Entity;
import Kairo.EngineCore.NativeGameplay;
import Kairo.EngineCore.Scene;
import Kairo.EngineCore.TextFormat;
import Kairo.Foundation.Math;

export namespace kairo::engine
{
    inline const std::filesystem::path DefaultNativeGameplayManifestPath =
        std::filesystem::path("Config") / "NativeGameplay.knative";

    struct NativeGameplayManifest final
    {
        std::vector<NativeGameplayAttachment> Attachments;
    };

    class NativeGameplayManifestFormatError final : public std::runtime_error
    {
    public:
        NativeGameplayManifestFormatError(std::size_t line, std::size_t column, std::string message)
            : std::runtime_error("Kairo native gameplay manifest " + std::to_string(line) + ":" +
                std::to_string(column) + ": " + message), Line(line), Column(column) {}
        std::size_t Line;
        std::size_t Column;
    };

    namespace native_manifest_detail
    {
        constexpr std::size_t MaximumBytes = 8u * 1024u * 1024u;
        constexpr std::size_t MaximumAttachments = 100'000u;
        constexpr std::size_t MaximumPropertiesPerAttachment = 1024u;

        using Token = FormatToken;

        [[nodiscard]] inline std::vector<Token> Tokenize(std::string_view line, std::size_t lineNumber)
        {
            return TokenizeFormatLine<NativeGameplayManifestFormatError>(line, lineNumber);
        }

        inline void RequireCount(const std::vector<Token>& tokens, std::size_t expected,
            std::size_t line, std::string_view statement)
        {
            RequireTokenCount<NativeGameplayManifestFormatError>(tokens, expected, line, statement);
        }

        [[nodiscard]] inline std::uint64_t ParseUInt64(const Token& token,
            std::size_t line, std::string_view field)
        {
            std::uint64_t value = 0u;
            const auto [end, error] = std::from_chars(
                token.Text.data(), token.Text.data() + token.Text.size(), value);
            if (error != std::errc{} || end != token.Text.data() + token.Text.size())
                throw NativeGameplayManifestFormatError(line, token.Column,
                    std::string(field) + " must be an unsigned 64-bit integer");
            return value;
        }

        [[nodiscard]] inline double ParseDouble(const Token& token,
            std::size_t line, std::string_view field)
        {
            std::istringstream stream(token.Text);
            stream.imbue(std::locale::classic());
            double value = 0.0;
            stream >> value;
            stream >> std::ws;
            if (!stream || !stream.eof() || !std::isfinite(value))
                throw NativeGameplayManifestFormatError(line, token.Column,
                    std::string(field) + " must be a finite decimal number");
            return value;
        }

        [[nodiscard]] inline bool ParseBool(const Token& token, std::size_t line)
        {
            if (token.Text == "true") return true;
            if (token.Text == "false") return false;
            throw NativeGameplayManifestFormatError(line, token.Column,
                "boolean value must be true or false");
        }

        [[nodiscard]] inline std::string ValueTypeName(const NativeGameplayValue& value)
        {
            switch (value.index())
            {
                case 0u: return "bool";
                case 1u: return "number";
                case 2u: return "string";
                case 3u: return "vec3";
                case 4u: return "entity";
                default: throw std::invalid_argument("Native gameplay property value type is invalid.");
            }
        }

        [[nodiscard]] inline std::string SerializeValue(const NativeGameplayValue& value)
        {
            std::ostringstream output;
            output.imbue(std::locale::classic());
            output << std::setprecision(std::numeric_limits<double>::max_digits10);
            switch (value.index())
            {
                case 0u:
                    output << (std::get<bool>(value) ? "true" : "false");
                    break;
                case 1u:
                    output << std::get<double>(value);
                    break;
                case 2u:
                    output << QuoteFormatText(std::get<std::string>(value));
                    break;
                case 3u:
                {
                    const auto vector = std::get<kairo::foundation::math::Vec3d>(value);
                    output << vector.x << ' ' << vector.y << ' ' << vector.z;
                    break;
                }
                case 4u:
                    output << std::get<Entity>(value).Value;
                    break;
                default:
                    throw std::invalid_argument("Native gameplay property value type is invalid.");
            }
            return output.str();
        }

        [[nodiscard]] inline bool SameValueType(
            const NativeGameplayValue& a, const NativeGameplayValue& b) noexcept
        {
            return a.index() == b.index();
        }
    }

    inline void ValidateNativeGameplayManifest(
        const NativeGameplayManifest& manifest,
        const Scene& scene,
        const NativeGameplayRegistry* registry = nullptr)
    {
        using namespace native_manifest_detail;
        if (manifest.Attachments.size() > MaximumAttachments)
            throw std::length_error("Native gameplay manifest exceeds 100,000 attachments.");

        std::set<std::pair<std::uint64_t, std::string>> unique;
        for (const auto& attachment : manifest.Attachments)
        {
            if (!attachment.Target || !scene.Contains(attachment.Target))
                throw std::invalid_argument("Native gameplay attachment targets a missing scene entity.");
            if (attachment.TypeName.empty())
                throw std::invalid_argument("Native gameplay attachment type name cannot be empty.");
            if (attachment.Properties.size() > MaximumPropertiesPerAttachment)
                throw std::length_error("Native gameplay attachment exceeds 1024 property overrides.");
            if (!unique.emplace(attachment.Target.Value, attachment.TypeName).second)
                throw std::invalid_argument("Native gameplay manifest contains a duplicate entity/type attachment.");

            if (registry == nullptr) continue;
            const auto& type = registry->Type(attachment.TypeName);
            std::map<std::string, const NativeGameplayProperty*> properties;
            for (const auto& property : type.Properties) properties.emplace(property.Name, &property);
            for (const auto& [name, value] : attachment.Properties)
            {
                const auto found = properties.find(name);
                if (found == properties.end())
                    throw std::invalid_argument("Native gameplay manifest overrides an unknown reflected property.");
                if (!SameValueType(value, found->second->DefaultValue))
                    throw std::invalid_argument("Native gameplay manifest property override has the wrong type.");
            }
        }
    }

    [[nodiscard]] inline std::string SerializeNativeGameplayManifest(
        const NativeGameplayManifest& manifest)
    {
        using namespace native_manifest_detail;
        std::vector<const NativeGameplayAttachment*> ordered;
        ordered.reserve(manifest.Attachments.size());
        for (const auto& attachment : manifest.Attachments) ordered.push_back(&attachment);
        std::sort(ordered.begin(), ordered.end(), [](const auto* a, const auto* b) {
            if (a->Target.Value != b->Target.Value) return a->Target.Value < b->Target.Value;
            return a->TypeName < b->TypeName;
        });

        std::string output = "kairo-native-gameplay 1\n";
        for (const auto* attachment : ordered)
        {
            if (!attachment->Target || attachment->TypeName.empty())
                throw std::invalid_argument("Native gameplay attachment is invalid.");
            if (attachment->Properties.size() > MaximumPropertiesPerAttachment)
                throw std::length_error("Native gameplay attachment exceeds 1024 property overrides.");
            output += "behaviour " + std::to_string(attachment->Target.Value) + " " +
                QuoteFormatText(attachment->TypeName) + " " +
                (attachment->Enabled ? "true" : "false") + "\n";
            for (const auto& [name, value] : attachment->Properties)
            {
                if (name.empty()) throw std::invalid_argument("Native gameplay property name cannot be empty.");
                output += "property " + QuoteFormatText(name) + " " + ValueTypeName(value) + " " +
                    SerializeValue(value) + "\n";
            }
            output += "end-behaviour\n";
        }
        if (output.size() > MaximumBytes)
            throw std::length_error("Native gameplay manifest exceeds its 8 MiB safety limit.");
        return output;
    }

    [[nodiscard]] inline NativeGameplayManifest ParseNativeGameplayManifest(std::string_view source)
    {
        using namespace native_manifest_detail;
        if (source.size() > MaximumBytes)
            throw std::length_error("Native gameplay manifest exceeds its 8 MiB safety limit.");

        NativeGameplayManifest manifest;
        std::optional<NativeGameplayAttachment> current;
        bool headerSeen = false;
        std::istringstream input{ std::string(source) };
        std::string lineText;
        std::size_t lineNumber = 0u;
        while (std::getline(input, lineText))
        {
            ++lineNumber;
            const auto tokens = Tokenize(lineText, lineNumber);
            if (tokens.empty()) continue;
            if (!headerSeen)
            {
                RequireCount(tokens, 2u, lineNumber, "kairo-native-gameplay header");
                if (tokens[0].Text != "kairo-native-gameplay" || tokens[1].Text != "1")
                    throw NativeGameplayManifestFormatError(lineNumber, tokens[0].Column,
                        "expected 'kairo-native-gameplay 1' header");
                headerSeen = true;
                continue;
            }

            if (tokens[0].Text == "behaviour")
            {
                RequireCount(tokens, 4u, lineNumber, tokens[0].Text);
                if (current.has_value())
                    throw NativeGameplayManifestFormatError(lineNumber, tokens[0].Column,
                        "nested behaviour statement is not allowed");
                if (manifest.Attachments.size() >= MaximumAttachments)
                    throw std::length_error("Native gameplay manifest exceeds 100,000 attachments.");
                NativeGameplayAttachment attachment;
                const auto entityValue = ParseUInt64(tokens[1], lineNumber, "entity");
                if (entityValue > std::numeric_limits<std::uint32_t>::max())
                    throw NativeGameplayManifestFormatError(lineNumber, tokens[1].Column, "entity exceeds 32-bit scene identity range");
                attachment.Target = Entity{ static_cast<std::uint32_t>(entityValue) };
                attachment.TypeName = tokens[2].Text;
                attachment.Enabled = ParseBool(tokens[3], lineNumber);
                if (!attachment.Target || attachment.TypeName.empty())
                    throw NativeGameplayManifestFormatError(lineNumber, tokens[1].Column,
                        "behaviour entity and type name must be valid");
                current = std::move(attachment);
            }
            else if (tokens[0].Text == "property")
            {
                if (!current.has_value())
                    throw NativeGameplayManifestFormatError(lineNumber, tokens[0].Column,
                        "property statement requires an open behaviour");
                if (current->Properties.size() >= MaximumPropertiesPerAttachment)
                    throw std::length_error("Native gameplay attachment exceeds 1024 property overrides.");
                if (tokens.size() < 4u)
                    throw NativeGameplayManifestFormatError(lineNumber, tokens[0].Column,
                        "property statement is incomplete");
                const std::string name = tokens[1].Text;
                if (name.empty() || current->Properties.contains(name))
                    throw NativeGameplayManifestFormatError(lineNumber, tokens[1].Column,
                        "property name is empty or duplicated");
                const std::string& type = tokens[2].Text;
                NativeGameplayValue value;
                if (type == "bool")
                {
                    RequireCount(tokens, 4u, lineNumber, tokens[0].Text);
                    value = ParseBool(tokens[3], lineNumber);
                }
                else if (type == "number")
                {
                    RequireCount(tokens, 4u, lineNumber, tokens[0].Text);
                    value = ParseDouble(tokens[3], lineNumber, "number property");
                }
                else if (type == "string")
                {
                    RequireCount(tokens, 4u, lineNumber, tokens[0].Text);
                    value = tokens[3].Text;
                }
                else if (type == "vec3")
                {
                    RequireCount(tokens, 6u, lineNumber, tokens[0].Text);
                    value = kairo::foundation::math::Vec3d{
                        ParseDouble(tokens[3], lineNumber, "vector x"),
                        ParseDouble(tokens[4], lineNumber, "vector y"),
                        ParseDouble(tokens[5], lineNumber, "vector z") };
                }
                else if (type == "entity")
                {
                    RequireCount(tokens, 4u, lineNumber, tokens[0].Text);
                    const auto entityValue = ParseUInt64(tokens[3], lineNumber, "entity property");
                    if (entityValue > std::numeric_limits<std::uint32_t>::max())
                        throw NativeGameplayManifestFormatError(lineNumber, tokens[3].Column, "entity property exceeds 32-bit scene identity range");
                    value = Entity{ static_cast<std::uint32_t>(entityValue) };
                }
                else
                {
                    throw NativeGameplayManifestFormatError(lineNumber, tokens[2].Column,
                        "unknown native gameplay property value type");
                }
                current->Properties.emplace(name, std::move(value));
            }
            else if (tokens[0].Text == "end-behaviour")
            {
                RequireCount(tokens, 1u, lineNumber, tokens[0].Text);
                if (!current.has_value())
                    throw NativeGameplayManifestFormatError(lineNumber, tokens[0].Column,
                        "end-behaviour has no matching behaviour");
                manifest.Attachments.push_back(std::move(*current));
                current.reset();
            }
            else
            {
                throw NativeGameplayManifestFormatError(lineNumber, tokens[0].Column,
                    "unknown statement '" + tokens[0].Text + "'");
            }
        }
        if (!headerSeen)
            throw NativeGameplayManifestFormatError(1u, 1u, "missing native gameplay header");
        if (current.has_value())
            throw NativeGameplayManifestFormatError(lineNumber + 1u, 1u,
                "unterminated behaviour block");
        return manifest;
    }

    [[nodiscard]] inline NativeGameplayManifest LoadNativeGameplayManifest(
        const std::filesystem::path& path)
    {
        return ParseNativeGameplayManifest(
            LoadBoundedTextFile(path, native_manifest_detail::MaximumBytes,
                "Kairo native gameplay manifest"));
    }

    inline void SaveNativeGameplayManifest(
        const std::filesystem::path& path,
        const NativeGameplayManifest& manifest)
    {
        SaveTextFileAtomically(path, SerializeNativeGameplayManifest(manifest),
            "Kairo native gameplay manifest");
    }

    inline void AttachNativeGameplayManifest(
        NativeGameplayRuntime& runtime,
        const NativeGameplayManifest& manifest,
        const Scene& scene,
        const NativeGameplayRegistry& registry)
    {
        ValidateNativeGameplayManifest(manifest, scene, &registry);
        for (const auto& attachment : manifest.Attachments)
            runtime.Attach(attachment);
    }
}
