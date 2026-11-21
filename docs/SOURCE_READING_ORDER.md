# KairoEngineCore — linked learning sequence

KairoEngineCore defines **platform-neutral scene, application, input, serialization and gameplay contracts**. It intentionally does not own a Vulkan/Metal device or a physics world. Its adapters in [KairoGameEngine](https://github.com/swayam8624/KairoGameEngine) connect it to [KairoRenderer](https://github.com/swayam8624/KairoRenderer) and [KairoPhysicsEngine](https://github.com/swayam8624/KairoPhysicsEngine).

## Start with the application and scene model

1. [README](../README.md) and [CMakeLists](../CMakeLists.txt) — current build, package dependencies and test scope.
2. [KairoEngineCore.cppm](../KairoEngineCore.cppm) — umbrella public C++ module imports; find individual interfaces before reading everything.
3. [Entity.cppm](../Entity.cppm), [Components.cppm](../Components.cppm), [RuntimeComponents.cppm](../RuntimeComponents.cppm) — scene-local entity IDs and transform/runtime component payloads. Do not confuse these with [KairoECS](https://github.com/swayam8624/KairoECS)'s generational runtime storage handles.
4. [Scene.cppm](../Scene.cppm), [SceneComposition.cppm](../SceneComposition.cppm) — creation, lookup, composition and ownership of scene entities/components.
5. [Time.cppm](../Time.cppm), [Event.cppm](../Event.cppm), [Layer.cppm](../Layer.cppm), [Application.cppm](../Application.cppm) — frame time, event routing, layer lifecycle and application composition.
6. [Input.cppm](../Input.cppm), [InputMap.cppm](../InputMap.cppm) — raw host events and persisted semantic actions. Follow the [player input bridge](https://github.com/swayam8624/KairoGameEngine/blob/main/Runtime/KairoPlayer/RuntimeInputBridge.cppm).

## Learn durable data and project boot

7. [ProjectDescriptor.cppm](../ProjectDescriptor.cppm), [TextValidation.cppm](../TextValidation.cppm), [TextFormat.cppm](../TextFormat.cppm) — common `.kproject` descriptor, portable text and path validation.
8. [SceneSerialization.cppm](../SceneSerialization.cppm), [SceneSerializationV5.cppm](../SceneSerializationV5.cppm) — scene formats and migrations. Pair with [KairoAssets manifests](https://github.com/swayam8624/KairoAssets/blob/main/AssetManifest.cppm).
9. [RuntimeSceneCommands.cppm](../RuntimeSceneCommands.cppm), [RuntimeWorld.cppm](../RuntimeWorld.cppm), [WorldStreaming.cppm](../WorldStreaming.cppm) — live edits, streamed world state and dynamic entity lifecycles.
10. [SaveGame.cppm](../SaveGame.cppm) — world persistence, distinct from editor project/scene serialization.

## Execution, gameplay and services

11. [Logger.cppm](../Logger.cppm), [CoreDiagnostics.cppm](../CoreDiagnostics.cppm), [JobSystem.cppm](../JobSystem.cppm) — observability, worker tasks and lifetime safety.
12. [LogicArtifact.cppm](../LogicArtifact.cppm), [LogicBytecode.cppm](../LogicBytecode.cppm), [LogicState.cppm](../LogicState.cppm), [GameplayVM.cppm](../GameplayVM.cppm) — authored gameplay logic compilation/interpretation and state.
13. [NativeGameplay.cppm](../NativeGameplay.cppm), [NativeGameplayManifest.cppm](../NativeGameplayManifest.cppm) — compiled gameplay integration and durable declarations.
14. [AnimationRuntime.cppm](../AnimationRuntime.cppm), [AnimationController.cppm](../AnimationController.cppm), [AnimationRootMotion.cppm](../AnimationRootMotion.cppm) — animation state and motion contribution.
15. [AudioRuntime.cppm](../AudioRuntime.cppm), [AudioSceneComponents.cppm](../AudioSceneComponents.cppm), [AudioScenePersistence.cppm](../AudioScenePersistence.cppm) — authored sound definitions vs host audio playback.
16. [ProductionSystems.cppm](../ProductionSystems.cppm), [ProductionRuntime.cppm](../ProductionRuntime.cppm), [ShippingRuntime.cppm](../ShippingRuntime.cppm) — optional production and shipping facilities.

## Prove the contracts

17. [tests](../tests/) — look for `EngineCoreTests.cpp`, `ProjectDescriptorTests.cpp`, `RuntimeSceneCommandsTests.cpp`, `WorldStreamingTests.cpp`, `SaveGameTests.cpp`, and gameplay tests.
18. [docs/PHASES_7_12_RUNTIME.md](PHASES_7_12_RUNTIME.md), [docs/PHASES_13_15_SHIPPING_RUNTIME.md](PHASES_13_15_SHIPPING_RUNTIME.md), [docs/PORTABILITY.md](PORTABILITY.md) — compare intended scope with tested behaviour.

## Cross-file traces

**Frame:** platform host → Event/Input → InputMap action state → Application layers and GameplayVM/native logic → scene transform updates → external physics/render adapters. EngineCore must not hardcode native GPU/solver APIs.

**Project launch:** `.kproject` → ProjectDescriptor validation → KairoAssets manifest → SceneSerialization → RuntimeProject in KairoGameEngine → input/physics/render/audio runtime bridges.

**Save game:** live RuntimeWorld state → stable serialization identity → SaveGame payload → later reconstruction. Do not mistake temporary GPU handles or physics-body IDs for durable scene identities.

**Streaming:** WorldStreaming selects cells → runtime scene commands create/disable entities → physics/render adapters synchronize resources. Avoid keeping invalid references after cell unload.

## Exercises

1. Trace one key into a named action and then a gameplay transform update.
2. Explain EngineCore Entity vs KairoECS Entity and whether their IDs are interchangeable.
3. Serialize/restore a world, identify all foreign/session-only handles rebuilt at runtime.
4. Follow one scene unload through live components and external physics/render bridges.
5. Distinguish application frame delta from fixed-step physics time.

The original library code is intended to be educational and extensible under [LICENSE](../LICENSE); third-party libraries and imported resources have their own terms.
