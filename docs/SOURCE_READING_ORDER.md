# Teaching source map — KairoEngineCore

Portable application, scene, persistence, input, animation and shipping contracts.

Read these source links in order, identify the caller and callee for each public API, then check the error/ownership contract and its tests.

## Reading order

1. [`CMakeLists.txt`](../CMakeLists.txt)
2. [`KairoEngineCore.cppm`](../KairoEngineCore.cppm)
3. [`Entity.cppm`](../Entity.cppm)
4. [`Components.cppm`](../Components.cppm)
5. [`Scene.cppm`](../Scene.cppm)
6. [`RuntimeComponents.cppm`](../RuntimeComponents.cppm)
7. [`ProjectDescriptor.cppm`](../ProjectDescriptor.cppm)
8. [`SceneSerialization.cppm`](../SceneSerialization.cppm)
9. [`Input.cppm`](../Input.cppm)
10. [`InputMap.cppm`](../InputMap.cppm)
11. [`JobSystem.cppm`](../JobSystem.cppm)
12. [`CoreReflection.cppm`](../CoreReflection.cppm)
13. [`AnimationRuntime.cppm`](../AnimationRuntime.cppm)
14. [`AudioRuntime.cppm`](../AudioRuntime.cppm)
15. [`GameplayVM.cppm`](../GameplayVM.cppm)
16. [`ShippingRuntime.cppm`](../ShippingRuntime.cppm)
17. [`tests`](../tests)

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
