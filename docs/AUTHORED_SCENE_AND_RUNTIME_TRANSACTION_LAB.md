# From an authored entity to a committed runtime scene

This is a worked learning guide for [Scene.cppm](../Scene.cppm), [RuntimeWorld.cppm](../RuntimeWorld.cppm), [SceneSerialization.cppm](../SceneSerialization.cppm) and the versioned scene extensions. Begin with the broader [project-to-runtime walkthrough](SCENE_RUNTIME_WALKTHROUGH.md), then inspect the functions and test their failure paths.

## 1. A Scene entity is not a GPU handle or ECS slot

KairoEngineCore::Entity is a stable scene-local identifier. Scene owns the authoritative map of entity ID to its record: name, local transform, settings, parent/children and optional components for mesh, camera, lights, audio, logic and physics.

A KairoECS Entity contains a process-local index and generation. A PhysicsWorld BodyID identifies a body in the live solver, while a Renderer MeshHandle identifies a process-local native GPU resource. These all have different ownership, invalidation and serialization rules.

**CreateEntityWithID** restores a specific ID from a serialized file. It refuses zero/duplicates and advances the allocation cursor when necessary so a subsequent CreateEntity does not reuse an imported stable ID. Saving and loading a scene must not generate new IDs based on file row order.

**Exercise:** create entities with IDs 2, 10 and 7 in arbitrary order. Verify that Scene::Entities returns sorted IDs and that restoring them preserves identity without storing a GPU handle.

## 2. Hierarchy is a directed acyclic graph

Scene::SetParent checks that child and parent are both live, forbids self-parenting and traverses proposed parent's ancestors to reject cycles. On success it updates the child's Parent and its old/new parents' sorted Children lists. Scene::DestroyEntity removes an entity **and every descendant** with an iterative traversal.

A transform is authored as **local** TRS relative to its parent. WorldTransform walks ancestry to the root, then composes transforms in root-to-leaf order. With column-vector convention, the rightmost transform applies to a point first.

**Worked hierarchy:** parent translation (3,0,0) and child local translation (0,2,0), both identity rotation and unit scale, yield child world translation (3,2,0). Reparent the child to a root with no translation: its local transform remains (0,2,0), so its *world* position changes. Preserving world position requires a specific Editor policy to adjust the local transform.

**Exercise:** construct A→B→C; attempt SetParent(A,C). The check must reject the cycle without modifying A's prior parent or any child lists. Then destroy B and verify that C no longer exists.

Nonuniform scaling combined with rotations can produce shear that pure TRS cannot represent exactly. A world matrix decomposition policy must be explicit if the Editor promises perfect preservation during arbitrary reparenting.

## 3. Enabled status and authoring visibility

An entity can have Enabled=true locally while its ancestor has Enabled=false. Scene::IsActiveInHierarchy checks **every ancestor**, so any disabled parent makes the child inactive for scene-derived render/gameplay purposes. RenderableEntities also requires an existing, visible MeshRenderer.

This traversal returns sorted scene IDs, not GPU draw order or actual camera-visible pixels. A renderer may perform additional frustum, layer and occlusion culling after resolving these authored descriptors.

**Exercise:** mark the root disabled, leave a child MeshRenderer visible, and verify the child disappears from RenderableEntities. Re-enable root without modifying the child's local flag and check it returns.

## 4. Why a deferred command queue exists

A gameplay system may iterate all scene entities while an enemy spawns, is destroyed, changes parent or receives a new tag. Modifying the authoritative map during iteration can invalidate traversal and cause inconsistent observations across systems.

RuntimeWorld therefore stores *intent commands* such as QueueSpawn, QueueDestroy, QueueSetTransform and QueueSetParent. It exposes the last **committed** Scene through Snapshot; queued changes are not visible there until Commit.

A RuntimeSpawnTicket is a request correlation ID. After committing, RuntimeCommitResult maps that ticket to the actual newly allocated EngineCore Entity. Persist the resulting Entity if necessary, **not** the ticket.

## 5. The Commit transaction is an entire Scene copy

RuntimeWorld::Commit follows this source sequence:

