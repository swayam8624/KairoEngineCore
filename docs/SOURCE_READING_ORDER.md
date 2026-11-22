# Teaching source map — KairoEngineCore

Portable application, scene, persistence, input, animation and shipping contracts.

Read these source links in order, identify the caller and callee for each public API, then check the error/ownership contract and its tests.

## Reading order

Start with the [complete project-to-runtime lifecycle walkthrough](SCENE_RUNTIME_WALKTHROUGH.md).
The stages below form a learning curriculum, not CMake dependency order.

**Cross-repository integration lab:** [RuntimeComponents + Scene → shared draw extraction and GPU handle boundaries](https://github.com/swayam8624/KairoGameEngine/blob/main/docs/ASSET_SCENE_TO_GPU_LIFETIME_LAB.md). Read the authored/CPU contracts before following the native GPU backend; source inspection is not a native test result.

**Practice with real source:** [Stable IDs, world transforms, deferred command failures and versioned saves](AUTHORED_SCENE_AND_RUNTIME_TRANSACTION_LAB.md). This guide distinguishes authored scene objects from the ECS, physics and graphics runtime handles.

### Stage A — Project bootstrap and authoritative scene

1. [`CMakeLists.txt`](../CMakeLists.txt), [`KairoEngineCore.cppm`](../KairoEngineCore.cppm): build/modules and exported API boundaries.
2. [`ProjectDescriptor.cppm`](../ProjectDescriptor.cppm), [`TextFormat.cppm`](../TextFormat.cppm) and [`TextValidation.cppm`](../TextValidation.cppm): shared .kproject syntax and safe path/atomic persistence.
3. [`Entity.cppm`](../Entity.cppm), [`Components.cppm`](../Components.cppm) and [`RuntimeComponents.cppm`](../RuntimeComponents.cppm): stable ID and component contracts.
4. [`Scene.cppm`](../Scene.cppm): owned entity/component registry, hierarchy and active state.
5. [`SceneSerialization.cppm`](../SceneSerialization.cppm), [`SceneSerializationV5.cppm`](../SceneSerializationV5.cppm) and [`SceneComposition.cppm`](../SceneComposition.cppm): portable state, versioning and composed transforms.
6. [`tests/EngineCoreTests.cpp`](../tests/EngineCoreTests.cpp), [`tests/ProjectDescriptorTests.cpp`](../tests/ProjectDescriptorTests.cpp) and [`tests/SceneCompositionTests.cpp`](../tests/SceneCompositionTests.cpp).

**Exercise:** create/restore a parent-child scene and verify the IDs are
independent from serialization order; distinguish reparenting with local
transform preserved from a world-transform-preserving editor operation.

### Stage B — Frame lifecycle and runtime mutations

7. [`Application.cppm`](../Application.cppm), [`Layer.cppm`](../Layer.cppm) and [`Event.cppm`](../Event.cppm): host-injected layers and event dispatch.
8. [`Time.cppm`](../Time.cppm), [`Input.cppm`](../Input.cppm) and [`InputMap.cppm`](../InputMap.cppm): frame clock, raw inputs and named actions.
9. [`RuntimeWorld.cppm`](../RuntimeWorld.cppm) and [`RuntimeSceneCommands.cppm`](../RuntimeSceneCommands.cppm): queued edits, commit boundary, runtime revision.
10. [`WorldStreaming.cppm`](../WorldStreaming.cppm): stream/tile state and lifecycle.
11. [`JobSystem.cppm`](../JobSystem.cppm), [`CoreDiagnostics.cppm`](../CoreDiagnostics.cppm) and [`Logger.cppm`](../Logger.cppm): CPU jobs and non-GPU diagnostics.
12. [`tests/RuntimeSceneCommandsTests.cpp`](../tests/RuntimeSceneCommandsTests.cpp) and [`tests/WorldStreamingTests.cpp`](../tests/WorldStreamingTests.cpp).

**Exercise:** queue a valid and invalid scene edit together, attempt
commit, then verify the live scene/revision did not partially mutate.
A spawn ticket is not a durable Entity ID or a PhysicsWorld body handle.

### Stage C — Animation, audio and gameplay

13. [`AnimationRuntime.cppm`](../AnimationRuntime.cppm), [`AnimationController.cppm`](../AnimationController.cppm), [`AnimationRootMotion.cppm`](../AnimationRootMotion.cppm) and [`GltfSkinningRuntime.cppm`](../GltfSkinningRuntime.cppm).
14. [`AudioRuntime.cppm`](../AudioRuntime.cppm), [`AudioSceneComponents.cppm`](../AudioSceneComponents.cppm) and [`AudioScenePersistence.cppm`](../AudioScenePersistence.cppm).
15. [`LogicArtifact.cppm`](../LogicArtifact.cppm), [`LogicBytecode.cppm`](../LogicBytecode.cppm), [`LogicState.cppm`](../LogicState.cppm) and [`GameplayVM.cppm`](../GameplayVM.cppm).
16. [`NativeGameplay.cppm`](../NativeGameplay.cppm), [`NativeGameplayManifest.cppm`](../NativeGameplayManifest.cppm), [`ProductionSystems.cppm`](../ProductionSystems.cppm) and [`ProductionRuntime.cppm`](../ProductionRuntime.cppm).
17. [`ProductionSystemsManifest.cppm`](../ProductionSystemsManifest.cppm), [`ShippingRuntime.cppm`](../ShippingRuntime.cppm) and [`SaveGame.cppm`](../SaveGame.cppm).
18. [`CoreReflection.cppm`](../CoreReflection.cppm), [`Platform.cppm`](../Platform.cppm) and [`RuntimeSceneCommands.cppm`](../RuntimeSceneCommands.cppm): inspector, platform and mutation integration.
19. Match each subsystem with its specific test listed in the full inventory.

**Exercise:** trace glTF animation output to a render-scene skin palette
without allowing EngineCore to own native GPU resources. Differentiate
authored .kscene persistence from runtime save-game state.

### Stage D — Host integration and validation

20. Follow KairoGameEngine's [shared render bridge](https://github.com/swayam8624/KairoGameEngine/blob/main/Runtime/KairoRealtimeRenderBridge/RealtimeSceneBridge.cppm) and physics bridges to convert authored descriptors into native runtime state.
21. Use [KairoEditor's source guide](https://github.com/swayam8624/KairoEditor/blob/main/docs/SOURCE_READING_ORDER.md) to trace the authoring side of the same .kproject/.kscene format.
22. Inspect remaining subsystem tests in the inventory below and record exact-head build/test results before claiming a verified host.

## Repository integration and API contracts

Editor writes scenes; Player uses EngineCore to load/update authored data; Renderer and PhysicsEngine bind through adapters. EngineCore must not become coupled to a graphics-device lifetime.

## Teaching standards

- Comments explain invariants, decision reasons, lifetime, prerequisites and cross-file effects; they do not narrate obvious statements.
- Follow upstream interfaces before implementation details; distinguish implemented, host-verified and planned features.
- Run available tests at a pinned commit and consult [STATUS.md](../STATUS.md) / [STATUS.yaml](../STATUS.yaml) before declaring completion.
- Reuse original contributions under [LICENSE](../LICENSE), subject to third-party code and asset terms.


## Complete source-file inventory

This ordered inventory supplements the **curated** reading path above. It includes every first-party code, build, script, shader and test file found in the current teaching branch at the time of indexing. Read the curated path **first**, then work down the categories below, following imports and calls when a dependent file precedes its implementation. External/vendor source, binaries, test ROMs and generated evidence are deliberately excluded from this source-learning checklist; their rights and verification are independent.

### Entry points and build (1)

1. [`CMakeLists.txt`](../CMakeLists.txt)

### Interfaces and modules (43)

1. [`AnimationController.cppm`](../AnimationController.cppm)
2. [`AnimationRootMotion.cppm`](../AnimationRootMotion.cppm)
3. [`AnimationRuntime.cppm`](../AnimationRuntime.cppm)
4. [`Application.cppm`](../Application.cppm)
5. [`AudioRuntime.cppm`](../AudioRuntime.cppm)
6. [`AudioSceneComponents.cppm`](../AudioSceneComponents.cppm)
7. [`AudioScenePersistence.cppm`](../AudioScenePersistence.cppm)
8. [`Components.cppm`](../Components.cppm)
9. [`CoreDiagnostics.cppm`](../CoreDiagnostics.cppm)
10. [`CoreReflection.cppm`](../CoreReflection.cppm)
11. [`Entity.cppm`](../Entity.cppm)
12. [`Event.cppm`](../Event.cppm)
13. [`GameplayVM.cppm`](../GameplayVM.cppm)
14. [`GltfSkinningRuntime.cppm`](../GltfSkinningRuntime.cppm)
15. [`Input.cppm`](../Input.cppm)
16. [`InputMap.cppm`](../InputMap.cppm)
17. [`JobSystem.cppm`](../JobSystem.cppm)
18. [`KairoEngineCore.cppm`](../KairoEngineCore.cppm)
19. [`Layer.cppm`](../Layer.cppm)
20. [`Logger.cppm`](../Logger.cppm)
21. [`LogicArtifact.cppm`](../LogicArtifact.cppm)
22. [`LogicBytecode.cppm`](../LogicBytecode.cppm)
23. [`LogicState.cppm`](../LogicState.cppm)
24. [`NativeGameplay.cppm`](../NativeGameplay.cppm)
25. [`NativeGameplayManifest.cppm`](../NativeGameplayManifest.cppm)
26. [`Platform.cppm`](../Platform.cppm)
27. [`ProductionRuntime.cppm`](../ProductionRuntime.cppm)
28. [`ProductionSystems.cppm`](../ProductionSystems.cppm)
29. [`ProductionSystemsManifest.cppm`](../ProductionSystemsManifest.cppm)
30. [`ProjectDescriptor.cppm`](../ProjectDescriptor.cppm)
31. [`RuntimeComponents.cppm`](../RuntimeComponents.cppm)
32. [`RuntimeSceneCommands.cppm`](../RuntimeSceneCommands.cppm)
33. [`RuntimeWorld.cppm`](../RuntimeWorld.cppm)
34. [`SaveGame.cppm`](../SaveGame.cppm)
35. [`Scene.cppm`](../Scene.cppm)
36. [`SceneComposition.cppm`](../SceneComposition.cppm)
37. [`SceneSerialization.cppm`](../SceneSerialization.cppm)
38. [`SceneSerializationV5.cppm`](../SceneSerializationV5.cppm)
39. [`ShippingRuntime.cppm`](../ShippingRuntime.cppm)
40. [`TextFormat.cppm`](../TextFormat.cppm)
41. [`TextValidation.cppm`](../TextValidation.cppm)
42. [`Time.cppm`](../Time.cppm)
43. [`WorldStreaming.cppm`](../WorldStreaming.cppm)

### Verification and tests (26)

1. [`tests/AnimationControllerTests.cpp`](../tests/AnimationControllerTests.cpp)
2. [`tests/AnimationRootMotionTests.cpp`](../tests/AnimationRootMotionTests.cpp)
3. [`tests/AnimationRuntimeTests.cpp`](../tests/AnimationRuntimeTests.cpp)
4. [`tests/AudioRuntimeTests.cpp`](../tests/AudioRuntimeTests.cpp)
5. [`tests/AudioSceneComponentTests.cpp`](../tests/AudioSceneComponentTests.cpp)
6. [`tests/AudioScenePersistenceTests.cpp`](../tests/AudioScenePersistenceTests.cpp)
7. [`tests/DeterministicBytecodeTests.cpp`](../tests/DeterministicBytecodeTests.cpp)
8. [`tests/EngineCoreTests.cpp`](../tests/EngineCoreTests.cpp)
9. [`tests/LogicArtifactPayloadBoundaryTests.cpp`](../tests/LogicArtifactPayloadBoundaryTests.cpp)
10. [`tests/LogicStateTests.cpp`](../tests/LogicStateTests.cpp)
11. [`tests/NativeGameplayManifestTests.cpp`](../tests/NativeGameplayManifestTests.cpp)
12. [`tests/NativeGameplayTests.cpp`](../tests/NativeGameplayTests.cpp)
13. [`tests/Phase3RuntimeTests.cpp`](../tests/Phase3RuntimeTests.cpp)
14. [`tests/PlatformTests.cpp`](../tests/PlatformTests.cpp)
15. [`tests/ProductionRuntimeTests.cpp`](../tests/ProductionRuntimeTests.cpp)
16. [`tests/ProductionSystemsTests.cpp`](../tests/ProductionSystemsTests.cpp)
17. [`tests/ProjectDescriptorTests.cpp`](../tests/ProjectDescriptorTests.cpp)
18. [`tests/ReflectionV2IntegrationTests.cpp`](../tests/ReflectionV2IntegrationTests.cpp)
19. [`tests/RenderingComponentTests.cpp`](../tests/RenderingComponentTests.cpp)
20. [`tests/RuntimeSceneCommandsTests.cpp`](../tests/RuntimeSceneCommandsTests.cpp)
21. [`tests/SaveGameTests.cpp`](../tests/SaveGameTests.cpp)
22. [`tests/SceneAudioSerializationV5Tests.cpp`](../tests/SceneAudioSerializationV5Tests.cpp)
23. [`tests/SceneCompositionTests.cpp`](../tests/SceneCompositionTests.cpp)
24. [`tests/ShippingRuntimeTests.cpp`](../tests/ShippingRuntimeTests.cpp)
25. [`tests/WorldStreamingScaleTests.cpp`](../tests/WorldStreamingScaleTests.cpp)
26. [`tests/WorldStreamingTests.cpp`](../tests/WorldStreamingTests.cpp)

Source-index snapshot: 70 code/build/test files, 13 other tracked files (including excluded third-party, documentation and assets). Use a new tree audit before treating the index as current after subsequent source changes.
