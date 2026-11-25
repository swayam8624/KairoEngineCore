module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

export module Kairo.EngineCore.NativeGameplay;

import Kairo.EngineCore.Entity;
import Kairo.EngineCore.Event;
import Kairo.EngineCore.RuntimeWorld;
import Kairo.Foundation.Math;

export namespace kairo::engine
{
    using NativeGameplayValue = std::variant<bool, double, std::string,
        kairo::foundation::math::Vec3d, Entity>;

    enum class NativeGameplayPropertyType : std::uint8_t
    {
        Boolean,
        Number,
        String,
        Vector3,
        EntityReference
    };

    struct NativeGameplayProperty final
    {
        std::string Name;
        NativeGameplayPropertyType Type = NativeGameplayPropertyType::Number;
        NativeGameplayValue DefaultValue = 0.0;
        bool Exposed = true;
        std::optional<double> Minimum;
        std::optional<double> Maximum;
    };

    struct NativeGameplayTypeInfo final
    {
        std::string TypeName;
        std::vector<NativeGameplayProperty> Properties;
        std::uint32_t Version = 1u;
    };

    struct NativeGameplayAttachment final
    {
        Entity Target{};
        std::string TypeName;
        bool Enabled = true;
        std::map<std::string, NativeGameplayValue> Properties;
    };

    enum class NativeReloadPolicy : std::uint8_t
    {
        RestartInstances,
        PreserveCompatibleProperties
    };

    struct GameplayArchetype final
    {
        std::string Name;
        RuntimeSpawnRequest Spawn;
        std::vector<NativeGameplayAttachment> Behaviours;
    };

    // A game-defined signal is distinct from operating-system input events.
    // Optional target restricts delivery; broadcast keeps stable actor order.
    struct NativeGameplaySignal final
    {
        std::string Name;
        std::optional<Entity> Source;
        std::optional<Entity> Target;
        NativeGameplayValue Payload=false;
    };

    class NativeGameplayContext final
    {
    public:
        NativeGameplayContext(RuntimeWorld& world, Entity self, double deltaSeconds = 0.0)
            : m_World(world), m_Self(self), m_DeltaSeconds(deltaSeconds) {}

        [[nodiscard]] RuntimeWorld& World() noexcept { return m_World; }
        [[nodiscard]] const RuntimeWorld& World() const noexcept { return m_World; }
        [[nodiscard]] Entity Self() const noexcept { return m_Self; }
        [[nodiscard]] double DeltaSeconds() const noexcept { return m_DeltaSeconds; }

        [[nodiscard]] RuntimeSpawnTicket Spawn(RuntimeSpawnRequest request)
        { return m_World.QueueSpawn(std::move(request)); }
        void Destroy(Entity target) { m_World.QueueDestroy(target); }
        void SetTransform(Entity target, kairo::foundation::math::Transformf transform)
        { m_World.QueueSetTransform(target, std::move(transform)); }
        void SetEnabled(Entity target, bool enabled) { m_World.QueueSetEnabled(target, enabled); }
        void AddTag(Entity target, std::string tag) { m_World.QueueAddTag(target, std::move(tag)); }
        void RemoveTag(Entity target, std::string tag) { m_World.QueueRemoveTag(target, std::move(tag)); }

    private:
        RuntimeWorld& m_World;
        Entity m_Self{};
        double m_DeltaSeconds = 0.0;
    };

    class NativeGameplaySystem
    {
    public:
        virtual ~NativeGameplaySystem() = default;
        virtual void OnBeginPlay(NativeGameplayContext&) {}
        virtual void OnFixedUpdate(NativeGameplayContext&) {}
        virtual void OnUpdate(NativeGameplayContext&) {}
        virtual void OnEvent(NativeGameplayContext&, const Event&) {}
        virtual void OnSignal(NativeGameplayContext&, const NativeGameplaySignal&) {}
        virtual void OnEndPlay(NativeGameplayContext&) {}
        virtual void ApplyProperty(std::string_view, const NativeGameplayValue&) {}
        [[nodiscard]] virtual std::optional<NativeGameplayValue> ReadProperty(std::string_view) const
        { return std::nullopt; }
    };

