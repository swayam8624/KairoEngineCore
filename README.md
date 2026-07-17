# KairoEngineCore

`KairoEngineCore` is the platform-neutral scene and application layer between
Kairo's renderer, physics engine, and future editor. It intentionally owns no
GLFW/Vulkan or physics world; host applications inject those systems.

```text
KairoMath ----> KairoEngineCore -> KairoRenderer / KairoPhysicsEngine adapters
KairoAssets --/
KairoReflection -/
```

V1 provides stable `Entity` IDs, a scene registry, name/transform/runtime
components, a versioned project bootstrap contract, a monotonic frame clock,
typed events/layers, a bounded thread-safe logger, typed cross-system
diagnostics, a fixed worker job system, and platform-neutral input state. Renderer and physics bindings remain
outside the core so each backend can preserve its own lifetime rules.

## Project bootstrap

`Kairo.EngineCore.ProjectDescriptor` owns the runtime-neutral `.kproject`
contract shared by KairoHub, KairoEditor, build tools, and KairoPlayer. Keeping
this format in EngineCore prevents launchers and players from depending on the
editor while ensuring every host validates the same startup scene, asset
manifest, input map, engine version, plugins, rendering profile, and build
profiles.

Project V1 remains readable with deterministic defaults. Explicit saves emit
V2, reject unsafe or ambiguous relative paths, and use a flushed colocated
temporary plus atomic replacement so a failed write cannot destroy the last
valid descriptor. Located parse errors report one-based line and column
positions. The reusable `TextValidation` and `TextFormat` modules provide the
same bounded UTF-8, quoted-token, and atomic-text primitives to other
EngineCore-owned formats.

```cpp
import Kairo.EngineCore;

ProjectDescriptor project = LoadProjectDescriptor("Game.kproject");
project.StartupScene = "Scenes/Title.kscene";
SaveProjectDescriptor("Game.kproject", project);
```

## Reflection

`Kairo.EngineCore.Reflection` registers metadata for the scalar fields already
owned by EngineCore: names, camera projection settings, mesh visibility, and
opaque physics bindings. The catalog uses `KairoReflection` rather than raw
ImGui controls, so an editor inspector, document validator, search index, and
future graph parameter UI share the same stable keys and constraints.

```cpp
import Kairo.EngineCore;
import Kairo.Reflection;

kairo::reflection::ReflectionRegistry registry;
RegisterEngineCoreReflection(registry);
```

`TransformComponent::Local` is intentionally deferred until KairoReflection
gains explicit composite vector/quaternion adapters. It is not exposed as a
collection of accidental scalar fields.

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

## Gameplay input

`Kairo.EngineCore.Input` owns frame-scoped raw keyboard, mouse, and four-slot
standardized gamepad state. Platform adapters update it without leaking GLFW,
Win32, Cocoa, or another window API into gameplay. Held, pressed, and released
queries use explicit previous-frame snapshots; analog gamepad axes are finite
and clamped to `[-1, 1]`.

`Kairo.EngineCore.InputMap` turns raw state into named button, 1D-axis, and
2D-axis actions. `.kinput` V1 is bounded, deterministic, atomically saved, and
reports exact one-based line/column errors. Axis bindings combine and clamp;
gamepad deadzones are removed and the remaining range is rescaled.

```text
kairo-input 1
action "Move" axis2d
action "Jump" button
bind "Move" key W 0 1 0
bind "Move" key A -1 0 0
bind "Move" gamepad-axis LeftX 1 0 0.15
bind "Move" gamepad-axis LeftY 0 -1 0.15
bind "Jump" key Space 1 0 0
bind "Jump" gamepad-button A 1 0 0
```

Each `bind` statement contains action, device, named control, X/Y contribution,
and deadzone. Supported devices are `key`, `mouse-button`, `gamepad-button`, and
`gamepad-axis`; only analog-axis bindings may use a non-zero deadzone.

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

`Kairo.EngineCore.SceneSerialization` provides deterministic `kairo-scene 2`
text serialization with stable entity IDs, quoted names, transforms, mesh
renderer bindings, cameras, parent relationships, enabled state, 64 layers,
and bounded sorted tags. Loading validates mesh/material UUIDs and their
declared types against the project's live `AssetRegistry`. Parse failures carry
exact one-based line and column positions; file loading replaces the destination
scene only after the complete document validates, and saving uses a flushed
same-directory temporary followed by atomic replacement.

V1 remains readable. It migrates in memory to root entities that are enabled,
on layer zero, with no tags; only an explicit save emits V2. Parent references
may point forward in the file, but missing entities, self-parenting, and cycles
are rejected with the relationship's source location. Runtime reparenting keeps
the local transform unchanged, and destroying a parent destroys its complete
descendant subtree so no dangling relationships remain.

```text
kairo-scene 2
entity 9 "Hero Cube"
enabled true
layer 2
tag "player"
transform 0 1 0 0 0 0 1 1 1 1
mesh-renderer 00000000-0000-4000-8000-000000000101 00000000-0000-4000-8000-000000000102 true
end
```

`RigidBodyComponent` and `ColliderComponent` are persistent authoring
descriptors, not runtime handles. They store motion type, density, gravity,
damping, primitive shape, dimensions, friction, restitution, category/collision
masks, and
trigger state. Play-mode adapters create process-local body/collider IDs and
retain those mappings outside the scene. Legacy V1 numeric bindings remain
readable and migrate to dynamic-body/box defaults; V2 never writes those IDs.

```text
rigid-body dynamic 1 1 0.05 0.05
collider box 0.5 0.5 0.5 0.5 0.1 1 4294967295 false
```

```bash
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++
cmake --build build
ctest --test-dir build --output-on-failure
```
