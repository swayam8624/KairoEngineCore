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