    using NativeGameplayFactory = std::function<std::unique_ptr<NativeGameplaySystem>()>;

    class NativeGameplayRegistry final
    {
    public:
        void Register(NativeGameplayTypeInfo type, NativeGameplayFactory factory)
        {
            if (type.TypeName.empty() || !factory)
                throw std::invalid_argument("Native gameplay registration requires a type name and factory.");
            for (const auto& property : type.Properties) ValidateProperty(property);
            if(type.Version==0u)throw std::invalid_argument("Native gameplay type version cannot be zero.");
            std::sort(type.Properties.begin(), type.Properties.end(),
                [](const auto& a, const auto& b) { return a.Name < b.Name; });
            for(std::size_t i=1;i<type.Properties.size();++i)
                if(type.Properties[i-1].Name==type.Properties[i].Name)
                    throw std::invalid_argument("Duplicate native gameplay property.");
            const std::string key = type.TypeName;
            if (m_Types.contains(key)) throw std::invalid_argument("Native gameplay type is already registered.");
            m_Types.emplace(key, Entry{ std::move(type), std::move(factory) });
        }

        [[nodiscard]] bool Contains(std::string_view typeName) const
        { return m_Types.contains(std::string(typeName)); }

        [[nodiscard]] const NativeGameplayTypeInfo& Type(std::string_view typeName) const
        {
            const auto found = m_Types.find(std::string(typeName));
            if (found == m_Types.end()) throw std::out_of_range("Native gameplay type is not registered.");
            return found->second.Info;
        }

        [[nodiscard]] std::unique_ptr<NativeGameplaySystem> Create(std::string_view typeName) const
        {
            const auto found = m_Types.find(std::string(typeName));
            if (found == m_Types.end()) throw std::out_of_range("Native gameplay type is not registered.");
            auto instance = found->second.Factory();
            if (!instance) throw std::runtime_error("Native gameplay factory returned null.");
            return instance;
        }

        [[nodiscard]] std::vector<NativeGameplayTypeInfo> Types() const
        {
            std::vector<NativeGameplayTypeInfo> result;
            result.reserve(m_Types.size());
            for (const auto& [name, entry] : m_Types) { (void)name; result.push_back(entry.Info); }
            return result;
        }

        static void ValidateValue(const NativeGameplayProperty& property,const NativeGameplayValue& value)
        {
            if(value.index()!=property.DefaultValue.index())
                throw std::invalid_argument("Native gameplay property has the wrong type.");
            if(const auto* number=std::get_if<double>(&value))
            {
                if(!std::isfinite(*number)||(property.Minimum&&*number<*property.Minimum)||
                    (property.Maximum&&*number>*property.Maximum))
                    throw std::invalid_argument("Native gameplay number is outside its finite range.");
            }
            if(const auto* vector=std::get_if<kairo::foundation::math::Vec3d>(&value))
                if(!std::isfinite(vector->x)||!std::isfinite(vector->y)||!std::isfinite(vector->z))
                    throw std::invalid_argument("Native gameplay vector must be finite.");
        }

    private:
        struct Entry final { NativeGameplayTypeInfo Info; NativeGameplayFactory Factory; };
        std::map<std::string, Entry> m_Types;

