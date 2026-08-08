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
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module Kairo.EngineCore.ProductionSystemsManifest;

import Kairo.EngineCore.ProductionSystems;
import Kairo.EngineCore.TextFormat;

export namespace kairo::engine
{
    inline const std::filesystem::path DefaultProductionSystemsManifestPath =
        std::filesystem::path("Config") / "Production.kproduction";

    struct ProductionAnimationKey final
    {
        std::string Channel;
        AnimationKeyframe Key;
    };

    struct ProductionAnimationDescriptor final
    {
        std::string Name;
        double Duration = 0.0;
        std::vector<ProductionAnimationKey> Keys;
    };

    struct ProductionTerrainDescriptor final
    {
        std::size_t Width = 257u;
        std::size_t Height = 257u;
        double CellSize = 1.0;
    };

    struct ProductionFoliageDescriptor final
    {
        std::size_t InstanceCount = 0u;
        std::uint64_t Seed = 1u;
    };

    struct ProductionParticleDescriptor final
    {
        std::size_t Capacity = 4096u;
    };

    struct ProductionClothDescriptor final
    {
        std::size_t MaximumParticles = 4096u;
        std::size_t ConstraintIterations = 4u;
    };

    struct ProductionFluidDescriptor final
    {
        std::size_t Width = 128u;
        std::size_t Height = 128u;
        double Diffusion = 0.05;
    };

    struct ProductionStreamingDescriptor final
    {
        double CellSize = 128.0;
        std::int32_t Radius = 2;
    };


    class ProductionSystemsManifestFormatError final : public std::runtime_error
    {
    public:
        ProductionSystemsManifestFormatError(std::size_t line, std::size_t column, std::string message)
            : std::runtime_error("Kairo production manifest " + std::to_string(line) + ":" +
                std::to_string(column) + ": " + message), Line(line), Column(column) {}
        std::size_t Line;
        std::size_t Column;
    };

    struct ProductionSystemsManifest final
    {
        std::vector<ProductionAnimationDescriptor> Animations;
        std::optional<ProductionTerrainDescriptor> Terrain;
        std::optional<ProductionFoliageDescriptor> Foliage;
        std::optional<ProductionParticleDescriptor> Particles;
        std::optional<ProductionClothDescriptor> Cloth;
        std::optional<ProductionFluidDescriptor> Fluid;
        std::optional<ProductionStreamingDescriptor> Streaming;
    };

    struct ProductionPerformanceBudget final
    {
        std::size_t MaximumAnimationKeys = 1'000'000u;
        std::size_t MaximumTerrainCells = 4'194'304u;
        std::size_t MaximumFoliageInstances = 1'000'000u;
        std::size_t MaximumParticles = 1'000'000u;
        std::size_t MaximumClothParticles = 250'000u;
        std::size_t MaximumFluidCells = 1'048'576u;
        std::size_t MaximumStreamingCells = 4096u;
        std::size_t MaximumEstimatedFrameOperations = 8'000'000u;
    };

    struct ProductionWorkloadEstimate final
    {
        std::size_t AnimationKeys = 0u;
        std::size_t TerrainCells = 0u;
        std::size_t FoliageInstances = 0u;
        std::size_t ParticleCapacity = 0u;
        std::size_t ClothParticles = 0u;
        std::size_t FluidCells = 0u;
        std::size_t StreamingCells = 0u;
        std::size_t EstimatedFrameOperations = 0u;
    };

    namespace production_manifest_detail
    {
        constexpr std::size_t MaximumBytes = 32u * 1024u * 1024u;

        [[nodiscard]] inline std::size_t CheckedProduct(std::size_t a, std::size_t b,
            std::string_view what)
        {
            if (a != 0u && b > std::numeric_limits<std::size_t>::max() / a)
                throw std::length_error(std::string(what) + " size overflows.");
            return a * b;
        }

        template<class T>
        [[nodiscard]] T ParseInteger(const FormatToken& token,
            std::size_t line, std::string_view field)
        {
            T value{};
            const auto [end, error] = std::from_chars(
                token.Text.data(), token.Text.data() + token.Text.size(), value);
            if (error != std::errc{} || end != token.Text.data() + token.Text.size())
                throw std::invalid_argument("Production manifest line " + std::to_string(line) +
                    " has invalid " + std::string(field) + ".");
            stream >> std::ws;
            if (!stream.eof())
                throw std::invalid_argument("Production manifest line " + std::to_string(line) +
                    " has invalid " + std::string(field) + ".");
            return value;
        }

