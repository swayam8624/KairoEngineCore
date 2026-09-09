module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

export module Kairo.EngineCore.SceneComposition;

export import Kairo.EngineCore.Entity;
export import Kairo.EngineCore.Scene;

export namespace kairo::engine
{
    struct SceneAppendResult final
    {
        std::vector<std::pair<Entity, Entity>> EntityMap;

        [[nodiscard]] std::optional<Entity> Resolve(Entity source) const noexcept
        {
            const auto found = std::ranges::find_if(EntityMap,
                [source](const auto& mapping) { return mapping.first == source; });
            return found == EntityMap.end()
                ? std::nullopt
                : std::optional<Entity>{ found->second };
        }

        [[nodiscard]] std::vector<Entity> DestinationEntities() const
        {
            std::vector<Entity> result;
            result.reserve(EntityMap.size());
            for (const auto& [source, destination] : EntityMap)
            {
                (void)source;
                result.push_back(destination);
            }
            return result;
        }

        [[nodiscard]] bool Empty() const noexcept { return EntityMap.empty(); }
        [[nodiscard]] std::size_t Size() const noexcept { return EntityMap.size(); }
    };

    namespace scene_composition_detail
    {
        [[nodiscard]] inline Entity ResolveRequired(
            const std::unordered_map<std::uint32_t, Entity>& mapping,
            Entity source)
        {
            const auto found = mapping.find(source.Value);
            if (found == mapping.end())
                throw std::logic_error(
                    "Scene composition source entity is missing from its remap table.");
            return found->second;
        }

        inline void CopyComponents(
            Scene& destination,
            Entity destinationEntity,
            const Scene& source,
            Entity sourceEntity)
        {
            if (source.HasMeshRenderer(sourceEntity))
                destination.SetMeshRenderer(destinationEntity,
                    source.MeshRenderer(sourceEntity));
            if (source.HasSceneInstance(sourceEntity))
                destination.SetSceneInstance(destinationEntity,
                    source.SceneInstance(sourceEntity));
            if (source.HasCamera(sourceEntity))
                destination.SetCamera(destinationEntity,
                    source.Camera(sourceEntity));
            if (source.HasLight(sourceEntity))
                destination.SetLight(destinationEntity,
                    source.Light(sourceEntity));
            if (source.HasEnvironment(sourceEntity))
                destination.SetEnvironment(destinationEntity,
                    source.Environment(sourceEntity));
            if (source.HasLogic(sourceEntity))
                destination.SetLogic(destinationEntity,
                    source.Logic(sourceEntity));
            if (source.HasRigidBody(sourceEntity))
                destination.SetRigidBody(destinationEntity,
                    source.RigidBody(sourceEntity));
            if (source.HasCollider(sourceEntity))
                destination.SetCollider(destinationEntity,
                    source.Collider(sourceEntity));
            if (source.HasAudioEmitter(sourceEntity))
                destination.SetAudioEmitter(destinationEntity,
                    source.AudioEmitter(sourceEntity));
            if (source.HasAudioListener(sourceEntity))
                destination.SetAudioListener(destinationEntity,
                    source.AudioListener(sourceEntity));
        }
    }