        static void ValidateProperty(const NativeGameplayProperty& property)
        {
            if (property.Name.empty()) throw std::invalid_argument("Native gameplay property name cannot be empty.");
            const auto expected = [&]() -> std::size_t {
                switch (property.Type)
                {
                    case NativeGameplayPropertyType::Boolean: return 0u;
                    case NativeGameplayPropertyType::Number: return 1u;
                    case NativeGameplayPropertyType::String: return 2u;
                    case NativeGameplayPropertyType::Vector3: return 3u;
                    case NativeGameplayPropertyType::EntityReference: return 4u;
                }
                throw std::invalid_argument("Native gameplay property type is invalid.");
            }();
            if (property.DefaultValue.index() != expected)
                throw std::invalid_argument("Native gameplay property default does not match its declared type.");
            if ((property.Minimum&&!std::isfinite(*property.Minimum))||
                (property.Maximum&&!std::isfinite(*property.Maximum))||
                (property.Minimum&&property.Maximum&&*property.Minimum>*property.Maximum))
                throw std::invalid_argument("Native gameplay property range is invalid.");
            ValidateValue(property,property.DefaultValue);
        }
    };

    class NativeGameplayRuntime final
    {
    public:
        NativeGameplayRuntime(RuntimeWorld& world, const NativeGameplayRegistry& registry)
            : m_World(world), m_Registry(registry) {}

        void Attach(NativeGameplayAttachment attachment)
        {
            if (!m_World.Snapshot().Contains(attachment.Target))
                throw std::out_of_range("Native gameplay attachment targets a missing entity.");
            const auto& type = m_Registry.Type(attachment.TypeName);
            Instance instance;
            instance.Attachment = std::move(attachment);
            instance.System = m_Registry.Create(instance.Attachment.TypeName);
            ApplyDefaultsAndOverrides(type, instance);
            m_Instances.push_back(std::move(instance));
            SortInstances();
        }

        [[nodiscard]] std::size_t InstanceCount() const noexcept { return m_Instances.size(); }

        void BeginPlay()
        {
            if (m_Playing) return;
            m_Playing = true;
            DispatchLifecycle([](NativeGameplaySystem& system, NativeGameplayContext& context) {
                system.OnBeginPlay(context);
            }, 0.0);
        }

        void FixedUpdate(double deltaSeconds)
        {
            RequireDelta(deltaSeconds);
            RequirePlaying();
            DispatchLifecycle([](NativeGameplaySystem& system, NativeGameplayContext& context) {
                system.OnFixedUpdate(context);
            }, deltaSeconds);
        }

        void Update(double deltaSeconds)
        {
            RequireDelta(deltaSeconds);
            RequirePlaying();
            DispatchLifecycle([](NativeGameplaySystem& system, NativeGameplayContext& context) {
                system.OnUpdate(context);
            }, deltaSeconds);
        }

        void DispatchEvent(const Event& event)
        {
            RequirePlaying();
            for (auto& instance : m_Instances)
            {
                if (!instance.Attachment.Enabled || !m_World.Snapshot().Contains(instance.Attachment.Target)) continue;
                NativeGameplayContext context(m_World, instance.Attachment.Target);
                instance.System->OnEvent(context, event);
            }
            CommitIfNeeded();
        }

        void DispatchSignal(const NativeGameplaySignal& signal)
        {
            RequirePlaying();
            if(signal.Name.empty()||signal.Name.size()>256u)
                throw std::invalid_argument("Native gameplay signal name is invalid.");
            if(signal.Source&&!m_World.Snapshot().Contains(*signal.Source))
                throw std::out_of_range("Native gameplay signal source is missing.");
            if(signal.Target&&!m_World.Snapshot().Contains(*signal.Target))
                throw std::out_of_range("Native gameplay signal target is missing.");
            for(auto& instance:m_Instances)
            {
                const auto actor=instance.Attachment.Target;
                if(!instance.Attachment.Enabled||!m_World.Snapshot().Contains(actor)||
                    (signal.Target&&*signal.Target!=actor))continue;
                NativeGameplayContext context(m_World,actor);
                instance.System->OnSignal(context,signal);
            }
            CommitIfNeeded();
        }

        void EndPlay()
        {
            if (!m_Playing) return;
            DispatchLifecycle([](NativeGameplaySystem& system, NativeGameplayContext& context) {
                system.OnEndPlay(context);
            }, 0.0);
            m_Playing = false;
        }

