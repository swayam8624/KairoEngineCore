// TEACHING ENTRY POINT: Public C++23 umbrella for engine-authored scene,
// project bootstrap, deterministic runtime state, input, time and gameplay
// contracts. It intentionally owns neither a Vulkan/Metal graphics device
// nor a PhysicsWorld: Player/Editor adapters instantiate and translate those.
//
// A typical lifecycle: ProjectDescriptor loads validated .kproject paths,
// AssetRegistry resolves persistent content IDs, Scene owns Entity/component
// records, RuntimeWorld queues state changes until commit, SceneSerialization
// persists that state, and the host feeds Application/production systems.
// KairoRenderer and PhysicsEngine consume adapted snapshots separately.
//
// `export import` republishes an API; import order is not runtime call order.
// Read docs/SOURCE_READING_ORDER.md and the matching component tests.
export module Kairo.EngineCore;
export import Kairo.Assets;
export import Kairo.EngineCore.TextValidation;
export import Kairo.EngineCore.TextFormat;
export import Kairo.EngineCore.ProjectDescriptor;
export import Kairo.EngineCore.Platform;
export import Kairo.EngineCore.Entity;
export import Kairo.EngineCore.Components;
export import Kairo.EngineCore.RuntimeComponents;
export import Kairo.EngineCore.Scene;
export import Kairo.EngineCore.RuntimeSceneCommands;
export import Kairo.EngineCore.SceneSerialization;
export import Kairo.EngineCore.SceneSerializationV5;
export import Kairo.EngineCore.SceneComposition;
export import Kairo.EngineCore.RuntimeWorld;
export import Kairo.EngineCore.WorldStreaming;
export import Kairo.EngineCore.AnimationRuntime;
export import Kairo.EngineCore.AnimationController;
export import Kairo.EngineCore.AnimationRootMotion;
export import Kairo.EngineCore.GltfSkinningRuntime;
export import Kairo.EngineCore.AudioRuntime;
export import Kairo.EngineCore.AudioSceneComponents;
export import Kairo.EngineCore.AudioScenePersistence;
export import Kairo.EngineCore.NativeGameplay;
export import Kairo.EngineCore.NativeGameplayManifest;
export import Kairo.EngineCore.ProductionSystems;
export import Kairo.EngineCore.ProductionSystemsManifest;
export import Kairo.EngineCore.ProductionRuntime;
export import Kairo.EngineCore.ShippingRuntime;
export import Kairo.EngineCore.Reflection;
export import Kairo.EngineCore.Time;
export import Kairo.EngineCore.Event;
export import Kairo.EngineCore.Input;
export import Kairo.EngineCore.InputMap;
export import Kairo.EngineCore.LogicState;
export import Kairo.EngineCore.LogicBytecode;
export import Kairo.EngineCore.LogicArtifact;
export import Kairo.EngineCore.GameplayVM;
// SaveGame is the stable archive boundary for engine- and game-owned persistent chunks.
export import Kairo.EngineCore.SaveGame;
export import Kairo.EngineCore.Layer;
export import Kairo.EngineCore.Logger;
export import Kairo.EngineCore.Diagnostics;
export import Kairo.EngineCore.JobSystem;
export import Kairo.EngineCore.Application;