    /// Appends one complete scene fragment into an existing scene without
    /// preserving source-local entity IDs. The operation is transactional: all
    /// entities/components/hierarchy are first applied to a copy and the live
    /// destination changes only after the entire fragment validates.
    ///
    /// The returned source->destination mapping is the ownership token used by
    /// large-world streaming and prefab-like runtime systems to retain exact
    /// identity for later lookup or removal.
    [[nodiscard]] inline SceneAppendResult AppendScene(
        Scene& destination,
        const Scene& source)
    {
        Scene candidate = destination;
        SceneAppendResult result;
        const std::vector<Entity> sourceEntities = source.Entities();
        result.EntityMap.reserve(sourceEntities.size());

        std::unordered_map<std::uint32_t, Entity> mapping;
        mapping.reserve(sourceEntities.size());

        // First create all destination records so parent references can be
        // remapped regardless of source entity ordering.
        for (const Entity sourceEntity : sourceEntities)
        {
            const Entity destinationEntity = candidate.CreateEntity(
                source.Name(sourceEntity).Value);
            mapping.emplace(sourceEntity.Value, destinationEntity);
            result.EntityMap.emplace_back(sourceEntity, destinationEntity);

            candidate.Transform(destinationEntity) = source.Transform(sourceEntity);
            candidate.SetEnabled(destinationEntity, source.IsEnabled(sourceEntity));
            candidate.SetLayer(destinationEntity, source.Layer(sourceEntity));
            for (const auto& tag : source.Tags(sourceEntity))
                candidate.AddTag(destinationEntity, tag);
        }

        // Copy optional authored components after the base records exist. A
        // conflict such as two primary cameras/listeners throws against the
        // candidate and therefore leaves the live destination untouched.
        for (const Entity sourceEntity : sourceEntities)
        {
            const Entity destinationEntity =
                scene_composition_detail::ResolveRequired(mapping, sourceEntity);
            scene_composition_detail::CopyComponents(
                candidate, destinationEntity, source, sourceEntity);
        }

        // Hierarchy comes last so children may refer to parents appearing later
        // in the source file/entity order.
        for (const Entity sourceEntity : sourceEntities)
        {
            const auto sourceParent = source.Parent(sourceEntity);
            if (!sourceParent.has_value()) continue;
            candidate.SetParent(
                scene_composition_detail::ResolveRequired(mapping, sourceEntity),
                scene_composition_detail::ResolveRequired(mapping, *sourceParent));
        }

        destination = std::move(candidate);
        return result;
    }

    /// Removes the surviving entities owned by one prior AppendScene result.
    /// External parents are safe: a streamed root may be attached under a
    /// persistent entity and will detach during removal. External children are
    /// rejected because Scene::DestroyEntity deliberately destroys descendants;
    /// accepting such a relationship would let unloading a cell silently delete
    /// persistent/gameplay-spawned entities. The safety check and destruction
    /// are transactional against a candidate scene.
    [[nodiscard]] inline std::size_t RemoveAppendedScene(
        Scene& destination,
        const SceneAppendResult& appended)
    {
        std::unordered_set<std::uint32_t> owned;
        owned.reserve(appended.EntityMap.size());
        for (const auto& [source, entity] : appended.EntityMap)
        {
            (void)source;
            if (destination.Contains(entity)) owned.insert(entity.Value);
        }
        if (owned.empty()) return 0u;

        for (const auto& [source, entity] : appended.EntityMap)
        {
            (void)source;
            if (!owned.contains(entity.Value)) continue;
            for (const Entity child : destination.Children(entity))
                if (!owned.contains(child.Value))
                    throw std::logic_error(
                        "Cannot remove an appended scene while a non-owned child is parented beneath it.");
        }

        Scene candidate = destination;
        std::vector<Entity> roots;
        roots.reserve(owned.size());
        for (const auto& [source, entity] : appended.EntityMap)
        {
            (void)source;
            if (!owned.contains(entity.Value) || !candidate.Contains(entity)) continue;
            const auto parent = candidate.Parent(entity);
            if (!parent.has_value() || !owned.contains(parent->Value))
                roots.push_back(entity);
        }

        std::ranges::sort(roots, {}, &Entity::Value);
        for (const Entity root : roots)
            if (candidate.Contains(root)) candidate.DestroyEntity(root);

        for (const auto& [source, entity] : appended.EntityMap)
        {
            (void)source;
            if (owned.contains(entity.Value) && candidate.Contains(entity))
                throw std::logic_error(
                    "Scene composition removal left an owned entity unreachable from its owned roots.");
        }

        const std::size_t removed = owned.size();
        destination = std::move(candidate);
        return removed;
    }
}