        [[nodiscard]] inline double ParseDouble(const FormatToken& token,
            std::size_t line, std::string_view field)
        {
            std::istringstream stream(token.Text);
            stream.imbue(std::locale::classic());
            double value = 0.0;
            stream >> value;
            if (stream.fail() || !std::isfinite(value))
                throw std::invalid_argument("Production manifest line " + std::to_string(line) +
                    " has invalid " + std::string(field) + ".");
            stream >> std::ws;
            if (!stream.eof())
                throw std::invalid_argument("Production manifest line " + std::to_string(line) +
                    " has invalid " + std::string(field) + ".");
            return value;
        }
    }

    [[nodiscard]] inline ProductionWorkloadEstimate EstimateProductionWorkload(
        const ProductionSystemsManifest& manifest)
    {
        using namespace production_manifest_detail;
        ProductionWorkloadEstimate result;
        for (const auto& animation : manifest.Animations)
            result.AnimationKeys += animation.Keys.size();
        if (manifest.Terrain)
            result.TerrainCells = CheckedProduct(manifest.Terrain->Width,
                manifest.Terrain->Height, "terrain");
        if (manifest.Foliage) result.FoliageInstances = manifest.Foliage->InstanceCount;
        if (manifest.Particles) result.ParticleCapacity = manifest.Particles->Capacity;
        if (manifest.Cloth) result.ClothParticles = manifest.Cloth->MaximumParticles;
        if (manifest.Fluid)
            result.FluidCells = CheckedProduct(manifest.Fluid->Width,
                manifest.Fluid->Height, "fluid");
        if (manifest.Streaming)
        {
            const std::size_t diameter = static_cast<std::size_t>(manifest.Streaming->Radius * 2 + 1);
            result.StreamingCells = CheckedProduct(diameter, diameter, "streaming");
        }
        result.EstimatedFrameOperations = result.AnimationKeys + result.FoliageInstances +
            result.ParticleCapacity + result.ClothParticles *
                (manifest.Cloth ? manifest.Cloth->ConstraintIterations : 0u) +
            result.FluidCells * 5u + result.StreamingCells;
        return result;
    }

    inline void ValidateProductionSystemsManifest(
        const ProductionSystemsManifest& manifest,
        const ProductionPerformanceBudget& budget = {})
    {
        std::size_t keys = 0u;
        for (const auto& animation : manifest.Animations)
        {
            if (animation.Name.empty() || !std::isfinite(animation.Duration) || animation.Duration < 0.0)
                throw std::invalid_argument("Production animation descriptor is invalid.");
            keys += animation.Keys.size();
            for (const auto& key : animation.Keys)
            {
                if (key.Channel.empty() || !std::isfinite(key.Key.Time) || key.Key.Time < 0.0 ||
                    key.Key.Time > animation.Duration)
                    throw std::invalid_argument("Production animation key is invalid.");
            }
        }
        if (keys > budget.MaximumAnimationKeys)
            throw std::length_error("Production animation key budget exceeded.");
        if (manifest.Terrain)
        {
            if (manifest.Terrain->Width < 2u || manifest.Terrain->Height < 2u ||
                !std::isfinite(manifest.Terrain->CellSize) || manifest.Terrain->CellSize <= 0.0)
                throw std::invalid_argument("Production terrain descriptor is invalid.");
        }
        if (manifest.Fluid && (manifest.Fluid->Width < 3u || manifest.Fluid->Height < 3u ||
            !std::isfinite(manifest.Fluid->Diffusion) || manifest.Fluid->Diffusion < 0.0 ||
            manifest.Fluid->Diffusion > 0.25))
            throw std::invalid_argument("Production fluid descriptor is invalid.");
        if (manifest.Streaming && (!std::isfinite(manifest.Streaming->CellSize) ||
            manifest.Streaming->CellSize <= 0.0 || manifest.Streaming->Radius < 0 ||
            manifest.Streaming->Radius > 64))
            throw std::invalid_argument("Production streaming descriptor is invalid.");
        if (manifest.Cloth && (manifest.Cloth->MaximumParticles == 0u ||
            manifest.Cloth->ConstraintIterations == 0u || manifest.Cloth->ConstraintIterations > 128u))
            throw std::invalid_argument("Production cloth descriptor is invalid.");
        if (manifest.Particles && manifest.Particles->Capacity == 0u)
            throw std::invalid_argument("Production particle capacity cannot be zero.");

        const auto estimate = EstimateProductionWorkload(manifest);
        if (estimate.TerrainCells > budget.MaximumTerrainCells ||
            estimate.FoliageInstances > budget.MaximumFoliageInstances ||
            estimate.ParticleCapacity > budget.MaximumParticles ||
            estimate.ClothParticles > budget.MaximumClothParticles ||
            estimate.FluidCells > budget.MaximumFluidCells ||
            estimate.StreamingCells > budget.MaximumStreamingCells ||
            estimate.EstimatedFrameOperations > budget.MaximumEstimatedFrameOperations)
            throw std::length_error("Production manifest exceeds its configured performance budget.");
    }