        [[nodiscard]] RuntimeSpawnTicket SpawnArchetype(const GameplayArchetype& archetype)
        {
            if (archetype.Name.empty()) throw std::invalid_argument("Gameplay archetype name cannot be empty.");
            return m_World.QueueSpawn(archetype.Spawn);
        }

        void Reload(const NativeGameplayRegistry& replacement, NativeReloadPolicy policy)
        {
            // Build every replacement before publishing any. A throwing
            // factory or incompatible property cannot partially swap systems.
            auto registryCandidate=replacement;
            std::vector<Instance> candidate;
            candidate.reserve(m_Instances.size());
            for(const auto& previous:m_Instances)
            {
                Instance next;
                next.Attachment=previous.Attachment;
                next.Attachment.Properties.clear();
                const auto& type=registryCandidate.Type(next.Attachment.TypeName);
                next.System=registryCandidate.Create(next.Attachment.TypeName);
                if(policy==NativeReloadPolicy::PreserveCompatibleProperties)
                    for(const auto& property:type.Properties)
                    {
                        const auto found=previous.Attachment.Properties.find(property.Name);
                        if(found==previous.Attachment.Properties.end())continue;
                        try { NativeGameplayRegistry::ValidateValue(property,found->second); }
                        catch(const std::invalid_argument&) { continue; }
                        next.Attachment.Properties.emplace(property.Name,found->second);
                    }
                ApplyDefaultsAndOverrides(type,next);
                candidate.push_back(std::move(next));
            }
            m_Registry=std::move(registryCandidate);
            m_Instances=std::move(candidate);
        }

    private:
        struct Instance final
        {
            NativeGameplayAttachment Attachment;
            std::unique_ptr<NativeGameplaySystem> System;
        };

        RuntimeWorld& m_World;
        NativeGameplayRegistry m_Registry;
        std::vector<Instance> m_Instances;
        bool m_Playing = false;

        void ApplyDefaultsAndOverrides(const NativeGameplayTypeInfo& type, Instance& instance)
        {
            for(const auto& [name,value]:instance.Attachment.Properties)
            {
                (void)value;
                if(std::none_of(type.Properties.begin(),type.Properties.end(),
                    [&](const auto& property){return property.Name==name;}))
                    throw std::invalid_argument("Unknown native gameplay property override.");
            }
            for (const auto& property : type.Properties)
            {
                const auto found = instance.Attachment.Properties.find(property.Name);
                const NativeGameplayValue& value = found == instance.Attachment.Properties.end()
                    ? property.DefaultValue : found->second;
                NativeGameplayRegistry::ValidateValue(property,value);
                instance.System->ApplyProperty(property.Name, value);
                instance.Attachment.Properties.insert_or_assign(property.Name, value);
            }
        }

        void SortInstances()
        {
            std::sort(m_Instances.begin(), m_Instances.end(), [](const Instance& a, const Instance& b) {
                if (a.Attachment.Target.Value != b.Attachment.Target.Value)
                    return a.Attachment.Target.Value < b.Attachment.Target.Value;
                return a.Attachment.TypeName < b.Attachment.TypeName;
            });
        }

        template<class Callback>
        void DispatchLifecycle(Callback&& callback, double deltaSeconds)
        {
            for (auto& instance : m_Instances)
            {
                if (!instance.Attachment.Enabled || !m_World.Snapshot().Contains(instance.Attachment.Target)) continue;
                NativeGameplayContext context(m_World, instance.Attachment.Target, deltaSeconds);
                callback(*instance.System, context);
            }
            CommitIfNeeded();
        }

        void CommitIfNeeded()
        {
            if (m_World.PendingCommandCount() != 0u) (void)m_World.Commit();
        }
        void RequirePlaying() const
        { if (!m_Playing) throw std::logic_error("Native gameplay runtime is not playing."); }
        static void RequireDelta(double deltaSeconds)
        { if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0) throw std::invalid_argument("Gameplay delta must be finite and non-negative."); }
    };
}
