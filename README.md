# KairoEngineCore

`KairoEngineCore` is the platform-neutral scene and application layer between
Kairo's renderer, physics engine, and future editor. It intentionally owns no
GLFW/Vulkan or physics world; host applications inject those systems.

```text
KairoMath ----> KairoEngineCore -> KairoRenderer / KairoPhysicsEngine adapters
KairoAssets --/
```

V1 provides stable `Entity` IDs, a scene registry, name/transform/runtime
components, a monotonic frame clock, typed events/layers, a bounded thread-safe
logger, typed cross-system diagnostics, a fixed worker job system, and platform-neutral input state. Renderer and physics bindings remain
outside the core so each backend can preserve its own lifetime rules.

## Diagnostics

`Kairo.EngineCore.Diagnostics` introduces `CoreDiagnostics`, the common
channelled diagnostic facade for engine hosts and tools. It intentionally
reuses the existing bounded `Logger` for synchronization, severity filtering,
retention, and snapshots rather than keeping a second record store. Emitters
choose a stable top-level channel (`Core`, `Assets`, `Scene`, `Renderer`,
`Physics`, `Editor`, or `Tooling`) and may add a narrow scope such as
`Renderer/Swapchain` or `Assets/Import`.

```cpp
CoreDiagnostics diagnostics;
diagnostics.Emit(DiagnosticChannel::Renderer, "Swapchain", LogSeverity::Warning,
    "Recreating after an out-of-date surface.");
```

The editor console, file sinks, and telemetry integrations consume the same
ordered `Snapshot()` records. Core diagnostics deliberately has no GLFW,
Vulkan, filesystem, or external logging dependency.

Scene enumeration returns stable entity IDs in ascending creation order so
hierarchies, serializers, and systems do not depend on unordered storage order.
Scenes own optional mesh renderer, camera, rigid-body, and collider components.
Mesh renderer components use persistent typed `KairoAssets` handles rather than
path strings, so asset moves do not invalidate scene references.
`RenderableEntities()` filters visible meshes deterministically for renderer
extraction while registry lookup, decoded content, and GPU resources remain
adapter-owned.

Dependency resolution prefers `KAIRO_ASSETS_SOURCE_DIR`, then sibling
`../KairoAssets`, then the GitHub `main` branch. EngineCore publicly links the
asset identity API but never owns decoded asset or GPU-resource lifetimes.

## Scene persistence

`Kairo.EngineCore.SceneSerialization` provides deterministic `kairo-scene 1`
text serialization with stable entity IDs, quoted names, transforms, mesh
renderer bindings, and cameras. Loading validates mesh/material UUIDs and their
declared types against the project's live `AssetRegistry`. Parse failures carry
exact one-based line and column positions; file loading replaces the destination
scene only after the complete document validates, and saving uses a flushed
same-directory temporary followed by atomic replacement.

```text
kairo-scene 1
entity 9 "Hero Cube"
transform 0 1 0 0 0 0 1 1 1 1
mesh-renderer 00000000-0000-4000-8000-000000000101 00000000-0000-4000-8000-000000000102 true
end
```

`RigidBodyComponent` and `ColliderComponent` contain process-local adapter
handles, so they are intentionally not serialized. A later persistent physics
authoring component will describe body/collider creation; play mode will then
rebuild runtime handles from that authored data.

```bash
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++
cmake --build build
ctest --test-dir build --output-on-failure
```
