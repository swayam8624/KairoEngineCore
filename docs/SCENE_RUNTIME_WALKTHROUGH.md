# From a .kproject to an authored scene and running world

This is a teaching companion to [the source-reading map](SOURCE_READING_ORDER.md).
It explains the C++23 architecture *as implemented*, not a claim that an
entire shipped game has passed runtime acceptance tests.

## 1. Project bootstrap is backend-neutral

[`ProjectDescriptor.cppm`](../ProjectDescriptor.cppm) owns the versioned
`.kproject` contract shared by KairoHub, Editor, build tools and Player. It
stores paths and preferences such as startup scene, asset manifest, input map,
graphics-backend policy and build profiles, without creating a native window
or GPU device. Paths and text records must be bounded and validated before
a launcher uses them.

The graphics-backend field is an *authored preference*. Actual availability
and host selection happen later in
[`KairoRenderer/GraphicsBackend.cppm`](https://github.com/swayam8624/KairoRenderer/blob/docs/teaching-walkthrough-20261121/GraphicsBackend.cppm).
Likewise, `AssetID` references need validation against KairoAssets;
loading a project descriptor alone does not import any mesh or texture.

**Exercise:** Change a project from automatic rendering to an explicit
unavailable backend. Trace where the policy is parsed and where the
renderer is supposed to reject unsupported selection.

## 2. Identity, hierarchy and components

[`Entity.cppm`](../Entity.cppm),
[`Components.cppm`](../Components.cppm),
[`RuntimeComponents.cppm`](../RuntimeComponents.cppm) and
[`Scene.cppm`](../Scene.cppm) define the portable scene model.

`Scene` owns entity IDs and optional component descriptors for names,
transforms, meshes, cameras, lights, environment, audio, logic and physics.
These are **authoritative authored records**, not necessarily live GPU
allocations or PhysicsWorld body handles. `CreateEntityWithID` restores
serialized identity without assigning IDs according to file order.

A parent/child relationship describes scene hierarchy. `SetParent`
rejects self-parenting and cycles, but preserves the child's *local*
transform. Preserving a world transform while reparenting is an explicit
Editor policy; the scene model does not invent it.

`DestroyEntity` removes an entire descendant subtree iteratively. Any
cached Entity references in an inspector, editor selection or runtime
adapter must be revalidated after deletion.

**Exercise:** Create parent -> child -> grandchild, serialize and restore
with the original IDs, then attempt to attach the parent beneath the
grandchild. Verify cycle rejection and the local/world transform policy.

## 3. From scene text to validated objects

[`SceneSerialization.cppm`](../SceneSerialization.cppm) parses a bounded
source format with line/column diagnostics and KairoAssets validation.
Entity references, including parents, may require deferred resolution
after all records are loaded. It should create a valid candidate Scene
before replacing an existing live scene.

[`SceneSerializationV5.cppm`](../SceneSerializationV5.cppm) and
[`AudioScenePersistence.cppm`](../AudioScenePersistence.cppm) extend
versioned content. Do not assume that all serialized component types live
in the older SceneSerialization module or that a new field can be added
without a compatibility test.

**Exercise:** Load a valid scene followed by a malformed one. Confirm
that failure does not leave the scene half-replaced. Then check whether
the versioned serializer retains audio component data.

## 4. The deferred RuntimeWorld mutation boundary

[`RuntimeWorld.cppm`](../RuntimeWorld.cppm) owns a Scene plus queued
commands for spawn, destroy, rename, transforms, parenting, tags and
enable state. Why defer changes? A gameplay system may iterate live
entities while another system wants to spawn/destroy one; mutating
containers mid-iteration can invalidate the traversal.

A `RuntimeSpawnTicket` maps one *pending* spawn request to the eventual
Entity allocated during commit. It is not itself a persisted scene ID.

`Commit` copies the current scene into a candidate, applies every queued
command in order, then publishes the candidate after success. A failed
command should leave the live scene/revision unchanged rather than
exposing a half-spawned frame.

```text
Snapshot() -> read-only live scene
QueueSpawn / QueueDestroy / QueueSetTransform -> pending operations
Commit() -> apply to candidate, publish, bump revision
Host adapters -> reconcile renderer/physics bindings against new revision
```

**Exercise:** Queue a valid spawn and an invalid parent edit in the same
batch. Verify failure leaves live identity and revision unchanged.
Do not cache pointers into a Scene across `Commit` or `Replace`.

## 5. Application, input, time and host ownership

[`Application.cppm`](../Application.cppm) owns scene/layer objects,
[`Time.cppm`](../Time.cppm) the clock,
[`Input.cppm`](../Input.cppm) raw frame-scoped input,
[`InputMap.cppm`](../InputMap.cppm) action mappings and
[`Event.cppm`](../Event.cppm) typed event dispatch.

A platform-native host injects OS events and runtime layers. The
EngineCore `Application` should not become a GLFW or VkInstance owner.
The Editor and Player may use different hosts while sharing a scene and
project schema.

[`JobSystem.cppm`](../JobSystem.cppm) handles CPU job scheduling; do not
confuse work-queue completion with GPU queue synchronization. See
[`CoreDiagnostics.cppm`](../CoreDiagnostics.cppm) and
[`Logger.cppm`](../Logger.cppm) for host-neutral diagnostics.

## 6. Animation, audio and production systems

[`AnimationRuntime.cppm`](../AnimationRuntime.cppm),
[`AnimationController.cppm`](../AnimationController.cppm),
[`AnimationRootMotion.cppm`](../AnimationRootMotion.cppm) and
[`GltfSkinningRuntime.cppm`](../GltfSkinningRuntime.cppm) implement
animation/pose contracts. Editor timelines and Player render adapters
consume these outputs; updating a pose does not automatically create GPU
skinning buffers.

[`AudioRuntime.cppm`](../AudioRuntime.cppm),
[`AudioSceneComponents.cppm`](../AudioSceneComponents.cppm) and
[`AudioScenePersistence.cppm`](../AudioScenePersistence.cppm) define
audio-facing scene and runtime responsibilities. Host audio devices remain
outside the pure scene schema.

[`GameplayVM.cppm`](../GameplayVM.cppm),
[`NativeGameplay.cppm`](../NativeGameplay.cppm),
[`ProductionSystems.cppm`](../ProductionSystems.cppm) and
[`ProductionRuntime.cppm`](../ProductionRuntime.cppm) define additional
runtime behavior. `ShippingRuntime`, `SaveGame` and `WorldStreaming`
handle distinct delivery, persistence and streaming concerns; none is a
substitute for authored `.kscene` serialization.

## 7. Three state lifetimes that must not be conflated

**Authored**: `AssetRegistry` / `Scene` / `.kproject` /
`.kscene` — persistent project identity and data.

**Runtime**: `RuntimeWorld` and host-created physics/audio/gameplay
state — simulation and transient runtime results.

**GPU**: renderer-owned mesh/texture/device/swapchain handles — may be
reallocated on resize/backend recreation without changing authored IDs.

Trace conversion using
[KairoGameEngine's shared scene bridge](https://github.com/swayam8624/KairoGameEngine/blob/docs/teaching-walkthrough-20261121/Runtime/KairoRealtimeRenderBridge/RealtimeSceneBridge.cppm)
and PhysicsEngine adapters. A scene serialization file does not have to
contain cached physics impulses or VkImage handles.

## 8. Suggested verification

Read [`tests/EngineCoreTests.cpp`](../tests/EngineCoreTests.cpp),
[`tests/RuntimeSceneCommandsTests.cpp`](../tests/RuntimeSceneCommandsTests.cpp),
[`tests/ProjectDescriptorTests.cpp`](../tests/ProjectDescriptorTests.cpp),
[`tests/SceneCompositionTests.cpp`](../tests/SceneCompositionTests.cpp),
[`tests/AudioScenePersistenceTests.cpp`](../tests/AudioScenePersistenceTests.cpp)
and [`tests/WorldStreamingTests.cpp`](../tests/WorldStreamingTests.cpp).
Record the exact revision, compiler, test command and output before
calling a host path verified.

*Evidence note: this guide is an inspection-based walkthrough of source
contracts on the teaching branch. It does not assert a new successful
build, native runtime acceptance or scene-format compatibility campaign.*