1. If no commands are pending, return the current revision without mutation.
2. Check that uint64 revision increment is possible.
3. Copy the current Scene into a candidate.
4. Apply every queued command against that candidate in submission order.
5. If all succeed, move the candidate into the live Scene, clear pending commands, increment revision and return spawn/destroy results.

The copy is an exception-safety decision. Suppose a queue contains Spawn(A), SetParent(A, missing ID), and Rename(B). The invalid parent operation can throw. Since all commands were applied only to the candidate, the live Scene remains unchanged; pending commands also remain for inspection, correction or DiscardPending.

**Exercise:** create an existing entity X. Queue a valid rename of X, then a deletion of a nonexistent entity. Confirm Commit fails and Snapshot still contains the original name and revision. Discard the bad batch and commit the rename alone.

**Performance consequence:** a full Scene copy happens for every nonempty batch, even if only one component changes. Benchmark large-scene commits before claiming an O(number of edited entities) update. An incremental transaction log could optimize it, but would require a careful rollback strategy and tests.

## 6. Entity destruction and adapter invalidation

When a scene entity is destroyed, its authored record and descendants vanish. The scene model does **not** automatically delete GPU mesh buffers, audio voices or PhysicsWorld bodies. KairoGameEngine's scene-to-render and scene-to-physics adapters must reconcile the changed revision and retire or rebind their process-local handles.

RuntimeWorld::Replace publishes an entirely new Scene, clears pending old-scene commands and increments revision. Any Editor component references or ECS mirror mappings based on the previous Scene must be treated as invalid until refreshed.

**Exercise:** record a Renderer object ID and Physics BodyID for an entity. Destroy it in EngineCore and explain why neither runtime handle may be reused for a different authored entity without an explicit mapping update.

## 7. Parse and save an authored scene safely

SceneSerialization.cppm parses a bounded, versioned text format. Tokens carry source line/column information so bad fields can produce actionable diagnostics. Numeric fields must parse completely with stable locale rules; path and identity references must be checked against an AssetRegistry.

ParseScene creates the scene entities in a detached candidate and records PendingParent references. Only after all IDs exist does it apply hierarchy links, allowing a child record to appear before its parent. Scene::SetParent still enforces acyclicity.

LoadScene checks file size, reads complete bytes and assigns the parsed candidate only after all validation succeeds. SaveScene writes a complete same-directory temporary file and atomically replaces the destination. These are *scene persistence* operations, not a transaction encompassing all textures, audio files, imported data and GPU memory.

**Exercise:** save A→B with components, reorder the text records, load into a fresh scene and compare IDs, hierarchy and properties. Then inject an invalid parent or type-mismatched texture reference; the prior live destination must remain unchanged.

## 8. Extension formats and undo requirements

Versioned SceneSerializationV5 and AudioScenePersistence add additional component/format coverage. A source consumer must select the appropriate schema and compatibility path; an older parser cannot silently discard newer fields.

KairoEditor's command history captures authored before/after states for Undo/Redo. **The currently inspected Editor EntitySnapshot omits AudioEmitter and AudioListener components**, so delete→undo for audio-bearing subtrees requires a separate functional fix and dedicated regression tests. Do not call this lossless restoration until those tests pass.

The Scene's persistent record and the runtime solver snapshot have different meanings. PhysicsWorld contact impulses, broadphase proxies and Renderer GPU objects do not belong in the normal authored .kscene text.

## 9. Tests and correctness checklist

Start with [RuntimeSceneCommandsTests.cpp](../tests/RuntimeSceneCommandsTests.cpp), [EngineCoreTests.cpp](../tests/EngineCoreTests.cpp), [ProjectDescriptorTests.cpp](../tests/ProjectDescriptorTests.cpp) and the audio/serialization tests indexed in [SOURCE_READING_ORDER.md](SOURCE_READING_ORDER.md).

Check stable ID restoration, hierarchy cycle rejection, subtree deletion, root-disabled visibility, failed batch atomicity, uncommitted ticket behavior, revision overflow, delayed parent links, bad AssetIDs, malformed scene parsing, deterministic serialization and safe live-adapter reconciliation.

Record exact SHA, compiler, platform, test output and whether integration included a native renderer/audio/physics host. A passing pure Scene test does not certify native Player or Editor lifecycle behavior.

*Evidence: source-learning documentation; no new native C++ build or end-to-end engine test was run for this batch.*