    [[nodiscard]] inline std::string SerializeProductionSystemsManifest(
        const ProductionSystemsManifest& manifest)
    {
        ValidateProductionSystemsManifest(manifest);
        std::ostringstream output;
        output.imbue(std::locale::classic());
        output << std::setprecision(std::numeric_limits<double>::max_digits10);
        output << "kairo-production 1\n";
        for (const auto& animation : manifest.Animations)
        {
            output << "animation " << QuoteFormatText(animation.Name) << ' '
                << animation.Duration << '\n';
            for (const auto& key : animation.Keys)
                output << "key " << QuoteFormatText(animation.Name) << ' '
                    << QuoteFormatText(key.Channel) << ' ' << key.Key.Time << ' '
                    << key.Key.Translation.X << ' ' << key.Key.Translation.Y << ' '
                    << key.Key.Translation.Z << '\n';
        }
        if (manifest.Terrain) output << "terrain " << manifest.Terrain->Width << ' '
            << manifest.Terrain->Height << ' ' << manifest.Terrain->CellSize << '\n';
        if (manifest.Foliage) output << "foliage " << manifest.Foliage->InstanceCount << ' '
            << manifest.Foliage->Seed << '\n';
        if (manifest.Particles) output << "particles " << manifest.Particles->Capacity << '\n';
        if (manifest.Cloth) output << "cloth " << manifest.Cloth->MaximumParticles << ' '
            << manifest.Cloth->ConstraintIterations << '\n';
        if (manifest.Fluid) output << "fluid " << manifest.Fluid->Width << ' '
            << manifest.Fluid->Height << ' ' << manifest.Fluid->Diffusion << '\n';
        if (manifest.Streaming) output << "streaming " << manifest.Streaming->CellSize << ' '
            << manifest.Streaming->Radius << '\n';
        output << "end\n";
        const auto text = output.str();
        if (text.size() > production_manifest_detail::MaximumBytes)
            throw std::length_error("Production manifest exceeds 32 MiB.");
        return text;
    }

