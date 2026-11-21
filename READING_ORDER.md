# KairoEngineCore: linked source reading order

Begin with stable scene entity IDs, common project descriptors and scene persistence, then application/time/input/event contracts, reflection, and eventually animation, audio, streaming and shipping runtime. Rendering and physics are injected through adapters in KairoGameEngine: EngineCore must remain free of native GPU/physics-world ownership.

## API interactions to understand

Follow a .kproject parse through ProjectDescriptor, a scene Entity component through Scene and persistence, and a runtime input action through InputMap. Distinguish portable authoring identity from transient renderer handles and physics body state.

## Read these files in sequence (69)

1. [`KairoEngineCore.cppm`](KairoEngineCore.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
2. [`Entity.cppm`](Entity.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
3. [`AudioSceneComponents.cppm`](AudioSceneComponents.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
4. [`Components.cppm`](Components.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
5. [`RuntimeComponents.cppm`](RuntimeComponents.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
6. [`ProjectDescriptor.cppm`](ProjectDescriptor.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
7. [`Scene.cppm`](Scene.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
8. [`SceneSerialization.cppm`](SceneSerialization.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
9. [`Input.cppm`](Input.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
10. [`InputMap.cppm`](InputMap.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
11. [`Event.cppm`](Event.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
12. [`JobSystem.cppm`](JobSystem.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
13. [`CoreReflection.cppm`](CoreReflection.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
14. [`WorldStreaming.cppm`](WorldStreaming.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
15. [`SaveGame.cppm`](SaveGame.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
16. [`ShippingRuntime.cppm`](ShippingRuntime.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
17. [`AnimationController.cppm`](AnimationController.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
18. [`AnimationRootMotion.cppm`](AnimationRootMotion.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
19. [`AnimationRuntime.cppm`](AnimationRuntime.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
20. [`Application.cppm`](Application.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
21. [`AudioRuntime.cppm`](AudioRuntime.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
22. [`AudioScenePersistence.cppm`](AudioScenePersistence.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
23. [`CoreDiagnostics.cppm`](CoreDiagnostics.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
24. [`GameplayVM.cppm`](GameplayVM.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
25. [`GltfSkinningRuntime.cppm`](GltfSkinningRuntime.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
26. [`Layer.cppm`](Layer.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
27. [`Logger.cppm`](Logger.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
28. [`LogicArtifact.cppm`](LogicArtifact.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
29. [`LogicBytecode.cppm`](LogicBytecode.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
30. [`LogicState.cppm`](LogicState.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
31. [`NativeGameplay.cppm`](NativeGameplay.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
32. [`NativeGameplayManifest.cppm`](NativeGameplayManifest.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
33. [`Platform.cppm`](Platform.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
34. [`ProductionRuntime.cppm`](ProductionRuntime.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
35. [`ProductionSystems.cppm`](ProductionSystems.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
36. [`ProductionSystemsManifest.cppm`](ProductionSystemsManifest.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
37. [`RuntimeSceneCommands.cppm`](RuntimeSceneCommands.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
38. [`RuntimeWorld.cppm`](RuntimeWorld.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
39. [`SceneComposition.cppm`](SceneComposition.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
40. [`SceneSerializationV5.cppm`](SceneSerializationV5.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
41. [`tests/AnimationControllerTests.cpp`](tests/AnimationControllerTests.cpp) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
42. [`tests/AnimationRootMotionTests.cpp`](tests/AnimationRootMotionTests.cpp) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
43. [`tests/AnimationRuntimeTests.cpp`](tests/AnimationRuntimeTests.cpp) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
44. [`tests/AudioRuntimeTests.cpp`](tests/AudioRuntimeTests.cpp) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
45. [`tests/AudioSceneComponentTests.cpp`](tests/AudioSceneComponentTests.cpp) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
46. [`tests/AudioScenePersistenceTests.cpp`](tests/AudioScenePersistenceTests.cpp) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
47. [`tests/DeterministicBytecodeTests.cpp`](tests/DeterministicBytecodeTests.cpp) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
48. [`tests/EngineCoreTests.cpp`](tests/EngineCoreTests.cpp) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
49. [`tests/LogicArtifactPayloadBoundaryTests.cpp`](tests/LogicArtifactPayloadBoundaryTests.cpp) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
50. [`tests/LogicStateTests.cpp`](tests/LogicStateTests.cpp) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
51. [`tests/NativeGameplayManifestTests.cpp`](tests/NativeGameplayManifestTests.cpp) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
52. [`tests/NativeGameplayTests.cpp`](tests/NativeGameplayTests.cpp) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
53. [`tests/Phase3RuntimeTests.cpp`](tests/Phase3RuntimeTests.cpp) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
54. [`tests/PlatformTests.cpp`](tests/PlatformTests.cpp) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
55. [`tests/ProductionRuntimeTests.cpp`](tests/ProductionRuntimeTests.cpp) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
56. [`tests/ProductionSystemsTests.cpp`](tests/ProductionSystemsTests.cpp) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
57. [`tests/ProjectDescriptorTests.cpp`](tests/ProjectDescriptorTests.cpp) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
58. [`tests/ReflectionV2IntegrationTests.cpp`](tests/ReflectionV2IntegrationTests.cpp) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
59. [`tests/RenderingComponentTests.cpp`](tests/RenderingComponentTests.cpp) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
60. [`tests/RuntimeSceneCommandsTests.cpp`](tests/RuntimeSceneCommandsTests.cpp) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
61. [`tests/SaveGameTests.cpp`](tests/SaveGameTests.cpp) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
62. [`tests/SceneAudioSerializationV5Tests.cpp`](tests/SceneAudioSerializationV5Tests.cpp) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
63. [`tests/SceneCompositionTests.cpp`](tests/SceneCompositionTests.cpp) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
64. [`tests/ShippingRuntimeTests.cpp`](tests/ShippingRuntimeTests.cpp) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
65. [`tests/WorldStreamingScaleTests.cpp`](tests/WorldStreamingScaleTests.cpp) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
66. [`tests/WorldStreamingTests.cpp`](tests/WorldStreamingTests.cpp) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
67. [`TextFormat.cppm`](TextFormat.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
68. [`TextValidation.cppm`](TextValidation.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.
69. [`Time.cppm`](Time.cppm) — inspect its imports, data ownership, validation and exported functions; follow its use from previous modules into downstream modules and tests.

## Verification and reuse

Read [README.md](README.md) and [CMakeLists.txt](CMakeLists.txt) for build/module boundaries where present. Test invalid inputs, lifetime errors, domain boundaries and the exact source revision being used by [KairoGameEngine](https://github.com/swayam8624/KairoGameEngine). A source file list or module compile is not full integration acceptance. Original software is shared under [MIT](LICENSE) for learning, reuse and continued development; external dependencies and assets retain their own terms. Per-function annotations are a subsequent documentation pass.
