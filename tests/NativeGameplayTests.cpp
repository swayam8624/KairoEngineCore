#include <memory>
#include <string>
#include <variant>

#include <catch2/catch_test_macros.hpp>

import Kairo.EngineCore.NativeGameplay;
import Kairo.EngineCore.RuntimeWorld;
import Kairo.EngineCore.Scene;

namespace
{
    class CounterSystem final : public kairo::engine::NativeGameplaySystem
    {
    public:
        static inline int Began = 0;
        static inline int Updated = 0;
        double Speed = 0.0;

        void OnBeginPlay(kairo::engine::NativeGameplayContext& context) override
        {
            ++Began;
            context.AddTag(context.Self(), "native-began");
        }
        void OnUpdate(kairo::engine::NativeGameplayContext& context) override
        {
            ++Updated;
            if (Speed > 1.0) context.AddTag(context.Self(), "fast");
        }
        void ApplyProperty(std::string_view name,
            const kairo::engine::NativeGameplayValue& value) override
        {
            if (name == "speed") Speed = std::get<double>(value);
        }
    };
}

TEST_CASE("Native gameplay registry reflects properties and drives lifecycle")
{
    using namespace kairo::engine;
    CounterSystem::Began = 0;
    CounterSystem::Updated = 0;

    NativeGameplayRegistry registry;
    NativeGameplayTypeInfo type;
    type.TypeName = "Counter";
    type.Properties.push_back({ "speed", NativeGameplayPropertyType::Number, 1.0, true, 0.0, 10.0 });
    registry.Register(type, [] { return std::make_unique<CounterSystem>(); });
    REQUIRE(registry.Contains("Counter"));
    REQUIRE(registry.Type("Counter").Properties.size() == 1u);

    Scene scene;
    const Entity actor = scene.CreateEntity("Actor");
    RuntimeWorld world(std::move(scene));
    NativeGameplayRuntime runtime(world, registry);
    NativeGameplayAttachment attachment;
    attachment.Target = actor;
    attachment.TypeName = "Counter";
    attachment.Properties["speed"] = 2.0;
    runtime.Attach(std::move(attachment));

    runtime.BeginPlay();
    CHECK(CounterSystem::Began == 1);
    CHECK(world.Snapshot().HasTag(actor, "native-began"));
    runtime.Update(1.0 / 60.0);
    CHECK(CounterSystem::Updated == 1);
    CHECK(world.Snapshot().HasTag(actor, "fast"));
    runtime.EndPlay();
}

TEST_CASE("Native gameplay property type mismatches fail before play")
{
    using namespace kairo::engine;
    NativeGameplayRegistry registry;
    NativeGameplayTypeInfo type;
    type.TypeName = "Counter";
    type.Properties.push_back({ "speed", NativeGameplayPropertyType::Number, 1.0 });
    registry.Register(type, [] { return std::make_unique<CounterSystem>(); });

    Scene scene;
    const Entity actor = scene.CreateEntity("Actor");
    RuntimeWorld world(std::move(scene));
    NativeGameplayRuntime runtime(world, registry);
    NativeGameplayAttachment attachment;
    attachment.Target = actor;
    attachment.TypeName = "Counter";
    attachment.Properties["speed"] = std::string("wrong");
    REQUIRE_THROWS_AS(runtime.Attach(std::move(attachment)), std::invalid_argument);
}