    [[nodiscard]] inline ProductionSystemsManifest ParseProductionSystemsManifest(
        std::string_view text)
    {
        using namespace production_manifest_detail;
        if (text.size() > MaximumBytes) throw std::length_error("Production manifest exceeds 32 MiB.");
        ProductionSystemsManifest manifest;
        std::istringstream input{ std::string(text) };
        std::string lineText;
        std::size_t line = 0u;
        bool header = false;
        bool ended = false;
        while (std::getline(input, lineText))
        {
            ++line;
            const auto tokens = TokenizeFormatLine<ProductionSystemsManifestFormatError>(lineText, line);
            if (tokens.empty()) continue;
            if (!header)
            {
                if (tokens.size() != 2u || tokens[0].Text != "kairo-production" || tokens[1].Text != "1")
                    throw std::invalid_argument("Production manifest has an invalid header.");
                header = true;
                continue;
            }
            const auto& command = tokens[0].Text;
            if (command == "end") { ended = true; break; }
            if (command == "animation")
            {
                if (tokens.size() != 3u) throw std::invalid_argument("Production animation line is invalid.");
                manifest.Animations.push_back({ tokens[1].Text,
                    ParseDouble(tokens[2], line, "animation duration"), {} });
            }
            else if (command == "key")
            {
                if (tokens.size() != 7u) throw std::invalid_argument("Production animation key line is invalid.");
                auto found = std::find_if(manifest.Animations.begin(), manifest.Animations.end(),
                    [&](const auto& animation) { return animation.Name == tokens[1].Text; });
                if (found == manifest.Animations.end())
                    throw std::invalid_argument("Production animation key references an unknown clip.");
                found->Keys.push_back({ tokens[2].Text, {
                    ParseDouble(tokens[3], line, "key time"), {
                        ParseDouble(tokens[4], line, "key x"),
                        ParseDouble(tokens[5], line, "key y"),
                        ParseDouble(tokens[6], line, "key z") } } });
            }
            else if (command == "terrain")
            {
                if (tokens.size() != 4u || manifest.Terrain) throw std::invalid_argument("Production terrain line is invalid.");
                manifest.Terrain = ProductionTerrainDescriptor{
                    ParseInteger<std::size_t>(tokens[1], line, "terrain width"),
                    ParseInteger<std::size_t>(tokens[2], line, "terrain height"),
                    ParseDouble(tokens[3], line, "terrain cell size") };
            }
            else if (command == "foliage")
            {
                if (tokens.size() != 3u || manifest.Foliage) throw std::invalid_argument("Production foliage line is invalid.");
                manifest.Foliage = ProductionFoliageDescriptor{
                    ParseInteger<std::size_t>(tokens[1], line, "foliage count"),
                    ParseInteger<std::uint64_t>(tokens[2], line, "foliage seed") };
            }
            else if (command == "particles")
            {
                if (tokens.size() != 2u || manifest.Particles) throw std::invalid_argument("Production particle line is invalid.");
                manifest.Particles = ProductionParticleDescriptor{
                    ParseInteger<std::size_t>(tokens[1], line, "particle capacity") };
            }
            else if (command == "cloth")
            {
                if (tokens.size() != 3u || manifest.Cloth) throw std::invalid_argument("Production cloth line is invalid.");
                manifest.Cloth = ProductionClothDescriptor{
                    ParseInteger<std::size_t>(tokens[1], line, "cloth particle budget"),
                    ParseInteger<std::size_t>(tokens[2], line, "cloth iterations") };
            }
            else if (command == "fluid")
            {
                if (tokens.size() != 4u || manifest.Fluid) throw std::invalid_argument("Production fluid line is invalid.");
                manifest.Fluid = ProductionFluidDescriptor{
                    ParseInteger<std::size_t>(tokens[1], line, "fluid width"),
                    ParseInteger<std::size_t>(tokens[2], line, "fluid height"),
                    ParseDouble(tokens[3], line, "fluid diffusion") };
            }
            else if (command == "streaming")
            {
                if (tokens.size() != 3u || manifest.Streaming) throw std::invalid_argument("Production streaming line is invalid.");
                manifest.Streaming = ProductionStreamingDescriptor{
                    ParseDouble(tokens[1], line, "streaming cell size"),
                    ParseInteger<std::int32_t>(tokens[2], line, "streaming radius") };
            }
            else throw std::invalid_argument("Production manifest contains an unknown statement.");
        }
        if (!header || !ended) throw std::invalid_argument("Production manifest is incomplete.");
        std::string trailing;
        while (std::getline(input, trailing))
            if (!TokenizeFormatLine<ProductionSystemsManifestFormatError>(trailing, ++line).empty())
                throw std::invalid_argument("Production manifest contains data after end.");
        ValidateProductionSystemsManifest(manifest);
        return manifest;
    }

    inline void SaveProductionSystemsManifest(const std::filesystem::path& path,
        const ProductionSystemsManifest& manifest)
    {
        SaveTextFileAtomically(path, SerializeProductionSystemsManifest(manifest),
            "Kairo production systems manifest");
    }

    [[nodiscard]] inline ProductionSystemsManifest LoadProductionSystemsManifest(
        const std::filesystem::path& path)
    {
        return ParseProductionSystemsManifest(LoadBoundedTextFile(path,
            production_manifest_detail::MaximumBytes, "Kairo production systems manifest"));
    }

    [[nodiscard]] inline AnimationClip BuildAnimationClip(
        const ProductionAnimationDescriptor& descriptor)
    {
        AnimationClip clip(descriptor.Duration);
        for (const auto& key : descriptor.Keys) clip.AddKey(key.Channel, key.Key);
        return clip;
    }
}
